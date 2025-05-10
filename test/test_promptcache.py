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
        canister_argument='(record { promptcache = "prompt.cache"; chunk = blob "\\47\\47\\55\\46\\03"; chunksize = 5 : nat64; offset = 0 : nat64; })',
        network=network,
    )
    expected_response = f'(variant {{ Ok = record {{ filename = ".canister_cache/{principal}/sessions/prompt.cache"; filesize = 5 : nat64; filesha256 = "fe3b34fd092c3e2c6da3270eb91c4d3e9c2c6f891c21b6ed7358bf5ecca2d207";}} }})'
    assert response == expected_response

def test__upload_prompt_cache_chunk_1(network: str, principal: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="upload_prompt_cache_chunk",
        canister_argument='(record { promptcache = "prompt.cache"; chunk = blob "\\08\\33\\41\\43\\04"; chunksize = 5 : nat64; offset = 5 : nat64; })',
        network=network,
    )
    expected_response = f'(variant {{ Ok = record {{ filename = ".canister_cache/{principal}/sessions/prompt.cache"; filesize = 10 : nat64; filesha256 = "438bb530032946102742839ca22319211409cbd1c403f87a82e68e35e89e8c15";}} }})'
    assert response == expected_response

# Start another upload to test concurrent uploads
def test__upload_prompt_cache_chunk_another_0(network: str, principal: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="upload_prompt_cache_chunk",
        canister_argument='(record { promptcache = "another_prompt.cache"; chunk = blob "\\03\\46\\55\\47\\47"; chunksize = 5 : nat64; offset = 0 : nat64; })',
        network=network,
    )
    expected_response = f'(variant {{ Ok = record {{ filename = ".canister_cache/{principal}/sessions/another_prompt.cache"; filesize = 5 : nat64; filesha256 = "fd789322b6e4d1a517f1b75768f0f9ebc5747076811ee04e8a5f0731320f4884";}} }})'
    assert response == expected_response

def test__upload_prompt_cache_chunk_2(network: str, principal: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="upload_prompt_cache_chunk",
        canister_argument='(record { promptcache = "prompt.cache"; chunk = blob "\\03\\46\\55\\47\\47"; chunksize = 5 : nat64; offset = 10 : nat64; })',
        network=network,
    )
    expected_response = f'(variant {{ Ok = record {{ filename = ".canister_cache/{principal}/sessions/prompt.cache"; filesize = 15 : nat64; filesha256 = "f042bddd385f498e5888d99d83b4e86cde25589356b4a9c89e705413a7b2dc57";}} }})'
    assert response == expected_response

# Continue the other upload to test concurrent uploads
def test__upload_prompt_cache_chunk_another_1(network: str, principal: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="upload_prompt_cache_chunk",
        canister_argument='(record { promptcache = "another_prompt.cache"; chunk = blob "\\04\\33\\41\\43\\08"; chunksize = 5 : nat64; offset = 5 : nat64; })',
        network=network,
    )
    expected_response = f'(variant {{ Ok = record {{ filename = ".canister_cache/{principal}/sessions/another_prompt.cache"; filesize = 10 : nat64; filesha256 = "a76c5d12c465c064d093af7678023364d20622935fe951e29d5c28a6ef66572b";}} }})'
    assert response == expected_response

def test__uploaded_prompt_cache_details(network: str, principal: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="uploaded_prompt_cache_details",
        canister_argument='(record { promptcache = "prompt.cache"; })',
        network=network,
    )
    expected_response = f'(variant {{ Ok = record {{ filename = ".canister_cache/{principal}/sessions/prompt.cache"; filesize = 15 : nat64; filesha256 = "f042bddd385f498e5888d99d83b4e86cde25589356b4a9c89e705413a7b2dc57";}} }})'
    assert response == expected_response

def test__download_prompt_cache_chunk_0(network: str, principal: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="download_prompt_cache_chunk",
        canister_argument='(record { promptcache = "prompt.cache"; chunksize = 5 : nat64; offset = 0 : nat64;})',
        network=network,
    )
    expected_response = '(variant { Ok = record { done = false; chunk = blob "\\47\\47\\55\\46\\03"; offset = 0 : nat64; filesize = 15 : nat64; chunksize = 5 : nat64;} })'
    assert response == expected_response

def test__download_prompt_cache_chunk_1(network: str, principal: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="download_prompt_cache_chunk",
        canister_argument='(record { promptcache = "prompt.cache"; chunksize = 5 : nat64; offset = 5 : nat64;})',
        network=network,
    )
    expected_response = '(variant { Ok = record { done = false; chunk = blob "\\08\\33\\41\\43\\04"; offset = 5 : nat64; filesize = 15 : nat64; chunksize = 5 : nat64;} })'
    assert response == expected_response

def test__download_prompt_cache_chunk_2(network: str, principal: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="download_prompt_cache_chunk",
        canister_argument='(record { promptcache = "prompt.cache"; chunksize = 5 : nat64; offset = 10 : nat64;})',
        network=network,
    )
    expected_response = '(variant { Ok = record { done = true; chunk = blob "\\03\\46\\55\\47\\47"; offset = 10 : nat64; filesize = 15 : nat64; chunksize = 5 : nat64;} })'
    assert response == expected_response

def test__download_prompt_cache_chunk_another_full(network: str, principal: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="download_prompt_cache_chunk",
        canister_argument='(record { promptcache = "another_prompt.cache"; chunksize = 1000 : nat64; offset = 0 : nat64;})',
        network=network,
    )
    expected_response = '(variant { Ok = record { done = true; chunk = blob "\\03\\46\\55\\47\\47\\04\\33\\41\\43\\08"; offset = 0 : nat64; filesize = 10 : nat64; chunksize = 10 : nat64;} })'
    assert response == expected_response
