"""Download a file and write it to disk

Run with:

    python -m scripts.download [options] FILENAME

    python -m scripts.download --help

"""

# pylint: disable=invalid-name, too-few-public-methods, no-member, too-many-statements, line-too-long

import sys
from pathlib import Path
from typing import List
from .ic_py_canister import get_canister, run_dfx_command
from .parse_args_download import parse_args
from .calculate_sha256 import calculate_sha256

ROOT_PATH = Path(__file__).parent.parent

#  0 - none
#  1 - minimal
#  2 - a lot
DEBUG_VERBOSE = 1


def main() -> int:
    """Downloads a file from the canister and writes it to disk."""

    args = parse_args()

    canister_filename = args.__dict__["canister-filename"]

    filetype = args.filetype
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

    if canister_id == "":
        canister_id = run_dfx_command(
            f"dfx canister --network {network} id {canister_name} "
        )

    print(
        f"Summary:"
        f"\n - canister_filename   = {canister_filename}"
        f"\n - filetype            = {filetype}"
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
    with open(local_filename_path, "wb") as f:
        while not done:
            if filetype == "promptcache":
                response = canister_instance.download_prompt_cache_chunk(
                    {
                        "promptcache": canister_filename,
                        "chunksize": chunksize,
                        "offset": offset,
                    }
                )
            else:
                response = canister_instance.file_download_chunk(
                    {
                        "filename": canister_filename,
                        "chunksize": chunksize,
                        "offset": offset,
                    }
                )

            if "Ok" in response[0].keys():
                r_filesize = response[0]["Ok"]["filesize"]
                r_chunk: List[int] = response[0]["Ok"]["chunk"]
                r_chunksize = response[0]["Ok"]["chunksize"]
                r_offset = response[0]["Ok"]["offset"]
                total_received = offset + len(r_chunk)
                print(
                    "--"
                    "\nDownloaded a chunk:"
                    f"\n- filesize       = {r_filesize} bytes ({r_filesize/1024/1024:.3f} Mb), "
                    f"\n- len(chunk)     = {len(r_chunk)} bytes ({len(r_chunk)/1024/1024:.3f} Mb), "
                    f"\n- chunksize      = {r_chunksize} bytes ({r_chunksize/1024/1024:.3f} Mb), "
                    f"\n- offset         = {r_offset} bytes ({r_offset/1024/1024:.3f} Mb), "
                    f"\n- total_received = {total_received} bytes ({total_received/1024/1024:.3f} Mb)"
                )
                offset += len(r_chunk)

                f.write(bytearray(r_chunk))

                done = response[0]["Ok"]["done"]
            else:
                print("Something went wrong:")
                print(response)
                sys.exit(1)

    # ---------------------------------------------------------------------------
    local_file_sha256 = calculate_sha256(local_filename_path)
    local_file_size = local_filename_path.stat().st_size

    # ---------------------------------------------------------------------------
    print(
        f"--\nCongratulations, the file was succesfully downloaded to "
        f"{local_filename_path}!"
    )
    print(f"Filesize    : {local_file_size} bytes ({local_file_size/1024/1024:.3f} Mb)")
    print(f"SHA256 hash : {local_file_sha256}")

    try:
        print("💯 🎉 🏁")
    except UnicodeEncodeError:
        print(" ")

    # ---------------------------------------------------------------------------
    return 0


if __name__ == "__main__":
    sys.exit(main())
