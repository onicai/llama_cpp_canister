"""The pytest fixtures
   https://docs.pytest.org/en/latest/fixture.html
"""
# pylint: disable=missing-function-docstring, unused-import, wildcard-import, unused-wildcard-import, line-too-long, unused-argument
import os
import subprocess
from typing import Dict
import pytest
from icpp.conftest_base import *  # pytest fixtures provided by icpp
from icpp.run_shell_cmd import run_shell_cmd

# Define your pytest fixtures below

# A second icp identity, which is NOT a controller of the canister. The
# `test-llm-wasm` Makefile target creates it and exports the variable; the
# default keeps a manual `pytest` run working without any setup beyond creating
# the identity once.
NON_CONTROLLER_IDENTITY_ENV_VAR = "ICPP_PRO_TEST_IDENTITY_NON_CONTROLLER"
NON_CONTROLLER_IDENTITY_DEFAULT = "llama-cpp-other-user"


@pytest.fixture(scope="session")
def identity_non_controller(identity: str) -> Dict[str, str]:
    """An identity that is neither a controller nor an admin of the canister.

    Returns its name & principal, so a test can pass `identity=<name>` to
    `call_canister_api` for that one call. Unlike `identity_anonymous`, this is
    an authenticated caller, which is what proves an endpoint checks *who* the
    caller is and not merely that they are not anonymous.
    """
    name = (
        os.environ.get(NON_CONTROLLER_IDENTITY_ENV_VAR, "").strip()
        or NON_CONTROLLER_IDENTITY_DEFAULT
    )
    if name == identity:
        pytest.fail(
            f"ERROR: ${NON_CONTROLLER_IDENTITY_ENV_VAR} is '{name}', which is the "
            f"identity the tests already run as. It must be a different identity, "
            f"or it would be a controller of the canister."
        )

    try:
        principal = run_shell_cmd(
            f"icp identity principal --identity {name}",
            capture_output=True,
            timeout_seconds=30,
        ).rstrip("\n")
    except subprocess.CalledProcessError as e:
        pytest.fail(
            f"ERROR: identity '{name}' does not exist. Create it with:\n\n"
            f"    icp identity new {name} --storage plaintext\n\n"
            f"or run the QA via `make test-llm-wasm`, which creates it.\n\n{e.output}"
        )

    return {"identity": name, "principal": principal}
