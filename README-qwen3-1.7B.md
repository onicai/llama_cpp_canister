# Qwen3-1.7B (optional larger model)

> A bigger, markedly stronger sibling of the default **Qwen3-0.6B**. Use it when a
> task needs reliable **instruction-following** — negation ("do _not_ mention X"),
> keeping a secret, or accurate, non-generic wording. On our word-guessing use case
> the 0.6B leaked the secret word ~half the time and gave hokey clues; the 1.7B
> keeps the word hidden and gives specific, accurate clues (examples below).
>
> It runs on the **same `llama_cpp` canister** as the default — you just upload a
> different gguf and load it with the settings below. Follow the main
> [README.md](README.md) for `# Set up` (dfx, Python env, build, deploy, cycles, and
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
   the 0.6B's ~8–20 tokens per call traps. Decoding **4 tokens per call** stays
   safely under the limit — for both prompt ingestion and generation. The trade-off
   is more `run_update` round-trips per response.

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
  --network local \
  --canister llama_cpp \
  --canister-filename models/model.gguf \
  --filetype gguf \
  --hf-sha256 "b139949c5bd74937ad8ed8c8cf3d9ffb1e99c866c823204dc42c0d91fa181897" \
  models/Qwen/Qwen3-1.7B-GGUF/Qwen3-1.7B-Q4_K_M.gguf
```

Check filesize & sha256 in the canister:

```bash
dfx canister call llama_cpp uploaded_file_details '(record { filename = "models/model.gguf" })'
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
dfx canister call llama_cpp load_model '(record {
  args = vec {
    "--model"; "models/model.gguf";
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
should report ~2.07 GiB used after load.

## Set max_tokens

```bash
dfx canister call llama_cpp set_max_tokens '(record {
  max_tokens_query = 1 : nat64;
  max_tokens_update = 4 : nat64
})'
```

`max_tokens_update = 4` is the ceiling for this model (see limit #2 above). Ingestion
and generation both advance 4 tokens per `run_update` call, so your app must loop
`run_update` more times per response than with the 0.6B.

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
dfx canister call llama_cpp new_chat '(record {
  args = vec { "--prompt-cache"; "prompt.cache"; "--cache-type-k"; "q8_0"; "--cache-type-v"; "q8_0"; }
})'
```

Ingest the prompt — repeat until `prompt_remaining` is empty (keep sending the full
prompt):

```bash
dfx canister call llama_cpp run_update '(record {
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
dfx canister call llama_cpp run_update '(record {
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
dfx canister call llama_cpp remove_prompt_cache '(record {
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
