"""Monitors the logs of llama_cpp canisters and saves them outside the canister.

The canister log is a small ring buffer (each canister keeps ~50 records, and every
logged line takes two), so busy refusals, expiries and traps rotate out within seconds
during a full call. This script polls `icp canister logs`, keeps only the records it
has not seen yet, joins them into lines (a line can span several records), prints the
lines and appends them to:

  scripts/logs-<network>/<canister>.log   one file per canister
  scripts/logs-<network>/combined.log     all canisters

Each line starts with the index and time of its first record. A restart resumes after
the last written line (kept in scripts/logs-<network>/<canister>.next, with the canister
id). It starts over when the id changed, or when the log no longer reaches that record
(a recreated local network). It warns when records were lost between two polls, and
marks the next line "[may be truncated]", also after a restart; then lower --interval.

Run with:

    python -m scripts.monitor_logs --network local
    python -m scripts.monitor_logs --network production --canister llama_cpp
"""

import argparse
import datetime
import json
import math
import os
import signal
import subprocess
import sys
import time
from contextlib import contextmanager
from functools import partial
from pathlib import Path
from typing import Any, Callable, Dict, Iterator, List, Optional, Tuple, TypeVar

SCRIPT_DIR = Path(__file__).parent
REPO_ROOT = SCRIPT_DIR.parent
TEST_IDENTITY = "llama-cpp-testing"
FETCH_TIMEOUT_SECONDS = 60

COLORS = ["\033[36m", "\033[33m", "\033[35m", "\033[32m", "\033[34m", "\033[31m"]
RESET = "\033[0m"


def default_identity(network: str) -> str:
    """The identity to read the logs as: .icp-identity on production, else a test one."""
    if network == "local":
        return os.environ.get("ICPP_PRO_TEST_IDENTITY", TEST_IDENTITY)
    path = REPO_ROOT / ".icp-identity"
    if path.exists():
        for line in path.read_text(encoding="utf-8").splitlines():
            if line.strip() and not line.strip().startswith("#"):
                return line.split()[0]
    sys.exit(f"ERROR: {path} is missing or empty: it names the identity for {network}.")


def get_records(
    canister: str, network: str, identity: str, since_index: Optional[int]
) -> Optional[List[Dict[str, Any]]]:
    """Fetches the canister's log records ({index, timestamp, content}), from
    since_index on if given. None when the fetch failed."""
    cmd = ["icp", "canister", "logs", canister, "-e", network]
    cmd += ["--identity", identity, "--json"]
    if since_index is not None:
        cmd += ["--since-index", str(since_index)]
    try:
        output = subprocess.run(
            cmd,
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=True,
            timeout=FETCH_TIMEOUT_SECONDS,
        ).stdout
    except subprocess.CalledProcessError as e:
        print(f"[monitor] {canister}: {e.stderr.strip()[:300]}", file=sys.stderr)
        return None
    except subprocess.TimeoutExpired:
        print(
            f"[monitor] {canister}: no logs after {FETCH_TIMEOUT_SECONDS}s, "
            "trying again",
            file=sys.stderr,
        )
        return None
    records: List[Dict[str, Any]] = json.loads(output).get("log_records", [])
    return records


def stamp(record: Dict[str, Any]) -> str:
    """The "[<index>. <UTC time>]" prefix of a line, from its first record."""
    t = datetime.datetime.fromtimestamp(
        record["timestamp"] / 1e9, tz=datetime.timezone.utc
    )
    return f"[{record['index']}. {t.strftime('%Y-%m-%dT%H:%M:%S.%fZ')}]"


class Lines:  # pylint: disable=too-few-public-methods
    """Joins the records of one canister into lines. An unfinished line is kept
    until a later record ends it."""

    def __init__(self) -> None:
        self.first: Optional[Dict[str, Any]] = None  # first record of `text`
        self.written = 0  # characters of first's content already in written lines
        self.text = ""
        self.after_loss = False  # records were lost before `text`

    def add(self, record: Dict[str, Any], skip: int = 0) -> List[str]:
        """Adds a record, without its first `skip` characters (written before a
        restart); returns the lines it completes."""
        if self.first is None:
            self.first, self.written = record, skip
        self.text += record["content"][skip:]
        lines: List[str] = []
        while "\n" in self.text:
            line, self.text = self.text.split("\n", 1)
            if line:  # a blank line carries nothing
                marker = "[may be truncated] " if self.after_loss else ""
                lines.append(f"{stamp(self.first)}: {marker}{line}")
                self.after_loss = False
            self.first = record
            self.written = len(record["content"]) - len(self.text)
        if not self.text:
            self.first = None
        return lines


def canister_id(canister: str, network: str, identity: str) -> Optional[str]:
    """The id of a canister name (a canister id is returned as is). None when
    the lookup failed."""
    cmd = ["icp", "canister", "status", canister, "-e", network]
    cmd += ["--identity", identity, "--id-only"]
    try:
        return subprocess.run(
            cmd,
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=True,
            timeout=FETCH_TIMEOUT_SECONDS,
        ).stdout.strip()
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as e:
        print(f"[monitor] {canister}: no canister id ({e})", file=sys.stderr)
        return None


T = TypeVar("T")


def retry(fetch: Callable[[], Optional[T]], interval: float) -> T:
    """Calls fetch until it succeeds: a failure tells nothing about the log."""
    while True:
        result = fetch()
        if result is not None:
            return result
        time.sleep(interval)


def positive(value: str) -> float:
    """An argparse type: a finite number of seconds greater than 0."""
    seconds = float(value)
    if not math.isfinite(seconds) or seconds <= 0:
        raise argparse.ArgumentTypeError(f"{value} is not a number greater than 0")
    return seconds


