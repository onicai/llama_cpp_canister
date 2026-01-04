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


# ------------------------------------------------------------------
# check_access tests
def test__check_access_anonymous(identity_anonymous: Dict[str, str], network: str) -> None:
    """Test check_access rejects anonymous caller"""
    assert identity_anonymous["identity"] == "anonymous"
    assert identity_anonymous["principal"] == "2vxsx-fae"

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="check_access",
        canister_argument="()",
        network=network,
    )
    expected_response = '(variant { Err = variant { Other = "Access Denied" } })'
    assert response == expected_response


def test__check_access_controller(network: str) -> None:
    """Test check_access succeeds for controller"""
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="check_access",
        canister_argument="()",
        network=network,
    )
    expected_response = '(variant { Ok = record { status_code = 200 : nat16;} })'
    assert response == expected_response


# ------------------------------------------------------------------
# whoami tests
def test__whoami(network: str, principal: str) -> None:
    """Test whoami returns caller's principal"""
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="whoami",
        canister_argument="()",
        network=network,
    )
    expected_response = f'("{principal}")'
    assert response == expected_response


def test__whoami_anonymous(identity_anonymous: Dict[str, str], network: str) -> None:
    """Test whoami returns anonymous principal"""
    assert identity_anonymous["identity"] == "anonymous"
    assert identity_anonymous["principal"] == "2vxsx-fae"

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="whoami",
        canister_argument="()",
        network=network,
    )
    expected_response = '("2vxsx-fae")'
    assert response == expected_response


# =============================================================================
# Additional Anonymous Access Denial Tests
# =============================================================================

def test__set_max_tokens_anonymous(identity_anonymous: Dict[str, str], network: str) -> None:
    """Test set_max_tokens rejects anonymous caller"""
    assert identity_anonymous["identity"] == "anonymous"

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="set_max_tokens",
        canister_argument='(record { max_tokens_update = 100 : nat64; max_tokens_query = 100 : nat64 })',
        network=network,
    )
    expected_response = '(variant { Err = variant { Other = "Access Denied" } })'
    assert response == expected_response


def test__load_model_anonymous(identity_anonymous: Dict[str, str], network: str) -> None:
    """Test load_model rejects anonymous caller (OutputRecordResult format)"""
    assert identity_anonymous["identity"] == "anonymous"

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="load_model",
        canister_argument='(record { args = vec { "--help" } })',
        network=network,
    )
    # load_model uses OutputRecordResult format for access denied
    assert 'Err' in response
    assert 'status_code = 401' in response or 'Access Denied' in response


def test__log_pause_anonymous(identity_anonymous: Dict[str, str], network: str) -> None:
    """Test log_pause rejects anonymous caller"""
    assert identity_anonymous["identity"] == "anonymous"

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="log_pause",
        canister_argument="()",
        network=network,
    )
    expected_response = '(variant { Err = variant { Other = "Access Denied" } })'
    assert response == expected_response


def test__log_resume_anonymous(identity_anonymous: Dict[str, str], network: str) -> None:
    """Test log_resume rejects anonymous caller"""
    assert identity_anonymous["identity"] == "anonymous"

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="log_resume",
        canister_argument="()",
        network=network,
    )
    expected_response = '(variant { Err = variant { Other = "Access Denied" } })'
    assert response == expected_response


def test__remove_log_file_anonymous(identity_anonymous: Dict[str, str], network: str) -> None:
    """Test remove_log_file rejects anonymous caller (OutputRecordResult format)"""
    assert identity_anonymous["identity"] == "anonymous"

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="remove_log_file",
        canister_argument='(record { args = vec {} })',
        network=network,
    )
    # remove_log_file uses OutputRecordResult format for access denied
    assert 'Err' in response
    assert 'status_code = 401' in response or 'Access Denied' in response


def test__new_chat_anonymous(identity_anonymous: Dict[str, str], network: str) -> None:
    """Test new_chat rejects anonymous caller (OutputRecordResult format)"""
    assert identity_anonymous["identity"] == "anonymous"

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="new_chat",
        canister_argument='(record { args = vec { "--help" } })',
        network=network,
    )
    # new_chat uses OutputRecordResult format for access denied
    assert 'Err' in response
    assert 'status_code = 401' in response or 'Access Denied' in response


def test__run_query_anonymous(identity_anonymous: Dict[str, str], network: str) -> None:
    """Test run_query rejects anonymous caller (OutputRecordResult format)"""
    assert identity_anonymous["identity"] == "anonymous"

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="run_query",
        canister_argument='(record { args = vec { "--help" } })',
        network=network,
    )
    # run_query uses OutputRecordResult format for access denied
    assert 'Err' in response
    assert 'status_code = 401' in response or 'Access Denied' in response


def test__run_update_anonymous(identity_anonymous: Dict[str, str], network: str) -> None:
    """Test run_update rejects anonymous caller (OutputRecordResult format)"""
    assert identity_anonymous["identity"] == "anonymous"

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="run_update",
        canister_argument='(record { args = vec { "--help" } })',
        network=network,
    )
    # run_update uses OutputRecordResult format for access denied
    assert 'Err' in response
    assert 'status_code = 401' in response or 'Access Denied' in response


def test__remove_prompt_cache_anonymous(identity_anonymous: Dict[str, str], network: str) -> None:
    """Test remove_prompt_cache rejects anonymous caller (OutputRecordResult format)"""
    assert identity_anonymous["identity"] == "anonymous"

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="remove_prompt_cache",
        canister_argument='(record { args = vec { "test.cache" } })',
        network=network,
    )
    # remove_prompt_cache uses OutputRecordResult format for access denied
    assert 'Err' in response
    assert 'status_code = 401' in response or 'Access Denied' in response


def test__copy_prompt_cache_anonymous(identity_anonymous: Dict[str, str], network: str) -> None:
    """Test copy_prompt_cache rejects anonymous caller"""
    assert identity_anonymous["identity"] == "anonymous"

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="copy_prompt_cache",
        canister_argument='(record { from = "source.cache"; to = "dest.cache" })',
        network=network,
    )
    expected_response = '(variant { Err = variant { Other = "Access Denied" } })'
    assert response == expected_response


def test__get_chats_anonymous(identity_anonymous: Dict[str, str], network: str) -> None:
    """Test get_chats rejects anonymous caller"""
    assert identity_anonymous["identity"] == "anonymous"

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="get_chats",
        canister_argument="()",
        network=network,
    )
    expected_response = '(variant { Err = variant { Other = "Access Denied" } })'
    assert response == expected_response


def test__chats_resume_anonymous(identity_anonymous: Dict[str, str], network: str) -> None:
    """Test chats_resume rejects anonymous caller"""
    assert identity_anonymous["identity"] == "anonymous"

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="chats_resume",
        canister_argument="()",
        network=network,
    )
    expected_response = '(variant { Err = variant { Other = "Access Denied" } })'
    assert response == expected_response


def test__chats_pause_anonymous(identity_anonymous: Dict[str, str], network: str) -> None:
    """Test chats_pause rejects anonymous caller"""
    assert identity_anonymous["identity"] == "anonymous"

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="chats_pause",
        canister_argument="()",
        network=network,
    )
    expected_response = '(variant { Err = variant { Other = "Access Denied" } })'
    assert response == expected_response
