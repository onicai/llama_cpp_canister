# Decision models (System One) on llama_cpp_canister

> **Status: available since llama_cpp_canister v0.20.0** (llama.cpp b11476, `run_decision`
> endpoint), verified on a local replica with Julia-1 and Laya. **Kev-0.8B** (the state,
> up to ~4,000 tokens, is stored over several calls; each question must still fit in one
> call) since v0.21.0. This document describes the technology, what
> upstream llama.cpp ships, the canister design, and the measured results.
>
> Step-by-step model guides:
> - [README-decision-model-julia-1.md](README-decision-model-julia-1.md): Julia-1, 144M, 50+ languages, ~210 tokens per question
> - [README-decision-model-Laya.md](README-decision-model-Laya.md): Laya, 421M, English, 33 tokens per question
> - [README-decision-model-kev-0.8b.md](README-decision-model-kev-0.8b.md): Kev-0.8B, 0.8B, English, the state is stored once and every question costs only its own tokens

## What a decision model is

A decision model answers typed questions about a **state** by **scoring the
options you give it**, instead of generating text. It reads the input in one
forward pass and returns one of your options with a probability attached.

The format comes from TypeSafe's **Jev** model and its **System One** API.
A chat LLM in JSON mode needs one forward pass per output token, plus parsing,
plus retries when the JSON is malformed. A decision model needs one forward pass
and never produces malformed output: the answer is always one of the options.
The response reports `output_tokens: 0`.

Typical uses: routing a request, intent detection, moderation, checking an
agent's work, choosing an agent's next step, scoring a response.

For an on-chain AI agent this is a strong fit: the canister needs a decision,
not prose, and a single forward pass with no generation loop fits the IC's
per-call instruction budget far better than token-by-token generation.

## The System One API

A request has a `state` and one or more named `questions`:

```json
{
  "state": {
    "message": "Hi, I was charged twice for my order #4471 and I want a refund.",
    "plan": "pro",
    "order": { "id": 4471, "items": ["phone case", "charger"] }
  },
  "questions": {
    "intent": {
      "type": "choice",
      "instructions": "What does the customer want?",
      "criteria": {
        "refund": "wants money back",
        "cancel": "wants to cancel an order",
        "track": "wants to know where an order is",
        "other": "anything else"
      }
    },
    "urgent": {
      "type": "noul",
      "instructions": "Does this need a human within the hour?"
    },
    "frustration": {
      "type": "score",
      "instructions": "How frustrated is the customer?",
      "criteria": ["calm", "mildly annoyed", "annoyed", "angry"]
    }
  }
}
```

The `state` can be plain text or any JSON value. Three question types:

| Type     | `criteria`                                                  | Answer                                                                     |
|----------|-------------------------------------------------------------|----------------------------------------------------------------------------|
| `choice` | object of `option: description` (description may be `null`) | `choice` (top option), `probabilities`, `confidence`                       |
| `score`  | array of 2 to 10 ordered levels                             | `score` (expected level, can be fractional), `probabilities`, `confidence` |
| `noul`   | optional `{"true": ..., "false": ...}`                      | `noul`: the probability of yes                                             |

The response:

```json
{
  "model": "...",
  "answers": {
    "intent":      { "type": "choice", "choice": "refund", "probabilities": { "refund": 0.99, "...": 0.0 }, "confidence": 0.98 },
    "urgent":      { "type": "noul", "noul": 0.10 },
    "frustration": { "type": "score", "score": 1.4, "legend": { "...": "..." }, "probabilities": { "...": 0.0 }, "confidence": 0.5 }
  },
  "usage": { "input_tokens": 414, "output_tokens": 0 }
}
```

## What upstream llama.cpp ships

Support landed in [PR #29818](https://github.com/ggml-org/llama.cpp/pull/29818),
"llama, server: add /v1/systemone API (models: laya, julia-1, lev, openjev,
kev)", merged on 2026-10-02 as `a4cb4c61`. `llama-server` serves it as
`POST /v1/systemone`.

The PR describes decision models as "fancy wrappers around traditional
embedding models (BERT/Qwen/etc)". The changes, by component:

| Component  | Change                                                                                                                                                                  |
|------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Conversion | converts the HF models; writes `{arch}.decision.*` metadata, extra tensors and a `systemone` chat template into the GGUF                                                |
| libllama   | a decision head in `src/models/modern-bert.cpp`, an optional `cls_out` projection in `src/models/qwen35.cpp`, the `mmbert` pre-tokenizer, `{arch}.decision.block_count` |
| common     | `common_get_decision_type()`; `common_init_result` switches `laya` and `kev` models to embedding mode with pooling `NONE` automatically                                 |
| Server     | `tools/server/server-decision.cpp` (681 lines): request parsing, prompt rendering, scoring, the answer format; `/v1/systemone`                                          |

