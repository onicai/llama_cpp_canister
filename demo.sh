#!/bin/sh

#######################################################################
# This is a Linux & Mac shell script
#
# (-) Install icpp-pro or icpp-free in a python environment
# (-) Install dfx
# (-) In a terminal:
#
#     ./demo.sh
#
#######################################################################
echo " "
echo "--------------------------------------------------"
echo "Stopping the local network"
dfx stop

echo " "
echo "--------------------------------------------------"
echo "Starting the local network as a background process"
dfx start --clean --background

#######################################################################
echo "--------------------------------------------------"
echo "Building the wasm with wasi-sdk"
icpp build-wasm --to-compile all
# icpp build-wasm --to-compile mine-no-lib

#######################################################################
echo " "
echo "--------------------------------------------------"
echo "Deploying the wasm to a canister on the local network"
dfx deploy

#######################################################################
echo " "
echo "--------------------------------------------------"
echo "Uploading the *.gguf model file"
python -m scripts.upload models/stories260Ktok512.gguf
# python -m scripts.upload models/stories15Mtok4096.gguf
# python -m scripts.upload ../../repos_hf/Phi-3-mini-4k-instruct-gguf/Phi-3-mini-4k-instruct-q4.gguf --canister-file models/Phi-3-mini-4k-instruct-q4.gguf

#######################################################################
echo " "
echo "--------------------------------------------------"
echo "Running some manual tests with dfx"
dfx canister call llama_cpp run_query '(record { args = vec {"--model"; "models/stories260Ktok512.gguf"; "--prompt"; "Patrick loves ice-cream. On a hot day "; "--n-predict"; "25"; "--ctx-size"; "128"} })'
dfx canister call llama_cpp run_update '(record { args = vec {"--model"; "models/stories260Ktok512.gguf"; "--prompt"; "Patrick loves ice-cream. On a hot day "; "--n-predict"; "25"; "--ctx-size"; "128"} })'

#######################################################################
echo " "
echo "--------------------------------------------------"
echo "Running the full smoketests with pytest"
pytest -vv --network=local

#######################################################################
echo "--------------------------------------------------"
echo "Stopping the local network"
dfx stop

#######################################################################
echo " "
echo "--------------------------------------------------"
echo "Building the OS native debug executable with clang++"
icpp build-native --to-compile all
# icpp build-native --to-compile mine

#######################################################################
echo " "
echo "--------------------------------------------------"
echo "Running the OS native debug executable"
./build-native/mockic.exe