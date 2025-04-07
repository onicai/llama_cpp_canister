#!/bin/bash

#######################################################################
# run from parent folder as:
# scripts/deploy.sh --mode [install/$DEPLOY_MODE/upgrade] [--network ic]
#######################################################################

# Default network type is local
NETWORK_TYPE="local"

DEPLOY_MODE="install"

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
        --mode)
            shift
            if [ "$1" = "install" ] || [ "$1" = "$DEPLOY_MODE" ] || [ "$1" = "upgrade" ]; then
                DEPLOY_MODE=$1
            else
                echo "Invalid mode: $1. Use 'install', '$DEPLOY_MODE' or 'upgrade'."
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
echo "--------------------------------------------------"
echo "Deploying the wasm to llama_cpp"
if [ "$NETWORK_TYPE" = "ic" ]; then
    if [ "$SUBNET" = "none" ]; then
        yes | dfx deploy llama_cpp -m $DEPLOY_MODE --yes --network $NETWORK_TYPE
    else
        yes | dfx deploy llama_cpp -m $DEPLOY_MODE --yes --network $NETWORK_TYPE --subnet $SUBNET
    fi
else
    yes | dfx deploy llama_cpp -m $DEPLOY_MODE --yes --network $NETWORK_TYPE
fi 

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
    echo 🎉
fi
