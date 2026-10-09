# Julia-1 (decision model, 144M, 50+ languages)

> **Julia-1** is a small, multilingual **decision model**: it answers typed questions
> (`choice`, `score`, yes/no `noul`) about a state with probabilities, in one forward
> pass, without generating text. Base model mmBERT-small (24 layers x 384), Apache 2.0,
> source [SupersonicLabs/Julia-1](https://huggingface.co/SupersonicLabs/Julia-1),
> GGUF [ggml-org/Julia-1-GGUF](https://huggingface.co/ggml-org/Julia-1-GGUF).
>
> Read [README-decision-models.md](README-decision-models.md) first: what decision
> models are, the `run_decision` interface, and how a request is answered.
>
> It runs on the **same `llama_cpp` canister** as the chat models. Follow the main
> [README.md](README.md) for `# Set up` (icp-cli, Python env, build, deploy, cycles),
> then use the steps here.

## Headline results (local replica, llama_cpp_canister with llama.cpp b11476)

| Measure                         | Value                                                     |
|---------------------------------|-----------------------------------------------------------|
| gguf                            | Julia-1-Q8_0.gguf, 168,166,496 bytes                      |
| heap after `load_model`         | 316 MB (`wasm_heap_bytes = 316_276_736`)                  |
| instructions per token          | ~170-180 M                                                |
| max tokens per question         | ~210 per update call (209 tokens = 37.6 B instructions)   |
| one 87-token question           | ~14.9 B instructions, ~14.9 B cycles on the local replica |
| recommended `max_tokens_update` | 200 (the per-call token budget of `run_decision`)         |

The 5-question request of llama.cpp PR #29818 (396 tokens) is answered in 3 update
calls (161 + 152 + 83 tokens) with a budget of 200.

## Get the gguf

```bash
mkdir -p models/ggml-org/Julia-1-GGUF
```

```bash
wget -c -O models/ggml-org/Julia-1-GGUF/Julia-1-Q8_0.gguf https://huggingface.co/ggml-org/Julia-1-GGUF/resolve/main/Julia-1-Q8_0.gguf
```

Verify the sha256:

```bash
shasum -a 256 models/ggml-org/Julia-1-GGUF/Julia-1-Q8_0.gguf
# -> 1ea6a7e87156eeeda88cb7a36a61265b37ba7b993897b7289b99aea5b5e47069
```

## Upload the gguf

```bash
python -m scripts.upload --network local --canister llama_cpp --canister-filename models/model.gguf models/ggml-org/Julia-1-GGUF/Julia-1-Q8_0.gguf
```

## Load the model

The model is a bidirectional encoder: the whole prompt of a question is evaluated in one
batch, so `--batch-size` and `--ubatch-size` must be at least the longest question
(2048 is plenty). No KV cache is allocated. `--no-warmup` as for every model.

```bash
icp canister call llama_cpp load_model '(record { args = vec {"--model"; "models/model.gguf"; "--no-warmup"; "-c"; "2048"; "-b"; "2048"; "-ub"; "2048"} })' -e local
# -> Ok = record { output = "Model succesfully loaded into memory."; ... }
```

`load_model` reads the decision metadata from the gguf (`modern-bert.decision.type =
laya`) and switches the context to embedding mode. On a decision model, `run_update`,
`run_query` and `new_chat` return `Err` ("use run_decision").

## Set the per-call token budget

`run_decision` answers the questions of a request in order, as long as they fit in
`max_tokens_update` tokens per update call; the rest is reported as `pending` and answered
when you re-send the same request. With ~180 M instructions per token, **200** keeps a
call under the 40 B instruction limit:

```bash
icp canister call llama_cpp set_max_tokens '(record { max_tokens_query = 200 : nat64; max_tokens_update = 200 : nat64 })' -e local
```

A single question longer than the budget returns `Err` (its token count and the budget).

## Ask a question

The `intent` question of llama.cpp PR #29818, with the customer data as a JSON state:

```bash
icp canister call llama_cpp run_decision '(record { state = variant { Json = "{\"message\": \"Hi, I was charged twice for my order #4471 and I want a refund.\", \"plan\": \"pro\", \"order\": {\"id\": 4471, \"items\": [\"phone case\", \"charger\"]}}" }; questions = vec { record { id = "intent"; kind = variant { choice }; instructions = "What does the customer want?" } }; options = vec { record { question_id = "intent"; key = "refund"; description = "wants money back" }; record { question_id = "intent"; key = "cancel"; description = "wants to cancel an order" }; record { question_id = "intent"; key = "track"; description = "wants to know where an order is" }; record { question_id = "intent"; key = "other"; description = "anything else" } } })' -e local
```

Verified on-chain output (local replica):

```
answers = vec { record { id = "intent"; kind = variant { choice }; choice = "refund"; confidence = 0.98; ... } };
probabilities = vec { refund 0.983; cancel 0.0006; track 0.016; other 0.0000 };
input_tokens = 87; pending = vec {};
```

and the canister log (`icp canister logs llama_cpp -e local`):

```
llama_cpp: run_decision - intent (choice, 87 tokens) -> refund (p=0.98, confidence=0.98)
llama_cpp: run_decision - 87 tokens this call, 1/1 questions answered
```

Several questions about the same state go in one request; each gets its own forward pass
(the state is part of every question's prompt). Re-send the identical request while
`pending` is not empty: answers of earlier calls are kept in a per-principal file and
returned with the new ones.

## Compared with llama-server

`llama-server` (`POST /v1/systemone`, CPU, same llama.cpp commit) on the same question:
refund 0.971, track 0.029. The canister: refund 0.983, track 0.016.

Julia-1 at Q8_0 is **sensitive to kernel rounding**. Clear decisions agree everywhere, but
close calls can differ between backends, and even between CPU and Metal `llama-server`.
On the PR's `frustration` question, CPU llama-server says "mildly annoyed" (0.63) while
Metal llama-server and the canister say "annoyed". The canister's own results are
deterministic: the same request on the same wasm always gives the same probabilities.

## What it's good (and not good) for

- **Good:** routing and intent detection, moderation (`noul`: "is this spam?"),
  urgency triage, checking an agent's output before it acts, in 50+ languages, on short
  inputs. Act on high-confidence answers.
- **Not good:** inputs longer than ~200 tokens per question (for a long state, use
  [Kev-0.8B](README-decision-model-kev-0.8b.md): it stores the state, up to ~4,000
  tokens, over several calls, while each question's instructions and options must still
  fit in one call), and fine distinctions where the top two options are close.
- **State format matters.** For the PR's refund message, Julia-1 answers `refund` (0.98)
  when the state is the PR's JSON object, but `track` (0.57; llama-server CPU: 0.90) when
  the same sentence is sent as plain text. Test your state format against llama-server.
- For English with higher precision, see [README-decision-model-Laya.md](README-decision-model-Laya.md)
  (much more expensive per token).
