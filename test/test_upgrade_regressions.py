"""Regression tests for the two bug classes that a llama.cpp upgrade re-introduces.

READ THIS BEFORE DELETING OR WEAKENING A TEST HERE.

`src/main_.cpp` is a PORT of upstream's `tools/completion/completion.cpp` and is
re-done on every llama.cpp upgrade. It carries ~93 `ICPP-PATCH` markers, and
patches have been silently dropped in a re-port before (b10076 lost the
`use_mmap` and warmup patches — see README-0003-305ba519.md, Bugs #3/#4). Those
losses only surfaced as runtime traps on a replica, long after `make
test-llm-native` was green.

The two classes, both documented in README-contributors-guide.md
("Two classes of bug that upstream does NOT have"):

  1. Upstream's try/catch error handling is DEAD in the canister. The WASI
     exception shim makes `throw` a trap and `catch` a no-op, so an upstream API
     whose contract is "catch internally, return an error code" TRAPS instead.
  2. The llama_context outlives the common_params that created it. Any
     `params.<x>` that upstream assumes equals the context's `cparams.<x>` is
     stale here — it is the common_params default, not what the context has.

WHY THESE LIVE IN PYTEST AND NOT IN native/:
The native build cannot reproduce either failure. Native has working exceptions
(so class 1 degrades to a clean Err instead of a trap) and no instruction limit.
Only the deployed wasm on a replica has the shim and the trap semantics these
tests assert on. A green native suite proves nothing here.

This file loads the model itself, with a deliberately SMALL --batch-size, so it
must run last in a QA iteration (see scripts/qa_deploy_and_pytest.py).

$ pytest -vv --network local --identity "$(icp identity default)" test/test_upgrade_regressions.py
"""

# pylint: disable=missing-function-docstring, line-too-long

import re
from pathlib import Path

from .candid_compat import call_canister_api

ICP_YAML_PATH = Path(__file__).parent / "../icp.yaml"
CANISTER_NAME = "llama_cpp"

MODEL = "models/tiny.gguf"

# The vendored fork is a SEPARATE repo (onicai/llama_cpp_onicai_fork, branch
# `onicai`) re-vendored on every llama.cpp upgrade, so patches in it are at risk
# of being silently dropped -- exactly as b10076 lost the use_mmap and warmup
# patches (README-0003-305ba519.md, Bugs #3/#4).
GGML_BACKEND_CPP = (
    Path(__file__).parent
    / "../src/llama_cpp_onicai_fork/ggml/src/ggml-backend.cpp"
)

# The whole point: load with a batch far below the max_tokens used later, so the
# context's n_batch and common_params' 2048 default are unmistakably different.
LOAD_BATCH = 8
LOAD_CTX = 512

# > LOAD_BATCH, so a decode sized from params.n_batch would exceed the context's
# batch and trip GGML_ASSERT(n_tokens_all <= cparams.n_batch).
MAX_TOKENS_ABOVE_BATCH = 40

CACHE_BATCH = "upgrade_regr_batch/prompt.cache"
CACHE_FOREIGN = "upgrade_regr_foreign/prompt.cache"
# Save/restore + branch caches, for the copy_prompt_cache tests.
CACHE_LIVE = "upgrade_regr_copy/prompt.cache"
CACHE_SAVE = "upgrade_regr_copy/prompt-save.cache"
CACHE_BRANCH = "upgrade_regr_copy/prompt-branch.cache"

# Ingested once, then re-sent to read back how much of it is served from cache.
WARM_PROMPT = "Joe loves writing stories and poems"

# Tokenizes to well over LOAD_CTX tokens on any vocab.
LONG_PROMPT = "the quick brown fox jumps over the lazy dog and then runs away " * 40

# Markers of a canister TRAP, as surfaced by icp when a call is rejected.
# IC0502 = failed GGML_ASSERT / memory fault, IC0503 = uncaught C++ exception.
TRAP_MARKERS = (
    "IC0502",
    "IC0503",
    "heap out of bounds",
    "unreachable",
    "Canister trapped",
    "UNCAUGHT C++ EXCEPTION",
)


def _call(network: str, method: str, arg: str) -> str:
    return call_canister_api(
        icp_yaml_path=ICP_YAML_PATH,
        canister_name=CANISTER_NAME,
        canister_method=method,
        canister_argument=arg,
        network=network,
    )


