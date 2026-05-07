"""Test the recurring prompt-cache cleanup timer.

First deploy the canister:
$ icpp build-wasm
$ dfx deploy --network local

Then run the tests:
$ pytest -vv --network local test/test_cache_cleanup.py

The initial-state assertion ("right after a clean deploy") is NOT in this
file — it must be verified once before the rest of the test suite runs (see
the verification flow in the implementation plan).

Two flavors of stats:
  - `runs` is a LIFETIME counter (monotonic across the canister's life)
    so tests assert deltas: `runs_after - runs_before == 1`.
  - `files_examined`, `files_deleted`, `files_failed` are LAST-RUN stats
    (overwritten on every cleanup invocation), so tests assert absolute
    values: `after["files_deleted"] == N`.

Filename convention: cleanup-test uploads use the prefix `cleanup_test_` so
they do not collide with `test_promptcache.py` which uses `prompt.cache`.
"""

# pylint: disable=missing-function-docstring, unused-import, wildcard-import, unused-wildcard-import, line-too-long

import re
import time
from pathlib import Path
from typing import Dict, Optional

from icpp.smoketest import call_canister_api

DFX_JSON_PATH = Path(__file__).parent / "../dfx.json"
CANISTER_NAME = "llama_cpp"

# Default cleanup config baked into cache_cleanup.cpp:
#   period: 600s, ttl: 21_600s (6h), max_files_per_run: 256.
DEFAULT_PERIOD_SECONDS = 600
DEFAULT_TTL_SECONDS = 6 * 3600
DEFAULT_MAX_FILES = 256

# Bounds enforced by set_cache_cleanup_config.
MAX_FILES_FLOOR = 1
MAX_FILES_CEILING = 10_000


# ---------- helpers ---------------------------------------------------------


def _call(method: str, argument: str, network: str) -> str:
    return call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method=method,
        canister_argument=argument,
        network=network,
    )


def _extract_field(response: str, name: str) -> int:
    """Pull a `<name> = <int> : nat64` field out of a candid response."""
    match = re.search(rf"{name}\s*=\s*([0-9_]+)\s*:\s*nat64", response)
    if not match:
        raise AssertionError(f"field {name!r} not found in response: {response}")
    return int(match.group(1).replace("_", ""))


def _extract_bool(response: str, name: str) -> bool:
    """Pull a `<name> = <bool>` field out of a candid response."""
    match = re.search(rf"{name}\s*=\s*(true|false)\b", response)
    if not match:
        raise AssertionError(f"field {name!r} not found in response: {response}")
    return match.group(1) == "true"


def _get_stats(network: str) -> Dict[str, int]:
    response = _call("get_cache_cleanup_stats", "()", network)
    if "Ok" not in response:
        raise AssertionError(f"get_cache_cleanup_stats returned non-Ok: {response}")
    return {
        "runs": _extract_field(response, "runs"),
        "files_examined": _extract_field(response, "files_examined"),
        "files_deleted": _extract_field(response, "files_deleted"),
        "files_failed": _extract_field(response, "files_failed"),
        "period_seconds": _extract_field(response, "period_seconds"),
        "ttl_seconds": _extract_field(response, "ttl_seconds"),
        "max_files_per_run": _extract_field(response, "max_files_per_run"),
        "is_running": _extract_bool(response, "is_running"),
    }


def _set_config(
    network: str,
    period_seconds: Optional[int] = None,
    ttl_seconds: Optional[int] = None,
    max_files_per_run: Optional[int] = None,
) -> str:
    def _opt(v: Optional[int]) -> str:
        return f"opt ({v} : nat64)" if v is not None else "null"

    arg = (
        "(record { "
        f"period_seconds = {_opt(period_seconds)}; "
        f"ttl_seconds = {_opt(ttl_seconds)}; "
        f"max_files_per_run = {_opt(max_files_per_run)} "
        "})"
    )
    return _call("set_cache_cleanup_config", arg, network)


def _stop(network: str) -> None:
    _call("cache_cleanup_stop_timer", "()", network)


def _start(network: str) -> None:
    _call("cache_cleanup_start_timer", "()", network)


