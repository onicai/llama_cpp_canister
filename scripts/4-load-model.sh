#!/bin/bash

#######################################################################
# run from parent folder as:
# scripts/load-model.sh --network [local|ic]
#######################################################################

# Default network type is local
NETWORK_TYPE="local"
NUM_LLMS_DEPLOYED=1

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
echo "Loading model for $NUM_LLMS_DEPLOYED llms"
llm_id_start=0
llm_id_end=$((NUM_LLMS_DEPLOYED - 1))

for i in $(seq $llm_id_start $llm_id_end)
do
    echo " "
    echo "--------------------------------------------------"
    echo "Checking health endpoint for llm_$i"
    output=$(dfx canister call llm_$i health --network $NETWORK_TYPE )

    if [ "$output" != "(variant { Ok = record { status_code = 200 : nat16 } })" ]; then
        echo "llm_$i health check failed. Exiting."
        echo $output
        echo "****************************************************************"
        echo "llm_$i health check failed. Exiting."
        echo "****************************************************************"
        exit 1
    else
        echo "llm_$i health check succeeded."
    fi

    echo " "
    echo "--------------------------------------------------"
    echo "Calling load_model for llm_$i"
    output=$(dfx canister call llm_$i load_model \
            '(record { args = vec {"--model"; "models/model.gguf";} })' \
            --network "$NETWORK_TYPE")

    if ! echo "$output" | grep -q " Ok "; then
        echo "llm_$i load_model failed. Exiting."
        echo $output
        echo "****************************************************************"
        echo "llm_$i load_model failed. Exiting."
        echo "****************************************************************"
        exit 1
    else
        echo "llm_$i load_model succeeded."
        echo 🎉
    fi
done