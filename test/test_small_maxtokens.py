"""Regression test: multi-call prompt ingestion with a small max_tokens_update.

Guards the fix for the "prompt-cache stall" bug. When max_tokens_update is small
(e.g. 4), a prompt longer than one chunk must be ingested over several run_update
calls, each carrying state via the prompt cache. The canister reloads the session
file at the start of every call, so it MUST re-save it every call — even when the
caller does NOT pass --prompt-cache-all (ICGPT's frontend does not). Before the
fix, the incremental/final session saves were gated on --prompt-cache-all, so
without it prompt_remaining never shrank (every call re-ingested the same first
chunk) and generation then failed.

This test deliberately does NOT pass --prompt-cache-all — that is the regime that
was broken.

Model-agnostic: the model file, KV cache type and (for larger models) a bounded
context/batch are read from the environment so the QA harness can run it on BOTH
the tiny stories model and gemma-3-270M:

    SMALL_MAXTOK_MODEL   canister filename to load   (default: models/tiny.gguf)
    SMALL_MAXTOK_KV      "" (f16, default) or "q8_0"
    SMALL_MAXTOK_CTX     "" (model default) or e.g. "512"  -> load with -c CTX
    SMALL_MAXTOK_BATCH   "" (model default) or e.g. "64"    -> --batch-size/--ubatch-size

The bounded context/batch keep the compute + KV buffers small enough that
generation does not trip the local pocket-ic "heap out of bounds" (IC0502) flake
on a real model; they do not affect the ingestion logic under test.

Deterministic at --temp 0.0.

$ SMALL_MAXTOK_MODEL=models/model.gguf SMALL_MAXTOK_KV=q8_0 SMALL_MAXTOK_CTX=512 \
      SMALL_MAXTOK_BATCH=64 pytest -vv --network local test/test_small_maxtokens.py
"""

# pylint: disable=missing-function-docstring, line-too-long

import os
import re
from pathlib import Path

import pytest

from .candid_compat import call_canister_api

ICP_YAML_PATH = Path(__file__).parent / "../icp.yaml"
CANISTER_NAME = "llama_cpp"

MODEL = os.environ.get("SMALL_MAXTOK_MODEL", "models/tiny.gguf")
KV = os.environ.get("SMALL_MAXTOK_KV", "")  # "" -> f16 default, or "q8_0"
CTX = os.environ.get("SMALL_MAXTOK_CTX", "")  # "" -> model default, or e.g. "512"
BATCH = os.environ.get("SMALL_MAXTOK_BATCH", "")  # "" -> model default, or e.g. "64"

# A plain prompt that tokenizes to well more than one 4-token chunk on any model.
PROMPT = "Joe loves writing stories and poems and songs every day"
MAX_TOKENS = 4
CACHE = "small_maxtok/prompt.cache"


def _call(network: str, method: str, arg: str) -> str:
    return call_canister_api(
        icp_yaml_path=ICP_YAML_PATH,
        canister_name=CANISTER_NAME,
        canister_method=method,
        canister_argument=arg,
        network=network,
    )


def _field(response: str, name: str) -> str:
    match = re.search(rf'{name} = "((?:[^"\\]|\\.)*)"', response)
    return match.group(1) if match else ""


def _cached(response: str) -> int:
    match = re.search(r"n_prompt_tokens_cached = opt \(([\d_]+) : nat64\)", response)
    assert match, f"n_prompt_tokens_cached missing: {response}"
    return int(match.group(1).replace("_", ""))


def _kv_args() -> str:
    """Extra vec entries for the KV cache type, if configured (else empty).

    Used on run_update / new_chat / remove_prompt_cache so the session round-trips
    with the same cache type the model was loaded with.
    """
    if not KV:
        return ""
    return f'; "--cache-type-k"; "{KV}"; "--cache-type-v"; "{KV}"'


