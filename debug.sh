#!/bin/sh

#######################################################################
echo "--------------------------------------------------"
echo "Building the wasm with wasi-sdk"
# icpp build-wasm --to-compile all
icpp build-wasm --to-compile mine-no-lib

#######################################################################
echo " "
echo "--------------------------------------------------"
echo "Deploying the wasm to a canister on the local network"
dfx deploy

#######################################################################
echo " "
echo "--------------------------------------------------"
echo "Uploading the *.gguf model file"
# python -m scripts.upload models/stories260Ktok512.gguf
python -m scripts.upload models/stories15Mtok4096.gguf
# python -m scripts.upload ../../repos_hf/Phi-3-mini-4k-instruct-gguf/Phi-3-mini-4k-instruct-q4.gguf --canister-file models/Phi-3-mini-4k-instruct-q4.gguf

#######################################################################
echo " "
echo "--------------------------------------------------"
echo "Running some manual tests with dfx"
# dfx canister call llama_cpp run_query '(record { args = vec {"--model"; "models/stories260Ktok512.gguf"; "--prompt"; "Patrick loves ice-cream. On a hot day "; "--n-predict"; "25"; "--ctx-size"; "128"; "--verbose-prompt"} })'
# dfx canister call llama_cpp run_update '(record { args = vec {"--model"; "models/stories260Ktok512.gguf"; "--prompt"; "Patrick loves ice-cream. On a hot day "; "--n-predict"; "25"; "--ctx-size"; "128"; "--verbose-prompt"} })'
# dfx canister call llama_cpp run_query '(record { args = vec {"--model"; "models/stories15Mtok4096.gguf"; "--prompt"; "Patrick loves ice-cream. On a hot day "; "--n-predict"; "25"; "--ctx-size"; "128"; "--verbose-prompt"} })'
dfx canister call llama_cpp run_update '(record { args = vec {"--model"; "models/stories15Mtok4096.gguf"; "--prompt"; "Patrick loves ice-cream. On a hot day "; "--n-predict"; "25"; "--ctx-size"; "128"; "--verbose-prompt"} })'

#######################################################################
# echo " "
# echo "--------------------------------------------------"
# echo "Running the full smoketests with pytest"
# pytest -vv --network=local