# Qwen3-1.7B (optional larger model)

> A bigger, markedly stronger sibling of the default **Qwen3-0.6B**. Use it when a
> task needs reliable **instruction-following** — negation ("do _not_ mention X"),
> keeping a secret, or accurate, non-generic wording. On our word-guessing use case
> the 0.6B leaked the secret word ~half the time and gave hokey clues; the 1.7B
> keeps the word hidden and gives specific, accurate clues (examples below).
>
> It runs on the **same `llama_cpp` canister** as the default — you just upload a
> different gguf and load it with the settings below. Follow the main
> [README.md](README.md) for `# Set up` (icp, Python env, build, deploy, cycles, and
> the 3.75 GiB `wasm_memory_limit`), then use the model-specific steps here.

## Two hard Internet-Computer limits you must respect

These are not tuning preferences — get them wrong and the canister traps:

1. **Use the Q4_K_M quantization, not Q8_0.** A single message can read at most
   ~2 GiB from stable memory. The Q8_0 gguf is ~1.83 GB and trips this on load with
   `IC0524` ("stable memory out of bounds"). The **Q4_K_M** gguf (~1.1 GB) loads
   cleanly. (Heap after load ≈ 2.07 GiB, leaving ~1.68 GiB headroom under the
   3.75 GiB limit.)

2. **Set `max_tokens_update` to 4.** A message may execute at most 40 B
   instructions (`IC0522`). The 1.7B is ~2.8× the compute of the 0.6B, so decoding
   the 0.6B's ~8–20 tokens per call is rejected. Decoding **4 tokens per call**
   stays safely under the limit — for both prompt ingestion and generation. The
   trade-off is more `run_update` round-trips per response.

   > **Note (fixed):** there used to be a *second*, undocumented ceiling here.
   > `run_update` sized its decode batches from the per-call `--batch-size`
   > default (2048) instead of the batch the persisted context was actually
   > created with, so any `max_tokens_update` above the loaded `--batch-size`
   > (**8** for this model) tripped
   > `GGML_ASSERT(n_tokens_all <= cparams.n_batch)` and **trapped the canister**
   > (`IC0502`), corrupting that caller's `prompt.cache`. Decode is now chunked
   > at the context's real batch, so the instruction limit above is the only
   > ceiling, and exceeding it is a clean `IC0522` rejection rather than a trap.

## Upload the gguf file

Download the **Q4_K_M** gguf from HuggingFace (unsloth):
https://huggingface.co/unsloth/Qwen3-1.7B-GGUF

```bash
mkdir -p models/Qwen/Qwen3-1.7B-GGUF
wget -c \
  -O models/Qwen/Qwen3-1.7B-GGUF/Qwen3-1.7B-Q4_K_M.gguf \
  https://huggingface.co/unsloth/Qwen3-1.7B-GGUF/resolve/main/Qwen3-1.7B-Q4_K_M.gguf
```

Verify the sha256 after download:

```bash
$ sha256sum models/Qwen/Qwen3-1.7B-GGUF/Qwen3-1.7B-Q4_K_M.gguf
b139949c5bd74937ad8ed8c8cf3d9ffb1e99c866c823204dc42c0d91fa181897
```

Upload it to the canister as `models/model.gguf`:

```bash
python -m scripts.upload \
  -e local \
  --canister llama_cpp \
  --canister-filename models/model.gguf \
  --filetype gguf \
  --hf-sha256 "b139949c5bd74937ad8ed8c8cf3d9ffb1e99c866c823204dc42c0d91fa181897" \
  models/Qwen/Qwen3-1.7B-GGUF/Qwen3-1.7B-Q4_K_M.gguf
```

Check filesize & sha256 in the canister:

```bash
icp canister call llama_cpp -e local uploaded_file_details '(record { filename = "models/model.gguf" })'
# ->
(
  variant {
    Ok = record {
      filename = "models/model.gguf";
      filesize = 1_107_409_472 : nat64;
      filesha256 = "b139949c5bd74937ad8ed8c8cf3d9ffb1e99c866c823204dc42c0d91fa181897";
    }
  },
)
```

## Load the model

Quantize both K and V caches and keep the micro-batch small:

```bash
icp canister call llama_cpp -e local load_model '(record {
  args = vec {
    "--model"; "models/model.gguf";
    "--no-warmup";
    "-c"; "16384";
    "--batch-size"; "8";
    "--ubatch-size"; "8";
    "--cache-type-k"; "q8_0";
    "--cache-type-v"; "q8_0";
  }
})'
# -> Ok = record { output = "Model succesfully loaded into memory."; ... }
```

You can watch the heap with the `get_memory_status` query (see main README) — it
should report **~2.04 GiB** used after load.

**`--no-warmup` is worth ~316 MiB of permanent headroom.** Warmup runs a dummy decode
whose transient peak is far above what normal inference needs, and because wasm linear
memory never shrinks, that peak becomes the canister's permanent high-water. Measured on
mainnet at `-c 16384`, from a fresh heap each time:

