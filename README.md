[![llama_cpp_canister](https://github.com/onicai/llama_cpp_canister/actions/workflows/cicd-mac.yml/badge.svg)](https://github.com/onicai/llama_cpp_canister/actions/workflows/cicd-mac.yml)

# llama.cpp for the Internet Computer.

![llama](https://user-images.githubusercontent.com/1991296/230134379-7181e485-c521-4d23-a0d6-f7b3b61ba524.png)


`llama_cpp_canister` allows you to deploy [ggerganov/llama.cpp](https://github.com/ggerganov/llama.cpp) as a Smart Contract on the Internet Computer.

- MIT open source 🧑‍💻
- Well documented 📝
- Fully QA'd via CI/CD ✅
- Easy to build, test & deploy 🚧
- Smoke testing framework using pytest 🚬


# Try it out

You can try out a deployed version at https://icgpt.onicai.com

# Need help?

If you decide to use llama_cpp_canister in your ICP dApp, we want to help you.

We do NOT consider llama_cpp_canister "our IP". It is for the broad benefit of DeAI on ICP, and we hope many of you will try it out and use it.

Please join our [OpenChat C++ community](https://oc.app/community/cklkv-3aaaa-aaaar-ar7uq-cai/?ref=6e3y2-4yaaa-aaaaf-araya-cai) for any questions, discussions or feedback. ❤️

# Capabilities 🔥

- You can deploy LLMs up to ~0.5B parameters.
- The full context window of the LLM is used. (128K tokens for the Qwen2.5 example below.) 


# Set up

WARNING: Currently, the canister can only be build on a `Mac` ! 

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
  # from ./llama_cpp_canister folder
  make build-info-cpp-wasm
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

- Install dfx:

   ```bash
   sh -ci "$(curl -fsSL https://internetcomputer.org/install.sh)"

   # Configure your shell
   source "$HOME/.local/share/dfx/env"
   ```

- Build & Deploy the canister `llama_cpp`:

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
  this example we use the powerful qwen2.5-0.5b-instruct-q8_0.gguf model.

  - Download the model from huggingface: https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF

    Store it in: `models/Qwen/Qwen2.5-0.5B-Instruct-GGUF/qwen2.5-0.5b-instruct-q8_0.gguf`
    
  - Upload the gguf file:
    ```bash
    python -m scripts.upload --network local --canister llama_cpp --canister-filename models/model.gguf models/Qwen/Qwen2.5-0.5B-Instruct-GGUF/qwen2.5-0.5b-instruct-q8_0.gguf
    ```
  
  - Only needed after a canister upgrade (`dfx deploy -m upgrade`), re-load the gguf file into Orthogonal Persisted (OP) working memory 
  
    This step is already done by scripts.upload above, so you can skip it if you just ran that.

    After a canister upgrade, the gguf file in the canister is still there, because it is persisted in 
    stable memory, but you need to load it into Orthogonal Persisted (working) memory, which is erased during a canister upgrade.

    ```bash
    dfx canister call llama_cpp load_model '(record { args = vec {"--model"; "models/model.gguf";} })'
    ```

  - Set the max_tokens for this model, to avoid it hits the IC's instruction limit
    ```
    dfx canister call llama_cpp set_max_tokens '(record { max_tokens_query = 10 : nat64; max_tokens_update = 10 : nat64 })'

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

    ```bash
    # Start a new chat - this resets the prompt-cache for this conversation
    dfx canister call llama_cpp new_chat '(record { args = vec {"--prompt-cache"; "my_cache/prompt.cache"} })'

    # Repeat this call until `prompt_remaining` in the response is empty. 
    # This ingest the prompt into the prompt-cache, using multiple update calls
    # Important: KEEP SENDING THE FULL PROMPT 
    dfx canister call llama_cpp run_update '(record { args = vec {"--prompt-cache"; "my_cache/prompt.cache"; "--prompt-cache-all"; "-sp"; "-p"; "<|im_start|>system\nYou are a helpful assistant.<|im_end|>\n<|im_start|>user\ngive me a short introduction to LLMs.<|im_end|>\n<|im_start|>assistant\n"; "-n"; "512" } })' 
    ...

    # Once `prompt_remaining` in the response is empty, repeat this call, with an empty prompt, until `generated_eog=true`
    # Now the LLM is generating new tokens !
    dfx canister call llama_cpp run_update '(record { args = vec {"--prompt-cache"; "my_cache/prompt.cache"; "--prompt-cache-all"; "-sp"; "-p"; ""; "-n"; "512" } })'

    ...

    # Once `generated_eog` in the response is `true`, the LLM is done generating

    # this is the response after several update calls and it has reached eog:
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

    ########################################
    # Tip. Add this to the args vec if you #
    #      want to see how many tokens the #
    #      canister can generate before it #
    #      hits the instruction limit      #
    #                                      #
    #      ;"--print-token-count"; "1"     #
    ########################################

    ```

    Note: The sequence of update calls to the canister is required because the Internet Computer has a limitation
    on the number of computations it allows per call. At the moment, only 10 tokens can be generated per call.
    This sequence of update calls is equivalent to using the [ggerganov/llama.cpp](https://github.com/ggerganov/llama.cpp) 
    repo directly and running the `llama-cli` locally, with the command:
    ```
    ./llama-cli -m /models/Qwen/Qwen2.5-0.5B-Instruct-GGUF/qwen2.5-0.5b-instruct-q8_0.gguf --prompt-cache prompt.cache --prompt-cache-all -sp -p "<|im_start|>system\nYou are a helpful assistant.<|im_end|>\n<|im_start|>user\ngive me a short introduction to LLMs.<|im_end|>\n<|im_start|>assistant\n" -n 512 -fa -ngl 80 
    ```
    In above command, the `-fa -ngl 80` arguments are useful only on GPU. We do not use them when calling the IC, because
    the canister has a CPU only.

  - Retrieving saved chats

    Up to 3 chats per principal are saved.
    The `get_chats` method retrieves them for the principal of the caller.

    ```
    dfx canister call llama_cpp get_chats
    ```


- You can download the `main.log` file from the canister with:
  ```
  python -m scripts.download --network local --canister llama_cpp --local-filename main.log main.log
  ```

## Smoke testing the deployed LLM

You can run a smoketest on the deployed LLM:

- Deploy Qwen2.5 model as described above

- Run the smoketests for the Qwen2.5 LLM deployed to your local IC network:

  ```
  # First test the canister functions, like 'health'
  pytest -vv test/test_canister_functions.py

  # Then run the inference tests
  pytest -vv test/test_qwen2.py
  ```

## Securing your LLM

By default, only a controller can call the inference endpoints:
- new_chat
- run_update
- run_query

You can open up the inference endpoints using the following command:

```
# 
# 0 = only controllers
# 1 = all except anonymous
dfx canister call llama_cpp set_access '(record {level = 1 : nat16})'

# Verify it worked
dfx canister call llama_cpp get_access
```