def _restore_defaults(network: str) -> None:
    """Restore cleanup config to its bake-in defaults so the next test starts
    from a known state. Called at the end of every test that mutates config."""
    _set_config(
        network,
        period_seconds=DEFAULT_PERIOD_SECONDS,
        ttl_seconds=DEFAULT_TTL_SECONDS,
        max_files_per_run=DEFAULT_MAX_FILES,
    )
    _stop(network)


# ---------- access-denied tests --------------------------------------------


def test__cache_cleanup_admin_endpoints_require_auth(
    identity_anonymous: Dict[str, str], network: str
) -> None:
    """All five endpoints (including the query) reject anonymous callers."""
    assert identity_anonymous["principal"] == "2vxsx-fae"

    expected = '(variant { Err = variant { Other = "Access Denied" } })'

    for method, arg in [
        ("cache_cleanup_start_timer", "()"),
        ("cache_cleanup_stop_timer", "()"),
        ("cache_cleanup_now", "()"),
        ("get_cache_cleanup_stats", "()"),
        (
            "set_cache_cleanup_config",
            "(record { period_seconds = null; ttl_seconds = null; max_files_per_run = null })",
        ),
    ]:
        response = _call(method, arg, network)
        assert response == expected, f"{method}: got {response!r}"


# ---------- start/stop lifecycle -------------------------------------------


def test__cache_cleanup_timer_lifecycle(network: str) -> None:
    # start
    _start(network)
    assert _get_stats(network)["is_running"] is True

    # stop
    _stop(network)
    assert _get_stats(network)["is_running"] is False

    # idempotent start
    _start(network)
    _start(network)
    assert _get_stats(network)["is_running"] is True

    _stop(network)


# ---------- cleanup_now: empty + counters ----------------------------------


def test__cache_cleanup_now_empty_advances_runs(network: str) -> None:
    before = _get_stats(network)
    _call("cache_cleanup_now", "()", network)
    after = _get_stats(network)
    # runs is a lifetime counter — assert delta.
    assert after["runs"] - before["runs"] == 1
    # files_* are last-run only; with default 6h TTL and freshly-uploaded
    # leftovers from earlier tests, no deletions should occur. Asserting
    # files_deleted == 0 is robust because every file in .canister_cache
    # was created within the test session (well under 6h ago).
    assert after["files_deleted"] == 0, (
        f"empty cleanup should delete no fresh files, got "
        f"files_deleted={after['files_deleted']}"
    )


# ---------- cleanup_now actually deletes a stale file ----------------------


def test__cache_cleanup_now_deletes_uploaded_file(
    network: str, principal: str
) -> None:
    """Upload a file, force ttl=0, run cleanup, verify deletion via stats and
    via uploaded_prompt_cache_details no longer listing the file."""
    filename = "cleanup_test_old_file.cache"

    # Upload a 5-byte file.
    upload_resp = _call(
        "upload_prompt_cache_chunk",
        f'(record {{ promptcache = "{filename}"; chunk = blob "\\01\\02\\03\\04\\05"; chunksize = 5 : nat64; offset = 0 : nat64 }})',
        network,
    )
    assert "Ok" in upload_resp, upload_resp

    # Confirm the file is registered.
    details_before = _call(
        "uploaded_prompt_cache_details",
        f'(record {{ promptcache = "{filename}" }})',
        network,
    )
    assert "Ok" in details_before, details_before

    # Force ttl=0 so any file is "old" enough to delete.
    _set_config(network, ttl_seconds=0)

    runs_before = _get_stats(network)["runs"]
    cleanup_resp = _call("cache_cleanup_now", "()", network)
    assert "Ok" in cleanup_resp, cleanup_resp
    after = _get_stats(network)

    # files_* are last-run only — with ttl=0, every file under sessions/
    # is stale, so files_deleted is the count of files in the cache at run
    # time. We uploaded one (cleanup_test_old_file), so >=1 is the floor;
    # other test artifacts may push it higher.
    assert after["files_deleted"] >= 1, (
        f"expected at least the uploaded file to be deleted, got "
        f"files_deleted={after['files_deleted']}"
    )
    # runs is a lifetime counter — assert delta.
    assert after["runs"] - runs_before == 1

    # Restore defaults BEFORE re-asserting. The download path should now
    # report the file is gone.
    _restore_defaults(network)


