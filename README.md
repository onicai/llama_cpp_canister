[![llama_cpp_canister](https://github.com/onicai/llama_cpp_canister/actions/workflows/cicd-mac.yml/badge.svg)](https://github.com/onicai/llama_cpp_canister/actions/workflows/cicd-mac.yml)

# llama.cpp for the Internet Computer

![llama](https://user-images.githubusercontent.com/1991296/230134379-7181e485-c521-4d23-a0d6-f7b3b61ba524.png)

`llama_cpp_canister` lets you deploy [ggml-org/llama.cpp](https://github.com/ggml-org/llama.cpp) as a Smart Contract on the Internet Computer and run an LLM fully on-chain — the verifiable brain for your on-chain AI agents. Our recommended default is **Qwen3-0.6B**, run in non-thinking mode for **multi-turn conversations** with up to a ~12K-word context.

# Capabilities 🔥

- **Run any LLM on-chain** via the gguf format — we recommend **Qwen3-0.6B** ([Qwen/Qwen3-0.6B-GGUF](https://huggingface.co/Qwen/Qwen3-0.6B-GGUF)).
- **Multi-turn, verifiable inference** — long back-and-forth conversations, entirely on-chain.
- MIT open source 🧑‍💻 · Well documented 📝 · Fully QA'd via CI/CD ✅
- Easy to build, test & deploy 🚧 · Smoke-testing framework using pytest 🚬

# Model guides

Each model has its own guide in [docs/](docs/): how to get, upload and load it, its
measured cost and limits, and what it is good for.

- **Chat models:**
  - Qwen3-0.6B, the recommended default: this README.
  - [Qwen2.5-0.5B-Instruct](docs/README-qwen2.5.md), the previous default.
  - [Qwen3-1.7B](docs/README-qwen3-1.7B.md), an optional larger model.
  - [LFM2.5-1.2B-Instruct](docs/README-LFM2.5-1.2B.md), a faster replacement for Qwen3-1.7B.
  - [LFM2.5-2.6B](docs/README-LFM2.5-2.6B.md), an on-device agent model.
  - [Gemma-3-270M](docs/README-gemma-3.md), the smallest on-chain model.
  - [Hindi](docs/README-Language-Hindi.md): which model to use.
- **Decision models (`run_decision`, `run_decision_full`):**
  - [Decision models](docs/README-decision-models.md): what they are, and the interface.
  - [Julia-1](docs/README-decision-model-julia-1.md),
    [Laya](docs/README-decision-model-Laya.md),
    [Kev-0.8B](docs/README-decision-model-kev-0.8b.md).
- **Sequence classifiers (`run_classify`, `run_classify_query`):**
  - [Sequence classifiers](docs/README-classifier-models.md), with the DAO-moderation example.
  - [Train your own classifier](docs/README-classifier-training.md).

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

- No need to clone the repo. The zip has what the steps below need: the wasm, `icp.yaml`, `requirements.txt`, the upload, download and log-monitor scripts, the `test/` smoke tests, the model guides in `docs/`, and the classifier example in `examples/classifier/` (its GGUF conversion needs a llama.cpp checkout, see `docs/README-classifier-training.md`).
- No need to build the wasm. It is included in the release zip.

Each release has notes on its GitHub page: what changed, what an app must change, and the commits since the previous release. The summary part is also in `docs/release-notes/` in the zip and the repository.

The build files (`Makefile` and its `make ...` targets, `docker/`, `icpp.toml`, the C++ sources) and the contributor guides are not in the zip. They are in the [repository](https://github.com/onicai/llama_cpp_canister).

# Set up

- Install icp-cli (and ic-wasm). Requires [Node.js](https://nodejs.org/) >= 22:

  ```bash
  npm install -g @icp-sdk/icp-cli @icp-sdk/ic-wasm

  # Verify it is installed
  icp --version
  ```

  > **Note:** icp-cli replaces the deprecated `dfx`. This project was tested
  > with **icp-cli 1.7.0**.

- Create an icp identity to deploy, upload and test with, and make it your default:

  ```bash
  icp identity new llama-cpp-dev --storage plaintext   # once
  icp identity default llama-cpp-dev
  ```

  All steps below run as your default identity, and they must all run as the
  **same** one: the canister's controller is whoever deployed it, and most
  endpoints are controller-only. It cannot be `anonymous` (the default on a fresh
  icp-cli install): the upload script and pytest sign their calls locally by
  exporting the key, and the anonymous identity has no key to export. For the
  same reason it must not be password protected. A plaintext key is unencrypted
  on disk, so use this identity for local development only.

- Clone the repo and its children:

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

  - Reproducible build (what a release ships) — needs only Docker:

    ```bash
    make docker-build-base   # once; the pinned toolchain image
    make docker-build-wasm   # builds out/llama_cpp.wasm and prints its sha256
    ```

    This builds inside a pinned `linux/amd64` image, so the same commit produces
    the same sha256 on any machine. It also copies the hashed artifact into
    `build/`, which is where `icp deploy` picks it up. The llama.cpp fork commit
    is pinned in [`version_fork.env`](version_fork.env), and the toolchain
    versions in [`docker/docker-compose.yml`](docker/docker-compose.yml).

    On Apple Silicon this runs under emulation and is slow. That is deliberate:
    pinning the platform is what makes the hash match everywhere.

  - Local development build — faster, but *not* the reproducible one:

    ```bash
    make build-info-cpp-wasm
    icpp build-wasm
    ```

    Notes:

    - This path needs a local toolchain (`icpp install-wasi-sdk`,
      `icpp install-rust`). It works on macOS and on Linux x86_64 with glibc
      2.35 or newer (icpp-binaryen publishes a `manylinux_2_35_x86_64` wheel
      and no sdist).
    - Its output will not have the same hash as a release, because the absolute
      source paths differ (they are baked in via `GGML_ABORT`/`__FILE__`). Use
      the Docker build whenever the hash matters.
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

    `icp.yaml` defines four canisters that share the same wasm: **`llama_cpp`** (the
    default, serving **Qwen3-0.6B** — the steps below), **`llama_cpp_qwen25`** (the
    previous default, **Qwen2.5-0.5B** — see [docs/README-qwen2.5.md](docs/README-qwen2.5.md)),
    **`llama_cpp_qwen3_17b`** (the larger **Qwen3-1.7B** — see
    [docs/README-qwen3-1.7B.md](docs/README-qwen3-1.7B.md)), and **`llama_cpp_kev`** (the
    **Kev-0.8B** decision model — see
    [docs/README-decision-model-kev-0.8b.md](docs/README-decision-model-kev-0.8b.md)).
    Deploy just the one you need by naming it:

    ```bash
    icp deploy llama_cpp -e local -y

    # When upgrading the code in the canister, use:
    icp deploy llama_cpp -m upgrade -e local -y
    ```

    An upgrade keeps the files in stable memory (the uploaded model, prompt caches)
    and the access settings, but not the loaded model or the max_tokens. After an
    upgrade, `ready` returns "Model not yet loaded" until you call `load_model`
    again, and both max_tokens values are back at their default (0 = no cap), so
    call `set_max_tokens` again if you use other values.

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

  _(For the previous default, Qwen2.5-0.5B, see [docs/README-qwen2.5.md](docs/README-qwen2.5.md).)_

  _(For a larger, stronger model — noticeably better at instruction-following,
  negation, and secret-keeping — see [docs/README-qwen3-1.7B.md](docs/README-qwen3-1.7B.md).)_

  - Download the model from huggingface: https://huggingface.co/Qwen/Qwen3-0.6B-GGUF

    Store it in: `models/Qwen/Qwen3-0.6B-GGUF/Qwen3-0.6B-Q8_0.gguf`

    ```bash
    mkdir -p models/Qwen/Qwen3-0.6B-GGUF
    curl -L -C - \
      -o models/Qwen/Qwen3-0.6B-GGUF/Qwen3-0.6B-Q8_0.gguf \
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
      --network local \
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
  pytest -vv --network local --identity "$(icp identity default)" test/test_qwen3.py
  ```

  `--identity` must name the identity that deployed the canister;
  `$(icp identity default)` is that identity only if you deployed as your default
  identity. See [Smoke testing the deployed LLM](#smoke-testing-the-deployed-llm).

  It takes ~5 minutes. It loads the model and sets max_tokens itself (the next two
  steps set them again), and it leaves prompt-cache files behind, among them
  `prompt.cache`. The chat steps below then report cached tokens, unless you first
  remove `prompt.cache` with `remove_prompt_cache`.

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
  conversation) at ~1.76 GiB heap, ~2 GiB under the 3.75 GiB `wasm_memory_limit`. A
  second `load_model` raises the heap to ~2.30 GiB, because the heap does not shrink;
  further reloads stay there. See
  [Context size & memory](#appendix-b-context-size--memory) for the full mechanism, levers, and
  **risks**. This assumes you raised the `wasm_memory_limit` to 3.75 GiB (see the
  `icp canister settings update` step above); watch live usage with the `get_memory_status` query.

- Set the max_tokens for this model: `0` means no cap

  ```bash
  icp canister call llama_cpp -e local set_max_tokens '(record {
    max_tokens_query = 1 : nat64;
    max_tokens_update = 0 : nat64
  })'

  icp canister call llama_cpp -e local get_max_tokens '()'
  ```

  With `max_tokens_update = 0`, every update call, and every chunk of a
  `run_update_full`, does as many tokens as fit: it stops by itself before it would use
  more than 36B instructions (the IC's limit is 40B). For Qwen3-0.6B that is ~20 tokens
  per call. _(See Appendix A for other models.)_

- Chat with the LLM

  - Ensure the canister is ready for Inference, with the model loaded

    ```bash
    icp canister call llama_cpp -e local ready '()'
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

    `new_chat` re-uses an existing `prompt.cache`. For a fresh start, first remove it
    with `remove_prompt_cache` (shown below). The output below, with
    `n_prompt_tokens_cached = opt (0 : nat64)`, assumes there was no earlier cache.

    ***

    Ingest the prompt and generate the whole response, in one call:

    (-) The assistant turn ends with the empty `<think>\n\n</think>\n\n` block, so
        Qwen3 runs in non-thinking mode
    (-) `"-n"; "512"` is the most tokens the whole response may have

    ```bash
    icp canister call llama_cpp -e local run_update_full '(record {
      args = vec {
        "--prompt-cache"; "prompt.cache"; "--prompt-cache-all";
        "--cache-type-k"; "q8_0"; "--cache-type-v"; "q8_0";
        "--repeat-penalty"; "1.1";
        "--temp"; "0.6";
        "-sp";
        "-p"; "<|im_start|>user\ngive me a short introduction to LLMs.<|im_end|>\n<|im_start|>assistant\n<think>\n\n</think>\n\n";
        "-n"; "512"
      }
    })'
    ```

    It returns after ~15s on a local network (5 chunks). The `output` is the whole
    response, with **no `<think>` tokens**:

    ```bash
    (
      variant {
        Ok = record {
          n_prompt_tokens = opt (22 : nat64);
          output = "LLMs (Large Language Models) are advanced artificial intelligence systems designed to understand, generate, and interact with text in a human-like way. They can analyze language patterns, perform tasks like summarization, translation, or answering questions, making them versatile for various applications across different industries.<|im_end|>";
          n_tokens_generated = opt (57 : nat64);
          conversation = "<|im_start|>user\ngive me a short introduction to LLMs.<|im_end|>\n<|im_start|>assistant\n<think>\n\n</think>\n\nLLMs (Large Language Models) are advanced artificial intelligence systems designed to understand, generate, and interact with text in a human-like way. They can analyze language patterns, perform tasks like summarization, translation, or answering questions, making them versatile for various applications across different industries.<|im_end|>";
          error = "";
          n_prompt_tokens_remaining = opt (0 : nat64);
          n_prompt_tokens_cached = opt (0 : nat64);
          status_code = 200 : nat16;
          prompt_remaining = "";
          generated_eog = true;
          partial = opt false;
          n_prompt_tokens_decoded = opt (22 : nat64);
        }
      },
    )
    ```

    How it works and what to expect:

    - **Chunks:** the IC limits the instructions of one message, so the call works in
      chunks. After each chunk it yields to itself (a self-call), which commits the
      chunk, session file included, and continues in a new message with a fresh
      instruction limit. Other calls can run between two chunks; every chunk reloads
      the caller's session file.
    - **Same output, same cost:** it gives the same output as the `run_update` loop
      below at the same chunk size, and uses the same cycles. Each chunk is one
      consensus round, so a response takes about as long as the loop: ~16s per 64
      tokens on a local replica, ~60s per 64 tokens on mainnet. The reply only comes
      when the whole response is done, so give the client a timeout that covers all of
      it: several minutes for `-n 512` on mainnet.
    - **`-n`** is the total for the whole response.
    - **`n_*` counts** cover the whole call. A response that ends on `-n` counts its
      last sampled token.
    - **409:** a prompt cache is held while its `run_update_full` runs. On that prompt
      cache, another `run_update_full`, `run_update`, `new_chat` or `remove_prompt_cache`
      gets status 409, and `copy_prompt_cache` (to it) and `upload_prompt_cache_chunk`
      return an error. While any `run_update_full` or `run_decision_full` runs,
      `load_model` and a call with `--model` also get 409: the model cannot change
      under it.
    - **A trapped call:** if a chunk traps after a yield, its prompt cache stays held
      until 10 minutes without progress have passed. The canister log then shows
      `busy - expired <session> after <n>s without progress`, and every refused call
      logs `<endpoint> - refused: ...`.
    - **`partial = opt true`:** the call stopped early, because the canister is
      stopping (`icp canister stop`, or an upgrade) or a runaway cap of 500 chunks was
      reached. The reply holds what was generated; continue it with `run_update` and
      an empty prompt. A complete reply has `partial = opt false`; other replies have
      `partial = null`.

    ***

    **Exact token accounting.** Every `run_update` / `run_update_full` / `run_query`
    **success** record also carries five `opt nat64` fields with the exact token counts
    (they decode to `null` on non-run records such as `new_chat` / `load_model`):

    | Field                       | Meaning                                                      |
    | --------------------------- | ------------------------------------------------------------ |
    | `n_prompt_tokens`           | total prompt tokens presented                                |
    | `n_prompt_tokens_cached`    | prompt-cache prefix reused for free (the cache-break offset) |
    | `n_prompt_tokens_decoded`   | prompt tokens actually decoded                               |
    | `n_tokens_generated`        | tokens generated                                             |
    | `n_prompt_tokens_remaining` | prompt suffix left for the next call                         |

    They always reconcile: `cached + decoded + remaining == n_prompt_tokens`, and
    `n_tokens_generated` is `0` until the prompt is fully ingested
    (`n_prompt_tokens_remaining == 0`). This lets a caller measure the exact
    ingest-vs-generate split and the prompt-cache break offset. Being `opt`, they are
    upgrade-safe: a client built against the older `.did` simply ignores them.

    ***

    **Multi-turn conversation.** Qwen3 handles back-and-forth conversations well. To
    continue a chat, send the **full accumulated conversation** as the prompt each
    turn — the prompt-cache reuses the prefix it shares with the cached
    conversation, so only the new part is processed. The example below is a new
    conversation, with its own turn 1 written out in the prompt; it does not continue
    the one above, so it reuses only the few tokens it shares with `prompt.cache`.
    Turn 2 asks the model to recall facts stated in turn 1:

    ```bash
    icp canister call llama_cpp -e local run_update_full '(record {
      args = vec {
        "--prompt-cache"; "prompt.cache"; "--prompt-cache-all";
        "--cache-type-k"; "q8_0"; "--cache-type-v"; "q8_0";
        "--temp"; "0.6"; "-sp";
        "-p"; "<|im_start|>user\nMy name is Sam and I have 3 cats named Milo, Coco, and Ziggy.<|im_end|>\n<|im_start|>assistant\nNice to meet you, Sam! You have three cats: Milo, Coco, and Ziggy.<|im_end|>\n<|im_start|>user\nHow many cats do I have, what are their names, and what is my name?<|im_end|>\n<|im_start|>assistant\n<think>\n\n</think>\n\n";
        "-n"; "512"
      }
    })'
    ```

    The assistant correctly recalls turn 1:

    ```
    - **Number of cats**: 3
    - **Names**: Milo, Coco, and Ziggy
    - **Your name**: Sam
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

    ***

    **Show text while it is being generated: the `run_update` loop.** `run_update_full`
    replies once, at the end. To show the text as it is generated, drive the chunks
    yourself with `run_update`, one update call per chunk:

    1. Ingest the prompt: repeat this call, with the full prompt and `"-n"; "1"`, until
       `prompt_remaining` in the response is empty.

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

    2. Generate: repeat this call, with an empty prompt and `"-n"; "512"`, until
       `generated_eog` is `true`. Each call returns the next ~20 tokens in `output`.

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

    In the loop, `-n` is the most tokens per call, and `max_tokens_update` (0 = no cap)
    can lower it further.

    The response is the `output` of every call concatenated, including the last ingest
    call: the token that `"-n"; "1"` samples at the end of the prompt is the first token
    of the response, and the generate calls continue after it. Every call's `output`
    holds the text of the `n_tokens_generated` tokens of that call, so the counts of the
    calls add up to the tokens of the response. A special token sampled without `-sp`
    is counted but adds no text.

    **Query or update?**

    | Use case                              | Call                                   | Why                                          |
    |---------------------------------------|----------------------------------------|----------------------------------------------|
    | A whole response                      | `run_update_full`                      | many chunks of up to ~34-36B each            |
    | Show text while it is being generated | `run_update` loop                      | the client gets text between calls           |
    | A decision request                    | `run_decision_full`                    | 3-9 steps of ~20-37B each                    |
    | A resumable, client-driven decision   | `run_decision` loop                    | each call returns the answers so far         |
    | `run_query` on an LLM                 | only for tiny models without a session | clear + load of a session is ~4.5B (mainnet) |
    | A decision question as a query        | not viable                             | every model needs more than 5B per question  |
    | A sequence classifier (one text)      | `run_classify_query` or `run_classify` | one forward pass: a short text fits a query  |

    A query has a limit of 5B instructions, and a composite query shares 5B with
    every query it calls. A query answer comes from one replica and is not
    certified: where the answer must be trusted, use an update.

    This is equivalent to using the [ggml-org/llama.cpp](https://github.com/ggml-org/llama.cpp)
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
    icp canister call llama_cpp -e local chats_resume '()'
    icp canister call llama_cpp -e local chats_pause '()'
    ```

    When on, up to 3 chats per principal are saved.
    The `get_chats` method retrieves them for the principal of the caller.

    ```
    icp canister call llama_cpp -e local get_chats '()'
    ```

# log_pause & log_resume

The llama.cpp code is quite verbose. In llama_cpp_canister, you can
turn the logging off and back on with these commands:

```bash
# turn off logging
icp canister call llama_cpp -e local log_pause '()'

# turn on logging
icp canister call llama_cpp -e local log_resume '()'
```

# Monitoring the canister log

The canister log (`icp canister logs llama_cpp`) is a ring buffer of 4096 bytes by
default: ~50 records, and every logged line takes at least two. During a
`run_update_full` or `run_decision_full`, each chunk or step logs a few lines (and
llama.cpp many more), so a busy refusal, an expiry or a trap rotates out within seconds.

Raise the log memory to its maximum of 2 MiB:

```bash
icp canister settings update llama_cpp --log-memory-limit 2mib -e local
```

To keep the log, capture it from outside with `scripts/monitor_logs.sh`:

```bash
# local network, as the test identity (llama-cpp-testing or $ICPP_PRO_TEST_IDENTITY)
scripts/monitor_logs.sh --network local

# mainnet, as the identity named in .icp-identity
scripts/monitor_logs.sh --network production
```

- **What it does:** it polls the log every second, joins the new records into lines (a
  line can span several records), prints them, and appends them to
  `scripts/logs-<network>/<canister>.log` and `scripts/logs-<network>/combined.log`.
  Each line starts with the index and time of its first record. Ctrl+C stops it; a
  restart resumes after the last saved record, without duplicates.
- **Lost records:** when the buffer wrapped between two polls, it prints how many records
  were lost. At the default 4096 bytes, llama.cpp's own logging during a full call loses
  records even at 1s; with 2 MiB, none were lost. Otherwise, lower `--interval`.
- **Options:**
  - `--canister <name or id>`, repeated for several canisters (default `llama_cpp`);
  - `--identity <name>`, which must be a controller unless the log visibility is public;
  - `--interval <seconds>` (default 1).

# Logging to a file

For debug purposes, you can tell the canister to log to a file and download it afterwards.

Pass `"--log-file"; "main.log";` to each `run_update` call.

Afterwards, you can download the `main.log` file from the canister with:

```bash
python -m scripts.download \
  --network local \
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

- Tell pytest which icp identity to run as. Since icpp-pro 6.0.0 this is
  required: the tests never pick up the machine-wide active identity by
  themselves, because any other process can change it mid-run. Most endpoints
  are controller-only, so it must be **the identity you deployed the canister
  with** — if you followed the steps above, that is your active identity:

  ```bash
  icp identity default    # prints the name to pass as --identity
  ```

  It must not be password protected: icpp-pro exports the key to sign the calls
  locally, which a password-protected identity cannot do non-interactively.

  Pass the name as `--identity <name>` (as below), or export
  `ICPP_PRO_TEST_IDENTITY=<name>` once per shell.

- Run the smoketests for the Qwen3-0.6B LLM deployed to your local IC network:

  ```
  # First test the canister functions, like 'health'
  pytest -vv --network local --identity "$(icp identity default)" test/test_canister_functions.py

  # Then run the inference tests (multi-turn, non-thinking)
  pytest -vv --network local --identity "$(icp identity default)" test/test_qwen3.py
  ```

  _(The previous default, Qwen2.5-0.5B, is still covered by `test/test_qwen2.py`.)_

  `test/test_files.py` additionally needs a SECOND identity that is *not* a
  controller, to verify that the filesystem endpoints deny an authenticated but
  unauthorized caller. It defaults to `llama-cpp-other-user` and is overridden
  with `$ICPP_PRO_TEST_IDENTITY_NON_CONTROLLER`:

  ```bash
  icp identity new llama-cpp-other-user --storage plaintext
  ```

  `test/test_files.py` also expects `.canister_cache` to hold only the files it
  creates, so it fails after the chat steps or the other test files. Run it on a
  freshly installed canister: `icp deploy llama_cpp -m reinstall -e local -y`
  erases all the canister's files, the model included.

- Or let the full QA do all of it for you — it creates the two identities
  (`llama-cpp-testing` and `llama-cpp-other-user`), deploys as the first, and
  runs every suite, without ever changing your active identity:

  ```bash
  make test-llm-wasm   # in the repo, not in the release zip
  ```

# Prompt Caching

When a prompt cache file is already present, llama_cpp_canister automatically applies prompt caching to reduce latency and cost.

All repetitive content at the beginning of the prompt does not need to be processed by the LLM, so make sure to design your AI agent prompts such that repetitive content is placed at the beginning.

Next to each prompt cache file `<name>` the canister keeps two small sidecar files:

- `<name>.icppfmt` stamps the format, model and context layout the cache was written with.
- `<name>.logits` holds the logits after the cache's last token: `n_vocab` × 4 bytes, about 600 KB for Qwen3. The model context is shared by all callers, and the cache itself holds no logits. A call that continues a conversation without decoding anything first, such as a generate call with an empty prompt, samples its first token from these logits instead of from whatever the context decoded last for another caller.

`remove_prompt_cache` and `copy_prompt_cache` remove and copy the sidecars together with the cache. A cache written before v0.23.0 has no `.logits` file; it gets one after its next call.

Each caller of the llama_cpp_canister has its own cache folder, and has the following endpoints available to manage their prompt-cache files:

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
      done = false;
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
      done = false;
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
# A larger chunksize goes faster: the scripts default to 2_000_000
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
#       -> use 200_000 or higher (2_000_000 max) in actual upload
#       -> experiment what chunksize results in fastest upload
# Note: demo.cache, so the demo does not overwrite your prompt.cache
icp canister call llama_cpp -e local upload_prompt_cache_chunk '(record {
  promptcache = "demo.cache";
  chunk = blob "\47\47\55\46\03";
  chunksize = 5 : nat64;
  offset = 0 : nat64;
})'
# -> this will return
(
  variant {
    Ok = record {
      filename = ".canister_cache/<principal-id>/sessions/demo.cache";
      filesize = 5 : nat64;
      filesha256 = "fe3b34fd092c3e2c6da3270eb91c4d3e9c2c6f891c21b6ed7358bf5ecca2d207";
    }
  },
)

# --
# Then call it again to upload the next chunk of bytes
icp canister call llama_cpp -e local upload_prompt_cache_chunk '(record {
  promptcache = "demo.cache";
  chunk = blob "\08\33\41\43\04";
  chunksize = 5 : nat64;
  offset = 5 : nat64;
})'
# -> this will return
(
  variant {
    Ok = record {
      filename = ".canister_cache/<principal-id>/sessions/demo.cache";
      filesize = 10 : nat64;
      filesha256 = "438bb530032946102742839ca22319211409cbd1c403f87a82e68e35e89e8c15";
    }
  },
)

# --
# You can check the filesize & sha256 of the uploaded prompt cache file in the canister
icp canister call llama_cpp -e local uploaded_prompt_cache_details '(record {
  promptcache = "demo.cache";
})'
# -> this will return
(
  variant {
    Ok = record {
      filename = ".canister_cache/<principal-id>/sessions/demo.cache";
      filesize = 10 : nat64;
      filesha256 = "438bb530032946102742839ca22319211409cbd1c403f87a82e68e35e89e8c15";
    }
  },
)

# --
# An upload removes the cache's stamp, so the canister does not trust the bytes
# yet: an unstamped cache is discarded on its first use. Stamp it with the
# `.icppfmt` file it was downloaded with, and the sha256 of the uploaded bytes:
icp canister call llama_cpp -e local stamp_uploaded_prompt_cache '(record {
  promptcache = "prompt.cache";
  stamp = "<the text of prompt.cache.icppfmt>";
  filesha256 = "<sha256 of prompt.cache>";
})'

