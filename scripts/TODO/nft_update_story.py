"""Update an NFT owned by a bitcoin ordinal, from a toml file

Run with:

    python -m scripts.nft_update_story --network ic --canister <canister-name> --nft-config <path-to-your-nft-config-toml-file> --token-ids <path-to-the-token-ids-toml-file>   # pylint: disable=line-too-long
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
        f"\n - network         = {network}"
        f"\n - canister        = {canister_name}"
        f"\n - canister_id     = {canister_id}"
        f"\n - dfx_json_path   = {dfx_json_path}"
        f"\n - candid_path     = {candid_path}"
        f"\n - nft_config_path = {nft_config_path}"
        f"\n - token_ids_path  = {token_ids_path}"
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
    # Start the story
    nft_id = nft_config["nft_id"]

    NFT = {}
    NFT["token_id"] = token_ids["token_ids"][str(nft_id)]

    prompt = nft_config["prompt"]
    print(
        "--\nGenerating a story with:"
        f"\n - token_id     = {NFT['token_id']}"
        f"\n - prompt                 = {prompt['prompt']}"
        f"\n - temperature            = {prompt['temperature']}"
        f"\n - topp                   = {prompt['topp']}"
        f"\n - steps                  = {prompt['steps']}"
        f"\n - rng_seed               = {prompt['rng_seed']}"
    )

    use_full_prompt = True

    if use_full_prompt:
        response = canister_llama2.nft_story_start(NFT, prompt)
        print(response)
        if "Ok" in response[0].keys():
            if DEBUG_VERBOSE >= 2:
                print("OK!")
        else:
            print("Something went wrong:")
            sys.exit(1)
    else:
        words = prompt["prompt"].split()  # Splitting the initial prompt into words

        # First, use nft_story_start for the first word
        first_word = words[0]
        print(f"--\nStarting a new story with the first word: {first_word}")
        prompt["prompt"] = first_word  # Update the prompt with the first word
        response = canister_llama2.nft_story_start(NFT, prompt)
        print(response)
        if "Ok" in response[0].keys():
            if DEBUG_VERBOSE >= 2:
                print("OK!")
        else:
            print("Something went wrong:")
            sys.exit(1)

        # ---------------------------------------------------------------------------
        # Then, use nft_story_continue for the rest of the words
        for word in words[1:]:  # Starting from the second word
            print(f"--\nContinuing the story with the word: {word}")
            prompt["prompt"] = word  # Update the prompt with the current word
            response = canister_llama2.nft_story_continue(NFT, prompt)
            print(response)
            if "Ok" in response[0].keys():
                if DEBUG_VERBOSE >= 2:
                    print("OK!")
            else:
                print("Something went wrong:")
                sys.exit(1)

        response = canister_llama2.nft_story_start(NFT, prompt)
        print(response)
        if "Ok" in response[0].keys():
            if DEBUG_VERBOSE >= 2:
                print("OK!")
        else:
            print("Something went wrong:")
            sys.exit(1)

    # ---------------------------------------------------------------------------
    # Continue the story with empty prompt until we reached the end.

    print("--\nContinuing the story until done...")

    prompt["prompt"] = ""
    while True:
        response = canister_llama2.nft_story_continue(NFT, prompt)
        print(response)
        if "Ok" in response[0].keys():
            # Check if the number of generated tokens is less than the requested tokens
            if response[0]["Ok"]["num_tokens"] < prompt['steps']:
                print(f'The end! - num_tokens = {response[0]["Ok"]["num_tokens"]}')
                break
            # Check if the response is an empty string. If it is, break out of the loop.
            if response[0]["Ok"]["inference"] == "":
                print("The end! - we got an empty string. THIS IS AN ERROR ACTUALLY. WE SHOULD NOT GET HERE..")
                print("Something went wrong:")
                sys.exit(1)
        else:
            print("Something went wrong:")
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