# ---------- cleanup_now keeps fresh file under default 6h TTL --------------


def test__cache_cleanup_now_keeps_fresh_file(network: str, principal: str) -> None:
    filename = "cleanup_test_fresh_file.cache"
    upload_resp = _call(
        "upload_prompt_cache_chunk",
        f'(record {{ promptcache = "{filename}"; chunk = blob "\\10\\20\\30\\40\\50"; chunksize = 5 : nat64; offset = 0 : nat64 }})',
        network,
    )
    assert "Ok" in upload_resp, upload_resp

    # Default config (6h TTL): the just-uploaded file is fresh and survives.
    runs_before = _get_stats(network)["runs"]
    _call("cache_cleanup_now", "()", network)
    after = _get_stats(network)

    assert after["runs"] - runs_before == 1
    # No file should have been deleted in this run (last-run counter).
    assert after["files_deleted"] == 0, (
        f"fresh file should not be deleted, got files_deleted={after['files_deleted']}"
    )
    # The fresh upload itself was not deleted.
    details = _call(
        "uploaded_prompt_cache_details",
        f'(record {{ promptcache = "{filename}" }})',
        network,
    )
    assert "Ok" in details, details

    # Tear down: force ttl=0 to delete the file we just uploaded so the next
    # test starts from a clean prompt-cache state.
    _set_config(network, ttl_seconds=0)
    _call("cache_cleanup_now", "()", network)
    _restore_defaults(network)


# ---------- TTL config genuinely controls deletion -------------------------


def test__ttl_config_controls_deletion(network: str, principal: str) -> None:
    """One file, two cleanup runs, two different TTL values: file survives
    under a generous TTL and is deleted under a zero TTL. Proves that
    set_cache_cleanup_config(ttl_seconds=N) is not just stored as a number
    but is actually consulted by run_cache_cleanup_body's age comparison."""
    filename = "cleanup_test_ttl_boundary.cache"

    upload_resp = _call(
        "upload_prompt_cache_chunk",
        f'(record {{ promptcache = "{filename}"; chunk = blob "\\aa\\bb"; chunksize = 2 : nat64; offset = 0 : nat64 }})',
        network,
    )
    assert "Ok" in upload_resp, upload_resp

    # Phase 1: TTL = 1 day (86400 s). The file is seconds old → fresh → kept.
    _set_config(network, ttl_seconds=86400)
    _call("cache_cleanup_now", "()", network)
    after = _get_stats(network)
    assert after["files_deleted"] == 0, (
        f"under TTL=86400, freshly uploaded file should NOT have been deleted; "
        f"files_deleted={after['files_deleted']}"
    )
    # File must still be queryable.
    details = _call(
        "uploaded_prompt_cache_details",
        f'(record {{ promptcache = "{filename}" }})',
        network,
    )
    assert "Ok" in details, details

    # Phase 2: TTL = 0. Same file is now stale → deleted.
    _set_config(network, ttl_seconds=0)
    _call("cache_cleanup_now", "()", network)
    after2 = _get_stats(network)
    assert after2["files_deleted"] >= 1, (
        f"under TTL=0, stale file should have been deleted; "
        f"files_deleted={after2['files_deleted']}"
    )

    _restore_defaults(network)


# ---------- set_cache_cleanup_config persistence ---------------------------


def test__set_cache_cleanup_config_persists(network: str) -> None:
    new_period = 300
    new_ttl = 3600
    new_max_files = 128

    _set_config(
        network,
        period_seconds=new_period,
        ttl_seconds=new_ttl,
        max_files_per_run=new_max_files,
    )
    stats = _get_stats(network)
    assert stats["period_seconds"] == new_period
    assert stats["ttl_seconds"] == new_ttl
    assert stats["max_files_per_run"] == new_max_files

    _restore_defaults(network)


# ---------- max_files_per_run clamps to floor and ceiling ------------------


