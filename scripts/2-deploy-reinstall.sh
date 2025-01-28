#!/bin/bash

#######################################################################
# run from parent folder as:
# scripts/deploy-reinstall.sh --network [local|ic]
#######################################################################

# Default network type is local
NETWORK_TYPE="local"
NUM_LLMS_DEPLOYED=1

# When deploying to IC, we deploy to a specific subnet
# none will not use subnet parameter in deploy to ic
SUBNET="none"
# SUBNET="-------"

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
echo "Deploying $NUM_LLMS_DEPLOYED llms to subnet $SUBNET"
llm_id_start=0
llm_id_end=$((NUM_LLMS_DEPLOYED - 1))

for i in $(seq $llm_id_start $llm_id_end)
do
    echo "--------------------------------------------------"
    echo "Deploying the wasm to llm_$i"
    if [ "$NETWORK_TYPE" = "ic" ]; then
        if [ "$SUBNET" = "none" ]; then
            yes | dfx deploy llm_$i -m reinstall --yes --network $NETWORK_TYPE
        else
            yes | dfx deploy llm_$i -m reinstall --yes --network $NETWORK_TYPE --subnet $SUBNET
        fi
    else
        yes | dfx deploy llm_$i -m reinstall --yes --network $NETWORK_TYPE
    fi 
    
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
        echo 🎉
    fi
done