def _assert_no_trap(response: str, what: str) -> None:
    """A trap is never acceptable: it rolls back the message AND leaves the
    caller's prompt.cache half-written, so every later call re-traps."""
    for marker in TRAP_MARKERS:
        assert marker not in response, (
            f"{what} TRAPPED (marker {marker!r}).\n"
            f"An ICPP-PATCH in src/main_.cpp was most likely lost in a re-port — "
            f"see README-contributors-guide.md, "
            f"'The v0.16.2 fixes live in main_.cpp — RE-APPLY them'.\n"
            f"{response}"
        )


def _cached(response: str) -> int:
    match = re.search(r"n_prompt_tokens_cached = opt \(([\d_]+) : nat64\)", response)
    assert match, f"n_prompt_tokens_cached missing: {response}"
    return int(match.group(1).replace("_", ""))


def _decoded(response: str) -> int:
    match = re.search(r"n_prompt_tokens_decoded = opt \(([\d_]+) : nat64\)", response)
    assert match, f"n_prompt_tokens_decoded missing: {response}"
    return int(match.group(1).replace("_", ""))


def _field(response: str, name: str) -> str:
    match = re.search(rf'{name} = "((?:[^"\\]|\\.)*)"', response)
    return match.group(1) if match else ""


def _copy_cache(network: str, from_cache: str, to_cache: str) -> str:
    return _call(
        network,
        "copy_prompt_cache",
        f'(record {{ from = "{from_cache}"; to = "{to_cache}" }})',
    )


def _ingest_fully(network: str, cache: str, prompt: str) -> None:
    """Loop run_update until the whole prompt is in the cache."""
    for _ in range(30):
        resp = _run(network, cache, prompt, "1")
        _assert_no_trap(resp, "run_update during ingestion")
        assert "(variant { Ok" in resp, resp
        if _field(resp, "prompt_remaining") == "":
            return
    raise AssertionError(f"prompt never fully ingested into {cache}")


def _warm_reading(network: str, cache: str, prompt: str) -> int:
    """Re-send an already-ingested prompt; returns how many tokens were served
    from the cache. Decoding ANY of them means the cache was not reused."""
    resp = _run(network, cache, prompt, "1")
    _assert_no_trap(resp, "warm-cache reading")
    assert "(variant { Ok" in resp, resp
    assert _decoded(resp) == 0, (
        f"{_decoded(resp)} prompt tokens had to be re-ingested, so the cache was "
        f"NOT reused (a silent cold start).\n{resp}"
    )
    return _cached(resp)


def _set_max_tokens(network: str, value: int) -> str:
    return _call(
        network,
        "set_max_tokens",
        f"(record {{ max_tokens_query = 1 : nat64; max_tokens_update = {value} : nat64 }})",
    )


def _run(network: str, cache: str, prompt: str, n: str) -> str:
    return _call(
        network,
        "run_update",
        '(record { args = vec {"--prompt-cache"; "'
        + cache
        + '"; "--prompt-cache-all"; "--samplers"; "temperature"; "--temp"; "0.0"'
        + '; "-p"; "'
        + prompt
        + '"; "-n"; "'
        + n
        + '"} })',
    )


