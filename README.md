[![llama_cpp_canister](https://github.com/onicai/llama_cpp_canister/actions/workflows/cicd-mac.yml/badge.svg)](https://github.com/onicai/llama_cpp_canister/actions/workflows/cicd-mac.yml)

# [ggerganov/llama.cpp](https://github.com/ggerganov/llama.cpp) for the Internet Computer.

![llama](https://user-images.githubusercontent.com/1991296/230134379-7181e485-c521-4d23-a0d6-f7b3b61ba524.png)


# [ggerganov/llama.cpp](https://github.com/ggerganov/llama.cpp) for the Internet Computer.

This repo allows you to deploy llama.cpp as a Smart Contract to the Internet Computer.

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

- VERY IMPORTANT: Use Python 3.11 ❗❗❗

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

## qwen2.5-0.5b-instruct-q8_0.gguf (676 Mb; ~14 tokens max)

  - Download the model from huggingface: https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF

    Store it in: `models/Qwen/Qwen2.5-0.5B-Instruct-GGUF/qwen2.5-0.5b-instruct-q8_0.gguf`
    
  - Upload the model:
    ```bash
    python -m scripts.upload --network local --canister llama_cpp --canister-filename models/qwen2.5-0.5b-instruct-q8_0.gguf models/Qwen/Qwen2.5-0.5B-Instruct-GGUF/qwen2.5-0.5b-instruct-q8_0.gguf
    ```
  
  - Load the model into OP memory (Do once, and note that it is already done by scripts.upload above)
    ```bash
    dfx canister call llama_cpp load_model '(record { args = vec {"--model"; "models/qwen2.5-0.5b-instruct-q8_0.gguf";} })'
    ```

  - Set the max_tokens for this model, to avoid it hits the IC's instruction limit
    ```
    dfx canister call llama_cpp set_max_tokens '(record { max_tokens_query = 12 : nat64; max_tokens_update = 12 : nat64 })'

    dfx canister call llama_cpp get_max_tokens
    ```

  - Ensure the canister is ready for Inference, with the model loaded
    ```bash
    dfx canister call llama_cpp ready
    ```

  - Chat with the LLM:

    Details how to use the Qwen models with llama.cpp:
    https://qwen.readthedocs.io/en/latest/run_locally/llama.cpp.html

    ```bash
    # Start a new chat - this resets the prompt-cache for this conversation
    dfx canister call llama_cpp new_chat '(record { args = vec {"--prompt-cache"; "my_cache/prompt.cache"} })'

    # Repeat this call until the prompt_remaining is empty. KEEP SENDING THE ORIGINAL PROMPT 
    dfx canister call llama_cpp run_update '(record { args = vec {"--prompt-cache"; "my_cache/prompt.cache"; "--prompt-cache-all"; "-sp"; "-p"; "<|im_start|>system\nYou are a helpful assistant.<|im_end|>\n<|im_start|>user\ngive me a short introduction to LLMs.<|im_end|>\n<|im_start|>assistant\n"; "-n"; "512" } })' 
    ...

    # Once prompt_remaining is empty, repeat this call, with an empty prompt, until the `generated_eog=true`:
    dfx canister call llama_cpp run_update '(record { args = vec {"--prompt-cache"; "my_cache/prompt.cache"; "--prompt-cache-all"; "-sp"; "-p"; ""; "-n"; "512" } })'

    ...

    # Once generated_eog = true, the LLM is done generating

    # this is the output after several update calls and it has reached eog:
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

    ########################################################
    # NOTE: This is the equivalent llama-cli call, when running llama.cpp locally
    ./llama-cli -m /models/Qwen/Qwen2.5-0.5B-Instruct-GGUF/qwen2.5-0.5b-instruct-q8_0.gguf -sp -p "<|im_start|>system\nYou are a helpful assistant.<|im_end|>\n<|im_start|>user\ngive me a short introduction to LLMs.<|im_end|>\n<|im_start|>assistant\n"  -fa -ngl 80 -n 512 --prompt-cache prompt.cache --prompt-cache-all


    ```

    ########################################
    # Tip. Add this to the args vec if you #
    #      want to see how many tokens the #
    #      canister can generate before it #
    #      hits the instruction limit      #
    #                                      #
    #      ;"--print-token-count"; "1"     #
    ########################################

  - Deployed to mainnet at canister: 6uwoh-vaaaa-aaaag-amema-cai

      To be able to upload the model, I had to change the [compute allocation](https://internetcomputer.org/docs/current/developer-docs/smart-contracts/maintain/settings#compute-allocation)

      ```
      # check the settings
      dfx canister status --ic llama_cpp  

      # Set a compute allocation  (costs a rental fee)
      dfx canister update-settings --ic llama_cpp --compute-allocation 50
      ```
    
    - Cost of uploading the 676 Mb model, using the compute-allocation of 50, cost about:
      
      ~3 TCycle = $4 

    - Cost of 10 tokens = 31_868_339_839 Cycles = ~0.03 TCycles = ~$0.04
      So, 1 Token = ~$0.004

---
---

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
    # Repeat until the LLM says it is done
    dfx canister call llama_cpp run_update '(record { args = vec {"--prompt-cache"; "my_cache/prompt.cache"; "--prompt-cache-all";"--samplers"; "top_p"; "--temp"; "0.1"; "--top-p"; "0.9"; "-n"; "50";} })'

    # After a couple of calls, you will get something like this as output, unless you hit the context limit error:
    (
      variant {
        Ok = record {
          status_code = 200 : nat16;
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