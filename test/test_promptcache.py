"""Test promptcache APIs

First deploy the canister:
$ icpp build-wasm
$ icp deploy -e local -y

Then run the tests:
$ pytest -vv --network local --identity "$(icp identity default)" test/test_promptcache.py

To run it against a deployment to the IC, just replace `local` with `production` in the commands above.

"""
# pylint: disable=missing-function-docstring, unused-import, wildcard-import, unused-wildcard-import, line-too-long

import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import Dict
import pytest
from .candid_compat import call_canister_api, dict_to_candid_text, norm

# Path to the icp.yaml file
ICP_YAML_PATH = Path(__file__).parent / "../icp.yaml"

# Canister in the icp.yaml file we want to test
CANISTER_NAME = "llama_cpp"


# =============================================================================
# Anonymous Access Denial Tests
# =============================================================================

def test__upload_prompt_cache_chunk_anonymous(identity_anonymous: Dict[str, str], network: str) -> None:
    """Test that upload_prompt_cache_chunk rejects anonymous caller"""
    assert identity_anonymous["identity"] == "anonymous"
    assert identity_anonymous["principal"] == "2vxsx-fae"

    response = call_canister_api(
        icp_yaml_path=ICP_YAML_PATH,
        canister_name=CANISTER_NAME,
        canister_method="upload_prompt_cache_chunk",
        canister_argument='(record { promptcache = "test.cache"; chunk = blob "\\01\\02\\03"; chunksize = 3 : nat64; offset = 0 : nat64 })',
        network=network,
    )
    expected_response = '(variant { Err = variant { Other = "Access Denied" } })'
    assert response == norm(expected_response)


def test__download_prompt_cache_chunk_anonymous(identity_anonymous: Dict[str, str], network: str) -> None:
    """Test that download_prompt_cache_chunk rejects anonymous caller"""
    assert identity_anonymous["identity"] == "anonymous"
    assert identity_anonymous["principal"] == "2vxsx-fae"

    response = call_canister_api(
        icp_yaml_path=ICP_YAML_PATH,
        canister_name=CANISTER_NAME,
        canister_method="download_prompt_cache_chunk",
        canister_argument='(record { promptcache = "test.cache"; chunksize = 1024 : nat64; offset = 0 : nat64 })',
        network=network,
    )
    expected_response = '(variant { Err = variant { Other = "Access Denied" } })'
    assert response == norm(expected_response)


def test__uploaded_prompt_cache_details_anonymous(identity_anonymous: Dict[str, str], network: str) -> None:
    """Test that uploaded_prompt_cache_details rejects anonymous caller"""
    assert identity_anonymous["identity"] == "anonymous"
    assert identity_anonymous["principal"] == "2vxsx-fae"

    response = call_canister_api(
        icp_yaml_path=ICP_YAML_PATH,
        canister_name=CANISTER_NAME,
        canister_method="uploaded_prompt_cache_details",
        canister_argument='(record { promptcache = "test.cache" })',
        network=network,
    )
    expected_response = '(variant { Err = variant { Other = "Access Denied" } })'
    assert response == norm(expected_response)


# =============================================================================
# Controller Success Tests
# =============================================================================

def test__upload_prompt_cache_chunk_0(network: str, principal: str) -> None:
    response = call_canister_api(
        icp_yaml_path=ICP_YAML_PATH,
        canister_name=CANISTER_NAME,
        canister_method="upload_prompt_cache_chunk",
        canister_argument='(record { promptcache = "prompt.cache"; chunk = blob "\\47\\47\\55\\46\\03"; chunksize = 5 : nat64; offset = 0 : nat64; })',
        network=network,
    )
    expected_response = f'(variant {{ Ok = record {{ filename = ".canister_cache/{principal}/sessions/prompt.cache"; filesize = 5 : nat64; filesha256 = "fe3b34fd092c3e2c6da3270eb91c4d3e9c2c6f891c21b6ed7358bf5ecca2d207";}} }})'
    assert response == norm(expected_response)

def test__upload_prompt_cache_chunk_1(network: str, principal: str) -> None:
    response = call_canister_api(
        icp_yaml_path=ICP_YAML_PATH,
        canister_name=CANISTER_NAME,
        canister_method="upload_prompt_cache_chunk",
        canister_argument='(record { promptcache = "prompt.cache"; chunk = blob "\\08\\33\\41\\43\\04"; chunksize = 5 : nat64; offset = 5 : nat64; })',
        network=network,
    )
    expected_response = f'(variant {{ Ok = record {{ filename = ".canister_cache/{principal}/sessions/prompt.cache"; filesize = 10 : nat64; filesha256 = "438bb530032946102742839ca22319211409cbd1c403f87a82e68e35e89e8c15";}} }})'
    assert response == norm(expected_response)

