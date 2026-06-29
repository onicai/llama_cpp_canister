"""Test the recurring cycle-balance monitor.

First deploy the canister:
$ icpp build-wasm
$ dfx deploy --network local

Then run the tests:
$ pytest -vv --network local test/test_cycle_balance.py

Lifecycle is operator-driven (like the prompt-cache cleanup timer): the timer
is not auto-armed, so right after a clean deploy `get_cycle_balance` returns a
clear "tracking is off" error. Starting the timer refreshes the cached balance
once immediately, so the Ok value is non-zero without waiting an hour.
"""

# pylint: disable=missing-function-docstring, unused-import, line-too-long

import re
from pathlib import Path
from typing import Dict

from icpp.smoketest import call_canister_api

DFX_JSON_PATH = Path(__file__).parent / "../dfx.json"
CANISTER_NAME = "llama_cpp"

TRACKING_OFF_MESSAGE = (
    "cycle balance tracking is off — an admin must call cycle_balance_start_timer"
)


# ---------- helpers ---------------------------------------------------------


def _call(method: str, argument: str, network: str) -> str:
    return call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method=method,
        canister_argument=argument,
        network=network,
    )


def _start(network: str) -> str:
    return _call("cycle_balance_start_timer", "()", network)


def _stop(network: str) -> str:
    return _call("cycle_balance_stop_timer", "()", network)


def _extract_nat(response: str, name: str, candid_type: str) -> int:
    """Pull a `<name> = <int> : <candid_type>` field out of a candid response."""
    match = re.search(rf"{name}\s*=\s*([0-9_]+)\s*:\s*{candid_type}", response)
    if not match:
        raise AssertionError(f"field {name!r} not found in response: {response}")
    return int(match.group(1).replace("_", ""))


# ---------- access-denied test ---------------------------------------------


def test__cycle_balance_endpoints_require_auth(
    identity_anonymous: Dict[str, str], network: str
) -> None:
    """All three endpoints (including the query) reject anonymous callers."""
    assert identity_anonymous["principal"] == "2vxsx-fae"

    expected = '(variant { Err = variant { Other = "Access Denied" } })'

    for method, arg in [
        ("cycle_balance_start_timer", "()"),
        ("cycle_balance_stop_timer", "()"),
        ("get_cycle_balance", "()"),
    ]:
        response = _call(method, arg, network)
        assert response == expected, f"{method}: got {response!r}"


# ---------- tracking-off error (timer not armed) ---------------------------


def test__get_cycle_balance_off_returns_clear_error(network: str) -> None:
    # Guarantee the timer is off regardless of prior tests in this session.
    _stop(network)

    response = _call("get_cycle_balance", "()", network)
    assert "Err" in response, response
    assert TRACKING_OFF_MESSAGE in response, response


# ---------- start arms the timer -------------------------------------------


def test__cycle_balance_start_timer(network: str) -> None:
    response = _start(network)
    assert response == "(variant { Ok = record { status_code = 200 : nat16;} })", response


# ---------- get returns a fresh, non-zero balance --------------------------


def test__get_cycle_balance_ok_after_start(network: str) -> None:
    # start refreshes the cached balance once immediately.
    _start(network)

    response = _call("get_cycle_balance", "()", network)
    assert "Ok" in response, response

    cycle_balance = _extract_nat(response, "cycle_balance", "nat")
    updated_at_ns = _extract_nat(response, "updated_at_ns", "nat64")
    assert cycle_balance > 0, f"expected non-zero cycle_balance, got {cycle_balance}"
    assert updated_at_ns > 0, f"expected non-zero updated_at_ns, got {updated_at_ns}"


# ---------- stop turns tracking back off -----------------------------------


def test__cycle_balance_stop_timer(network: str) -> None:
    response = _stop(network)
    assert response == "(variant { Ok = record { status_code = 200 : nat16;} })", response

    # After stop, the query again reports tracking is off.
    off = _call("get_cycle_balance", "()", network)
    assert TRACKING_OFF_MESSAGE in off, off