Most of the logic is in the server, deliberately, to keep `libllama` changes
small.

### How a request is answered

For each question, the server:

1. **Renders a prompt** from the state, the question and its options with the
   **Jinja template named `systemone`** stored in the GGUF.
2. **Tokenizes** it and finds where to read each option's score.
3. Runs **one forward pass**.
4. **Reads one raw score per option.** Where it reads them depends on the
   model's decision type (below).
5. Applies a **softmax with a per-model temperature** (from
   `{arch}.decision.temperature.*` metadata, bucketed by the number of options)
   and computes the **confidence** with TypeSafe's published formulas.

There are four decision types, stored as `{arch}.decision.type`:

| Type      | Base       | Where the score of an option is read                                                                   |
|-----------|------------|--------------------------------------------------------------------------------------------------------|
| `laya`    | ModernBERT | the embeddings output at a `[MASK]` marker token in front of each option; one column per question type |
| `kev`     | Qwen3.5    | scaled dot product of the hidden state of the last token and of a `<\|box_end\|>` token per option     |
| `lev`     | Qwen3.5    | the logits of one label token per option (`A`, `B`, ...), read at the last prompt token                |
| `openjev` | Qwen3.8    | same as `lev`; also accepts images                                                                     |

**The `laya` type** is the one that matters for the canister, because both
small models use it. Its prompt layout is
`[CLS] question [SEP] ([MASK] option)* [SEP] state [SEP]`. The model is a
bidirectional encoder: every token sees every other token, so the whole
sequence goes through in **one batch**. The decision head is a few extra
pre-norm transformer blocks at the end of the model. It runs once per question
type (a learned type embedding is added to the input) and outputs three scores
per token. The score for a question is column `choice`, `score` or `noul` of
the output at each `[MASK]` marker.

### The models

All six are published as pre-converted GGUFs under `ggml-org`:

| Model    | Size | Base              | Decision type | Languages              | Images | License      | GGUF                                  |
|----------|------|-------------------|---------------|------------------------|--------|--------------|---------------------------------------|
| Julia-1  | 144M | mmBERT-small      | `laya`        | 50+                    | no     | Apache 2.0   | `ggml-org/Julia-1-GGUF`: Q8_0 168 MB  |
| Laya     | 421M | ModernBERT-large  | `laya`        | English                | no     | Apache 2.0   | `ggml-org/Laya-GGUF`: Q8_0 449 MB     |
| Kev-0.8B | 0.8B | Qwen3.5-0.8B-Base | `kev`         | English                | no     | Apache 2.0   | `ggml-org/Kev-0.8B-GGUF`: Q8_0 812 MB |
| Kev-4B   | 4B   | Qwen3.5-4B-Base   | `kev`         | English                | no     | Apache 2.0   | `ggml-org/Kev-4B-GGUF`: Q4_K_M 3.0 GB |
| lev      | 4B   | Qwen3.5-4B        | `lev`         | English                | no     | Apache 2.0   | `ggml-org/lev-GGUF`: Q4_K_M 3.0 GB    |
| OpenJev  | 27B  | Qwen3.8-27B       | `openjev`     | en, de, fr, hi, zh, ja | yes    | CC BY-NC 4.0 | `ggml-org/OpenJev-GGUF`: Q4_K_M 19 GB |

Upstream also publishes `ggml-org/tinylaya-for-testing-gguf` (97 MB), a slice
of Laya used by its tests. It goes through the same `laya` code path, so we use
it for the native unit tests.

The PR's reference results show llama.cpp matches the original implementations
closely. For the sample request above, the largest difference on any
probability is 8.4e-4 for Laya and 2.2e-2 for Julia-1.

The models are calibrated differently: the HF blog notes that a vague ticket
scored 0.25 with Julia-1 and 0.80 with Kev-4B. Confidence thresholds have to
be chosen per model.

## Which models fit in a canister

| Model    | Fit               | Why                                                                                                          |
|----------|-------------------|--------------------------------------------------------------------------------------------------------------|
| Julia-1  | yes, primary      | 168 MB; ~170-180 M instructions per token: ~210 tokens per question per call                                 |
| Laya     | yes, compact      | 449 MB; ~1.2 B instructions per token: 33 tokens per question per call                                       |
| Kev-0.8B | yes, stored state | 812 MB; ~1.4 B instructions per token: the state is stored ~24 tokens per call, then ~22 tokens per question |
| Kev-4B   | no                | 3.0 GB gguf: over the ~2 GiB a single message can read from stable memory (`IC0524`)                         |
| lev      | no                | same as Kev-4B                                                                                               |
| OpenJev  | no                | 19 GB                                                                                                        |