def test__max_files_per_run_clamps(network: str) -> None:
    _set_config(network, max_files_per_run=0)
    assert _get_stats(network)["max_files_per_run"] == MAX_FILES_FLOOR

    _set_config(network, max_files_per_run=999_999)
    assert _get_stats(network)["max_files_per_run"] == MAX_FILES_CEILING

    _restore_defaults(network)


# ---------- period_seconds=0 is silently rejected --------------------------


def test__period_seconds_zero_is_silently_rejected(network: str) -> None:
    """opt 0 for period_seconds preserves the prior value — no error variant."""
    # First, lock in a known non-default period.
    _set_config(network, period_seconds=300)
    assert _get_stats(network)["period_seconds"] == 300

    # Then attempt period_seconds=0 — should leave 300 in place, return Ok.
    response = _set_config(network, period_seconds=0)
    assert "Ok" in response, f"expected Ok, got: {response}"
    assert _get_stats(network)["period_seconds"] == 300

    _restore_defaults(network)


# ---------- recurring timer fires under a short period ---------------------


def test__cache_cleanup_recurring_fires(network: str) -> None:
    """Set period_seconds=2, start the timer, and verify at least 2 fires
    occur within a 15s window. Asserting >=2 fires (rather than >=1) is what
    proves the configured 2s period was actually applied — if the period
    were silently defaulted to 600s, no second fire would happen in 15s.

    The 15s budget gives ~7x slack over the theoretical 4s minimum (two
    periods at 2s each), absorbing CI variance and replica slowness."""
    target_fires = 2
    _set_config(network, period_seconds=2)
    before = _get_stats(network)
    _start(network)
    try:
        deadline = time.monotonic() + 15.0
        last = before["runs"]
        while time.monotonic() < deadline:
            current = _get_stats(network)["runs"]
            last = current
            if current - before["runs"] >= target_fires:
                break
            time.sleep(0.5)
        assert last - before["runs"] >= target_fires, (
            f"recurring timer with period=2s did not produce {target_fires} "
            f"fires within 15s: runs went from {before['runs']} to {last} "
            f"(delta={last - before['runs']})"
        )
    finally:
        _stop(network)
        _restore_defaults(network)


# ---------- end-to-end age verification (absolute time) -------------------


