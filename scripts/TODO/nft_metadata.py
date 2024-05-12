"""Summarizes the NFT Collection.

Run with:

    python -m scripts.nft_metadata
"""

# pylint: disable=invalid-name, too-few-public-methods, no-member, too-many-statements

import sys
from pathlib import Path
from pprint import pprint
from .ic_py_canister import get_canister
from .parse_args_nft_init import parse_args

ROOT_PATH = Path(__file__).parent.parent

#  0 - none
#  1 - minimal
#  2 - a lot
DEBUG_VERBOSE = 1


def main() -> int:
    """Summarize the NFT Collection."""

    args = parse_args()

    network = args.network
    canister_name = args.canister
    canister_id = args.canister_id
    candid_path = ROOT_PATH / args.candid

    dfx_json_path = ROOT_PATH / "dfx.json"

    print(
        f"Summary of canister & NFT Collection:"
        f"\n - network         = {network}"
        f"\n - canister        = {canister_name}"
        f"\n - canister_id     = {canister_id}"
        f"\n - dfx_json_path   = {dfx_json_path}"
        f"\n - candid_path     = {candid_path}"
    )

    # ---------------------------------------------------------------------------
    # get ic-py based Canister instance
    canister_llama2 = get_canister(canister_name, candid_path, network, canister_id)

    # check health (liveness)
    print("--\nChecking liveness of canister (did we deploy it!)")
    response = canister_llama2.health()
    if "Ok" in response[0].keys():
        print("Ok!")
    else:
        print("Not OK, response is:")
        print(response)

    # ---------------------------------------------------------------------------
    # Summarize the NFT Collection
    print("--\nSummary of the NFT Collection.")
    response = canister_llama2.nft_metadata()
    pprint(response[0])

    # ---------------------------------------------------------------------------
    return 0


if __name__ == "__main__":
    sys.exit(main())