# You can wrap the upload call in a loop, as in scripts/upload.py
# When prompt.cache.icppfmt is next to the local file, it also stamps the cache
python -m scripts.upload \
    --network local \
    --canister llama_cpp \
    --canister-filename prompt.cache \
    --filetype promptcache \
    --chunksize 2000000 \
    prompt.cache
```

## Saving and restoring a prompt cache

`python -m scripts.download --filetype promptcache prompt.cache` saves the cache
and its stamp (`prompt.cache.icppfmt`). `python -m scripts.upload --filetype
promptcache prompt.cache` uploads the cache again and calls
`stamp_uploaded_prompt_cache`, which stamps it only when:

- a model is loaded, and the upload of the cache is complete and unchanged since;
- `filesha256` equals the sha256 of the uploaded bytes;
- the three lines of the stamp equal this build's format, the loaded model and the
  loaded context layout (`--ctx-size`, cache types, ...): restore a cache into the
  same release, with the same `load_model` arguments;
- the head of the cache is a session file of this llama.cpp version, for the
  loaded model's architecture, with no more tokens than the context and only
  tokens of the vocabulary.

The canister writes its own stamp; the downloaded one is only compared. On a
mismatch the call returns an `Err` that shows both sides, the cache stays
unstamped, and its first use discards it and starts cold. A restored cache has
no `.logits` file, so its first call decodes its last token again.

A model with a recurrent or hybrid memory, such as LFM2.5, cannot decode just the
last token again, so it cannot restore a cache: `stamp_uploaded_prompt_cache`
returns an `Err` for it, and the upload starts cold.

The sidecars themselves cannot be uploaded or copied: `upload_prompt_cache_chunk`,
`copy_prompt_cache` and `remove_prompt_cache` reject names ending in `.icppfmt` or
`.logits`. `download_prompt_cache_chunk` can download them.

# Access control

By default, only a controller (or an admin, see `assignAdminRole`) can call these
endpoints:

- new_chat, run_update, run_update_full, run_query
- run_decision, run_decision_full, remove_decision_scope, pin_decision_state,
  unpin_decision_state
- run_classify, run_classify_query
- remove_prompt_cache, copy_prompt_cache
- get_chats, chats_resume, chats_pause

You can open up these endpoints using the following command:

```bash
#
# 0 = only controllers
# 1 = all except anonymous
icp canister call llama_cpp -e local set_access '(record {
  level = 1 : nat16
})'

