# Laya (decision model, 421M, English)

> **Laya** is an English **decision model**: it answers typed questions (`choice`,
> `score`, yes/no `noul`) about a state with probabilities, in one forward pass, without
> generating text. Base model ModernBERT-large (30 layers x 1024), Apache 2.0, source
> [convaiinnovations/laya](https://huggingface.co/convaiinnovations/laya), GGUF
> [ggml-org/Laya-GGUF](https://huggingface.co/ggml-org/Laya-GGUF).
>
> Read [README-decision-models.md](README-decision-models.md) first: what decision
> models are, the `run_decision` interface, and how a request is answered.
>
> It runs on the **same `llama_cpp` canister** as the chat models. Follow the main
> [README.md](README.md) for `# Set up`, then use the steps here.

## Headline results (local replica, llama_cpp_canister with llama.cpp b11476)

| Measure                         | Value                                                 |
|---------------------------------|-------------------------------------------------------|
| gguf                            | Laya-Q8_0.gguf, 449,397,600 bytes                     |
| heap after `load_model`         | 686 MB (`wasm_heap_bytes = 686_227_456`)              |
| instructions per token          | ~1.2 B (about 7x Julia-1)                             |
| max tokens per question         | **33 per update call** (34 exceeds 40 B instructions) |
| one 29-token question           | ~34.5 B cycles on the local replica                   |
| recommended `max_tokens_update` | 32 (the per-call token budget of `run_decision`)      |
| accuracy vs llama-server (CPU)  | within 0.011 on every probability                     |

Laya is numerically stable: llama-server reproduces the PR #29818 reference table to
0.004, and the canister matches llama-server to 0.011.

## Prompts must be compact

Each question is one forward pass over question + options + state, and it must fit in 33
tokens. Laya's template spends tokens on structure, so write tight questions:

| Question                                                                                           | Tokens |
|----------------------------------------------------------------------------------------------------|--------|
| `choice` "Intent?" refund/cancel/track/other, state "Charged twice for order 4471, refund please!" | 29     |
| `noul` "Urgent?" (true: "urgent", false: "can wait"), same state                                   | 32     |
| `score` "Mood?" calm/angry, same state                                                             | 31     |
| `score` "Mood?" calm/annoyed/angry, same state                                                     | > 33   |

- Score levels render as `level 0: calm`: about 4 tokens per level.
- A `noul` without descriptions renders as "no, the statement does not hold" / "yes, the
  statement holds": give short descriptions instead.
- Keep the state to the essential sentence. For longer states, use
  [Kev-0.8B](README-decision-model-kev-0.8b.md): it stores the state over several calls.

## Get the gguf

```bash
mkdir -p models/ggml-org/Laya-GGUF
```

```bash
wget -c -O models/ggml-org/Laya-GGUF/Laya-Q8_0.gguf https://huggingface.co/ggml-org/Laya-GGUF/resolve/main/Laya-Q8_0.gguf
```

Verify the sha256:

```bash
shasum -a 256 models/ggml-org/Laya-GGUF/Laya-Q8_0.gguf
# -> c06528c5746d3bb8baa72a27938be95abbfd0b226f8471e8a9e365ed0bb066d2
```

## Upload the gguf

```bash
python -m scripts.upload --network local --canister llama_cpp --canister-filename models/model.gguf models/ggml-org/Laya-GGUF/Laya-Q8_0.gguf
```

## Load the model

```bash
icp canister call llama_cpp load_model '(record { args = vec {"--model"; "models/model.gguf"; "--no-warmup"; "-c"; "2048"; "-b"; "2048"; "-ub"; "2048"} })' -e local
# -> Ok = record { output = "Model succesfully loaded into memory."; ... }
```

## Set the per-call token budget

```bash
icp canister call llama_cpp set_max_tokens '(record { max_tokens_query = 32 : nat64; max_tokens_update = 32 : nat64 })' -e local
```

With a budget of 32, every call answers one question. A question longer than 32 tokens
returns `Err` with its token count.

## Ask a question

```bash
icp canister call llama_cpp run_decision '(record { state = variant { Text = "Charged twice for order 4471, refund please!" }; questions = vec { record { id = "intent"; kind = variant { choice }; instructions = "Intent?" } }; options = vec { record { question_id = "intent"; key = "refund"; description = "" }; record { question_id = "intent"; key = "cancel"; description = "" }; record { question_id = "intent"; key = "track"; description = "" }; record { question_id = "intent"; key = "other"; description = "" } } })' -e local
```

Verified on-chain output (local replica), and the canister log:

```
answers = vec { record { id = "intent"; kind = variant { choice }; choice = "refund"; confidence = 0.93; ... } };
input_tokens = 29; pending = vec {};

llama_cpp: run_decision - intent (choice, 29 tokens) -> refund (p=0.95, confidence=0.93)
```

The three compact questions above, sent as one request, are answered in 3 update calls
(29 + 32 + 31 tokens):

| Question    | Canister     | llama-server (CPU) |
|-------------|--------------|--------------------|
| intent      | refund 0.951 | refund 0.962       |
| urgent      | p(yes) 0.907 | p(yes) 0.906       |
| frustration | angry 0.9905 | angry 0.9908       |

## What it's good (and not good) for

- **Good:** precise English decisions on very short inputs: a chat line, a command, a
  title, a one-sentence agent output. Best accuracy of the two on-chain decision models.
- **Not good:** anything longer than ~33 tokens per question, including most real
  messages with context. For longer inputs, use [Julia-1](README-decision-model-julia-1.md)
  (~210 tokens per question) or [Kev-0.8B](README-decision-model-kev-0.8b.md): its state, up
  to ~4,000 tokens, is stored over several calls, while each question (instructions +
  options) must still fit in one call.
