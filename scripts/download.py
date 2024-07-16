"""Download a file and write it to disk

Run with:

    python -m scripts.download FILENAME

    python -m scripts.download --help

"""

# pylint: disable=invalid-name, too-few-public-methods, no-member, too-many-statements

import sys
from pathlib import Path
from typing import List
from .ic_py_canister import get_canister
from .parse_args_download import parse_args

ROOT_PATH = Path(__file__).parent.parent

#  0 - none
#  1 - minimal
#  2 - a lot
DEBUG_VERBOSE = 1


def main() -> int:
    """Downloads a file from the canister and writes it to disk."""

    args = parse_args()

    canister_filename = args.__dict__["canister-filename"]

    network = args.network
    canister_name = args.canister
    canister_id = args.canister_id
    candid_path = ROOT_PATH / args.candid
    if args.local_filename is not None:
        local_filename_path = ROOT_PATH / args.local_filename
    else:
        local_filename_path = ROOT_PATH / canister_filename
    chunksize = args.chunksize

    dfx_json_path = ROOT_PATH / "dfx.json"

    print(
        f"Summary:"
        f"\n - canister_filename   = {canister_filename}"
        f"\n - local_filename_path = {local_filename_path}"
        f"\n - chunksize           = {chunksize} ({chunksize/1024/1024:.3f} Mb)"
        f"\n - network             = {network}"
        f"\n - canister            = {canister_name}"
        f"\n - canister_id         = {canister_id}"
        f"\n - dfx_json_path       = {dfx_json_path}"
        f"\n - candid_path         = {candid_path}"
    )

    # ---------------------------------------------------------------------------
    # get ic-py based Canister instance
    canister_instance = get_canister(canister_name, candid_path, network, canister_id)

    # check health (liveness)
    print("--\nChecking liveness of canister (did we deploy it!)")
    response = canister_instance.health()
    if "Ok" in response[0].keys():
        print("Ok!")
    else:
        print("Not OK, response is:")
        print(response)

    # ---------------------------------------------------------------------------
    # DOWNLOAD FILE

    # Download bytes from the canister and write it to local disk
    print(f"--\nDownloading the file: {canister_filename}")
    print(f"--\nSaving to: {local_filename_path}")

    done = False
    offset = 0
    with open(local_filename_path, "ab") as f:
        while not done:
            response = canister_instance.file_download_chunk(
                {
                    "filename": canister_filename,
                    "chunksize": chunksize,
                    "offset": offset,
                }
            )
            if "Ok" in response[0].keys():
                chunk: List[int] = response[0]["Ok"]["chunk"]
                offset += len(chunk)
                print(
                    f"filesize, chunksize, total_received: "
                    f"{response[0]['Ok']['filesize']}, {len(chunk)}, {offset} "
                )

                f.write(bytearray(chunk))

                done = response[0]["Ok"]["done"]
            else:
                print("Something went wrong:")
                print(response)
                sys.exit(1)

    # ---------------------------------------------------------------------------
    print(
        f"--\nCongratulations, the file was succesfully downloaded to "
        f"{local_filename_path}!"
    )
    try:
        print("💯 🎉 🏁")
    except UnicodeEncodeError:
        print(" ")

    # ---------------------------------------------------------------------------
    return 0


if __name__ == "__main__":
    sys.exit(main())
