#!/bin/bash

#######################################################################
# run from parent folder as:
# scripts/monitor_logs.sh --network [local|production] [more options of scripts/monitor_logs.py]
#######################################################################

# Default network type is local
NETWORK_TYPE="local"
EXTRA_ARGS=()

# Parse command line arguments for network type; pass the rest on
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
            EXTRA_ARGS+=("$1")
            shift
            ;;
    esac
done

echo "Using network type: $NETWORK_TYPE"

python -m scripts.monitor_logs --network "$NETWORK_TYPE" "${EXTRA_ARGS[@]}"