# Verify it worked
icp canister call llama_cpp -e local get_access '()'

# A caller can check its access rights with
icp canister call llama_cpp -e local check_access '()'
```

The access level and the admin roles (`assignAdminRole`) are saved in the
file `.canister_settings.json` on every change, and `canister_post_upgrade`
restores them, so they survive an upgrade. The first upgrade from a version
before v0.22.0 starts from the defaults, because there is no file yet.

# File Management

A C++ canister on the Internet Computer uses a virtual file system, stored in Stable Memory.

Several endpoints are exposed that allows you to interact & manage the files.

For example, you can explore what is stored in the `.canister_cache` folder:

```bash
# Query call to list all files & directories in a folder
icp canister call llama_cpp -e local recursive_dir_content_query '(record {dir = ".canister_cache"; max_entries = 0 : nat64})'
# Update call in case you hit the instruction limit
icp canister call llama_cpp -e local recursive_dir_content_update '(record {dir = ".canister_cache"; max_entries = 0 : nat64})'
# Update call to get eg. the first 5000 entries, in case you still hit the instruction limit
icp canister call llama_cpp -e local recursive_dir_content_update '(record {dir = ".canister_cache"; max_entries = 5000 : nat64})'

# Get the size of a file in bytes
icp canister call llama_cpp -e local filesystem_file_size '(record {filename = "<filename>"})'

