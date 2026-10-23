"""Qwen3-0.6B-Q8_0 smoke tests — multi-turn, non-thinking on-chain inference.

Qwen3 is a hybrid *thinking* model. This canister runs it in NON-thinking mode by
ending the assistant turn with an empty `<think>\\n\\n</think>\\n\\n` block (the
official chat-template behaviour for `enable_thinking=false`). These tests assert
the theme behaviours with ROBUST (non-exact-token) checks: the generated output is
coherent and contains no `<think>`/`</think>` tokens, and a multi-turn conversation
recalls facts from earlier turns.

Deploy + upload the model as `models/model.gguf`, then:
$ pytest -vv --network local --identity "$(icp identity default)" test/test_qwen3.py

Notes:
- Requires the tuned config: the canister's wasm_memory_limit is raised to 3.75 GiB
  (via `icp canister settings update`), and load_model uses --ctx-size 16384
  --batch-size 64 --ubatch-size 64 --cache-type-k q8_0 --cache-type-v q8_0. The small
  batch shrinks the compute buffers so the KV cache is the only thing scaling with
  context; ctx 16384 sits at ~1.76 GiB heap (~2 GiB headroom under the 3.75 GiB limit).
"""

# pylint: disable=missing-function-docstring, line-too-long

import inspect
import re
import threading
from pathlib import Path

from .candid_compat import call_canister_api, norm

ICP_YAML_PATH = Path(__file__).parent / "../icp.yaml"
CANISTER_NAME = "llama_cpp"

PRINT_RESPONSE = True


def current_func_name() -> str:
    return inspect.stack()[1].function


# Shared KV-cache args (dual q8_0). Reused by new_chat / run_update.
_CACHE = '"--cache-type-k"; "q8_0"; "--cache-type-v"; "q8_0"'

# Non-thinking prompts. `\\n` in this Python source is the two literal characters
# backslash-n, which Candid then decodes to a newline. The assistant turn ends with
# an empty <think></think> block => enable_thinking=false.
PROMPT_INTRO = (
    "<|im_start|>user\\ngive me a short introduction to LLMs.<|im_end|>\\n"
    "<|im_start|>assistant\\n<think>\\n\\n</think>\\n\\n"
)
PROMPT_MULTITURN = (
    "<|im_start|>user\\nMy name is Sam and I have 3 cats named Milo, Coco, and Ziggy.<|im_end|>\\n"
    "<|im_start|>assistant\\nNice to meet you, Sam! You have three cats: Milo, Coco, and Ziggy.<|im_end|>\\n"
    "<|im_start|>user\\nHow many cats do I have, what are their names, and what is my name?<|im_end|>\\n"
    "<|im_start|>assistant\\n<think>\\n\\n</think>\\n\\n"
)


def _call(
    network: str, method: str, arg: str, timeout_seconds: int | None = None
) -> str:
    """Calls a canister method; a full call needs a longer client timeout."""
    response = call_canister_api(
        icp_yaml_path=ICP_YAML_PATH,
        canister_name=CANISTER_NAME,
        canister_method=method,
        canister_argument=arg,
        network=network,
        timeout_seconds=timeout_seconds,
    )
    if PRINT_RESPONSE:
        print(f"{inspect.stack()[1].function}: {method}: {response}")
    return response


def _assert_ok(response: str, context: str) -> None:
    """The reply must be a decodable Candid Ok record.

    NOT `assert "Ok" in response`: when the strict Candid decoder rejects a reply,
    call_canister_api returns a multi-KB "Failed call to api ..." string that echoes
    the whole command back - and that string itself contains "Ok", so the substring
    check passes on exactly the failure it is meant to catch. Assert the shape.
    """
    assert response.startswith("(variant { Ok"), f"{context}: {response[:400]}"


def _field(response: str, name: str) -> str:
    """Extract a `name = "..."` string field from a Candid response (handles escapes)."""
    match = re.search(rf'{name} = "((?:[^"\\]|\\.)*)"', response)
    return match.group(1) if match else ""


