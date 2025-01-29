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
echo "Checking health endpoint for llm_$i"
output=$(dfx canister call llm_$i health --network $NETWORK_TYPE )

if [ "$output" != "(variant { Ok = record { status_code = 200 : nat16 } })" ]; then
    echo "llm_$i health check failed."
    echo $output
    exit 1
else
    echo "llm_$i health check succeeded."
fi

echo " "
echo "--------------------------------------------------"
echo "Calling new_chat for llm_$i"
dfx canister call llm_$i new_chat '(record { args = vec {"--prompt-cache"; "prompt.cache"} })' --network $NETWORK_TYPE