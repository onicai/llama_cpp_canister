"""Qwen3-0.6B-Q8_0 smoke tests — multi-turn, non-thinking on-chain inference.

Qwen3 is a hybrid *thinking* model. This canister runs it in NON-thinking mode by
ending the assistant turn with an empty `<think>\\n\\n</think>\\n\\n` block (the
official chat-template behaviour for `enable_thinking=false`). These tests assert
the theme behaviours with ROBUST (non-exact-token) checks: the generated output is
coherent and contains no `<think>`/`</think>` tokens, and a multi-turn conversation
recalls facts from earlier turns.

Deploy + upload the model as `models/model.gguf`, then:
$ pytest -vv --network local --identity "$(icp identity default)" test/test_qwen3.py

Notes:
- Requires the tuned config: the canister's wasm_memory_limit is raised to 3.75 GiB
  (via `icp canister settings update`), and load_model uses --ctx-size 16384
  --batch-size 64 --ubatch-size 64 --cache-type-k q8_0 --cache-type-v q8_0. The small
  batch shrinks the compute buffers so the KV cache is the only thing scaling with
  context; ctx 16384 sits at ~1.76 GiB heap (~2 GiB headroom under the 3.75 GiB limit).
"""

# pylint: disable=missing-function-docstring, line-too-long

import inspect
import re
from pathlib import Path

from .candid_compat import call_canister_api, norm

ICP_YAML_PATH = Path(__file__).parent / "../icp.yaml"
CANISTER_NAME = "llama_cpp"

PRINT_RESPONSE = True


def current_func_name() -> str:
    return inspect.stack()[1].function


# Shared KV-cache args (dual q8_0). Reused by new_chat / run_update.
_CACHE = '"--cache-type-k"; "q8_0"; "--cache-type-v"; "q8_0"'

# Non-thinking prompts. `\\n` in this Python source is the two literal characters
# backslash-n, which Candid then decodes to a newline. The assistant turn ends with
# an empty <think></think> block => enable_thinking=false.
PROMPT_INTRO = (
    "<|im_start|>user\\ngive me a short introduction to LLMs.<|im_end|>\\n"
    "<|im_start|>assistant\\n<think>\\n\\n</think>\\n\\n"
)
PROMPT_MULTITURN = (
    "<|im_start|>user\\nMy name is Sam and I have 3 cats named Milo, Coco, and Ziggy.<|im_end|>\\n"
    "<|im_start|>assistant\\nNice to meet you, Sam! You have three cats: Milo, Coco, and Ziggy.<|im_end|>\\n"
    "<|im_start|>user\\nHow many cats do I have, what are their names, and what is my name?<|im_end|>\\n"
    "<|im_start|>assistant\\n<think>\\n\\n</think>\\n\\n"
)


def _call(network: str, method: str, arg: str) -> str:
    response = call_canister_api(
        icp_yaml_path=ICP_YAML_PATH,
        canister_name=CANISTER_NAME,
        canister_method=method,
        canister_argument=arg,
        network=network,
    )
    if PRINT_RESPONSE:
        print(f"{inspect.stack()[1].function}: {method}: {response}")
    return response


def _field(response: str, name: str) -> str:
    """Extract a `name = "..."` string field from a Candid response (handles escapes)."""
    match = re.search(rf'{name} = "((?:[^"\\]|\\.)*)"', response)
    return match.group(1) if match else ""


def _run_update(network: str, prompt: str, n: str) -> str:
    arg = (
        '(record { args = vec {"--prompt-cache"; "prompt.cache"; "--prompt-cache-all"; '
        f'{_CACHE}; "--temp"; "0.6"; "-sp"; "-p"; "{prompt}"; "-n"; "{n}"}} }})'
    )
    return _call(network, "run_update", arg)


def _ingest(network: str, prompt: str) -> None:
    """Prefill the prompt (‑n 1) until prompt_remaining is empty."""
    for _ in range(25):
        resp = _run_update(network, prompt, "1")
        assert "Ok" in resp, resp
        if 'prompt_remaining = ""' in resp:
            return
    raise AssertionError("prefill did not complete within 25 calls")


def _generate(network: str, max_calls: int = 25) -> str:
    """Generate (empty prompt) to EOG, returning the concatenated generated output."""
    out = ""
    for _ in range(max_calls):
        resp = _run_update(network, "", "512")
        assert "Ok" in resp, resp
        out += _field(resp, "output")
        if "generated_eog = true" in resp:
            break
    return out


# --------------------------------------------------------------------------------
def test__load_model(network: str) -> None:
    response = _call(
        network,
        "load_model",
        '(record { args = vec {"--model"; "models/model.gguf"; '
        f'{_CACHE}; "--batch-size"; "64"; "--ubatch-size"; "64"; '
        '"--ctx-size"; "16384"} })',
    )
    assert "(variant { Ok" in response


def test__set_max_tokens(network: str) -> None:
    response = _call(
        network,
        "set_max_tokens",
        "(record { max_tokens_query = 1 : nat64; max_tokens_update = 20 : nat64 })",
    )
    assert "(variant { Ok" in response


def test__ready(network: str) -> None:
    response = _call(network, "ready", "()")
    assert response == norm("(variant { Ok = record { status_code = 200 : nat16;} })")


def test__generate_no_think(network: str) -> None:
    """enable_thinking=false must yield a coherent answer with NO <think> tokens."""
    assert "Ok" in _call(
        network,
        "new_chat",
        f'(record {{ args = vec {{"--prompt-cache"; "prompt.cache"; {_CACHE}}} }})',
    )
    _ingest(network, PROMPT_INTRO)
    output = _generate(network)

    assert output.strip(), "no tokens generated"
    assert "<think>" not in output, f"unexpected <think> in output: {output!r}"
    assert "</think>" not in output, f"unexpected </think> in output: {output!r}"


def test__multi_turn_recall(network: str) -> None:
    """A follow-up turn must recall facts from the earlier turn (non-thinking)."""
    assert "Ok" in _call(
        network,
        "new_chat",
        f'(record {{ args = vec {{"--prompt-cache"; "prompt.cache"; {_CACHE}}} }})',
    )
    _ingest(network, PROMPT_MULTITURN)
    answer = _generate(network)

    assert answer.strip(), "no tokens generated"
    assert "<think>" not in answer, f"unexpected <think> in answer: {answer!r}"
    # Recall from turn 1: the user's name and the cats.
    assert "Sam" in answer, f"did not recall the name: {answer!r}"
    assert any(
        name in answer for name in ("Milo", "Coco", "Ziggy")
    ), f"did not recall the cat names: {answer!r}"
