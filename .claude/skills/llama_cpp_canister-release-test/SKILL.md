---
name: llama_cpp_canister-release-test
description: Test a llama_cpp_canister release zip end-to-end
disable-model-invocation: false
user-invocable: true
argument-hint: [release-tag]
allowed-tools: Bash, Read, AskUserQuestion
---

# Test llama_cpp_canister Release

Tests that a release zip contains everything needed and works end-to-end.

Follow these steps exactly in order. Abort and report on any failure.

## 1. Download & unzip release

Ask the user which release tag to test. If they say "latest", determine it with:

```bash
gh release view --repo onicai/llama_cpp_canister --json tagName --jq '.tagName'
```

Download and unzip:

```bash
rm -rf /tmp/llama_cpp_release_test
mkdir -p /tmp/llama_cpp_release_test
gh release download <TAG> --repo onicai/llama_cpp_canister --dir /tmp/llama_cpp_release_test
mkdir -p /tmp/llama_cpp_release_test/<TAG>
unzip /tmp/llama_cpp_release_test/llama_cpp_canister_<TAG>.zip -d /tmp/llama_cpp_release_test/<TAG>/
```

All subsequent commands run from `/tmp/llama_cpp_release_test/<TAG>/`.

## 2. Follow README setup instructions

### Verify dfx

```bash
dfx --version
```

Verify the version is >= 0.31.0. If not, warn the user.

### Create conda environment

```bash
source /opt/miniconda3/etc/profile.d/conda.sh
conda create -y -n llama_cpp_canister_release_test python=3.11
conda activate llama_cpp_canister_release_test
```

### Install dependencies

```bash
cd /tmp/llama_cpp_release_test/<TAG>
pip install -r requirements.txt
```

## 3. Deploy

```bash
cd /tmp/llama_cpp_release_test/<TAG>
dfx start --clean --background
dfx deploy --network local
dfx ledger fabricate-cycles --canister llama_cpp --t 20
```

## 4. Download Qwen2.5 model from HuggingFace

```bash
cd /tmp/llama_cpp_release_test/<TAG>
mkdir -p models/Qwen/Qwen2.5-0.5B-Instruct-GGUF
wget -c -O models/Qwen/Qwen2.5-0.5B-Instruct-GGUF/qwen2.5-0.5b-instruct-q8_0.gguf \
  https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/resolve/main/qwen2.5-0.5b-instruct-q8_0.gguf
```

Verify SHA256:

```bash
echo "ca59ca7f13d0e15a8cfa77bd17e65d24f6844b554a7b6c12e07a5f89ff76844e  models/Qwen/Qwen2.5-0.5B-Instruct-GGUF/qwen2.5-0.5b-instruct-q8_0.gguf" | shasum -a 256 -c
```

If the checksum does not match, abort immediately.

## 5. Upload model into canister

```bash
cd /tmp/llama_cpp_release_test/<TAG>
python -m scripts.upload --network local --canister llama_cpp \
  --canister-filename models/model.gguf --filetype gguf \
  --hf-sha256 "ca59ca7f13d0e15a8cfa77bd17e65d24f6844b554a7b6c12e07a5f89ff76844e" \
  models/Qwen/Qwen2.5-0.5B-Instruct-GGUF/qwen2.5-0.5b-instruct-q8_0.gguf
```

## 6. Run pytest

```bash
cd /tmp/llama_cpp_release_test/<TAG>
pytest -vv --network local test/test_qwen2.py
```

If any tests fail, report the failures but continue to cleanup.

## 7. Cleanup

```bash
cd /tmp/llama_cpp_release_test/<TAG>
dfx stop
```

Ask the user if they want to remove the test directory and conda environment. If yes:

```bash
rm -rf /tmp/llama_cpp_release_test
source /opt/miniconda3/etc/profile.d/conda.sh
conda env remove -y -n llama_cpp_canister_release_test
```

## 8. Summary

Report:
- Which release tag was tested
- Whether all tests passed or which tests failed
- Any issues encountered during the test
