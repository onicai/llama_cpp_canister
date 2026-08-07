# LFM2.5-2.6B — an on-device agent model, running on-chain

Liquid AI [released LFM2.5-2.6B](https://www.liquid.ai/blog/lfm2-5-2-6b) on
2026-08-04 as a model you can run *on your own device* — 220 tok/s on an M5 Max,
30 tok/s on a phone, under 2.5 GB of memory, with tool calling and a 128 K
context.

**It also runs unmodified inside an ICP canister**, where there is no device at
all: the weights live in the canister's stable memory, inference runs in the
replicated execution environment, and the only thing a user needs is a browser.

This document records what that costs, measured on mainnet.

> **Read this before adopting it.** The 2.6B *runs*, but it is the slowest model
> we have deployed — **4 tokens per update call** — and it **fails the
> word-guessing quality gate** at 96/100. For the word game,
> [LFM2.5-1.2B](README-LFM2.5-1.2B.md) remains the recommendation at 9
> tokens/call and 100/100. The 2.6B is interesting for its *agentic* skills
> (tool calling, multi-step planning), which this gate does not measure.

---

## Headline results (mainnet, `-c 4096`, dual q8_0 KV, `--batch-size 8`)

| metric                                  | Qwen3-1.7B      | LFM2.5-1.2B    | **LFM2.5-2.6B**     |
| --------------------------------------- | --------------- | -------------- | ------------------- |
| gguf size (Q4_K_M)                      | 1,107,409,472 B | 730,895,168 B  | **1,674,454,848 B** |
| **generation ceiling (tokens/call)**    | 6               | **9**          | **4**               |
| **ingestion ceiling (tokens/call)**     | 8               | 11             | **4**               |
| cycles per `run_update` (max_tokens 4)  | 26,836,073,673  | 12,446,618,543 | **38,865,179,739**  |
| native decode, `llama-bench -t 1 -n 32` | 7.92 tok/s      | 11.93 tok/s    | **5.83 tok/s**      |
| word-game gate (20 words × 5 seeds)     | 100/100         | 100/100        | **96/100 — FAIL**   |

A 60-token answer needs **15 `run_update` calls** on the 2.6B, against 7 on the
1.2B and 10 on Qwen3-1.7B.

All three cycle figures are measured at the same `max_tokens = 4`, so they
compare directly: the 2.6B costs **3.1× the 1.2B** and **1.4× Qwen3-1.7B** for
the identical amount of work. At 38.9 B cycles it sits at **97 % of the 40 B
instruction limit** — which is precisely why a 5th token is rejected.

## Why it is slow despite the hybrid architecture

From the gguf's own metadata:

| key                          | value                                          |
| ---------------------------- | ---------------------------------------------- |
| `lfm2.block_count`           | 30 — only **8 attention**, 22 short-convolution |
| `lfm2.embedding_length`      | 2048                                            |
| `lfm2.feed_forward_length`   | 10752                                           |
| `lfm2.vocab_size`            | **128000**                                      |
| `lfm2.context_length`        | 128000                                          |
| `lfm2.shortconv.l_cache`     | 3                                               |

The hybrid design (22 of 30 blocks are cheap short convolutions) is what makes
the 1.2B fast. Here it is outweighed by two things: 2.7 B weights, and a
**128 000-token vocabulary**. The output projection is 2048 × 128000 ≈ 262 M
weights that every single generated token must run through — that one matmul is
paid per token and does not shrink with the hybrid block mix.

## Get the gguf

From [LiquidAI/LFM2.5-2.6B-GGUF](https://huggingface.co/LiquidAI/LFM2.5-2.6B-GGUF):

```bash
mkdir -p models/LiquidAI/LFM2.5-2.6B-GGUF
curl -L -o models/LiquidAI/LFM2.5-2.6B-GGUF/LFM2.5-2.6B-Q4_K_M.gguf \
  https://huggingface.co/LiquidAI/LFM2.5-2.6B-GGUF/resolve/main/LFM2.5-2.6B-Q4_K_M.gguf

shasum -a 256 models/LiquidAI/LFM2.5-2.6B-GGUF/LFM2.5-2.6B-Q4_K_M.gguf
# 79fdf00351b46cf26f020aead28d01889886be87c55fa0eb907e6f9b00bfee14
```

Upload it (note `--network` takes the icp *environment* name). At ~27 MB/min
this takes a little over an hour:

```bash
python -m scripts.upload \
  --network production \
  --canister llama_cpp_qwen3_17b \
  --canister-filename models/lfm2-2.6b.gguf \
  --filetype gguf \
  --hf-sha256 "79fdf00351b46cf26f020aead28d01889886be87c55fa0eb907e6f9b00bfee14" \
  models/LiquidAI/LFM2.5-2.6B-GGUF/LFM2.5-2.6B-Q4_K_M.gguf
```

## Load the model

`lfm2` is supported by the vendored b10076 fork, so the 2.6B loads with no code
change. Dual q8_0 KV works, which means flash attention is active (a quantized V
cache throws without it).

```bash
icp canister call llama_cpp_qwen3_17b load_model '(record { args = vec {
  "--model"; "models/lfm2-2.6b.gguf";
  "-c"; "4096";
  "--batch-size"; "8"; "--ubatch-size"; "8";
  "--cache-type-k"; "q8_0"; "--cache-type-v"; "q8_0";
} })' -e production
# -> Ok = record { output = "Model succesfully loaded into memory."; ... }
```

The load returns in ~10 s.

## Set max_tokens

Measured on mainnet at `-c 4096` by walking `max_tokens_update` until `IC0522`.
**Both phases cap at 4** — unusual: on every other model ingestion is cheaper
than generation and its ceiling is higher.

| value | generation                           | ingestion                            |
| ----- | ------------------------------------ | ------------------------------------ |
| 2     | ok                                   | —                                    |
| **4** | **ok — the ceiling**                 | **ok — the ceiling**                 |
| 5     | rejected, `IC0522` instruction limit | rejected, `IC0522` instruction limit |
| 6     | rejected, `IC0522` instruction limit | —                                    |

```bash
icp canister call llama_cpp_qwen3_17b set_max_tokens \
  '(record { max_tokens_query = 1 : nat64; max_tokens_update = 3 : nat64 })' -e production
```

Use **3**, not 4, in production: the ceiling shrinks as a conversation grows,
and a larger `--ctx-size` lowers it too — see
[Appendix A: max_tokens](README.md#appendix-a-max_tokens) for why the context
you allocate is a flat tax on every call. Overshooting is safe to recover from:
the call is rejected with `IC0522` and rolled back, leaving the prompt cache
intact.

## Non-thinking mode — the one thing you must get right

**LFM2.5-2.6B is a reasoning model, and its chat template always opens a think
block.** The template ends with:

```
<|im_start|>assistant\n<think>
```

Left alone, the model spends its entire token budget reasoning out loud — which
at 4 tokens per call means dozens of update calls before any answer appears, and
the reasoning itself names the word you were hiding.

**Suppress it by prefilling a closed, empty think block** — the same trick Qwen3
uses for `enable_thinking=false`. The canister applies **no** chat template
(`LLAMA_ICP_NO_CHAT_TEMPLATES`), so the app sends this string verbatim. **Do not
write the BOS token** — the canister adds it from the vocab
(`llama_vocab_get_add_bos`):

```
<|im_start|>system
{system}<|im_end|>
<|im_start|>user
{user}<|im_end|>
<|im_start|>assistant
<think></think>
```

Verified on-chain. Three consecutive `run_update` calls at `max_tokens = 4`, run
with `-sp` so special tokens *would* be visible if the model emitted any:

```
gen 1: 'The morning began with'
gen 2: ' the sound of a'
gen 3: ' doorbell. \n\n'
```

No `<think>` token appears, and generation continues cleanly across the
prompt-cache round trip between calls.

## Quality: it fails the word-guessing gate

Run the gate yourself:

```bash
python scripts/quality_gate_word_game.py \
  --model models/LiquidAI/LFM2.5-2.6B-GGUF/LFM2.5-2.6B-Q4_K_M.gguf \
  --format lfm2-nothink --full --repeat 5
```

| prompt                                             | score      | verdict |
| -------------------------------------------------- | ---------- | ------- |
| README prompt (the one Qwen3 passes)               | 46/50      | FAIL    |
| + "Reply with exactly one sentence and then stop." | **96/100** | FAIL    |

### Failure 1 — it over-answers, then leaks in the runaway tail

Under the README prompt the model ignores "ONE short clue". It writes a
paragraph, then emits `\n\nHint:` and starts a *second* clue. Every leak is in
that tail, not in the first sentence:

```
HONEY   '...used as a natural sweetener or in cooking and baking. It has a
         distinctive taste and is known for its role in honeycomb production.'
BRIDGE  '...It can also refer to a pair of cards or the act of playing bridge
         as a card game. \n\nHint: A structure that spans a gap over water...'
```

Adding `Reply with exactly one sentence and then stop.` to the system prompt
fixes the rambling and lifts the 10-word screen to a clean 50/50.

### Failure 2 — on the full 20-word list it still leaks

The 10-word screen is not a gate. On all 20 words the tuned prompt still scores
96/100:

```
COFFEE     'A popular beverage made from roasted coffee beans, ...'   (3 of 5 seeds)
TELEPHONE  '...often connected by telephone lines or through in...'   (1 of 5 seeds)
```

That COFFEE sentence is *verbatim* the failure that eliminated Granite-4.0-1B.
It appears to be a genuinely hard word for small models: the obvious description
runs through the word itself.

### The prompt edit is not free

Per this repo's own methodology — *do not "improve" the system prompt without
re-running the reference* — the tuned prompt was re-run against the incumbent:

| model                       | README prompt | + "one sentence and then stop." |
| --------------------------- | ------------- | ------------------------------- |
| LFM2.5-1.2B (minus "small") | **100/100**   | 99/100                          |
| LFM2.5-2.6B                 | 46/50         | 96/100                          |

The edit *costs* the 1.2B its perfect score, and the single leak it causes is
the prompt's own fault — the model wrote the stop instruction into its answer:

```
VOLCANO  'A large mountain formed by volcanic activity.  \nStop!'
```

So the prompt is part of the model choice, not independent of it. Adopting the
2.6B would mean adopting a prompt that degrades the model we would be replacing.

## Known issue: `get_memory_status` traps above 4 GB of stable memory

With three ggufs uploaded, this canister's stable memory crossed 4 GB and the
endpoint now traps:

```
IC0502: Canister trapped: 32 bit stable memory api used on a memory larger than 4GB
Canister Backtrace: stable_size
```

`src/memory_status.cpp:29` calls the 32-bit `stable_size()`. It needs the 64-bit
`stable64_size` — which icpp-pro's `ic0.h` does not declare, so the import has to
be added by hand (the same way `ic0.performance_counter` was). Until then the
wasm heap after loading this model is **not measured**; it loads and runs inside
the 3.75 GiB `wasm_memory_limit`, which is all we can currently assert.

## Status and what is not verified

Verified on mainnet: uploads and checksums, loads in wasm at `-c 4096` with dual
q8_0 KV, non-thinking prefill works, both `max_tokens` ceilings, prompt-cache
round trip across calls.

Not verified:

- **Wasm heap after load** — blocked by the `get_memory_status` bug above.
- **Only `-c 4096`.** Larger contexts are untested; the ceiling is already 4, so
  a bigger context may push generation below one token per call.
- **Tool calling / agentic use**, which is the model's actual selling point and
  is not what the word game measures.
- **Long conversations.** All measurements used a ~20-token context.
- **`copy_prompt_cache` / multi-chat switching** with this model.
- **No native exact-token test** exists for LFM2 models; only Qwen3 has one
  (`native/test_qwen3.cpp`).

All three ggufs are on the experimentation canister — `models/model.gguf`
(Qwen3-1.7B), `models/lfm2.gguf` (LFM2.5-1.2B) and `models/lfm2-2.6b.gguf` — so
any of them can be loaded for comparison without re-uploading.
