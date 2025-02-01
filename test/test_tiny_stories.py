"""Test canister APIs

   First deploy the canister, then run:

   $ pytest --network=[local/ic] test_apis.py

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

def test__load_model(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="load_model",
        canister_argument='(record { args = vec {"--model"; "models/model.gguf";} })',
        network=network,
    )
    assert "(variant { Ok" in response

def test__set_max_tokens(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="set_max_tokens",
        canister_argument='(record { max_tokens_query = 50 : nat64; max_tokens_update = 50 : nat64 })',
        network=network,
    )
    assert "(variant { Ok" in response

def test__get_max_tokens(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="get_max_tokens",
        canister_argument='()',
        network=network,
    )
    expected_response = '(record { max_tokens_query = 50 : nat64; max_tokens_update = 50 : nat64;})'
    assert expected_response == response

def test__ready(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="ready",
        canister_argument="()",
        network=network,
    )
    expected_response = '(variant { Ok = record { status_code = 200 : nat16;} })'
    assert response == expected_response

def test__new_chat_err(identity_anonymous: Dict[str, str], network: str) -> None:
    # double check the identity_anonymous fixture worked
    assert identity_anonymous["identity"] == "anonymous"
    assert identity_anonymous["principal"] == "2vxsx-fae"

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="new_chat",
        canister_argument='(record { args = vec {"--prompt-cache"; "prompt.cache"} })',
        network=network,
    )
    assert "(variant { Err" in response

def test__new_chat(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="new_chat",
        canister_argument='(record { args = vec {"--prompt-cache"; "prompt.cache"} })',
        network=network,
    )
    assert "(variant { Ok" in response

def test__run_update_err(identity_anonymous: Dict[str, str], network: str) -> None:
    # double check the identity_anonymous fixture worked
    assert identity_anonymous["identity"] == "anonymous"
    assert identity_anonymous["principal"] == "2vxsx-fae"

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="run_update",
        canister_argument='(record { args = vec {"--prompt"; "Patrick loves ice-cream. On a hot day "; "--n-predict"; "20"; "--ctx-size"; "128"} })',
        network=network,
    )
    assert "(variant { Err" in response

def test__run_update(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="run_update",
        canister_argument='(record { args = vec {"--prompt"; "Patrick loves ice-cream. On a hot day "; "--n-predict"; "20"; "--ctx-size"; "128"} })',
        network=network,
    )
    assert "(variant { Ok" in response

# -----------
def test__run_query_err(identity_anonymous: Dict[str, str], network: str) -> None:
    # double check the identity_anonymous fixture worked
    assert identity_anonymous["identity"] == "anonymous"
    assert identity_anonymous["principal"] == "2vxsx-fae"

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="run_query",
        canister_argument='(record { args = vec {"--prompt"; "Patrick loves ice-cream. On a hot day "; "--n-predict"; "20"; "--ctx-size"; "128"} })',
        network=network,
    )
    assert "(variant { Err" in response

def test__run_query(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="run_query",
        canister_argument='(record { args = vec {"--prompt"; "Patrick loves ice-cream. On a hot day "; "--n-predict"; "20"; "--ctx-size"; "128"} })',
        network=network,
    )
    assert "(variant { Ok" in response

def test__remove_prompt_cache(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="remove_prompt_cache",
        canister_argument='(record { args = vec {"--prompt-cache"; "prompt.cache"} })',
        network=network,
    )
    assert "(variant { Ok" in response

def test__remove_log_file(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="remove_log_file",
        canister_argument='(record { args = vec {"--log-file"; "main.log"} })',
        network=network,
    )
    assert "(variant { Ok" in response