# Get the creation timestamp of a file in nanoseconds (also returns age of file in seconds)
icp canister call llama_cpp -e local get_creation_timestamp_ns '(record {filename = "<filename>"})'

# remove a file or empty directory
icp canister call llama_cpp -e local filesystem_remove '(record {filename = "<filename>"})'
```

# Prompt-Cache Cleanup Timer

The canister can self-maintain its prompt-cache directory on a recurring
schedule, deleting files in `.canister_cache/<principal>/sessions/` (and in
its scope sub-directories, see `run_decision`) whose `mtime` is older than a
configurable Time to Live (TTL). Pinned decision states are skipped. Once you
start the timer, the canister handles prompt-cache hygiene on its own.

**Defaults:**
- period: 600 seconds (10 minutes between cleanup ticks)
- TTL: 21600 seconds (6 hours — older files are deleted)
- per-tick cap: 256 files (caps work-per-tick to stay under the IC's
  per-message instruction budget; the next tick continues)
- max_decision_states: 256 stored decision states (Kev-0.8B, ~20 MB each)
  over all callers and scopes; storing one more removes the oldest unpinned
  one. 0 = no limit.
- max_decision_states_per_scope: 8 stored decision states per caller and
  scope (and 8 without a scope); storing one more removes the oldest unpinned
  one of that scope. 0 = no limit.

**Operator-driven lifecycle.** The timer is off until you call
`cache_cleanup_start_timer`. The config and whether the timer runs are saved
on every change, and `canister_post_upgrade` re-arms a running timer, so you
start it once. (Before v0.22.0 it had to be started after every upgrade.)

All endpoints below require **admin role**:
- `cache_cleanup_start_timer`, `cache_cleanup_stop_timer`,
  `cache_cleanup_now`, `set_cache_cleanup_config` need `AdminUpdate` role
  (controller or whitelisted via `assignAdminRole`).
- `get_cache_cleanup_stats` needs `AdminQuery` role.

```bash
# ------------------------------------------------------------------
# Arm the recurring timer (once; it survives upgrades)
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
#                             is_running = ...;
#                             max_decision_states = opt 256;
#                             max_decision_states_per_scope = opt 8 } })

