#!/bin/bash

#######################################################################
# run from parent folder as:
# scripts/test.sh --network [local|ic]
#######################################################################

# Default network type is local
NETWORK_TYPE="local"
i=0 # llm_$i will be tested

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
echo "Calling run_update for llm_$i"
dfx canister call llm_$i run_update '(record { args = vec {"--prompt-cache"; "prompt.cache"; "--prompt-cache-all"; "-sp"; "-p"; ""; "-n"; "512" } })'
