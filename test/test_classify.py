"""Smoke tests for run_classify and run_classify_query (sequence classifiers).

Runs against the classifier that scripts/qa_deploy_and_pytest.py uploaded as
models/model.gguf: Ettin-encoder-17M in f32, a ModernBERT classifier with 18 labels
(treasure-guard's guard model). See README-classifier-models.md.

The reference scores come from upstream llama.cpp, with the f16 GGUF:
  llama-embedding -m <gguf> --pooling rank -p "<text>" --embd-normalize -1
The canister must match them within TOL per score.

Replies are decoded with icp-py-core (icp_candid), an encoder independent of
icpp-pro.

Run:
$ pytest -vv --network local --identity llama-cpp-testing test/test_classify.py
"""

# pylint: disable=missing-function-docstring, line-too-long

from pathlib import Path
from typing import Any, Dict, List, Optional

import pytest
from icp_candid import Types, decode, encode

from .candid_compat import call_canister_api

ICP_YAML_PATH = Path(__file__).parent / "../icp.yaml"
CANISTER_NAME = "llama_cpp"
TOL = 0.05

# The documented f32 arguments: -ub 192 keeps the longest text under the 5B
# instructions of a query (README-classifier-models.md)
LOAD_ARGS = '"--embedding"; "--pooling"; "rank"; "--no-warmup"; "-c"; "192"; "-ub"; "192"'

REFERENCE = {
    "I grab a handful of coins from the chest and run!": [
        -1.152, 2.17, -0.543, -0.297, 2.573, -2.099, -0.778, -0.017, 3.489,
        2.168, -1.108, 4.771, 0.893, -1.456, -0.172, -1.96, -3.033, 3.071,
    ],
    "Can I see the treasure? I promise I won't touch it.": [
        -2.871, -1.228, -0.637, 0.41, 6.737, 1.087, -0.409, -1.045, 1.464,
        5.313, -0.636, 1.939, -0.357, -0.271, -1.915, -2.361, -4.674, -1.642,
    ],
    "Why did the canister cross the subnet? To get to the other cycle!": [
        -2.909, -1.658, -3.459, -0.545, -0.008, 1.759, -1.394, -3.363, 1.395,
        2.306, -1.975, 1.888, 0.052, 1.852, -2.769, -5.095, 3.853, -1.82,
    ],
}
LABELS = [
    "intent:gift", "intent:bribe", "intent:threat", "intent:trick",
    "intent:plea", "intent:chat",
    "respect:0", "respect:1", "respect:2", "respect:3", "respect:4",
    "humor:0", "humor:1", "humor:2", "humor:3", "humor:4",
    "icp", "steal",
]  # fmt: skip

ClassifyInput = Types.Record({"text": Types.Text})
ApiError = Types.Variant({"Other": Types.Text, "StatusCode": Types.Nat16})
ClassifyResult = Types.Variant(
    {
        "Ok": Types.Record(
            {
                "scores": Types.Vec(Types.Float64),
                "labels": Types.Vec(Types.Text),
                "input_tokens": Types.Nat64,
                "instructions": Types.Opt(Types.Nat64),
            }
        ),
        "Err": ApiError,
    }
)


def call(network: str, method: str, arg: str) -> str:
    return call_canister_api(
        icp_yaml_path=ICP_YAML_PATH,
        canister_name=CANISTER_NAME,
        canister_method=method,
        canister_argument=arg,
        network=network,
    )


def classify(
    network: str,
    text: str,
    method: str = "run_classify",
    identity: Optional[str] = None,
) -> Dict[str, Any]:
    """Calls run_classify (or `method`); returns {"Ok": ...} or {"Err": ...}."""
    reply = call_canister_api(
        icp_yaml_path=ICP_YAML_PATH,
        canister_name=CANISTER_NAME,
        canister_method=method,
        canister_argument=encode([{"type": ClassifyInput, "value": {"text": text}}]).hex(),
        canister_input="hex",
        canister_output="hex",
        network=network,
        identity=identity,
    )
    reply = reply.strip().strip("()").strip().rstrip(",").strip().strip('"')
    try:
        value: Dict[str, Any] = decode(bytes.fromhex(reply), ClassifyResult)[0]["value"]
        return value
    except ValueError:
        return {"Err": {"Reject": reply}}  # a reject (e.g. a trap) is not hex


def top(scores: List[float]) -> int:
    return max(range(len(scores)), key=lambda i: scores[i])


def test__load_model(network: str) -> None:
    response = call(
        network,
        "load_model",
        f'(record {{ args = vec {{"--model"; "models/model.gguf"; {LOAD_ARGS}}} }})',
    )
    assert "Model succesfully loaded into memory." in response


@pytest.mark.parametrize("method", ["run_classify", "run_classify_query"])
def test__reference_scores(network: str, method: str) -> None:
    for text, reference in REFERENCE.items():
        r = classify(network, text, method)
        assert "Ok" in r, (text, r)
        ok = r["Ok"]
        assert ok["labels"] == LABELS, ok["labels"]
        assert len(ok["scores"]) == len(reference)
        max_diff = max(abs(a - b) for a, b in zip(ok["scores"], reference))
        assert max_diff <= TOL, (text, max_diff, ok["scores"])
        assert top(ok["scores"]) == top(reference), text
        assert ok["input_tokens"] > 2
        assert len(ok["instructions"]) == 1 and ok["instructions"][0] > 0, ok
        print(
            f"{method}: {ok['input_tokens']} tokens, "
            f"{ok['instructions'][0]:_} instructions, max diff {max_diff:.4f}: {text}"
        )


def test__query_equals_update(network: str) -> None:
    text = next(iter(REFERENCE))
    update = classify(network, text)["Ok"]["scores"]
    query = classify(network, text, "run_classify_query")["Ok"]["scores"]
    assert update == query


def test__errors(network: str) -> None:
    assert classify(network, "") == {"Err": {"Other": "text is empty"}}
    assert classify(network, "a" * 8193) == {
        "Err": {"Other": "text is 8193 bytes, more than the limit of 8192"}
    }
    # more tokens than -ub 192: an Err, not a trap
    r = classify(network, "the guard " * 100)
    assert "tokens, more than the 192 of the loaded context" in r["Err"]["Other"], r


def test__access(
    network: str,
    identity_anonymous: Dict[str, str],
    identity_non_controller: Dict[str, str],
) -> None:
    text = next(iter(REFERENCE))
    for who in (identity_anonymous, identity_non_controller):
        for method in ("run_classify", "run_classify_query"):
            r = classify(network, text, method, identity=who["identity"])
            assert r == {"Err": {"Other": "Access Denied"}}, (who, method, r)


def test__no_generation_on_a_classifier(network: str) -> None:
    response = call(
        network,
        "run_update",
        '(record { args = vec {"--prompt-cache"; "classify.cache"; "-p"; "hi"; "-n"; "1"} })',
    )
    assert "The loaded model is a sequence classifier: use run_classify." in response
