"""UTF-8 boundary regression: a Devanagari prompt must not break the Candid reply.

`run_update` returns text in `max_tokens`-sized chunks, and byte-level BPE encodes
Devanagari as byte tokens, so a chunk boundary lands in the middle of a multi-byte
codepoint. Candid `text` is spec'd as valid UTF-8, so the CALLER traps while decoding
the reply - in Motoko that trap is not catchable and kills the calling canister
(IC0503 'utf8_validate: string is not UTF-8'). See TMP-HANDOVER-hindi-utf8-chunking.md.

This runs on the tiny stories model, which CI already deploys: its tokenizer falls back
to byte tokens for Devanagari, which is exactly the splitting case. No multi-GB model and
no extra CI time needed - the bug reproduces at `-n 1` during prompt INGESTION, because
`prompt_remaining` is split at a token boundary that is not a codepoint boundary.

Observed on v0.16.6 before the fix, with this very prompt:
    conversation     ends   ... 20 e0 a4   -> truncated 3-byte sequence
    prompt_remaining starts aa e0 a4 b0 ... -> orphan continuation byte

Both clients reject that, so either would catch it:
  - icp-cli (what these tests use) with `--output candid` errors out, and
    `call_canister_api` turns that into a "Failed call to api ..." string, so the
    `assert "Ok" in resp` below fails.
  - icp-py-core raises UnicodeDecodeError.

$ pytest -vv --network local --identity llama-cpp-testing test/test_tiny_stories_utf8.py
"""

# pylint: disable=missing-function-docstring, line-too-long

import inspect
from pathlib import Path

from .candid_compat import call_canister_api

ICP_YAML_PATH = Path(__file__).parent / "../icp.yaml"
CANISTER_NAME = "llama_cpp"

PRINT_RESPONSE = True

# Its own cache file, so this test cannot disturb the other tiny-stories tests.
_CACHE = '"--prompt-cache"; "utf8_boundary.cache"'

# Devanagari: every character is a 3-byte UTF-8 sequence, and the tiny model's
# tokenizer emits them as individual BYTE tokens -> a token boundary splits them.
PROMPT_HINDI = "धैर्य पर एक सूक्ति लिखिए"

# U+FFFD. `output` must never contain it: generated text is CARRIED across calls,
# not sanitized, so no bytes are lost. The informational fields may contain it.
REPLACEMENT_CHAR = "�"


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


def _run_update(network: str, prompt: str, n: str) -> str:
    arg = (
        f'(record {{ args = vec {{{_CACHE}; "--prompt-cache-all"; '
        f'"-n"; "{n}"; "-p"; "{prompt}"}} }})'
    )
    return _call(network, "run_update", arg)


# --------------------------------------------------------------------------------
def _assert_ok(resp: str, context: str) -> None:
    """The reply must be a decodable Candid Ok record.

    Do NOT use `assert "Ok" in resp`: when icp-cli's strict decoder rejects the reply
    it returns a ~6.6 KB "Failed call to api ..." string that echoes the whole command
    back, and that string CONTAINS the substring "Ok" - so the naive check passes on
    exactly the failure it is meant to catch. Assert the shape instead.
    """
    assert resp.startswith("(variant { Ok"), (
        f"{context}: the canister returned a reply the strict Candid decoder rejected "
        f"(invalid UTF-8 at a chunk boundary is the expected cause): {resp[:400]}"
    )


def test__new_chat(network: str) -> None:
    """Start from a REMOVED cache, not just a new_chat.

    new_chat re-uses an existing prompt-cache file ("Re-using existing prompt-cache
    file ..."). If a previous run already ingested this prompt, the next run consumes
    it in one call, no boundary is ever split, and the test passes vacuously.
    """
    _call(network, "remove_prompt_cache", f"(record {{ args = vec {{{_CACHE}}} }})")
    _assert_ok(
        _call(network, "new_chat", f"(record {{ args = vec {{{_CACHE}}} }})"),
        "new_chat",
    )


def test__ingest_devanagari_prompt_stays_valid_utf8(network: str) -> None:
    """Ingest at -n 1 so EVERY call splits the prompt at a byte-token boundary.

    A failure here is NOT a python problem: it means icp-cli's strict Candid decoder
    refused the reply, which is precisely what kills a Motoko caller.
    """
    for i in range(40):
        resp = _run_update(network, PROMPT_HINDI, "1")
        _assert_ok(resp, f"ingest call {i}")
        if 'prompt_remaining = ""' in resp:
            return
    raise AssertionError("prompt ingestion did not complete within 40 calls")


def test__generate_after_devanagari_prompt(network: str) -> None:
    """Generation following a Devanagari prompt must also round-trip cleanly."""
    out = ""
    for i in range(10):
        resp = _run_update(network, "", "20")
        _assert_ok(resp, f"generation call {i}")
        out += _field(resp, "output")
        if "generated_eog = true" in resp:
            break
    # `output` is carried, never sanitized, so a replacement char here means bytes
    # were dropped rather than deferred to the next call.
    assert REPLACEMENT_CHAR not in out, f"U+FFFD in generated output: {out!r}"


def _field(response: str, name: str) -> str:
    import re  # pylint: disable=import-outside-toplevel

    match = re.search(rf'{name} = "((?:[^"\\]|\\.)*)"', response)
    return match.group(1) if match else ""
