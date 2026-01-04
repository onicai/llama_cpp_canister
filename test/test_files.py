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
        canister_method="recursive_dir_content_query",
        canister_argument='(record {dir = "does_not_exist"; max_entries = 0 : nat64})',
        network=network,
    )
    expected_response = '(variant { Err = variant { Other = "recursive_dir_content_: Directory does not exist: does_not_exist\\n" } })'
    assert response == expected_response

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="recursive_dir_content_update",
        canister_argument='(record {dir = "does_not_exist"; max_entries = 0 : nat64})',
        network=network,
    )
    expected_response = '(variant { Err = variant { Other = "recursive_dir_content_: Directory does not exist: does_not_exist\\n" } })'
    assert response == expected_response

def test__recursive_dir_content_anonymous(identity_anonymous: Dict[str, str], network: str) -> None:
    # double check the identity_anonymous fixture worked
    assert identity_anonymous["identity"] == "anonymous"
    assert identity_anonymous["principal"] == "2vxsx-fae"

    principal = identity_anonymous["principal"]

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="recursive_dir_content_query",
        canister_argument='(record {dir = ".canister_cache"; max_entries = 0 : nat64})',
        network=network,
    )
    expected_response = '(variant { Err = variant { Other = "Access Denied" } })'
    assert response == expected_response

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="recursive_dir_content_update",
        canister_argument='(record {dir = ".canister_cache"; max_entries = 0 : nat64})',
        network=network,
    )
    expected_response = '(variant { Err = variant { Other = "Access Denied" } })'
    assert response == expected_response

# This test requires to run the test with non-default identity --> TODO: qa script must run with non-default identity
#
# def test__recursive_dir_content_non_controller(identity_default: Dict[str, str], network: str) -> None:
#     principal = identity_default["principal"]

#     response = call_canister_api(
#         dfx_json_path=DFX_JSON_PATH,
#         canister_name=CANISTER_NAME,
#         canister_method="recursive_dir_content_query",
#         canister_argument='(record {dir = ".canister_cache"; max_entries = 0 : nat64})',
#         network=network,
#     )
#     expected_response = '(variant { Err = variant { Other = "Access Denied" } })'
#     assert response == expected_response

#     response = call_canister_api(
#         dfx_json_path=DFX_JSON_PATH,
#         canister_name=CANISTER_NAME,
#         canister_method="recursive_dir_content_update",
#         canister_argument='(record {dir = ".canister_cache"; max_entries = 0 : nat64})',
#         network=network,
#     )
#     expected_response = '(variant { Err = variant { Other = "Access Denied" } })'
#     assert response == expected_response

def test__recursive_dir_content_controller(network: str, principal: str) -> None:

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="recursive_dir_content_query",
        canister_argument='(record {dir = ".canister_cache"; max_entries = 0 : nat64})',
        network=network,
    )
    expected_response = f'(variant {{ Ok = vec {{ record {{ filename = ".canister_cache/{principal}"; filetype = "directory";}}; record {{ filename = ".canister_cache/{principal}/sessions"; filetype = "directory";}}; record {{ filename = ".canister_cache/{principal}/sessions/prompt.cache"; filetype = "file";}}; record {{ filename = ".canister_cache/{principal}/sessions/another_prompt.cache"; filetype = "file";}};}} }})'
    assert response == expected_response

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="recursive_dir_content_update",
        canister_argument='(record {dir = ".canister_cache"; max_entries = 0 : nat64})',
        network=network,
    )
    expected_response = f'(variant {{ Ok = vec {{ record {{ filename = ".canister_cache/{principal}"; filetype = "directory";}}; record {{ filename = ".canister_cache/{principal}/sessions"; filetype = "directory";}}; record {{ filename = ".canister_cache/{principal}/sessions/prompt.cache"; filetype = "file";}}; record {{ filename = ".canister_cache/{principal}/sessions/another_prompt.cache"; filetype = "file";}};}} }})'
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
    expected_response = '(variant { Err = variant { Other = "File does not exist: does_not_exist.bin" } })'
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
def test__get_creation_timestamp_ns_non_existing(network: str, principal: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="get_creation_timestamp_ns",
        canister_argument='(record {filename = "does_not_exist.bin"})',
        network=network,
    )
    expected_response = '(variant { Err = variant { Other = "File does not exist: does_not_exist.bin" } })'
    assert response == expected_response

def test__get_creation_timestamp_ns_anonymous(identity_anonymous: Dict[str, str], network: str) -> None:
    # double check the identity_anonymous fixture worked
    assert identity_anonymous["identity"] == "anonymous"
    assert identity_anonymous["principal"] == "2vxsx-fae"

    principal = identity_anonymous["principal"]
    filename = f".canister_cache/{principal}/sessions/prompt.cache"

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="get_creation_timestamp_ns",
        canister_argument=f'(record {{filename = "{filename}"}})',
        network=network,
    )
    expected_response = f'(variant {{ Err = variant {{ Other = "Access Denied" }} }})'
    assert response == expected_response

# This test requires to run the test with non-default identity --> TODO: qa script must run with non-default identity
#
# def test__get_creation_timestamp_ns_non_controller(identity_default: Dict[str, str], network: str) -> None:
#     principal = identity_default["principal"]
#     filename = f".canister_cache/{principal}/sessions/prompt.cache"