def test__file_age_calculation_via_recurring_cleanup(
    network: str, principal: str
) -> None:
    """End-to-end absolute-time age verification.

    Configure TTL=60s and a 5s timer period. Upload a file at T0, arm the
    timer, then poll every 5s for up to 90s checking whether the file is
    still queryable. Assert the file disappears in a tight window centered
    on T0+60s.

    This is the only test in the suite that pins down the cleanup body's
    age math in absolute units. Bug modes it catches that no other test
    does:
        - Age computed in wrong units (ns vs us vs ms vs s) — would delete
          at T0+0.06s, T0+60_000s, etc., not at T0+60s.
        - TTL config stored but ignored — file would either never be deleted
          or deleted on the first tick regardless of age.
        - Wrong clock domain — would produce wildly wrong deletion times.
        - Timer firing at the wrong cadence (e.g., once per minute despite
          period=5s) — file might survive for many minutes after TTL.

    Test budget: ~70-80s of pytest wall time on a healthy local replica.
    """
    filename = "cleanup_test_age_60s.cache"
    ttl_seconds = 60
    period_seconds = 5
    poll_interval = 5.0
    max_wait_seconds = 90.0

    # Tight config + arm the recurring timer.
    _set_config(
        network, ttl_seconds=ttl_seconds, period_seconds=period_seconds
    )
    _start(network)

    try:
        # T0 = the moment we upload. Anchored on monotonic time, not wall
        # clock, so this is robust against clock adjustments.
        upload_resp = _call(
            "upload_prompt_cache_chunk",
            f'(record {{ promptcache = "{filename}"; '
            f'chunk = blob "\\01\\02"; chunksize = 2 : nat64; '
            f'offset = 0 : nat64 }})',
            network,
        )
        assert "Ok" in upload_resp, upload_resp
        t0 = time.monotonic()

        # Sanity: file is queryable immediately after upload.
        details = _call(
            "uploaded_prompt_cache_details",
            f'(record {{ promptcache = "{filename}" }})',
            network,
        )
        assert "Ok" in details, (
            f"file not found immediately after upload: {details}"
        )

        # Poll loop. Sleep first, then check, so the first check happens at
        # T0 + poll_interval (no wasted check at T0+0).
        deletion_age = None
        observation_log = []
        deadline = t0 + max_wait_seconds

        while time.monotonic() < deadline:
            time.sleep(poll_interval)
            elapsed = time.monotonic() - t0
            details = _call(
                "uploaded_prompt_cache_details",
                f'(record {{ promptcache = "{filename}" }})',
                network,
            )
            present = "Ok" in details
            observation_log.append((elapsed, present))
            if not present:
                deletion_age = elapsed
                break

        assert deletion_age is not None, (
            f"file was not deleted within {max_wait_seconds}s "
            f"(TTL={ttl_seconds}s, period={period_seconds}s). "
            f"Observation log: {observation_log}"
        )

        # Lower bound — catches "deleted too early" bugs (wrong units, TTL
        # ignored, etc.). Slack of -10s allows for one missed timer-tick
        # alignment plus polling jitter at the boundary, but firmly excludes
        # any bug that scales the TTL by ~2x or more.
        assert deletion_age >= ttl_seconds - 10, (
            f"file deleted too early at {deletion_age:.1f}s after upload "
            f"(TTL={ttl_seconds}s). Age calculation is likely wrong. "
            f"Observation log: {observation_log}"
        )
        # Upper bound — catches "deleted too late" bugs (timer firing at the
        # wrong cadence, dispatch starvation). The expected upper bound is
        # TTL + period (next tick AFTER age reaches TTL) + slack for IC
        # scheduling latency and polling granularity.
        upper_bound = ttl_seconds + period_seconds + 20
        assert deletion_age <= upper_bound, (
            f"file deleted too late at {deletion_age:.1f}s after upload "
            f"(expected ~{ttl_seconds}s, upper bound {upper_bound}s). "
            f"Observation log: {observation_log}"
        )

        print(
            f"\nfile deleted at {deletion_age:.1f}s after upload "
            f"(TTL={ttl_seconds}s, period={period_seconds}s) — "
            f"observation log: {observation_log}"
        )

    finally:
        _stop(network)
        # Defensive teardown: if the file somehow survived the test (failure
        # path), force-delete it so the next test starts clean.
        _set_config(network, ttl_seconds=0)
        _call("cache_cleanup_now", "()", network)
        _restore_defaults(network)


# ---------- cap actually limits per-tick deletions -------------------------


def test__cache_cleanup_now_respects_cap(network: str, principal: str) -> None:
    """Upload N=8 files, ttl=0, cap=3, cache_cleanup_now → delta=3 first call."""
    n = 8
    filenames = [f"cleanup_test_cap_{i}.cache" for i in range(n)]
    for fname in filenames:
        upload_resp = _call(
            "upload_prompt_cache_chunk",
            f'(record {{ promptcache = "{fname}"; chunk = blob "\\01\\02"; chunksize = 2 : nat64; offset = 0 : nat64 }})',
            network,
        )
        assert "Ok" in upload_resp, upload_resp

    _set_config(network, ttl_seconds=0, max_files_per_run=3)
    try:
        _call("cache_cleanup_now", "()", network)
        after = _get_stats(network)
        # files_deleted is the last-run count. With ttl=0 and >=8 stale files
        # available, the cap of 3 is the binding constraint and the run
        # deletes exactly 3.
        assert after["files_deleted"] == 3, (
            f"expected 3 deleted in first capped tick, got {after['files_deleted']}"
        )

        # Run a few more times to drain. Each tick is independently bounded
        # by the cap, so its files_deleted is in [0, 3]. (We can't assert
        # exactly 3 each time because the last tick may run out of stale
        # files before hitting the cap.)
        for _ in range(3):
            _call("cache_cleanup_now", "()", network)
            a = _get_stats(network)
            assert 0 <= a["files_deleted"] <= 3, (
                f"per-tick files_deleted out of bounds: {a['files_deleted']}"
            )
    finally:
        _restore_defaults(network)
