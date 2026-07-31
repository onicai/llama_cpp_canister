"""Deploys & runs pytest in a freshly started local network for some LLMs"""

import sys
from pathlib import Path
from typing import Any, Dict, List
import subprocess
import typer
from icpp.run_shell_cmd import run_shell_cmd

# SCRIPTS_PATH = Path(__file__).parent
ROOT_PATH = Path(__file__).parent.parent


def icp_network_stop() -> None:
    """Stop the local network, tolerating a non-zero exit when none is running.

    `icp network stop` returns non-zero when there is no running network, which
    is not a failure for our purposes (dfx's `stop` was lenient about this).
    """
    subprocess.run(["icp", "network", "stop"], cwd=ROOT_PATH, check=False)


def main() -> int:
    """Start local network; Deploy canister; Upload LLM model; Pytest"""
    try:
        typer.echo("--\nBuild the wasm")
        run_shell_cmd(
            "icpp build-wasm --to-compile all",
            cwd=ROOT_PATH,
        )

        # Tests shared across every model iteration (model-agnostic).
        shared = [
            "test/test_canister_functions.py",
            "test/test_promptcache.py",
            "test/test_files.py",
            "test/test_cycle_balance.py",
        ]

        # Each entry deploys a fresh canister, uploads its model, and runs its own
        # test_paths. `env` is applied to every pytest run in that iteration (used
        # to point test_small_maxtokens at the right model / KV type). The
        # small-max_tokens prompt-cache regression is run on BOTH the tiny model
        # and gemma-3-270M so both the f16 and q8_0 KV paths are covered.
        tests: List[Dict[str, Any]] = [
            {
                "filename": "models/stories260Ktok512.gguf",
                "canister_filename": "models/tiny.gguf",
                "wasm_memory_limit": None,
                "env": {"SMALL_MAXTOK_MODEL": "models/tiny.gguf"},
                "test_paths": shared
                + [
                    "test/test_tiny_stories.py",
                    # after a model is loaded; exact token-accounting reconciliation.
                    "test/test_token_counts.py",
                    # small-max_tokens multi-call ingestion regression (f16 KV).
                    "test/test_small_maxtokens.py",
                ],
            },
            # gemma-3-270M (gemma3 / iSWA architecture, q8_0 KV): exercises the
            # small-max_tokens prompt-cache regression on a real model. Requires the
            # gemma gguf downloaded (see README-gemma-3.md) and the raised wasm
            # memory limit. test_small_maxtokens loads the model itself.
            {
                "filename": "models/google/gemma-3-270m-it-GGUF/gemma-3-270m-it-Q8_0.gguf",  # pylint: disable=line-too-long
                "canister_filename": "models/model.gguf",
                "wasm_memory_limit": 4026531840,  # 3.75 GiB
                "env": {
                    "SMALL_MAXTOK_MODEL": "models/model.gguf",
                    "SMALL_MAXTOK_KV": "q8_0",
                },
                "test_paths": ["test/test_small_maxtokens.py"],
            },
            # The Qwen models time out in the Github action; run them locally by
            # uncommenting (schema: filename, canister_filename, wasm_memory_limit,
            # env, test_paths). Qwen3-0.6B multi-turn is exercised by test_qwen3.py.
            # {
            #     "filename": "models/Qwen/Qwen3-0.6B-GGUF/Qwen3-0.6B-Q8_0.gguf",
            #     "canister_filename": "models/model.gguf",
            #     "wasm_memory_limit": 4026531840,
            #     "env": {},
            #     "test_paths": ["test/test_qwen3.py"],
            # },
        ]

        for test in tests:
            filename = test["filename"]
            canister_filename = test["canister_filename"]
            test_paths = test["test_paths"]
            env_prefix = "".join(f"{k}={v} " for k, v in test.get("env", {}).items())

            typer.echo("--\nStop the local network")
            icp_network_stop()

            typer.echo("--\nStart a clean local network")
            # icp-cli has no `--clean` flag. The clean-start equivalent is to
            # remove the disposable cache (replica state + the managed network's
            # id mappings) and start fresh. NEVER remove .icp/data — it holds the
            # committed mainnet id mappings.
            run_shell_cmd("rm -rf .icp/cache", cwd=ROOT_PATH)
            run_shell_cmd("icp network start -d", cwd=ROOT_PATH)

            typer.echo(f"--\nDeploy {ROOT_PATH.name}")
            run_shell_cmd("icp deploy -e local -y", cwd=ROOT_PATH)

            # Top up cycles so loading a larger model can grow wasm memory
            # (otherwise load_model traps with IC0532 insufficient-cycles).
            typer.echo("--\nTop up cycles")
            run_shell_cmd(
                "icp canister top-up llama_cpp --amount 20000000000000 -e local",
                cwd=ROOT_PATH,
            )

            if test.get("wasm_memory_limit"):
                typer.echo(
                    f"--\nRaise wasm memory limit to {test['wasm_memory_limit']}"
                )
                run_shell_cmd(
                    "icp canister settings update llama_cpp "
                    f"--wasm-memory-limit {test['wasm_memory_limit']} -e local",
                    cwd=ROOT_PATH,
                )

            typer.echo(f"--\nUpload {filename}")
            run_shell_cmd(
                f" python -m scripts.upload --network local --canister llama_cpp "
                f" --canister-filename {canister_filename} {filename}",
                cwd=ROOT_PATH,
            )

            for test_path in test_paths:
                typer.echo(f"--\nRun pytest on {test_path}")
                run_shell_cmd(
                    f"{env_prefix}pytest -vv --network=local {test_path}",
                    cwd=ROOT_PATH,
                )

            typer.echo("--\nStop the local network")
            icp_network_stop()

    except subprocess.CalledProcessError as e:
        typer.echo("--\nSomething did not pass")
        icp_network_stop()
        return e.returncode

    typer.echo("--\nCongratulations, everything passed!")
    try:
        typer.echo("💯 🎉 🏁")
    except UnicodeEncodeError:
        typer.echo(" ")
    return 0


if __name__ == "__main__":
    sys.exit(main())
