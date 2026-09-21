"""Quality gate for the funnAI Judge: is it calibrated, and does qwen3 help?

Companion to scripts/quality_gate_word_game.py, same philosophy: a single sample
is not a gate. The difference is that the JUDGE IS DETERMINISTIC in production
(temp 0.0, seed 42, greedy), so repeats of an identical prompt add nothing -
variation has to come from varying the inputs. --repeat therefore only applies to
the stochastic roles (challenger temp 0.7, mainer temp 0.8).

Two things production does that dominate the results, both reproduced here:

  * the Judge makes exactly ONE generation call (maxContinueLoopCount = 1), and
    the canister clamps it to max_tokens_update. So max_tokens is a HARD CAP on
    the Judge's entire output, not a per-chunk budget.
  * the score is Nat.fromText over the ENTIRE raw output - "3" parses, "3\\n" and
    " 3" do not and are stored as 0.

Backends produce the same numbers because the Judge is greedy:
  --backend native    llama-server built from src/llama_cpp_onicai_fork (fast)
  --backend canister  the deployed llama_cpp canister (slow, authoritative)
  --backend both      run native, then re-run a subset on the canister and
                      ASSERT the generated text matches byte for byte

Usage:
  # verify the prompt renders correctly before spending time on a batch
  python scripts/quality_gate_judge.py --backend native \
      --model models/Qwen/Qwen2.5-0.5B-Instruct-GGUF/qwen2.5-0.5b-instruct-q8_0.gguf \
      --smoke

  # experiment 1: is qwen2.5 calibrated at max_tokens 20?
  python scripts/quality_gate_judge.py --backend native \
      --model models/Qwen/Qwen2.5-0.5B-Instruct-GGUF/qwen2.5-0.5b-instruct-q8_0.gguf \
      --set A --max-tokens 12,20,40 --out /tmp/judge_qwen25.json

  # experiment 2: qwen3, which MUST use --nothink or it reasons past the budget
  python scripts/quality_gate_judge.py --backend native \
      --model models/Qwen/Qwen3-0.6B-GGUF/Qwen3-0.6B-Q8_0.gguf --nothink \
      --set A --max-tokens 12,20,40 --out /tmp/judge_qwen3.json
"""

import argparse
import json
import os
import statistics
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional, Tuple

sys.path.insert(0, str(Path(__file__).parent))

# pylint: disable=wrong-import-position,import-error
from quality_gate_judge_data import (  # type: ignore[import-not-found]  # noqa: E402
    ASSISTANT_NOTHINK,
    ASSISTANT_PLAIN,
    CASES,
    ROLE_PARAMS,
    challenger_prompt,
    judge_prompt,
    mainer_prompt,
    parse_score_notebook,
    parse_score_production,
    set_a_pairs,
    set_b_pairs,
)

# (prompt, max_tokens, temp, seed) -> (raw output, metadata)
Completer = Callable[[str, int, float, Optional[int]], Tuple[str, Dict[str, Any]]]

ROOT = Path(__file__).parent.parent
# The funnAI repo is cloned as a sibling of this one; override with JUDGE_JSON
# when it lives elsewhere.
JUDGE_JSON = Path(
    os.environ.get("JUDGE_JSON") or ROOT.parent / "funnAI/PoAIW/scripts/3-judge.json"
)
STARTS_WITH = ["What", "Who", "Where", "When", "Why", "How", "Which", "Can", "Is", "Do"]
TOPICS = [
    "crypto",
    "nature",
    "space",
    "history",
    "science",
    "technology",
    "engineering",
    "math",
    "art",
    "music",
]
TEMPLATE_JUNK = ["<|im_start|>", "<|im_end|>", "<|endoftext|>", "<think>", "</think>"]


# --------------------------------------------------------------------------
# Native backend: llama-server from the vendored fork
# --------------------------------------------------------------------------


def start_server(
    binary: str, model: str, port: int, ctx: int
) -> "subprocess.Popen[bytes]":
    """Launch llama-server and block until /health answers.

    Deliberately not a context manager: the process must outlive this function so
    every prompt reuses one loaded model.
    """
    cmd = [
        binary,
        "-m",
        model,
        "--port",
        str(port),
        "-c",
        str(ctx),
        "--no-warmup",
        "-np",
        "1",
    ]
    # pylint: disable=consider-using-with
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    t0 = time.time()
    while time.time() - t0 < 300:
        if proc.poll() is not None:
            raise RuntimeError(f"llama-server exited early (rc={proc.returncode})")
        try:
            with urllib.request.urlopen(f"http://127.0.0.1:{port}/health", timeout=2):
                return proc
        except (urllib.error.URLError, ConnectionError, OSError):
            time.sleep(1)
    proc.kill()
    raise RuntimeError("llama-server did not become healthy within 300s")


