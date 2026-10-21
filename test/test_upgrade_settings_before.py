"""Settings that must survive an upgrade: set them before it.

scripts/qa_deploy_and_pytest.py runs this file, upgrades the canister in place
(`icp deploy -e local -y --mode upgrade`), then runs
test/test_upgrade_settings_after.py, which checks them and resets them.

The settings are those a gateway in front of the canister relies on: its admin
role (here: the non-controller test identity), the access level, and the
cleanup timer of the stored states with its config.
"""

# pylint: disable=missing-function-docstring, line-too-long

from pathlib import Path
from typing import Dict

from .candid_compat import call_canister_api

ICP_YAML_PATH = Path(__file__).parent / "../icp.yaml"
CANISTER_NAME = "llama_cpp"
TTL_48H = 48 * 3600


def call(network: str, method: str, arg: str, identity: str = "") -> str:
    return call_canister_api(
        icp_yaml_path=ICP_YAML_PATH,
        canister_name=CANISTER_NAME,
        canister_method=method,
        canister_argument=arg,
        network=network,
        **({"identity": identity} if identity else {}),
    )


def test__set_the_settings(
    network: str, identity_non_controller: Dict[str, str]
) -> None:
    principal = identity_non_controller["principal"]
    response = call(
        network,
        "assignAdminRole",
        f'(record {{ "principal" = "{principal}"; role = variant {{ AdminUpdate }}; note = "the gateway" }})',
    )
    assert "Ok" in response, response
    assert "Ok" in call(network, "set_access", "(record { level = 0 : nat16 })")
    response = call(
        network,
        "set_cache_cleanup_config",
        f"(record {{ ttl_seconds = opt ({TTL_48H} : nat64); max_decision_states = opt (99 : nat64); max_decision_states_per_scope = opt (50 : nat64) }})",
    )
    assert "Ok" in response, response
    assert "is_running = true" in call(network, "cache_cleanup_start_timer", "()")
