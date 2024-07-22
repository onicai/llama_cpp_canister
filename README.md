[![llama_cpp_canister](https://github.com/onicai/llama_cpp_canister/actions/workflows/cicd-mac.yml/badge.svg)](https://github.com/onicai/llama_cpp_canister/actions/workflows/cicd-mac.yml)

![llama](https://user-images.githubusercontent.com/1991296/230134379-7181e485-c521-4d23-a0d6-f7b3b61ba524.png)


# [ggerganov/llama.cpp](https://github.com/ggerganov/llama.cpp) for the Internet Computer.


# Getting Started

Currently, the canister can only be build on a `mac` !

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

   _(Note: On Windows, just install dfx in wsl, and icpp-pro in PowerShell will know where to find it. )_
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
  - Upload the 260K parameter model:
    _(We included this fine-tuned model in the repo)_
    ```bash
    python -m scripts.upload --network local --canister llama_cpp models/stories260Ktok512.gguf
    ```

- Test it with dfx.

  - Generate 20 tokens, using the `run_query` or `run_update` call:

    ```bash
    $ dfx canister call llama_cpp run_query '(record { args = vec {"--model"; "models/stories260Ktok512.gguf"; "--prompt"; "Patrick loves ice-cream. On a hot day "; "--n-predict"; "20"; "--ctx-size"; "128"} })'
    
    $ dfx canister call llama_cpp run_update '(record { args = vec {"--model"; "models/stories260Ktok512.gguf"; "--prompt"; "Patrick loves ice-cream. On a hot day "; "--n-predict"; "20"; "--ctx-size"; "128"} })'
    
    -> See token generation in the dfx log window


# Models

## HuggingFace

You can find a lot of models in the llama.cpp *.gguf format on HuggingFace.

Don't try them out yet though, they will not yet run, but hit the instruction limit.

See next steps on how we will fix this.

We will start by expanding our tests to the tiny stories models:

### TinyStories - [onicai/llama_cpp_canister_models](https://huggingface.co/onicai/llama_cpp_canister_models)

| model                    | notes                                                 |
| ------------------------ | ----------------------------------------------------- |
| stories260Ktok512.guff   | Works! Use this for development & debugging           |
| stories15Mtok4096.guff   | todo |
| stories42Mtok4096.guff   | todo |
| stories42Mtok32000.guff  | todo |
| stories110Mtok32000.guff | todo |

# TODO

## Run larger models

We focus on two types of models:

1. the TinyStories models:

   These small models are fantastic for fleshing out the implementation:

   - 260K, 15M, 42M, 110M
   - non-quantized
   - quantized with 4-bits

2. Larger models fine-tuned for chat:

   This will allow you to have a conversation, as you do in ChatGPT:

   - [microsoft/phi-3](...), quantized with 4-bits

## Optimizations

In order to run the larger models, following optimizations are planned:

- Don't read model as part of `run_query` or `run_update`, but read it only once
- Use SIMD, to reduce number of instructions required to generate a token
- Use quantized models
- ... other items as IC capabilities grow ...


## Canister with sequence of update calls

Because a single update call is never enough, due to the instructions limit, a sequence of update calls is required. This is non-trivial, because the state of the LLM at the end of each update call must be saved.

The llama.cpp code has a caching mechanism that likely can be used for this purpose.

## Completions endpoint

Implement an endpoint that is very similar to the industry standard completions API. This will ensure that the LLM canister can be easily integrated into both Web3 and Web2 applications.

## Integrate into ICGPT

Once finished, we will integrate it all into [ICGPT](https://icgpt.icpp.world/).

## Support build on Ubuntu & Windows

Currently, the build process only works on a mac.
We will expand it to also work on Ubuntu & Windows

# Appendix A: DFINITY DeAI grant project

This project is sponsored by a DFINITY DeAI grant.

The status for milestone 1 & 2 are summarized below.

# Milestone 2 (Status on July 21, 2024)

The following tasks were completed:

- Implement stable memory in icpp-pro
  - Accomplished by integrating wasi2ic
  - Release in [icpp-pro 4.1.0](https://docs.icpp.world/)
- Port the ggerganov/llama.cpp C/C++ code to the IC
- Create an icpp-pro project that encapsulates the llama.cpp code
- Deploy it to local network
- Write python scripts to upload the model weights and tokenizer
- Test it locally
  - We were able to run the `stories260Ktok512.gguf` model
  - Token generation works, until it hits the instruction limit
- Implement CI/CD pipeline using GitHub actions

# Milestone 1 - 30 day sprint (COMPLETED May 12, 2024)

As part of an initial 30 day sprint, the following tasks were completed:

## Run llama.cpp locally

The procedure to compile & run & debug a CLANG++ compiled native version is described below.

## Load the different models from Huggingface

In the native version of llama.cpp, we tested the following models:

- All TinyStories models stored on [huggingface/onicai/llama_cpp_canister_models](https://huggingface.co/onicai/llama_cpp_canister_models)
- The 4q model on [huggingface/microsoft/Phi-3-mini-4k-instruct-gguf](https://huggingface.co/microsoft/Phi-3-mini-4k-instruct-gguf)

## Create Azure pipeline to train tinyStories model

We trained several TinyStories models and converted them into the \*.guff format required by llama.cpp
All models are stored on [huggingface/onicai/llama_cpp_canister_models](https://huggingface.co/onicai/llama_cpp_canister_models)

## study code and create implementation plan

We dug deep into the code and studied it by stepping through it in the debugger with VS Code. We have gained sufficient understanding to create a solid implementation plan.