def native_complete(
    port: int, prompt: str, max_tokens: int, temp: float, seed: Optional[int]
) -> Tuple[str, Dict[str, Any]]:
    """One /completion call with a RAW prompt (no chat template applied)."""
    payload: Dict[str, Any] = {
        "prompt": prompt,
        "n_predict": max_tokens,
        "temperature": temp,
        # Pinned to the fork's common.h defaults, which production never
        # overrides. At temp 0.0 they are inert anyway (greedy).
        "top_k": 40,
        "top_p": 0.95,
        "min_p": 0.05,
        "repeat_penalty": 1.0,
        "cache_prompt": False,
    }
    if seed is not None:
        payload["seed"] = seed
    req = urllib.request.Request(
        f"http://127.0.0.1:{port}/completion",
        json.dumps(payload).encode(),
        {"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(req, timeout=900) as r:
        d = json.loads(r.read())
    return d.get("content", ""), d


# --------------------------------------------------------------------------
# Canister backend
# --------------------------------------------------------------------------


class CanisterBackend:
    """Drives the deployed canister, reproducing production's ONE generation call.

    Production restores a pre-ingested prompt cache and then makes a single
    run_update. Restoring that cache needs the Challenger's upload machinery, so
    instead we ingest normally and then make exactly ONE generation call - which
    reproduces the property that actually matters: the Judge sees at most
    max_tokens_update tokens, once.
    """

    def __init__(self, canister: str, network: str = "local") -> None:
        # pylint: disable=import-outside-toplevel,import-error
        from ic_py_canister import (  # type: ignore[import-not-found]
            get_canister,
        )

        self.canister = canister
        self.inst = get_canister(canister, ROOT / "build" / "llama_cpp.did", network)
        self._cache_n = 0

    @staticmethod
    def _ok(resp: Any) -> Dict[str, Any]:
        # pylint: disable=import-outside-toplevel,import-error
        from ic_py_canister import extract_variant  # noqa: E402

        result: Dict[str, Any] = extract_variant(resp)
        if "Ok" not in result:
            raise RuntimeError(f"canister returned Err: {result}")
        ok: Dict[str, Any] = result["Ok"]
        return ok

    def set_max_tokens(self, n: int) -> None:
        """Set the per-call generation cap, as production does."""
        self.inst.set_max_tokens(
            {"max_tokens_query": 1, "max_tokens_update": n}, verify_certificate=False
        )

    def complete(
        self, prompt: str, max_tokens: int, temp: float, seed: Optional[int]
    ) -> Tuple[str, Dict[str, Any]]:
        """new_chat -> ingest to completion -> exactly ONE generation call.

        max_tokens is enforced canister-side by set_max_tokens(), not per call.
        """
        del max_tokens  # enforced by set_max_tokens(), kept for a common signature
        self._cache_n += 1
        cache = f"qgj_{self._cache_n}.cache"
        base = ["--prompt-cache", cache]
        self._ok(self.inst.new_chat({"args": base}, verify_certificate=False))

        gen_args = base + [
            "--prompt-cache-all",
            "--simple-io",
            "--no-display-prompt",
            "--seed",
            str(seed if seed is not None else 42),
            "--temp",
            str(temp),
        ]

        # Ingest with -n 1, exactly like test/test_qwen3.py::_ingest. Using a big
        # -n here instead would let the model generate DURING ingestion and that
        # output would be dropped - which for a Judge emitting a single digit
        # means losing the entire answer.
        out = ""
        eog = False
        tokens = 0
        for _ in range(60):
            ok = self._ok(
                self.inst.run_update(
                    {"args": gen_args + ["-n", "1", "-p", prompt]},
                    verify_certificate=False,
                )
            )
            if ok["prompt_remaining"] == "":
                # Output from the ingest phase is DISCARDED, exactly as
                # test/test_qwen3.py::_ingest does: generation replays from the
                # cached state, so counting it here double-counts the first token
                # (the canister returned '33' where native returned '3').
                break
        else:
            raise RuntimeError("prompt ingestion did not complete in 60 calls")

        # Then exactly ONE generation call, mirroring maxContinueLoopCount = 1.
        ok = self._ok(
            self.inst.run_update(
                {"args": gen_args + ["-n", "1024", "-p", ""]},
                verify_certificate=False,
            )
        )
        out += ok["output"]
        eog = bool(ok["generated_eog"])
        tokens += _opt(ok.get("n_tokens_generated")) or 0

        self.inst.remove_prompt_cache({"args": base}, verify_certificate=False)
        return out, {"generated_eog": eog, "n_tokens_generated": tokens}


def _opt(v: Any) -> Optional[int]:
    """Decode an `opt nat64`, which icp-py-core returns as [] / [N] (or None)."""
    if isinstance(v, list):
        return int(v[0]) if v else None
    return int(v) if v is not None else None


# --------------------------------------------------------------------------
# Experiments
# --------------------------------------------------------------------------


def run_judge(
    complete: Completer,
    rows: List[Dict[str, Any]],
    max_tokens_list: List[int],
    assistant: str,
    verbose: bool,
) -> List[Dict[str, Any]]:
    """Score every (question, answer) row at every max_tokens setting.

    One run per row: the Judge is greedy and fixed-seed, so repeats would return
    byte-identical output.
    """
    out: List[Dict[str, Any]] = []
    total = len(rows) * len(max_tokens_list)
    i = 0
    for mt in max_tokens_list:
        for row in rows:
            i += 1
            prompt = judge_prompt(row["question"], row["answer"], assistant)
            raw, meta = complete(prompt, mt, 0.0, 42)
            rec = dict(row)
            rec.update(
                max_tokens=mt,
                raw_output=raw,
                score=parse_score_production(raw),
                score_notebook=parse_score_notebook(raw),
                n_tokens_generated=meta.get("tokens_predicted")
                or meta.get("n_tokens_generated"),
                stopped_naturally=_stopped(meta),
            )
            out.append(rec)
            if verbose or rec["score"] == 0:
                print(
                    f"  [mt={mt:3d}] tier={row['tier']} score={rec['score']} "
                    f"raw={raw!r:40.40} {row['case_id']}",
                    flush=True,
                )
            if i % 25 == 0:
                print(f"  ... {i}/{total}", flush=True)
    return out


def _stopped(meta: Dict[str, Any]) -> Optional[bool]:
    """Did generation end on EOG, rather than being cut off by the cap?"""
    if "generated_eog" in meta:
        return bool(meta["generated_eog"])
    st = meta.get("stop_type")
    if st is not None:
        return st in ("eos", "word")
    if "stopped_eos" in meta:
        return bool(meta["stopped_eos"])
    return None


def run_generation(
    complete: Completer,
    role: str,
    repeat: int,
    assistant: str,
    max_tokens: int,
    verbose: bool,
) -> List[Dict[str, Any]]:
    """Challenger / mAIner: stochastic, so sample repeatedly and check objectively."""
    params = ROLE_PARAMS[role]
    rows = []
    for rep in range(repeat):
        for idx, topic in enumerate(TOPICS):
            if role == "challenger":
                sw = STARTS_WITH[(idx + rep) % len(STARTS_WITH)]
                prompt = challenger_prompt(topic, sw, assistant)
            else:
                case = CASES[(idx + rep) % len(CASES)]
                sw = None
                prompt = mainer_prompt(case["question"], assistant)
            seed = 1000 + rep * 100 + idx
            raw, meta = complete(prompt, max_tokens, params["temp"], seed)
            text = raw.strip()
            checks = {
                "non_empty": bool(text),
                "no_template_junk": not any(j in raw for j in TEMPLATE_JUNK),
            }
            if role == "challenger":
                checks["ends_with_question_mark"] = text.endswith("?")
                assert sw is not None  # set on the challenger branch above
                checks["starts_with_requested_word"] = text.lower().startswith(
                    sw.lower()
                )
            rows.append(
                {
                    "role": role,
                    "topic": topic,
                    "rep": rep,
                    "starts_with": sw,
                    "max_tokens": max_tokens,
                    "raw_output": raw,
                    "text": text,
                    "checks": checks,
                    "all_pass": all(checks.values()),
                    "stopped_naturally": _stopped(meta),
                }
            )
            if verbose or not all(checks.values()):
                bad = [k for k, v in checks.items() if not v]
                print(
                    f"  [{'ok  ' if not bad else 'FAIL'}] {topic:12s} {text[:70]!r}"
                    + (f"  failed={bad}" if bad else ""),
                    flush=True,
                )
    return rows


# --------------------------------------------------------------------------
# Reporting
# --------------------------------------------------------------------------


def report_judge(rows: List[Dict[str, Any]]) -> None:
    """Print parse rate, per-tier distribution and rank correlation."""
    print("\n================ JUDGE ================")
    for mt in sorted({r["max_tokens"] for r in rows}):
        sub = [r for r in rows if r["max_tokens"] == mt]
        parsed = [r for r in sub if r["score"] > 0]
        print(f"\n--- max_tokens = {mt}  (n={len(sub)})")
        print(
            f"  parse-success : {len(parsed)}/{len(sub)} "
            f"({100*len(parsed)/len(sub):.0f}%)   [score 0 = unparseable]"
        )
        stopped = [r for r in sub if r["stopped_naturally"] is True]
        print(
            f"  stopped by EOG: {len(stopped)}/{len(sub)} "
            f"({100*len(stopped)/len(sub):.0f}%)   [else cut off by the cap]"
        )
        tiered = [r for r in sub if r.get("tier") is not None]
        if tiered:
            print("  score by expected tier:")
            for tier in sorted({r["tier"] for r in tiered}, reverse=True):
                ts = [r["score"] for r in tiered if r["tier"] == tier]
                ok = [s for s in ts if s > 0]
                mean = f"{statistics.mean(ok):.2f}" if ok else "  - "
                print(
                    f"    tier {tier}: n={len(ts):3d}  mean={mean}  "
                    f"dist={_dist(ts)}"
                )
            rho = _spearman([r["tier"] for r in tiered], [r["score"] for r in tiered])
            print(
                f"  tier->score Spearman rho: {rho:+.3f}   "
                f"(+1 = perfectly ranked, 0 = no relation)"
            )
        real = [
            r
            for r in sub
            if r.get("real_production_answer")
            and r.get("case_id") == "music-jazz-fusion"
        ]
        for r in real:
            print(
                f"  CHALLENGE 27 (production scored 1): score={r['score']} "
                f"raw={r['raw_output']!r}"
            )


def _dist(scores: List[int]) -> str:
    """Compact "score:count" histogram."""
    return " ".join(f"{s}:{scores.count(s)}" for s in sorted(set(scores)))


def _spearman(a: List[int], b: List[int]) -> float:
    """Spearman rank correlation, ties averaged. No scipy dependency."""

    def rank(xs: List[int]) -> List[float]:
        order = sorted(range(len(xs)), key=lambda i: xs[i])
        r = [0.0] * len(xs)
        i = 0
        while i < len(order):
            j = i
            while j + 1 < len(order) and xs[order[j + 1]] == xs[order[i]]:
                j += 1
            avg = (i + j) / 2 + 1
            for k in range(i, j + 1):
                r[order[k]] = avg
            i = j + 1
        return r

    ra, rb = rank(a), rank(b)
    n = len(a)
    ma, mb = sum(ra) / n, sum(rb) / n
    num = sum((ra[i] - ma) * (rb[i] - mb) for i in range(n))
    da = sum((x - ma) ** 2 for x in ra) ** 0.5
    db = sum((x - mb) ** 2 for x in rb) ** 0.5
    return num / (da * db) if da and db else 0.0


def report_generation(rows: List[Dict[str, Any]], role: str) -> None:
    """Print objective check tallies plus samples to read."""
    print(f"\n================ {role.upper()} ================")
    n = len(rows)
    passed = sum(r["all_pass"] for r in rows)
    print(f"  all objective checks: {passed}/{n}")
    keys = sorted({k for r in rows for k in r["checks"]})
    for k in keys:
        ok = sum(r["checks"].get(k, True) for r in rows)
        print(f"    {k:28s}: {ok}/{n}")
    print("  samples (read these - objective checks cannot judge usefulness):")
    for r in rows[:10]:
        print(f"    [{r['topic']:12s}] {r['text'][:90]!r}")


# --------------------------------------------------------------------------


def main() -> int:
    """Run the gate; 0 on success."""
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--backend", default="native", choices=["native", "canister", "both"]
    )
    ap.add_argument("--model", help="gguf path (native backend)")
    ap.add_argument("--binary", default="/tmp/llama-native/bin/llama-server")
    ap.add_argument("--canister", default="llama_cpp")
    ap.add_argument("--network", default="local")
    ap.add_argument(
        "--role", default="judge", choices=["judge", "challenger", "mainer"]
    )
    ap.add_argument("--set", dest="which_set", default="A", choices=["A", "B", "both"])
    ap.add_argument(
        "--max-tokens", default="20", help="comma-separated sweep, e.g. 12,20,40"
    )
    ap.add_argument(
        "--nothink",
        action="store_true",
        help="prefill a closed empty <think> block (REQUIRED for qwen3)",
    )
    ap.add_argument("--limit", type=int, default=None, help="cap rows (set B is 767)")
    ap.add_argument(
        "--repeat",
        type=int,
        default=3,
        help="samples per prompt for the STOCHASTIC roles only",
    )
    ap.add_argument("--port", type=int, default=8137)
    ap.add_argument("--ctx", type=int, default=4096)
    ap.add_argument(
        "--confirm-n",
        type=int,
        default=20,
        help="rows to re-run on the canister for --backend both",
    )
    ap.add_argument("--out", default=None)
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument(
        "--smoke",
        action="store_true",
        help="one prompt, dumped raw - verify the template first",
    )
    a = ap.parse_args()

    assistant = ASSISTANT_NOTHINK if a.nothink else ASSISTANT_PLAIN
    max_tokens_list = [int(x) for x in a.max_tokens.split(",")]

    rows: List[Dict[str, Any]] = []
    if a.role == "judge":
        if a.which_set in ("A", "both"):
            rows += set_a_pairs()
        if a.which_set in ("B", "both"):
            rows += set_b_pairs(str(JUDGE_JSON), limit=a.limit)
        if a.limit and a.which_set == "A":
            rows = rows[: a.limit]

    proc = None
    results: Dict[str, Any] = {"argv": sys.argv, "assistant": assistant}
    try:
        if a.backend in ("native", "both"):
            if not a.model:
                ap.error("--model is required for the native backend")
            print(f"server : starting {Path(a.model).name} ...", flush=True)
            t0 = time.time()
            proc = start_server(a.binary, a.model, a.port, a.ctx)
            print(f"server : up in {time.time()-t0:.0f}s", flush=True)

            def native(
                p: str, mt: int, temp: float, seed: Optional[int]
            ) -> Tuple[str, Dict[str, Any]]:
                return native_complete(a.port, p, mt, temp, seed)

            complete = native
        else:
            cb = CanisterBackend(a.canister, a.network)
            cb.set_max_tokens(max_tokens_list[0])

            def canister(
                p: str, mt: int, temp: float, seed: Optional[int]
            ) -> Tuple[str, Dict[str, Any]]:
                cb.set_max_tokens(mt)
                return cb.complete(p, mt, temp, seed)

            complete = canister

        if a.smoke:
            if a.role == "judge":
                r = rows[0]
                prompt = judge_prompt(r["question"], r["answer"], assistant)
            elif a.role == "challenger":
                prompt = challenger_prompt("music", "What", assistant)
            else:
                prompt = mainer_prompt(CASES[0]["question"], assistant)
            print("----- RAW PROMPT -----")
            print(repr(prompt))
            print("----------------------", flush=True)
            raw, meta = complete(prompt, max_tokens_list[0], 0.0, 42)
            print(f"RAW OUTPUT : {raw!r}")
            print(f"score      : {parse_score_production(raw)}")
            print(f"stopped    : {_stopped(meta)}")
            return 0

        if a.role == "judge":
            print(
                f"judge  : {len(rows)} rows x {len(max_tokens_list)} max_tokens",
                flush=True,
            )
            jrows = run_judge(complete, rows, max_tokens_list, assistant, a.verbose)
            results["judge"] = jrows
            report_judge(jrows)

            if a.backend == "both":
                print(
                    "\n--- canister equivalence check "
                    f"({a.confirm_n} rows, greedy => must match byte for byte)"
                )
                cb = CanisterBackend(a.canister, a.network)
                mt = max_tokens_list[0]
                cb.set_max_tokens(mt)
                mism = 0
                subset = [r for r in jrows if r["max_tokens"] == mt][: a.confirm_n]
                for r in subset:
                    p = judge_prompt(r["question"], r["answer"], assistant)
                    craw, _ = cb.complete(p, mt, 0.0, 42)
                    if craw != r["raw_output"]:
                        mism += 1
                        print(
                            f"  MISMATCH {r['case_id']}: native={r['raw_output']!r} "
                            f"canister={craw!r}"
                        )
                print(f"  matched: {len(subset)-mism}/{len(subset)}")
                results["equivalence"] = {"n": len(subset), "mismatches": mism}
                if mism:
                    print(
                        "  => native numbers are NOT trustworthy; rerun with "
                        "--backend canister"
                    )
        else:
            grows = run_generation(
                complete, a.role, a.repeat, assistant, max_tokens_list[0], a.verbose
            )
            results[a.role] = grows
            report_generation(grows, a.role)
    finally:
        if proc is not None:
            proc.terminate()
            try:
                proc.wait(timeout=20)
            except subprocess.TimeoutExpired:
                proc.kill()

    if a.out:
        Path(a.out).write_text(json.dumps(results, indent=2), encoding="utf-8")
        print(f"\nwrote {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
