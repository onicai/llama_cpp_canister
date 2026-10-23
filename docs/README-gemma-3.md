# Gemma-3-270M (smallest on-chain model)

> The **smallest** model we run on `llama_cpp_canister` — Google's **Gemma-3-270M**
> (270 M parameters, instruction-tuned). Use it when you want the **cheapest, fastest**
> on-chain LLM and the task is simple: basic chat, short structured answers, or
> high-throughput generation where a low quality ceiling is acceptable. It generates
> **~44 tokens per update call** (vs ~25 for the 0.6B), and its heap peaks at only
> ~0.9 GiB, so it needs **no `wasm_memory_limit` bump**.
>
> Verified end-to-end on a local `llama_cpp_canister` (v0.14.0 wasm, llama.cpp
> **b10076** fork): it loads, generates coherent text, and its token ceiling was
> measured against the IC's 40 B-instruction limit (below).
>
> It runs on the **same `llama_cpp` canister** as the default — just upload this gguf
> and load it. Follow the main [README.md](../README.md) for `# Set up` (icp-cli, Python
> env, build, deploy, cycles), then use the model-specific steps here.

## Two things to know

1. **Architecture support.** Gemma-3-270M is the **`gemma3`** architecture. The
   canister's llama.cpp **b10076** fork loads it (`load_model` returns "Model
   succesfully loaded into memory."). Older forks may not — this was verified on the
   v0.14.0 build.
2. **Use `--temp 0.7`.** With greedy decoding (temp 0) this tiny model sometimes ends
   a turn immediately (empty answer). A non-zero temperature fixes it.

## Upload the gguf file

Download the instruction-tuned Q8_0 gguf (unsloth):
https://huggingface.co/unsloth/gemma-3-270m-it-GGUF

```bash
mkdir -p models/google/gemma-3-270m-it-GGUF
wget -c \
  -O models/google/gemma-3-270m-it-GGUF/gemma-3-270m-it-Q8_0.gguf \
  https://huggingface.co/unsloth/gemma-3-270m-it-GGUF/resolve/main/gemma-3-270m-it-Q8_0.gguf
```

Verify the sha256 after download:

```bash
$ sha256sum models/google/gemma-3-270m-it-GGUF/gemma-3-270m-it-Q8_0.gguf
d156a5159f2f79c1b1d53c7c1cc20f1ff28ab8d00f17a292620aad13399b9698
```

Upload it to the canister as `models/model.gguf`:

```bash
python -m scripts.upload \
  --network local \
  --canister llama_cpp \
  --canister-filename models/model.gguf \
  --filetype gguf \
  --hf-sha256 "d156a5159f2f79c1b1d53c7c1cc20f1ff28ab8d00f17a292620aad13399b9698" \
  models/google/gemma-3-270m-it-GGUF/gemma-3-270m-it-Q8_0.gguf
```

Check filesize & sha256 in the canister:

```bash
icp canister call llama_cpp uploaded_file_details '(record { filename = "models/model.gguf" })' -e local --query
# ->
(
  variant {
    Ok = record {
      filename = "models/model.gguf";
      filesize = 291_546_144 : nat64;
      filesha256 = "d156a5159f2f79c1b1d53c7c1cc20f1ff28ab8d00f17a292620aad13399b9698";
    }
  },
)
```

## Load the model

Use a **q8_0 KV cache** — measured against the default f16 cache it gives a slightly
higher token ceiling (~55 vs ~50) and is marginally faster, because the quantized KV
halves the prompt-cache state written on every call:

```bash
icp canister call llama_cpp load_model '(record {
  args = vec {
    "--model"; "models/model.gguf";
    "-c"; "4096";
    "--cache-type-k"; "q8_0";
    "--cache-type-v"; "q8_0";
  }
})' -e local
# -> Ok = record { output = "Model succesfully loaded into memory."; ... }
```

Heap after load is ~0.9 GiB (`get_memory_status` reports `wasm_heap_bytes ≈
928_645_120`), well under the default limit — so, unlike Qwen3, **no
`wasm_memory_limit` bump is required**. You can raise `-c` (Gemma 3 supports up to
32K) if you have memory to spare.

## Set max_tokens

```bash
icp canister call llama_cpp set_max_tokens '(record {
  max_tokens_query = 1 : nat64;
  max_tokens_update = 0 : nat64
})' -e local
```

`0` means no cap: every update call, and every chunk of a `run_update_full`, stops by
itself before the IC's instruction limit.

With the q8_0 KV cache above, the measured **generation ceiling is ~55 tokens per
call** at short context (55 OK, 60 traps with `IC0522` — the IC's
40 B-instruction-per-message limit), so a response takes few calls or chunks.

> How this was measured: a fresh `new_chat` → short prefill → one generation call of
> N tokens, scanning N until it traps. On the local replica (whose limit is the same
> `40000000000` instructions as mainnet, so the number carries over) the boundary was
> **55 OK / 60 TRAP** with the q8_0 KV cache (~50 with the default f16 cache; expect
> ±5 run-to-run). This ~55 tok/call is well above the 0.6B's ~25 — a smaller model
> buys more tokens per call.

## Chat

Gemma 3 uses its own turn template — end the prompt with the model turn opener:

```
<start_of_turn>user
{your message}<end_of_turn>
<start_of_turn>model
```

Start a new chat, then ingest the prompt and generate the whole response in one call
(`--temp 0.7`):

```bash
icp canister call llama_cpp new_chat '(record {
  args = vec { "--prompt-cache"; "prompt.cache"; "--cache-type-k"; "q8_0"; "--cache-type-v"; "q8_0"; }
})' -e local

icp canister call llama_cpp run_update_full '(record {
  args = vec {
    "--prompt-cache"; "prompt.cache"; "--prompt-cache-all";
    "--cache-type-k"; "q8_0"; "--cache-type-v"; "q8_0";
    "--temp"; "0.7";
    "-sp";
    "-p"; "<start_of_turn>user\nWrite a few sentences about why the ocean is important.<end_of_turn>\n<start_of_turn>model\n";
    "-n"; "512"
  }
})' -e local
```

To show the text while it is being generated, use the `run_update` loop instead (see the
[main README](../README.md#set-up)).

Remove the prompt cache when done:

```bash
icp canister call llama_cpp remove_prompt_cache '(record {
  args = vec { "--prompt-cache"; "prompt.cache" }
})' -e local
```

### Verified on-chain output (local canister)

For the prompt above, one `run_update_full` call (5.4s, 2 chunks, 58 tokens) returned:

> The ocean is vital to our planet's health and sustains life. It provides essential
> food sources, regulates climate patterns, and supports a vast array of marine
> ecosystems. Its deep waters are teeming with life, offering habitats for countless
> species and contributing to the overall biodiversity of the world.

Coherent and on-topic — good for a 270M model.

## What it's good (and not good) for

- **Good:** basic Q&A, short factual answers, simple formatting, high-throughput
  generation, demos of the "smallest possible on-chain LLM". Cheapest cycles and most
  tokens/call of any model here.
- **Not good:** anything needing precise instruction-following, negation, or
  secret-keeping. On the word-guessing use case it leaks the secret word roughly half
  the time (like Qwen3-0.6B) — enforce secrecy in your app, not the model (see the
  note in the main README). For stronger instruction-following, step up to
  [Qwen3-0.6B](../README.md) or [Qwen3-1.7B](README-qwen3-1.7B.md).
