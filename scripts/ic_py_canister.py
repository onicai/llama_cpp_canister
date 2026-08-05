"""Returns the icp-py-core Canister instance, for calling the endpoints."""

import json
import os
import sys
import subprocess
from pathlib import Path
from typing import Any, List, Optional
from icp_core import Agent, Identity, Client, Canister
from icpp.run_shell_cmd import run_shell_cmd

ROOT_PATH = Path(__file__).parent.parent

# We use the `icp` CLI to look up the network URL, the identity's private key,
# and canister ids. (This project migrated from dfx to icp-cli. Unlike dfx, icp
# emits no deprecation banner on stdout, so no output-scrubbing is needed.)
ICP = "icp"

# The wasm QA deploys as ${ICPP_PRO_TEST_IDENTITY} (see the Makefile), and only
# a controller may upload. So when that variable is set, sign as that identity
# rather than as the machine-wide active one - otherwise the upload would be
# rejected by a canister this process just deployed under a different identity.
IDENTITY_ENV_VAR = "ICPP_PRO_TEST_IDENTITY"


def run_icp_command(cmd: str, quiet: bool = False) -> Optional[str]:
    """Runs an `icp` command as a subprocess and returns its stripped stdout."""
    try:
        return run_shell_cmd(cmd, capture_output=True).rstrip("\n")
    except subprocess.CalledProcessError as e:
        if not quiet:
            print(f"Failed icp command: '{cmd}' with error: \n{e.output}")
    return None


def extract_variant(response: List[Any]) -> Any:
    """Extract variant result from icp-py-core response.

    icp-py-core returns: [{'type': 'variant', 'value': {'Ok': {...}}}]
    old ic-py returned:  [{'Ok': {...}}]
    This helper normalizes both formats to {'Ok': {...}} or {'Err': ...}.
    """
    item = response[0]
    if "value" in item:
        return item["value"]
    return item


def get_agent(network: str = "local") -> Agent:
    """Returns an icp-py-core Agent instance.

    `network` is the name of an environment in `icp.yaml` (e.g. "local" or
    "production"). icp-cli assigns the local network a random ephemeral port on
    every start, so the URL is read back from `icp network status` rather than
    hardcoded.
    """

    # Read the network URL from icp (works for both the managed local network
    # and connected networks like mainnet).
    print(f"--\nReading the '{network}' network status...")
    status_json = run_icp_command(f"{ICP} network status -e {network} --json")
    if status_json is None:
        print(f"Error: could not get network status for environment '{network}'.")
        print("If this is the local network, start it first:  icp network start -d")
        sys.exit(1)
    # Strip any trailing slash: icp reports the api_url with one (e.g.
    # "http://localhost:61795/"), but icp-py-core appends "/api/v3/...", which
    # would otherwise produce a "//api/v3" double slash that the replica rejects.
    network_url = json.loads(status_json)["api_url"].rstrip("/")
    print(f"Network URL        = {network_url}")

    # Get the name of the identity to sign as: ${ICPP_PRO_TEST_IDENTITY} when
    # set, else the machine-wide active one (`dfx identity whoami`'s successor).
    identity_whoami = os.environ.get(IDENTITY_ENV_VAR, "").strip() or run_icp_command(
        f"{ICP} identity default"
    )
    print(f"Using identity = {identity_whoami}")

    # Get the private key (PEM) of that identity.
    private_key = run_icp_command(f"{ICP} identity export {identity_whoami}")

    # Create an Identity instance using the private key
    identity = Identity.from_pem(private_key)

    # Create an HTTP client instance for making HTTPS calls to the IC
    # https://smartcontracts.org/docs/interface-spec/index.html#http-interface
    client = Client(url=network_url)

    # Create an IC agent to communicate with IC canisters
    agent = Agent(identity, client)
    return agent


def get_canister(
    canister_name: str,
    candid_path: Path,
    network: str = "local",
    canister_id: Optional[str] = "",
) -> Canister:
    """Returns an icp-py-core Canister instance"""

    agent = get_agent(network=network)

    # Try to get the id of the canister if not provided explicitly.
    # `icp canister status <name> --id-only` reads icp-cli's id store, so it
    # resolves the id even when the network is down. We also provide the option
    # to just pass in the canister_id directly.
    if canister_id == "":
        canister_id = run_icp_command(
            f"{ICP} canister status {canister_name} -e {network} --id-only"
        )
    print(f"Canister ID = {canister_id}")

    # Read canister's candid from file
    with open(
        candid_path,
        "r",
        encoding="utf-8",
    ) as f:
        canister_did = f.read()

    # Create a Canister instance
    return Canister(agent=agent, canister_id=canister_id, candid_str=canister_did)
