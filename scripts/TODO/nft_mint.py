"""Mint an NFT owned by a bitcoin ordinal, from a toml file

Run with:

    python -m scripts.nft_mint --network ic --canister <canister-name> --nft-config <path-to-your-nft-config-toml-file> --token-ids <path-to-the-token-ids-toml-file>   # pylint: disable=line-too-long
"""

# pylint: disable=invalid-name, too-few-public-methods, no-member, too-many-statements, broad-except

import sys
from pathlib import Path
from pprint import pprint
from typing import Dict, Any
from .ic_py_canister import get_canister
from .parse_args_nft_mint import parse_args
from .nft_config import read_toml

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

    nft_config_path = Path(args.nft_config)
    token_ids_path = Path(args.token_ids)

    dfx_json_path = ROOT_PATH / "dfx.json"

    print(
        f"Summary of canister & NFT Collection:"
        f"\n - network                  = {network}"
        f"\n - canister                 = {canister_name}"
        f"\n - canister_id              = {canister_id}"
        f"\n - dfx_json_path            = {dfx_json_path}"
        f"\n - candid_path              = {candid_path}"
        f"\n - nft_config_path          = {nft_config_path}"
        f"\n - token_ids_path           = {token_ids_path}"
    )

    nft_config: Dict[Any, Any] = read_toml(nft_config_path)
    token_ids: Dict[Any, Any] = read_toml(token_ids_path)

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
    # check readiness for inference
    print("--\nChecking if the canister is ready for inference.")
    response = canister_llama2.ready()
    if "Ok" in response[0].keys():
        if DEBUG_VERBOSE >= 2:
            print("OK!")
    else:
        print("Something went wrong:")
        print(response)
        sys.exit(1)

    # ---------------------------------------------------------------------------
    # Mint the NFT
    #
    nft_id = nft_config["nft_id"]

    NFT = {}
    NFT["token_id"] = token_ids["token_ids"][str(nft_id)]

    print(f'--\nMinting NFT for token_id  = {NFT["token_id"]}')
    error: bool = False
    try:
        response = canister_llama2.nft_mint(NFT)
        print(response)
        if "Ok" in response[0].keys():
            if DEBUG_VERBOSE >= 2:
                print("OK!")
        else:
            error = True
    except Exception as e:
        print(f"An error occurred: {e}")
        if "already exists" in str(e):
            print("Accepting this error, will continue...")
        else:
            error = True

    if error:
        print("Something went wrong")
        user_input = input("Do you still want to continue? [yes/no] ")
        if user_input.lower() not in ("yes", "y"):
            sys.exit(1)

    # ---------------------------------------------------------------------------
    # Summarize the NFT Collection
    print("--\nSummary of the NFT Collection.")
    response = canister_llama2.nft_metadata()
    pprint(response[0])

    # ---------------------------------------------------------------------------
    return 0


if __name__ == "__main__":
    sys.exit(main())
