[![llama_cpp_canister](https://github.com/onicai/llama_cpp_canister/actions/workflows/cicd-mac.yml/badge.svg)](https://github.com/onicai/llama_cpp_canister/actions/workflows/cicd-mac.yml)

# [ggerganov/llama.cpp](https://github.com/ggerganov/llama.cpp) for the Internet Computer.

![llama](https://user-images.githubusercontent.com/1991296/230134379-7181e485-c521-4d23-a0d6-f7b3b61ba524.png)


# [ggerganov/llama.cpp](https://github.com/ggerganov/llama.cpp) for the Internet Computer.

This repo allows you to deploy llama.cpp as a Smart Contract to the Internet Computer.


# Overview

The following models work:

| Model | File Size | Location | Status | Notes |
| ----- | --------- | -------- | ------ | ------|
| stories260Ktok512.gguf             |   2.00 Mb | ./models | ✅ | Testing only |
| stories15Mtok4096.gguf             |  32.00 Mb | ./models | ✅ | Ok |
| storiesICP42Mtok4096.gguf          | 113.00 Mb | ./models | ✅ | Works great |
| gpt2.Q8_0.gguf                     | 176.00 Mb | https://huggingface.co/igorbkz/gpt2-Q8_0-GGUF | ✅ | Not very good|

<br><br>

---

The following models load, but hit instruction limit after a few tokens, making it unusable:
| Model | File Size | Location | Status | Notes |
| ----- | --------- | -------- | ------ | ------|
| tinyllama-1.1b-chat-v1.0.Q8_0.gguf |   1.17 Gb | https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF | ✅ | 4 tokens max |

<br><br>

---
The following models do not load, because they do not fit in wasm32 memory
 Model | File Size | Location | Status | Notes |
| ----- | --------- | -------- | ------ | ------|
| Phi-3-mini-4k-instruct-q4.gguf     |   2.39 Gb | https://huggingface.co/microsoft/Phi-3-mini-4k-instruct-gguf | 🚫 | Needs wasm64 |

<br><br>


# WARNING ⚠️

This repo is under heavy development. 🚧

- Important limitation is that it only works on a `Mac`. (Windows & Linux is coming soon)
- Only use it if you're brave 💪 and not afraid of digging ⛏️ into the details of C++, ICP & LLMs.
- Things in this README should be mostly correct, though no guarantee.
- Everything is moving fast, so refresh your local clone frequently. ⏰ 
- The canister endpoint APIs are not yet fixed. Expect breaking changes ❗❗❗


# Questions & Discussions ❓

Please join our [OpenChat C++ community](https://oc.app/community/cklkv-3aaaa-aaaar-ar7uq-cai/?ref=6e3y2-4yaaa-aaaaf-araya-cai) for any questions, discussions or feedback. ❤️


# Set up

WARNING: Currently, the canister can only be build on a `mac` ! 

- Use Python 3.11 ❗❗❗

- Install  [icpp-pro](https://docs.icpp.world/installation.html), the C++ Canister Development Kit (CDK) for the Internet Computer

- Clone the repo and it's children:

   ```bash
   # Clone this repo
   git clone git@github.com:onicai/llama_cpp_canister.git

   # Clone llama_cpp_onicai_fork, our forked version of llama.cpp
   # Into the ./src folder
   cd src
   git clone git@github.com:onicai/llama_cpp_onicai_fork.git

   # Initialize the submodules of the llama_cpp_onicai_fork repo
   cd llama_cpp_onicai_fork
   git submodule init
   git submodule update
   ```

- Create the file src/llama_cpp_onicai_fork/common/build-info.cpp
  ```
  make build-info-cpp-wasm
  ```
  TODO: recipe for Windows.

- Create a Python environment with dependencies installed

   ```bash
   # We use MiniConda (Use Python 3.11 ❗❗❗)
   conda create --name llama_cpp_canister python=3.11
   conda activate llama_cpp_canister

   # Install the python dependencies
   # From root of llama_cpp_canister repo:
   pip install -r requirements.txt
   ```

- Install dfx:

   ```bash
   sh -ci "$(curl -fsSL https://internetcomputer.org/install.sh)"

   # Configure your shell
   source "$HOME/.local/share/dfx/env"
   ```

   _(Note 1: On Windows, just install dfx in wsl, and icpp-pro in PowerShell will know where to find it. )_
   _(Note 2: It does not yet work on Windows... Stay tuned... )_

- Build & Deploy a pre-trained model to canister `llama_cpp`:

  - Compile & link to WebAssembly (wasm):
    ```bash
    icpp build-wasm
    ```
    Note: 
    
    The first time you run this command, the tool-chain will be installed in ~/.icpp
    
    This can take a few minutes, depending on your internet speed and computer.

  - Start the local network:
    ```bash
    dfx start --clean
    ```  
  - Deploy the wasm to a canister on the local network:
    ```bash
    dfx deploy
    ```
  - Check the health endpoint of the `llama_cpp` canister:
    ```bash
    $ dfx canister call llama_cpp health
    (variant { Ok = record { status_code = 200 : nat16 } })
    ```
  
# Build & Test models

## storiesICP42Mtok4096.gguf (113.0 Mb)

  This is a fine-tuned model that generates funny stories about ICP & ckBTC.

  The context window for the model is 128 tokens, and that is the maximum length llama.cpp allows for token generation.
  
  The same deployment & test procedures can be used for the really small test models `stories260Ktok512.gguf` & `stories15Mtok4096.gguf`. Those two models are great for fleshing out the deployment, but the LLMs themselves are too small to create comprehensive stories.

  - Download the model from huggingface: https://huggingface.co/onicai/llama_cpp_canister_models
    
    Store it in: `models/storiesICP42Mtok4096.gguf`

  - Upload the model:
    ```bash
    python -m scripts.upload --network local --canister llama_cpp --canister-filename models/storiesICP42Mtok4096.gguf models/storiesICP42Mtok4096.gguf
    ```

  - Load the model into OP memory

    This command will load a model into working memory (Orthogonal Persisted):
    ```bash
    dfx canister call llama_cpp load_model '(record { args = vec {"--model"; "models/storiesICP42Mtok4096.gguf";} })'
    ```

  - Ensure the canister is ready for Inference, with the model loaded
    ```bash
    dfx canister call llama_cpp ready
    ```

  - Chat with the LLM:

    ```bash
    # Start a new chat - this resets the prompt-cache for this conversation
    dfx canister call llama_cpp new_chat '(record { args = vec {"--prompt-cache"; "my_cache/prompt.cache"} })'

    # Create 50 tokens from a prompt, with caching
    dfx canister call llama_cpp run_update '(record { args = vec {"--prompt-cache"; "my_cache/prompt.cache"; "--prompt-cache-all";"--samplers"; "top_p"; "--temp"; "0.1"; "--top-p"; "0.9"; "-n"; "50"; "-p"; "Dominic loves writing stories"} })'

    # Create another 50 tokens, using the cache - just continue, no new prompt provided
    # Repeat until the LLM says it is done or until you hit the context limit with the error:
    #    `main_: error: prompt is too long`
    #
    dfx canister call llama_cpp run_update '(record { args = vec {"--prompt-cache"; "my_cache/prompt.cache"; "--prompt-cache-all";"--samplers"; "top_p"; "--temp"; "0.1"; "--top-p"; "0.9"; "-n"; "50";} })'

    # After a couple of calls, you will get something like this as output, unless you hit the context limit error:
    (
      variant {
        Ok = record {
          status = 200 : nat16;
          output = "";
          error = "";
          input = " Dominic loves writing stories. He wanted to share his love with others, so he built a fun website on the Internet Computer. With his ckBTC, he bought a cool new book with new characters. Every night before bed, Dominic read his favorite stories with his favorite characters. The end.";
        }
      },
    )
    

    ########################################
    # Tip. Add this to the args vec if you #
    #      want to see how many tokens the #
    #      canister can generate before it #
    #      hits the instruction limit      #
    #                                      #
    #      ;"--print-token-count"; "1"     #
    ########################################

## qwen2-0_5b-instruct-q8_0.gguf (531 Mb; ~14 tokens max)

  - Download the model from huggingface: https://huggingface.co/Qwen/Qwen2-0.5B-Instruct-GGUF

    Store it in: `models/Qwen/Qwen2-0.5B-Instruct-GGUF/qwen2-0_5b-instruct-q8_0.gguf`
    
  - Upload the model:
    ```bash
    python -m scripts.upload --network local --canister llama_cpp --canister-filename models/qwen2-0_5b-instruct-q8_0.gguf models/Qwen/Qwen2-0.5B-Instruct-GGUF/qwen2-0_5b-instruct-q8_0.gguf
    ```

  - Load the model into OP memory (Do once, and note that it is already done by scripts.upload above)
    ```bash
    dfx canister call llama_cpp load_model '(record { args = vec {"--model"; "models/qwen2-0_5b-instruct-q8_0.gguf";} })'
    ```

  - Ensure the canister is ready for Inference, with the model loaded
    ```bash
    dfx canister call llama_cpp ready
    ```

  - Chat with the LLM:

    Different ways to use this model with llama.cpp:
    https://qwen.readthedocs.io/en/latest/run_locally/llama.cpp.html

    ```bash
    # Start a new chat - this resets the prompt-cache for this conversation
    dfx canister call llama_cpp new_chat '(record { args = vec {"--prompt-cache"; "my_cache/prompt.cache"} })'

    # This is how you would do it if there was no instructions limit...
    dfx canister call llama_cpp run_update '(record { args = vec {"--prompt-cache"; "my_cache/prompt.cache"; "--prompt-cache-all"; "-sp"; "-p"; "<|im_start|>system\nYou are a helpful assistant.<|im_end|>\n<|im_start|>user\ngive me a short introduction to LLMs.<|im_end|>\n<|im_start|>assistant\n"; "-n"; "512" } })' 

    # But... This model can generate 14 tokens, including initial prompt, before hitting the instruction limit
    # So, split it up...
    # TODO: build the initial prompt in multiple steps... 
    # For now, skip this step, so, NO system instructions
    dfx canister call llama_cpp run_update '(record { args = vec {"--prompt-cache"; "my_cache/prompt.cache"; "--prompt-cache-all"; "-sp"; "-p"; "<|im_start|>system\nExplain Large Language Models.<|im_end|>\n"; "-n"; "1" } })'
    # And for now, just start here. 
    # The user prompt:
    # (-) sandwiched in between:  
    #     <|im_start|>user\n ... <|im_end|>\n
    # Tell the LLM that next up is the assistant to generate tokens:
    #     <|im_start|>assistant\n
    # (-) generate 1 token, which is NOT stored in the cache...:
    dfx canister call llama_cpp run_update '(record { args = vec {"--prompt-cache"; "my_cache/prompt.cache"; "--prompt-cache-all"; "-sp"; "-p"; "<|im_start|>user\nExplain Large Language Models.<|im_end|>\n<|im_start|>assistant\n"; "-n"; "1" } })'
    
    # Continue generating, 10 tokens at a time
    dfx canister call llama_cpp run_update '(record { args = vec {"--prompt-cache"; "my_cache/prompt.cache"; "--prompt-cache-all"; "-sp"; "-n"; "10" } })'
    
    # At some point, you will get an <|im_end|> special section back, which indicates the end of the assistant token generation:
    ```
    % dfx canister call llama_cpp run_update '(record { args = vec {"--prompt-cache"; "my_cache/prompt.cache"; "--prompt-cache-all"; "-sp"; "-n"; "10" } })'
    (
      variant {
        Ok = record {
          status = 200 : nat16;
          output = " and recommendation systems.<|im_end|>";
          error = "";
          input = "<|im_start|>user\nExplain Large Language Models.<|im_end|>\n<|im_start|>assistant\nLarge Language Models (LLMs) are artificial intelligence models that can generate human-like text or answer questions based on large amounts of text data. They are commonly used in natural language processing (NLP) tasks such as question answering, text summarization, and natural language generation. LLMs can be trained on large amounts of text data, making them highly efficient and scalable for a wide range of applications. They can also be trained on unstructured or semi-structured data, making them useful for tasks such as sentiment analysis, topic modeling,";
        }
      },
    )
    ```

    ########################################
    # Tip. Add this to the args vec if you #
    #      want to see how many tokens the #
    #      canister can generate before it #
    #      hits the instruction limit      #
    #                                      #
    #      ;"--print-token-count"; "1"     #
    ########################################