| load | heap after load |
| ---- | --------------- |
| with `--no-warmup` | 2_187_264_000 (**2.04 GiB**) |
| without | 2_518_351_872 (2.35 GiB) |

The memory is saved, not merely deferred: after `--no-warmup`, running a real
`new_chat` + `run_update` left the heap unchanged at 2_187_264_000. (Verified with
`--batch-size 8` and `max_tokens_update = 4`; a much larger batch could still reach the
warmup peak.)

> **`-c 16384` requires v0.16.4 or later.** Between 2026-07-30 and v0.16.4 this load
> was rejected on mainnet with
> `IC0522: ... large memory operation that used 4_1xx_xxx_xxx instructions and exceeded
> the slice limit 2_000_000_000`, and `-c 4096` was the stopgap.
>
> Cause: an IC platform change (`dfinity/ic` `b7225383e` / `#10789`, the deterministic
> memory tracker) made every touched 4 KiB page cost ~10 000 instructions (5 000
> `accessed` + 5 000 `dirty`; heap writes used to be 1 000 and heap reads free). Zeroing
> the KV cache is a single `memset` the IC cannot interrupt, so its whole cost lands in
> one slice. At `-c 16384` that is ~2.44 B instructions — more than a full 2 B slice,
> and over the 4 B carry-over allowance whenever it happens to start late in a slice.
> (That timing dependence is why the failure was *not* monotonic in `--ctx-size`:
> `-c 24576` loaded fine while `-c 16384` failed, reproducibly.)
>
> Fix in v0.16.4: the CPU buffer type now reports a 128 MiB `get_max_size`
> (`ggml-backend.cpp`, `#ifdef __wasi__`), so llama.cpp's own
> `ggml_backend_alloc_ctx_tensors_from_buft()` splits the KV cache into several buffers
> whose `clear()` does one `memset` each — giving the scheduler a pause point between
> them. Verified on mainnet.

## Set max_tokens

```bash
icp canister call llama_cpp -e local set_max_tokens '(record {
  max_tokens_query = 1 : nat64;
  max_tokens_update = 4 : nat64
})'
```

`max_tokens_update = 4` is the recommended value for this model (see limit #2
above). Ingestion and generation both advance 4 tokens per `run_update` call, so
your app must loop `run_update` more times per response than with the 0.6B.

### Measured ceilings

Probed on a local replica (same 40 B instruction limit as mainnet) at
**`-c 4096` `--batch-size 8 --ubatch-size 8`, dual q8_0 KV** — walking
`max_tokens_update` up until the call is rejected with `IC0522`:

| Phase                          | highest value that worked | first value rejected |
| ------------------------------ | ------------------------- | -------------------- |
| Prompt ingestion (`-p <text>`) | **8**                     | 9                    |
| Generation (`-p ""`)           | **6**                     | 7                    |

Ingestion is cheaper per token than generation, which is why its ceiling is higher.

**These were measured at a short context (~20–30 tokens), at `-c 4096`.** Two things
make them shrink:

- The per-token cost grows with the KV span, so both ceilings drop as a conversation
  gets longer.