def _run_update(network: str, prompt: str, n: str) -> str:
    arg = (
        '(record { args = vec {"--prompt-cache"; "prompt.cache"; "--prompt-cache-all"; '
        f'{_CACHE}; "--temp"; "0.6"; "-sp"; "-p"; "{prompt}"; "-n"; "{n}"}} }})'
    )
    return _call(network, "run_update", arg)


def _ingest(network: str, prompt: str) -> str:
    """Prefill the prompt (-n 1) until prompt_remaining is empty.

    Returns the output: the token the last call samples starts the response.
    """
    out = ""
    for i in range(25):
        resp = _run_update(network, prompt, "1")
        _assert_ok(resp, f"ingest call {i}")
        out += _field(resp, "output")
        if 'prompt_remaining = ""' in resp:
            return out
    raise AssertionError("prefill did not complete within 25 calls")


def _generate(network: str, max_calls: int = 25) -> str:
    """Generate (empty prompt) to EOG, returning the concatenated generated output."""
    out = ""
    for i in range(max_calls):
        resp = _run_update(network, "", "512")
        _assert_ok(resp, f"generate call {i}")
        out += _field(resp, "output")
        if "generated_eog = true" in resp:
            break
    return out


# --------------------------------------------------------------------------------
def test__load_model(network: str) -> None:
    response = _call(
        network,
        "load_model",
        '(record { args = vec {"--model"; "models/model.gguf"; '
        f'{_CACHE}; "--batch-size"; "64"; "--ubatch-size"; "64"; '
        '"--ctx-size"; "16384"} })',
    )
    assert "(variant { Ok" in response


def test__set_max_tokens(network: str) -> None:
    response = _call(
        network,
        "set_max_tokens",
        "(record { max_tokens_query = 1 : nat64; max_tokens_update = 20 : nat64 })",
    )
    assert "(variant { Ok" in response


def test__ready(network: str) -> None:
    response = _call(network, "ready", "()")
    assert response == norm("(variant { Ok = record { status_code = 200 : nat16;} })")


def test__generate_no_think(network: str) -> None:
    """enable_thinking=false must yield a coherent answer with NO <think> tokens."""
    _assert_ok(
        _call(
            network,
            "new_chat",
            f'(record {{ args = vec {{"--prompt-cache"; "prompt.cache"; {_CACHE}}} }})',
        ),
        "new_chat",
    )
    output = _ingest(network, PROMPT_INTRO)
    output += _generate(network)

    assert output.strip(), "no tokens generated"
    assert "<think>" not in output, f"unexpected <think> in output: {output!r}"
    assert "</think>" not in output, f"unexpected </think> in output: {output!r}"


def test__multi_turn_recall(network: str) -> None:
    """A follow-up turn must recall facts from the earlier turn (non-thinking)."""
    _assert_ok(
        _call(
            network,
            "new_chat",
            f'(record {{ args = vec {{"--prompt-cache"; "prompt.cache"; {_CACHE}}} }})',
        ),
        "new_chat",
    )
    answer = _ingest(network, PROMPT_MULTITURN)
    answer += _generate(network)

    assert answer.strip(), "no tokens generated"
    assert "<think>" not in answer, f"unexpected <think> in answer: {answer!r}"
    # Recall from turn 1: the user's name and the cats.
    assert "Sam" in answer, f"did not recall the name: {answer!r}"
    assert any(
        name in answer for name in ("Milo", "Coco", "Ziggy")
    ), f"did not recall the cat names: {answer!r}"


# --------------------------------------------------------------------------------
def _greedy_raw(network: str, cache: str, prompt: str, n: str) -> str:
    """One greedy (temp 0) run_update on the given prompt cache, Ok or Err."""
    arg = (
        f'(record {{ args = vec {{"--prompt-cache"; "{cache}"; "--prompt-cache-all"; '
        f'{_CACHE}; "--samplers"; "temperature"; "--temp"; "0.0"; "-sp"; '
        f'"-p"; "{prompt}"; "-n"; "{n}"}} }})'
    )
    return _call(network, "run_update", arg)


def _greedy(network: str, cache: str, prompt: str, n: str) -> str:
    """One greedy (temp 0) run_update on the given prompt cache."""
    response = _greedy_raw(network, cache, prompt, n)
    _assert_ok(response, f"run_update {cache}")
    return response


