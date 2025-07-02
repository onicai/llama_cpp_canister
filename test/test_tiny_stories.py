"""Test canister

First deploy the canister:
$ icpp build-wasm
$ dfx deploy --network local

Then upload the model:
$ python -m scripts.upload --network local --canister llama_cpp --canister-filename models/tiny.gguf --filetype gguf models/stories260Ktok512.gguf

Then run the tests for this model::
$ pytest -vv --network local test/test_tiny_stories.py

To run it against a deployment to the IC, just replace `local` with `ic` in the commands above.

"""
# pylint: disable=missing-function-docstring, unused-import, wildcard-import, unused-wildcard-import, line-too-long

from pathlib import Path
from typing import Dict
import pytest
from icpp.smoketest import call_canister_api, dict_to_candid_text
import inspect

# Path to the dfx.json file
DFX_JSON_PATH = Path(__file__).parent / "../dfx.json"

# Canister in the dfx.json file we want to test
CANISTER_NAME = "llama_cpp"

# Helper function to get the current function name
def current_func_name():
    frame_info = inspect.stack()[1]
    # In Python 3.11+, frame_info has a 'function' attribute
    if hasattr(frame_info, "function"):
        return frame_info.function
    # In older versions (3.8–3.10), use co_name
    return frame_info.frame.f_code.co_name

PRINT_RESPONSE = True

def test__chats_resume(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="chats_resume",
        canister_argument='()',
        network=network,
    )
    if PRINT_RESPONSE:
        print(f"{current_func_name()}: response: {response}")
    expected_response = '(variant { Ok = record { status_code = 200 : nat16;} })'
    assert response == expected_response

def test__log_pause(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="log_pause",
        canister_argument='()',
        network=network,
    )
    if PRINT_RESPONSE:
        print(f"{current_func_name()}: response: {response}")
    expected_response = '(variant { Ok = record { status_code = 200 : nat16;} })'
    assert response == expected_response

def test__load_model(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="load_model",
        canister_argument='(record { args = vec {"--model"; "models/tiny.gguf";} })',
        network=network,
    )
    if PRINT_RESPONSE:
        print(f"{current_func_name()}: response: {response}")
    assert "(variant { Ok" in response

def test__uploaded_file_details(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="uploaded_file_details",
        canister_argument='(record { filename = "models/tiny.gguf" })',
        network=network,
    )
    if PRINT_RESPONSE:
        print(f"{current_func_name()}: response: {response}")
    expected_response = '(variant { Ok = record { filename = "models/tiny.gguf"; filesize = 1_185_376 : nat64; filesha256 = "047bf46455a544931cff6fef14d7910154c56afbc23ab1c5e56a72e69912c04b";} })'
    assert response == expected_response

def test__set_max_tokens(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="set_max_tokens",
        canister_argument='(record { max_tokens_query = 5 : nat64; max_tokens_update = 5 : nat64 })',
        network=network,
    )
    if PRINT_RESPONSE:
        print(f"{current_func_name()}: response: {response}")
    assert "(variant { Ok" in response

def test__get_max_tokens(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="get_max_tokens",
        canister_argument='()',
        network=network,
    )
    if PRINT_RESPONSE:
        print(f"{current_func_name()}: response: {response}")
    expected_response = '(record { max_tokens_query = 5 : nat64; max_tokens_update = 5 : nat64;})'
    assert response == expected_response

def test__ready(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="ready",
        canister_argument="()",
        network=network,
    )
    if PRINT_RESPONSE:
        print(f"{current_func_name()}: response: {response}")
    expected_response = '(variant { Ok = record { status_code = 200 : nat16;} })'
    assert response == expected_response

def test__remove_prompt_cache_1(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="remove_prompt_cache",
        canister_argument='(record { args = vec {"--prompt-cache"; "prompt.cache"} })',
        network=network,
    )
    if PRINT_RESPONSE:
        print(f"{current_func_name()}: response: {response}")
    assert "(variant { Ok" in response

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
    if PRINT_RESPONSE:
        print(f"{current_func_name()}: response: {response}")
    assert "(variant { Err" in response

def test__new_chat_1(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="new_chat",
        canister_argument='(record { args = vec {"--prompt-cache"; "prompt.cache"} })',
        network=network,
    )
    if PRINT_RESPONSE:
        print(f"{current_func_name()}: response: {response}")
    assert "(variant { Ok" in response

