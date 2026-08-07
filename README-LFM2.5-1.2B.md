# LFM2.5-1.2B-Instruct — a faster replacement for Qwen3-1.7B

Liquid AI's LFM2.5-1.2B-Instruct passes the word-guessing quality gate that
Qwen3-1.7B passes, at **half the cycles per call** and **9 tokens per update call
instead of 6**.

Why it is cheaper: LFM2 is a hybrid architecture — 16 blocks, of which only 6 are
grouped-query attention and the other 10 are double-gated short convolutions. It
is also 1.2 B parameters against Qwen3's 1.7 B.

---

## Headline results (mainnet, `-c 4096`, dual q8_0 KV, `--batch-size 8`)

| metric                                   | Qwen3-1.7B      | **LFM2.5-1.2B**      | change              |
| ---------------------------------------- | --------------- | -------------------- | ------------------- |
| gguf size (Q4_K_M)                       | 1,107,409,472 B | **730,895,168 B**    | −34 %               |
| wasm heap after load                     | ~2.69 GiB       | **2.06 GiB**         | −0.6 GiB            |
| cycles per `run_update` (max_tokens 4)   | 26,836,073,673  | **12,446,618,543**   | **0.464× (2.16× cheaper)** |
| **generation ceiling (tokens/call)**     | **6**           | **9**                | **+50 %**           |
| word-game gate (20 words × 5 seeds)      | 100/100         | **100/100**          | equal               |
| native decode, `llama-bench -t 1`        | 7.92 tok/s      | 11.93 tok/s          | +51 %               |

For an app, a 60-token answer needs **7 `run_update` calls instead of 10**, and
each call costs less than half the cycles.

## Get the gguf

