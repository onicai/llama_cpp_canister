# Decision models (System One) on llama_cpp_canister

> **Status: research and planning. Not yet available in a release.**
> Running decision models needs the llama.cpp upgrade (upgrade 0004) and a new
> `run_decision` endpoint. This document describes the technology, what
> upstream llama.cpp ships, which models fit in a canister, and the design.
> The "Running on the IC" section gets filled in with measured results as the
> work lands.
>
> Step-by-step model guides (written once verified on-chain):
> - [README-decision-model-julia-1.md](README-decision-model-julia-1.md): Julia-1, 144M, 50+ languages
> - [README-decision-model-Laya.md](README-decision-model-Laya.md): Laya, 421M, English

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

All five are published as pre-converted GGUFs under `ggml-org`:

| Model   | Size | Base             | Decision type | Languages              | Images | License      | GGUF                                  |
|---------|------|------------------|---------------|------------------------|--------|--------------|---------------------------------------|
| Julia-1 | 144M | mmBERT-small     | `laya`        | 50+                    | no     | Apache 2.0   | `ggml-org/Julia-1-GGUF`: Q8_0 168 MB  |
| Laya    | 421M | ModernBERT-large | `laya`        | English                | no     | Apache 2.0   | `ggml-org/Laya-GGUF`: Q8_0 449 MB     |
| Kev-4B  | 4B   | Qwen3.5-4B-Base  | `kev`         | English                | no     | Apache 2.0   | `ggml-org/Kev-4B-GGUF`: Q4_K_M 3.0 GB |
| lev     | 4B   | Qwen3.5-4B       | `lev`         | English                | no     | Apache 2.0   | `ggml-org/lev-GGUF`: Q4_K_M 3.0 GB    |
| OpenJev | 27B  | Qwen3.8-27B      | `openjev`     | en, de, fr, hi, zh, ja | yes    | CC BY-NC 4.0 | `ggml-org/OpenJev-GGUF`: Q4_K_M 19 GB |

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

| Model   | Fit          | Why                                                                                  |
|---------|--------------|--------------------------------------------------------------------------------------|
| Julia-1 | yes, primary | 168 MB; small encoder, cheap single forward pass                                     |
| Laya    | yes          | 449 MB; larger encoder, so a lower max state size per call                           |
| Kev-4B  | no           | 3.0 GB gguf: over the ~2 GiB a single message can read from stable memory (`IC0524`) |
| lev     | no           | same as Kev-4B                                                                       |
| OpenJev | no           | 19 GB                                                                                |

Kev-4B and lev would also be too slow even if they loaded: LFM2.5-2.6B already
manages only 4 tokens per call ([README-LFM2.5-2.6B.md](README-LFM2.5-2.6B.md)),
and a decision prompt is several hundred tokens.

Julia-1 and Laya share the `laya` decision type, so one code path in the
canister serves both.

## The main constraint: one question, one call

The canister normally processes a long prompt across several update calls. Each
call ingests a chunk and saves the KV cache in the prompt cache file, and the
next call continues from there.

An encoder cannot do that. Attention is bidirectional and there is no KV cache:
the whole sequence of a question goes through `llama_decode` in one batch, so a
question must fit within **one update call's instruction limit** (40 B
instructions). This sets the maximum state size per question, and it will be
measured for Julia-1 and Laya before the endpoint is finalized.

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
template exactly like `llama-server` does.

One related rule stays: `params.use_jinja` remains `false` on the text
generation path. Upstream computes
`add_bos = llama_vocab_get_add_bos(vocab) && !params.use_jinja`, so turning it
on would change tokenization of the existing chat models. The decision path
calls the Jinja runtime directly and does not use that flag.

## Design for the canister

- **Load** with the existing `load_model`. The GGUF metadata switches the
  context to embedding mode; no extra flags are needed.
- **Ask** with a new update endpoint `run_decision`, with a typed Candid
  interface. Access rules are the same as `run_update`.

```candid
type DecisionState = variant { Text : text; Json : text };
type DecisionQuestion = record {
  id : text;
  instructions : text;
  kind : variant {
    choice : vec record { key : text; description : opt text };
    score  : vec text;
    noul   : record { yes : opt text; no : opt text };
  };
};
type DecisionProb = record { key : text; probability : float64 };
type DecisionAnswer = record {
  id : text;
  answer : variant {
    choice : record { choice : text; confidence : float64; probabilities : vec DecisionProb };
    score  : record { score : float64; confidence : float64; probabilities : vec DecisionProb };
    noul   : record { yes : float64 };
  };
};
type DecisionResult = variant {
  Ok  : record { answers : vec DecisionAnswer; input_tokens : nat64 };
  Err : ApiError;
};
run_decision : (record { state : DecisionState; questions : vec DecisionQuestion }) -> (DecisionResult);
```

- **Implementation:** `src/decision.cpp` is a port of upstream
  `server-decision.cpp`, keeping its structure and function names so future
  llama.cpp upgrades can diff against it. Scope is the `laya` type: no images,
  no shared-prefix batching.
- **Calling the wrong endpoint** gives a clear error: `run_update` on a
  decision model, or `run_decision` on a chat model.

## Running on the IC

_To be filled in with measured results: instructions per question, max state
size per call, cycles per call, heap after load, and the probabilities compared
with native `llama-server` on the same llama.cpp commit._

## Sources

- [Georgi Gerganov on X: "Decision models in llama.cpp are now available"](https://x.com/ggerganov/status/2106029758350032937)
- [HF blog: New in llama.cpp: Decision Models](https://huggingface.co/blog/ggml-org/decision-models-in-llamacpp)
- [llama.cpp PR #29818](https://github.com/ggml-org/llama.cpp/pull/29818)
- [llama.cpp server README](https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md)
- [ggml-org/Julia-1-GGUF](https://huggingface.co/ggml-org/Julia-1-GGUF), [ggml-org/Laya-GGUF](https://huggingface.co/ggml-org/Laya-GGUF), [ggml-org/Kev-4B-GGUF](https://huggingface.co/ggml-org/Kev-4B-GGUF), [ggml-org/lev-GGUF](https://huggingface.co/ggml-org/lev-GGUF), [ggml-org/OpenJev-GGUF](https://huggingface.co/ggml-org/OpenJev-GGUF)
- [explainx.ai: llama.cpp Decision Models](https://explainx.ai/blog/llama-cpp-decision-model-support-2026)
- [DEV Community: Run Local AI Decision Models with llama.cpp](https://dev.to/koolkamalkishor/run-local-ai-decision-models-with-llamacpp-for-fast-classification-2im3)
