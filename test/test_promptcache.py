"""Test promptcache APIs

   First deploy the canister, then run:

   $ pytest -vv --network=[local/ic] test/test_promptcache.py

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


def test__upload_prompt_cache_chunk_0(network: str, principal: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="upload_prompt_cache_chunk",
        canister_argument='(record { promptcache = "prompt.cache"; chunk = blob "\47\47\55\46\03"; chunksize = 5 : nat64; offset = 0 : nat64; })',
        network=network,
    )
    expected_response = f'(variant {{ Ok = record {{ filename = ".canister_cache/{principal}/sessions/prompt.cache"; filesize = 3 : nat64; filesha256 = "7a58b749228e5843e28feee5d3e698c107ebb965783f4df5473079054a8a6999";}} }})'
    assert response == expected_response

def test__upload_prompt_cache_chunk_1(network: str, principal: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="upload_prompt_cache_chunk",
        canister_argument='(record { promptcache = "prompt.cache"; chunk = blob "\47\47\55\46\03"; chunksize = 5 : nat64; offset = 5 : nat64; })',
        network=network,
    )
    expected_response = f'(variant {{ Ok = record {{ filename = ".canister_cache/{principal}/sessions/prompt.cache"; filesize = 8 : nat64; filesha256 = "8879ea297cf1eb023538169232559f6d1e3d9642f4435d029df1e77608489a90";}} }})'
    assert response == expected_response

def test__uploaded_prompt_cache_details(network: str, principal: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="uploaded_prompt_cache_details",
        canister_argument='(record { promptcache = "prompt.cache"; })',
        network=network,
    )
    expected_response = f'(variant {{ Ok = record {{ filename = ".canister_cache/{principal}/sessions/prompt.cache"; filesize = 8 : nat64; filesha256 = "8879ea297cf1eb023538169232559f6d1e3d9642f4435d029df1e77608489a90";}} }})'
    assert response == expected_response

def test__download_prompt_cache_chunk_0(network: str, principal: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="download_prompt_cache_chunk",
        canister_argument='(record { promptcache = "prompt.cache"; chunksize = 5 : nat64; offset = 0 : nat64;})',
        network=network,
    )
    expected_response = '(variant { Ok = record { done = false; chunk = blob "\\2d\\26\\03\\46\\03"; offset = 0 : nat64; filesize = 8 : nat64; chunksize = 5 : nat64;} })'
    assert response == expected_response

def test__download_prompt_cache_chunk_1(network: str, principal: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="download_prompt_cache_chunk",
        canister_argument='(record { promptcache = "prompt.cache"; chunksize = 5 : nat64; offset = 5 : nat64;})',
        network=network,
    )
    expected_response = '(variant { Ok = record { done = true; chunk = blob "\\2d\\26\\03"; offset = 5 : nat64; filesize = 8 : nat64; chunksize = 3 : nat64;} })'
    assert response == expected_response