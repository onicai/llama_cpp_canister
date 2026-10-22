"""Runs a command, and kills it when its memory use goes over a limit.

The native tests (`./build-native/mockic.exe`) run the canister code on the host,
where nothing caps its memory the way the IC does. A bug that allocates without
limit then fills the host's memory and swap within minutes. This guard checks the
command's resident memory every second; over the limit, it kills the command,
prints an ERROR (also as a GitHub Actions error annotation) and exits with 1.
Otherwise it exits with the command's own exit code.

Run with:

    python -m scripts.memory_guard --max-gb 4 -- ./build-native/mockic.exe
"""

import argparse
import os
import subprocess
import sys
import time
from typing import List, Optional


def rss_bytes(pid: int) -> Optional[int]:
    """The resident memory of a process, from `ps` (macOS and Linux). None when
    the process is gone."""
    result = subprocess.run(
        ["ps", "-o", "rss=", "-p", str(pid)],
        capture_output=True,
        text=True,
        check=False,
    )
    kilobytes = result.stdout.strip()
    return int(kilobytes) * 1024 if kilobytes.isdigit() else None


def error(message: str) -> None:
    """Prints an ERROR line, and a GitHub Actions error annotation in CI."""
    print(f"ERROR: {message}", file=sys.stderr, flush=True)
    if os.environ.get("GITHUB_ACTIONS") == "true":
        print(f"::error title=memory guard::{message}", flush=True)


def guard(command: List[str], max_bytes: int, interval: float) -> int:
    """Runs the command; kills it above max_bytes. Returns the exit code."""
    with subprocess.Popen(command) as process:
        peak = 0
        while process.poll() is None:
            rss = rss_bytes(process.pid) or 0
            peak = max(peak, rss)
            if rss > max_bytes:
                process.kill()
                process.wait()
                error(
                    f"{command[0]} used {rss / 2**30:.1f} GiB, over the "
                    f"{max_bytes / 2**30:.1f} GiB limit of the memory guard: "
                    "killed. Something allocates without limit."
                )
                return 1
            time.sleep(interval)
        print(f"[memory guard] {command[0]}: peak {peak / 2**30:.2f} GiB")
        return process.returncode


def main() -> int:
    """Parses the arguments and runs the guarded command."""
    parser = argparse.ArgumentParser(description="Run a command with a memory limit.")
    parser.add_argument(
        "--max-gb",
        type=float,
        required=True,
        help="the resident memory limit in GiB",
    )
    parser.add_argument(
        "--interval",
        type=float,
        default=1.0,
        help="seconds between memory checks (default: 1)",
    )
    parser.add_argument("command", nargs="+", help="the command, after --")
    args = parser.parse_args()
    return guard(args.command, int(args.max_gb * 2**30), args.interval)


if __name__ == "__main__":
    sys.exit(main())
