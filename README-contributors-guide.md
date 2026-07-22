# Contributors Guide

# Setup

Follow steps of [llama_cpp_canister/README/Getting Started](https://github.com/onicai/llama_cpp_canister/blob/main/README.md#getting-started)

# How to upgrade llama.cpp

## Sync fork
In GitHub, `Sync fork` for master branch of https://github.com/onicai/llama_cpp_onicai_fork

## Fetch the tags from upstream repo

`llama.cpp` continously creates new releases, named `bxxxx`

You can fetch the tags from these releases and add them to our forked repo:

After cloning the `llama_cpp_onicai_fork` repo to you local computer:

```
# From llama_cpp_onicai_fork
git remote add upstream https://github.com/ggml-org/llama.cpp.git

# after this, the tags will apear locally
git fetch upstream --tags

# after this, the tags will appear in GitHub
git push origin --tags
```

## llama_cpp_onicai_fork: setup a local branch
Take following steps locally:
- git fetch 

- These are the git-sha values of the llama.cpp versions we branched from:

  | upgrade # | llama.cpp sha | llama.cpp release-tag |    date    |
  | --------- | ------------- | --------------------- | ---------- |
  |    0002   |     615212    |         b4532         | Feb  2 '25 |
  |    0001   |     b841d0    |         -             | Oct 18 '24 |
  |    0000   |     5cdb37    |         -             | Jul 21 '24 |


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

## Details for each upgrade

See the files: README-<upgrade #>-<llama.cpp sha>.md

## Recommended porting/validation order

Established during the b10076 upgrade (see `README-0003-305ba519.md`). Native tests pass
almost everything the real canister will reject, so escalate through four gates in order —
do NOT skip to a dfx deploy:

1. **Native** (`make all-tests` / MockIC) — fastest loop; catches API/merge/link errors and
   verifies exact-token output. But native has real mmap, threads, exceptions, stack and
   getenv, so a green native suite proves almost nothing about canister *runtime* behavior.
   (In b10076, native was 111/111 while five wasm-only bugs were still live.)

2. **Faithful wasmtime harness** (`scripts/wasm_harness.py`) — BEFORE touching dfx. The IC
   gives no wasm backtrace for a trap; this does.
   - Run the **pre-optimize** wasm `build/llama_cpp_before_opt.wasm` so backtraces show
     function NAMES (binaryen's `optimize()` strips the name section from the deployed wasm).
   - Just instantiating runs the C++ ctors → catches static-init faults. Pass `--method` (and
     a `didc encode`d arg) to reach faults deeper inside `load_model`/`run_update`.
   - Then run the optimized `build/llama_cpp.wasm` too, to confirm `optimize()` did not change
     behavior.

3. **Local IC replica** — confirmation, not primary debugging.
   - ALWAYS `dfx deploy` or `dfx canister install --wasm build/llama_cpp.wasm`; the `.dfx`
     cache can serve a stale binary. Verify the module hash changed after install.
   - Run the full pipeline: upload → `load_model` → `new_chat` → `run_update`.

4. **Mainnet** — throughput / behavior under the real 40B instruction cap.

**Interpretation rule that saves the most time:** if `scripts/wasm_harness.py` says the binary
is clean but the IC traps, suspect the DEPLOY PIPELINE (stale `.dfx` cache, wrong `--wasm`),
not the binary. In b10076, a multi-hour "install trap" chase was ultimately dfx installing a
stale cached wasm.

## Branch management

We need to rethink this logic, but for now it is ok...

### llama_cpp_onicai_fork
Do NOT merge the `onicai-<git-sha>` branch into the `onicai` branch, but replace it:

```
# do the onicai branch management while master branch is checked out
git checkout master
git branch -m onicai onicai-<git-sha-old>
git branch -m onicai-<git-sha-new> onicai
git push --force origin onicai:onicai
git push origin onicai-<git-sha-old>:onicai-<git-sha-old>
#
# Switch to the onicai branch, which now contains the <git-sha-new> version
git checkout onicai
```

## llama_cpp_canister

Merge the `onicai-<git-sha>` branch into the `onicai` branch