From [LiquidAI/LFM2.5-1.2B-Instruct-GGUF](https://huggingface.co/LiquidAI/LFM2.5-1.2B-Instruct-GGUF):

```bash
mkdir -p models/LiquidAI/LFM2.5-1.2B-Instruct-GGUF
curl -L -o models/LiquidAI/LFM2.5-1.2B-Instruct-GGUF/LFM2.5-1.2B-Instruct-Q4_K_M.gguf \
  https://huggingface.co/LiquidAI/LFM2.5-1.2B-Instruct-GGUF/resolve/main/LFM2.5-1.2B-Instruct-Q4_K_M.gguf

shasum -a 256 models/LiquidAI/LFM2.5-1.2B-Instruct-GGUF/LFM2.5-1.2B-Instruct-Q4_K_M.gguf
# b1b3de114215d9507409a662a501a631095a479a419584e8a2ded6304b19b4f5
```

Upload it (note `--network` takes the icp *environment* name):

```bash
python -m scripts.upload \
  --network production \
  --canister llama_cpp_qwen3_17b \
  --canister-filename models/lfm2.gguf \
  --filetype gguf \
  --hf-sha256 "b1b3de114215d9507409a662a501a631095a479a419584e8a2ded6304b19b4f5" \
  models/LiquidAI/LFM2.5-1.2B-Instruct-GGUF/LFM2.5-1.2B-Instruct-Q4_K_M.gguf
```

## Load the model

`lfm2` is supported by the vendored b10076 fork and **loads in wasm**. Dual q8_0
KV works, which means flash attention is active (a quantized V cache throws
without it).

```bash
icp canister call llama_cpp_qwen3_17b load_model '(record { args = vec {
  "--model"; "models/lfm2.gguf";
  "-c"; "4096";
  "--batch-size"; "8"; "--ubatch-size"; "8";
  "--cache-type-k"; "q8_0"; "--cache-type-v"; "q8_0";
} })' -e production
# -> Ok = record { output = "Model succesfully loaded into memory."; ... }

icp canister call llama_cpp_qwen3_17b get_memory_status '()' -e production
# -> wasm_heap_bytes = 2_215_903_232   (2.06 GiB)
```

## Set max_tokens

Measured on mainnet at `-c 4096` by walking `max_tokens_update` until `IC0522`:

| value | result |
| ----- | ------ |
| 8     | ok     |
| **9** | **ok — the ceiling** |
| 10    | rejected, `IC0522` instruction limit |

```bash
icp canister call llama_cpp_qwen3_17b set_max_tokens \
  '(record { max_tokens_query = 1 : nat64; max_tokens_update = 8 : nat64 })' -e production
```

Use **8**, not 9, in production: the ceiling shrinks as a conversation grows, and
a larger `--ctx-size` lowers it too — see
[Appendix A: max_tokens](README.md#appendix-a-max_tokens) for why the context you
allocate is a flat tax on every call. Overshooting is safe to recover from: the
call is rejected with `IC0522` and rolled back, leaving the prompt cache intact.

## The prompt — read this before judging quality

**The canister applies NO chat template** (`LLAMA_ICP_NO_CHAT_TEMPLATES`), so the
app must send the fully rendered prompt. LFM2 uses ChatML. **Do not write the BOS
token** — the canister adds it from the vocab (`llama_vocab_get_add_bos`):

```
<|im_start|>system
{system}<|im_end|>
<|im_start|>user
{user}<|im_end|>
<|im_start|>assistant
```

### ⚠️ One word in the system prompt matters

With `README-qwen3-1.7B.md`'s system prompt **verbatim**, LFM2 copies the word
"small" out of the CAT example and produces factually wrong hints:

```
PIANO   'Small musical instrument with keys, ...'      <- a piano is not small
HONEY   'Small sweet substance used in cooking'
```

**Deleting the single word "small" from the example fixes both** and keeps the
gate at 100/100. Use this system prompt with LFM2:

```
You support a word-guessing game. When asked for a hint about a word, give ONE
short clue that describes what the thing IS or DOES, using other words, without
writing the word itself.
Example:
Request: Provide a hint about CAT. Do not mention CAT.
Hint: A furry pet that purrs and loves to chase mice.
```

> **The prompt is part of the model choice, not independent of it.** That same
> edit *breaks* Qwen3-1.7B (it starts leaking "volcanic ash" for VOLCANO in 3 of
> 5 seeds). Whenever you change model or prompt, re-run the gate for the pair:
> `python scripts/quality_gate_word_game.py --model <gguf> --format lfm2 --repeat 5`

## Verified on-chain results

Full flow per word — `remove_prompt_cache` → `new_chat` → ingest until
`prompt_remaining` is empty → generate until `generated_eog`. Sampling
`--temp 0.5 --top-p 0.8 --top-k 20 --repeat-penalty 1.1`:

| word   | ingest calls | gen calls | eog  | output |
| ------ | ------------ | --------- | ---- | ------ |
| DOG    | 25           | 4         | true | `A loyal companion with a wagging tail and playful spirit.` |
| COFFEE | 26           | 4         | true | `A warm drink enjoyed in the morning, known for its rich flavor.` |
| PIANO  | 26           | 3         | true | `Musical instrument with keys, often played for relaxation.` |

No leaks, every one terminated on its own, and the prompt-cache round trip works
across calls. (COFFEE is worth noting: Granite-4.0-1B leaks it deterministically
— `"made from roasted coffee beans"` — which is why Granite was rejected.)

The ingest call counts above were taken at `max_tokens_update = 4`. At the
measured ceiling of 8 they drop by half.

### Two output details for the app

- Those runs passed `-sp`, so **special tokens appear in `output`** — the raw
  text ends with `<|im_end|>`. Drop `-sp`, or strip the marker app-side.
- Qwen3's per-call **boundary-repeat artifact** (the first token of a call
  repeating the previous call's last token) was **not** observed here, but the
  concatenation logic should still dedupe defensively.

## Status and what is not yet verified

Verified: loads in wasm, dual q8_0 KV, quality gate on-chain, prompt-cache round
trip, cycles/call, `max_tokens` ceiling.

Not yet verified:

- **Only `-c 4096`.** Larger contexts are untested for this model — re-measure
  the ceiling and the heap before using `-c 16384`.
- **`copy_prompt_cache` / multi-chat switching** with LFM2's hybrid layout.
- **Long conversations.** All measurements used a short context (~20–30 tokens).
- **The native test suite** has no LFM2 exact-token test; only Qwen3 has one
  (`native/test_qwen3.cpp`).

Both ggufs are on the experimentation canister — `models/model.gguf`
(Qwen3-1.7B) and `models/lfm2.gguf` — so either can be loaded for comparison
without re-uploading.