# ---------------------------------------------------------------------------
# Class 3: a patch in the vendored fork, lost on re-vendor.
# Guards the CPU-buffer get_max_size split (fixes IC0522 at large --ctx-size).
# ---------------------------------------------------------------------------
def test__cpu_buffer_max_size_patch_is_present() -> None:
    """The CPU buffer type must cap buffer size, so big memsets get split.

    WHY THIS IS A SOURCE CHECK AND NOT A BEHAVIOURAL TEST
    -----------------------------------------------------
    The split cannot be observed at runtime from outside the canister:

      * native never splits -- get_max_size returns SIZE_MAX off-WASI by design,
        so n_buffers is always 1 there;
      * the local replica cannot fail either way -- it runs at
        dirty_page_overhead = 1000 (its network-launcher predates dfinity/ic
        b7225383e), where the ceiling is ~5x higher than mainnet's and
        unreachable in a wasm32 heap;
      * the split is invisible in the logs -- llama.cpp prints
        `ggml_backend_buffer_name(buf)`, which resolves to the BUFFER TYPE's
        name ("CPU") for a multi_buffer too, and the size it prints is the
        total either way.

    So a behavioural test would pass with or without the fix, i.e. it could
    never go red -- which makes it worthless as a regression test. The failure
    mode actually worth guarding is the patch being dropped when the fork is
    re-vendored, and that is exactly what this checks.

    If this fails: re-apply the ICPP-PATCH in ggml-backend.cpp, or Qwen3-1.7B at
    --ctx-size 16384 will again be rejected on mainnet with IC0522 "large memory
    operation ... exceeded the slice limit". See
    TMP-fix-load-model-large-ctx-ic0522.md.
    """
    assert GGML_BACKEND_CPP.exists(), f"vendored fork missing: {GGML_BACKEND_CPP}"
    src = GGML_BACKEND_CPP.read_text(encoding="utf-8")

    assert "ggml_backend_cpu_buffer_type_get_max_size" in src, (
        "The ICPP-PATCH that bounds CPU buffer size is GONE from the vendored "
        "fork -- most likely dropped in a re-vendor. Without it "
        "ggml_backend_alloc_ctx_tensors_from_buft() never splits, the whole KV "
        "cache is cleared by one memset, and load_model at a large --ctx-size "
        "is rejected with IC0522 on mainnet."
    )

    # ...and it must actually be wired into the buffer type, not merely defined.
    assert re.search(
        r"\.get_max_size\s*=\s*\*/\s*ggml_backend_cpu_buffer_type_get_max_size", src
    ), (
        "ggml_backend_cpu_buffer_type_get_max_size() exists but is NOT wired "
        "into ggml_backend_cpu_buffer_type's iface (.get_max_size), so it has "
        "no effect."
    )


def test__load_model_with_small_batch(network: str) -> None:
    """Load with an explicitly small batch — the precondition for everything below."""
    resp = _call(
        network,
        "load_model",
        '(record { args = vec {"--model"; "'
        + MODEL
        + f'"; "-c"; "{LOAD_CTX}"; "--batch-size"; "{LOAD_BATCH}"; "--ubatch-size"; "{LOAD_BATCH}"'
        + "} })",
    )
    _assert_no_trap(resp, "load_model")
    assert "(variant { Ok" in resp, resp


# ---------------------------------------------------------------------------
# Class 2: the context outlives the params that created it.
# Guards ICPP-PATCH items 1-3 (n_batch_ctx) in src/main_.cpp.
# ---------------------------------------------------------------------------
def test__max_tokens_above_loaded_batch_does_not_trap(network: str) -> None:
    """max_tokens > the loaded --batch-size must NOT trap.

    Before the fix, main_.cpp clamped n_eval to `params.n_batch` (the 2048
    common_params default) while llama_decode asserts against the context's
    `cparams.n_batch` (LOAD_BATCH). The clamp was dead code, so any
    max_tokens_update above the loaded batch tripped
    `GGML_ASSERT(n_tokens_all <= cparams.n_batch)` -> IC0502 'unreachable'.

    Decode is now chunked at llama_n_batch(ctx), so this is either a normal Ok
    or — if the model/host is slow enough — a clean IC0522 instruction-limit
    rejection. Never a trap.
    """
    assert "Ok" in _set_max_tokens(network, MAX_TOKENS_ABOVE_BATCH)
    _call(
        network,
        "remove_prompt_cache",
        f'(record {{ args = vec {{"--prompt-cache"; "{CACHE_BATCH}"}} }})',
    )
    assert "Ok" in _call(
        network,
        "new_chat",
        f'(record {{ args = vec {{"--prompt-cache"; "{CACHE_BATCH}"}} }})',
    )

    resp = _run(network, CACHE_BATCH, "Joe loves writing stories and poems and songs every single day of the week", "1")
    _assert_no_trap(resp, f"run_update with max_tokens={MAX_TOKENS_ABOVE_BATCH} > batch={LOAD_BATCH}")
    # The tiny model decodes this well inside the instruction limit, so it must
    # actually succeed rather than merely "not trap".
    assert "(variant { Ok" in resp, resp

    _set_max_tokens(network, 4)