#     response = call_canister_api(
#         dfx_json_path=DFX_JSON_PATH,
#         canister_name=CANISTER_NAME,
#         canister_method="get_creation_timestamp_ns",
#         canister_argument=f'(record {{filename = "{filename}"}})',
#         network=network,
#     )
#     expected_response = f'(variant {{ Err = variant {{ Other = "Access Denied" }} }})'
#     assert response == expected_response

def test__get_creation_timestamp_ns_controller(network: str, principal: str) -> None:
    filename = f".canister_cache/{principal}/sessions/prompt.cache"

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="get_creation_timestamp_ns",
        canister_argument=f'(record {{filename = "{filename}"}})',
        network=network,
    )
    expected_response_startswith = f'(variant {{ Ok = record {{ msg = "File exists: .canister_cache/{principal}/sessions/prompt.cache\\nFile creation timestamp_ns: '
    assert response.startswith(expected_response_startswith)

# ------------------------------------------------------------------
def test__filesystem_remove_non_existing(network: str, principal: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="filesystem_remove",
        canister_argument='(record {filename = "does_not_exist.bin"})',
        network=network,
    )
    expected_response = f'(variant {{ Ok = record {{ msg = "Path does not exist: does_not_exist.bin\\n"; filename = "does_not_exist.bin"; exists = false; removed = false;}} }})'
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
    expected_response = f'(variant {{ Ok = record {{ msg = "Path removed successfully: .canister_cache/{principal}/sessions/prompt.cache"; filename = ".canister_cache/{principal}/sessions/prompt.cache"; exists = true; removed = true;}} }})'
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

# ------------------------------------------------------------------
# file_download_chunk tests
def test__file_download_chunk_anonymous(identity_anonymous: Dict[str, str], network: str) -> None:
    """Test that file_download_chunk rejects anonymous caller"""
    assert identity_anonymous["identity"] == "anonymous"
    assert identity_anonymous["principal"] == "2vxsx-fae"

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="file_download_chunk",
        canister_argument='(record { filename = "test.bin"; chunksize = 1024 : nat64; offset = 0 : nat64 })',
        network=network,
    )
    expected_response = '(variant { Err = variant { Other = "Access Denied" } })'
    assert response == expected_response


# ------------------------------------------------------------------
# uploaded_file_details tests
def test__uploaded_file_details_anonymous(identity_anonymous: Dict[str, str], network: str) -> None:
    """Test that uploaded_file_details rejects anonymous caller"""
    assert identity_anonymous["identity"] == "anonymous"
    assert identity_anonymous["principal"] == "2vxsx-fae"

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="uploaded_file_details",
        canister_argument='(record { filename = "test.bin" })',
        network=network,
    )
    expected_response = '(variant { Err = variant { Other = "Access Denied" } })'
    assert response == expected_response


# ------------------------------------------------------------------
# Security test: MAX_CHUNK_SIZE validation
def test__file_download_chunk_exceeds_max_chunk_size(network: str, principal: str) -> None:
    """Test that file_download_chunk rejects chunksize exceeding MAX_CHUNK_SIZE (2MB)"""
    filename = f".canister_cache/{principal}/sessions/another_prompt.cache"
    # 3MB chunksize exceeds the 2MB MAX_CHUNK_SIZE limit
    chunksize_3mb = 3 * 1024 * 1024

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="file_download_chunk",
        canister_argument=f'(record {{filename = "{filename}"; chunksize = {chunksize_3mb} : nat64; offset = 0 : nat64}})',
        network=network,
    )
    expected_response = f'(variant {{ Err = variant {{ Other = "file_download_chunk_: chunksize {chunksize_3mb} exceeds limit 2097152" }} }})'
    assert response == expected_response


# ------------------------------------------------------------------
# file_upload_chunk tests
def test__file_upload_chunk_anonymous(identity_anonymous: Dict[str, str], network: str) -> None:
    """Test that file_upload_chunk rejects anonymous caller"""
    assert identity_anonymous["identity"] == "anonymous"
    assert identity_anonymous["principal"] == "2vxsx-fae"

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="file_upload_chunk",
        canister_argument='(record { filename = "test.bin"; chunk = blob "\\01\\02\\03"; chunksize = 3 : nat64; offset = 0 : nat64 })',
        network=network,
    )
    expected_response = '(variant { Err = variant { Other = "Access Denied" } })'
    assert response == expected_response


def test__file_upload_chunk_controller(network: str, principal: str) -> None:
    """Test that file_upload_chunk succeeds for controller"""
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="file_upload_chunk",
        canister_argument='(record { filename = "models/test_upload.bin"; chunk = blob "\\01\\02\\03\\04\\05"; chunksize = 5 : nat64; offset = 0 : nat64 })',
        network=network,
    )
    assert response.startswith('(variant { Ok = record {')
    assert 'filename = "models/test_upload.bin"' in response
    assert 'filesize = 5' in response


def test__file_upload_chunk_cleanup(network: str, principal: str) -> None:
    """Cleanup: remove the test file created by file_upload_chunk test"""
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="filesystem_remove",
        canister_argument='(record { filename = "models/test_upload.bin" })',
        network=network,
    )
    # Accept either removed or not found
    assert 'Ok' in response or 'does not exist' in response
