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
CANISTER_NAME = "greet"


def test__greet_0(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="greet_0",
        canister_argument="()",
        network=network,
    )
    expected_response = '("hello!")'
    assert response == expected_response


# Run this test with anonymous identity
def test__greet_0_auth_err(identity_anonymous: Dict[str, str], network: str) -> None:
    # double check the identity_anonymous fixture worked
    assert identity_anonymous["identity"] == "anonymous"
    assert identity_anonymous["principal"] == "2vxsx-fae"

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="greet_0_auth",
        canister_argument="()",
        network=network,
    )
    expected_response = "(variant { err = 401 : nat16 })"
    assert response == expected_response


# Run this test with a logged in default identity
def test__greet_0_auth_ok(identity_default: Dict[str, str], network: str) -> None:
    # double check the identity_anonymous fixture worked
    assert identity_default["identity"] == "default"

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="greet_0_auth",
        canister_argument="()",
        network=network,
    )
    principal = identity_default["principal"]
    expected_response = f'(variant {{ ok = "Hello {principal}" }})'
    assert response == expected_response


def test__greet_1(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="greet_1",
        canister_argument="()",
        network=network,
    )
    expected_response = "(2_023 : int)"
    assert response == expected_response


def test__greet_2(network: str, principal: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="greet_2",
        canister_argument='("C++ Developer")',
        network=network,
    )
    expected_response = f'("hello C++ Developer!\\nYour principal is: {principal}")'
    assert response == expected_response


def test__greet_3(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="greet_3",
        canister_argument='(record { "icpp version" = 1 : int; OS = "Linux" : text })',
        network=network,
    )
    expected_response = '(record { "icpp Release Details" = "Version = 1 & Operating System = Linux"; "release year" = 2_023 : int;})'
    assert response == expected_response


def test__greet_4(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="greet_4",
        canister_argument="(record { 6 = 42 : int; 9 = 43 : int }, record { 7 = 44 : int; 10 = 45 : int })",
        network=network,
    )
    expected_response = (
        '("Hello!", "Your secret numbers are:", 42 : int, 43 : int, 44 : int, 45 : int)'
    )
    assert response == expected_response


def test__greet_json(network: str, principal: str) -> None:
    d = {"name": "AJB"}
    text_in = dict_to_candid_text(d)
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="greet_json",
        canister_argument=text_in,
        network=network,
    )
    expected_response = '("{\\"greet\\":\\"Hello AJB!\\"}")'
    assert response == expected_response
