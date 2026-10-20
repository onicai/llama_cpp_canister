"""Monitors the logs of llama_cpp canisters and saves them outside the canister.

The canister log is a small ring buffer (each canister keeps ~50 records, and every
logged line takes two), so busy refusals, expiries and traps rotate out within seconds
during a full call. This script polls `icp canister logs`, keeps only the records it
has not seen yet, prints them and appends them to:

  scripts/logs-<network>/<canister>.log   one file per canister
  scripts/logs-<network>/combined.log     all canisters

It warns when records were lost between two polls; then lower --interval.

Run with:

    python -m scripts.monitor_logs --network local
    python -m scripts.monitor_logs --network production --canister llama_cpp
"""

import argparse
import os
import re
import subprocess
import sys
import time
from pathlib import Path
from typing import Dict, List, Optional, Tuple

SCRIPT_DIR = Path(__file__).parent
REPO_ROOT = SCRIPT_DIR.parent
TEST_IDENTITY = "llama-cpp-testing"

# A log record starts with "[<index>. <timestamp>]: "; its content may span lines
RECORD_START = re.compile(r"^\[(\d+)\. ([^\]]+)\]: ?(.*)$")

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


def parse_records(output: str) -> List[Tuple[int, str, str]]:
    """Splits `icp canister logs` output into (index, timestamp, content) records."""
    records: List[Tuple[int, str, str]] = []
    for line in output.splitlines():
        m = RECORD_START.match(line)
        if m:
            records.append((int(m.group(1)), m.group(2), m.group(3)))
        elif records:
            index, timestamp, content = records[-1]
            records[-1] = (index, timestamp, content + "\n" + line)
    return [(i, t, c.rstrip("\n")) for i, t, c in records]


def get_records(
    canister: str, network: str, identity: str, since_index: Optional[int]
) -> List[Tuple[int, str, str]]:
    """Fetches the canister's log records, from since_index on if given."""
    cmd = ["icp", "canister", "logs", canister, "-e", network, "--identity", identity]
    if since_index is not None:
        cmd += ["--since-index", str(since_index)]
    try:
        output = subprocess.run(
            cmd, cwd=REPO_ROOT, capture_output=True, text=True, check=True
        ).stdout
    except subprocess.CalledProcessError as e:
        print(f"[monitor] {canister}: {e.stderr.strip()[:300]}", file=sys.stderr)
        return []
    return parse_records(output)


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
    parser.add_argument(
        "--keep-empty",
        action="store_true",
        help="also keep the empty records that follow every logged line",
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
    next_index: Dict[str, Optional[int]] = {c: None for c in canisters}
    try:
        while True:
            for canister in canisters:
                records = get_records(
                    canister, args.network, identity, next_index[canister]
                )
                if not records:
                    continue
                expected = next_index[canister]
                if expected is not None and records[0][0] > expected:
                    print(
                        f"[monitor] {canister}: {records[0][0] - expected} log records "
                        "were lost between two polls; lower --interval",
                        file=sys.stderr,
                    )
                next_index[canister] = records[-1][0] + 1
                lines = [
                    f"[{i}. {t}]: {c}" for i, t, c in records if c or args.keep_empty
                ]
                if not lines:
                    continue
                with open(
                    log_dir / f"{canister}.log", "a", encoding="utf-8"
                ) as f1, open(combined, "a", encoding="utf-8") as f2:
                    for line in lines:
                        f1.write(line + "\n")
                        f2.write((f"[{canister}] " if tag else "") + line + "\n")
                        prefix = f"{color[canister]}[{canister}]{RESET} " if tag else ""
                        print(prefix + line, flush=True)
            time.sleep(args.interval)
    except KeyboardInterrupt:
        print("\n[monitor] stopped")
    return 0


if __name__ == "__main__":
    sys.exit(main())