# Inspect stats (query, fast). `runs` and `last_run_ns` are lifetime
# counters; `files_examined`, `files_deleted`, `files_failed` reflect the
# MOST RECENT cleanup run only.
icp canister call llama_cpp -e local get_cache_cleanup_stats '()'

# Adjust config (each field is `opt nat64`; null = no change).
#   - period_seconds: must be > 0; opt 0 is silently rejected.
#   - ttl_seconds   : 0 is valid ("delete every file under sessions/").
#   - max_files_per_run: clamped to [1, 10000].
#   - max_decision_states, max_decision_states_per_scope: 0 = no limit.
# If the timer is already running, the new period is applied transparently.
icp canister call llama_cpp -e local set_cache_cleanup_config '(record {
  period_seconds    = opt (300 : nat64);
  ttl_seconds       = opt (3600 : nat64);
  max_files_per_run = opt (128 : nat64)
})'

# Same call to update only the TTL (a missing field is null: no change)
icp canister call llama_cpp -e local set_cache_cleanup_config '(record {
  ttl_seconds       = opt (172800 : nat64)
})'
```

# Cycle Balance Monitoring

The `get_cycle_balance` query returns the canister's live cycle balance (the
IC's `canister_cycle_balance128` system call), with `updated_at_ns` set to the
time of the call. It needs `AdminQuery` role (controller or whitelisted via
`assignAdminRole`), so a controller canister can fund the canister when its
balance runs low.

```bash
icp canister call llama_cpp -e local get_cycle_balance '()'
# -> (variant { Ok = record { cycle_balance = ... : nat; updated_at_ns = ... : nat64 } })
```

Before v0.22.0 the query returned an error unless an admin had armed an hourly
tracking timer after the last upgrade. The timer endpoints
(`cycle_balance_start_timer`, `cycle_balance_stop_timer`, `AdminUpdate` role)
are kept for compatibility; nothing requires them any more.

# Memory Status

Larger models (e.g. Qwen3-0.6B) run close to the canister's wasm memory limit.
The `get_memory_status` query lets you watch usage — the `wasm_heap_bytes` value is
the number that climbs toward the `wasm_memory_limit` and, when it reaches it,
causes `heap out of bounds` (IC0502) traps during `load_model` / generation.

Access: **non-anonymous** callers only (anonymous callers get an access-denied error).

```bash
icp canister call llama_cpp -e local get_memory_status '()'
# ->
(
  variant {
    Ok = record {
      wasm_heap_bytes = 1_758_068_736 : nat64;   # linear-memory high-water-mark (~1.76 GiB at ctx 16384, after one load_model)
      stable_bytes = 1_132_527_616 : nat64;      # model file + virtual filesystem
    }
  },
)
```

The heap never shrinks: a second `load_model` raises `wasm_heap_bytes` to ~2.30 GiB, and
further reloads keep it there.

The `wasm_memory_limit` itself is set with `icp canister settings update llama_cpp
--wasm-memory-limit 4026531840` (see the setup steps). Check it
with `icp canister status llama_cpp`. If `wasm_heap_bytes` approaches the limit, lower
`--batch-size`/`--ubatch-size` (biggest win), reduce `--ctx-size`, and/or quantize the KV
cache (`--cache-type-k`/`-v q8_0`) when loading — see [Context size & memory](#appendix-b-context-size--memory).

# Wasm Verification

Every release is built by the reproducible Docker build in [`docker/`](docker/), so anyone
can rebuild a release from source and confirm a deployed canister is running exactly that
wasm. Each GitHub release shows the commit it was built from and the sha256 it produced.

Rebuild a release and compare it against a live canister in one step:

```bash
git checkout <the commit shown on the release page>
make docker-build-base
make docker-verify-wasm VERIFY_CANISTER=<canister-id>
```

This needs a clone of the [repository](https://github.com/onicai/llama_cpp_canister):
the `Makefile` and `docker/` are not in the release zip.

Or just read the deployed module hash. This needs no special rights, because
`--public` reads the public state tree:

```bash
icp canister status <canister-id> -n ic --public   # prints "Module hash: 0x..."
```

# Appendix A: max_tokens

With `max_tokens_update = 0` (no cap), you do not need these numbers: every update call,
and every chunk of a `run_update_full`, stops by itself before the instruction limit.
They tell how many tokens fit in one call or chunk, and so how many calls or chunks a
response takes.

The size and settings for models impact the number of tokens that can be generated
in 1 update call before hitting the instruction limit of the Internet Computer.

The instruction limit is 40 billion instructions per update call

We tested several LLM models available on HuggingFace:

| Model                                                                                           | # weights | file size | quantization | --cache-type-k | max*tokens<br> *(ingestion)\_ | max*tokens<br> *(generation)\_ |
| ----------------------------------------------------------------------------------------------- | --------- | --------- | ------------ | -------------- | ----------------------------- | ------------------------------ |
| [gemma-3-270m-it-Q8_0.gguf](https://huggingface.co/unsloth/gemma-3-270m-it-GGUF) (tiniest)      | 270 M     | 0.29 GB   | q8_0         | q8_0           | -                             | 44                             |
| [Qwen3-0.6B-Q8_0.gguf](https://huggingface.co/Qwen/Qwen3-0.6B-GGUF) (default)                   | 600 M     | 0.64 GB   | q8_0         | q8_0           | -                             | 25                             |
| [qwen2.5-0.5b-instruct-q8_0.gguf](https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF)       | 630 M     | 0.68 GB   | q8_0         | q8_0           | -                             | 25                             |
| [LFM2.5-1.2B-Instruct-Q4_K_M.gguf](https://huggingface.co/LiquidAI/LFM2.5-1.2B-Instruct-GGUF) ‡ | 1.2 B     | 0.73 GB   | Q4_K_M       | q8_0           | 11                            | 9                              |
| [Qwen3-1.7B-Q4_K_M.gguf](https://huggingface.co/unsloth/Qwen3-1.7B-GGUF) ‡                      | 1.7 B     | 1.11 GB   | Q4_K_M       | q8_0           | 8                             | 6                              |
| [LFM2.5-2.6B-Q4_K_M.gguf](https://huggingface.co/LiquidAI/LFM2.5-2.6B-GGUF) ‡                   | 2.7 B     | 1.67 GB   | Q4_K_M       | q8_0           | 4                             | 4                              |

‡ The three bottom rows are the **larger models**, and their numbers were measured
with **`--ctx-size 4096 --batch-size 8 --ubatch-size 8`** — not the `--batch-size 64
--ctx-size 16384` used for the rows above, so they are not directly comparable.

**A larger `--ctx-size` costs you tokens per call.** Qwen3-1.7B drops from 6
tokens/call at `-c 4096` to 5 at `-c 16384`. The reason: every update call gets
the same 40 B instruction budget, and part of it is spent *before* the first
token is produced, on housekeeping the KV cache.

**The per-call cost also grows as the conversation grows.** Measured on mainnet
(`6uo7o-dyaaa-aaaag-ay5ha-cai`, Qwen3-0.6B, `max_tokens_update = 20`): nine
generation calls succeeded, the tenth trapped with IC0522, after ~200 cumulative
generated tokens. Same args on every call — only the conversation length changed.
Independent of language.

Measured on mainnet for Qwen3-1.7B, on two separately loaded models:

| `--ctx-size` | spent before the first token | per generated token | tokens/call |
| ------------ | ---------------------------- | ------------------- | ----------- |
| 4096         | 1.4 B instructions           | 6.4 B instructions  | 6           |
| 24576        | 7.0 B instructions           | 6.4 B instructions  | 5           |

So pick the smallest `--ctx-size` your conversations actually need: the unused
part of the context is not free.

_(We have benchmarked other models too — SmolLM2, Llama-3.2, DeepSeek-R1 1.5B, and other Qwen2.5 quants — but their pre-b10076 numbers must be re-measured before we list them here.)_

NOTEs:

- To experiment with prompt design and determine their exact costs, use [ICGPT](https://icgpt.onicai.com/docs)
- **`gemma-3-270m-it-Q8_0` is the smallest model we tested** — the b10076 fork loads the `gemma3` architecture. With a **q8_0 KV cache** the measured **generation ceiling is ~55 tokens/call** (55 OK / 60 traps at short context, on a local replica whose instruction limit is the same 40 B as mainnet; ~50 with the default f16 cache), so `max_tokens_update = 44` is a safe value. Use `--temp 0.7` — greedy decoding can end a turn immediately. Its heap peaks at only ~0.9 GiB after load, so the **default `wasm_memory_limit` is enough** (no 3.75 GiB bump). See [docs/README-gemma-3.md](docs/README-gemma-3.md) for the full recipe.
- **`qwen2.5-0.5b-instruct-q8_0 is an older model**: 25 tokens/call sustained, 28 first-call ceiling. This is the current model in [funnAI](https://funnai.onicai.com).
- **`Qwen3-0.6B-Q8_0`**: ~25 tokens/call generation, first-call ceiling ~25-29. It needs `--cache-type-k q8_0 --cache-type-v q8_0 --batch-size 64 --ubatch-size 64 --ctx-size 16384` and a `wasm_memory_limit` of 3.75 GiB; the small batch shrinks the compute buffers so a 16K context fits with ~2 GiB headroom — see [Context size & memory](#appendix-b-context-size--memory) for the mechanism, levers, and risks.
- **`LFM2.5-1.2B-Instruct-Q4_K_M` is the fastest model that still passes our word-guessing quality gate** — Liquid AI's hybrid architecture (only 6 of 16 blocks are attention; the other 10 are short convolutions). Measured on mainnet at `-c 4096`: **9 tokens/call generation, 11 ingestion**, and **12.4 B cycles per `run_update` versus Qwen3-1.7B's 26.8 B — 2.16x cheaper per call** at 34 % less disk. Note it needs one word ("small") removed from the word-game example prompt, or it copies it into its hints. See [docs/README-LFM2.5-1.2B.md](docs/README-LFM2.5-1.2B.md).
- **`Qwen3-1.7B-Q4_K_M` is the largest model we tested on-chain so far.** Use Q4_K_M, not Q8_0: a single message can read at most ~2 GiB from stable memory and the Q8_0 gguf (~1.83 GB) trips `IC0524` on load. It needs `--batch-size 8 --ubatch-size 8` and a 3.75 GiB `wasm_memory_limit`, and `--ctx-size 16384` requires v0.16.4 or later. See [docs/README-qwen3-1.7B.md](docs/README-qwen3-1.7B.md).
- **Decision models (System One)**: `run_decision_full` answers typed `choice` / `score` / yes-no questions about a state with probabilities, instead of generating text, in one update call that yields to itself between its steps. **Julia-1** (144M, 50+ languages) fits ~210 tokens per question per update call (~14.9 B instructions for an 87-token question); **Laya** (421M, English, more precise) fits 33. **Kev-0.8B** (causal, English) stores the state once per caller, ingesting it over as many calls as needed (and only the new part when the state grows), and then answers each question from it at up to ~22 tokens per question: ingest once, ask many. A request with several questions continues over the steps of that one call (for Laya, send one question per request). A caller that serves many users (a gateway) passes a `scope` per user, and each reply reports the instructions it used. See [docs/README-decision-models.md](docs/README-decision-models.md), [docs/README-decision-model-julia-1.md](docs/README-decision-model-julia-1.md), [docs/README-decision-model-Laya.md](docs/README-decision-model-Laya.md) and [docs/README-decision-model-kev-0.8b.md](docs/README-decision-model-kev-0.8b.md).
- **Sequence classifiers (BERT / ModernBERT)**: `run_classify` and `run_classify_query` return the raw scores of a classification head with its labels for one text, in one forward pass, as upstream `llama-embedding --pooling rank` does. Load the model with `--embedding --pooling rank`, in **f32**, which runs on wasm SIMD (3.6x cheaper than f16). The example, **onicai/dao-moderation-classifier** (Ettin-encoder-17M), moderates a DAO's chat channel: 15 scores per message (kind, toxicity, scam, misleading, respectful, action), 0.07-1.67 B instructions per message, so an OpenChat moderation bot calls it as a query. See [docs/README-classifier-models.md](docs/README-classifier-models.md), and [docs/README-classifier-training.md](docs/README-classifier-training.md) to train your own.
- During prompt ingestion phase, the max_tokens before hitting the instruction limit is higher as during the generation phase.
- We use `"--temp"; "0.6"; "--repeat-penalty"; "1.1";`, as recommended on several model cards
- For each model, we selected a `--cache-type-k` that gives the highest max_tokens while still providing good results.

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
| `wasm_memory_limit` | The ceiling itself (≤ 3.75 GiB on wasm32). | Set via `icp canister settings update`. |

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
