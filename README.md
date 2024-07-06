[![llama_cpp_canister](https://github.com/onicai/llama_cpp_canister/actions/workflows/cicd.yml/badge.svg)](https://github.com/onicai/llama_cpp_canister/actions/workflows/cicd.yml)

![llama](https://user-images.githubusercontent.com/1991296/230134379-7181e485-c521-4d23-a0d6-f7b3b61ba524.png)
Run [ggerganov/llama.cpp](https://github.com/ggerganov/llama.cpp) in a canister of the Internet Computer.

# Status of DFINITY DeAI grant project: ICGPT V2

# 30 day sprint (COMPLETED)

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

We dug deep into the code and studied it by stepping through it in the debugger with VS Code. We have gained sufficient understanding to create a solid implementation plan, which is provided in the next section.

Work on this has already begun.

# Implementation plan

## icpp.toml

We identified the code from llama.cpp that must be included in the canister, and created the [icpp.toml](...) for the icpp-pro project that builds & deploys the canister:

```toml
[build-wasm]
...
cpp_paths = [
    "src/llama_cpp_onicai_fork/unicode-data.cpp",
    "src/llama_cpp_onicai_fork/unicode.cpp",
    "src/llama_cpp_onicai_fork/common/json-schema-to-grammar.cpp",
    "src/llama_cpp_onicai_fork/common/build-info.cpp",
    "src/llama_cpp_onicai_fork/common/grammar-parser.cpp",
    "src/llama_cpp_onicai_fork/common/sampling.cpp",
    "src/llama_cpp_onicai_fork/common/common.cpp",
    "src/llama_cpp_onicai_fork/llama.cpp",
]
...
c_paths = [
    "src/llama_cpp_onicai_fork/ggml.c",
    "src/llama_cpp_onicai_fork/ggml-alloc.c",
    "src/llama_cpp_onicai_fork/ggml-backend.c",
    "src/llama_cpp_onicai_fork/ggml-quants.c",
]
...
[build-native]
cpp_paths = [
    "src/llama_cpp_onicai_fork/examples/main/main.cpp",
    "src/llama_cpp_onicai_fork/common/console.cpp",
]
...
```

## Native debug build

The native build is already functional, but this will change drastically as we need to refactor main.cpp dramatically, since the canister is not a console application:

```bash
# build the native executable
icpp build-native

# run it
./build/mockic.exe -m ../../repos_hf/llama_cpp_canister_models/stories15Mtok4096.gguf -p "Patrick loves ice-cream. On a hot day " -n 600 -c 128
```

## Baseline canister, with one update call

I first want to create the baseline canister, with an LLM loaded, and I can do one update call. The LLM must generate tokens until it runs into the instructions limit.

I want to be able to run variations of two types of models:

1. the TinyStories models:

   These small models are fantastic for fleshing out the implementation:

   - 260K, 15M, 42M, 110M
   - non-quantized
   - quantized with 4-bits

2. Larger models fine-tuned for chat:

   This will allow you to have a conversation, as you do in ChatGPT:

   - [microsoft/phi-3](...), quantized with 4-bits

### Code refactoring

I need to refactor the llama.cpp application for running in a canister:

1. Exceptions are not allowed on the IC
   - Replace all `throw` statements with a `trap`
   - Refactor all `try-catch` statements
2. ...other items that will surface during compilation with wasi-sdk...
3. No console
   - Refactor `examples/main.cpp`, so the console code becomes an API, callable from canister endpoint

### Model upload endpoint

When running locally, llama.cpp reads the model+tokenizer from a _.gguf file.
We need to create a canister endpoint to upload this _.gguf file as raw bytes, and then map it into the memory structures of the LLM.

### Completions endpoint

I plan to implement an endpoint that is very similar to the industry standard completions API. This will ensure that the LLM canister can be easily integrated into both Web3 and Web2 applications.

## Canister with sequence of update calls

Because a single update call is never enough, due to the instructions limit, a sequence of update calls is required. This is non-trivial, because the state of the LLM at the end of each update call must be saved.

I did an initial study on the data structures of the llama.cpp code, but it is not immediately clear what data must be preserved to be able to continue the token generation in a sub-sequent call. The internal data structure is very different from llama2.c, which had a very nice `RunState` data-structure that contained everything needed.

The llama.cpp code has a caching mechanism that potentially can be used for this purpose, but this is not yet proven. More research will be needed.

Even so, the steps required for this task are clear:

- Save LLM state after update call
- Expland completions API endpoint to allow a sequence of update calls for token generation until done

# Setup

Clone the repo and it's children:

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

Create a Python environment with dependencies installed

```bash
# We use MiniConda
conda create --name llama_cpp_canister python=3.11
conda activate llama_cpp_canister

# Install the python dependencies
pip install -r requirements.txt
```

# Models

## HuggingFace

You can find many models in the llama.cpp \*.gguf format on HuggingFace.

Just upload them into your IC canister to create your own, private on-chain LLM.

We're providing here a small list of available models, and will indicate if they run or not yet.

### TinyStories - [onicai/llama2_cpp_canister_models](https://huggingface.co/onicai/llama2_cpp_canister_models)

| model                    | notes                                              |
| ------------------------ | -------------------------------------------------- |
| stories260Ktok512.guff   | Use this for development & debugging               |
| stories15Mtok4096.guff   | Fits in canister & works well !                    |
| stories42Mtok4096.guff   | As of April 28, hits instruction limit of canister |
| stories42Mtok32000.guff  | As of April 28, hits instruction limit of canister |
| stories110Mtok32000.guff | As of April 28, hits instruction limit of canister |

# Run/Debug original llama.cpp on-device

You can test the models by first running them in regular llama.cpp, on your device, using the clang compiler.

To run & debug locally with the original llama.cpp code, do the following:

- Check out the `master` branch
- Follow instructions of examples/main/README.md:

  - Get a model from [huggingface/onicai/llama_cpp_canister_models](https://huggingface.co/onicai/llama_cpp_canister_models)

  - Build with clang++, using debug:

    ```
    # With flags used by compile to wasm for the IC canister
    make main LLAMA_DEBUG=1 CC=clang CXX=clang++ LLAMA_NO_METAL=1 LLAMA_NO_ACCELERATE=1 LLAMA_NO_LLAMAFILE=1

    # With acceleration for local hardware
    make main LLAMA_DEBUG=1 CC=clang CXX=clang++ LLAMA_NO_METAL=1
    ```

  - Run it:

    ```
    ./main -m models/stories15Mtok4096.gguf -p "Joe loves writing stories" -n 600 -c 128
    ```

  - Debug it:
    - Set breakpoints in `examples/main/main.cpp`
    - Use lldb to debug