def test__run_update_err(identity_anonymous: Dict[str, str], network: str) -> None:
    # double check the identity_anonymous fixture worked
    assert identity_anonymous["identity"] == "anonymous"
    assert identity_anonymous["principal"] == "2vxsx-fae"

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="run_update",
        canister_argument='(record { args = vec {"--prompt-cache"; "prompt.cache"; "--prompt-cache-all"; "--samplers"; "temperature"; "--temp"; "0.0"; "-n"; "3"; "-p"; "Joe loves writing stories"} })',
        network=network,
    )
    if PRINT_RESPONSE:
        print(f"{current_func_name()}: response: {response}")
    assert "(variant { Err" in response

def test__run_update_1(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="run_update",
        canister_argument='(record { args = vec {"--prompt-cache"; "prompt.cache"; "--prompt-cache-all"; "--samplers"; "temperature"; "--temp"; "0.0"; "-n"; "3"; "-p"; "Joe loves writing stories"} })',
        network=network,
    )
    if PRINT_RESPONSE:
        print(f"{current_func_name()}: response: {response}")
    expected_response = '(variant { Ok = record { output = ""; conversation = " Joe"; error = ""; status_code = 200 : nat16; prompt_remaining = " loves writing stories"; generated_eog = false;} })'
    assert response == expected_response

def test__copy_prompt_cache_save(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="copy_prompt_cache",
        canister_argument='(record { from = "prompt.cache"; to = "prompt-save.cache"} )',
        network=network,
    )
    if PRINT_RESPONSE:
        print(f"{current_func_name()}: response: {response}")
    assert "(variant { Ok" in response

def test__run_update_2(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="run_update",
        canister_argument='(record { args = vec {"--prompt-cache"; "prompt.cache"; "--prompt-cache-all"; "--samplers"; "temperature"; "--temp"; "0.0"; "-n"; "3"; "-p"; "Joe loves writing stories"} })',
        network=network,
    )
    if PRINT_RESPONSE:
        print(f"{current_func_name()}: response: {response}")
    expected_response = '(variant { Ok = record { output = ""; conversation = " Joe loves wr"; error = ""; status_code = 200 : nat16; prompt_remaining = "iting stories"; generated_eog = false;} })'
    assert response == expected_response

def test__run_update_3(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="run_update",
        canister_argument='(record { args = vec {"--prompt-cache"; "prompt.cache"; "--prompt-cache-all"; "--samplers"; "temperature"; "--temp"; "0.0"; "-n"; "3"; "-p"; "Joe loves writing stories"} })',
        network=network,
    )
    if PRINT_RESPONSE:
        print(f"{current_func_name()}: response: {response}")
    expected_response = '(variant { Ok = record { output = ""; conversation = " Joe loves writing stori"; error = ""; status_code = 200 : nat16; prompt_remaining = "es"; generated_eog = false;} })'
    assert response == expected_response

def test__run_update_4(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="run_update",
        canister_argument='(record { args = vec {"--prompt-cache"; "prompt.cache"; "--prompt-cache-all"; "--samplers"; "temperature"; "--temp"; "0.0"; "-n"; "3"; "-p"; "Joe loves writing stories"} })',
        network=network,
    )
    if PRINT_RESPONSE:
        print(f"{current_func_name()}: response: {response}")
    expected_response = '(variant { Ok = record { output = ". He li"; conversation = " Joe loves writing stories. He"; error = ""; status_code = 200 : nat16; prompt_remaining = ""; generated_eog = false;} })'
    assert response == expected_response

def test__run_update_5(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="run_update",
        canister_argument='(record { args = vec {"--prompt-cache"; "prompt.cache"; "--prompt-cache-all"; "--samplers"; "temperature"; "--temp"; "0.0"; "-n"; "3"; "-p"; ""} })',
        network=network,
    )
    if PRINT_RESPONSE:
        print(f"{current_func_name()}: response: {response}")
    expected_response = '(variant { Ok = record { output = " liked to"; conversation = " Joe loves writing stories. He liked"; error = ""; status_code = 200 : nat16; prompt_remaining = ""; generated_eog = false;} })'
    assert response == expected_response

def test__get_chats_ok(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="get_chats",
        canister_argument='()',
        network=network,
    )
    if PRINT_RESPONSE:
        print(f"{current_func_name()}: response: {response}")
    assert 'Ok' in response

