"""Smoke tests for run_decision (decision models, System One).

Runs against the decision model that scripts/qa_deploy_and_pytest.py uploaded as
models/model.gguf. DECISION_MODEL selects which one it is, and so which budget,
request and reference answers apply (see MODELS):

    DECISION_MODEL=julia-1   (default)  ggml-org/Julia-1-GGUF  Julia-1-Q8_0.gguf
    DECISION_MODEL=laya                 ggml-org/Laya-GGUF     Laya-Q8_0.gguf
    DECISION_MODEL=kev-0.8b             ggml-org/Kev-0.8B-GGUF Kev-0.8B-Q8_0.gguf

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
import time
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

# A state of 84 tokens for Kev-0.8B: it is ingested over several calls
KEV_STATE = (
    "Hi, I ordered a pair of running shoes (order 88213) ten days ago. The tracking "
    "page has said 'label created' since the 3rd and nothing has moved. I need them "
    "for a race on Saturday. Can you tell me where the parcel actually is, or should "
    "I just cancel and buy somewhere else? Honestly a bit disappointed, this is my "
    "third order with you."
)
# A new message appended to KEV_STATE: a grown state continues from the stored one
KEV_STATE_UPDATE = "\nUpdate: the parcel arrived today, all good now."
KEV_QUESTIONS = {
    **{k: v for k, v in LAYA_QUESTIONS.items() if k != "frustration"},
    "mood": {
        "type": "score",
        "instructions": "Mood?",
        "criteria": ["calm", "annoyed", "angry"],
    },
    "team": {
        "type": "choice",
        "instructions": "Which team should handle this?",
        "criteria": {
            "billing": None,
            "shipping": None,
            "technical": None,
            "sales": None,
        },
    },
}

LOAD_ARGS = '"--no-warmup"; "-c"; "2048"; "-b"; "2048"; "-ub"; "2048"'

# Per model: the load_model args, the per-call token budget (max_tokens_update), the tolerance on
# probabilities, and the llama-server (CPU) reference answers for the request.
# Reference format: question id -> {option key -> probability}; noul -> {"true": p}
MODELS: Dict[str, Dict[str, Any]] = {
    # Julia-1 at Q8_0 is sensitive to kernel rounding: llama-server itself gives
    # different answers on CPU and Metal for some questions (frustration: CPU
    # "mildly annoyed", Metal "annoyed"). So no tolerance on its probabilities,
    # only the clear decisions (reference top probability >= CLEAR) must match.
    "julia-1": {
        "load_args": LOAD_ARGS,
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
        "load_args": LOAD_ARGS,
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
    # Kev-0.8B is causal: the state is ingested once, over several calls, into a
    # per-caller state file, and each question then costs only its own tokens
    # (~1.45 B instructions per token with -fa off and an f32 KV cache). A budget
    # of 24 (a multiple of the 8-token ingestion step) makes the test need
    # several calls; the canister also stops each call before the IC limit.
    "kev-0.8b": {
        "load_args": '"--no-warmup"; "-c"; "4096"; "-b"; "64"; "-ub"; "64"; "-fa"; "off"; "--cache-type-k"; "f32"; "--cache-type-v"; "f32"',
        "budget": 24,
        "tolerance": 0.05,
        "state": KEV_STATE,
        "questions": KEV_QUESTIONS,
        "reference": {
            "intent": {
                "refund": 0.04,
                "cancel": 0.276,
                "track": 0.2589,
                "other": 0.4251,
            },
            "urgent": {"true": 0.5081},
            "mood": {"calm": 0.1882, "annoyed": 0.5703, "angry": 0.2415},
            "team": {
                "billing": 0.0907,
                "shipping": 0.6321,
                "technical": 0.1158,
                "sales": 0.1614,
            },
        },
    },
}

DecisionKind = Types.Variant(
    {"choice": Types.Null, "score": Types.Null, "noul": Types.Null}
)
DECISION_INPUT_FIELDS = {
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
DecisionInput = Types.Record(DECISION_INPUT_FIELDS)
# with a scope (kev); DecisionInput without one checks that a missing opt field
# decodes as null, as it does for clients built before scopes existed
DecisionInputScoped = Types.Record(
    {**DECISION_INPUT_FIELDS, "scope": Types.Opt(Types.Text)}
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
                "state_tokens": Types.Opt(Types.Nat64),
                "state_tokens_remaining": Types.Opt(Types.Nat64),
                "state_key": Types.Opt(Types.Text),
                "instructions": Types.Opt(Types.Nat64),
            }
        ),
        "Err": ApiError,
    }
)


def model() -> Dict[str, Any]:
    return MODELS[os.environ.get("DECISION_MODEL", "julia-1")]


def is_kev() -> bool:
    return os.environ.get("DECISION_MODEL", "julia-1").startswith("kev")


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
    network: str,
    request: Dict[str, Any],
    identity: Optional[str] = None,
    method: str = "run_decision",
) -> Dict[str, Any]:
    """Calls run_decision (or `method`); returns {"Ok": ...} or {"Err": ...}."""
    reply = call_canister_api(
        icp_yaml_path=ICP_YAML_PATH,
        canister_name=CANISTER_NAME,
        canister_method=method,
        canister_argument=encode(
            [
                {
                    "type": (
                        DecisionInputScoped if "scope" in request else DecisionInput
                    ),
                    "value": request,
                }
            ]
        ).hex(),
        canister_input="hex",
        canister_output="hex",
        network=network,
        identity=identity,
        # run_decision_full answers the whole request, over many messages
        timeout_seconds=300 if method == "run_decision_full" else None,
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
        # every Ok reports the work done, storing calls included
        assert len(r["Ok"]["instructions"]) == 1 and r["Ok"]["instructions"][0] > 0, r
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
        f'(record {{ args = vec {{"--model"; "models/model.gguf"; {model()["load_args"]}}} }})',
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
    remaining = [r["state_tokens_remaining"] for r in replies]
    if is_kev():
        # the state is ingested over several calls, then stored
        print(f"state_tokens_remaining per call: {remaining}")
        assert len(set(r["state_tokens"][0] for r in replies)) == 1
        assert remaining[0][0] > 0 and remaining[-1] == [0], remaining
        counts = [x[0] for x in remaining]
        assert counts == sorted(counts, reverse=True), counts
    else:
        assert all(x == [] for x in remaining), remaining

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


def test__kev_reuses_the_stored_state(network: str) -> None:
    """A new request about the same state does not ingest it again."""
    if not is_kev():
        return
    m = model()
    set_budget(network, m["budget"])
    request = flat(m["state"], {"intent": m["questions"]["intent"]})
    r = decide(network, request)
    assert "Ok" in r, r
    ok = r["Ok"]
    assert ok["state_tokens_remaining"] == [0], ok
    assert ok["pending"] == [] and len(ok["answers"]) == 1, ok
    # only the question's own tokens were evaluated
    assert 0 < ok["input_tokens"] < ok["state_tokens"][0], ok


def test__kev_grown_state_continues_from_the_stored_one(network: str) -> None:
    """A state with new data appended ingests only the new tokens."""
    if not is_kev():
        return
    m = model()
    set_budget(network, m["budget"])
    request = flat(m["state"] + KEV_STATE_UPDATE, {"intent": m["questions"]["intent"]})
    replies = decide_all(network, request)
    final = replies[-1]
    assert [a["id"] for a in final["answers"]] == ["intent"], final
    n_state = final["state_tokens"][0]
    n_evaluated = sum(r["input_tokens"] for r in replies)
    print(f"\ngrown state of {n_state} tokens, {n_evaluated} tokens evaluated")
    # only the appended tokens and the question, not the whole state again
    assert n_evaluated < n_state, (n_evaluated, n_state)


# A short state (~50 tokens, more than a question), stored in one call
KEV_SHORT_STATE = (
    "Order 1 was sent ten days ago. The tracking page has not changed since, "
    "and the customer asks where the parcel is and when it will arrive."
)
NAT64_MAX = 2**64 - 1


def short_request(scope: Optional[str] = None) -> Dict[str, Any]:
    request = flat(KEV_SHORT_STATE, {"intent": model()["questions"]["intent"]})
    if scope is not None:
        request["scope"] = [scope]
    return request


def ingested(replies: List[Dict[str, Any]]) -> bool:
    """Whether the calls of a request stored the state (rather than reusing a
    stored one). Storing it can take more than one call: each call stops
    before the IC's instruction limit."""
    n_evaluated = sum(r["input_tokens"] for r in replies)
    return bool(n_evaluated > replies[-1]["state_tokens"][0])


