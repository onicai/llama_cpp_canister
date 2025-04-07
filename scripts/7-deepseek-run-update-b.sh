#!/bin/bash

#######################################################################
# run from parent folder as:
# scripts/test.sh --network [local|ic]
#######################################################################

# Default network type is local
NETWORK_TYPE="local"

# Parse command line arguments for network type
while [ $# -gt 0 ]; do
    case "$1" in
        --network)
            shift
            if [ "$1" = "local" ] || [ "$1" = "ic" ]; then
                NETWORK_TYPE=$1
            else
                echo "Invalid network type: $1. Use 'local' or 'ic'."
                exit 1
            fi
            shift
            ;;
        *)
            echo "Unknown argument: $1"
            echo "Usage: $0 --network [local|ic]"
            exit 1
            ;;
    esac
done

echo "Using network type: $NETWORK_TYPE"

echo " "
echo "--------------------------------------------------"
echo "Calling run_update for llama_cpp"
dfx canister call llama_cpp run_update '(record { args = vec {"--cache-type-k"; "q8_0"; "--no-warmup"; "--prompt-cache"; "prompt.cache"; "--prompt-cache-all"; "-sp"; "-p"; "";} })'
