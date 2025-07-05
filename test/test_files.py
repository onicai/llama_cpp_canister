"""Test promptcache APIs

First deploy the canister:
$ icpp build-wasm
$ dfx deploy --network local

Then run the tests:
$ pytest -vv --network local test/test_promptcache.py

To run it against a deployment to the IC, just replace `local` with `ic` in the commands above.

"""
# pylint: disable=missing-function-docstring, unused-import, wildcard-import, unused-wildcard-import, line-too-long

from pathlib import Path
from typing import Dict
import pytest
from icpp.smoketest import call_canister_api, dict_to_candid_text

# Path to the dfx.json file
DFX_JSON_PATH = Path(__file__).parent / "../dfx.json"

# Canister in the dfx.json file we want to test
CANISTER_NAME = "llama_cpp"


def test__upload_prompt_cache_file(network: str, principal: str) -> None:
    # Upload two dummy prompt cache files to the canister - The other tests rely on these files being present
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="upload_prompt_cache_chunk",
        canister_argument='(record { promptcache = "prompt.cache"; chunk = blob "\\47\\47\\55\\46\\03"; chunksize = 5 : nat64; offset = 0 : nat64; })',
        network=network,
    )
    expected_response = f'(variant {{ Ok = record {{ filename = ".canister_cache/{principal}/sessions/prompt.cache"; filesize = 5 : nat64; filesha256 = "fe3b34fd092c3e2c6da3270eb91c4d3e9c2c6f891c21b6ed7358bf5ecca2d207";}} }})'
    assert response == expected_response

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="upload_prompt_cache_chunk",
        canister_argument='(record { promptcache = "another_prompt.cache"; chunk = blob "\\03\\46\\55\\47\\47"; chunksize = 5 : nat64; offset = 0 : nat64; })',
        network=network,
    )
    expected_response = f'(variant {{ Ok = record {{ filename = ".canister_cache/{principal}/sessions/another_prompt.cache"; filesize = 5 : nat64; filesha256 = "fd789322b6e4d1a517f1b75768f0f9ebc5747076811ee04e8a5f0731320f4884";}} }})'
    assert response == expected_response

# ------------------------------------------------------------------
def test__recursive_dir_content_non_existing(network: str, principal: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="recursive_dir_content",
        canister_argument='(record {dir = "does_not_exist"})',
        network=network,
    )
    expected_response = f'(variant {{ Err = variant {{ Other = "recursive_dir_content: Directory does not exist: does_not_exist\\n" }} }})'
    assert response == expected_response

def test__recursive_dir_content_anonymous(identity_anonymous: Dict[str, str], network: str) -> None:
    # double check the identity_anonymous fixture worked
    assert identity_anonymous["identity"] == "anonymous"
    assert identity_anonymous["principal"] == "2vxsx-fae"

    principal = identity_anonymous["principal"]

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="recursive_dir_content",
        canister_argument=f'(record {{dir = ".canister_cache"}})',
        network=network,
    )
    expected_response = f'(variant {{ Err = variant {{ Other = "Access Denied" }} }})'
    assert response == expected_response

# This test requires to run the test with non-default identity --> TODO: qa script must run with non-default identity
#
# def test__recursive_dir_content_non_controller(identity_default: Dict[str, str], network: str) -> None:
#     principal = identity_default["principal"]

#     response = call_canister_api(
#         dfx_json_path=DFX_JSON_PATH,
#         canister_name=CANISTER_NAME,
#         canister_method="recursive_dir_content",
#         canister_argument=f'(record {{dir = ".canister_cache"}})',
#         network=network,
#     )
#     expected_response = f'(variant {{ Err = variant {{ Other = "Access Denied" }} }})'
#     assert response == expected_response

def test__recursive_dir_content_controller(network: str, principal: str) -> None:

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="recursive_dir_content",
        canister_argument=f'(record {{dir = ".canister_cache"}})',
        network=network,
    )
    expected_response = f'(variant {{ Ok = vec {{ record {{ filename = ".canister_cache/{principal}"; filesize = 0 : nat64; filetype = "directory";}}; record {{ filename = ".canister_cache/{principal}/sessions"; filesize = 0 : nat64; filetype = "directory";}}; record {{ filename = ".canister_cache/{principal}/sessions/prompt.cache"; filesize = 5 : nat64; filetype = "file";}}; record {{ filename = ".canister_cache/{principal}/sessions/another_prompt.cache"; filesize = 5 : nat64; filetype = "file";}};}} }})'
    assert response == expected_response

# ------------------------------------------------------------------
def test__filesystem_file_size_non_existing(network: str, principal: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="filesystem_file_size",
        canister_argument='(record {filename = "does_not_exist.bin"})',
        network=network,
    )
    expected_response = f'(variant {{ Ok = record {{ msg = "File does not exist: does_not_exist.bin\\n"; filename = "does_not_exist.bin"; filesize = 0 : nat64; exists = false;}} }})'
    assert response == expected_response

