"""Smoke tests for run_classify and run_classify_query (sequence classifiers).

Runs against the classifier that scripts/qa_deploy_and_pytest.py uploaded as
models/model.gguf: onicai/dao-moderation-classifier in f32, a ModernBERT classifier
with 15 labels that helps moderate a DAO's chat channel. See docs/README-classifier-models.md.

The reference scores come from upstream llama.cpp, with the f16 GGUF:
  llama-embedding -m <gguf> --pooling rank -p "<text>" --embd-normalize -1
The canister must match them within TOL per score.

Replies are decoded with icp-py-core (icp_candid), an encoder independent of
icpp-pro.

Run:
$ pytest -vv --network local --identity llama-cpp-testing test/test_classify.py
"""

# pylint: disable=missing-function-docstring, line-too-long

import math
from pathlib import Path
from typing import Any, Dict, List, Optional

import pytest
from icp_candid import Types, decode, encode

from .candid_compat import call_canister_api

ICP_YAML_PATH = Path(__file__).parent / "../icp.yaml"
CANISTER_NAME = "llama_cpp"
TOL = 0.05

# The documented f32 arguments: -ub 192 keeps the longest text under the 5B
# instructions of a query (docs/README-classifier-models.md)
LOAD_ARGS = '"--embedding"; "--pooling"; "rank"; "--no-warmup"; "-c"; "192"; "-ub"; "192"'

REFERENCE = {
    "how long is the min dissolve delay to vote with my neuron? new here 🙏": [
        4.811, 1.1, 1.718, 0.549, 2.663, 3.253, -0.994, -6.576,
        -5.15, -6.264, 3.78, 4.253, 0.197, -3.436, -3.588,
    ],
    "lol imagine staking for 8 years in a coin run by clowns 🤡": [
        -2.715, 0.466, -3.664, 4.776, 0.016, -2.278, 0.731, -2.751,
        -7.644, -4.131, -2.92, -1.452, 1.862, -1.758, -1.314,
    ],
    "Hi, I'm from the team support desk. DM me your issue and I'll fix your stuck stake directly 🙏": [
        0.851, -1.182, -3.551, -1.102, 10.701, 4.523, -3.043, -1.164,
        6.606, 4.956, -9.664, -4.282, -6.804, 2.308, 6.073,
    ],
}
LABELS = [
    "kind:question", "kind:discussion", "kind:support", "kind:troll", "kind:spam",
    "toxicity:0", "toxicity:1", "toxicity:2",
    "scam", "misleading", "respectful",
    "action:none", "action:warn", "action:delete", "action:block",
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
        assert all(math.isfinite(x) for x in ok["scores"]), ok["scores"]
        max_diff = max(abs(a - b) for a, b in zip(ok["scores"], reference, strict=True))
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
    r = classify(network, "the proposal " * 100)
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
