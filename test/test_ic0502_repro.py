"""funnAI-controller replay harness for the IC0502 `heap out of bounds` traps.

Drives a locally deployed llama_cpp canister EXACTLY the way the funnAI prd
Judge / ShareService controllers drive their LLM canisters, per
TMP-HANDOVER-IC0502-funnAI-test-harness.md: load the model ONCE, then replay
many conversations against that single persisted llama_context. The golden
prompts below are verbatim from the prd logs of 2026-09-17 (challengeId 41351
and the GameState rotation set) — the exact traffic in flight when the prd
canisters trapped. Whitespace (including trailing spaces) is part of what the
tokenizer and the prompt cache see: keep it byte-exact.

Semantics:
- RED on a pre-fix wasm  = SUCCESS for the repro handover (a local trap).
- GREEN on a fixed wasm  = the regression gate (2N clean cycles for the soak).

Setup (once, outside pytest — see TMP-HANDOVER-IC0502-funnAI-test-harness.md):
  icp network start -d
  icp deploy -e local -y --identity llama-cpp-testing
  python -m scripts.upload --network local --canister llama_cpp \\
      --canister-filename models/model.gguf \\
      models/Qwen/Qwen2.5-0.5B-Instruct-GGUF/qwen2.5-0.5b-instruct-q8_0.gguf

Run (N = conversations; default 5 for the per-commit gate, hundreds for a soak):
  IC0502_REPRO_CYCLES=300 pytest -vv --network local \\
      --identity llama-cpp-testing test/test_ic0502_repro.py

Between harness runs, reset the wasm heap ONLY with `--mode reinstall`;
stop/start and a same-hash upgrade keep the persisted context (and any heap
corruption) alive.
"""

# pylint: disable=missing-function-docstring, line-too-long

import inspect
import os
from pathlib import Path

from .candid_compat import call_canister_api

ICP_YAML_PATH = Path(__file__).parent / "../icp.yaml"
CANISTER_NAME = "llama_cpp"

PRINT_RESPONSE = False

# Number of full controller conversations to replay against the ONE persisted
# context. prd traps recurred within tens of cycles; use hundreds for a soak.
CYCLES = int(os.environ.get("IC0502_REPRO_CYCLES", "5"))

# Markers of a canister TRAP as surfaced when a call is rejected (same set as
# test_upgrade_regressions.py). IC0502 is the prd signature.
TRAP_MARKERS = (
    "IC0502",
    "IC0503",
    "heap out of bounds",
    "unreachable",
    "Canister trapped",
    "UNCAUGHT C++ EXCEPTION",
)

# --------------------------------------------------------------------------------
# Golden prd data (TMP-HANDOVER-IC0502-funnAI-test-harness.md).
# `\\n` in this Python source is the two literal characters backslash-n, which
# the Candid text decoder turns into a newline — the same trick test_qwen3.py
# uses. Trailing spaces inside the strings are verbatim from prd.

# (1) Challenger — generate the challenge.
CHALLENGER_TEMPLATE = (
    "<|im_start|>user\\n"
    "Ask a question that can be answered with common knowledge. Do NOT give the answer. "
    "Ask me a question about {topic}, and start the question with {starts_with}.\\n"
    "<|im_end|>\\n"
    "<|im_start|>assistant\\n"
)

# (2) ShareService / mAIner — answer the challenge.
MAINER_TEMPLATE = (
    "<|im_start|>user\\n"
    "Answer the following question as brief as possible. This is the question: {question}\\n"
    "<|im_end|>\\n"
    "<|im_start|>assistant\\n"
)