def test__filesystem_file_size_anonymous(identity_anonymous: Dict[str, str], network: str) -> None:
    # double check the identity_anonymous fixture worked
    assert identity_anonymous["identity"] == "anonymous"
    assert identity_anonymous["principal"] == "2vxsx-fae"

    principal = identity_anonymous["principal"]
    filename = f".canister_cache/{principal}/sessions/prompt.cache"

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="filesystem_file_size",
        canister_argument=f'(record {{filename = "{filename}"}})',
        network=network,
    )
    expected_response = f'(variant {{ Err = variant {{ Other = "Access Denied" }} }})'
    assert response == expected_response

# This test requires to run the test with non-default identity --> TODO: qa script must run with non-default identity
#
# def test__filesystem_file_size_non_controller(identity_default: Dict[str, str], network: str) -> None:
#     principal = identity_default["principal"]
#     filename = f".canister_cache/{principal}/sessions/prompt.cache"

#     response = call_canister_api(
#         dfx_json_path=DFX_JSON_PATH,
#         canister_name=CANISTER_NAME,
#         canister_method="filesystem_file_size",
#         canister_argument=f'(record {{filename = "{filename}"}})',
#         network=network,
#     )
#     expected_response = f'(variant {{ Err = variant {{ Other = "Access Denied" }} }})'
#     assert response == expected_response

def test__filesystem_file_size_controller(network: str, principal: str) -> None:
    filename = f".canister_cache/{principal}/sessions/prompt.cache"

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="filesystem_file_size",
        canister_argument=f'(record {{filename = "{filename}"}})',
        network=network,
    )
    expected_response = f'(variant {{ Ok = record {{ msg = "File exists: .canister_cache/{principal}/sessions/prompt.cache\\nFile size: 5 bytes\\n"; filename = ".canister_cache/{principal}/sessions/prompt.cache"; filesize = 5 : nat64; exists = true;}} }})'
    assert response == expected_response

# ------------------------------------------------------------------
def test__filesystem_remove_non_existing(network: str, principal: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="filesystem_remove",
        canister_argument='(record {filename = "does_not_exist.bin"})',
        network=network,
    )
    expected_response = f'(variant {{ Ok = record {{ msg = "File does not exist: does_not_exist.bin\\n"; filename = "does_not_exist.bin"; filesize = 0 : nat64; exists = false; removed = false;}} }})'
    assert response == expected_response

def test__filesystem_remove_anonymous(identity_anonymous: Dict[str, str], network: str) -> None:
    # double check the identity_anonymous fixture worked
    assert identity_anonymous["identity"] == "anonymous"
    assert identity_anonymous["principal"] == "2vxsx-fae"

    principal = identity_anonymous["principal"]
    filename = f".canister_cache/{principal}/sessions/prompt.cache"

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="filesystem_remove",
        canister_argument=f'(record {{filename = "{filename}"}})',
        network=network,
    )
    expected_response = f'(variant {{ Err = variant {{ Other = "Access Denied" }} }})'
    assert response == expected_response

# This test requires to run the test with non-default identity --> TODO: qa script must run with non-default identity
# 
# def test__filesystem_remove_non_controller(identity_default: Dict[str, str], network: str) -> None:
#     principal = identity_default["principal"]
#     filename = f".canister_cache/{principal}/sessions/prompt.cache"

#     response = call_canister_api(
#         dfx_json_path=DFX_JSON_PATH,
#         canister_name=CANISTER_NAME,
#         canister_method="filesystem_remove",
#         canister_argument=f'(record {{filename = "{filename}"}})',
#         network=network,
#     )
#     expected_response = f'(variant {{ Err = variant {{ Other = "Access Denied" }} }})'
#     assert response == expected_response

def test__filesystem_remove_controller(network: str, principal: str) -> None:
    filename = f".canister_cache/{principal}/sessions/prompt.cache"

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="filesystem_remove",
        canister_argument=f'(record {{filename = "{filename}"}})',
        network=network,
    )
    expected_response = f'(variant {{ Ok = record {{ msg = "File removed successfully: .canister_cache/{principal}/sessions/prompt.cache"; filename = ".canister_cache/{principal}/sessions/prompt.cache"; filesize = 5 : nat64; exists = true; removed = true;}} }})'
    assert response == expected_response

# The other file should still be there after removing the first one
def test__filesystem_file_size_controller_another_prompt(network: str, principal: str) -> None:
    filename = f".canister_cache/{principal}/sessions/another_prompt.cache"

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="filesystem_file_size",
        canister_argument=f'(record {{filename = "{filename}"}})',
        network=network,
    )
    expected_response = f'(variant {{ Ok = record {{ msg = "File exists: .canister_cache/{principal}/sessions/another_prompt.cache\\nFile size: 5 bytes\\n"; filename = ".canister_cache/{principal}/sessions/another_prompt.cache"; filesize = 5 : nat64; exists = true;}} }})'
    assert response == expected_response

def test__filesystem_remove_controller_another_prompt(network: str, principal: str) -> None:
    filename = f".canister_cache/{principal}/sessions/another_prompt.cache"

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="filesystem_remove",
        canister_argument=f'(record {{filename = "{filename}"}})',
        network=network,
    )
    expected_response = f'(variant {{ Ok = record {{ msg = "File removed successfully: .canister_cache/{principal}/sessions/another_prompt.cache"; filename = ".canister_cache/{principal}/sessions/another_prompt.cache"; filesize = 5 : nat64; exists = true; removed = true;}} }})'
    assert response == expected_response