def test__remove_prompt_cache_1(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="remove_prompt_cache",
        canister_argument='(record { args = vec {"--prompt-cache"; "prompt.cache"} })',
        network=network,
    )
    if PRINT_RESPONSE:
        print(f"{current_func_name()}: response: {response}")
    assert "(variant { Ok" in response

def test__copy_prompt_cache_restore(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="copy_prompt_cache",
        canister_argument='(record { from = "prompt-save.cache"; to = "prompt.cache"} )',
        network=network,
    )
    if PRINT_RESPONSE:
        print(f"{current_func_name()}: response: {response}")
    assert "(variant { Ok" in response

def test__chats_pause(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="chats_pause",
        canister_argument='()',
        network=network,
    )
    if PRINT_RESPONSE:
        print(f"{current_func_name()}: response: {response}")
    expected_response = '(variant { Ok = record { status_code = 200 : nat16;} })'
    assert response == expected_response

def test__get_chats_err(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="get_chats",
        canister_argument='()',
        network=network,
    )
    if PRINT_RESPONSE:
        print(f"{current_func_name()}: response: {response}")
    assert 'Err' in response

def test__new_chat_2(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="new_chat",
        canister_argument='(record { args = vec {"--prompt-cache"; "prompt.cache"} })',
        network=network,
    )
    if PRINT_RESPONSE:
        print(f"{current_func_name()}: response: {response}")
    assert "(variant { Ok" in response

def test__run_update_2_2(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="run_update",
        canister_argument='(record { args = vec {"--prompt-cache"; "prompt.cache"; "--prompt-cache-all"; "--samplers"; "temperature"; "--temp"; "0.0"; "-n"; "3"; "-p"; "Joe loves writing stories"} })',
        network=network,
    )
    if PRINT_RESPONSE:
        print(f"{current_func_name()}: response: {response}")
    expected_response = '(variant { Ok = record { output = ""; conversation = " Joe loves wr"; error = ""; status_code = 200 : nat16; prompt_remaining = "iting stories"; generated_eog = false;} })'
    assert response == expected_response

def test__run_update_2_3(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="run_update",
        canister_argument='(record { args = vec {"--prompt-cache"; "prompt.cache"; "--prompt-cache-all"; "--samplers"; "temperature"; "--temp"; "0.0"; "-n"; "3"; "-p"; "Joe loves writing stories"} })',
        network=network,
    )
    if PRINT_RESPONSE:
        print(f"{current_func_name()}: response: {response}")
    expected_response = '(variant { Ok = record { output = ""; conversation = " Joe loves writing stori"; error = ""; status_code = 200 : nat16; prompt_remaining = "es"; generated_eog = false;} })'
    assert response == expected_response

def test__run_update_2_4(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="run_update",
        canister_argument='(record { args = vec {"--prompt-cache"; "prompt.cache"; "--prompt-cache-all"; "--samplers"; "temperature"; "--temp"; "0.0"; "-n"; "3"; "-p"; "Joe loves writing stories"} })',
        network=network,
    )
    if PRINT_RESPONSE:
        print(f"{current_func_name()}: response: {response}")
    expected_response = '(variant { Ok = record { output = ". He li"; conversation = " Joe loves writing stories. He"; error = ""; status_code = 200 : nat16; prompt_remaining = ""; generated_eog = false;} })'
    assert response == expected_response

def test__run_update_2_5(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="run_update",
        canister_argument='(record { args = vec {"--prompt-cache"; "prompt.cache"; "--prompt-cache-all"; "--samplers"; "temperature"; "--temp"; "0.0"; "-n"; "3"; "-p"; ""} })',
        network=network,
    )
    if PRINT_RESPONSE:
        print(f"{current_func_name()}: response: {response}")
    expected_response = '(variant { Ok = record { output = " liked to"; conversation = " Joe loves writing stories. He liked"; error = ""; status_code = 200 : nat16; prompt_remaining = ""; generated_eog = false;} })'
    assert response == expected_response

def test__remove_prompt_cache_cleanup(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="remove_prompt_cache",
        canister_argument='(record { args = vec {"--prompt-cache"; "prompt.cache"} })',
        network=network,
    )
    assert "(variant { Ok" in response

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="remove_prompt_cache",
        canister_argument='(record { args = vec {"--prompt-cache"; "prompt-save.cache"} })',
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

def test__log_resume(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="log_resume",
        canister_argument='()',
        network=network,
    )
    expected_response = '(variant { Ok = record { status_code = 200 : nat16;} })'
    assert response == expected_response