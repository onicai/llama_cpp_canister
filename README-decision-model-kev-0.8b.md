# Kev-0.8B (decision model, 0.8B, English, stored state)

> **Kev-0.8B** is an English **decision model**: it answers typed questions (`choice`,
> `score`, yes/no `noul`) about a state with probabilities, without generating text.
> Base model Qwen3.5-0.8B-Base (causal, 24 layers: 18 Gated DeltaNet + 6 attention) with
> a pointer head, Apache 2.0, source
> [jaredpalmer/kev-0.8b](https://huggingface.co/jaredpalmer/kev-0.8b), GGUF
> [ggml-org/Kev-0.8B-GGUF](https://huggingface.co/ggml-org/Kev-0.8B-GGUF).
>
> Read [README-decision-models.md](README-decision-models.md) first: what decision
> models are, the `run_decision` interface, and how a request is answered.
>
> It runs on the **same `llama_cpp` canister** as the chat models. Follow the main
> [README.md](README.md) for `# Set up`, then use the steps here.

## Ingest once, ask many

Julia-1 and Laya read the whole question + options + state in one forward pass, which
must fit in one update call. Kev is different: it is causal and its prompt starts with
the state, so the state is a prefix that does not depend on the question.

`run_decision` therefore stores the state per caller:

1. The first calls of a request **ingest the state**, ~24 tokens per call, into a
   session file, exactly like `run_update` ingests a long prompt. The reply shows the
   progress: `state_tokens` and `state_tokens_remaining`.
2. Once it is stored, every call **answers questions** from it: each question decodes only
   its own tokens (instructions + options), never the state again.
3. A **later request about the same state** (other questions) starts answering right
   away: the state is still stored.
4. A **grown state** (the stored state with new data appended) continues from the
   stored one: only the new tokens are ingested.

You re-send the same request until `pending` is empty, as for the other decision models.

## Headline results (local replica, llama_cpp_canister with llama.cpp b11476)

| Measure                         | Value                                                                               |
|---------------------------------|-------------------------------------------------------------------------------------|
| gguf                            | Kev-0.8B-Q8_0.gguf, 812,406,304 bytes                                               |
| heap after `load_model`         | 1.21 GB (`wasm_heap_bytes = 1_211_826_176`), `-c 4096`, f32 KV cache                |
| instructions per state token    | ~1.4 B (1.60 B at 2,000 tokens): **24 state tokens stored per call**                |
| instructions per question token | ~1.42 B: **~22 tokens per question** without the state (~18 on a 2,000-token state) |
| stored state                    | ~20 MB + ~24 KB per state token; ~1.5 B to save, ~1-2 B to load                     |
| max state size                  | ~4,000 tokens: `-c 4096` holds the state plus one question (measured up to 2,000)   |
| one 16-token question           | ~22.8 B instructions on an 84-token state, ~26 B on a 2,000-token one               |
| `max_tokens_update`             | 0: each call stops itself before the IC's instruction limit                         |
| accuracy vs llama-server (CPU)  | within 0.013 on every probability                                                   |

## Quality

Kev-0.8B is the smallest Kev, and its model card says it is markedly less accurate than
Kev-4B: 0.697 on out-of-domain questions and 0.851 on CFPB complaint documents. Test it
on your own questions and choose confidence thresholds for it; they differ per decision
model.

## Get the gguf

```bash
mkdir -p models/ggml-org/Kev-0.8B-GGUF
```

```bash
wget -c -O models/ggml-org/Kev-0.8B-GGUF/Kev-0.8B-Q8_0.gguf https://huggingface.co/ggml-org/Kev-0.8B-GGUF/resolve/main/Kev-0.8B-Q8_0.gguf
```

Verify the sha256:

```bash
shasum -a 256 models/ggml-org/Kev-0.8B-GGUF/Kev-0.8B-Q8_0.gguf
# -> 27278f34eb3273bceea4c053dc50dd61a5161da21a718c4aacdf8fd5830771d0
```

## Upload the gguf

With the load args below, the heap stays at ~1.2 GB, also while states are stored and
questions answered. That fits within a new canister's default `wasm_memory_limit` of
3 GiB. We recommend raising it to 3.75 GiB anyway, as for the other models: it leaves
headroom for a larger `--ctx-size` or `--ubatch-size`.

```bash
icp canister settings update llama_cpp --wasm-memory-limit 4026531840 -e local
```

```bash
python -m scripts.upload --network local --canister llama_cpp --canister-filename models/model.gguf models/ggml-org/Kev-0.8B-GGUF/Kev-0.8B-Q8_0.gguf
```

## Load the model

```bash
icp canister call llama_cpp load_model '(record { args = vec {"--model"; "models/model.gguf"; "--no-warmup"; "-c"; "4096"; "-b"; "64"; "-ub"; "64"; "-fa"; "off"; "--cache-type-k"; "f32"; "--cache-type-v"; "f32"} })' -e local
# -> Ok = record { output = "Model succesfully loaded into memory."; ... }
```

- `-fa off --cache-type-k f32 --cache-type-v f32` keeps the cost of a token almost flat as
  the state grows. Measured per token at 0 / ~550 / ~2000 state tokens:

  | Attention setup                 | 0 tokens | ~550 tokens | ~2000 tokens |
  |---------------------------------|----------|-------------|--------------|
  | `-fa off`, f32 KV (recommended) | 1.42 B   | 1.45 B      | 1.60 B       |
  | `-fa off`, f16 KV               | 1.42 B   | 1.76 B      | -            |
  | flash attention on (default)    | 1.42 B   | 2.05 B      | -            |

  The f32 cache makes the stored state bigger (~24 KB per token), which is cheaper than
  the attention it saves.
- `-c 4096` sets the largest state plus question: ~4,000 state tokens. It is required:
  the default is the model's training context. Kev was trained on states up to 7,552
  tokens, but a larger `-c` does not help: beyond ~5,000 tokens (estimated) loading and
  saving the stored state no longer leave room in one call.
- `-b 64 -ub 64` keeps the per-call buffers small. A question must fit in `-ub` tokens.
- Do not pass `-np` / `--parallel`: the stored state needs one sequence.

## Set the per-call token budget

```bash
icp canister call llama_cpp set_max_tokens '(record { max_tokens_query = 0 : nat64; max_tokens_update = 0 : nat64 })' -e local
```

For Kev, `0` (no token cap) is the best setting: each call measures its own cost and
stops before the IC's 40 B instruction limit, storing as much of the state and answering
as many questions as fit. A non-zero `max_tokens_update` caps the tokens per call on top
of that. A question that cannot fit in one call, even on its own, returns `Err`.

## Ask questions

A support message of 84 tokens and two questions:

```bash
icp canister call llama_cpp run_decision '(record { state = variant { Text = "Hi, I ordered a pair of running shoes (order 88213) ten days ago. The tracking page has said '\''label created'\'' since the 3rd and nothing has moved. I need them for a race on Saturday. Can you tell me where the parcel actually is, or should I just cancel and buy somewhere else? Honestly a bit disappointed, this is my third order with you." }; questions = vec { record { id = "intent"; kind = variant { choice }; instructions = "Intent?" }; record { id = "team"; kind = variant { choice }; instructions = "Which team should handle this?" } }; options = vec { record { question_id = "intent"; key = "refund"; description = "" }; record { question_id = "intent"; key = "cancel"; description = "" }; record { question_id = "intent"; key = "track"; description = "" }; record { question_id = "intent"; key = "other"; description = "" }; record { question_id = "team"; key = "billing"; description = "" }; record { question_id = "team"; key = "shipping"; description = "" }; record { question_id = "team"; key = "technical"; description = "" }; record { question_id = "team"; key = "sales"; description = "" } } })' -e local
```

Re-send it until `pending` is empty. Verified on-chain (local replica, fresh canister),
cycles per call measured with `icp canister status`:

| Call | `state_tokens_remaining` | `input_tokens` | Answered               | Cycles |
|------|--------------------------|----------------|------------------------|--------|
| 1    | 60                       | 24             | -                      | 35.5 B |
| 2    | 36                       | 24             | -                      | 35.5 B |
| 3    | 12                       | 24             | -                      | 35.7 B |
| 4    | 0                        | 12             | -                      | 19.2 B |
| 5    | 0                        | 16             | intent: other (0.435)  | 23.6 B |
| 6    | 0                        | 20             | team: shipping (0.628) | 29.2 B |

Each call stores 24 state tokens (three steps of 8), then stops: a fourth step would pass
the instruction limit. Call 4 stores the last 12 tokens; `intent` does not fit after them,
so it is answered by call 5. The canister log of call 5:

```
llama_cpp: run_decision - state: loaded 84/84 tokens from .canister_cache/<principal>/sessions/decision-state-5987ddcaa12bc988.session
llama_cpp: run_decision - intent: 16 tokens after the state, 22.79 B instructions (1.42 B per token), scores [-1.67, 2.54, 2.26, 3.61], softmax T=2.35
llama_cpp: run_decision - intent (choice, 16 tokens) -> other (p=0.43, confidence=0.25)
```

| Question | Canister       | llama-server (CPU) |
|----------|----------------|--------------------|
| intent   | other 0.435    | other 0.425        |
| team     | shipping 0.628 | shipping 0.632     |

A new request about the same state, e.g. only `intent`, is now answered in one call of 16
tokens: the state is still stored.

## Growing states: append, then ask again

A state that grows as data arrives (a trading log with a new day, a game's move list after
each move, a conversation with a new message) does not have to be ingested again. When a
request's state is not stored yet, `run_decision` looks for the caller's stored state with
the longest token list that the new state starts with, loads it, and ingests only the
rest. The older state stays stored, so requests about it keep working.

Verified natively (`test_decision_kev`): the 84-token state above, with
`"\nUpdate: the parcel arrived today, all good now."` appended (96 tokens), is answered in
one call: 12 new tokens + the 16-token question. The answer equals the one of the same
state ingested from scratch, within 0.01.

```
llama_cpp: run_decision - state: loaded 84/96 tokens from .canister_cache/<principal>/sessions/decision-state-<hash>.session
llama_cpp: run_decision - state: continues the stored state .canister_cache/<principal>/sessions/decision-state-<hash>.session
llama_cpp: run_decision - state: tokens 84..88 of 96
llama_cpp: run_decision - state: tokens 88..96 of 96
```

To make it work:

- **Append, never rewrite.** The new state must start with exactly the old one: for a
  `Text` state, the old text plus the new text; for a `Json` state, new keys or items
  at the end (the key order is kept).
- **Start the new data with a newline,** so the tokens at the old end do not change. If
  they do, the state is ingested from the start: correct, only slower.
- **Send what grows, not a snapshot.** A chess game as its move list grows; a board
  position (FEN) changes in the middle, so nothing can be reused.
- The state stays limited to ~4,000 tokens (`-c 4096`). For a log that keeps growing, start a new
  state from a recent window now and then.

## What it's good (and not good) for

- **Good:** decisions about a long state: a support ticket with its history, a document,
  an agent's context, asked several questions. The state is processed once per caller,
  and every question then costs one call or less.
- **Good:** many requests about the same state, e.g. a moderation or triage policy asked
  in steps, or new questions as a conversation goes on.
- **Good:** a state that grows by appending: each update only costs its new tokens.
- **Not good:** one long question: instructions + options must fit in ~22 tokens (keep
  option keys short; descriptions add tokens). A question that cannot fit returns `Err`.
- **Not good:** the most accurate answers on short inputs: use
  [Laya](README-decision-model-Laya.md) there.
- **Storage:** each stored state is ~20 MB + ~24 KB per state token, per caller, and a
  caller keeps at most 8 (the oldest is removed first).
- **Optional, expiry by age:** stored states are prompt-cache files, so the
  [prompt-cache cleanup timer](README.md#prompt-cache-cleanup-timer) can also remove
  them. It is off by default; start it with `cache_cleanup_start_timer` (again after
  every upgrade), and set its TTL with `set_cache_cleanup_config` (default 6 h). It then
  deletes a state whose last ingestion is older than the TTL; answering questions does
  not refresh that time. A removed state is ingested again on the next request about it.