def status(response: str) -> bool:
    return "status_code = 200" in response


def set_cleanup_ttl(network: str, seconds: int) -> None:
    response = call(
        network,
        "set_cache_cleanup_config",
        f"(record {{ ttl_seconds = opt ({seconds} : nat64) }})",
    )
    assert "Ok" in response, response


def test__kev_scopes_keep_separate_states(network: str) -> None:
    """A caller (e.g. a gateway) keeps one set of stored states per scope."""
    if not is_kev():
        return
    set_budget(network, 0)
    r = decide(network, short_request("../x"))
    assert r == {
        "Err": {"Other": "scope: '../x' must be 1-63 characters of a-z, 0-9 and -"}
    }, r
    for scope in ("scope-a", "scope-b"):
        assert status(call(network, "remove_decision_scope", f'("{scope}")'))

    replies_a = decide_all(network, short_request("scope-a"))
    replies_b = decide_all(network, short_request("scope-b"))
    # the same state, stored once per scope
    assert ingested(replies_a) and ingested(replies_b), (replies_a, replies_b)
    a, b = replies_a[-1], replies_b[-1]
    assert a["state_key"] == b["state_key"] and len(a["state_key"]) == 1, (a, b)
    assert a["probabilities"] == b["probabilities"]
    # stored: scope-a answers from its stored state
    again = decide_all(network, short_request("scope-a"))
    assert not ingested(again), again