def test__ctx_size_is_taken_from_the_context_not_params(network: str) -> None:
    """The prompt-too-long guard must use the LOADED ctx, not params.n_ctx.

    A second instance of class 2, and a canary for the whole class: run_update
    does not repeat `-c`, so if main_.cpp ever reads `params.n_ctx` here it would
    see common_params' default (far above LOAD_CTX) and silently accept a prompt
    that cannot fit the real context.

    With LOAD_CTX=512 the guard is `embd_inp.size() > n_ctx - 4`, so a prompt of
    hundreds of tokens must be rejected with a message naming 508.
    """
    resp = _run(network, CACHE_BATCH, LONG_PROMPT, "1")
    _assert_no_trap(resp, "run_update with an over-long prompt")
    assert "prompt is too long" in resp, (
        "The over-long prompt was NOT rejected, so the context guard is using a "
        f"stale params.n_ctx instead of llama_n_ctx(ctx)={LOAD_CTX}.\n{resp}"
    )
    assert f"max {LOAD_CTX - 4}" in resp, (
        f"Rejected, but not against the loaded context size ({LOAD_CTX} - 4).\n{resp}"
    )


# ---------------------------------------------------------------------------
# Class 1: upstream's try/catch is dead here.
# Guards ICPP-PATCH item 5 (discard-before-load, re-stamp-after-save).
# ---------------------------------------------------------------------------
def test__unreadable_prompt_cache_self_heals(network: str) -> None:
    """A prompt cache this build cannot read must be discarded, not trapped on.

    Upstream handles a bad session file by catching and returning false
    (llama_state_load_file). In the canister that catch is a no-op and the throw
    is a trap — so main_.cpp must validate BEFORE calling in, discard the file,
    and start cold.

    We create the unreadable cache by uploading bytes that are not a session
    file. That covers exactly the same code path as the real-world trigger (a
    cache written by a different model, which threw `wrong model arch: 'llama'
    instead of 'qwen3'`), without needing two models uploaded at once.
    """
    _call(
        network,
        "remove_prompt_cache",
        f'(record {{ args = vec {{"--prompt-cache"; "{CACHE_FOREIGN}"}} }})',
    )
    upload = _call(
        network,
        "upload_prompt_cache_chunk",
        f'(record {{ promptcache = "{CACHE_FOREIGN}"; chunk = blob "\\de\\ad\\be\\ef\\01\\02\\03\\04"; '
        f"chunksize = 8 : nat64; offset = 0 : nat64; }})",
    )
    assert "Ok" in upload, upload

    resp = _run(network, CACHE_FOREIGN, "Joe loves writing stories", "1")
    _assert_no_trap(resp, "run_update on an unreadable prompt cache")
    assert "(variant { Ok" in resp, (
        "An unreadable prompt cache was not self-healed. The "
        "prompt_cache_discard_if_stale() call before llama_state_load_file in "
        f"src/main_.cpp was most likely lost.\n{resp}"
    )
    assert _cached(resp) == 0, f"expected a cold start after discarding: {resp}"


def test__discarded_prompt_cache_is_restamped(network: str, principal: str) -> None:
    """After a discard, the fresh cache must be re-stamped and then REUSED.

    Without the prompt_cache_write_format_stamp() call after
    llama_state_save_file, the cache written by the previous test would be
    unstamped, so every later call would discard it again — a permanent cold
    start that silently destroys prompt-cache performance without ever failing.

    Asserted two ways, so this test stands on its own rather than only failing
    as a knock-on from the previous one:
      (a) STRUCTURAL — the ".icppfmt" sidecar exists, i.e. the stamp was written;
      (b) BEHAVIOURAL — the next call actually reuses the cache (cached > 0).
    """
    # (a) the stamp file itself must have been (re)written next to the cache
    sidecar = f".canister_cache/{principal}/sessions/{CACHE_FOREIGN}.icppfmt"
    stamp = _call(
        network, "filesystem_file_size", f'(record {{filename = "{sidecar}"}})'
    )
    assert "exists = true" in stamp, (
        f"No prompt-cache format stamp at {sidecar}. The "
        "prompt_cache_write_format_stamp() call after llama_state_save_file in "
        f"src/main_.cpp was most likely lost, so this cache will be discarded "
        f"again on every call — a permanent cold start.\n{stamp}"
    )

    # (b) and the cache must therefore actually be reused on the next call
    resp = _run(network, CACHE_FOREIGN, "Joe loves writing stories", "1")
    _assert_no_trap(resp, "second run_update on the re-created prompt cache")
    assert "(variant { Ok" in resp, resp
    assert _cached(resp) > 0, (
        "The prompt cache was discarded a SECOND time, so it is never being "
        "re-stamped. The prompt_cache_write_format_stamp() call after "
        f"llama_state_save_file in src/main_.cpp was most likely lost.\n{resp}"
    )