# Start another upload to test concurrent uploads
def test__upload_prompt_cache_chunk_another_0(network: str, principal: str) -> None:
    response = call_canister_api(
        icp_yaml_path=ICP_YAML_PATH,
        canister_name=CANISTER_NAME,
        canister_method="upload_prompt_cache_chunk",
        canister_argument='(record { promptcache = "another_prompt.cache"; chunk = blob "\\03\\46\\55\\47\\47"; chunksize = 5 : nat64; offset = 0 : nat64; })',
        network=network,
    )
    expected_response = f'(variant {{ Ok = record {{ filename = ".canister_cache/{principal}/sessions/another_prompt.cache"; filesize = 5 : nat64; filesha256 = "fd789322b6e4d1a517f1b75768f0f9ebc5747076811ee04e8a5f0731320f4884";}} }})'
    assert response == norm(expected_response)

def test__upload_prompt_cache_chunk_2(network: str, principal: str) -> None:
    response = call_canister_api(
        icp_yaml_path=ICP_YAML_PATH,
        canister_name=CANISTER_NAME,
        canister_method="upload_prompt_cache_chunk",
        canister_argument='(record { promptcache = "prompt.cache"; chunk = blob "\\03\\46\\55\\47\\47"; chunksize = 5 : nat64; offset = 10 : nat64; })',
        network=network,
    )
    expected_response = f'(variant {{ Ok = record {{ filename = ".canister_cache/{principal}/sessions/prompt.cache"; filesize = 15 : nat64; filesha256 = "f042bddd385f498e5888d99d83b4e86cde25589356b4a9c89e705413a7b2dc57";}} }})'
    assert response == norm(expected_response)

# Continue the other upload to test concurrent uploads
def test__upload_prompt_cache_chunk_another_1(network: str, principal: str) -> None:
    response = call_canister_api(
        icp_yaml_path=ICP_YAML_PATH,
        canister_name=CANISTER_NAME,
        canister_method="upload_prompt_cache_chunk",
        canister_argument='(record { promptcache = "another_prompt.cache"; chunk = blob "\\04\\33\\41\\43\\08"; chunksize = 5 : nat64; offset = 5 : nat64; })',
        network=network,
    )
    expected_response = f'(variant {{ Ok = record {{ filename = ".canister_cache/{principal}/sessions/another_prompt.cache"; filesize = 10 : nat64; filesha256 = "a76c5d12c465c064d093af7678023364d20622935fe951e29d5c28a6ef66572b";}} }})'
    assert response == norm(expected_response)

def test__uploaded_prompt_cache_details(network: str, principal: str) -> None:
    response = call_canister_api(
        icp_yaml_path=ICP_YAML_PATH,
        canister_name=CANISTER_NAME,
        canister_method="uploaded_prompt_cache_details",
        canister_argument='(record { promptcache = "prompt.cache"; })',
        network=network,
    )
    expected_response = f'(variant {{ Ok = record {{ filename = ".canister_cache/{principal}/sessions/prompt.cache"; filesize = 15 : nat64; filesha256 = "f042bddd385f498e5888d99d83b4e86cde25589356b4a9c89e705413a7b2dc57";}} }})'
    assert response == norm(expected_response)

def test__download_prompt_cache_chunk_0(network: str, principal: str) -> None:
    response = call_canister_api(
        icp_yaml_path=ICP_YAML_PATH,
        canister_name=CANISTER_NAME,
        canister_method="download_prompt_cache_chunk",
        canister_argument='(record { promptcache = "prompt.cache"; chunksize = 5 : nat64; offset = 0 : nat64;})',
        network=network,
    )
    expected_response = '(variant { Ok = record { done = false; chunk = blob "\\47\\47\\55\\46\\03"; offset = 0 : nat64; filesize = 15 : nat64; chunksize = 5 : nat64;} })'
    assert response == norm(expected_response)

def test__download_prompt_cache_chunk_1(network: str, principal: str) -> None:
    response = call_canister_api(
        icp_yaml_path=ICP_YAML_PATH,
        canister_name=CANISTER_NAME,
        canister_method="download_prompt_cache_chunk",
        canister_argument='(record { promptcache = "prompt.cache"; chunksize = 5 : nat64; offset = 5 : nat64;})',
        network=network,
    )
    expected_response = '(variant { Ok = record { done = false; chunk = blob "\\08\\33\\41\\43\\04"; offset = 5 : nat64; filesize = 15 : nat64; chunksize = 5 : nat64;} })'
    assert response == norm(expected_response)