# (3) Judge — grade the answer (Main.mo:723-728 concatenation, byte-exact).
JUDGE_TEMPLATE = (
    "<|im_start|>system\\n"
    "You grade answers based on its correctness to the question: \\n"
    " \\n"
    "- {question}\\n"
    "Grade the answer between 1 and 5\\n"
    "1 = completely wrong\\n"
    "2 = mostly wrong\\n"
    "3 = partially correct\\n"
    "4 = mostly correct\\n"
    "5 = completely correct\\n"
    "<|im_end|> \\n"
    "<|im_start|>user\\n"
    "Grade this answer based on its correctness: \\n"
    "- {answer}\\n"
    " \\n"
    "Respond with the grade only, nothing else.\\n"
    "\\n"
    "<|im_end|>\\n"
    "<|im_start|>assistant\\n"
)

# Real prd challenge/answer pairs (the engineering row of the handover table is
# elided ("...") in the source doc, so it is not reproducible byte-exact and is
# left out). Rotated across cycles to vary prompt shapes, as prd does.
GOLDEN = [
    {
        "topic": "history",
        "starts_with": "How",
        "question": "How did the Industrial Revolution in the 18th century impact the lives of common people?",
        "answer": "The Industrial Revolution in the 18th century greatly impacted the lives of common people by increasing the wealth and power of the wealthy, creating new industries, and leading to industrialization.",
        "seed": 114,
    },
    {
        "topic": "technology",
        "starts_with": "When",
        "question": "When did the first digital computer, the ENIAC, become available?",
        "answer": "The first digital computer, the ENIAC, became available in 1946.",
        "seed": 8,
    },
    {
        "topic": "space",
        "starts_with": "Where",
        "question": "Where is the Sun located in our solar system?",
        "answer": "The Sun is located in our solar system in the center.",
        "seed": 89,
    },
]

# Controller loop bounds. prd caps generation at 20 tokens/call
# (set_max_tokens 20/20), so ingest and generate both span many update calls.
MAX_INGEST_CALLS = 60
MAX_GENERATE_CALLS = 3  # mainerMaxContinueLoopCount = 3


def _call(network: str, method: str, arg: str) -> str:
    response = call_canister_api(
        icp_yaml_path=ICP_YAML_PATH,
        canister_name=CANISTER_NAME,
        canister_method=method,
        canister_argument=arg,
        network=network,
    )
    if PRINT_RESPONSE:
        print(f"{inspect.stack()[1].function}: {method}: {response}")
    return response


def _assert_no_trap(response: str, what: str) -> None:
    """A trap IS the reproduction: fail loudly with everything needed to replay it."""
    for marker in TRAP_MARKERS:
        assert marker not in response, (
            f"IC0502 REPRODUCED: {what} trapped (marker {marker!r}).\n{response}"
        )


def _field(response: str, name: str) -> str:
    import re

    match = re.search(rf'{name} = "((?:[^"\\]|\\.)*)"', response)
    return match.group(1) if match else ""


def _cached(response: str) -> int:
    import re

    match = re.search(r"n_prompt_tokens_cached = opt \(([\d_]+) : nat64\)", response)
    return int(match.group(1).replace("_", "")) if match else -1


def _run_update(network: str, cache: str, prompt: str, seed: int, temp: str) -> str:
    # Verbatim prd Judge/ShareService args (Main.mo:1043-1057): note -n 1024;
    # the per-call token count is capped by set_max_tokens (20), as in prd.
    arg = (
        '(record { args = vec {"--prompt-cache"; "'
        + cache
        + '"; "--prompt-cache-all"; "--simple-io"; "--no-display-prompt"; '
        + f'"-n"; "1024"; "--seed"; "{seed}"; "--temp"; "{temp}"; '
        + '"-p"; "'
        + prompt
        + '"} })'
    )
    return _call(network, "run_update", arg)


