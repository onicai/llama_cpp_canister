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
        canister_argument='(record { args = vec {"--model"; "models/qwen2.5-0.5b-instruct-q8_0.gguf";} })',
        network=network,
    )
    assert "(variant { Ok" in response

def test__set_max_tokens(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="set_max_tokens",
        canister_argument='(record { max_tokens_query = 12 : nat64; max_tokens_update = 12 : nat64 })',
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
    expected_response = '(record { max_tokens_query = 12 : nat64; max_tokens_update = 12 : nat64;})'
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

def test__new_chat(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="new_chat",
        canister_argument='(record { args = vec {"--prompt-cache"; "my_cache/prompt.cache"} })',
        network=network,
    )
    assert "(variant { Ok" in response

def test__run_update_1(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="run_update",
        canister_argument='(record { args = vec {"--prompt-cache"; "my_cache/prompt.cache"; "--prompt-cache-all"; "-sp"; "-n"; "512"; "-p"; "<|im_start|>system\nYou are a helpful assistant.<|im_end|>\n<|im_start|>user\nExplain Large Language Models.<|im_end|>\n<|im_start|>assistant\n"} })',
        network=network,
    )
    expected_response = '(variant { Ok = record { output = ""; conversation = "<|im_start|>system\nYou are a helpful assistant.<|im_end|>\n<|im_start|>"; error = ""; status_code = 200 : nat16; prompt_remaining = "user\nExplain Large Language Models.<|im_end|>\n<|im_start|>assistant\n"; generated_eog = false;} })'
    assert expected_response == response

def test__run_update_2(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="run_update",
        canister_argument='(record { args = vec {"--prompt-cache"; "my_cache/prompt.cache"; "--prompt-cache-all"; "-sp"; "-n"; "512"; "-p"; "<|im_start|>system\nYou are a helpful assistant.<|im_end|>\n<|im_start|>user\nExplain Large Language Models.<|im_end|>\n<|im_start|>assistant\n"} })',
        network=network,
    )
    expected_response = '(variant { Ok = record { status_code = 200 : nat16; error = ""; output = ""; input = "<|im_start|>system\nYou are a helpful assistant.<|im_end|>\n<|im_start|>user\nExplain Large Language Models.<|im_end|>\n<|im_start|>assistant"; prompt_remaining = "\n";} generated_eog=false : bool})'
    assert expected_response == response

def test__run_update_3(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="run_update",
        canister_argument='(record { args = vec {"--prompt-cache"; "my_cache/prompt.cache"; "--prompt-cache-all"; "-sp"; "-n"; "512"; "-p"; "<|im_start|>system\nYou are a helpful assistant.<|im_end|>\n<|im_start|>user\nExplain Large Language Models.<|im_end|>\n<|im_start|>assistant\n"} })',
        network=network,
    )
    assert "(variant { Ok" in response

def test__run_update_4(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="run_update",
        canister_argument='(record { args = vec {"--prompt-cache"; "my_cache/prompt.cache"; "--prompt-cache-all"; "-sp"; "-n"; "512"; "-p"; ""} })',
        network=network,
    )
    expected_response = '(variant { Ok = record { status_code = 200 : nat16; error = ""; output = ""; input = "<|im_start|>system\nYou are a helpful assistant.<|im_end|>\n<|im_start|>" ; prompt_remaining = "user\nExplain Large Language Models.<|im_end|>\n<|im_start|>assistant\n"; generated_eog=false : bool} })'
    assert "(variant { Ok" in response