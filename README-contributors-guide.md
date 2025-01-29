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
git remote add upstream https://github.com/ggerganov/llama.cpp.git

# after this, the tags will apear locally
git fetch upstream --tags

# after this, the tags will appear in GitHub
git push origin --tags
```

## llama_cpp_onicai_fork: setup a local branch
Take following steps locally:
- git fetch 

- These are the git-sha values of the llama.cpp versions we branched from:

  | upgrade # | llama.cpp sha | llama.cpp release-tag |
  | --------- | ------------- | --------------------- |
  |    0000   |     5cdb37    |         -             |
  |    0001   |     b841d0    |         -             |
  |    0002   |     615212    |         b4532         |


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

## Branch management

We need to rethink this logic, but for now it is ok...

### llama_cpp_onicai_fork
Do NOT merge the `onicai-<git-sha>` branch into the `onicai` branch, but replace it:

```
git branch -m onicai onicai-<git-sha-old>
git branch -m onicai-<git-sha-new> onicai
git push origin onicai:onicai
git push origin onicai-<git-sha-old>:onicai-<git-sha-old>
```

## llama_cpp_canister

Merge the `onicai-<git-sha>` branch into the `onicai` branch