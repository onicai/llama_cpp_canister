"""Smoke tests for run_decision (decision models, System One).

Runs against the decision model that scripts/qa_deploy_and_pytest.py uploaded as
models/model.gguf. DECISION_MODEL selects which one it is, and so which budget,
request and reference answers apply (see MODELS):

    DECISION_MODEL=julia-1   (default)  ggml-org/Julia-1-GGUF  Julia-1-Q8_0.gguf
    DECISION_MODEL=laya                 ggml-org/Laya-GGUF     Laya-Q8_0.gguf

Requests and replies are encoded/decoded with icp-py-core (icp_candid), so the
reply is checked structurally, and by an encoder independent of icpp-pro.

The reference answers come from llama-server (CPU, `-dev none`) built from the
same llama_cpp_onicai_fork commit, POST /v1/systemone with the same request.
Decision models are sensitive to kernel rounding (Julia-1 more than Laya), so:
- a choice question whose reference top probability is >= CLEAR must get the
  same choice;
- every probability must be within the model's tolerance of the reference
  (Laya; Julia-1 has none, see MODELS).

Run (DECISION_MODEL defaults to julia-1):
$ pytest -vv --network local --identity llama-cpp-testing test/test_decision.py
"""

# pylint: disable=missing-function-docstring, line-too-long

import json
import os
from pathlib import Path
from typing import Any, Dict, List, Optional

from icp_candid import Types, decode, encode

from .candid_compat import call_canister_api

ICP_YAML_PATH = Path(__file__).parent / "../icp.yaml"
CANISTER_NAME = "llama_cpp"
CLEAR = 0.6

# The request of llama.cpp PR #29818, in System One form
PR_STATE = {
    "message": "Hi, I was charged twice for my order #4471 and I want a refund.",
    "plan": "pro",
    "order": {"id": 4471, "items": ["phone case", "charger"]},
}
PR_QUESTIONS = {
    "intent": {
        "type": "choice",
        "instructions": "What does the customer want?",
        "criteria": {
            "refund": "wants money back",
            "cancel": "wants to cancel an order",
            "track": "wants to know where an order is",
            "other": "anything else",
        },
    },
    "urgent": {
        "type": "noul",
        "instructions": "Does this need a human within the hour?",
    },
    "frustration": {
        "type": "score",
        "instructions": "How frustrated is the customer?",
        "criteria": ["calm", "mildly annoyed", "annoyed", "angry"],
    },
    "refund": {
        "type": "noul",
        "instructions": "Is a refund requested?",
        "criteria": {"true": "money back is asked", "false": "no money back is asked"},
    },
    "team": {
        "type": "choice",
        "instructions": "Which team?",
        "criteria": {
            k: None
            for k in [
                "billing",
                "shipping",
                "technical",
                "sales",
                "legal",
                "returns",
                "fraud",
                "accounts",
                "retention",
                "other",
            ]
        },
    },
}

# A compact request for Laya (see MODELS)
LAYA_STATE = "Charged twice for order 4471, refund please!"
LAYA_QUESTIONS = {
    "intent": {
        "type": "choice",
        "instructions": "Intent?",
        "criteria": {"refund": None, "cancel": None, "track": None, "other": None},
    },
    "urgent": {
        "type": "noul",
        "instructions": "Urgent?",
        "criteria": {"true": "urgent", "false": "can wait"},
    },
    "frustration": {
        "type": "score",
        "instructions": "Mood?",
        "criteria": ["calm", "angry"],
    },
}

# Per model: the per-call token budget (max_tokens_update), the tolerance on
# probabilities, and the llama-server (CPU) reference answers for the request.
# Reference format: question id -> {option key -> probability}; noul -> {"true": p}
MODELS: Dict[str, Dict[str, Any]] = {
    # Julia-1 at Q8_0 is sensitive to kernel rounding: llama-server itself gives
    # different answers on CPU and Metal for some questions (frustration: CPU
    # "mildly annoyed", Metal "annoyed"). So no tolerance on its probabilities,
    # only the clear decisions (reference top probability >= CLEAR) must match.
    "julia-1": {
        "budget": 200,
        "tolerance": None,
        "state": PR_STATE,
        "questions": PR_QUESTIONS,
        "reference": {
            "intent": {"refund": 0.9712, "cancel": 0.0, "track": 0.0288, "other": 0.0},
            "urgent": {"true": 0.5363},
            "frustration": {
                "calm": 0.0284,
                "mildly annoyed": 0.6255,
                "annoyed": 0.3447,
                "angry": 0.0014,
            },
            "refund": {"true": 0.4639},
            "team": {
                "billing": 0.0089,
                "shipping": 0.0669,
                "technical": 0.0542,
                "sales": 0.3368,
                "legal": 0.0512,
                "returns": 0.0286,
                "fraud": 0.058,
                "accounts": 0.01,
                "retention": 0.0152,
                "other": 0.3701,
            },
        },  # filled in from REFERENCES below
    },
    # Laya evaluates at most 33 tokens per update call (~1.2 B instructions per
    # token), so it gets a compact request: every question fits in 32 tokens.
    "laya": {
        "budget": 32,
        "tolerance": 0.05,
        "state": LAYA_STATE,
        "questions": LAYA_QUESTIONS,
        "reference": {
            "intent": {
                "refund": 0.9617,
                "cancel": 0.0202,
                "track": 0.009,
                "other": 0.0091,
            },
            "urgent": {"true": 0.9064},
            "frustration": {"calm": 0.0092, "angry": 0.9908},
        },
    },
}