def _start(network: str, cache: str) -> None:
    """Start the prompt cache from scratch: remove it, then new_chat."""
    for method in ("remove_prompt_cache", "new_chat"):
        _call(
            network,
            method,
            f'(record {{ args = vec {{"--prompt-cache"; "{cache}"; {_CACHE}}} }})',
        )


def test__alternating_sessions_equal_solo(network: str) -> None:
    """Two prompt caches alternating run_update calls equal their solo runs.

    The model context is shared, and a session file holds no logits. Every
    generate call continues its session without decoding first, so it must
    sample from the session's own logits (the .logits sidecar), not from the
    other session's last decode.
    """
    prompts = {"alt-a": PROMPT_INTRO, "alt-b": PROMPT_MULTITURN}
    n_generate_calls = 3

    def turns(cache: str, prompt: str) -> list:
        """The calls of one session: ingest with -n 1, then generate."""
        calls = [(cache, prompt, "1")] * 25
        calls += [(cache, "", "512")] * n_generate_calls
        return calls

    def run(calls: list) -> dict:
        """Run the calls in order; return the last conversation per cache."""
        conversations: dict = {}
        ingested: set = set()
        for cache, prompt, n in calls:
            if prompt and cache in ingested:
                continue
            response = _greedy(network, cache, prompt, n)
            if prompt and 'prompt_remaining = ""' in response:
                ingested.add(cache)
            conversations[cache] = _field(response, "conversation")
        return conversations

    solo = {}
    for name, prompt in prompts.items():
        _start(network, f"{name}-solo.cache")
        solo[name] = run(turns(f"{name}-solo.cache", prompt))[f"{name}-solo.cache"]

    for name in prompts:
        _start(network, f"{name}.cache")
    a_calls = turns("alt-a.cache", prompts["alt-a"])
    b_calls = turns("alt-b.cache", prompts["alt-b"])
    interleaved = [call for pair in zip(a_calls, b_calls, strict=True) for call in pair]
    alternating = run(interleaved)

    assert solo["alt-a"] != solo["alt-b"]
    assert alternating["alt-a.cache"] == solo["alt-a"]
    assert alternating["alt-b.cache"] == solo["alt-b"]


def _set_max_tokens(network: str, query: int, update: int) -> None:
    """Sets the max_tokens of run_query and run_update; 0 means no cap."""
    response = _call(
        network,
        "set_max_tokens",
        f"(record {{ max_tokens_query = {query} : nat64; max_tokens_update = {update} : nat64 }})",
    )
    assert "(variant { Ok" in response


def test__instruction_bounded_chunks(network: str) -> None:
    """With no max_tokens cap, run_update stops on instructions instead of trapping.

    Batched prompt decoding costs ~1.2B instructions per token, so this prompt
    of ~110 tokens is far beyond the 40B limit of one call. run_update must
    split it over several calls at its 36B guard, and every prompt token the
    guard holds back must come back in a later call: the conversation ends up
    holding the whole prompt.
    """
    sentence = "The quick brown fox jumps over the lazy dog near the river bank. "
    n_sentences = 8
    prompt = (
        "<|im_start|>user\\nSummarize this text in one sentence: "
        + sentence * n_sentences
        + "<|im_end|>\\n<|im_start|>assistant\\n<think>\\n\\n</think>\\n\\n"
    )
    cache = "instructions.cache"
    _start(network, cache)
    _set_max_tokens(network, 0, 0)
    try:
        ingest_calls = 0
        response = ""
        while ingest_calls < 10:
            ingest_calls += 1
            response = _greedy(network, cache, prompt, "1")
            if 'prompt_remaining = ""' in response:
                break
        assert 'prompt_remaining = ""' in response, "the prompt was not ingested"
        assert ingest_calls > 1, "the instruction guard did not split the prompt"
        conversation = _field(response, "conversation")
        assert conversation.count(sentence.strip()) == n_sentences, conversation
        # The last ingest call also decoded the token -n 1 sampled: its output
        output = _field(response, "output")
        assert output and conversation.endswith(output), response
        prompt_part = conversation[: -len(output)]
        assert prompt_part.rstrip("\\n").endswith("</think>"), conversation

        generated_eog = False
        for _ in range(10):
            response = _greedy(network, cache, "", "512")
            assert "n_tokens_generated = opt (0 : nat64)" not in response, response
            if "generated_eog = true" in response:
                generated_eog = True
                break
        assert generated_eog, "the generation did not end within 10 calls"
    finally:
        _set_max_tokens(network, 1, 20)


