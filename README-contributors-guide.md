# Contributors Guide

# Setup

Follow steps of [llama_cpp_canister/README/Getting Started](https://github.com/onicai/llama_cpp_canister/blob/main/README.md#getting-started)

# How to debug original llama.cpp

- clone ggerganov/llama.cpp
- run these commands:
  ```
  make clean
  make LLAMA_DEBUG=1 llama-cli
  ```
- Then, debug using this `.vscode/launch.json`
  ```json
  {
      // Use IntelliSense to learn about possible attributes.
      // Hover to view descriptions of existing attributes.
      // For more information, visit: https://go.microsoft.com/fwlink/?linkid=830387
      "version": "0.2.0",
      "configurations": [
          {
              "type": "lldb",
              "request": "launch",
              "name": "llama-cli",
              "program": "${workspaceFolder}/llama-cli",
              "cwd": "${workspaceFolder}",
              "args": [
                  "-m",
                  "/Users/arjaan/onicai/repos_hf/llama_cpp_canister_models/stories260Ktok512.gguf",
                  "-p",
                  "Joe loves writing stories",
                  "-n",
                  "600",
                  "-c",
                  "128"
              ]
          }
      ]
  }
  ```
# How to upgrade llama.cpp

## Sync fork
In GitHub, `Sync fork` for master branch of https://github.com/onicai/llama_cpp_onicai_fork

## Setup a local branch
Take following steps locally:
- git fetch 

- Copy `src/llama_cpp_onicai_fork` to `src/llama_cpp_onica_fork_<git-sha>`

  This is just as a reference. We will remove this folder once all done.

- from master, create a new branch: `onicai-upgrade-<git-sha>`

  For `git-sha`, use the commit sha from which we're branching.

## Update all files

Unless something was drastically changed in llama.cpp, it is sufficient to just re-upgrade the files 
listed in [icpp.toml](https://github.com/onicai/llama_cpp_canister/blob/main/icpp.toml), plus their
header files.

As you do your upgrade, modify the descriptions below, to help with the next upgrade:
We use `meld` for comparing the files.

### cpp_paths

#### main_.cpp
`meld main_.cpp llama_cpp_onicai_fork/examples/main/main.cpp`
- use `main_` instead of `main`
- A few items related to console & ctrl+C need to be outcommented


#### llama_cpp_onicai_fork/src/llama.cpp
- add `#include "ic_api.h"`
- replace `throw std::runtime_error(format` with `IC_API::trap(std::string("RUNTIME ERROR: ") + format`
- replace `throw` with `IC_API::trap`
- outcomment `try - catch`. The program will abrupt in case of thrown exceptions.
- outcomment threading related items:
  - `#include <future>`
  - `#include <mutex>`
  - `#include <thread>`
- outcomment these functions completely:
  - `llama_tensor_quantize_internal`
  - `llama_model_quantize_internal`

#### llama_cpp_onicai_fork/src/llama-vocab.cpp
- add `#include "ic_api.h"`
- replace `throw std::runtime_error(format` with `IC_API::trap(std::string("RUNTIME ERROR: ") + format`
- outcomment `try - catch`. The program will abrupt in case of thrown exceptions.

#### llama_cpp_onicai_fork/src/llama-grammar.cpp
No changes needed

#### llama_cpp_onicai_fork/src/llama-sampling.cpp
No changes needed

#### llama_cpp_onicai_fork/src/unicode-data.cpp
- no modifications needed for the IC

#### llama_cpp_onicai_fork/src/unicode.cpp
- add `#include "ic_api.h"`
- replace `throw` with `IC_API::trap`

#### llama_cpp_onicai_fork/common/json-schema-to-grammar.cpp
- add `#include "ic_api.h"`
- replace `throw` with `IC_API::trap`
- outcomment `try - catch`. The program will abrupt in case of thrown exceptions.


#### llama_cpp_onicai_fork/common/build-info.cpp
- run this command to create it:
```
make build-info-cpp-wasm
``` 

#### llama_cpp_onicai_fork/common/grammar-parser.cpp
- add `#include "ic_api.h"`
- replace `throw` with `IC_API::trap`
- outcomment `try - catch`. The program will abrupt in case of thrown exceptions.

#### llama_cpp_onicai_fork/common/sampling.cpp
- add `#include "ic_api.h"`
- replace `throw` with `IC_API::trap`

#### llama_cpp_onicai_fork/common/common.cpp
- add `#include "ic_api.h"`
- replace `throw` with `IC_API::trap`
- outcomment all code related to `<pthread.h>`
- outcomment `try - catch`. The program will abrupt in case of thrown exceptions.
- outcomment `std::getenv`


---
### c_paths

#### llama_cpp_onicai_fork/ggml/src/ggml.c
- outcomment all code related to signals
  - `#include <signal.h>`
- Many threading outcomments. 

#### llama_cpp_onicai_fork/ggml/src/ggml-alloc.c
No updates needed for icpp-pro

#### llama_cpp_onicai_fork/ggml/src/ggml-backend.c
No updates needed for icpp-pro

#### llama_cpp_onicai_fork/ggml/src/ggml-quants.c
No updates needed for icpp-pro

#### llama_cpp_onicai_fork/ggml/src/ggml-aarch64.c
No updates needed for icpp-pro

---
### headers to modify

#### llama_cpp_onicai_fork/common/log.h
- `#include <thread>`
- Some other threading code

#### llama_cpp_onicai_fork/common/common.h
- `#include <thread>`