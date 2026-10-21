"""Monitors the logs of llama_cpp canisters and saves them outside the canister.

The canister log is a small ring buffer (each canister keeps ~50 records, and every
logged line takes two), so busy refusals, expiries and traps rotate out within seconds
during a full call. This script polls `icp canister logs`, keeps only the records it
has not seen yet, joins them into lines (a line can span several records), prints the
lines and appends them to:

  scripts/logs-<network>/<canister>.log   one file per canister
  scripts/logs-<network>/combined.log     all canisters

Each line starts with the index and time of its first record. A restart resumes after
the last saved record (kept in scripts/logs-<network>/<canister>.next). It warns when
records were lost between two polls; then lower --interval.

Run with:

    python -m scripts.monitor_logs --network local
    python -m scripts.monitor_logs --network production --canister llama_cpp
"""

import argparse
import datetime
import json
import os
import subprocess
import sys
import time
from pathlib import Path
from typing import Any, Dict, List, Optional

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
) -> List[Dict[str, Any]]:
    """Fetches the canister's log records ({index, timestamp, content}), from
    since_index on if given."""
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
        return []
    except subprocess.TimeoutExpired:
        print(
            f"[monitor] {canister}: no logs after {FETCH_TIMEOUT_SECONDS}s, "
            "trying again",
            file=sys.stderr,
        )
        return []
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
        self.text = ""

    def add(self, record: Dict[str, Any]) -> List[str]:
        """Adds a record; returns the lines it completes."""
        if self.first is None:
            self.first = record
        self.text += record["content"]
        lines: List[str] = []
        while "\n" in self.text:
            line, self.text = self.text.split("\n", 1)
            if line:  # a blank line carries nothing
                lines.append(f"{stamp(self.first)}: {line}")
            self.first = record
        if not self.text:
            self.first = None
        return lines


def read_next_index(path: Path) -> Optional[int]:
    """The index to resume from, saved by an earlier run."""
    try:
        return int(path.read_text(encoding="utf-8").strip())
    except (OSError, ValueError):
        return None


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
        type=float,
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
    next_index = {c: read_next_index(next_file[c]) for c in canisters}
    lines = {c: Lines() for c in canisters}
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
                done: List[str] = []
                for record in records:
                    done += lines[canister].add(record)
                write(canister, done)
                # resume at the first record of an unfinished line
                pending = lines[canister].first
                next_index[canister] = (
                    pending["index"] if pending else records[-1]["index"] + 1
                )
                next_file[canister].write_text(
                    str(next_index[canister]), encoding="utf-8"
                )
                if pending:  # fetched again from there on the next poll
                    lines[canister] = Lines()
            time.sleep(args.interval)
    except KeyboardInterrupt:
        print("\n[monitor] stopped")
    return 0


if __name__ == "__main__":
    sys.exit(main())