def test__download_prompt_cache_chunk_2(network: str, principal: str) -> None:
    response = call_canister_api(
        icp_yaml_path=ICP_YAML_PATH,
        canister_name=CANISTER_NAME,
        canister_method="download_prompt_cache_chunk",
        canister_argument='(record { promptcache = "prompt.cache"; chunksize = 5 : nat64; offset = 10 : nat64;})',
        network=network,
    )
    expected_response = '(variant { Ok = record { done = true; chunk = blob "\\03\\46\\55\\47\\47"; offset = 10 : nat64; filesize = 15 : nat64; chunksize = 5 : nat64;} })'
    assert response == norm(expected_response)

def test__download_prompt_cache_chunk_another_full(network: str, principal: str) -> None:
    response = call_canister_api(
        icp_yaml_path=ICP_YAML_PATH,
        canister_name=CANISTER_NAME,
        canister_method="download_prompt_cache_chunk",
        canister_argument='(record { promptcache = "another_prompt.cache"; chunksize = 1000 : nat64; offset = 0 : nat64;})',
        network=network,
    )
    expected_response = '(variant { Ok = record { done = true; chunk = blob "\\03\\46\\55\\47\\47\\04\\33\\41\\43\\08"; offset = 0 : nat64; filesize = 10 : nat64; chunksize = 10 : nat64;} })'
    assert response == norm(expected_response)


# ------------------------------------------------------------------
# The details of an uploaded cache only count while the file exists with its
# uploaded size: after remove_prompt_cache they are not found.
def test__uploaded_prompt_cache_details_after_remove(network: str) -> None:
    def call(method: str, arg: str) -> str:
        return call_canister_api(
            icp_yaml_path=ICP_YAML_PATH,
            canister_name=CANISTER_NAME,
            canister_method=method,
            canister_argument=arg,
            network=network,
        )

    response = call(
        "upload_prompt_cache_chunk",
        '(record { promptcache = "repro.cache"; chunk = blob "\\01\\02\\03\\04\\05"; chunksize = 5 : nat64; offset = 0 : nat64; })',
    )
    assert "Ok" in response, response
    response = call("uploaded_prompt_cache_details", '(record { promptcache = "repro.cache"; })')
    assert "filesize = 5" in response, response
    response = call("remove_prompt_cache", '(record { args = vec {"--prompt-cache"; "repro.cache"} })')
    assert "status_code = 200" in response, response
    response = call("uploaded_prompt_cache_details", '(record { promptcache = "repro.cache"; })')
    assert "Metadata for this file are not found" in response, response


# ------------------------------------------------------------------
# Restoring a prompt cache: download it with its stamp, remove it, upload it
# again (scripts.upload stamps it), and it continues like a copy that never
# left the canister. Needs the tiny model, uploaded as models/tiny.gguf.
ROOT_PATH = Path(__file__).parent.parent


def _call(network: str, method: str, arg: str) -> str:
    return call_canister_api(
        icp_yaml_path=ICP_YAML_PATH,
        canister_name=CANISTER_NAME,
        canister_method=method,
        canister_argument=arg,
        network=network,
        timeout_seconds=300,
    )


def _greedy_full(network: str, cache: str, prompt: str) -> str:
    arg = (
        f'(record {{ args = vec {{"--prompt-cache"; "{cache}"; "--prompt-cache-all"; '
        f'"--samplers"; "temperature"; "--temp"; "0.0"; "-p"; "{prompt}"; "-n"; "8"}} }})'
    )
    response = _call(network, "run_update_full", arg)
    assert "(variant { Ok" in response, response
    return response


def _output(response: str) -> str:
    match = re.search(r'output = "((?:[^"\\]|\\.)*)"', response)
    assert match, response
    return match.group(1)


def _n_cached(response: str) -> int:
    match = re.search(r"n_prompt_tokens_cached = opt \(([\d_]+) : nat64\)", response)
    assert match, response
    return int(match.group(1).replace("_", ""))


def _script(identity: str, *args: str) -> str:
    env = dict(os.environ, ICPP_PRO_TEST_IDENTITY=identity)
    completed = subprocess.run(
        [sys.executable, "-m", *args],
        cwd=ROOT_PATH,
        env=env,
        capture_output=True,
        text=True,
        check=False,
    )
    print(completed.stdout, completed.stderr)
    assert completed.returncode == 0, completed.stdout + completed.stderr
    return completed.stdout


