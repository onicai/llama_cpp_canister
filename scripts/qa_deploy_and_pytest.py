"""Deploys & runs pytest in a freshly started local network for some LLMs"""

import sys
from pathlib import Path
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

        tests = [
            {
                "filename": "models/stories260Ktok512.gguf",
                "canister_filename": "models/tiny.gguf",
                "test_path_model": "test/test_tiny_stories.py",
            },
            # The Qwen models time out in the Github action; run them locally by
            # uncommenting. Qwen3-0.6B is the current default reference model; its
            # multi-turn / non-thinking behaviour is exercised by test_qwen3.py.
            # {
            #     "filename": "models/Qwen/Qwen3-0.6B-GGUF/Qwen3-0.6B-Q8_0.gguf",
            #     "canister_filename": "models/model.gguf",
            #     "test_path_model": "test/test_qwen3.py",
            # },
            # {
            #     "filename": "models/Qwen/Qwen2.5-0.5B-Instruct-GGUF/qwen2.5-0.5b-instruct-q8_0.gguf",  # pylint: disable=line-too-long
            #     "canister_filename": "models/model.gguf",
            #     "test_path_model": "test/test_qwen2.py",
            # },
        ]

        test_path_canister = "test/test_canister_functions.py"
        test_path_promptcache = "test/test_promptcache.py"
        test_path_files = "test/test_files.py"
        test_path_cycle_balance = "test/test_cycle_balance.py"
        test_path_token_counts = "test/test_token_counts.py"
        for test in tests:
            filename = test["filename"]
            canister_filename = test["canister_filename"]
            test_path_model = test["test_path_model"]

            test_paths = [
                test_path_canister,
                test_path_promptcache,
                test_path_files,
                test_path_cycle_balance,
                test_path_model,
                # after test_path_model so a model is loaded; asserts the v0.15.0
                # exact token-accounting fields (model-agnostic reconciliation).
                test_path_token_counts,
            ]

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

            typer.echo(f"--\nUpload {filename}")
            run_shell_cmd(
                f" python -m scripts.upload --network local --canister llama_cpp "
                f" --canister-filename {canister_filename} {filename}",
                cwd=ROOT_PATH,
            )

            for test_path in test_paths:
                typer.echo(f"--\nRun pytest on {test_path}")
                run_shell_cmd(f"pytest -vv --network=local {test_path}", cwd=ROOT_PATH)

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