Kev-4B and lev would also be too slow even if they loaded: LFM2.5-2.6B already
manages only 4 tokens per call ([README-LFM2.5-2.6B.md](README-LFM2.5-2.6B.md)),
and a decision prompt is several hundred tokens.

Julia-1 and Laya share the `laya` decision type, so one code path in the
canister serves both. Kev-0.8B is the only `kev` model small enough; the other
Kev models are 3 GB and up.

## The main constraint: one question, one call

The canister normally processes a long prompt across several update calls. Each
call ingests a chunk and saves the KV cache in the prompt cache file, and the
next call continues from there.

An encoder cannot do that. Attention is bidirectional and there is no KV cache:
the whole sequence of a question goes through `llama_decode` in one batch, so a
question must fit within **one update call's instruction limit** (40 B
instructions). This sets the maximum size of a question (instructions + options +
state): measured ~210 tokens for Julia-1 and 33 tokens for Laya.

A request with several questions is still fine: `run_decision` answers as many as fit in
one call and returns the rest as `pending` (see below).

**Kev-0.8B lifts this constraint for the state.** Kev is causal (Qwen3.5) and its prompt
starts with the state, so the state is a prefix that does not depend on the question.
`run_decision` ingests it over as many calls as needed into a per-caller session file,
exactly like `run_update` ingests a long prompt, and every question then decodes only its
own tokens on top of the stored state. A later request about the same state skips the
ingestion, and a grown state (new data appended) continues from the stored one. Only one question (instructions + options, without the state) must fit in one
call: ~22 tokens on a short state (~18 on a 2,000-token state). Each call measures its own cost and stops before the
instruction limit, so a long state never traps.

The batch and micro-batch size (`--batch-size`, `--ubatch-size`) must be at
least the length of the longest question's sequence.

## Jinja is not a blocker

The canister build used to exclude `common/jinja/*` and `common/chat*.cpp`
"for the ICP globals limit" (see `icpp.toml` and
[README-0003-305ba519.md](README-0003-305ba519.md)). That exclusion was made as
a precaution early in upgrade 0003. The same upgrade log shows that the
post-link step removing exported globals and running the Binaryen optimizer
brings the module from 1046 defined globals to 1. Since icpp-pro 6.2.0 that
step is built into `icpp build-wasm`.

So the canister compiles the Jinja engine and renders the `systemone`
template exactly like `llama-server` does (verified: the same token counts).

One canister-specific detail: the template is parsed with `jinja::lexer` +
`jinja::parse_from_tokens`, NOT with `common_chat_template`. That constructor runs a
capability analysis that executes the template with dummy chat inputs and relies on
catching the resulting exception, and in a canister every C++ throw traps.

One related rule stays: `params.use_jinja` remains `false` on the text
generation path. Upstream computes
`add_bos = llama_vocab_get_add_bos(vocab) && !params.use_jinja`, so turning it
on would change tokenization of the existing chat models. The decision path
calls the Jinja runtime directly and does not use that flag.

## Design for the canister

- **Load** with the existing `load_model`. The GGUF metadata switches the context to
  embedding mode; no extra flags are needed. On a decision model, `run_update`,
  `run_query` and `new_chat` return `Err` ("use run_decision").
- **Ask** with the update endpoint `run_decision`. Access rules are the same as
  `run_update`.
- **Interface: flat typed Candid.** icpp-pro decodes a `vec record` as a record of flat
  vectors (no nested `vec record`, no `vec opt`, only unit variants inside a `vec`), so
  questions, options, answers and probabilities are flat tables linked by question id:

```candid
type DecisionKind = variant { choice; score; noul };
type DecisionInputRecord = record {
  state : variant { Text : text; Json : text };
  questions : vec record { id : text; kind : DecisionKind; instructions : text };
  options : vec record { question_id : text; key : text; description : text };
};
type DecisionOutputRecord = record {
  answers : vec record { id : text; kind : DecisionKind; choice : text;
                         score : float64; yes : float64; confidence : float64 };
  probabilities : vec record { question_id : text; key : text; probability : float64 };
  input_tokens : nat64;
  pending : vec text;
  state_tokens : opt nat64;            // kev only: the state's size in tokens
  state_tokens_remaining : opt nat64;  // kev only: not stored yet (0 = stored)
};
type DecisionResult = variant { Err : ApiError; Ok : DecisionOutputRecord };
run_decision : (DecisionInputRecord) -> (DecisionResult);
```

  - `choice`: one option row per option (key + optional description).
  - `score`: one row per level, in order (2 to 10 levels); `score` is the expected level
    index.
  - `noul`: optional rows with key `true` / `false` (descriptions); the answer is `yes`,
    the probability of yes.