def test__kev_pinned_state_survives_the_cleanup(network: str) -> None:
    if not is_kev():
        return
    set_budget(network, 0)
    key = decide_all(network, short_request("scope-a"))[-1]["state_key"][0]
    response = call(
        network,
        "pin_decision_state",
        f'(record {{ scope = "scope-a"; state_key = "{key}"; until_ns = {NAT64_MAX} : nat64 }})',
    )
    assert status(response), response
    response = call(
        network,
        "pin_decision_state",
        f'(record {{ scope = "scope-a"; state_key = "0"; until_ns = {NAT64_MAX} : nat64 }})',
    )
    assert "no stored state 0 in scope scope-a" in response, response

    # a cleanup with TTL 0 removes everything but the pinned state
    set_cleanup_ttl(network, 0)
    assert "Ok" in call(network, "cache_cleanup_now", "()")
    assert not ingested(decide_all(network, short_request("scope-a")))
    assert ingested(decide_all(network, short_request("scope-b")))

    response = call(
        network,
        "unpin_decision_state",
        f'(record {{ scope = "scope-a"; state_key = "{key}" }})',
    )
    assert status(response), response
    assert "Ok" in call(network, "cache_cleanup_now", "()")
    set_cleanup_ttl(network, 6 * 3600)
    assert ingested(decide_all(network, short_request("scope-a")))


def test__kev_remove_decision_scope(network: str) -> None:
    if not is_kev():
        return
    set_budget(network, 0)
    decide_all(network, short_request("scope-a"))
    assert status(call(network, "remove_decision_scope", '("scope-a")'))
    assert ingested(decide_all(network, short_request("scope-a")))
    for scope in ("scope-a", "scope-b"):
        assert status(call(network, "remove_decision_scope", f'("{scope}")'))


def test__reset_budget(network: str) -> None:
    set_budget(network, 0)


def test__run_decision_full_equals_loop(network: str) -> None:
    """One run_decision_full call answers what the run_decision loop answers.

    A model whose single forward pass does not fit a message after a yield
    (Laya) gets an error that points to run_decision, not a trap.
    """
    m = model()
    set_budget(network, 0 if is_kev() else m["budget"])
    request = flat(m["state"], m["questions"])
    request["scope"] = [f"loop{int(time.time())}"]
    looped = decide_all(network, request)[-1]

    request["scope"] = [f"full{int(time.time())}"]
    r = decide(network, request, method="run_decision_full")
    if os.environ.get("DECISION_MODEL", "julia-1") == "laya":
        # Laya must get the error, and only Laya: any other model must succeed
        assert "Err" in r, r
        assert "run_decision" in str(r["Err"]) and "Reject" not in str(r["Err"]), r
        return
    assert "Ok" in r, r
    ok = r["Ok"]
    assert ok["pending"] == [], ok
    assert probabilities(ok) == probabilities(looped)
    assert [a["choice"] for a in ok["answers"]] == [
        a["choice"] for a in looped["answers"]
    ]
    # the whole call: at least the instructions of its last step
    assert len(ok["instructions"]) == 1 and ok["instructions"][0] > 0, ok


def test__no_budget_never_traps(network: str) -> None:
    """max_tokens_update = 0 (no cap): each call stops before the instruction
    limit, so the loop and run_decision_full answer what the budgeted loop does.
    A laya pass too large for one message is an Err once the model's rate is
    measured; the first pass after load_model is not checked (it can trap).
    """
    m = model()
    request = flat(m["state"], m["questions"])
    # the request without a cap goes first: a trap there must not be missed
    set_budget(network, 0)
    request["scope"] = [f"nocap{int(time.time())}"]
    looped = decide_all(network, request)[-1]

    set_budget(network, 0 if is_kev() else m["budget"])
    request["scope"] = [f"budget{int(time.time())}"]
    budgeted = decide_all(network, request)[-1]
    assert probabilities(looped) == probabilities(budgeted)

    set_budget(network, 0)

    request["scope"] = [f"nocapfull{int(time.time())}"]
    r = decide(network, request, method="run_decision_full")
    if os.environ.get("DECISION_MODEL", "julia-1") == "laya":
        # the first step answers a question; the callback after the yield
        # cannot fit the next one
        assert "Err" in r and "Reject" not in str(r["Err"]), r
        assert "needs more instructions than a step after a yield has" in str(
            r["Err"]
        ), r
        # a single pass known to need more than one update call is an error,
        # not a trap (the rate is measured by the calls above)
        long_q = {"intent": dict(m["questions"]["intent"])}
        long_q["intent"]["instructions"] = " ".join(["Read the message carefully."] * 12)
        r = decide(network, flat(m["state"], long_q))
        assert "Err" in r and "more than one update call has" in str(r["Err"]), r
        return
    assert "Ok" in r, r
    assert probabilities(r["Ok"]) == probabilities(budgeted)