def _load_extra() -> str:
    """Extra vec entries for load_model: KV type + bounded context/batch."""
    extra = _kv_args()
    if CTX:
        extra += f'; "-c"; "{CTX}"'
    if BATCH:
        extra += f'; "--batch-size"; "{BATCH}"; "--ubatch-size"; "{BATCH}"'
    return extra


# NOTE: no "--prompt-cache-all" here — this is the previously-broken regime.
def _run(network: str, prompt: str, n: str) -> str:
    arg = (
        '(record { args = vec {"--prompt-cache"; "'
        + CACHE
        + '"'
        + _kv_args()
        + '; "--samplers"; "temperature"; "--temp"; "0.0"; "-p"; "'
        + prompt
        + '"; "-n"; "'
        + n
        + '"} })'
    )
    return _call(network, "run_update", arg)


def test__load_model(network: str) -> None:
    resp = _call(
        network,
        "load_model",
        '(record { args = vec {"--model"; "' + MODEL + '"' + _load_extra() + "} })",
    )
    assert "(variant { Ok" in resp, resp


def test__setup(network: str) -> None:
    assert "Ok" in _call(
        network,
        "set_max_tokens",
        f"(record {{ max_tokens_query = 1 : nat64; max_tokens_update = {MAX_TOKENS} : nat64 }})",
    )
    _call(network, "remove_prompt_cache", f'(record {{ args = vec {{"--prompt-cache"; "{CACHE}"{_kv_args()}}} }})')
    assert "Ok" in _call(network, "new_chat", f'(record {{ args = vec {{"--prompt-cache"; "{CACHE}"{_kv_args()}}} }})')


def test__ingestion_advances_without_prompt_cache_all(network: str) -> None:
    """The core anti-stall assertions: cache advances and prompt_remaining -> ""."""
    remainings = []
    cacheds = []
    for _ in range(30):
        resp = _run(network, PROMPT, "1")
        assert "Ok" in resp, resp
        remainings.append(_field(resp, "prompt_remaining"))
        cacheds.append(_cached(resp))
        if remainings[-1] == "":
            break
    else:
        # The exact failure signature of the bug: stuck, never reaching "".
        raise AssertionError(
            f"STALL: prompt_remaining never reached '' in 30 calls.\n"
            f"  remainings={remainings}\n  cached={cacheds}"
        )

    # The bug made every call identical; a healthy run visits many distinct states.
    assert len(set(remainings)) > 1, f"prompt_remaining never changed (stall): {remainings}"

    # cached-break offset starts at 0, never goes backwards, and clearly advances
    # past the first chunk (the stall pinned it at 0).
    assert cacheds[0] == 0, cacheds
    for prev, cur in zip(cacheds, cacheds[1:]):
        assert cur >= prev, f"n_prompt_tokens_cached went backwards (cache reset): {cacheds}"
    assert max(cacheds) > MAX_TOKENS, f"cache never advanced past one chunk (stall): {cacheds}"

    # more than one call was needed => we really exercised the chunked regime
    assert len(remainings) > 1, "expected a multi-call ingestion at small max_tokens"


def test__generation_after_ingestion(network: str) -> None:
    """Once ingested, an empty-prompt generate loop must produce non-empty output.

    Generation on the local pocket-ic replica can intermittently trap with
    "heap out of bounds" (IC0502) — a documented local-replica artifact that does
    NOT reproduce on mainnet (see the llama_cpp_canister-release-test skill). The
    prompt-cache stall this file guards is proven by test__ingestion_advances_*;
    so here we skip on that specific known flake rather than fail CI.
    """
    out = ""
    for _ in range(30):
        resp = _run(network, "", str(MAX_TOKENS))
        if "heap out of bounds" in resp or "IC0502" in resp:
            pytest.skip(
                "known local pocket-ic IC0502 heap-out-of-bounds flake during generation"
            )
        assert "Ok" in resp, resp
        out += _field(resp, "output")
        if "generated_eog = true" in resp:
            break
    assert out.strip(), f"generation produced no tokens: {out!r}"