DecisionKind = Types.Variant(
    {"choice": Types.Null, "score": Types.Null, "noul": Types.Null}
)
DecisionInput = Types.Record(
    {
        "state": Types.Variant({"Text": Types.Text, "Json": Types.Text}),
        "questions": Types.Vec(
            Types.Record(
                {"id": Types.Text, "kind": DecisionKind, "instructions": Types.Text}
            )
        ),
        "options": Types.Vec(
            Types.Record(
                {
                    "question_id": Types.Text,
                    "key": Types.Text,
                    "description": Types.Text,
                }
            )
        ),
    }
)
ApiError = Types.Variant({"Other": Types.Text, "StatusCode": Types.Nat16})
DecisionResult = Types.Variant(
    {
        "Ok": Types.Record(
            {
                "answers": Types.Vec(
                    Types.Record(
                        {
                            "id": Types.Text,
                            "kind": DecisionKind,
                            "choice": Types.Text,
                            "score": Types.Float64,
                            "yes": Types.Float64,
                            "confidence": Types.Float64,
                        }
                    )
                ),
                "probabilities": Types.Vec(
                    Types.Record(
                        {
                            "question_id": Types.Text,
                            "key": Types.Text,
                            "probability": Types.Float64,
                        }
                    )
                ),
                "input_tokens": Types.Nat64,
                "pending": Types.Vec(Types.Text),
            }
        ),
        "Err": ApiError,
    }
)


def model() -> Dict[str, Any]:
    return MODELS[os.environ.get("DECISION_MODEL", "julia-1")]


def flat(state: Any, questions: Dict[str, Any]) -> Dict[str, Any]:
    """System One request -> the flat run_decision input."""
    qs: List[Dict[str, Any]] = []
    opts: List[Dict[str, str]] = []
    for qid, q in questions.items():
        qs.append(
            {"id": qid, "kind": {q["type"]: None}, "instructions": q["instructions"]}
        )
        criteria = q.get("criteria")
        if q["type"] == "choice":
            for key, description in criteria.items():
                opts.append(
                    {"question_id": qid, "key": key, "description": description or ""}
                )
        elif q["type"] == "score":
            for level in criteria:
                opts.append({"question_id": qid, "key": level, "description": ""})
        elif criteria:
            for key in ("true", "false"):
                if key in criteria:
                    opts.append(
                        {"question_id": qid, "key": key, "description": criteria[key]}
                    )
    state_v = {"Text": state} if isinstance(state, str) else {"Json": json.dumps(state)}
    return {"state": state_v, "questions": qs, "options": opts}


def decide(
    network: str, request: Dict[str, Any], identity: Optional[str] = None
) -> Dict[str, Any]:
    """Calls run_decision; returns {"Ok": ...} or {"Err": ...}."""
    reply = call_canister_api(
        icp_yaml_path=ICP_YAML_PATH,
        canister_name=CANISTER_NAME,
        canister_method="run_decision",
        canister_argument=encode([{"type": DecisionInput, "value": request}]).hex(),
        canister_input="hex",
        canister_output="hex",
        network=network,
        identity=identity,
    )
    reply = reply.strip().strip("()").strip().rstrip(",").strip().strip('"')
    try:
        value: Dict[str, Any] = decode(bytes.fromhex(reply), DecisionResult)[0]["value"]
        return value
    except ValueError:
        return {"Err": {"Reject": reply}}  # a reject (e.g. a trap) is not hex


