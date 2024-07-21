"""Uploads a file to the canister

Run with:

    python -m scripts.upload FILENAME

    python -m scripts.upload --help

"""

# pylint: disable=invalid-name, too-few-public-methods, no-member, too-many-statements

import sys
from pathlib import Path
from typing import Generator
from .ic_py_canister import get_canister
from .parse_args_upload import parse_args

ROOT_PATH = Path(__file__).parent.parent

#  0 - none
#  1 - minimal
#  2 - a lot
DEBUG_VERBOSE = 1


def read_file_bytes(file_path: Path) -> bytes:
    """Returns the stories15Mtok4096.bin file as a bytes array"""
    file_bytes = b""
    try:
        with open(file_path, "rb") as file:
            file_bytes = file.read()

    except FileNotFoundError:
        print(f"ERROR: Unable to open the file {file_path}!")
        sys.exit(1)

    return file_bytes


def generate_chunks(data: bytes, chunksize: int) -> Generator[bytes, None, None]:
    """Generator function to iterate over chunks"""
    for i in range(0, len(data), chunksize):
        yield data[i : i + chunksize]


def main() -> int:
    """Uploads a local file to the canister."""

    args = parse_args()

    local_filename_path = ROOT_PATH / args.__dict__["local-filename"]

    network = args.network
    canister_name = args.canister
    canister_id = args.canister_id
    candid_path = ROOT_PATH / args.candid
    if args.canister_filename is not None:
        canister_filename = args.canister_filename
    else:
        canister_filename = args.__dict__["local-filename"]
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
    # UPLOAD FILE

    print(f"--\nUploading the file: {local_filename_path}")
    print(f"--\nTo canister file  : {canister_filename}")

    print(f"--\nReading the file into a bytes object: {local_filename_path}")
    file_bytes = read_file_bytes(local_filename_path)

    # Iterate over all chunks
    offset = 0
    for i, chunk in enumerate(generate_chunks(file_bytes, chunksize)):

        if DEBUG_VERBOSE == 0:
            pass
        elif DEBUG_VERBOSE == 1:
            print(
                f"chunk size = {len(chunk)} "
                f"len(file_bytes) = {len(file_bytes)} "
                f"offset = {offset} bytes "
                f"({((offset+len(chunk)) / len(file_bytes) * 100):.1f}%)"
            )
        else:
            print("+++++++++++++++++++++++++++++++++++++++++++++++++++++")
            print(f"Sending candid for {len(chunk)} bytes :")
            print(f"- i         = {i}")
            print(f"- progress  = {offset+len(chunk) / len(file_bytes) * 100:.1f} % ")
            print(f"- chunk[0]  = {chunk[0]}")
            print(f"- chunk[-1] = {chunk[-1]}")

        response = canister_instance.file_upload_chunk(
            {
                "filename": canister_filename,
                "chunk": chunk,
                "chunksize": chunksize,
                "offset": offset,
            }
        )  # pylint: disable=no-member
        if "Ok" in response[0].keys():
            print(f"OK! filesize = {response[0]['Ok']['filesize']}")
        else:
            print("Something went wrong:")
            print(response)
            sys.exit(1)

        offset += len(chunk)

    # ---------------------------------------------------------------------------
    print(
        f"--\nCongratulations, the file {local_filename_path} was succesfully uploaded!"
    )
    try:
        print("💯 🎉 🏁")
    except UnicodeEncodeError:
        print(" ")

    # ---------------------------------------------------------------------------
    return 0


if __name__ == "__main__":
    sys.exit(main())
