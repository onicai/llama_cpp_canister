# Qwen2.5-0.5B-Instruct (previous default model)

> This was the default reference model up to and including release **v0.12.1**.
> From **v0.13.0** the default is **Qwen3-0.6B** — see the main [README.md](README.md).
> Qwen2.5-0.5B is kept here (and in `test/test_qwen2.py` / `native/test_qwen2.cpp`)
> for regression, and remains an excellent, lightweight single-turn model. Follow
> the main README for `# Set up` (dfx, Python env, build & deploy), then use the
> model-specific steps below instead of the Qwen3 ones.

## Upload the gguf file

Download from HuggingFace: https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF

```bash
mkdir -p models/Qwen/Qwen2.5-0.5B-Instruct-GGUF
wget -c \
  -O models/Qwen/Qwen2.5-0.5B-Instruct-GGUF/qwen2.5-0.5b-instruct-q8_0.gguf \
  https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/resolve/main/qwen2.5-0.5b-instruct-q8_0.gguf
```

Verify the sha256 after download:

```bash
$ sha256sum models/Qwen/Qwen2.5-0.5B-Instruct-GGUF/qwen2.5-0.5b-instruct-q8_0.gguf
ca59ca7f13d0e15a8cfa77bd17e65d24f6844b554a7b6c12e07a5f89ff76844e
```

Upload it to the canister as `models/model.gguf`:

```bash
python -m scripts.upload \
  --network local \
  --canister llama_cpp \
  --canister-filename models/model.gguf \
  --filetype gguf \
  --hf-sha256 "ca59ca7f13d0e15a8cfa77bd17e65d24f6844b554a7b6c12e07a5f89ff76844e" \
  models/Qwen/Qwen2.5-0.5B-Instruct-GGUF/qwen2.5-0.5b-instruct-q8_0.gguf
```

Check filesize & sha256 in the canister:

```bash
dfx canister call llama_cpp uploaded_file_details '(record { filename = "models/model.gguf" })'
# ->
(
  variant {
    Ok = record {
      filename = "models/model.gguf";
      filesize = 675_710_816 : nat64;
      filesha256 = "ca59ca7f13d0e15a8cfa77bd17e65d24f6844b554a7b6c12e07a5f89ff76844e";
    }
  },
)
```

Optional pytest QA: `pytest -vv --network local test/test_qwen2.py`

## Load the model

Qwen2.5-0.5B fits with the default context and only needs the K cache quantized:

```bash
dfx canister call llama_cpp load_model '(record {
  args = vec {
    "--model"; "models/model.gguf";
    "--cache-type-k"; "q8_0";
  }
})'
```

## Set max_tokens

```bash
dfx canister call llama_cpp set_max_tokens '(record {
  max_tokens_query = 1 : nat64;
  max_tokens_update = 25 : nat64
})'
```

For this model, ~25 tokens can be generated per update call (measured on the b10076
build; the hard first-call ceiling is 28 before a call traps).

## Chat

Qwen2.5 is not a thinking model, so the assistant turn ends right after
`<|im_start|>assistant\n` (no `<think>` block).

Start a new chat:

```bash
dfx canister call llama_cpp new_chat '(record {
  args = vec { "--prompt-cache"; "prompt.cache"; "--cache-type-k"; "q8_0"; }
})'
```

Ingest the prompt — repeat until `prompt_remaining` is empty (`-n 1`, keep sending
the full prompt):

```bash
dfx canister call llama_cpp run_update '(record {
  args = vec {
    "--prompt-cache"; "prompt.cache"; "--prompt-cache-all";
    "--cache-type-k"; "q8_0";
    "--repeat-penalty"; "1.1";
    "--temp"; "0.6";
    "-sp";
    "-p"; "<|im_start|>system\nYou are a helpful assistant.<|im_end|>\n<|im_start|>user\ngive me a short introduction to LLMs.<|im_end|>\n<|im_start|>assistant\n";
    "-n"; "1"
  }
})'
```

Generate new tokens — repeat with an empty prompt until `generated_eog=true`:

```bash
dfx canister call llama_cpp run_update '(record {
  args = vec {
    "--prompt-cache"; "prompt.cache"; "--prompt-cache-all";
    "--cache-type-k"; "q8_0";
    "--repeat-penalty"; "1.1";
    "--temp"; "0.6";
    "-sp";
    "-p"; "";
    "-n"; "512"
  }
})'
```

Remove the prompt cache when done:

```bash
dfx canister call llama_cpp remove_prompt_cache '(record {
  args = vec { "--prompt-cache"; "prompt.cache" }
})'
```

Equivalent local `llama-cli` command:

```bash
<path-to>/llama-cli \
  -m /models/Qwen/Qwen2.5-0.5B-Instruct-GGUF/qwen2.5-0.5b-instruct-q8_0.gguf \
  --prompt-cache prompt.cache --prompt-cache-all \
  --cache-type-k q8_0 \
  --repeat-penalty 1.1 \
  --temp 0.6 \
  -sp \
  -p "<|im_start|>system\nYou are a helpful assistant.<|im_end|>\n<|im_start|>user\ngive me a short introduction to LLMs.<|im_end|>\n<|im_start|>assistant\n" \
  -n 512
```
