#!/bin/bash

#######################################################################
# run from parent folder as:
# scripts/load-model.sh --network [local|production]
#######################################################################

# Default network type is local
NETWORK_TYPE="local"

# MAX_TOKENS=128 # stories260Ktok512.gguf
# MAX_TOKENS=60 # stories15Mtok4096.gguf
# MAX_TOKENS=20 # SmolLM2-135M-Instruct-Q4_K_M.gguf
MAX_TOKENS=10 # qwen2.5-0.5b-instruct-q8_0.gguf
# MAX_TOKENS=2 # DeepSeek-R1-Distill-Qwen-1.5B-Q2_K.gguf

# Parse command line arguments for network type
while [ $# -gt 0 ]; do
    case "$1" in
        --network)
            shift
            if [ "$1" = "local" ] || [ "$1" = "production" ]; then
                NETWORK_TYPE=$1
            else
                echo "Invalid network type: $1. Use 'local' or 'production'."
                exit 1
            fi
            shift
            ;;
        *)
            echo "Unknown argument: $1"
            echo "Usage: $0 --network [local|production]"
            exit 1
            ;;
    esac
done

echo "Using network type: $NETWORK_TYPE"

#######################################################################
echo " "
echo "==================================================="
echo "set_max_tokens to $MAX_TOKENS for llama_cpp"

echo " "
echo "--------------------------------------------------"
echo "Checking health endpoint for llama_cpp"
output=$(icp canister call llama_cpp health -e $NETWORK_TYPE --query )

if [ "$output" != "(variant { Ok = record { status_code = 200 : nat16 } })" ]; then
    echo "llama_cpp health check failed"
    echo $output
    exit 1
else
    echo "llama_cpp health check succeeded."
fi

echo " "
echo "--------------------------------------------------"
echo "Setting max tokens to ($MAX_TOKENS) for llama_cpp"
output=$(icp canister call llama_cpp set_max_tokens \
        '(record { max_tokens_query = '"$MAX_TOKENS"' : nat64; max_tokens_update = '"$MAX_TOKENS"' : nat64 })' \
        --network "$NETWORK_TYPE")


if [ "$output" != "(variant { Ok = record { status_code = 200 : nat16 } })" ]; then
    echo "llama_cpp set_max_tokens failed."
    echo $output
    exit 1
else
    echo "llama_cpp set_max_tokens to $MAX_TOKENS succeeded."
    echo 🎉
fi