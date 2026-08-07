"""Stage-1 quality gate: the word-guessing game, run with native llama.cpp.

Qwen3-1.7B is the only model that has reliably passed this. Any candidate that
wants to replace it must pass here FIRST, on the laptop, before anyone spends
hours uploading a gguf to a canister. See TMP-benchmark-LLM-models.md.

Uses `llama-server` + its /completion endpoint, NOT /v1/chat/completions:
  * the model is loaded ONCE for all words (llama-cli would reload it per word)
  * /completion takes a RAW prompt, so no chat template is applied - which is
    what the canister does too (LLAMA_ICP_NO_CHAT_TEMPLATES). Whatever string
    passes here is exactly the string an app must send to `run_update`.

Usage:
  # Qwen3-1.7B, the reference that must pass
  python scripts/quality_gate_word_game.py \
      --model models/Qwen/Qwen3-1.7B-GGUF/Qwen3-1.7B-Q4_K_M.gguf --format qwen3

  # verify a new candidate's prompt format before judging its quality
  python scripts/quality_gate_word_game.py --model <gguf> --format lfm2 --smoke
"""

import argparse
import json
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request
from typing import Any, Dict, List, Tuple

WORDS = [
    "DOG",
    "PIANO",
    "VOLCANO",
    "LIBRARY",
    "HONEY",
    "BRIDGE",
    "WINTER",
    "MIRROR",
    "ELEPHANT",
    "CLOCK",
]

# The 10 words above are a quick screen. Ranking decisions are made on all 20
# (see --full): a model can look perfect on the first 10 and still leak on the
# wider list - that is exactly how both Granite variants were eliminated.
WORDS_EXTRA = [
    "OCEAN",
    "GUITAR",
    "PENGUIN",
    "CASTLE",
    "RAINBOW",
    "COFFEE",
    "MOUNTAIN",
    "TELEPHONE",
    "BUTTERFLY",
    "HOSPITAL",
]

SYSTEM = (
    "You support a word-guessing game. When asked for a hint about a word, give "
    "ONE short clue that describes what the thing IS or DOES, using other words, "
    "without writing the word itself.\n"
    "Example:\n"
    "Request: Provide a hint about CAT. Do not mention CAT.\n"
    "Hint: A small furry pet that purrs and loves to chase mice."
)
USER = "Provide a hint about {w}. Do not mention {w}."

# Per-architecture prompt templates, written out literally because the canister
# applies none. Add an entry per candidate and verify it with --smoke first: a
# wrong template reads exactly like a bad model.
FORMATS = {
    # Qwen3 non-thinking: the empty <think></think> block suppresses reasoning.
    "qwen3": (
        "<|im_start|>system\n{system}<|im_end|>\n"
        "<|im_start|>user\n{user}<|im_end|>\n"
        "<|im_start|>assistant\n<think>\n\n</think>\n\n"
    ),
    "chatml": (
        "<|im_start|>system\n{system}<|im_end|>\n"
        "<|im_start|>user\n{user}<|im_end|>\n"
        "<|im_start|>assistant\n"
    ),
    # LFM2/LFM2.5: verified from the gguf's own tokenizer.chat_template - it is
    # ChatML with bos_token prepended. BOS is NOT written here: llama.cpp and the
    # canister both add it from the vocab (llama_vocab_get_add_bos), so writing
    # it would double it.
    "lfm2": (
        "<|im_start|>system\n{system}<|im_end|>\n"
        "<|im_start|>user\n{user}<|im_end|>\n"
        "<|im_start|>assistant\n"
    ),
    # LFM2.5 reasoning models (e.g. 2.6B, 1.2B-Thinking) ALWAYS open a think
    # block: their template ends with "<|im_start|>assistant\n<think>". Prefill a
    # closed, empty one to suppress reasoning - same trick as qwen3 above.
    # Without this the model spends its whole token budget thinking out loud and
    # leaks the target word inside the reasoning.
    "lfm2-nothink": (
        "<|im_start|>system\n{system}<|im_end|>\n"
        "<|im_start|>user\n{user}<|im_end|>\n"
        "<|im_start|>assistant\n<think></think>\n"
    ),
    "granite": (
        "<|start_of_role|>system<|end_of_role|>{system}<|end_of_text|>\n"
        "<|start_of_role|>user<|end_of_role|>{user}<|end_of_text|>\n"
        "<|start_of_role|>assistant<|end_of_role|>"
    ),
    "gemma": (
        "<start_of_turn>user\n{system}\n\n{user}<end_of_turn>\n"
        "<start_of_turn>model\n"
    ),
}

STOPS = [
    "<|im_end|>",
    "<|end_of_text|>",
    "<end_of_turn>",
    "<|endoftext|>",
    "<|im_start|>",
    "<|start_of_role|>",
]


def start_server(
    model: str, port: int, ctx: int, threads: int, extra: List[str]
) -> subprocess.Popen:  # type: ignore[type-arg]
    """Launch llama-server and block until /health answers, returning the proc.

    The process is deliberately NOT a context manager: it must outlive this
    function so every word reuses one loaded model (the whole reason for using
    the server instead of llama-cli).
    """
    cmd = [
        "llama-server",
        "-m",
        model,
        "--port",
        str(port),
        "-c",
        str(ctx),
        "-t",
        str(threads),
        "--no-warmup",
        "-ngl",
        "0",
    ] + extra
    # Deliberately not a context manager - the process must outlive this
    # function; see the docstring.
    # pylint: disable=consider-using-with
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    url = f"http://127.0.0.1:{port}/health"
    for _ in range(600):  # up to ~10 min for a cold load
        if proc.poll() is not None:
            raise RuntimeError("llama-server exited during startup")
        try:
            with urllib.request.urlopen(url, timeout=2) as r:
                if r.status == 200:
                    return proc
        except (urllib.error.URLError, OSError, ValueError):
            # not listening yet / still loading weights - keep polling
            time.sleep(1)
    proc.kill()
    raise RuntimeError("llama-server did not become healthy")


