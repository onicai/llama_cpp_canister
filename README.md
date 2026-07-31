[![llama_cpp_canister](https://github.com/onicai/llama_cpp_canister/actions/workflows/cicd-mac.yml/badge.svg)](https://github.com/onicai/llama_cpp_canister/actions/workflows/cicd-mac.yml)

# llama.cpp for the Internet Computer

![llama](https://user-images.githubusercontent.com/1991296/230134379-7181e485-c521-4d23-a0d6-f7b3b61ba524.png)

`llama_cpp_canister` lets you deploy [ggml-org/llama.cpp](https://github.com/ggml-org/llama.cpp) as a Smart Contract on the Internet Computer and run an LLM fully on-chain — the verifiable brain for your on-chain AI agents. Our recommended default is **Qwen3-0.6B**, run in non-thinking mode for **multi-turn conversations** with up to a ~12K-word context.

# Capabilities 🔥

- **Run any LLM on-chain** via the gguf format — we recommend **Qwen3-0.6B** ([Qwen/Qwen3-0.6B-GGUF](https://huggingface.co/Qwen/Qwen3-0.6B-GGUF)).
- **Multi-turn, verifiable inference** — long back-and-forth conversations, entirely on-chain.
- MIT open source 🧑‍💻 · Well documented 📝 · Fully QA'd via CI/CD ✅
- Easy to build, test & deploy 🚧 · Smoke-testing framework using pytest 🚬

# In the wild

llama_cpp_canister is used as the on-chain LLM brain inside the following projects:

_(Issue a PR to get your project listed)_

| Project    | Description/Tagline                                                                                       | GitHub                                             | X                                                 | Token                                      | URL                                               |
| ---------- | --------------------------------------------------------------------------------------------------------- | -------------------------------------------------- | ------------------------------------------------- | ------------------------------------------ | ------------------------------------------------- |
| funnAI     | Truly Open AI with Bitcoin Tokenomics, plus much more!                                                    | -                                                  | [@onicaiHQ](https://x.com/onicaiHQ)               | $FUNNAI                                    | [funnai.onicai.com](https://funnai.onicai.com)    |
| ICGPT      | On-chain Prompt Studio<br>_Optimize your prompts against LLMs running inside Internet Computer canisters_ | [icgpt](https://github.com/icppWorld/icgpt)        | [@onicaiHQ](https://x.com/onicaiHQ)               | -                                          | [icgpt.onicai.com](https://icgpt.onicai.com)      |
| IConfucius | Wisdom fueled by Cycles<br>_Fully on chain AI tokenized on Bitcoin_                                       | [IConfucius](https://github.com/onicai/IConfucius) | [@IConfucius_odin](https://x.com/IConfucius_odin) | [$ICONFUCIUS](https://odin.fun/token/29m8) | [onicai.com](https://www.onicai.com/#/iconfucius) |

# Need help or have feedback? ❤️

- [OpenChat C++ community](https://oc.app/community/cklkv-3aaaa-aaaar-ar7uq-cai/?ref=6e3y2-4yaaa-aaaaf-araya-cai)
- [Forum: Llama.cpp on the Internet Computer](https://forum.dfinity.org/t/llama-cpp-on-the-internet-computer/33471?u=icpp)

# Using the release

You can just grab the latest [release](https://github.com/onicai/llama_cpp_canister/releases) and unzip it. Then follow the instructions from the README, but skip these steps:

- No need to clone the repo. Everything is included in the release zip
- No need to build the wasm. It is included in the release zip.

# Set up

- Install icp-cli (and ic-wasm). Requires [Node.js](https://nodejs.org/) >= 22:

  ```bash
  npm install -g @icp-sdk/icp-cli @icp-sdk/ic-wasm

  # Verify it is installed
  icp --version
  ```

  > **Note:** icp-cli replaces the deprecated `dfx`. This project was built and
  > tested against **icp-cli 1.2.0**.

- Clone the repo and it's children:

  _(skip when using the [release](https://github.com/onicai/llama_cpp_canister/releases))_

  Approach 1: HTTPS — no GitHub SSH keys required

  ```bash
  # Clone this repository
  git clone https://github.com/onicai/llama_cpp_canister.git
  cd llama_cpp_canister

  # Clone llama_cpp_onicai_fork (our fork of llama.cpp) into ./src
  cd src
  git clone https://github.com/onicai/llama_cpp_onicai_fork.git
  ```

  Approach 2: SSH — requires a configured GitHub SSH key
  ```bash
  # Clone this repository
  git clone git@github.com:onicai/llama_cpp_canister.git
  cd llama_cpp_canister

  # Clone llama_cpp_onicai_fork into ./src
  cd src
  git clone git@github.com:onicai/llama_cpp_onicai_fork.git
  ```

  Note: If you see Permission denied (publickey) errors, use the HTTPS method above or configure an SSH key in your GitHub account.


- Create a Python environment with dependencies installed

  ❗❗❗ Use Python 3.11 _(the version CI builds and tests against)_ ❗❗❗

  ```bash
  # We use MiniConda
  conda create --name llama_cpp_canister python=3.11
  conda activate llama_cpp_canister

  # Install the python dependencies
  # From root of llama_cpp_canister repo:
  pip install -r requirements.txt
  ```

- Build the wasm for canister `llama_cpp`:

  _(skip when using the [release](https://github.com/onicai/llama_cpp_canister/releases))_

  - Compile & link to WebAssembly (wasm):

    ```bash
    make build-info-cpp-wasm
    icpp build-wasm
    ```

    Notes:

    - The build of the wasm must be done on a `Mac` !
    - Instead of building the wasm, you can also grab the latest [release](https://github.com/onicai/llama_cpp_canister/releases) and unzip it.

- Deploy the wasm to a canister on the local network:

  - Start the local network:

    ```bash
    icp network start -d
    ```

    > **Note:** icp-cli runs one local network **per project** and (with
    > `gateway.port: 0` in `icp.yaml`) assigns a **random ephemeral port** on every
    > start, so parallel projects never collide. Never hardcode `localhost:8000`;
    > read the port back with `icp network status -e local --json`.

  - Deploy the wasm to a canister on the local network:

    `icp.yaml` defines two canisters that share the same wasm: **`llama_cpp`** (the
    default, serving **Qwen3-0.6B** — the steps below) and **`llama_cpp_qwen25`** (the
    previous default, **Qwen2.5-0.5B** — see [README-qwen2.5.md](README-qwen2.5.md)).
    Deploy just the one you need by naming it:

    ```bash
    icp deploy llama_cpp -e local -y

    # When upgrading the code in the canister, use:
    icp deploy llama_cpp -m upgrade -e local -y
    ```

  - Check the health endpoint of the `llama_cpp` canister (calling a canister by
    name requires naming its environment with `-e`):
    ```bash
    $ icp canister call llama_cpp health '()' -e local --query
    (variant { Ok = record { status_code = 200 : nat16 } })
    ```

- Ensure the canister has enough cycles

  The LLM upload and inference calls consume cycles. You can add with:

  ```bash
  # Add 20 trillion cycles (drawn from your local identity's seeded balance)
  icp canister top-up llama_cpp --amount 20000000000000 -e local
  ```

- Raise the canister's wasm memory limit (needed for larger models)

  The default reference model, Qwen3-0.6B, runs close to the wasm heap ceiling. Set
  the `wasm_memory_limit` to 3.75 GiB (wasm32 cannot address a full 4 GiB) with
  `icp canister settings update`:

  ```bash
  icp canister settings update llama_cpp --wasm-memory-limit 4026531840 -e local

  # verify
  icp canister status llama_cpp -e local | grep "Wasm memory limit"
  ```

- Upload gguf file

  The canister is now up & running, and ready to be loaded with a gguf file. Our
  default reference model is **Qwen3-0.6B** (q8_0) — a state-of-the-art small LLM
  that we run in **non-thinking** mode for clean, multi-turn on-chain inference
  (see the chat steps below). You can use any model available in gguf format.

  _(For the previous default, Qwen2.5-0.5B, see [README-qwen2.5.md](README-qwen2.5.md).)_

  _(For a larger, stronger model — noticeably better at instruction-following,
  negation, and secret-keeping — see [README-qwen3-1.7B.md](README-qwen3-1.7B.md).)_

  - Download the model from huggingface: https://huggingface.co/Qwen/Qwen3-0.6B-GGUF

    Store it in: `models/Qwen/Qwen3-0.6B-GGUF/Qwen3-0.6B-Q8_0.gguf`

    ```bash
    mkdir -p models/Qwen/Qwen3-0.6B-GGUF
    wget -c \
      -O models/Qwen/Qwen3-0.6B-GGUF/Qwen3-0.6B-Q8_0.gguf \
      https://huggingface.co/Qwen/Qwen3-0.6B-GGUF/resolve/main/Qwen3-0.6B-Q8_0.gguf
    ```

    After download, verify the sha256 hash:

    ```bash
    $ sha256sum models/Qwen/Qwen3-0.6B-GGUF/Qwen3-0.6B-Q8_0.gguf
    9465e63a22add5354d9bb4b99e90117043c7124007664907259bd16d043bb031
    ```

  - Upload the gguf file to the canister:

    ```bash
    python -m scripts.upload \
      -e local \
      --canister llama_cpp \
      --canister-filename models/model.gguf \
      --filetype gguf \
      --hf-sha256 "9465e63a22add5354d9bb4b99e90117043c7124007664907259bd16d043bb031" \
      models/Qwen/Qwen3-0.6B-GGUF/Qwen3-0.6B-Q8_0.gguf
    ```

    NOTEs:

    - In C++, files are stored in stable memory of the canister. They will survive a code upgrade.
    - The --hf-sha256 argument is optional but highly recommended:
      - The upload process will check if the file on disk has the same sha256 as the one you downloaded from HuggingFace.
      - The --hf-sha256 for our sample model can be found at https://huggingface.co/Qwen/Qwen3-0.6B-GGUF/blob/main/Qwen3-0.6B-Q8_0.gguf

  - Check the filesize & sha256 of the uploaded gguf file in the canister

    ```bash
    icp canister call llama_cpp -e local uploaded_file_details '(record {
      filename = "models/model.gguf"
    })'

    # Which returns the following for the Qwen3-0.6B-GGUF model
    (
      variant {
        Ok = record {
          filename = "models/model.gguf";
          filesize = 639_446_688 : nat64;
          filesha256 = "9465e63a22add5354d9bb4b99e90117043c7124007664907259bd16d043bb031";
        }
      },
    )
    ```

- Optional: You can now run a pytest based QA, using the icpp-pro smoketesting framework:

  ```bash
  pytest -vv --network local test/test_qwen3.py
  ```

- Load the gguf file into Orthogonal Persisted (OP) working memory

  ```bash
  icp canister call llama_cpp -e local load_model '(record {
    args = vec {
      "--model"; "models/model.gguf";
      "--cache-type-k"; "q8_0";
      "--cache-type-v"; "q8_0";
      "--batch-size"; "64";
      "--ubatch-size"; "64";
      "--ctx-size"; "16384";
    }
  })'
  ```

  **Why these args for Qwen3-0.6B?** Two levers keep a large context inside the wasm
  heap. First, we quantize **both** the K and V caches (`q8_0`), which halves the KV
  cache. Second — the key one — we set a small **`--batch-size 64 --ubatch-size 64`**.
  llama.cpp's compute buffers scale with batch size (the output/logits buffer is
  `batch × vocab` ≈ **1.2 GiB** at the default batch of 2048 for Qwen3's ~152K vocab; the
  attention buffer is `ubatch × ctx`). A canister serves one request at a time and is
  instruction-limited on the IC, so a large batch buys nothing here but costs GiBs of
  heap — shrinking it to 64 frees ~2 GiB. That leaves the KV cache as the only thing that
  grows with context, which is how we run **`--ctx-size 16384`** (~12K words of
  conversation) at ~1.76 GiB heap, ~2 GiB under the 3.75 GiB `wasm_memory_limit`. See
  [Context size & memory](#appendix-b-context-size--memory) for the full mechanism, levers, and
  **risks**. This assumes you raised the `wasm_memory_limit` to 3.75 GiB (see the
  `update-settings` step above); watch live usage with the `get_memory_status` query.

- Set the max_tokens for this model, to avoid it hits the IC's instruction limit

  _(See Appendix A for values of others models.)_

  ```bash
  icp canister call llama_cpp -e local set_max_tokens '(record {
    max_tokens_query = 1 : nat64;
    max_tokens_update = 20 : nat64
  })'

  icp canister call llama_cpp -e local get_max_tokens
  ```

  For Qwen3-0.6B the first-call ceiling is ~25–29 tokens; we use **20** to leave
  headroom as the context grows across a multi-turn conversation.

- Chat with the LLM

  - Ensure the canister is ready for Inference, with the model loaded

    ```bash
    icp canister call llama_cpp -e local ready
    ```

  - Chat with the LLM:

    Details how to use the Qwen models with llama.cpp:
    https://qwen.readthedocs.io/en/latest/run_locally/llama.cpp.html

    **Running Qwen3 in non-thinking mode.** Qwen3 is a hybrid *thinking* model.
    To disable thinking (no `<think>` tokens, lower cost) we end the assistant turn
    with an empty `<think>\n\n</think>\n\n` block — this is exactly what the official
    chat template emits for `enable_thinking=false`. All prompts below use that form.
    (Thinking mode _is_ possible but not recommended on this 0.6B under the canister's
    memory limits: it rambles, often does not terminate, and costs ~10x more tokens.)

    Start a new chat

    ```bash
    icp canister call llama_cpp -e local new_chat '(record {
      args = vec {
        "--prompt-cache"; "prompt.cache";
        "--cache-type-k"; "q8_0";
        "--cache-type-v"; "q8_0";
      }
    })'
    ```

    ***

    Ingest the prompt:

    Repeat this call until `prompt_remaining` in the response is empty.
    This ingests the prompt into the prompt-cache, using multiple update calls:
    (-) Keep sending the full prompt
    (-) Use `"-n"; "1"`, so it does not generate new tokens
    (-) The assistant turn ends with the empty `<think>\n\n</think>\n\n` block, so
        Qwen3 runs in non-thinking mode

    ```bash
    icp canister call llama_cpp -e local run_update '(record {
      args = vec {
        "--prompt-cache"; "prompt.cache"; "--prompt-cache-all";
        "--cache-type-k"; "q8_0"; "--cache-type-v"; "q8_0";
        "--repeat-penalty"; "1.1";
        "--temp"; "0.6";
        "-sp";
        "-p"; "<|im_start|>user\ngive me a short introduction to LLMs.<|im_end|>\n<|im_start|>assistant\n<think>\n\n</think>\n\n";
        "-n"; "1"
      }
    })'
    ```

    ***

    Generate new tokens:

    Once `prompt_remaining` in the response is empty.
    (-) repeat this call, until `generated_eog=true`
    (-) Use an empty prompt: `"-p"; "";`
    (-) Use `"-n"; "512"`, so it will now generate new tokens

    ```bash
    icp canister call llama_cpp -e local run_update '(record {
      args = vec {
        "--prompt-cache"; "prompt.cache"; "--prompt-cache-all";
        "--cache-type-k"; "q8_0"; "--cache-type-v"; "q8_0";
        "--repeat-penalty"; "1.1";
        "--temp"; "0.6";
        "-sp";
        "-p"; "";
        "-n"; "512"
      }
    })'
    ```

    ***

    Once `generated_eog` in the response is `true`, the LLM is done generating.
    The generated `output` contains **no `<think>` tokens** — a clean, direct answer:

    ```bash
    (
      variant {
        Ok = record {
          output = " from answering questions to generating creative content.<|im_end|>";
          conversation = "<|im_start|>user\ngive me a short introduction to LLMs.<|im_end|>\n<|im_start|>assistant\n<think>\n\n</think>\n\nA Large Language Model (LLM) is a type of artificial intelligence model that can understand and generate human-like text. These models are trained on vast amounts of text data and are capable of understanding and responding to natural language in a variety of ways, from answering questions to generating creative content.<|im_end|>";
          error = "";
          status_code = 200 : nat16;
          prompt_remaining = "";
          generated_eog = true;
        }
      },
    )
    ```

    ***

    **Exact token accounting.** Every `run_update` / `run_query` **success** record
    also carries five `opt nat64` fields with the exact token counts for that call
    (they decode to `null` on non-run records such as `new_chat` / `load_model`):

    | Field                       | Meaning                                                    |
    | --------------------------- | ---------------------------------------------------------- |
    | `n_prompt_tokens`           | total prompt tokens presented this call                    |
    | `n_prompt_tokens_cached`    | prompt-cache prefix reused for free (the cache-break offset)|
    | `n_prompt_tokens_decoded`   | prompt tokens actually decoded this call                   |
    | `n_tokens_generated`        | tokens generated this call                                 |
    | `n_prompt_tokens_remaining` | prompt suffix left for the next call                       |

    They always reconcile: `cached + decoded + remaining == n_prompt_tokens`, and
    `n_tokens_generated` is `0` until the prompt is fully ingested
    (`n_prompt_tokens_remaining == 0`). This lets a caller measure the exact
    ingest-vs-generate split and the prompt-cache break offset per call. Being
    `opt`, they are upgrade-safe: a client built against the older `.did` simply
    ignores them.

    ***

    **Multi-turn conversation.** Qwen3 handles back-and-forth conversations well. To
    continue a chat, send the **full accumulated conversation** as the prompt each
    turn — the prompt-cache reuses the shared prefix, so only the new turn is
    processed. Here, turn 2 asks the model to recall facts stated in turn 1
    (`new_chat` first, then ingest with `-n 1` until `prompt_remaining` is empty,
    then generate with an empty `-p` until `generated_eog=true`):

    ```bash
    icp canister call llama_cpp -e local run_update '(record {
      args = vec {
        "--prompt-cache"; "prompt.cache"; "--prompt-cache-all";
        "--cache-type-k"; "q8_0"; "--cache-type-v"; "q8_0";
        "--temp"; "0.6"; "-sp";
        "-p"; "<|im_start|>user\nMy name is Sam and I have 3 cats named Milo, Coco, and Ziggy.<|im_end|>\n<|im_start|>assistant\nNice to meet you, Sam! You have three cats: Milo, Coco, and Ziggy.<|im_end|>\n<|im_start|>user\nHow many cats do I have, what are their names, and what is my name?<|im_end|>\n<|im_start|>assistant\n<think>\n\n</think>\n\n";
        "-n"; "1"
      }
    })'
    ```

    The assistant correctly recalls turn 1:

    ```
    You have 3 cats: Milo, Coco, and Ziggy.
    Your name is Sam.
    ```

    ***

    Remove the prompt cache when done - this keeps stable memory usage at a minimum

    ```bash
    icp canister call llama_cpp -e local remove_prompt_cache '(record {
      args = vec {
        "--prompt-cache"; "prompt.cache"
      }
    })'
    ```

    Note: The sequence of update calls to the canister is required because the Internet Computer has a limitation
    on the number of instructions it allows per call. For Qwen3-0.6B, ~20 tokens are generated per update call (first-call ceiling ~25-29 before a call traps).

    This sequence of update calls is equivalent to using the [ggml-org/llama.cpp](https://github.com/ggml-org/llama.cpp)
    repo directly and running the `llama-cli` locally, with the command:

    ```bash
    <path-to>/llama-cli \
      -m /models/Qwen/Qwen3-0.6B-GGUF/Qwen3-0.6B-Q8_0.gguf \
      --prompt-cache prompt.cache --prompt-cache-all \
      --cache-type-k q8_0 --cache-type-v q8_0 --batch-size 64 --ubatch-size 64 --ctx-size 16384 \
      --repeat-penalty 1.1 \
      --temp 0.6 \
      -sp \
      -p "<|im_start|>user\ngive me a short introduction to LLMs.<|im_end|>\n<|im_start|>assistant\n<think>\n\n</think>\n\n" \
      -n 512
    ```

  - Saving the chats

    This functionality is off by default. You can turn it on/off with:

    ```bash
    icp canister call llama_cpp -e local chats_resume
    icp canister call llama_cpp -e local chats_pause
    ```

    When on, up to 3 chats per principal are saved.
    The `get_chats` method retrieves them for the principal of the caller.

    ```
    icp canister call llama_cpp -e local get_chats
    ```

# log_pause & log_resume

The llama.cpp code is quite verbose. In llama_cpp_canister, you can
turn the logging off and back on with these commands:

```bash
# turn off logging
icp canister call llama_cpp -e local log_pause

# turn on logging
icp canister call llama_cpp -e local log_resume
```

# Logging to a file

For debug purposes, you can tell the canister to log to a file and download it afterwards.

Pass `"--log-file"; "main.log";` to each `run_update` calls.

Afterwards, you can download the `main.log` file from the canister with:

```bash
python -m scripts.download \
  -e local \
  --canister llama_cpp \
  --local-filename main.log main.log
```

You can cleanup by deleting both the log & prompt.cache files in the canister:

```bash
icp canister call llama_cpp -e local remove_prompt_cache '(record {
  args = vec {
    "--prompt-cache"; "prompt.cache"
    }
})'

icp canister call llama_cpp -e local remove_log_file '(record {
  args = vec {
    "--log-file"; "main.log"
  }
})'
```

# Smoke testing the deployed LLM

You can run a smoketest on the deployed LLM:

- Deploy the Qwen3-0.6B model as described above

- Run the smoketests for the Qwen3-0.6B LLM deployed to your local IC network:

  ```
  # First test the canister functions, like 'health'
  pytest -vv --network local test/test_canister_functions.py

  # Then run the inference tests (multi-turn, non-thinking)
  pytest -vv --network local test/test_qwen3.py
  ```

  _(The previous default, Qwen2.5-0.5B, is still covered by `test/test_qwen2.py`.)_

# Prompt Caching

When a prompt cache file is already present, llama_cpp_canister automatically applies prompt caching to reduce latency and cost.

All repetitive content at the beginning of the prompt does not need to be processed by the LLM, so make sure to design your AI agent prompts such that repetitive content is placed at the beginning.

Each caller of the llama_cpp_canister has it's own cache folder, and has the following endpoints available to manage their prompt-cache files:

```bash
# Remove a prompt cache file from the caller's cache folder
icp canister call llama_cpp -e local remove_prompt_cache '(record {
  args = vec {
    "--prompt-cache"; "prompt.cache"
  }
})'

# Copy a prompt cache file within the caller's cache folder
icp canister call llama_cpp -e local copy_prompt_cache '(record {
  from = "prompt.cache";
  to = "prompt-save.cache"
})'

# ------------------------------------------------------------------
# Download a chunk of a prompt cache file
# Note: chunksize of 5 bytes is for demo only.
#       -> use 200_000 or higher (2_000_000 max) in actual download
#       -> experiment what chunksize results in fastest download
icp canister call llama_cpp -e local download_prompt_cache_chunk '(record {
  promptcache = "prompt.cache";
  chunksize = 5 : nat64;
  offset = 0 : nat64;
})'
# -> this will return something like:
(
  variant {
    Ok = record {
      done = true;
      chunk = blob "\47\47\55\46\03";
      offset = 0 : nat64;
      filesize = 675_710_816 : nat64;
      chunksize = 5 : nat64;
    }
  },
)

# --
# Then call it again to download the next chunk of bytes
icp canister call llama_cpp -e local download_prompt_cache_chunk '(record {
  promptcache = "prompt.cache";
  chunksize = 5 : nat64;
  offset = 5 : nat64;
})'
# -> this will return something like:
(
  variant {
    Ok = record {
      done = true;
      chunk = blob "\08\33\41\43\04";
      offset = 5 : nat64;
      filesize = 675_710_816 : nat64;
      chunksize = 5 : nat64;
    }
  },
)

# The IC has a limit on message size:
#    (-) same-subnet, inter-canister: Up to 10 MiB
#    (-) else                       : Up to  2 MiB
# You can wrap the download call in a loop, as in scripts/download.py
# Using a small chunksize goes faster !
#
python -m scripts.download \
    -e local \
    --canister llama_cpp \
    --filetype promptcache \
    --chunksize 2000000 \
    prompt.cache

# ------------------------------------------------------------------
# Upload a chunk of a prompt cache file
# Note: chunksize of 5 bytes is for demo only.
#       -> use 200_000 or higher (2_000_000 max) in actual download
#       -> experiment what chunksize results in fastest download
icp canister call llama_cpp -e local upload_prompt_cache_chunk '(record {
  promptcache = "prompt.cache";
  chunk = blob "\47\47\55\46\03";
  chunksize = 5 : nat64;
  offset = 0 : nat64;
})'
# -> this will return
(
  variant {
    Ok = record {
      filename = ".canister_cache/<principal-id>/sessions/prompt.cache";
      filesize = 5 : nat64;
      filesha256 = "fe3b34fd092c3e2c6da3270eb91c4d3e9c2c6f891c21b6ed7358bf5ecca2d207";
    }
  },
)

# --
# Then call it again to upload the next chunk of bytes
icp canister call llama_cpp -e local upload_prompt_cache_chunk '(record {
  promptcache = "prompt.cache";
  chunk = blob "\08\33\41\43\04";
  chunksize = 5 : nat64;
  offset = 5 : nat64;
})'
# -> this will return
(
  variant {
    Ok = record {
      filename = ".canister_cache/<principal-id>/sessions/prompt.cache";
      filesize = 10 : nat64;
      filesha256 = "438bb530032946102742839ca22319211409cbd1c403f87a82e68e35e89e8c15";
    }
  },
)

# --
# You can check the filesize & sha256 of the uploaded prompt cache file in the canister
icp canister call llama_cpp -e local uploaded_prompt_cache_details '(record {
  promptcache = "prompt.cache";
})'
# -> this will return
(
  variant {
    Ok = record {
      filename = ".canister_cache/<principal-id>/sessions/prompt.cache";
      filesize = 10 : nat64;
      filesha256 = "438bb530032946102742839ca22319211409cbd1c403f87a82e68e35e89e8c15";
    }
  },
)

# --
# You can wrap the upload call in a loop, as in scripts/upload.py
python -m scripts.upload \
    -e local \
    --canister llama_cpp \
    --canister-filename prompt.cache \
    --filetype promptcache \
    --chunksize 2000000 \
    prompt.cache
```

# Access control

By default, only a controller can call the inference endpoints:

- new_chat
- run_update
- run_query

You can open up the inference endpoints using the following command:

```bash
#
# 0 = only controllers
# 1 = all except anonymous
icp canister call llama_cpp -e local set_access '(record {
  level = 1 : nat16
})'

# Verify it worked
icp canister call llama_cpp -e local get_access

# A caller can check it's access rights with
icp canister call llama_cpp -e local check_access
```

# File Management

A C++ canister on the Internet Computer uses a virtual file system, stored in Stable Memory.

Several endpoints are exposed that allows you to interact & manage the files.

For example, you can explore what is stored in the `.canister_cache` folder:

```bash
# Query call to list all files & directories in a folder
icp canister call llama_cpp -e local recursive_dir_content_query '(record {dir = ".canister_cache"; max_entries = 0 : nat64})' --output json
# Update call in case you hit the instruction limit
icp canister call llama_cpp -e local recursive_dir_content_update '(record {dir = ".canister_cache"; max_entries = 0 : nat64})' --output json
# Update call to get eg. the first 5000 entries, in case you still hit the instruction limit
icp canister call llama_cpp -e local recursive_dir_content_update '(record {dir = ".canister_cache"; max_entries = 5000 : nat64})' --output json

# Get the size of a file in bytes
icp canister call llama_cpp -e local filesystem_file_size '(record {filename = "<filename>"})' --output json

# Get the creation timestamp of a file in nanoseconds (also returns age of file in seconds)
icp canister call llama_cpp -e local get_creation_timestamp_ns '(record {filename = "<filename>"})' --output json

# remove a file or empty directory
icp canister call llama_cpp -e local filesystem_remove '(record {filename = "<filename>"})'
```

# Prompt-Cache Cleanup Timer

The canister can self-maintain its prompt-cache directory on a recurring
schedule, deleting files in `.canister_cache/<principal>/sessions/` whose
`mtime` is older than a configurable Time to Live (TTL). Once you start the
timer, the canister handles prompt-cache hygiene on its own.

**Defaults:**
- period: 600 seconds (10 minutes between cleanup ticks)
- TTL: 21600 seconds (6 hours — older files are deleted)
- per-tick cap: 256 files (caps work-per-tick to stay under the IC's
  per-message instruction budget; the next tick continues)

**Operator-driven lifecycle.** The timer is **not** auto-armed in
`canister_init` or `canister_post_upgrade`. After every install / upgrade
you must explicitly call `cache_cleanup_start_timer`. Timer state is
in-memory only and does not survive an upgrade.

All endpoints below require **admin role**:
- `cache_cleanup_start_timer`, `cache_cleanup_stop_timer`,
  `cache_cleanup_now`, `set_cache_cleanup_config` need `AdminUpdate` role
  (controller or whitelisted via `assignAdminRole`).
- `get_cache_cleanup_stats` needs `AdminQuery` role.

```bash
# ------------------------------------------------------------------
# Arm the recurring timer (REQUIRED after every install / upgrade)
icp canister call llama_cpp -e local cache_cleanup_start_timer '()'
# -> (variant { Ok = record { ok = true; is_running = true } })

# Stop the recurring timer
icp canister call llama_cpp -e local cache_cleanup_stop_timer '()'
# -> (variant { Ok = record { ok = true; is_running = false } })

# Trigger one cleanup pass immediately (independent of the timer state)
icp canister call llama_cpp -e local cache_cleanup_now '()'
# -> (variant { Ok = record { runs = ...; files_examined = ...;
#                             files_deleted = ...; files_failed = ...;
#                             last_run_ns = ...; period_seconds = 600;
#                             ttl_seconds = 21_600;
#                             max_files_per_run = 256;
#                             is_running = ... } })

# Inspect stats (query, fast). `runs` and `last_run_ns` are lifetime
# counters; `files_examined`, `files_deleted`, `files_failed` reflect the
# MOST RECENT cleanup run only.
icp canister call llama_cpp -e local get_cache_cleanup_stats '()'

# Adjust config (each field is `opt nat64`; null = no change).
#   - period_seconds: must be > 0; opt 0 is silently rejected.
#   - ttl_seconds   : 0 is valid ("delete every file under sessions/").
#   - max_files_per_run: clamped to [1, 10000].
# If the timer is already running, the new period is applied transparently.
icp canister call llama_cpp -e local set_cache_cleanup_config '(record {
  period_seconds    = opt (300 : nat64);
  ttl_seconds       = opt (3600 : nat64);
  max_files_per_run = opt (128 : nat64)
})'

# Same call to update only the TTL, leaving period and cap unchanged
icp canister call llama_cpp -e local set_cache_cleanup_config '(record {
  period_seconds    = null;
  ttl_seconds       = opt (3600 : nat64);
  max_files_per_run = null
})'
```

# Cycle Balance Monitoring

The canister can track its own cycle balance on a recurring schedule. An
hourly timer refreshes a cached snapshot of the balance (via the IC's
`canister_cycle_balance128` system call), which admins read cheaply through
the `get_cycle_balance` query — no need for a live system call on every check.

**Defaults:**
- period: 3600 seconds (refreshed once per hour)

**Operator-driven lifecycle.** The timer is **not** auto-armed in
`canister_init` or `canister_post_upgrade`. After every install / upgrade you
must explicitly call `cycle_balance_start_timer`. Timer state and the cached
balance are in-memory only and do not survive an upgrade. While tracking is
off, `get_cycle_balance` returns a clear error instead of a stale value.

All endpoints below require **admin role**:
- `cycle_balance_start_timer`, `cycle_balance_stop_timer` need `AdminUpdate`
  role (controller or whitelisted via `assignAdminRole`).
- `get_cycle_balance` needs `AdminQuery` role.

```bash
# ------------------------------------------------------------------
# Turn ON cycle-balance tracking (REQUIRED after every install / upgrade).
# Refreshes the balance once immediately, then re-reads it hourly.
icp canister call llama_cpp -e local cycle_balance_start_timer '()'
# -> (variant { Ok = record { status_code = 200 : nat16 } })

# Read the cached balance (admin query, fast). updated_at_ns is the
# IC_API::time() at which the snapshot was taken.
icp canister call llama_cpp -e local get_cycle_balance '()'
# -> (variant { Ok = record { cycle_balance = ... : nat; updated_at_ns = ... : nat64 } })

# If tracking is OFF, the query returns a clear error instead of a stale value:
# -> (variant { Err = variant { Other = "cycle balance tracking is off — an admin must call cycle_balance_start_timer" } })

# Turn OFF cycle-balance tracking
icp canister call llama_cpp -e local cycle_balance_stop_timer '()'
# -> (variant { Ok = record { status_code = 200 : nat16 } })
```

# Memory Status

Larger models (e.g. Qwen3-0.6B) run close to the canister's wasm memory limit.
The `get_memory_status` query lets you watch usage — the `wasm_heap_bytes` value is
the number that climbs toward the `wasm_memory_limit` and, when it reaches it,
causes `heap out of bounds` (IC0502) traps during `load_model` / generation.

Access: **non-anonymous** callers only (anonymous callers get an access-denied error).

```bash
icp canister call llama_cpp -e local get_memory_status
# ->
(
  variant {
    Ok = record {
      wasm_heap_bytes = 1_758_068_736 : nat64;   # linear-memory high-water-mark (~1.76 GiB at ctx 16384)
      stable_bytes = 1_132_527_616 : nat64;      # model file + virtual filesystem
    }
  },
)
```

The `wasm_memory_limit` itself is set with `icp canister settings update llama_cpp
--wasm-memory-limit 4026531840` (see the setup steps) — it cannot go in `icp.yaml`'s
`initialization_values` when the canister is created through a cycles wallet. Check it
with `icp canister status llama_cpp`. If `wasm_heap_bytes` approaches the limit, lower
`--batch-size`/`--ubatch-size` (biggest win), reduce `--ctx-size`, and/or quantize the KV
cache (`--cache-type-k`/`-v q8_0`) when loading — see [Context size & memory](#appendix-b-context-size--memory).

# Wasm Verification (pre onicai SNS)

Anyone can independently verify that the deployed funnAI LLM canisters run the exact code built from this repo. See [README-wasm-verification.md](README-wasm-verification.md).

# Appendix A: max_tokens

The size and settings for models impact the number of tokens that can be generated
in 1 update call before hitting the instruction limit of the Internet Computer.

The instruction limit is 40 billion instructions per update call

We tested several LLM models available on HuggingFace:

| Model                                                                                     | # weights | file size | quantization | --cache-type-k | max*tokens<br> *(ingestion)\_ | max*tokens<br> *(generation)\_ |
| ----------------------------------------------------------------------------------------- | --------- | --------- | ------------ | -------------- | ----------------------------- | ------------------------------ |
| [Qwen3-0.6B-Q8_0.gguf](https://huggingface.co/Qwen/Qwen3-0.6B-GGUF) (default)             | 600 M     | 0.64 GB   | q8_0         | q8_0           | -                             | 25                             |
| [qwen2.5-0.5b-instruct-q8_0.gguf](https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF) | 630 M     | 0.68 GB   | q8_0         | q8_0           | -                             | 25                             |
| [gemma-3-270m-it-Q8_0.gguf](https://huggingface.co/unsloth/gemma-3-270m-it-GGUF) (tiniest) | 270 M     | 0.29 GB   | q8_0         | q8_0           | -                             | 44                             |

_(We have benchmarked other models too — SmolLM2, Llama-3.2, DeepSeek-R1 1.5B, and other Qwen2.5 quants — but their pre-b10076 numbers must be re-measured before we list them here.)_

NOTEs:

- **`Qwen3-0.6B-Q8_0` is the current default** (top row): ~25 tokens/call generation, first-call ceiling ~25-29. It needs `--cache-type-k q8_0 --cache-type-v q8_0 --batch-size 64 --ubatch-size 64 --ctx-size 16384` and a `wasm_memory_limit` of 3.75 GiB; the small batch shrinks the compute buffers so a 16K context fits with ~2 GiB headroom — see [Context size & memory](#appendix-b-context-size--memory) for the mechanism, levers, and risks.
- **`qwen2.5-0.5b-instruct-q8_0` was re-measured on b10076**: 25 tokens/call sustained, 28 first-call ceiling — up ~2.8x from ~10, thanks to the hand-written WASM SIMD q8_0 kernel.
- **`gemma-3-270m-it-Q8_0` is the smallest model we run on-chain** — the b10076 fork loads the `gemma3` architecture. With a **q8_0 KV cache** the measured **generation ceiling is ~55 tokens/call** (55 OK / 60 traps at short context, on a local replica whose instruction limit is the same 40 B as mainnet; ~50 with the default f16 cache), so `max_tokens_update = 44` is a safe value. Use `--temp 0.7` — greedy decoding can end a turn immediately. Its heap peaks at only ~0.9 GiB after load, so the **default `wasm_memory_limit` is enough** (no 3.75 GiB bump). See [README-gemma-3.md](README-gemma-3.md) for the full recipe.
- During prompt ingestion phase, the max_tokens before hitting the instruction limit is higher as during the generation phase.
- We use `"--temp"; "0.6"; "--repeat-penalty"; "1.1";`, as recommended on several model cards
- For each model, we selected a `--cache-type-k` that gives the highest max_tokens while still providing good results.
- The python notebook [scripts/promt-design.ipynb](./scripts/prompt-design.ipynb) allows you to try out these models w/o using an IC canister, to decide what model will work best for your on-chain AI agent

# Appendix B: Context size & memory

Qwen3-0.6B runs in a **wasm32 linear heap capped by `wasm_memory_limit`** (we set 3.75
GiB — wasm32 cannot address a full 4 GiB). Weights, KV cache, and compute buffers all live
in that heap, so context length is ultimately a memory-budget question. Here is the
mechanism, the levers, and the risks.

### Where the heap goes

- **Weights (~0.64 GiB)** — the q8_0 model, read into the heap at `load_model`.
- **KV cache (scales with context)** — preallocated for the *entire* `--ctx-size` at
  `load_model`, committed whether or not the conversation ever fills it. With dual-q8_0
  caching this is ~50 KB per context token for Qwen3-0.6B (8 KV heads × 28 layers).
- **Compute buffers (scale with batch)** — allocated for the forward pass. Two matter:
  the **output/logits buffer ≈ `batch × vocab`** (at the default `--batch-size 2048` and
  Qwen3's ~152K vocab that alone is **~1.2 GiB**) and the **attention buffer ≈
  `ubatch × ctx`**. With a small batch these become negligible and stop dominating.

### The levers

| Lever | Effect | Cost on the IC |
| --- | --- | --- |
| `--batch-size` / `--ubatch-size` | Shrinks the compute buffers. 2048/512 → **64/64** frees ~2 GiB. | ≈none — a canister serves one request at a time and prefill is already instruction-limited, so large batches buy nothing here. |
| `--cache-type-k` / `--cache-type-v` `q8_0` | Halves the KV cache vs f16. | Negligible quality impact. |
| `--ctx-size` | Sets conversation length **and** the preallocated KV cache. | Longer context = more KV heap. |
| `wasm_memory_limit` | The ceiling itself (≤ 3.75 GiB on wasm32). | Set via `update-settings`. |

With `--batch-size 64 --ubatch-size 64` the compute buffers stop scaling with context, so
the **KV cache is the only thing that grows with `--ctx-size`**. Measured with full prefill
+ multi-turn generation (16384 on mainnet; larger sizes from the local batch-64 sweep):

| `--ctx-size` | ≈ words | wasm heap (peak) | headroom to 3.75 GiB |
| ------------ | ------- | ---------------- | -------------------- |
| 1024 (old default) | ~750 | ~1.0 GiB | ~2.7 GiB |
| **16384** *(default)* | **~12K** | **1.76 GiB** | **~2.0 GiB** |
| 32768 | ~24K | ~2.6 GiB | ~1.1 GiB |
| 40960 (native max) | ~30K | ~3.0 GiB | ~0.7 GiB |

Our default is **`--ctx-size 16384` with `--batch-size 64 --ubatch-size 64`**: ~12,000 words of
conversation (16× the old ctx-1024 default) with a comfortable ~2 GiB safety margin,
verified end-to-end on mainnet. You can push `--ctx-size` toward the native 40960 (~30K
words) if you accept a tighter margin.

### ⚠️ The risk — a memory trap bricks the canister

Memory here is a hard wall, and hitting it is **not** a graceful error. If a `load_model`
or (more likely) an inference call needs to grow the heap past `wasm_memory_limit`, the
canister traps with `heap out of bounds` (IC0502). Recovery requires a
**`icp canister install --mode reinstall`** (which wipes the heap *and* the uploaded model),
then **re-uploading the gguf** and reloading.

Practical guidance:
- Keep real headroom (the default 16384 leaves ~2 GiB) rather than maxing out `--ctx-size`.
- The KV cost is committed at `load_model`, so a load that succeeds with headroom will not
  surprise you mid-conversation. Watch `wasm_heap_bytes` with `get_memory_status`.
- A pre-decode headroom guard (a clean `Err` instead of a trap) is planned hardening; until
  then, treat the memory limit as a wall to stay well clear of.

# Appendix C: heap-out-of-bounds

If you encounter a `heap out of bounds` error during `load_model`, this is likely a stack overflow in disguise. The WebAssembly runtime cannot distinguish between stack and heap memory violations.

**Cause:** Clang's default stack size for wasm32 is very small (~64KB), which can be exhausted when parsing large GGUF files with many tokenizer entries.

**Solution:** Increase the WASM stack size in `icpp.toml`. This repo uses 8MB:

```toml
cpp_link_flags = ["-Wl,-z,stack-size=8388608"]  # 8MB stack
```

For larger models, increase further (e.g., 16MB: `16777216`, 32MB: `33554432`).

Then rebuild with `icpp build-wasm`.

**Reference:** [DFINITY Forum discussion](https://forum.dfinity.org/t/heap-out-of-bounds-error-code-some-ic0502-on-c-code-run/25289)

# Acknowledgments

The b10076 upgrade — recovering ~2.8x generation throughput for Q8_0 models — was motivated and informed by the work of **Julien Aerni** (Meotis Sàrl), **Siméon Fluck** (Kaizen Corp SA), and **Dustin Becker** (ORIGYN Foundation):

- Their [forum analysis](https://forum.dfinity.org/t/on-chain-llm-inference-under-instruction-budgets-measured-live-on-icp-mainnet/74709) diagnosed that the previous build ran the `ggml_vec_dot_q8_0_q8_0` matmul kernel without a hand-written WASM SIMD path — the deficiency this upgrade fixes.
- We adapted their **WASI shim strategy** (no-op `<thread>`/`<mutex>`/`<future>`/`<condition_variable>` plus exception/dl stubs) for building recent llama.cpp on ICP, replacing the earlier per-file patching approach.

Their preprint: _On-Chain LLM Inference Under Instruction Budgets: An Instruction-Budget Cost Model, Ternary Floor Evidence, and Session Costs_ (2026), DOI [10.5281/zenodo.20607598](https://doi.org/10.5281/zenodo.20607598). The companion artifact is MIT-licensed.
