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
            if [ "$1" = "local" ] || [ "$1" = "production" ]; then
                NETWORK_TYPE=$1
            else
                echo "Invalid network type: $1. Use 'local' or 'production'."
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
            echo "Usage: $0 --network [local|production]"
            exit 1
            ;;
    esac
done

echo "Using network type: $NETWORK_TYPE"

# A canister's controller is whoever deployed it, and icpp-pro >= 6.0.0 runs the
# tests as ${ICPP_PRO_TEST_IDENTITY} instead of the machine-wide active identity.
# So deploy as that identity whenever it is set, and leave the deploy on the
# active identity (the normal interactive case) when it is not.
IDENTITY_ARGS=()
if [ -n "${ICPP_PRO_TEST_IDENTITY:-}" ]; then
    IDENTITY_ARGS=(--identity "$ICPP_PRO_TEST_IDENTITY")
    echo "Using identity: $ICPP_PRO_TEST_IDENTITY"
fi

#######################################################################
echo "--------------------------------------------------"
echo "Deploying the wasm to llama_cpp"
if [ "$NETWORK_TYPE" = "production" ]; then
    if [ "$SUBNET" = "none" ]; then
        icp deploy llama_cpp -m $DEPLOY_MODE -y -e $NETWORK_TYPE "${IDENTITY_ARGS[@]}"
    else
        icp deploy llama_cpp -m $DEPLOY_MODE -y -e $NETWORK_TYPE --subnet $SUBNET "${IDENTITY_ARGS[@]}"
    fi
else
    icp deploy llama_cpp -m $DEPLOY_MODE -y -e $NETWORK_TYPE "${IDENTITY_ARGS[@]}"
fi

echo " "
echo "--------------------------------------------------"
echo "Checking health endpoint for llama_cpp"
output=$(icp canister call llama_cpp health -e $NETWORK_TYPE --query )

if [ "$output" != "(variant { Ok = record { status_code = 200 : nat16 } })" ]; then
    echo "llama_cpp health check failed."
    echo $output        
    exit 1
else
    echo "llama_cpp health check succeeded."
    echo 🎉
fi