def complete(
    port: int, prompt: str, n_predict: int, seed: int = -1
) -> Tuple[str, Dict[str, Any]]:
    """POST a RAW prompt to /completion (no chat template applied)."""
    body = json.dumps(
        {
            "prompt": prompt,
            "n_predict": n_predict,
            "temperature": 0.5,
            "top_p": 0.8,
            "top_k": 20,
            "repeat_penalty": 1.1,
            "stop": STOPS,
            "cache_prompt": True,
            "seed": seed,
        }
    ).encode()
    req = urllib.request.Request(
        f"http://127.0.0.1:{port}/completion",
        body,
        {"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(req, timeout=900) as r:
        d = json.loads(r.read())
    return d.get("content", ""), d


def leaks(word: str, text: str) -> bool:
    """Did the hint give the word away?

    Stem-PREFIX matching, not an exact-variant list. An earlier version only
    checked {dog, dogs, dogy, ...} and happily passed
    'a fiery mountain formation built by VOLCANIC activity' for VOLCANO - which
    obviously gives the game away. Any output word sharing a long enough prefix
    with the target counts: volcanic/volcano, honeycomb/honey, pianist/piano,
    librarian/library, clockwise/clock.

    Prefix length is max(3, len(word)-2) so short words (DOG) still need a near
    exact match while longer ones catch derivations.
    """
    w = word.lower()
    n = max(3, len(w) - 2)
    pre = w[:n]
    for tok in set(re.findall(r"[a-z]+", text.lower())):
        if tok.startswith(pre) or w.startswith(tok[:n]) and len(tok) >= n:
            return True
    return False


def main() -> int:
    """Run the gate and return 0 only if every sample was leak-free."""
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", required=True)
    ap.add_argument("--format", default="qwen3", choices=sorted(FORMATS))
    ap.add_argument("--words", default=None)
    ap.add_argument(
        "--full",
        action="store_true",
        help="use all 20 words instead of the 10-word screen. Combined with "
        "--repeat 5 this is the 100-sample score models are ranked on.",
    )
    ap.add_argument("--n-predict", type=int, default=64)
    # Threads affect SPEED only, never quality - use the machine.
    ap.add_argument("--threads", type=int, default=8)
    ap.add_argument("--ctx", type=int, default=4096)
    ap.add_argument("--port", type=int, default=8127)
    ap.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="print every sample, not just leaks",
    )
    ap.add_argument(
        "--system",
        default=None,
        help="override the system prompt. The gate uses the README's "
        "prompt verbatim; use this only to probe whether a "
        "failure is prompt-induced rather than a real quality "
        "limit (report both results if so).",
    )
    ap.add_argument(
        "--repeat",
        type=int,
        default=3,
        help="independent samples per word (different seeds). Sampling is "
        "stochastic at temp 0.5, so ONE pass is not a gate - what matters "
        "is the leak RATE over many samples.",
    )
    ap.add_argument(
        "--smoke",
        action="store_true",
        help="one word, dump the raw prompt and full response - use "
        "this to verify a new chat format",
    )
    a, extra = ap.parse_known_args()

    system = a.system if a.system is not None else SYSTEM
    tmpl = FORMATS[a.format]
    default_words = WORDS + WORDS_EXTRA if a.full else WORDS
    spec = a.words if a.words is not None else ",".join(default_words)
    words = [w.strip().upper() for w in spec.split(",") if w.strip()]
    if a.smoke:
        words = words[:1]

    print(f"model  : {a.model}")
    print(f"format : {a.format}")
    print(f"words  : {len(words)}", flush=True)

    t0 = time.time()
    proc = start_server(a.model, a.port, a.ctx, a.threads, extra)
    print(f"server : up in {time.time()-t0:.0f}s\n", flush=True)

    n_leak = n_empty = n_run = 0
    rows = []
    reps = 1 if a.smoke else a.repeat
    try:
        for rep in range(reps):
            seed = 1000 + rep
            for w in words:
                n_run += 1
                prompt = tmpl.format(system=system, user=USER.format(w=w))
                if a.smoke:
                    print("----- RAW PROMPT -----")
                    print(prompt)
                    print("----------------------", flush=True)
                hint, meta = complete(a.port, prompt, a.n_predict, seed)
                hint = hint.strip()
                leaked = leaks(w, hint)
                n_leak += leaked
                n_empty += not hint
                rows.append((w, leaked, hint))
                flag = "LEAK!" if leaked else ("EMPTY" if not hint else "ok   ")
                if leaked or a.smoke or a.verbose:
                    print(f"[{flag}] r{rep} {w:9s} {hint[:100]!r}", flush=True)
                if a.smoke:
                    tim = meta.get("timings", {})
                    print(
                        f"\n  predicted_per_second: {tim.get('predicted_per_second')}"
                    )
                    print(
                        f"  stop reason: {meta.get('stop_type')} "
                        f"tokens: {tim.get('predicted_n')}"
                    )
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=20)
        except subprocess.TimeoutExpired:
            proc.kill()

    n = n_run
    print("\n================ GATE ================")
    print(
        f"  no leak   : {n-n_leak}/{n}   (must be {n}/{n} - a leak is an "
        f"outright reject)"
    )
    print(f"  non-empty : {n-n_empty}/{n}")
    print("  usable / single-clue / no-template-junk: judge by reading above")
    ok = n_leak == 0 and n_empty == 0
    print(f"  verdict   : {'PASS (objective checks)' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