def _full(network: str, cache: str, prompt: str, n: str = "-1") -> str:
    """One greedy (temp 0) run_update_full on the given prompt cache."""
    arg = (
        f'(record {{ args = vec {{"--prompt-cache"; "{cache}"; "--prompt-cache-all"; '
        f'{_CACHE}; "--samplers"; "temperature"; "--temp"; "0.0"; "-sp"; '
        f'"-p"; "{prompt}"; "-n"; "{n}"}} }})'
    )
    # One call generates the whole response, over many messages
    return _call(network, "run_update_full", arg, timeout_seconds=300)


def _loop(network: str, cache: str, prompt: str) -> str:
    """The multi-call run_update loop: ingest with -n 1, then generate to eog.

    The output of every call is part of the response, also of the last ingest
    call: the token -n 1 samples there is its first token.
    """
    output = ""
    for _ in range(25):
        response = _greedy(network, cache, prompt, "1")
        output += _field(response, "output")
        if 'prompt_remaining = ""' in response:
            break
    for _ in range(25):
        response = _greedy(network, cache, "", "512")
        output += _field(response, "output")
        if "generated_eog = true" in response:
            return output
    raise AssertionError("the loop did not reach eog within 25 calls")


def test__run_update_full_equals_loop(network: str) -> None:
    """One run_update_full call returns what the run_update loop returns.

    Both use the same chunk size (max_tokens_update 20), so greedy decoding
    sees the same batches.
    """
    _start(network, "full-loop.cache")
    looped = _loop(network, "full-loop.cache", PROMPT_INTRO)
    _start(network, "full-one.cache")
    response = _full(network, "full-one.cache", PROMPT_INTRO)
    _assert_ok(response, "run_update_full")
    assert "generated_eog = true" in response
    assert "partial = opt false" in response
    assert _field(response, "output") == looped


def test__run_update_full_concurrent(network: str) -> None:
    """Three run_update_full calls at once each equal their solo run.

    Their chunks interleave on the shared model context; each chunk reloads
    its own session file.
    """
    prompts = {"cc-a": PROMPT_INTRO, "cc-b": PROMPT_MULTITURN, "cc-c": PROMPT_INTRO}
    solo = {}
    for name, prompt in prompts.items():
        _start(network, f"{name}-solo.cache")
        solo[name] = _field(_full(network, f"{name}-solo.cache", prompt), "output")
        _start(network, f"{name}.cache")

    responses: dict = {}

    def call(name: str) -> None:
        """Stores the reply of a full call on its own cache."""
        responses[name] = _full(network, f"{name}.cache", prompts[name])

    threads = [threading.Thread(target=call, args=(name,)) for name in prompts]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    # A call that raised in its thread stored nothing.
    assert set(responses) == set(prompts), sorted(responses)
    for name, response in responses.items():
        _assert_ok(response, f"concurrent {name}")
        assert _field(response, "output") == solo[name], name


def test__run_update_full_busy_session(network: str) -> None:
    """While a run_update_full holds a session, run_update on it gets a 409."""
    cache = "busy.cache"
    _start(network, cache)
    responses: dict = {}
    holder = threading.Thread(
        target=lambda: responses.update(full=_full(network, cache, PROMPT_INTRO))
    )
    holder.start()
    # Poll until the holder takes the session. -n 0 decodes nothing, so a probe
    # that comes before the holder leaves the session as it is.
    seen_409 = False
    while holder.is_alive() and not seen_409:
        seen_409 = "status_code = 409" in _greedy_raw(network, cache, "", "0")
    holder.join()
    assert seen_409, "run_update on the held session never got a 409"
    _assert_ok(responses["full"], "the holder")
    assert "status_code = 200" in _greedy_raw(network, cache, "", "1")