def _conversation(
    network: str, role: str, cache: str, prompt: str, seed: int, temp: str
) -> str:
    """One full controller conversation: new_chat -> ingest -> generate -> save/remove.

    Returns the concatenated generated output. Any trap fails the test with the
    iteration context (the reproduction record the handover asks for).
    """
    what = f"{role} cache={cache}"

    resp = _call(
        network, "new_chat", f'(record {{ args = vec {{"--prompt-cache"; "{cache}"}} }})'
    )
    _assert_no_trap(resp, f"{what}: new_chat")
    assert "(variant { Ok" in resp, f"{what}: new_chat failed: {resp[:400]}"

    # Ingest: FULL prompt every call, until prompt_remaining == "" (prd step 3).
    last = ""
    for i in range(MAX_INGEST_CALLS):
        resp = _run_update(network, cache, prompt, seed, temp)
        _assert_no_trap(resp, f"{what}: ingest call {i} (last ok: {last[:200]})")
        assert "(variant { Ok" in resp, f"{what}: ingest call {i} failed: {resp[:400]}"
        last = resp
        if _field(resp, "prompt_remaining") == "":
            break
    else:
        raise AssertionError(f"{what}: prompt never fully ingested\n{last[:400]}")

    # Generate: empty prompt until EOG or the controller's continue cap (prd step 4).
    out = _field(last, "output")
    for i in range(MAX_GENERATE_CALLS):
        if "generated_eog = true" in last:
            break
        resp = _run_update(network, cache, "", seed, temp)
        _assert_no_trap(resp, f"{what}: generate call {i} (last ok: {last[:200]})")
        assert "(variant { Ok" in resp, f"{what}: generate call {i} failed: {resp[:400]}"
        last = resp
        out += _field(resp, "output")

    # Save + remove (prd step 5). The save copy is removed too — deviation from
    # prd, purely to keep the local VFS bounded over hundreds of cycles; the
    # copy_prompt_cache code path is still exercised.
    save = cache.replace(".cache", "-save.cache")
    resp = _call(
        network, "copy_prompt_cache", f'(record {{ from = "{cache}"; to = "{save}" }})'
    )
    _assert_no_trap(resp, f"{what}: copy_prompt_cache")
    for name in (cache, save):
        resp = _call(
            network,
            "remove_prompt_cache",
            f'(record {{ args = vec {{"--prompt-cache"; "{name}"}} }})',
        )
        _assert_no_trap(resp, f"{what}: remove_prompt_cache {name}")

    return out


# --------------------------------------------------------------------------------
def test__load_model(network: str) -> None:
    # Exactly the fleet's load args (funnAI/scripts/upgrade_llms.py): no extra
    # flags. n_ctx defaults to 0 = the model's training context (32768), which
    # matches the prd logs. Do NOT reload between conversations.
    response = _call(
        network,
        "load_model",
        '(record { args = vec {"--model"; "models/model.gguf"} })',
    )
    assert "(variant { Ok" in response, response[:400]


def test__set_max_tokens(network: str) -> None:
    # prd caps: 20/20 (funnAI/scripts/upgrade_llms.py).
    response = _call(
        network,
        "set_max_tokens",
        "(record { max_tokens_query = 20 : nat64; max_tokens_update = 20 : nat64 })",
    )
    assert "(variant { Ok" in response, response[:400]


