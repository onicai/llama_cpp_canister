"""Test canister

First deploy the canister:
$ icpp build-wasm
$ dfx deploy --network local

Then run the tests:
$ pytest -vv --network local test/test_canister_functions.py

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


def test__health(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="health",
        canister_argument="()",
        network=network,
    )
    expected_response = '(variant { Ok = record { status_code = 200 : nat16;} })'
    assert response == expected_response

def test__set_access_err(identity_anonymous: Dict[str, str], network: str) -> None:
    # double check the identity_anonymous fixture worked
    assert identity_anonymous["identity"] == "anonymous"
    assert identity_anonymous["principal"] == "2vxsx-fae"

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="set_access",
        canister_argument='(record { level = 0 : nat16 })',
        network=network,
    )
    expected_response = '(variant { Err = variant { Other = "Access Denied" } })'
    assert response == expected_response

def test__get_access_err(identity_anonymous: Dict[str, str], network: str) -> None:
    # double check the identity_anonymous fixture worked
    assert identity_anonymous["identity"] == "anonymous"
    assert identity_anonymous["principal"] == "2vxsx-fae"

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="get_access",
        canister_argument='(record { level = 0 : nat16 })',
        network=network,
    )
    expected_response = '(variant { Err = variant { Other = "Access Denied" } })'
    assert response == expected_response

def test__set_access_1(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="set_access",
        canister_argument='(record { level = 1 : nat16 })',
        network=network,
    )
    expected_response = '(variant { Ok = record { explanation = "All except anonymous"; level = 1 : nat16;} })'
    assert response == expected_response

def test__get_access_1(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="get_access",
        canister_argument='(record { level = 1 : nat16 })',
        network=network,
    )
    expected_response = '(variant { Ok = record { explanation = "All except anonymous"; level = 1 : nat16;} })'
    assert response == expected_response

def test__set_access_0(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="set_access",
        canister_argument='(record { level = 0 : nat16 })',
        network=network,
    )
    expected_response = '(variant { Ok = record { explanation = "Only controllers"; level = 0 : nat16;} })'
    assert response == expected_response

def test__get_access_0(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="get_access",
        canister_argument='(record { level = 0 : nat16 })',
        network=network,
    )
    expected_response = '(variant { Ok = record { explanation = "Only controllers"; level = 0 : nat16;} })'
    assert response == expected_response
