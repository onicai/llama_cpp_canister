"""Test canister

First deploy the canister:
$ icpp build-wasm
$ dfx deploy --network local

Then get the model from Hugging Face:
$ mkdir -p models/Qwen/Qwen2.5-0.5B-Instruct-GGUF
$ wget -c -O models/Qwen/Qwen2.5-0.5B-Instruct-GGUF/qwen2.5-0.5b-instruct-q8_0.gguf https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/resolve/main/qwen2.5-0.5b-instruct-q8_0.gguf

Then upload the model:
$ python -m scripts.upload --network local --canister llama_cpp --canister-filename models/model.gguf --filetype gguf models/Qwen/Qwen2.5-0.5B-Instruct-GGUF/qwen2.5-0.5b-instruct-q8_0.gguf

Then run the tests for this model::
$ pytest -vv --network local test/test_qwen2.py

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
        canister_argument='(record { max_tokens_query = 1 : nat64; max_tokens_update = 13 : nat64 })',
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
    expected_response = '(record { max_tokens_query = 1 : nat64; max_tokens_update = 13 : nat64;})'
    assert response == expected_response

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

def test__remove_prompt_cache_1(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="remove_prompt_cache",
        canister_argument='(record { args = vec {"--prompt-cache"; "prompt.cache"} })',
        network=network,
    )
    assert "(variant { Ok" in response

def test__new_chat_1(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="new_chat",
        canister_argument='(record { args = vec {"--prompt-cache"; "prompt.cache"} })',
        network=network,
    )
    assert "(variant { Ok" in response

def test__run_update_1(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="run_update",
        canister_argument='(record { args = vec {"--prompt-cache"; "prompt.cache"; "--prompt-cache-all"; "-sp"; "-n"; "512"; "-p"; "<|im_start|>system\nYou are a helpful assistant.<|im_end|>\n<|im_start|>user\nExplain Large Language Models.<|im_end|>\n<|im_start|>assistant\n"} })',
        network=network,
    )
    expected_response = '(variant { Ok = record { output = ""; conversation = "<|im_start|>system\\nYou are a helpful assistant.<|im_end|>\\n<|im_start|>user"; error = ""; status_code = 200 : nat16; prompt_remaining = "\\nExplain Large Language Models.<|im_end|>\\n<|im_start|>assistant\\n"; generated_eog = false;} })'
    assert response == expected_response

def test__copy_prompt_cache_save(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="copy_prompt_cache",
        canister_argument='(record { from = "prompt.cache"; to = "prompt-save.cache"} )',
        network=network,
    )
    assert "(variant { Ok" in response

def test__run_update_2(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="run_update",
        canister_argument='(record { args = vec {"--prompt-cache"; "prompt.cache"; "--prompt-cache-all"; "-sp"; "-n"; "512"; "-p"; "<|im_start|>system\nYou are a helpful assistant.<|im_end|>\n<|im_start|>user\nExplain Large Language Models.<|im_end|>\n<|im_start|>assistant\n"} })',
        network=network,
    )
    # Tokens are being generated. Can no longer assert the response.
    assert "(variant { Ok" in response

def test__run_update_3(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="run_update",
        canister_argument='(record { args = vec {"--prompt-cache"; "prompt.cache"; "--prompt-cache-all"; "-sp"; "-n"; "512"; "-p"; "<|im_start|>system\nYou are a helpful assistant.<|im_end|>\n<|im_start|>user\nExplain Large Language Models.<|im_end|>\n<|im_start|>assistant\n"} })',
        network=network,
    )
    # Tokens are being generated. Can no longer assert the response.
    assert "(variant { Ok" in response

def test__run_update_4(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="run_update",
        canister_argument='(record { args = vec {"--prompt-cache"; "prompt.cache"; "--prompt-cache-all"; "-sp"; "-n"; "512"; "-p"; ""} })',
        network=network,
    )
    expected_response = '(variant { Ok = record { status_code = 200 : nat16; error = ""; output = ""; input = "<|im_start|>system\nYou are a helpful assistant.<|im_end|>\n<|im_start|>" ; prompt_remaining = "user\nExplain Large Language Models.<|im_end|>\n<|im_start|>assistant\n"; generated_eog=false : bool} })'
    assert "(variant { Ok" in response

def test__remove_prompt_cache_2(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="remove_prompt_cache",
        canister_argument='(record { args = vec {"--prompt-cache"; "prompt.cache"} })',
        network=network,
    )
    assert "(variant { Ok" in response

def test__copy_prompt_cache_restore(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="copy_prompt_cache",
        canister_argument='(record { from = "prompt-save.cache"; to = "prompt.cache"} )',
        network=network,
    )
    assert "(variant { Ok" in response

def test__new_chat_2(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="new_chat",
        canister_argument='(record { args = vec {"--prompt-cache"; "prompt.cache"} })',
        network=network,
    )
    assert "(variant { Ok" in response

def test__run_update_2_2(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="run_update",
        canister_argument='(record { args = vec {"--prompt-cache"; "prompt.cache"; "--prompt-cache-all"; "-sp"; "-n"; "512"; "-p"; "<|im_start|>system\nYou are a helpful assistant.<|im_end|>\n<|im_start|>user\nExplain Large Language Models.<|im_end|>\n<|im_start|>assistant\n"} })',
        network=network,
    )
    expected_response = '(variant { Ok = record { output = "Large"; conversation = "<|im_start|>system\\nYou are a helpful assistant.<|im_end|>\\n<|im_start|>user\\nExplain Large Language Models.<|im_end|>\\n<|im_start|>assistant\\nLarge"; error = ""; status_code = 200 : nat16; prompt_remaining = ""; generated_eog = false;} })'
    assert response == expected_response

def test__chats_resume(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="chats_resume",
        canister_argument='()',
        network=network,
    )
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
    expected_response = '(variant { Ok = record { status_code = 200 : nat16;} })'
    assert response == expected_response

def test__new_chat_3(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="new_chat",
        canister_argument='(record { args = vec {"--prompt-cache"; "prompt.cache"} })',
        network=network,
    )
    assert "(variant { Ok" in response

def test__get_chats_1(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="get_chats",
        canister_argument='()',
        network=network,
    )
    assert 'Ok' in response

def test__chats_pause(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="chats_pause",
        canister_argument='()',
        network=network,
    )
    expected_response = '(variant { Ok = record { status_code = 200 : nat16;} })'
    assert response == expected_response

def test__get_chats_2(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="get_chats",
        canister_argument='()',
        network=network,
    )
    assert 'Err' in response

def test__new_chat_4(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="new_chat",
        canister_argument='(record { args = vec {"--prompt-cache"; "prompt.cache"} })',
        network=network,
    )
    assert "(variant { Ok" in response

def test__get_chats_3(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="get_chats",
        canister_argument='()',
        network=network,
    )
    assert 'Err' in response

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

def test__remove_log_file(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="remove_log_file",
        canister_argument='(record { args = vec {"--log-file"; "main.log"} })',
        network=network,
    )
    assert "(variant { Ok" in response

def test__new_chat_for_query_test(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="new_chat",
        canister_argument='(record { args = vec {"--prompt-cache"; "prompt.cache"} })',
        network=network,
    )
    assert "(variant { Ok" in response

def test__run_query_1(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="run_query",
        canister_argument='(record { args = vec {"--prompt-cache"; "prompt.cache"; "--prompt-cache-all"; "-sp"; "-n"; "512"; "-p"; "<|im_start|>system\nYou are a helpful assistant.<|im_end|>\n<|im_start|>user\nExplain Large Language Models.<|im_end|>\n<|im_start|>assistant\n"} })',
        network=network,
    )
    expected_response = '(variant { Ok = record { output = "<|im_start|>"; conversation = "<|im_start|>"; error = ""; status_code = 200 : nat16; prompt_remaining = "system\\nYou are a helpful assistant.<|im_end|>\\n<|im_start|>user\\nExplain Large Language Models.<|im_end|>\\n<|im_start|>assistant\\n"; generated_eog = false;} })'
    assert response == expected_response

def test__log_pause(network: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="log_pause",
        canister_argument='()',
        network=network,
    )
    expected_response = '(variant { Ok = record { status_code = 200 : nat16;} })'
    assert response == expected_response

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