- **Resume across calls**, like `run_update` ingesting a long prompt: each call answers
  the pending questions that fit in `max_tokens_update` tokens (`set_max_tokens`), in
  request order, and returns the answers so far plus the `pending` ids. Re-send the same
  request until `pending` is empty. The answers so far are kept in a per-principal file
  in `.canister_cache/<principal>/sessions/`, keyed by the request AND the loaded model,
  so the cache cleanup timer covers them and another model never reuses them.
- **Stored state (kev):** the state is ingested in chunks of up to `--batch-size` tokens,
  within the same `max_tokens_update` budget, into
  `.canister_cache/<principal>/sessions/decision-state-<hash>.session` (the hash covers the
  model, the context layout and the state tokens). Each question re-loads that checkpoint
  (~0.3 B instructions), because a recurrent memory cannot be rolled back, and the file is
  never written by a question. A state that is not stored yet continues from the caller's
  stored state with the longest token list it starts with (a grown state). Each call measures its own instructions and stops before
  the IC limit, leaving the rest `pending`. A file is ~20 MB + ~24 KB per state token
  (f32 KV cache, recommended with `-fa off`, which keeps the cost per token almost flat
  as the state grows); the cache
  cleanup timer removes it 6 h after it was stored, and a principal keeps at most 8.
- **Implementation:** `src/decision.cpp` ports the laya and kev paths of upstream
  `tools/server/server-decision.cpp` (b11476), keeping its function names so future
  llama.cpp upgrades can diff against it. Every input check returns an error instead of
  throwing (a throw traps the canister). Left out: images and the other decision types.
  One fork patch (`ICPP-PATCH` in `src/models/qwen35.cpp`): a kev model skips the LM head,
  which it never reads, which makes each question token ~1/3 cheaper.
- **Logging:** one line per answered question in the canister log, e.g.
  `llama_cpp: run_decision - intent (choice, 87 tokens) -> refund (p=0.98, confidence=0.98)`.

## Running on the IC

Measured on a local replica (same 40 B instruction limit per update call as mainnet):

| Measure                 | Julia-1                                                                   | Laya                      | Kev-0.8B                                      |
|-------------------------|---------------------------------------------------------------------------|---------------------------|-----------------------------------------------|
| gguf (Q8_0)             | 168 MB                                                                    | 449 MB                    | 812 MB                                        |
| heap after `load_model` | 316 MB                                                                    | 686 MB                    | 1.21 GB (`-c 4096`, f32 KV)                   |
| instructions per token  | ~170-180 M                                                                | ~1.2 B                    | ~1.4 B (1.60 B at 2,000 state tokens)         |
| max tokens per question | ~210                                                                      | 33                        | ~22, without the state                        |
| max state size          | within the question                                                       | within the question       | ~4,000 tokens (`-c 4096`), 24 stored per call |
| `max_tokens_update`     | 200                                                                       | 32                        | 0: each call stops itself before the limit    |
| example question        | 87 tokens, ~14.9 B cycles                                                 | 29 tokens, ~34.5 B cycles | 16 tokens on a stored state, ~22.8 B cycles   |
| vs llama-server (CPU)   | clear decisions agree; close calls can differ (Q8_0 rounding sensitivity) | within 0.011              | within 0.013                                  |

Port correctness was verified natively: the canister code with Laya matches llama-server
on the same commit to 0.006 on every probability of the PR #29818 request, with the same
414 tokens. Julia-1's larger differences come from the model's sensitivity to kernel
rounding: llama-server itself answers some of its questions differently on CPU and Metal.

## Sources

- [Georgi Gerganov on X: "Decision models in llama.cpp are now available"](https://x.com/ggerganov/status/2106029758350032937)
- [HF blog: New in llama.cpp: Decision Models](https://huggingface.co/blog/ggml-org/decision-models-in-llamacpp)
- [llama.cpp PR #29818](https://github.com/ggml-org/llama.cpp/pull/29818)
- [llama.cpp server README](https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md)
- [ggml-org/Julia-1-GGUF](https://huggingface.co/ggml-org/Julia-1-GGUF), [ggml-org/Laya-GGUF](https://huggingface.co/ggml-org/Laya-GGUF), [ggml-org/Kev-0.8B-GGUF](https://huggingface.co/ggml-org/Kev-0.8B-GGUF), [ggml-org/Kev-4B-GGUF](https://huggingface.co/ggml-org/Kev-4B-GGUF), [ggml-org/lev-GGUF](https://huggingface.co/ggml-org/lev-GGUF), [ggml-org/OpenJev-GGUF](https://huggingface.co/ggml-org/OpenJev-GGUF)
- [explainx.ai: llama.cpp Decision Models](https://explainx.ai/blog/llama-cpp-decision-model-support-2026)
- [DEV Community: Run Local AI Decision Models with llama.cpp](https://dev.to/koolkamalkishor/run-local-ai-decision-models-with-llamacpp-for-fast-classification-2im3)
