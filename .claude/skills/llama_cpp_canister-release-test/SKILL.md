---
name: llama_cpp_canister-release-test
description: Test a llama_cpp_canister release zip end-to-end (Qwen3-0.6B default flow)
disable-model-invocation: false
user-invocable: true
argument-hint: [release-tag]
allowed-tools: Bash, Read, AskUserQuestion
---

# Test llama_cpp_canister Release

Tests that a release zip contains everything needed and works end-to-end, using the
**Qwen3-0.6B** default flow (v0.13.0+). Follow these steps in order. Abort and report on
any failure — except the known local flake called out in step 8.

## 1. Download & unzip release

Ask the user which release tag to test. If they say "latest":

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

Sanity-check the zip: `build/llama_cpp.wasm` + `build/llama_cpp.did` present, `icp.yaml`
has **two** canisters (`llama_cpp` + `llama_cpp_qwen25`), and `test/test_qwen3.py` exists.

## 2. Start from a CLEAN local network

icp-cli runs one local network **per project** and has no `--clean` flag. The
clean-slate equivalent is to stop the network and remove the disposable cache. NEVER
remove `.icp/data` — it holds committed mainnet id mappings; only `.icp/cache` is
disposable:

```bash
icp network stop 2>/dev/null || true   # exits non-zero when none is running — that's fine
pkill -f pocket-ic 2>/dev/null || true
rm -rf .icp/cache
```

Verify the icp-cli version (`icp --version`); this flow was validated on 1.2.0.

## 3. Create conda environment + install deps

```bash
source /opt/miniconda3/etc/profile.d/conda.sh
conda create -y -n llama_cpp_canister_release_test python=3.11
conda activate llama_cpp_canister_release_test
cd /tmp/llama_cpp_release_test/<TAG>
pip install -r requirements.txt
```

(On Python 3.11 you get `binaryen.py 0.0.2`; that is expected and works — see the
`python-3.11-binaryen-globals` project memory.)

## 4. Deploy + configure the Qwen3 canister

`icp.yaml` defines two canisters — deploy **`llama_cpp`** (the Qwen3 default), not both.
`icp network start -d` picks a random ephemeral port; never hardcode `localhost:8000`.

Deploy as `llama-cpp-testing`: since icpp-pro 6.0.0 pytest is told which identity to
run as, and a canister's controller is whoever deployed it, so the two must agree.
Create it once with `icp identity new llama-cpp-testing --storage plaintext`.
Exporting `ICPP_PRO_TEST_IDENTITY` makes `python -m scripts.upload` (step 7) sign as
that identity too, which it must — only a controller may upload.

```bash
cd /tmp/llama_cpp_release_test/<TAG>
export ICPP_PRO_TEST_IDENTITY=llama-cpp-testing
icp network start -d
icp deploy llama_cpp -e local -y --identity "$ICPP_PRO_TEST_IDENTITY"
icp canister settings update llama_cpp --wasm-memory-limit 4026531840 -e local --identity "$ICPP_PRO_TEST_IDENTITY"   # 3.75 GiB — REQUIRED for Qwen3
icp canister top-up llama_cpp --amount 20000000000000 -e local                   # ~20T cycles
icp canister call llama_cpp health '()' -e local --query                         # -> Ok 200
icp canister status llama_cpp -e local | grep "Wasm memory limit"                # -> 4_026_531_840
```

## 5. Check the get_memory_status endpoint (new in v0.13.0)

```bash
icp canister call llama_cpp get_memory_status '()' -e local --query --identity "$ICPP_PRO_TEST_IDENTITY"  # -> Ok { wasm_heap_bytes; stable_bytes }
icp canister call llama_cpp get_memory_status '()' -e local --query --identity anonymous                  # -> Err "Access Denied"
```

## 6. Get the Qwen3 model

Download from HuggingFace, OR (faster) copy a local sha256-verified copy to skip the
639 MB download:

```bash
cd /tmp/llama_cpp_release_test/<TAG>
mkdir -p models/Qwen/Qwen3-0.6B-GGUF
wget -c -O models/Qwen/Qwen3-0.6B-GGUF/Qwen3-0.6B-Q8_0.gguf \
  https://huggingface.co/Qwen/Qwen3-0.6B-GGUF/resolve/main/Qwen3-0.6B-Q8_0.gguf
```

Verify SHA256 (abort immediately if it does not match):

```bash
echo "9465e63a22add5354d9bb4b99e90117043c7124007664907259bd16d043bb031  models/Qwen/Qwen3-0.6B-GGUF/Qwen3-0.6B-Q8_0.gguf" | shasum -a 256 -c
```

## 7. Upload model into canister

```bash
cd /tmp/llama_cpp_release_test/<TAG>
python -m scripts.upload --network local --canister llama_cpp \
  --canister-filename models/model.gguf --filetype gguf \
  --hf-sha256 "9465e63a22add5354d9bb4b99e90117043c7124007664907259bd16d043bb031" \
  models/Qwen/Qwen3-0.6B-GGUF/Qwen3-0.6B-Q8_0.gguf
```

Optionally confirm on-chain: `icp canister call llama_cpp -e local uploaded_file_details
'(record { filename = "models/model.gguf" })'` → filesize `639_446_688`, matching sha256.

## 8. Run pytest

```bash
cd /tmp/llama_cpp_release_test/<TAG>
pytest -vv --network local --identity "$ICPP_PRO_TEST_IDENTITY" test/test_qwen3.py
```

The suite loads Qwen3 at `--ctx-size 16384 --batch-size 64 --ubatch-size 64` (dual q8_0)
and runs non-thinking generation + multi-turn recall.

**KNOWN local flake — do not treat a single occurrence as a release failure.**
`test__multi_turn_recall` can *intermittently* trap with `heap out of bounds` (IC0502)
during generation on the **local pocket-ic** replica, even though the heap peaks at
~1.76 GiB — the same footprint as mainnet (where it passes 5/5) and well under the 3.75 GiB
limit. So it is **not** an out-of-memory condition, and it is recoverable (the canister keeps
working; a subsequent `load_model` succeeds). If — and only if — exactly this test fails,
**re-run the whole suite**; a clean 5/5 confirms it was a local boundary flake. A
deterministic failure of the other tests is a real problem.

## 9. (Optional) Qwen2.5 regression

The release also serves the previous default, Qwen2.5-0.5B, via the `llama_cpp_qwen25`
canister and `test/test_qwen2.py`. To exercise it: `icp deploy llama_cpp_qwen25 -e local -y --identity "$ICPP_PRO_TEST_IDENTITY"`, download
the Qwen2.5 gguf (sha256 `ca59ca7f13d0e15a8cfa77bd17e65d24f6844b554a7b6c12e07a5f89ff76844e`),
upload it to `llama_cpp_qwen25`, then `pytest -vv --network local --identity "$ICPP_PRO_TEST_IDENTITY" test/test_qwen2.py`.

## 10. Cleanup

```bash
cd /tmp/llama_cpp_release_test/<TAG>
icp network stop
```

Ask the user if they want to remove the test directory and conda environment. If yes:

```bash
rm -rf /tmp/llama_cpp_release_test
source /opt/miniconda3/etc/profile.d/conda.sh
conda env remove -y -n llama_cpp_canister_release_test
```

## 11. Summary

Report:
- Which release tag was tested
- Whether all tests passed (and whether a re-run was needed to clear the known
  multi_turn_recall local flake)
- Any issues encountered during the test
