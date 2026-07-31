"""Tool-agnostic Candid comparison helpers for the dfx -> icp-cli migration.

icp-cli's Candid pretty-printer differs from dfx's: it wraps records/vecs over
indented lines, prints the top-level value as ``( <value> , )``, and (unlike dfx)
does not emit a trailing ``;`` before a closing ``}``. The Candid *value* is the
same, so these tests compare a normalized form.

``call_canister_api`` here wraps ``icpp.smoketest.call_canister_api`` and returns
the normalized response, so existing substring assertions such as
``"(variant { Ok" in response`` keep working unchanged. For exact-match
assertions, normalize the expected value too::

    assert response == norm(expected_response)
"""

import re
from icpp.smoketest import call_canister_api as _raw_call_canister_api
from icpp.smoketest import dict_to_candid_text  # re-exported for the tests

__all__ = [
    "call_canister_api",
    "dict_to_candid_text",
    "norm",
    "strip_token_accounting",
]

# The 5 exact token-accounting fields added to the run-success record in v0.15.0.
# They are `opt nat64` and their exact values are covered by the native MockIC
# tests and by test_token_counts.py; the older exact-match smoke tests below
# strip them so they keep asserting only the output text / conversation / eog.
_TOKEN_ACCOUNTING_FIELDS = (
    "n_prompt_tokens_cached",
    "n_prompt_tokens_decoded",
    "n_prompt_tokens_remaining",
    "n_tokens_generated",
    "n_prompt_tokens",
)


def norm(candid_text: str) -> str:
    """Normalize pretty-printed Candid so dfx-style and icp-style output compare equal.

    - collapse all whitespace runs (flattens icp's multi-line wrapping),
    - drop dfx's trailing ``;`` before a closing ``}`` (icp omits it),
    - collapse icp's top-level ``( <value> , )`` back to ``(<value>)``.
    """
    s = re.sub(r"\s+", " ", candid_text).strip()  # flatten wrapping / collapse whitespace
    s = re.sub(r"\s*;\s*}", " }", s)  # `x;}` (dfx) -> `x }` (icp)
    s = re.sub(r"\s*,?\s*\)$", ")", s)  # `( ... , )` / `( ... )` -> top-level `)`
    s = re.sub(r"\(\s+", "(", s)  # `( x` -> `(x`
    s = re.sub(r"\s+\)", ")", s)  # `x )` -> `x)`
    return s


def strip_token_accounting(candid_text: str) -> str:
    """Remove the 5 v0.15.0 `opt nat64` token-accounting fields from a Candid value.

    Order-agnostic: matches each field by name wherever it appears in the record,
    so exact-match assertions written before v0.15.0 keep working regardless of
    where the Candid printer slots the new fields. A no-op on responses that do
    not carry the fields (e.g. new_chat / load_model records).
    """
    s = norm(candid_text)
    for name in _TOKEN_ACCOUNTING_FIELDS:  # longer names first: no prefix clashes
        s = re.sub(rf";?\s*{name}\s*=\s*opt \([\d_]+ : nat64\)", "", s)
    s = re.sub(r"record \{\s*;\s*", "record { ", s)  # leading field removed
    return norm(s)  # re-normalize spacing / trailing `;`


def call_canister_api(*args: object, **kwargs: object) -> str:
    """``icpp.smoketest.call_canister_api`` with the response normalized (see module docstring)."""
    return norm(_raw_call_canister_api(*args, **kwargs))