def read_next_index(path: Path, cid: str) -> Tuple[Optional[int], int, bool]:
    """Where to resume, saved by an earlier run for canister `cid`: the index, the
    characters of that record already written, and whether records were lost
    before it. Stops on a checkpoint it cannot read: starting over would write the
    saved lines again."""
    if not path.exists():
        return None, 0, False
    try:
        saved_id, index, skip, *lost = path.read_text(encoding="utf-8").split()
        if saved_id != cid:
            return None, 0, False
        if int(index) >= 0 and int(skip) >= 0 and lost in ([], ["lost"]):
            return int(index), int(skip), lost == ["lost"]
    except (OSError, ValueError):
        pass
    sys.exit(f"ERROR: {path} is not a checkpoint; delete it to start over.")


def save_next_index(path: Path, cid: str, lines: Lines, next_index: int) -> None:
    """Saves where to resume, replacing the file in one step: at the first record of
    an unfinished line, else at next_index."""
    if lines.first:
        index, skip = lines.first["index"], lines.written
    else:
        index, skip = next_index, 0
    lost = " lost" if lines.after_loss else ""
    tmp = path.with_suffix(".next.tmp")
    tmp.write_text(f"{cid} {index} {skip}{lost}", encoding="utf-8")
    tmp.replace(path)


@contextmanager
def uninterrupted() -> Iterator[None]:
    """Holds off Ctrl+C and SIGTERM until the block is done, so the lines written
    and the saved index always match."""
    signals = {signal.SIGINT, signal.SIGTERM}
    signal.pthread_sigmask(signal.SIG_BLOCK, signals)
    try:
        yield
    finally:
        signal.pthread_sigmask(signal.SIG_UNBLOCK, signals)


def main() -> int:
    """Polls the logs until interrupted."""
    parser = argparse.ArgumentParser(description="Monitor llama_cpp canister logs.")
    parser.add_argument(
        "--network",
        choices=["local", "production"],
        default="local",
        help="the icp environment (default: local)",
    )
    parser.add_argument(
        "--canister",
        action="append",
        help="a canister name or id; repeat for several (default: llama_cpp)",
    )
    parser.add_argument(
        "--identity",
        help="the identity to read the logs as; it must be a controller unless the "
        "log visibility is public (default: .icp-identity on production, "
        f"$ICPP_PRO_TEST_IDENTITY or {TEST_IDENTITY} locally)",
    )
    parser.add_argument(
        "--interval",
        type=positive,
        default=1.0,
        help="seconds between polls (default: 1)",
    )
    args = parser.parse_args()

    canisters: List[str] = args.canister or ["llama_cpp"]
    identity: str = args.identity or default_identity(args.network)
    log_dir = SCRIPT_DIR / f"logs-{args.network}"
    log_dir.mkdir(exist_ok=True)
    combined = log_dir / "combined.log"
    color = {c: COLORS[i % len(COLORS)] for i, c in enumerate(canisters)}
    tag = len(canisters) > 1

    print(
        f"[monitor] {', '.join(canisters)} on '{args.network}' as '{identity}', "
        f"every {args.interval}s, into {log_dir}"
    )

    def write(canister: str, lines: List[str]) -> None:
        with open(log_dir / f"{canister}.log", "a", encoding="utf-8") as f1, open(
            combined, "a", encoding="utf-8"
        ) as f2:
            for line in lines:
                f1.write(line + "\n")
                f2.write((f"[{canister}] " if tag else "") + line + "\n")
                prefix = f"{color[canister]}[{canister}]{RESET} " if tag else ""
                print(prefix + line, flush=True)

    next_file = {c: log_dir / f"{c}.next" for c in canisters}
    cid = {
        c: retry(partial(canister_id, c, args.network, identity), args.interval)
        for c in canisters
    }
    saved = {c: read_next_index(next_file[c], cid[c]) for c in canisters}
    next_index = {c: saved[c][0] for c in canisters}
    skip = {c: saved[c][1] for c in canisters}
    lines = {c: Lines() for c in canisters}
    for c in canisters:
        # a log that no longer reaches the saved index belongs to a new canister
        last = retry(
            partial(get_records, c, args.network, identity, None), args.interval
        )
        if next_index[c] is not None and (
            not last or next_index[c] > last[-1]["index"] + 1
        ):
            print(f"[monitor] {c}: a new log, starting from its first record")
            next_index[c] = None
        else:
            lines[c].after_loss = saved[c][2]
    try:
        while True:
            for canister in canisters:
                records = get_records(
                    canister, args.network, identity, next_index[canister]
                )
                if not records:
                    continue
                expected = next_index[canister]
                if expected is not None and records[0]["index"] > expected:
                    print(
                        f"[monitor] {canister}: {records[0]['index'] - expected} log "
                        "records were lost between two polls; lower --interval",
                        file=sys.stderr,
                    )
                    lines[canister].after_loss = True
                done: List[str] = []
                for record in records:
                    # a restart skips what it wrote of its first record
                    n = skip[canister] if record["index"] == expected else 0
                    done += lines[canister].add(record, n)
                skip[canister] = 0
                resume: int = records[-1]["index"] + 1
                next_index[canister] = resume
                with uninterrupted():
                    write(canister, done)
                    save_next_index(
                        next_file[canister], cid[canister], lines[canister], resume
                    )
            time.sleep(args.interval)
    except KeyboardInterrupt:
        print("\n[monitor] stopped")
    return 0


if __name__ == "__main__":
    sys.exit(main())
