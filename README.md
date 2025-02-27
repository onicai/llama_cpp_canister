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


# Try it out

You can try out a variety of fully on-chain LLMs at https://icgpt.onicai.com

# Need help or have feedback? ❤️

- [OpenChat C++ community](https://oc.app/community/cklkv-3aaaa-aaaar-ar7uq-cai/?ref=6e3y2-4yaaa-aaaaf-araya-cai) 
- [Forum: Llama.cpp on the Internet Computer](https://forum.dfinity.org/t/llama-cpp-on-the-internet-computer/33471?u=icpp)

# Capabilities 🔥

- Deploy any LLM available as a gguf file.

  *(The model must be able to produce at least 1 token per update call)*

- Our largest so far is DeepSeek-R1 1.5B (See [X](https://x.com/onicaiHQ/status/1884339580851151089)).
  

# Set up

The build of the wasm must be done on a `Mac` ! 

- Install dfx:

   ```bash
   sh -ci "$(curl -fsSL https://internetcomputer.org/install.sh)"

   # Configure your shell
   source "$HOME/.local/share/dfx/env"
   ```

- Clone the repo and it's children:

   ```bash
   # Clone this repo
   git clone git@github.com:onicai/llama_cpp_canister.git

   # Clone llama_cpp_onicai_fork, our forked version of llama.cpp
   # Into the ./src folder
   cd src
   git clone git@github.com:onicai/llama_cpp_onicai_fork.git
   ```

- Create a Python environment with dependencies installed
  
  ❗❗❗ Use Python 3.11 ❗❗❗
  
  _(This is needed for binaryen.py dependency)_

   ```bash
   # We use MiniConda
   conda create --name llama_cpp_canister python=3.11
   conda activate llama_cpp_canister

   # Install the python dependencies
   # From root of llama_cpp_canister repo:
   pip install -r requirements.txt
   ```

- Build & Deploy the canister `llama_cpp`:

  - Compile & link to WebAssembly (wasm):
    ```bash
    make build-info-cpp-wasm
    icpp build-wasm
    ```
    Note: The first time you run this command, the tool-chain will be installed in ~/.icpp

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
  

- Upload gguf file

  The canister is now up & running, and ready to be loaded with a gguf file. In
  this example we use the powerful `qwen2.5-0.5b-instruct-q8_0.gguf` model, but
  you can use any model availabe in gguf format.

  - Download the model from huggingface: https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF

    Store it in: `models/Qwen/Qwen2.5-0.5B-Instruct-GGUF/qwen2.5-0.5b-instruct-q8_0.gguf`
    
  - Upload the gguf file:
    ```bash
    python -m scripts.upload \
      --network local \
      --canister llama_cpp \
      --canister-filename models/model.gguf \
      models/Qwen/Qwen2.5-0.5B-Instruct-GGUF/qwen2.5-0.5b-instruct-q8_0.gguf
    ```

  NOTE: In C++, files are stored in stable memory of the canister.
        They will survive a code upgrade.
  
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
  
  *(See Appendix A for values of others models.)*
  ```bash
  dfx canister call llama_cpp set_max_tokens '(record { 
    max_tokens_query = 13 : nat64; 
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

    ---

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

    ---

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

    ---

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
    ---

    Remove the prompt cache when done - this keeps stable memory usage at a minimum
    ```bash
    dfx canister call llama_cpp remove_prompt_cache '(record { 
      args = vec {
        "--prompt-cache"; "prompt.cache"
      } 
    })'
    ```

    Note: The sequence of update calls to the canister is required because the Internet Computer has a limitation
    on the number of instructions it allows per call. For this model, 10 tokens can be generated per update call.

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

  - Retrieving saved chats

    Up to 3 chats per principal are saved.
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


# Appendix A: max_tokens

The size and settings for models impact the number of tokens that can be generated 
in 1 update call before hitting the instruction limit of the Internet Computer.

The instruction limit is 40 billion instructions per update call
 
We tested several LLM models available on HuggingFace:


| Model | # weights | file size | quantization | --cache-type-k |  max_tokens<br> *(ingestion)* | max_tokens<br> *(generation)* |
| ------| ----------| --------- | ------------ | ---------------| ----------------------------- | ----------------------------- |
| [SmolLM2-135M-Instruct-Q8_0.gguf](https://huggingface.co/tensorblock/SmolLM2-135M-Instruct-GGUF)               |  135 M | 0.15 GB | q8_0   | f16  |  - | 40 |
| [qwen2.5-0.5b-instruct-q4_k_m.gguf](https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF)                    |  630 M | 0.49 GB | q4_k_m | f16  |  - | 14 |
| [qwen2.5-0.5b-instruct-q8_0.gguf](https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF)                      |  630 M | 0.68 GB | q8_0   | q8_0 |  - | 13 |
| [Llama-3.2-1B-Instruct-Q4_K_M.gguf](https://huggingface.co/unsloth/Llama-3.2-1B-Instruct-GGUF)                 | 1.24 B | 0.81 GB | q4_k_m | q5_0 |  5 |  4 |
| [qwen2.5-1.5b-instruct-q4_k_m.gguf](https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF)                    | 1.78 B | 1.10 GB | q4_k_m | q8_0 |  - |  3 |
| [DeepSeek-R1-Distill-Qwen-1.5B-Q6_K.gguf](https://huggingface.co/unsloth/DeepSeek-R1-Distill-Qwen-1.5B-GGUF)   | 1.78 B | 1.46 GB | q6_k   | q8_0 |  4 |  3 |
| [DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_M.gguf](https://huggingface.co/unsloth/DeepSeek-R1-Distill-Qwen-1.5B-GGUF) | 1.78 B | 1.12 GB | q4_k_m | q8_0 |  4 |  3 |
| [DeepSeek-R1-Distill-Qwen-1.5B-Q2_K.gguf](https://huggingface.co/unsloth/DeepSeek-R1-Distill-Qwen-1.5B-GGUF)   | 1.78 B | 0.75 GB | q2_k   | q8_0 |  2 |  2 |



NOTEs: 
- During prompt ingestion phase, the max_tokens before hitting the instruction limit is higher as during the generation phase.
- We use `"--temp"; "0.6"; "--repeat-penalty"; "1.1";`, as recommended on several model cards
- For each model, we selected a `--cache-type-k` that gives the highest max_tokens while still providing good results.
- The python notebook [scripts/promt-design.ipynb](./scripts/prompt-design.ipynb) allows you to try out these models w/o using an IC canister, to decide what model will work best for your on-chain AI agent