def test__prompt_cache_round_trip(network: str, identity: str, tmp_path: Path) -> None:
    response = _call(
        network, "load_model", '(record { args = vec {"--model"; "models/tiny.gguf"} })'
    )
    assert "(variant { Ok" in response, response

    cache = "roundtrip.cache"
    for name in (cache, "roundtrip-ctrl.cache"):
        _call(network, "remove_prompt_cache", f'(record {{ args = vec {{"--prompt-cache"; "{name}"}} }})')
    _greedy_full(network, cache, "Joe loves writing stories")

    # The control continues a copy, which keeps its stamp and logits
    response = _call(
        network,
        "copy_prompt_cache",
        f'(record {{ from = "{cache}"; to = "roundtrip-ctrl.cache" }})',
    )
    assert "status_code = 200" in response, response
    control = _greedy_full(network, "roundtrip-ctrl.cache", "")

    local = tmp_path / "roundtrip.cache"
    stdout = _script(
        identity,
        "scripts.download",
        "--network", network,
        "--canister", CANISTER_NAME,
        "--filetype", "promptcache",
        "--local-filename", str(local),
        cache,
    )
    assert "SHA256 hash" in stdout
    stamp_path = Path(f"{local}.icppfmt")
    assert stamp_path.read_text(encoding="utf-8").count("\n") == 3

    response = _call(network, "remove_prompt_cache", f'(record {{ args = vec {{"--prompt-cache"; "{cache}"}} }})')
    assert "status_code = 200" in response, response

    stdout = _script(
        identity,
        "scripts.upload",
        "--network", network,
        "--canister", CANISTER_NAME,
        "--canister-filename", cache,
        "--filetype", "promptcache",
        str(local),
    )
    assert "OK! The prompt cache will be used on its first run." in stdout, stdout

    # Loaded, not discarded: all but the last token come from the cache, which
    # is decoded again for its logits
    restored = _greedy_full(network, cache, "")
    assert _n_cached(restored) == _n_cached(control) - 1, (restored, control)
    assert _output(control) and _output(restored) == _output(control), (restored, control)

    # Junk with a correct stamp is refused by the header check, and stays unstamped
    response = _call(
        network,
        "upload_prompt_cache_chunk",
        '(record { promptcache = "junk.cache"; chunk = blob "\\01\\02\\03\\04\\05\\06\\07\\08\\09\\0a"; chunksize = 10 : nat64; offset = 0 : nat64; })',
    )
    assert "(variant { Ok" in response, response
    sha = hashlib.sha256(bytes(range(1, 11))).hexdigest()
    stamp = stamp_path.read_text(encoding="utf-8")
    response = _call(
        network,
        "stamp_uploaded_prompt_cache",
        f"(record {{ promptcache = \"junk.cache\"; stamp = {json.dumps(stamp)}; filesha256 = \"{sha}\" }})",
    )
    assert "stamp_uploaded_prompt_cache: the file is shorter than a session file header" in response, response
    response = _call(
        network,
        "download_prompt_cache_chunk",
        '(record { promptcache = "junk.cache.icppfmt"; chunksize = 1000 : nat64; offset = 0 : nat64;})',
    )
    assert "Err" in response, response

    for name in (cache, "roundtrip-ctrl.cache", "junk.cache"):
        _call(network, "remove_prompt_cache", f'(record {{ args = vec {{"--prompt-cache"; "{name}"}} }})')


# ------------------------------------------------------------------
# The uploads above are raw blobs, never sessions: they cannot be stamped
def test__stamp_raw_blob_refused(network: str) -> None:
    response = _call(
        network,
        "stamp_uploaded_prompt_cache",
        '(record { promptcache = "another_prompt.cache"; stamp = "x"; filesha256 = "a76c5d12c465c064d093af7678023364d20622935fe951e29d5c28a6ef66572b" })',
    )
    assert "(variant { Err" in response, response


# A stamp is written by the canister only: it cannot be uploaded or copied
def test__sidecar_upload_refused(network: str) -> None:
    for name in ("prompt.cache.icppfmt", "prompt.cache.logits"):
        response = _call(
            network,
            "upload_prompt_cache_chunk",
            f'(record {{ promptcache = "{name}"; chunk = blob "\\01"; chunksize = 1 : nat64; offset = 0 : nat64; }})',
        )
        assert "names a prompt-cache sidecar" in response, response
    response = _call(
        network,
        "copy_prompt_cache",
        '(record { from = "prompt.cache"; to = "x.cache.icppfmt" })',
    )
    assert "names a prompt-cache sidecar" in response, response


def test__stamp_uploaded_prompt_cache_anonymous(identity_anonymous: Dict[str, str], network: str) -> None:
    assert identity_anonymous["identity"] == "anonymous"
    response = _call(
        network,
        "stamp_uploaded_prompt_cache",
        '(record { promptcache = "test.cache"; stamp = ""; filesha256 = "" })',
    )
    assert response == norm('(variant { Err = variant { Other = "Access Denied" } })')