- **At a larger `--ctx-size` they drop again.** Measured on mainnet with the same
  model and settings, varying only `--ctx-size`:

  | `--ctx-size` | cycles per `run_update` call | generation ceiling |
  | ------------ | ---------------------------- | ------------------ |
  | 4096  | 26,836,073,673 | **6** |
  | 16384 | —              | **5** |
  | 24576 | 31,132,686,530 | **5** |

  So a smaller context really does buy tokens per call: `-c 4096` gives 6 where
  `-c 24576` gives 5, at ~16 % fewer cycles. Re-measure the ceiling if you change
  the context.

  > Why: profiled on mainnet with the IC instruction counter, the per-call
  > `llama_memory_clear(mem, true)` costs **0.86 B instructions at `-c 4096` and
  > 5.16 B at `-c 24576`** — exactly linear in `--ctx-size`. Token generation
  > itself (`llama_decode`) is **25.72 B instructions regardless of context**, so
  > everything the larger context costs you is KV bookkeeping, not inference.
  > Switching to `clear(false)` only recovers ~4.6 %, because the IC charges a
  > page on first touch and the session restore then pays what the memset no
  > longer does. See [Appendix A: max_tokens](README.md#appendix-a-max_tokens)
  > for the per-context instruction budget.

That is why **4** — not the measured maximum — is the recommended setting: it has to
hold for the whole conversation, at whatever context you loaded.

If you want the extra throughput, `max_tokens_update` is a normal setting you can
change between phases. Ingesting a long system prompt at 8 halves the round-trips:

```bash
# ingest at 8 ...
icp canister call llama_cpp -e local set_max_tokens '(record { max_tokens_query = 1 : nat64; max_tokens_update = 8 : nat64 })'
#   ... loop run_update with -p "<prompt>" until prompt_remaining is empty ...
# ... then generate at 4
icp canister call llama_cpp -e local set_max_tokens '(record { max_tokens_query = 1 : nat64; max_tokens_update = 4 : nat64 })'
```

Overshooting is now **safe to recover from**: since the batch fix in limit #2, a value
that is too large is rejected with `IC0522` and the message is rolled back, leaving the
prompt cache intact — so an app may probe upward and fall back on rejection. Before
that fix, any value above the loaded `--batch-size` trapped and corrupted the cache.

## Chat — word-guessing game example

Qwen3 is a hybrid thinking model; run it in **non-thinking** mode by ending the
assistant turn with an empty `<think>\n\n</think>\n\n` block.

The system prompt teaches the "describe, don't name" behavior with one example; the
app fills in the word per request:

**System prompt**

```
You support a word-guessing game. When asked for a hint about a word, give ONE short clue that describes what the thing IS or DOES, using other words, without writing the word itself.
Example:
Request: Provide a hint about CAT. Do not mention CAT.
Hint: A small furry pet that purrs and loves to chase mice.
```

**User prompt (templated by the app)**

```
Provide a hint about {word}. Do not mention {word}.
```

Start a new chat:

```bash
icp canister call llama_cpp -e local new_chat '(record {
  args = vec { "--prompt-cache"; "prompt.cache"; "--cache-type-k"; "q8_0"; "--cache-type-v"; "q8_0"; }
})'
```

Ingest the prompt — repeat until `prompt_remaining` is empty (keep sending the full
prompt):

```bash
icp canister call llama_cpp -e local run_update '(record {
  args = vec {
    "--prompt-cache"; "prompt.cache"; "--prompt-cache-all";
    "--cache-type-k"; "q8_0"; "--cache-type-v"; "q8_0";
    "--temp"; "0.5"; "--top-p"; "0.8"; "--top-k"; "20"; "--repeat-penalty"; "1.1";
    "-sp";
    "-p"; "<|im_start|>system\nYou support a word-guessing game. When asked for a hint about a word, give ONE short clue that describes what the thing IS or DOES, using other words, without writing the word itself.\nExample:\nRequest: Provide a hint about CAT. Do not mention CAT.\nHint: A small furry pet that purrs and loves to chase mice.<|im_end|>\n<|im_start|>user\nProvide a hint about DOG. Do not mention DOG.<|im_end|>\n<|im_start|>assistant\n<think>\n\n</think>\n\n";
    "-n"; "1"
  }
})'
```

Generate — repeat with an empty prompt until `generated_eog=true`:

```bash
icp canister call llama_cpp -e local run_update '(record {
  args = vec {
    "--prompt-cache"; "prompt.cache"; "--prompt-cache-all";
    "--cache-type-k"; "q8_0"; "--cache-type-v"; "q8_0";
    "--temp"; "0.5"; "--top-p"; "0.8"; "--top-k"; "20"; "--repeat-penalty"; "1.1";
    "-sp";
    "-p"; "";
    "-n"; "4"
  }
})'
```

### Concatenating generation calls — drop the boundary-repeat token

Because generation is split across many small `run_update` calls, **each generation
call after the first repeats the previous call's last token as its own first
token**. For example, generating "A loyal companion that barks and plays fetch."
comes back as:

```
gen 0: "A loyal companion that"
gen 1: " that barks and"       <- repeats "that"
gen 2: " and plays fetch."     <- repeats "and"
gen 3: ".<|im_end|>"           <- repeats "."
```

When your app stitches the pieces together, drop the leading duplicate token of each
call after the first (or collapse adjacent identical tokens). That yields the clean
output. `--repeat-penalty` does **not** remove this — each `run_update` uses a fresh
sampler that cannot see the prior call's last token, so it is a mechanical
call-boundary artifact, not a sampling problem. (The 0.6B rarely shows it because it
emits ~20–28 tokens per call; the 1.7B, capped at 4, crosses many more boundaries.)

Remove the prompt cache when done:

```bash
icp canister call llama_cpp -e local remove_prompt_cache '(record {
  args = vec { "--prompt-cache"; "prompt.cache" }
})'
```

### Verified on-chain results (local canister, Q4_K_M)

Each clue below was generated by the canister with the system + user prompts above
(clues shown after removing the call-boundary repeat described above). The secret
word never appears in the clue (0 leaks), and the clues are specific and accurate:

| Secret word | Clue generated on-chain                                                        | Leak? |
| ----------- | ----------------------------------------------------------------------------- | ----- |
| DOG         | A loyal companion that barks and plays fetch.                                 | no    |
| HOUSE       | A shelter for animals or people, often used for living or resting.            | no    |
| CAR         | A fast-paced sport vehicle with four wheels that is used for transportation.  | no    |

Contrast with the default **Qwen3-0.6B** on the same prompts: it leaked the secret
word roughly half the time and produced vaguer, more generic clues. The extra
parameters of the 1.7B buy noticeably stronger instruction-following.

> Secret-keeping is best enforced in your app, not the model: keep the secret word
> and guess-checking in application code and only ask the model to paraphrase a hint.
> See the note in the main README on why sampling params alone can't guarantee a
> small model never leaks.
