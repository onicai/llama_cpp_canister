"""Import command line arguments for the scripts."""

import argparse


def parse_args() -> argparse.Namespace:
    """Returns the command line arguments"""
    parser = argparse.ArgumentParser(description="Upload a file")
    parser.add_argument(
        "local-filename",
        type=str,
        help="Local filename to upload",
    )
    parser.add_argument(
        "--canister-filename",
        type=str,
        default=None,
        help="Canister filename. Defaults to local filename",
    )
    parser.add_argument(
        "--network",
        type=str,
        default="local",
        help="Network: ic or local",
    )
    parser.add_argument(
        "--canister",
        type=str,
        default="llama_cpp",
        help="canister name in dfx.json",
    )
    parser.add_argument(
        "--canister-id",
        type=str,
        default="",
        help="canister-id from canister_ids.json; Overrules --canister",
    )
    parser.add_argument(
        "--candid",
        type=str,
        default="src/llama_cpp.did",
        help="canister's candid file",
    )
    parser.add_argument(
        "--chunksize",
        type=int,
        default=2000000,
        help="Chunk Size used during file download, in bytes",
    )

    args = parser.parse_args()
    return args