# ---------------------------------------------------------------------------
# copy_prompt_cache must carry the format/model stamp with the bytes.
#
# These assert on the INGESTED-TOKEN COUNT, not just "it still works": a cache
# that is silently discarded still returns correct output, it just re-ingests
# the whole conversation. That is a pure performance regression with no error
# anywhere — at max_tokens_update=4 a restored 256-token conversation costs ~64
# extra run_update round-trips — so only a token-count assertion catches it.
# ---------------------------------------------------------------------------
def test__copy_restore_keeps_the_cache_warm(network: str) -> None:
    """save -> remove -> restore must NOT re-ingest the conversation.

    The documented save/restore flow (README "Prompt Caching"):
        copy live -> save ; remove live ; copy save -> live
    The stamp describes the BYTES, so it has to travel with them. Without that,
    the restored cache is unstamped, gets discarded on first use, and every
    restore silently pays a full re-ingestion.
    """
    for cache in (CACHE_LIVE, CACHE_SAVE):
        _call(
            network,
            "remove_prompt_cache",
            f'(record {{ args = vec {{"--prompt-cache"; "{cache}"}} }})',
        )
    assert "Ok" in _call(
        network,
        "new_chat",
        f'(record {{ args = vec {{"--prompt-cache"; "{CACHE_LIVE}"}} }})',
    )

    _ingest_fully(network, CACHE_LIVE, WARM_PROMPT)
    cached_before = _warm_reading(network, CACHE_LIVE, WARM_PROMPT)
    assert cached_before > 0, "the cache never went warm to begin with"

    # save, drop the live cache, restore over the same name
    assert "Ok" in _copy_cache(network, CACHE_LIVE, CACHE_SAVE)
    assert "Ok" in _call(
        network,
        "remove_prompt_cache",
        f'(record {{ args = vec {{"--prompt-cache"; "{CACHE_LIVE}"}} }})',
    )
    assert "Ok" in _copy_cache(network, CACHE_SAVE, CACHE_LIVE)
    assert "Ok" in _call(
        network,
        "new_chat",
        f'(record {{ args = vec {{"--prompt-cache"; "{CACHE_LIVE}"}} }})',
    )

    cached_after = _warm_reading(network, CACHE_LIVE, WARM_PROMPT)
    assert cached_after == cached_before, (
        f"Restoring the cache changed the number of cached prompt tokens "
        f"({cached_before} -> {cached_after}), i.e. the conversation had to be "
        f"re-ingested. copy_prompt_cache is not carrying the .icppfmt stamp "
        f"(prompt_cache_copy_stamp in src/promptcache.cpp)."
    )


def test__copy_to_a_fresh_name_keeps_the_cache_warm(network: str) -> None:
    """Branching a conversation to a NEVER-USED name must also stay warm.

    copy_prompt_cache has never carried the stamp, in any version. Whether that
    showed up depended on the flow:

      copy -> new_chat -> run_update : cold since v0.12.0 (when stamping was
          added in the b10076 upgrade). new_chat has always discarded an
          unstamped cache, and a fresh destination has no stamp — so a branch
          was ALWAYS re-ingested. This test uses that flow.
      copy -> run_update (no new_chat) : warm until v0.16.1, cold from v0.16.2,
          which added the same discard to run_update.

    Copying to a name that was used before could still come out warm by
    accident, because the previous cache's orphaned stamp was left behind and
    vouched for the new bytes. v0.16.3 stops relying on that accident.
    """
    _call(
        network,
        "remove_prompt_cache",
        f'(record {{ args = vec {{"--prompt-cache"; "{CACHE_BRANCH}"}} }})',
    )
    cached_before = _warm_reading(network, CACHE_LIVE, WARM_PROMPT)

    assert "Ok" in _copy_cache(network, CACHE_LIVE, CACHE_BRANCH)
    assert "Ok" in _call(
        network,
        "new_chat",
        f'(record {{ args = vec {{"--prompt-cache"; "{CACHE_BRANCH}"}} }})',
    )

    cached_branch = _warm_reading(network, CACHE_BRANCH, WARM_PROMPT)
    assert cached_branch == cached_before, (
        f"A conversation branched to a fresh cache name re-ingested its prompt "
        f"({cached_before} -> {cached_branch} cached tokens). The branch is not "
        f"inheriting the .icppfmt stamp from the cache it was copied from."
    )
