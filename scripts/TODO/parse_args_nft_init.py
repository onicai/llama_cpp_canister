"""Import command line arguments for the scripts."""

import argparse


def parse_args() -> argparse.Namespace:
    """Returns the command line arguments"""
    parser = argparse.ArgumentParser(description="Initialize the NFT collection")
    parser.add_argument(
        "--network",
        type=str,
        default="local",
        help="Network: ic or local",
    )
    parser.add_argument(
        "--canister",
        type=str,
        default="no-default",
        help="canister name in dfx.json",
    )
    parser.add_argument(
        "--canister-id",
        type=str,
        default="",
        help="canister-id name canister_ids.json",
    )
    parser.add_argument(
        "--candid",
        type=str,
        default="src/llama2.did",
        help="canister's candid file",
    )

    parser.add_argument(
        "--nft-supply-cap",
        type=int,
        default=25,
        help="The max number of NFTs that will ever be minted.",
    )
    parser.add_argument(
        "--nft-symbol",
        type=str,
        default="no-default",
        help="Symbol of the NFT Collection",
    )
    parser.add_argument(
        "--nft-name",
        type=str,
        default="no-default",
        help="Name of the NFT Collection",
    )
    parser.add_argument(
        "--nft-description",
        type=str,
        default=("no-default"),
        help="Description of the NFT Collection",
    )

    args = parser.parse_args()
    return args
