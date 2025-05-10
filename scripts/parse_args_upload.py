"""Import command line arguments for the scripts."""

import argparse
from pathlib import Path

SCRIPT_PATH = Path(__file__).parent


def parse_args() -> argparse.Namespace:
    """Returns the command line arguments"""
    parser = argparse.ArgumentParser(description="Upload a file")
    parser.add_argument(
        "local-filename",
        type=str,
        help="Local filename to upload",
    )
    parser.add_argument(
        "--filetype",
        type=str,
        default="other",
        choices=["promptcache", "gguf", "wasm", "log", "other"],
        help="Type of file to download: promptcache, gguf, wasm, log, or other",
    )
    parser.add_argument(
        "--canister-filename",
        type=str,
        help="Canister filename",
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
        default=SCRIPT_PATH / "../build/llama_cpp.did",
        help="canister's candid file",
    )
    parser.add_argument(
        "--chunksize",
        type=int,
        default=2000000,
        help="Chunk Size used during file upload, in bytes",
    )
    parser.add_argument(
        "--hf-sha256",
        type=str,
        default=None,
        help="Optional - provides the HuggingFace Hash, to check against.",
    )

    args = parser.parse_args()
    return args
