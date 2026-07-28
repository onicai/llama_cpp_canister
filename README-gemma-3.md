# Gemma-3-270M (smallest on-chain model)

> The **smallest** model we run on `llama_cpp_canister` — Google's **Gemma-3-270M**
> (270 M parameters, instruction-tuned). Use it when you want the **cheapest, fastest**
> on-chain LLM and the task is simple: basic chat, short structured answers, or
> high-throughput generation where a low quality ceiling is acceptable. It generates
> **~40 tokens per update call** (vs ~25 for the 0.6B), and its heap peaks at only
> ~0.9 GiB, so it needs **no `wasm_memory_limit` bump**.
>
> Verified end-to-end on a local `llama_cpp_canister` (v0.14.0 wasm, llama.cpp
> **b10076** fork): it loads, generates coherent text, and its token ceiling was
> measured against the IC's 40 B-instruction limit (below).
>
> It runs on the **same `llama_cpp` canister** as the default — just upload this gguf
> and load it. Follow the main [README.md](README.md) for `# Set up` (icp-cli, Python
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

Gemma-3-270M is tiny, so the default f16 KV cache is fine — no quantized cache
needed:

```bash
icp canister call llama_cpp load_model '(record {
  args = vec {
    "--model"; "models/model.gguf";
    "-c"; "4096";
  }
})' -e local
# -> Ok = record { output = "Model succesfully loaded into memory."; ... }
```

Heap after load is ~0.9 GiB (`get_memory_status` reports `wasm_heap_bytes ≈
928_645_120`), well under the default limit — so, unlike Qwen3, **no
`wasm_memory_limit` bump is required**. You can raise `-c` (Gemma 3 supports up to
32K) if you have memory to spare.

## Set max_tokens

The measured **generation ceiling is 49 tokens per call** at short context (49 OK,
50 traps with `IC0522` — the IC's 40 B-instruction-per-message limit). Use **40** for
a safe margin, and lower it for long conversations (the per-token cost rises as the
context grows):

```bash
icp canister call llama_cpp set_max_tokens '(record {
  max_tokens_query = 1 : nat64;
  max_tokens_update = 40 : nat64
})' -e local
```

> How this was measured: a fresh `new_chat` → short prefill → one generation call of
> N tokens, scanning N until it traps. On the local replica (whose limit is the same
> `40000000000` instructions as mainnet, so the number carries over) the boundary was
> **49 OK / 50 TRAP**. This ~40 tok/call is higher than the 0.6B's ~25 — a smaller
> model buys more tokens per call.

## Chat

Gemma 3 uses its own turn template — end the prompt with the model turn opener:

```
<start_of_turn>user
{your message}<end_of_turn>
<start_of_turn>model
```

Ingest the prompt — repeat until `prompt_remaining` is empty (`--temp 0.7`, keep
sending the full prompt):

```bash
icp canister call llama_cpp run_update '(record {
  args = vec {
    "--prompt-cache"; "prompt.cache"; "--prompt-cache-all";
    "--temp"; "0.7";
    "-sp";
    "-p"; "<start_of_turn>user\nWrite a few sentences about why the ocean is important.<end_of_turn>\n<start_of_turn>model\n";
    "-n"; "1"
  }
})' -e local
```

Generate — repeat with an empty prompt until `generated_eog=true`:

```bash
icp canister call llama_cpp run_update '(record {
  args = vec {
    "--prompt-cache"; "prompt.cache"; "--prompt-cache-all";
    "--temp"; "0.7";
    "-sp";
    "-p"; "";
    "-n"; "40"
  }
})' -e local
```

Remove the prompt cache when done:

```bash
icp canister call llama_cpp remove_prompt_cache '(record {
  args = vec { "--prompt-cache"; "prompt.cache" }
})' -e local
```

### Verified on-chain output (local canister)

For the prompt above, the canister generated:

> The ocean is a vital resource, providing essential sustenance for marine life and
> supporting ecosystems worldwide. Its diverse environments and temperature
> variations ensure a stable and healthy environment for countless organisms.
> Furthermore, the ocean's …

Coherent and on-topic — good for a 270M model.

## What it's good (and not good) for

- **Good:** basic Q&A, short factual answers, simple formatting, high-throughput
  generation, demos of the "smallest possible on-chain LLM". Cheapest cycles and most
  tokens/call of any model here.
- **Not good:** anything needing precise instruction-following, negation, or
  secret-keeping. On the word-guessing use case it leaks the secret word roughly half
  the time (like Qwen3-0.6B) — enforce secrecy in your app, not the model (see the
  note in the main README). For stronger instruction-following, step up to
  [Qwen3-0.6B](README.md) or [Qwen3-1.7B](README-qwen3-1.7B.md).