def decide_all(network: str, request: Dict[str, Any]) -> List[Dict[str, Any]]:
    """Re-sends the request until nothing is pending; returns every Ok reply."""
    replies = []
    for _ in range(20):
        r = decide(network, request)
        assert "Ok" in r, r
        replies.append(r["Ok"])
        if not r["Ok"]["pending"]:
            return replies
    raise AssertionError("the request was not completed in 20 calls")


def call(network: str, method: str, arg: str) -> str:
    return call_canister_api(
        icp_yaml_path=ICP_YAML_PATH,
        canister_name=CANISTER_NAME,
        canister_method=method,
        canister_argument=arg,
        network=network,
    )


def set_budget(network: str, budget: int) -> None:
    response = call(
        network,
        "set_max_tokens",
        f"(record {{ max_tokens_query = {budget} : nat64; max_tokens_update = {budget} : nat64 }})",
    )
    assert "Ok" in response


def probabilities(ok: Dict[str, Any]) -> Dict[str, Dict[str, float]]:
    out: Dict[str, Dict[str, float]] = {}
    for p in ok["probabilities"]:
        out.setdefault(p["question_id"], {})[p["key"]] = p["probability"]
    return out


def test__load_model(network: str) -> None:
    response = call(
        network,
        "load_model",
        '(record { args = vec {"--model"; "models/model.gguf"; "--no-warmup"; "-c"; "2048"; "-b"; "2048"; "-ub"; "2048"} })',
    )
    assert "Model succesfully loaded into memory." in response


def test__access(
    network: str,
    identity_anonymous: Dict[str, str],
    identity_non_controller: Dict[str, str],
) -> None:
    request = flat(model()["state"], model()["questions"])
    for who in (identity_anonymous, identity_non_controller):
        r = decide(network, request, identity=who["identity"])
        assert r == {"Err": {"Other": "Access Denied"}}, (who, r)


def test__no_generation_on_a_decision_model(network: str) -> None:
    response = call(
        network,
        "run_update",
        '(record { args = vec {"--prompt-cache"; "decision.cache"; "-p"; "hi"; "-n"; "1"} })',
    )
    assert "The loaded model is a decision model: use run_decision." in response


def test__validation(network: str) -> None:
    r = decide(network, {"state": {"Text": "hi"}, "questions": [], "options": []})
    assert r == {"Err": {"Other": "questions: must not be empty"}}
    r = decide(
        network,
        {
            "state": {"Json": "{"},
            "questions": [
                {"id": "q", "kind": {"choice": None}, "instructions": "Which?"}
            ],
            "options": [{"question_id": "q", "key": "a", "description": ""}],
        },
    )
    assert r == {"Err": {"Other": "state: Json is not valid JSON"}}


def test__question_over_the_budget(network: str) -> None:
    set_budget(network, 10)
    request = flat(model()["state"], {"intent": model()["questions"]["intent"]})
    r = decide(network, request)
    assert (
        "Err" in r
        and "more than one update call can evaluate (max_tokens_update = 10)"
        in r["Err"]["Other"]
    ), r


def test__decisions_match_llama_server(network: str) -> None:
    m = model()
    set_budget(network, m["budget"])
    request = flat(m["state"], m["questions"])
    replies = decide_all(network, request)
    final = replies[-1]

    # every call stays within the budget; the last one has all answers, in order
    assert all(0 < r["input_tokens"] <= m["budget"] for r in replies), [
        r["input_tokens"] for r in replies
    ]
    assert [a["id"] for a in final["answers"]] == list(m["questions"])
    print(
        f"\n{len(replies)} calls, input_tokens per call: {[r['input_tokens'] for r in replies]}"
    )

    got = probabilities(final)
    for answer in final["answers"]:
        qid = answer["id"]
        ref = m["reference"][qid]
        print(
            f"{qid:12s} canister {json.dumps({k: round(v, 4) for k, v in got[qid].items()})}"
        )
        print(
            f"{'':12s} reference {json.dumps({k: round(v, 4) for k, v in ref.items()})}"
        )
        assert abs(sum(got[qid].values()) - 1.0) < 1e-6
        if m["tolerance"] is not None:
            for key, p in ref.items():
                assert abs(got[qid][key] - p) <= m["tolerance"], (
                    qid,
                    key,
                    got[qid][key],
                    p,
                )
        if "choice" in answer["kind"] and max(ref.values()) >= CLEAR:
            assert answer["choice"] == max(ref, key=ref.get), (
                qid,
                answer["choice"],
                ref,
            )


def test__reset_budget(network: str) -> None:
    set_budget(network, 0)