def test__replay_controller_cycles(network: str) -> None:
    """Alternate Judge (deterministic: seed 42, temp 0.0) and ShareService
    (temp 0.8, rotating prd seeds) conversations against the ONE persisted
    context, CYCLES times. Every run_update crosses both IC0502 trap windows
    (the reuse branch on the way in, the teardown on the way out)."""
    for i in range(CYCLES):
        golden = GOLDEN[i % len(GOLDEN)]

        # ShareService / mAIner answers the challenge.
        mainer_prompt = MAINER_TEMPLATE.format(question=golden["question"])
        out = _conversation(
            network,
            role=f"cycle {i} ShareService",
            cache=f"ic0502_{i:04d}_mainer.cache",
            prompt=mainer_prompt,
            seed=golden["seed"],
            temp="0.8",
        )
        assert out.strip(), f"cycle {i}: ShareService generated no tokens"

        # Judge grades the golden answer (fully deterministic).
        judge_prompt = JUDGE_TEMPLATE.format(
            question=golden["question"], answer=golden["answer"]
        )
        out = _conversation(
            network,
            role=f"cycle {i} Judge",
            cache=f"ic0502_{i:04d}_judge.cache",
            prompt=judge_prompt,
            seed=42,
            temp="0.0",
        )
        assert out.strip(), f"cycle {i}: Judge generated no tokens"

        # Challenger-style conversation for coverage (temp 0.7, prd flag set).
        challenger_prompt = CHALLENGER_TEMPLATE.format(
            topic=golden["topic"], starts_with=golden["starts_with"]
        )
        _conversation(
            network,
            role=f"cycle {i} Challenger",
            cache=f"ic0502_{i:04d}_challenger.cache",
            prompt=challenger_prompt,
            seed=golden["seed"],
            temp="0.7",
        )

        print(f"cycle {i + 1}/{CYCLES} clean")


# NOTE: this test RELOADS the model with different context sizes, so it must be
# the LAST test in this file -- the replay test above needs the fleet-config
# model still loaded.
def test__context_layout_change_discards_cache(network: str) -> None:
    """A cache written under one context layout must be DISCARDED (cold start),
    not trapped on, after a re-load_model with a different --ctx-size.

    The session-file byte layout depends on the context (ctx size, cache types,
    ...), not just the model. Before the fix the stamp covered only the model,
    so a re-load with different context flags left the old cache "valid" per the
    stamp; llama_state_load_file then threw on the layout mismatch -- a trap on
    WASI (its catch never runs) -- and every later call re-trapped. This is
    reachable on every canister upgrade: the VFS (caches) persists, statics
    (the loaded model) do not, so the model is re-loaded with whatever flags the
    new deploy uses.
    """
    cache = "ic0502_layout.cache"

    # Load with ctx=8192 and warm a cache under that layout.
    assert "(variant { Ok" in _call(
        network,
        "load_model",
        '(record { args = vec {"--model"; "models/model.gguf"; "--ctx-size"; "8192"} })',
    )
    _call(network, "set_max_tokens",
          "(record { max_tokens_query = 20 : nat64; max_tokens_update = 20 : nat64 })")
    _call(network, "remove_prompt_cache",
          f'(record {{ args = vec {{"--prompt-cache"; "{cache}"}} }})')
    assert "(variant { Ok" in _call(
        network, "new_chat", f'(record {{ args = vec {{"--prompt-cache"; "{cache}"}} }})')
    prompt = MAINER_TEMPLATE.format(question=GOLDEN[0]["question"])
    for _ in range(MAX_INGEST_CALLS):
        resp = _run_update(network, cache, prompt, 42, "0.0")
        _assert_no_trap(resp, "layout test: warm ingest")
        assert "(variant { Ok" in resp, resp
        if _field(resp, "prompt_remaining") == "":
            break

    # Re-load with a DIFFERENT ctx=4096: forces icpp_free_model + a new context
    # whose layout no longer matches the cache written above.
    assert "(variant { Ok" in _call(
        network,
        "load_model",
        '(record { args = vec {"--model"; "models/model.gguf"; "--ctx-size"; "4096"} })',
    )
    _call(network, "set_max_tokens",
          "(record { max_tokens_query = 20 : nat64; max_tokens_update = 20 : nat64 })")

    # run_update on the stale-layout cache must cold-start, never trap.
    resp = _run_update(network, cache, prompt, 42, "0.0")
    _assert_no_trap(resp, "layout test: run on stale-layout cache")
    assert "(variant { Ok" in resp, resp
    assert _cached(resp) == 0, (
        f"expected a cold start after the context layout changed, got "
        f"n_prompt_tokens_cached={_cached(resp)}: the layout mismatch was not "
        f"detected (prompt_cache_layout_id / state_layout_desc).\n{resp}"
    )
