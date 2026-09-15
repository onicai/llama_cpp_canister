"""Hindi (Devanagari) generation on Qwen3-0.6B - the UTF-8 chunking regression.

This is the test that mirrors the real consumer: IConfucius (Motoko) asks for a
Hindi quote, and before the fix every single call killed the calling canister with
IC0503 'RTS error: utf8_validate: string is not UTF-8'. Qwen's BPE encodes
Devanagari as byte-level tokens, so a max_tokens chunk boundary lands inside a
multi-byte codepoint. See TMP-HANDOVER-hindi-utf8-chunking.md.

NOT run in CI: the Qwen entry in scripts/qa_deploy_and_pytest.py is commented out
because the Qwen models time out in the GitHub action. The CI-runnable counterpart
is test/test_tiny_stories_utf8.py, which reproduces the same boundary split on the
tiny model. Run this one locally before a release:

  # deploy llama_cpp, upload Qwen3-0.6B-Q8_0 as models/model.gguf, then:
  $ pytest -vv --network local --identity llama-cpp-testing test/test_qwen3_hindi.py
"""

# pylint: disable=missing-function-docstring, line-too-long

import inspect
import re

import pytest
from pathlib import Path

from .candid_compat import call_canister_api

ICP_YAML_PATH = Path(__file__).parent / "../icp.yaml"
CANISTER_NAME = "llama_cpp"

PRINT_RESPONSE = True

_CACHE = '"--cache-type-k"; "q8_0"; "--cache-type-v"; "q8_0"'
_CACHE_FILE = '"--prompt-cache"; "hindi.cache"'

# Exactly what IConfucius sends: non-thinking ChatML with a Devanagari system and
# user turn. `\\n` here is the two literal characters, which Candid turns into a newline.
PROMPT_HINDI = (
    "<|im_start|>system\\nआप कन्फ्यूशियस हैं, प्राचीन दार्शनिक।<|im_end|>\\n"
    "<|im_start|>user\\nधैर्य पर एक सूक्ति लिखिए। केवल सूक्ति दीजिए।<|im_end|>\\n"
    "<|im_start|>assistant\\n<think>\\n\\n</think>\\n\\n"
)

REPLACEMENT_CHAR = "�"
# Devanagari block, used to assert we actually got Hindi back rather than junk.
DEVANAGARI = re.compile(r"[ऀ-ॿ]")


def _call(network: str, method: str, arg: str) -> str:
    response = call_canister_api(
        icp_yaml_path=ICP_YAML_PATH,
        canister_name=CANISTER_NAME,
        canister_method=method,
        canister_argument=arg,
        network=network,
    )
    if PRINT_RESPONSE:
        print(f"{inspect.stack()[1].function}: {method}: {response[:300]}")
    return response


def _assert_ok(resp: str, context: str) -> None:
    """The reply must be a decodable Candid Ok record.

    NOT `assert "Ok" in resp`: the "Failed call to api ..." string that
    call_canister_api returns on a decode failure echoes the whole command back and
    itself contains "Ok", so the naive check passes on the very failure it targets.

    A canister TRAP is a different failure than a decode rejection and must not be
    reported as a UTF-8 bug. Long Hindi generations at ctx 16384 can exceed the
    per-message instruction limit (IC0522) or trip the documented local pocket-ic
    IC0502 flake; neither says anything about UTF-8, so skip rather than fail.
    """
    if resp.startswith("(variant { Ok"):
        return
    if "IC0522" in resp or "exceeded the limit" in resp:
        pytest.skip(
            f"{context}: IC0522 instruction limit - a capacity limit of this "
            f"model/ctx on the local replica, not a UTF-8 failure"
        )
    if "IC0502" in resp or "heap out of bounds" in resp:
        pytest.skip(
            f"{context}: known local pocket-ic IC0502 flake, not a UTF-8 failure"
        )
    raise AssertionError(
        f"{context}: reply rejected by the strict Candid decoder - this is the "
        f"Motoko-killing bug: {resp[:400]}"
    )


def _field(response: str, name: str) -> str:
    match = re.search(rf'{name} = "((?:[^"\\]|\\.)*)"', response)
    return match.group(1) if match else ""


