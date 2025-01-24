# Contributors Guide

# Setup

Follow steps of [llama_cpp_canister/README/Getting Started](https://github.com/onicai/llama_cpp_canister/blob/main/README.md#getting-started)

# VS Code debugger

## lldb-mi hangs

On the Mac, there is an issue with lldb-mi: https://github.com/microsoft/vscode-cpptools/issues/7240

Upon stopping at a breakpoint in a new module, lldb-mi will try to load all local variables, and it goes into an endless loop.

The solution is to hide the VARIABLES section in the debug window, and rely on the WATCH section instead.

# How to run & debug original llama.cpp

- Clone ggerganov/llama.cpp  (Do NOT initialize submodules...)
  ```
  # Clone it as a sibling repo of llama_cpp_canister
  git clone https://github.com/ggerganov/llama.cpp.git
  ```
- Checkout the proper commit used as root of the onicai branch in llama_cpp_onicai_fork
  ```
  git checkout b841d0
  ```
- Build with these commands:
  ```
  make clean
  make LLAMA_DEBUG=1 llama-cli
  ```
- Run with Notebook

  File: scripts/prompt-design.ipynb

- Run with this command:
  ```
  ./llama-cli -m ../llama_cpp_canister/models/Qwen/Qwen2.5-0.5B-Instruct-GGUF/qwen2.5-0.5b-instruct-q8_0.gguf --prompt-cache prompt.cache --prompt-cache-all -sp -p "<|im_start|>system\nYou are a helpful assistant.<|im_end|>\n<|im_start|>user\ngive me a short introduction to LLMs.<|im_end|>\n<|im_start|>assistant\n" -n 512 -fa -ngl 80
  ```
  In above command, the `-fa -ngl 80` arguments are useful only on GPU. We do not use them when calling the IC, because
  the canister has a CPU only.
  
- Debug using this `.vscode/launch.json`
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

## Fetch the tags from upstream repo

`llama.cpp` continously creates new releases, named `bxxxx`

You can fetch the tags from these releases and add them to our forked repo:

After cloning the `llama_cpp_onicai_fork` repo to you local computer:

```
# From llama_cpp_onicai_fork
git remote add upstream https://github.com/ggerganov/llama.cpp.git

# after this, the tags will apear locally
git fetch upstream --tags

# after this, the tags will appear in GitHub
git push origin --tags


```

## llama_cpp_onicai_fork: setup a local branch
Take following steps locally:
- git fetch 

- This is the git-sha of the llama.cpp versions we branched from:
  - `615212` (git-sha-new)  , with release-tag `b4532`
  - `b841d0` (git-sha-old)  , no   release-tag
  - `5cdb37` (git-sha-older), no   release-tag

- Start with a fresh clone of llama_cpp_onicai_fork:
  ```bash
  # From folder: llama_cpp_canister\src

  # Copy old version, as a reference to use with meld
  # This is just as a reference. You can remove this folder once all done.
  # (-) Make sure the current `onicai` branch is checked out.
  #     The one that branched off from `git-sha-old`
  cp llama_cpp_onicai_fork llama_cpp_onicai_fork_<git-sha-old>

  # Clone the new version in place
  git clone git@github.com:onicai/llama_cpp_onicai_fork.git
  ```

- In llama_cpp_onicai_fork, from master, create a new branch: `onicai-<git-sha-new>`

  For `git-sha-new`, use the short commit sha from which we're branching.

## Update all files

Unless something was drastically changed in llama.cpp, it is sufficient to just re-upgrade the files 
listed in [icpp.toml](https://github.com/onicai/llama_cpp_canister/blob/main/icpp.toml), plus their
header files.

As you do your upgrade, modify the descriptions below, to help with the next upgrade:
We use `meld` for comparing the files:

```bash
brew install --cask dehesselle-meld
```

### cpp_paths

#### main_.cpp

```bash
# from folder: llama_cpp_canister/src

# To do the actual changes
meld main_.cpp llama_cpp_onicai_fork/examples/main/main.cpp

# To check what has changed between <git-sha-new> and <git-sha-old>
meld llama_cpp_onicai_fork/examples/main/main.cpp llama_cpp_onicai_fork_<git-sha-old>/examples/main/main.cpp
```
- use `main_` instead of `main`
- A few items related to console, ctrl+C & threading need to be outcommented
- Added logic for running in a canister with multiple update calls


#### llama_cpp_onicai_fork/src/llama.cpp
```bash
# from folder: llama_cpp_canister/src
# To do the actual changes
meld llama_cpp_onicai_fork/src/llama.cpp llama_cpp_onicai_fork_<git-sha-old>/src/llama.cpp
```
- add `#include "ic_api.h"`
- replace `throw std::runtime_error` with `IC_API::trap`
- outcomment `try - catch`. The program will abrupt in case of thrown exceptions.
- outcomment threading related items
- outcomment these functions completely:
  - `llama_tensor_quantize_internal`
  - `llama_model_quantize_internal`


#### llama_cpp_onicai_fork/src/llama-vocab.cpp
```bash
# from folder: llama_cpp_canister/src
meld llama_cpp_onicai_fork/src/llama-vocab.cpp llama_cpp_onicai_fork_<git-sha-old>/src/llama-vocab.cpp
```
- add `#include "ic_api.h"`
- replace `throw std::runtime_error` with `IC_API::trap`
- outcomment `try - catch`. The program will abrupt in case of thrown exceptions.

#### llama_cpp_onicai_fork/src/llama-grammar.cpp
- add `#include "ic_api.h"`
- replace `throw std::runtime_error` with `IC_API::trap`
- outcomment `try - catch`. The program will abrupt in case of thrown exceptions.

#### llama_cpp_onicai_fork/src/llama-sampling.cpp
- add `#include "ic_api.h"`
- replace `throw std::runtime_error` with `IC_API::trap`

#### llama_cpp_onicai_fork/src/llama-impl.cpp
- no modifications needed for the IC

#### src/llama_cpp_onicai_fork/src/llama-context.cpp
- add `#include "ic_api.h"`
- replace `throw std::runtime_error` with `IC_API::trap`

#### src/llama_cpp_onicai_fork/src/llama-arch.cpp
- no modifications needed for the IC

#### llama_cpp_onicai_fork/src/unicode-data.cpp
- no modifications needed for the IC

#### llama_cpp_onicai_fork/src/unicode.cpp
- add `#include "ic_api.h"`
- replace `throw std::runtime_error` with `IC_API::trap`
- replace `throw std::invalid_argument` with `IC_API::trap`
- outcomment `try - catch`. The program will abrupt in case of thrown exceptions.

#### llama_cpp_onicai_fork/src/llama-kv-cache.cpp
- no modifications needed for the IC

#### llama_cpp_onicai_fork/src/llama-chat.cpp
- outcomment `try - catch`. The program will abrupt in case of thrown exceptions.

#### llama_cpp_onicai_fork/src/llama-mmap.cpp
- add `#include "ic_api.h"`
- replace `throw std::runtime_error` with `IC_API::trap`

#### llama_cpp_onicai_fork/src/llama-model.cpp
- add `#include "ic_api.h"`
- replace `throw std::runtime_error` with `IC_API::trap`
- outcomment `try - catch`. The program will abrupt in case of thrown exceptions.

#### llama_cpp_onicai_fork/src/llama-batch.cpp
- no modifications needed for the IC

#### llama_cpp_onicai_fork/src/llama-adapter.cpp
- add `#include "ic_api.h"`
- replace `throw std::runtime_error` with `IC_API::trap`
- outcomment `try - catch`. The program will abrupt in case of thrown exceptions.

#### llama_cpp_onicai_fork/src/llama-model-loader.cpp
- add `#include "ic_api.h"`
- replace `throw std::runtime_error` with `IC_API::trap`

#### llama_cpp_onicai_fork/src/llama-hparams.cpp
- no modifications needed for the IC

#### llama_cpp_onicai_fork/common/arg.cpp
- add `#include "ic_api.h"`
- replace `throw std::runtime_error` with `IC_API::trap`
- replace `throw std::invalid_argument` with `IC_API::trap`
- outcomment `try - catch`. The program will abrupt in case of thrown exceptions.

#### llama_cpp_onicai_fork/common/json-schema-to-grammar.cpp
- add `#include "ic_api.h"`
- replace `throw std::runtime_error` with `IC_API::trap`
- replace `throw std::out_of_range` with `IC_API::trap`
- outcomment `try - catch`. The program will abrupt in case of thrown exceptions.

#### llama_cpp_onicai_fork/common/build-info.cpp
- run this command to create it:
```
make build-info-cpp-wasm
``` 

#### llama_cpp_onicai_fork/common/sampling.cpp
- add `#include "ic_api.h"`
- replace `throw std::runtime_error` with `IC_API::trap`

#### llama_cpp_onicai_fork/common/common.cpp
- add right below `#include llama.h`:
```C++
// ICPP-PATCH-START
#include "ic_api.h"
extern llama_model ** g_model; // The global variable from main_.cpp
// ICPP-PATCH-END
```
- replace `throw std::runtime_error` with `IC_API::trap`
- replace `throw std::invalid_argument` with `IC_API::trap`
- outcomment `try - catch`. The program will abrupt in case of thrown exceptions.
- outcomment `std::getenv`
  Compare to changes made last time (!)

- outcomment all code related to `<pthread.h>`:
  Compare to changes made last time (!)
  - cpu_get_num_physical_cores

- outcomment #ifdef LLAMA_USE_CURL
  Compare to changes made last time (!)

#### llama_cpp_onicai_fork/common/log.cpp
- Remove all threading logic
  #include <mutex>
  #include <thread>

#### llama_cpp_onicai_fork/ggml/src/ggml-backend.cpp
No updates needed for icpp-pro

---
### c_paths

#### llama_cpp_onicai_fork/ggml/src/ggml.c
- outcomment all code related to signals & threading
  - `#include "ggml-threading.h"`
  - `#include <signal.h>`


#### llama_cpp_onicai_fork/ggml/src/ggml-alloc.c
No updates needed for icpp-pro

#### llama_cpp_onicai_fork/ggml/src/ggml-quants.c
No updates needed for icpp-pro

#### llama_cpp_onicai_fork/ggml/src/ggml-threading.cpp
- outcomment all code related to threading

#### llama_cpp_onicai_fork/ggml/src/ggml-backend-reg.cpp
No updates needed for icpp-pro

#### llama_cpp_onicai_fork/ggml/src/gguf.cpp
- outcomment `try - catch`. The program will abrupt in case of thrown exceptions.

---
### headers to modify

#### llama_cpp_onicai_fork/common/chat-template.hpp
- replace `throw std::runtime_error` with `IC_API::trap`
- outcomment `try - catch`. The program will abrupt in case of thrown exceptions.

## llama_cpp_onicai_fork: replace `onicai` branch

TODO: RETHINK THIS LOGIC...
(-) Perhaps it is better to keep all the `onicai-<git-sha-...>` branches
(-) And just change the default branch to `onicai-<git-sha-new>`

That way:
(-) when someone clones, the are at the correct branch
(-) from the name, it is immediately clear what llama.cpp version was used
(-) we preserve the full history

---
Do NOT merge the `onicai-<git-sha>` branch into the `onicai` branch, but replace it:

```
git branch -m onicai onicai-<git-sha-old>
git branch -m onicai-<git-sha-new> onicai
git push origin onicai:onicai
git push origin onicai-<git-sha-old>:onicai-<git-sha-old>
```


------------
TODO: search in code files for: TODO-615212

(-) main_.cpp has a new static `global g_smpl`:
    static common_sampler          ** g_smpl;

    Q: Does this need to become a global variable, accessible from common.cpp ?
       Like we did for g_model ?

       In `common/common.cpp` we added:    
        ```
        // ICPP-PATCH-START
        #include "ic_api.h"
        extern llama_model ** g_model; // The global variable from main_.cpp
        // ICPP-PATCH-END
        ```

(-) main_.cpp renamed type for `g_params`:
    from: static gpt_params               * g_params;
    to  : static common_params            * g_params;

    Q: Does this need to become a global variable, accessible from common.cpp ?
       Like we did for g_model ?

(-) main_.cpp line 142: common_sampler * smpl = nullptr;

    Q: Does `smpl` need to become a static variable, like `model` & `ctx` ?

(-) main_.cpp line 147: // Don't give error if embd_inp = session_tokens. All is OK to just keep going

    Q: Is this logic for prompt_remaining still valid?

(-) main_.cpp line 208: // ICPP-TODO-START: This section is completely new...
    COMPLETELY NEW SECTION FOR THREADPOOLs... 

(-) LOG & LOG_TEE have been replaced by LOG, LOG_ERR, LOG_WRN, LOG_INF, LOG_CNT
    -> LOG is used just for Console/Stream Output
    -> LOG_xxx is used for ERR, WRN, INF, CNT --> Not sure yet where this goes...

    Q1: Did we change anything to LOG & LOG_TEE to get it to work ?
    Q2: Are we still using LOG & LOG_TEE ourselvs? If so, replace it.
    Q3: Can we remove the LOG & LOG_TEE 
    Q4: Do we need to update the README about downloading different LOG files?

(-) main_.cpp calls common_token_to_piece instead of llama_token_to_piece

    Q: Is this a new file:  common_token_to_piece
    A: No, it is in common.cpp

(-) main_.cpp calls common_tokenize instead of llama_tokenize

    Q: Is this a new file:  common_tokenize
    A: No, it is in common.cpp

(-) main_.cpp line 516, 826: New sampling subsystem !

    Q: Are these new files: 
       - common_sampler_init
       - common_sampler_sample
       - common_sampler_accept
    A: No, it is in sampling.cpp

(-) main_.cpp line 1123: common_sampler_free(smpl)

    We had outcommented code to NOT free the ctx & model storage:
    // Do NOT free ctx & model storage
    // -> we made `ctx` & `model` data static, so they are maintained across calls to the LLM
    // -> we do NOT reset g_ctx & g_model
    // -> we moved this into a free_model function, which can be called by canister's load_model
    // llama_free(ctx);
    // llama_free_model(model);

    // TODO-615212 -- Make sure this is correct
    // Do reset all other static memory
    reset_static_memory();

    Q1: Has this all moved into common_sampler_free ?

    Q2: Update usage of the free_model function?

    Q3: is reset_static_memory still correct ? 
    
    Q4: Is llama_sampling_free(ctx_sampling) now handled by common_sampler_free(smpl) ?


(-) llama-vocab.cpp  --- This function is no longer there. Is tinystories still working?

    We had added a check on `llama_token_bos(model)`, else the llama2.c models never stop generating:
      ```
      bool llama_token_is_eog_impl(const struct llama_vocab & vocab, llama_token token) {
          return token != -1 && (
              token == llama_token_eos_impl(vocab) ||
              token == llama_token_eot_impl(vocab) || 
              token == llama_token_bos_impl(vocab) // ICPP-PATCH: the llama2.c model predicts bos without first predicting an eos
          );
      }
      ```

(-) DEBUG: `llama_cpp_onicai_fork/common/log.cpp` step through the logic
          - verify the outcommented logic makes sense, or if we should just
            completely remove the pause() & resume() functions.

----------------------------------------------------------
NOTES:

(-) main_.cpp includes a new file: `llama_cpp_onicai_fork/common/chat-template.hpp`
    This is from Google, and a general chat_template, with tool calling !!!

(-) All the LLM architectures supported by llama_cpp_canister are listed in 
    `src/llama_cpp_onicai_fork/src/llama-arch.cpp`

(-) NOTE: `common/grammar-parser.cpp` is no longer there.
          It appears to be fully included in `src/llama-grammar.cpp`

(-) NOTE: `llama_cpp_onicai_fork/ggml/src/ggml-backend.cpp` used to be `llama_cpp_onicai_fork/ggml/src/ggml-backend.c`

(-) NOTE: `llama_cpp_onicai_fork/ggml/src/ggml-aarch64.c` no longer exists
          Previous update: No updates needed for icpp-pro

(-) NOTE: `llama_cpp_onicai_fork/common/log.h` no update was needed this time:
          Previous update:
          - `#include <thread>`
          - Some other threading code

(-) NOTE: `llama_cpp_onicai_fork/common/common.h` no update was needed this time:
          Previous update:
          - `#include <thread>`