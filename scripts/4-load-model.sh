#!/bin/bash

#######################################################################
# run from parent folder as:
# scripts/load-model.sh --network [local|ic]
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

#######################################################################
echo " "
echo "==================================================="
echo "Loading model"
echo " "
echo "--------------------------------------------------"
echo "Checking health endpoint for llama_cpp"
output=$(dfx canister call llama_cpp health --network $NETWORK_TYPE )

if [ "$output" != "(variant { Ok = record { status_code = 200 : nat16 } })" ]; then
    echo "llama_cpp health check failed."
    echo $output
    exit 1
else
    echo "llama_cpp health check succeeded."
fi

echo " "
echo "--------------------------------------------------"
echo "Calling load_model for llama_cpp"
output=$(dfx canister call llama_cpp load_model \
        '(record { args = vec {"--model"; "models/model.gguf"; "--no-warmup";} })' \
        --network "$NETWORK_TYPE")

if ! echo "$output" | grep -q " Ok "; then
    echo "llama_cpp load_model failed."
    echo $output
    exit 1
else
    echo "llama_cpp load_model succeeded."
    echo 🎉
fi