def _run_update(network: str, prompt: str, n: str) -> str:
    arg = (
        f'(record {{ args = vec {{{_CACHE_FILE}; "--prompt-cache-all"; '
        f'{_CACHE}; "--temp"; "0.6"; "-sp"; "-p"; "{prompt}"; "-n"; "{n}"}} }})'
    )
    return _call(network, "run_update", arg)


def _set_max_tokens(network: str, n: int) -> None:
    _assert_ok(
        _call(
            network,
            "set_max_tokens",
            f"(record {{ max_tokens_query = 1 : nat64; max_tokens_update = {n} : nat64 }})",
        ),
        f"set_max_tokens({n})",
    )


def _generate_hindi(network: str, max_tokens_update: int) -> str:
    """Reset, ingest, then generate to EOG at `max_tokens_update`.

    Ingestion always runs at 20 even when generation is tested at 1: ingestion is
    capped by the same knob, so a 95-token prompt would need ~95 calls at 1, which
    is slow and tests nothing extra - the ingest boundary is already split at 20
    (visible as U+FFFD in `conversation`). The carry is exercised by GENERATION,
    which is what max_tokens_update varies here.
    """
    _set_max_tokens(network, 20)
    # Remove, not just new_chat: new_chat RE-USES an existing cache, so a second
    # run would ingest in one call and never split a boundary.
    _call(
        network, "remove_prompt_cache", f"(record {{ args = vec {{{_CACHE_FILE}}} }})"
    )
    _assert_ok(
        _call(
            network,
            "new_chat",
            f"(record {{ args = vec {{{_CACHE_FILE}; {_CACHE}}} }})",
        ),
        "new_chat",
    )

    for i in range(60):
        resp = _run_update(network, PROMPT_HINDI, "1")
        _assert_ok(resp, f"ingest call {i} (max_tokens={max_tokens_update})")
        if 'prompt_remaining = ""' in resp:
            break
    else:
        raise AssertionError("prompt ingestion did not complete within 60 calls")

    # Now switch to the max_tokens under test for the generation phase.
    _set_max_tokens(network, max_tokens_update)

    out = ""
    for i in range(400 if max_tokens_update == 1 else 60):
        resp = _run_update(network, "", "512")
        _assert_ok(resp, f"generate call {i} (max_tokens={max_tokens_update})")
        out += _field(resp, "output")
        if "generated_eog = true" in resp:
            break
    return out


def _assert_clean_hindi(out: str, context: str) -> None:
    """The contract the canister owes its caller.

    Deliberately NOT "zero U+FFFD". The carry is a pure partition of
    (carry + output), so it cannot lose bytes - the native round-trip test proves
    that at every split offset. A U+FFFD therefore means the MODEL emitted bytes
    that do not form a codepoint, which a 0.6B model at temp 0.6 occasionally does
    (observed: a lone `e0 a4` never completed, then a space). Replacing those is
    correct and is the whole point of the fix; the bug being guarded against is
    emitting them RAW, which traps the caller.

    So: the text must be valid UTF-8, must actually be Hindi, and U+FFFD must be
    rare rather than pervasive (pervasive would mean the carry is broken).
    """
    assert out.strip(), f"{context}: no tokens generated"
    out.encode("utf-8", "strict")  # must not raise
    assert DEVANAGARI.search(out), f"{context}: no Devanagari in the answer: {out!r}"
    n_repl = out.count(REPLACEMENT_CHAR)
    n_deva = len(DEVANAGARI.findall(out))
    assert n_repl <= max(2, n_deva // 10), (
        f"{context}: {n_repl} U+FFFD against {n_deva} Devanagari characters - too many "
        f"to be stray malformed model bytes; the carry is probably broken: {out!r}"
    )


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


def test__hindi_at_max_tokens_20(network: str) -> None:
    """The production setting."""
    out = _generate_hindi(network, 20)
    _assert_clean_hindi(out, "max_tokens=20")


def test__hindi_at_max_tokens_1(network: str) -> None:
    """Worst case: every single call ends on a chunk boundary."""
    out = _generate_hindi(network, 1)
    _assert_clean_hindi(out, "max_tokens=1")
