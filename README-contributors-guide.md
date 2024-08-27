# Contributors Guide

# Setup

Follow steps of [llama_cpp_canister/README/Getting Started](https://github.com/onicai/llama_cpp_canister/blob/main/README.md#getting-started)

# How to debug original llama.cpp

- clone ggerganov/llama.cpp
- checkout the proper commit used as root of the onicai branch in llama_cpp_onicai_fork
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
                  "<PATH_TO>/llama_cpp_canister_models/stories260Ktok512.gguf",
                  "--samplers",
                  "top_p",
                  "--temp",
                  "0.1",
                  "--top-p",
                  "0.9",
                  "-n",
                  "600",
                  "-p",
                  "Joe loves writing stories"
              ]
          }
      ]
  }
  ```
# How to upgrade llama.cpp

## Sync fork
In GitHub, `Sync fork` for master branch of https://github.com/onicai/llama_cpp_onicai_fork

## llama_cpp_onicai_fork: setup a local branch
Take following steps locally:
- git fetch 

- Copy `src/llama_cpp_onicai_fork` to `<temp>/llama_cpp_onica_fork_<git-sha>`

  This is just as a reference. We will remove this folder once all done.

- from master, create a new branch: `onicai-<git-sha>`

  For `git-sha`, use the short commit sha from which we're branching.

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
- add a check on `llama_token_bos(model)`, else the llama2.c models never stop generating:
  ```
  bool llama_token_is_eog_impl(const struct llama_vocab & vocab, llama_token token) {
      return token != -1 && (
          token == llama_token_eos_impl(vocab) ||
          token == llama_token_eot_impl(vocab) || 
          token == llama_token_bos_impl(vocab) // ICPP-PATCH: the llama2.c model predicts bos without first predicting an eos
      );
  }
  ```

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

## llama_cpp_onicai_fork: replace `onicai` branch

Do NOT merge the `onicai-<git-sha>` branch into the `onicai` branch, but replace it:

```
git branch -m onicai onicai-<old-git-sha>
git branch -m onicai-<git-sha> onicai
git push origin onicai:onicai
git push origin onicai-<old-git-sha>:onicai-<old-git-sha>
```