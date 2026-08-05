"""Exact token-accounting smoke tests (v0.15.0).

Verifies the 5 `opt nat64` fields that `run_update` gained in v0.15.0 so ICGPT's
on-chain Prompt Cost Lab can measure the prompt-cache break offset and the exact
ingest-vs-generate split per call:

    n_prompt_tokens            total prompt tokens presented this call
    n_prompt_tokens_cached     prompt-cache prefix hit  (= cache-break offset)
    n_prompt_tokens_decoded    prompt tokens actually decoded this call
    n_tokens_generated         tokens generated this call
    n_prompt_tokens_remaining  prompt suffix left for the next call

Asserted properties:
- fields present on `run_update` success; decode to `null` on `new_chat`
- reconciliation, every call: cached + decoded + remaining == n_prompt_tokens
- generation only after ingest: n_tokens_generated > 0 => remaining == 0
- cold call: cached == 0
- multi-call ingest (same prompt re-sent, capped at max_tokens=5): the cache
  advances (cur.cached == prev.cached + prev.decoded) and
  first.cached + Σ decoded == n_prompt_tokens
- warm re-send of a fully cached prefix: cached == n_prompt_tokens, decoded == 0

Runs against the tiny stories model (models/tiny.gguf) deployed by
scripts/qa_deploy_and_pytest.py; deterministic at --temp 0.0.

$ pytest -vv --network local --identity "$(icp identity default)" test/test_token_counts.py
"""

# pylint: disable=missing-function-docstring, line-too-long

import re
from pathlib import Path

from .candid_compat import call_canister_api

ICP_YAML_PATH = Path(__file__).parent / "../icp.yaml"
CANISTER_NAME = "llama_cpp"

PROMPT = "Joe loves writing stories"
FIELDS = (
    "n_prompt_tokens",
    "n_prompt_tokens_cached",
    "n_prompt_tokens_decoded",
    "n_tokens_generated",
    "n_prompt_tokens_remaining",
)
_REMOVE_ARG = '(record { args = vec {"--prompt-cache"; "prompt.cache"} })'
_NEW_CHAT_ARG = '(record { args = vec {"--prompt-cache"; "prompt.cache"} })'


def _call(network: str, method: str, arg: str) -> str:
    return call_canister_api(
        icp_yaml_path=ICP_YAML_PATH,
        canister_name=CANISTER_NAME,
        canister_method=method,
        canister_argument=arg,
        network=network,
    )


def _nat(response: str, name: str) -> int:
    """Extract an `name = opt (N : nat64)` field (handles icp's `_` grouping)."""
    match = re.search(rf"{name} = opt \(([\d_]+) : nat64\)", response)
    assert match, f"{name} missing in: {response}"
    return int(match.group(1).replace("_", ""))


def _tokens(response: str) -> dict:
    assert "Ok" in response, response
    return {name: _nat(response, name) for name in FIELDS}


def _reconciles(t: dict) -> None:
    assert (
        t["n_prompt_tokens_cached"]
        + t["n_prompt_tokens_decoded"]
        + t["n_prompt_tokens_remaining"]
        == t["n_prompt_tokens"]
    ), t
    # generation happens only once the prompt is fully ingested
    assert t["n_tokens_generated"] == 0 or t["n_prompt_tokens_remaining"] == 0, t


def _run(network: str, prompt: str, n: str = "5") -> str:
    arg = (
        '(record { args = vec {"--prompt-cache"; "prompt.cache"; "--prompt-cache-all"; '
        '"--samplers"; "temperature"; "--temp"; "0.0"; "-n"; "'
        + n
        + '"; "-p"; "'
        + prompt
        + '"} })'
    )
    return _call(network, "run_update", arg)


def _new_chat(network: str) -> str:
    return _call(network, "new_chat", _NEW_CHAT_ARG)


# --------------------------------------------------------------------------------
def test__setup(network: str) -> None:
    # cap each run_update at 5 tokens so a 16-token prompt ingests over several calls
    assert "Ok" in _call(
        network,
        "set_max_tokens",
        "(record { max_tokens_query = 5 : nat64; max_tokens_update = 5 : nat64 })",
    )


def test__new_chat_reports_token_fields_null(network: str) -> None:
    # new_chat builds a record WITHOUT the opt fields; decoded against the v0.15.0
    # .did every one of them is `null` (the whole point of making them `opt`).
    _call(network, "remove_prompt_cache", _REMOVE_ARG)
    resp = _new_chat(network)
    assert "Ok" in resp, resp
    for name in FIELDS:
        assert f"{name} = null" in resp, f"{name} not null on new_chat: {resp}"


def test__cold_and_multi_call_ingest(network: str) -> None:
    _call(network, "remove_prompt_cache", _REMOVE_ARG)
    assert "Ok" in _new_chat(network)

    calls = []
    for _ in range(25):
        t = _tokens(_run(network, PROMPT))
        _reconciles(t)
        calls.append(t)
        if t["n_prompt_tokens_remaining"] == 0:
            break
    else:
        raise AssertionError("prompt did not fully ingest within 25 calls")

    total = calls[0]["n_prompt_tokens"]
    assert total > 0
    assert len(calls) > 1, "expected a multi-call ingest with max_tokens=5"

    # cold first call: nothing cached yet
    assert calls[0]["n_prompt_tokens_cached"] == 0, calls[0]

    # every call sees the same total prompt length
    assert all(c["n_prompt_tokens"] == total for c in calls), calls

    # the cache advances exactly by what the previous call decoded
    for prev, cur in zip(calls, calls[1:]):
        assert (
            cur["n_prompt_tokens_cached"]
            == prev["n_prompt_tokens_cached"] + prev["n_prompt_tokens_decoded"]
        ), (prev, cur)

    # core ingest identity
    assert (
        calls[0]["n_prompt_tokens_cached"]
        + sum(c["n_prompt_tokens_decoded"] for c in calls)
        == total
    ), calls

    # no generation while prompt tokens remain; the final call clears the prompt
    for c in calls[:-1]:
        assert c["n_tokens_generated"] == 0, c
        assert c["n_prompt_tokens_remaining"] > 0, c
    assert calls[-1]["n_prompt_tokens_remaining"] == 0, calls[-1]


def test__warm_resend_full_prefix(network: str) -> None:
    # The previous test left the full PROMPT in prompt.cache. A fresh chat that
    # re-sends the same prompt must hit the cache entirely: cached == total,
    # decoded == 0. This is the Prompt Cost Lab's cache-break-offset property.
    assert "Ok" in _new_chat(network)
    t = _tokens(_run(network, PROMPT))
    _reconciles(t)
    assert t["n_prompt_tokens_cached"] == t["n_prompt_tokens"], t
    assert t["n_prompt_tokens_decoded"] == 0, t
    assert t["n_prompt_tokens_remaining"] == 0, t
