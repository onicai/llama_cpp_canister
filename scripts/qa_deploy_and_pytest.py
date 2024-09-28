"""Deploys & runs pytest in a freshly started local network for some LLMs"""

import sys
import platform
from pathlib import Path
import subprocess
import typer
from icpp.run_shell_cmd import run_shell_cmd
from icpp.run_dfx_cmd import run_dfx_cmd

# SCRIPTS_PATH = Path(__file__).parent
ROOT_PATH = Path(__file__).parent.parent


def main() -> int:
    """Start local network; Deploy canister; Upload LLM model; Pytest"""
    try:
        if platform.system() == "Windows":
            # On Windows, we run it all within a single Powershell script
            print("QA ON WINDOWS IS NOT YET SUPPORTED")
            sys.exit(1)
            # run_shell_cmd(
            #     SCRIPTS_PATH / "smoketest.ps1",
            #     run_in_powershell=True,
            #     cwd=ROOT_PATH,
            # )
        else:
            # On Mac & Ubuntu, it is much more flexible

            typer.echo("--\nBuild the wasm")
            run_shell_cmd(
                "icpp build-wasm --to-compile all",
                cwd=ROOT_PATH,
            )

            tests = [
                {
                    "filename": "models/stories260Ktok512.gguf",
                    "canister_filename": "models/stories260Ktok512.gguf",
                    "test_path_model": "test/test_tiny_stories.py",
                },
                # This times out in Github action. Can only be run locally.
                # {
                #     "filename": "models/Qwen/Qwen2.5-0.5B-Instruct-GGUF/qwen2.5-0.5b-instruct-q8_0.gguf",  # pylint: disable=line-too-long
                #     "canister_filename": "models/qwen2.5-0.5b-instruct-q8_0.gguf",
                #     "test_path_model": "test/test_qwen2.py",
                # },
            ]

            test_path_canister = "test/test_canister_functions.py"
            for test in tests:
                filename = test["filename"]
                canister_filename = test["canister_filename"]
                test_path_model = test["test_path_model"]

                test_paths = [test_path_canister, test_path_model]

                typer.echo("--\nStop the local network")
                run_dfx_cmd("stop")

                typer.echo("--\nStart a clean local network")
                run_dfx_cmd("start --clean --background")

                typer.echo(f"--\nDeploy {ROOT_PATH.name}")
                run_dfx_cmd("deploy", cwd=ROOT_PATH)

                typer.echo(f"--\nUpload {filename}")
                run_shell_cmd(
                    f" python -m scripts.upload --network local --canister llama_cpp "
                    f" --canister-filename {canister_filename} {filename}",
                    cwd=ROOT_PATH,
                )

                for test_path in test_paths:
                    typer.echo(f"--\nRun pytest on {test_path}")
                    run_shell_cmd(
                        f"pytest -vv --network=local {test_path}", cwd=ROOT_PATH
                    )

                typer.echo("--\nStop the local network")
                run_dfx_cmd("stop")

    except subprocess.CalledProcessError as e:
        typer.echo("--\nSomething did not pass")
        if platform.system() != "Windows":
            run_dfx_cmd("stop")
        return e.returncode

    typer.echo("--\nCongratulations, everything passed!")
    try:
        typer.echo("💯 🎉 🏁")
    except UnicodeEncodeError:
        typer.echo(" ")
    return 0


if __name__ == "__main__":
    sys.exit(main())
