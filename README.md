[![llama_cpp_canister](https://github.com/onicai/llama_cpp_canister/actions/workflows/cicd-mac.yml/badge.svg)](https://github.com/onicai/llama_cpp_canister/actions/workflows/cicd-mac.yml)

# llama.cpp for the Internet Computer.

![llama](https://user-images.githubusercontent.com/1991296/230134379-7181e485-c521-4d23-a0d6-f7b3b61ba524.png)

`llama_cpp_canister` allows you to deploy [ggml-org/llama.cpp](https://github.com/ggml-org/llama.cpp) as a Smart Contract on the Internet Computer,
and run an LLM on-chain as the brain for your on-chain AI Agents.

- Run any LLM on-chain via the gguf format 🔥
- Solves your cybersecurity problem 🔐
- MIT open source 🧑‍💻
- Well documented 📝
- Fully QA'd via CI/CD ✅
- Easy to build, test & deploy 🚧
- Smoke testing framework using pytest 🚬

# In the wild

llama_cpp_canister is used as the on-chain LLM brain inside the following projects:

_(Issue a PR to get your project listed)_

| Project    | Description/Tagline                                                 | GitHub                                                 | X                                                 | Token                                      | URL                                               |
| ---------- | ------------------------------------------------------------------- | ------------------------------------------------------ | ------------------------------------------------- | ------------------------------------------ | ------------------------------------------------- |
| funnAI     | The next-generation AI ecosystem | -     | [@onicaiHQ](https://x.com/onicaiHQ) | $FUNNAI | [funnai.onicai.com](https://funnai.onicai.com) |
| IConfucius | Wisdom fueled by Cycles<br>_Fully on chain AI tokenized on Bitcoin_ | [IConfucius](https://github.com/onicai/IConfucius)     | [@IConfucius_odin](https://x.com/IConfucius_odin) | [$ICONFUCIUS](https://odin.fun/token/29m8) | [onicai.com](https://www.onicai.com/#/iconfucius) |
| Forseti    | Norse God of Justice<br>_Fully on chain AI tokenized on Bitcoin_    | [forseti.fun](https://github.com/forria64/forseti.fun) | [@forsetidotfun](https://x.com/forsetidotfun)     | [$FORSETI](https://odin.fun/token/2dld)    | [forseti.fun](https://forseti.fun)                |
| ICGPT      | on-chain LLMs<br>_Chat with on-chain LLMs_                          | [icgpt](https://github.com/icppWorld/icgpt)            | [@onicaiHQ](https://x.com/onicaiHQ)               | -                                          | [icgpt.onicai.com](https://icgpt.onicai.com)      |

# Need help or have feedback? ❤️

- [OpenChat C++ community](https://oc.app/community/cklkv-3aaaa-aaaar-ar7uq-cai/?ref=6e3y2-4yaaa-aaaaf-araya-cai)
- [Forum: Llama.cpp on the Internet Computer](https://forum.dfinity.org/t/llama-cpp-on-the-internet-computer/33471?u=icpp)

# Capabilities 🔥

- Deploy any LLM available as a gguf file.

  _(The model must be able to produce at least 1 token per update call)_

- Our largest so far is DeepSeek-R1 1.5B (See [X](https://x.com/onicaiHQ/status/1884339580851151089)).

# Using the release

You can just grab the latest [release](https://github.com/onicai/llama_cpp_canister/releases) and unzip it. Then follow the instructions from the README, but skip these steps:

- No need to clone the repo. Everything is included in the release zip
- No need to build the wasm. It is included in the release zip.

# Set up

- Install dfx:

  ```bash
  sh -ci "$(curl -fsSL https://internetcomputer.org/install.sh)"

  # Configure your shell
  source "$HOME/.local/share/dfx/env"
  ```

- Clone the repo and it's children:

  _(skip when using the [release](https://github.com/onicai/llama_cpp_canister/releases))_

  ```bash
  # Clone this repo
  git clone git@github.com:onicai/llama_cpp_canister.git

  # Clone llama_cpp_onicai_fork, our forked version of llama.cpp
  # Into the ./src folder
  cd src
  git clone git@github.com:onicai/llama_cpp_onicai_fork.git
  ```

- Create a Python environment with dependencies installed

  ❗❗❗ Use Python 3.11 _(This is needed for binaryen.py dependency)_ ❗❗❗

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
    dfx start --clean
    ```

  - Deploy the wasm to a canister on the local network:

    ```bash
    dfx deploy

    # When upgrading the code in the canister, use:
    dfx deploy -m upgrade
    ```

  - Check the health endpoint of the `llama_cpp` canister:
    ```bash
    $ dfx canister call llama_cpp health
    (variant { Ok = record { status_code = 200 : nat16 } })
    ```

- Ensure the canister has enough cycles

  The LLM upload and inference calls consume cycles. You can add with:

  ```bash
  # Add 20 trillion cycles
  dfx ledger fabricate-cycles --canister llama_cpp --t 20
  ```

- Upload gguf file

  The canister is now up & running, and ready to be loaded with a gguf file. In
  this example we use the powerful `qwen2.5-0.5b-instruct-q8_0.gguf` model, but
  you can use any model availabe in gguf format.

  - Download the model from huggingface: https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF

    Store it in: `models/Qwen/Qwen2.5-0.5B-Instruct-GGUF/qwen2.5-0.5b-instruct-q8_0.gguf`

    ```bash
    mkdir -p models/Qwen/Qwen2.5-0.5B-Instruct-GGUF
    wget -c \
      -O models/Qwen/Qwen2.5-0.5B-Instruct-GGUF/qwen2.5-0.5b-instruct-q8_0.gguf \
      https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/resolve/main/qwen2.5-0.5b-instruct-q8_0.gguf
    ```

    After download, verify the sha256 hash:

    ```bash
    $ sha256sum models/Qwen/Qwen2.5-0.5B-Instruct-GGUF/qwen2.5-0.5b-instruct-q8_0.gguf
    ca59ca7f13d0e15a8cfa77bd17e65d24f6844b554a7b6c12e07a5f89ff76844e
    ```

  - Upload the gguf file to the canister:

    ```bash
    python -m scripts.upload \
      --network local \
      --canister llama_cpp \
      --canister-filename models/model.gguf \
      --filetype gguf \
      --hf-sha256 "ca59ca7f13d0e15a8cfa77bd17e65d24f6844b554a7b6c12e07a5f89ff76844e" \
      models/Qwen/Qwen2.5-0.5B-Instruct-GGUF/qwen2.5-0.5b-instruct-q8_0.gguf
    ```

    NOTEs:

    - In C++, files are stored in stable memory of the canister. They will survive a code upgrade.
    - The --hf-sha256 argument is optional but highly recommended:
      - The upload process will check if the file on disk has the same sha256 as the one you downloaded from HuggingFace.
      - The --hf-sha256 for our sample model can be found at https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/blob/main/qwen2.5-0.5b-instruct-q8_0.gguf

  - Check the filesize & sha256 of the uploaded gguf file in the canister

    ```bash
    dfx canister call llama_cpp uploaded_file_details '(record {
      filename = "models/model.gguf"
    })'

    # Which returns the following for the Qwen2.5-0.5B-Instruct-GGUF model
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

- Optional: You can now run a pytest based QA, using the icpp-pro smoketesting framework:

  ```bash
  pytest -vv test/test_qwen2.py
  ```

- Load the gguf file into Orthogonal Persisted (OP) working memory

  ```bash
  dfx canister call llama_cpp load_model '(record {
    args = vec {
      "--model"; "models/model.gguf";
      "--cache-type-k"; "q8_0";
    }
  })'
  ```

- Set the max_tokens for this model, to avoid it hits the IC's instruction limit

  _(See Appendix A for values of others models.)_

  ```bash
  dfx canister call llama_cpp set_max_tokens '(record {
    max_tokens_query = 1 : nat64;
    max_tokens_update = 13 : nat64
  })'

  dfx canister call llama_cpp get_max_tokens
  ```

- Chat with the LLM

  - Ensure the canister is ready for Inference, with the model loaded

    ```bash
    dfx canister call llama_cpp ready
    ```

  - Chat with the LLM:

    Details how to use the Qwen models with llama.cpp:
    https://qwen.readthedocs.io/en/latest/run_locally/llama.cpp.html

    Start a new chat

    ```bash
    dfx canister call llama_cpp new_chat '(record {
      args = vec {
        "--prompt-cache"; "prompt.cache";
        "--cache-type-k"; "q8_0";
      }
    })'
    ```

    ***

    Ingest the prompt:

    Repeat this call until `prompt_remaining` in the response is empty.
    This ingest the prompt into the prompt-cache, using multiple update calls:
    (-) Keep sending the full prompt
    (-) Use `"-n"; "1"`, so it does not generate new tokens

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

    ***

    Generate new tokens:

    Once `prompt_remaining` in the response is empty.
    (-) repeat this call, until `generated_eog=true`
    (-) Use an empty prompt: `"-p"; "";`
    (-) Use `"-n"; "512"`, so it will now generate new tokens

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

    ***

    Once `generated_eog` in the response is `true`, the LLM is done generating

    This is the response after several update calls and it has reached eog:

    ```bash
    (
      variant {
        Ok = record {
          output = " level of complexity than the original text.<|im_end|>";
          conversation = "<|im_start|>system\nYou are a helpful assistant.<|im_end|>\n<|im_start|>user\ngive me a short introduction to LLMs.<|im_end|>\n<|im_start|>assistant\nLLMs are large language models, or generative models, that can generate text based on a given input. These models are trained on a large corpus of text and are able to generate text that is similar to the input. They can be used for a wide range of applications, such as language translation, question answering, and text generation for various tasks. LLMs are often referred to as \"artificial general intelligence\" because they can generate text that is not only similar to the input but also has a higher level of complexity than the original text.<|im_end|>";
          error = "";
          status_code = 200 : nat16;
          prompt_remaining = "";
          generated_eog = true;
        }
      },
    )
    ```

    ***

    Remove the prompt cache when done - this keeps stable memory usage at a minimum

    ```bash
    dfx canister call llama_cpp remove_prompt_cache '(record {
      args = vec {
        "--prompt-cache"; "prompt.cache"
      }
    })'
    ```

    Note: The sequence of update calls to the canister is required because the Internet Computer has a limitation
    on the number of instructions it allows per call. For this model, 13 tokens can be generated per update call.

    This sequence of update calls is equivalent to using the [ggml-org/llama.cpp](https://github.com/ggml-org/llama.cpp)
    repo directly and running the `llama-cli` locally, with the command:

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

  - Saving the chats

    This functionality is off by default. You can turn it on/off with:

    ```bash
    dfx canister call llama_cpp chats_resume
    dfx canister call llama_cpp chats_pause
    ```

    When on, up to 3 chats per principal are saved.
    The `get_chats` method retrieves them for the principal of the caller.

    ```
    dfx canister call llama_cpp get_chats
    ```

# log_pause & log_resume

The llama.cpp code is quite verbose. In llama_cpp_canister, you can
turn the logging off and back on with these commands:

```bash
# turn off logging
dfx canister call llama_cpp log_pause

# turn on logging
dfx canister call llama_cpp log_resume
```

# Logging to a file

For debug purposes, you can tell the canister to log to a file and download it afterwards.

Pass `"--log-file"; "main.log";` to each `run_update` calls.

Afterwards, you can download the `main.log` file from the canister with:

```bash
python -m scripts.download \
  --network local \
  --canister llama_cpp \
  --local-filename main.log main.log
```

You can cleanup by deleting both the log & prompt.cache files in the canister:

```bash
dfx canister call llama_cpp remove_prompt_cache '(record {
  args = vec {
    "--prompt-cache"; "prompt.cache"
    }
})'

dfx canister call llama_cpp remove_log_file '(record {
  args = vec {
    "--log-file"; "main.log"
  }
})'
```

# Smoke testing the deployed LLM

You can run a smoketest on the deployed LLM:

- Deploy Qwen2.5 model as described above

- Run the smoketests for the Qwen2.5 LLM deployed to your local IC network:

  ```
  # First test the canister functions, like 'health'
  pytest -vv test/test_canister_functions.py

  # Then run the inference tests
  pytest -vv test/test_qwen2.py
  ```

# Prompt Caching

When a prompt cache file is already present, llama_cpp_canister automatically applies prompt caching to reduce latency and cost.

All repetitive content at the beginning of the prompt does not need to be processed by the LLM, so make sure to design your AI agent prompts such that repetitive content is placed at the beginning.

Each caller of the llama_cpp_canister has it's own cache folder, and has the following endpoints available to manage their prompt-cache files:

```bash
# Remove a prompt cache file from the caller's cache folder
dfx canister call llama_cpp remove_prompt_cache '(record {
  args = vec {
    "--prompt-cache"; "prompt.cache"
  }
})'

# Copy a prompt cache file within the caller's cache folder
dfx canister call llama_cpp copy_prompt_cache '(record {
  from = "prompt.cache";
  to = "prompt-save.cache"
})'

# ------------------------------------------------------------------
# Download a chunk of a prompt cache file
# Note: chunksize of 5 bytes is for demo only.
#       -> use 200_000 or higher (2_000_000 max) in actual download
#       -> experiment what chunksize results in fastest download
dfx canister call llama_cpp download_prompt_cache_chunk '(record {
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
dfx canister call llama_cpp download_prompt_cache_chunk '(record {
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
    --network local \
    --canister llama_cpp \
    --filetype promptcache \
    --chunksize 2000000 \
    prompt.cache

# ------------------------------------------------------------------
# Upload a chunk of a prompt cache file
# Note: chunksize of 5 bytes is for demo only.
#       -> use 200_000 or higher (2_000_000 max) in actual download
#       -> experiment what chunksize results in fastest download
dfx canister call llama_cpp upload_prompt_cache_chunk '(record {
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
dfx canister call llama_cpp upload_prompt_cache_chunk '(record {
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
dfx canister call llama_cpp uploaded_prompt_cache_details '(record {
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
    --network local \
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
dfx canister call llama_cpp set_access '(record {
  level = 1 : nat16
})'

# Verify it worked
dfx canister call llama_cpp get_access

# A caller can check it's access rights with
dfx canister call llama_cpp check_access
```

# File Management

A C++ canister on the Internet Computer uses a virtual file system, stored in Stable Memory.

Several endpoints are exposed that allows you to interact & manage the files.

For example, you can explore what is stored in the `.canister_cache` folder:

```bash
# Query call to list all files & directories in a folder
dfx canister call llama_cpp recursive_dir_content_query '(record {dir = ".canister_cache"; max_entries = 0 : nat64})' --output json
# Update call in case you hit the instruction limit
dfx canister call llama_cpp recursive_dir_content_update '(record {dir = ".canister_cache"; max_entries = 0 : nat64})' --output json
# Update call to get eg. the first 5000 entries, in case you still hit the instruction limit
dfx canister call llama_cpp recursive_dir_content_update '(record {dir = ".canister_cache"; max_entries = 5000 : nat64})' --output json

# Get the size of a file in bytes
dfx canister call llama_cpp filesystem_file_size '(record {filename = "<filename>"})' --output json

# Get the creation timestamp of a file in nanoseconds (also returns age of file in seconds)
dfx canister call llama_cpp get_creation_timestamp_ns '(record {filename = "<filename>"})' --output json

# remove a file or empty directory
dfx canister call llama_cpp filesystem_remove '(record {filename = "<filename>"})'
```

# Appendix A: max_tokens

The size and settings for models impact the number of tokens that can be generated
in 1 update call before hitting the instruction limit of the Internet Computer.

The instruction limit is 40 billion instructions per update call

We tested several LLM models available on HuggingFace:

| Model                                                                                                                    | # weights | file size | quantization   | --cache-type-k | max*tokens<br> *(ingestion)\_ | max*tokens<br> *(generation)\_ |
| ------------------------------------------------------------------------------------------------------------------------ | --------- | --------- | -------------- | -------------- | ----------------------------- | ------------------------------ |
| [SmolLM2-135M-Instruct-Q8_0.gguf](https://huggingface.co/tensorblock/SmolLM2-135M-Instruct-GGUF)                         | 135 M     | 0.15 GB   | q8_0           | f16            | -                             | 40                             |
| [qwen2.5-0.5b-instruct-q4_k_m.gguf](https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF)                              | 630 M     | 0.49 GB   | q4_k_m         | f16            | -                             | 14                             |
| [qwen2.5-0.5b-instruct-q8_0.gguf](https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF)                                | 630 M     | 0.68 GB   | q8_0           | q8_0           | -                             | 13                             |
| [Llama-3.2-1B-Instruct-Q4_K_M.gguf](https://huggingface.co/unsloth/Llama-3.2-1B-Instruct-GGUF)                           | 1.24 B    | 0.81 GB   | q4_k_m         | q5_0           | 5                             | 4                              |
| [qwen2.5-1.5b-instruct-q4_k_m.gguf](https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF)                              | 1.78 B    | 1.10 GB   | q4_k_m         | q8_0           | -                             | 3                              |
| [DeepSeek-R1-Distill-Qwen-1.5B-NexaQuant.gguf](https://huggingface.co/NexaAIDev/DeepSeek-R1-Distill-Qwen-1.5B-NexaQuant) | 1.78 B    | 1.34 GB   | NexaQuant-4Bit | f16            | 4                             | 3                              |
| [DeepSeek-R1-Distill-Qwen-1.5B-Q6_K.gguf](https://huggingface.co/unsloth/DeepSeek-R1-Distill-Qwen-1.5B-GGUF)             | 1.78 B    | 1.46 GB   | q6_k           | q8_0           | 4                             | 3                              |
| [DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_M.gguf](https://huggingface.co/unsloth/DeepSeek-R1-Distill-Qwen-1.5B-GGUF)           | 1.78 B    | 1.12 GB   | q4_k_m         | q8_0           | 4                             | 3                              |
| [DeepSeek-R1-Distill-Qwen-1.5B-Q2_K.gguf](https://huggingface.co/unsloth/DeepSeek-R1-Distill-Qwen-1.5B-GGUF)             | 1.78 B    | 0.75 GB   | q2_k           | q8_0           | 2                             | 2                              |

NOTEs:

- During prompt ingestion phase, the max_tokens before hitting the instruction limit is higher as during the generation phase.
- We use `"--temp"; "0.6"; "--repeat-penalty"; "1.1";`, as recommended on several model cards
- For each model, we selected a `--cache-type-k` that gives the highest max_tokens while still providing good results.
- The python notebook [scripts/promt-design.ipynb](./scripts/prompt-design.ipynb) allows you to try out these models w/o using an IC canister, to decide what model will work best for your on-chain AI agent

# Appendix B: heap-out-of-bounds

If you encounter a `heap out of bounds` error during `load_model`, this is likely a stack overflow in disguise. The WebAssembly runtime cannot distinguish between stack and heap memory violations.

**Cause:** Clang's default stack size for wasm32 is very small (~64KB), which can be exhausted when parsing large GGUF files with many tokenizer entries.

**Solution:** Increase the WASM stack size in `icpp.toml`. This repo uses 8MB:

```toml
cpp_link_flags = ["-Wl,-z,stack-size=8388608"]  # 8MB stack
```

For larger models, increase further (e.g., 16MB: `16777216`, 32MB: `33554432`).

Then rebuild with `icpp build-wasm`.

**Reference:** [DFINITY Forum discussion](https://forum.dfinity.org/t/heap-out-of-bounds-error-code-some-ic0502-on-c-code-run/25289)
