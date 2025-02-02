#!/bin/bash

echo " "
echo "--------------------------------------------------"
echo "Building the wasm for llama_cpp_canister"
make build-info-cpp-wasm
# icpp build-wasm
icpp build-wasm --to-compile mine-no-lib