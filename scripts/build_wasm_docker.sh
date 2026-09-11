#!/bin/bash
# Builds llama_cpp.wasm inside the reproducible Docker image.
#
# This is the `wasm` service's command; see docker/docker-compose.yml. Do not
# run it on a host -- it writes to /app/llama_cpp_canister and assumes the
# pinned toolchain the base image provides.
#
# Mirrors PoAIW/src/*/scripts/build.sh: build, copy to out/, print the sha256.
set -euo pipefail

# Determinism knobs are set in the base image (LC_ALL, PYTHONHASHSEED, TZ,
# SOURCE_DATE_EPOCH); re-assert LC_ALL here because the link step's `*.o` glob
# order depends on it and a stray override would silently change the hash.
export LC_ALL=C

cd /app/llama_cpp_canister

echo "=============================================="
echo "fork repo         : ${FORK_REPO}"
echo "fork commit       : ${FORK_COMMIT}"
echo "fork commit short : ${FORK_COMMIT_SHORT}"
echo "fork build number : ${FORK_BUILD_NUMBER}"
echo "icpp-pro          : $(icpp --version | tr '\n' ' ')"
echo "=============================================="

# build-info.cpp is compiled INTO the wasm. Pass the pinned values in rather
# than letting build-info-cpp.sh derive them from the clone: this image fetches
# the fork at --depth 1, where `git rev-list --count` reports 1 and
# `git rev-parse --short` may pick a different abbreviation length.
export BUILD_NUMBER="${FORK_BUILD_NUMBER}"
export BUILD_COMMIT="${FORK_COMMIT_SHORT}"
make build-info-cpp-wasm

# --generate-bindings no: the JS bindings need didc, are written to
# src/declarations, and have no effect on the wasm. Skipped so the build has one
# less optional tool in it.
icpp build-wasm --generate-bindings no

mkdir -p out
cp build/llama_cpp.wasm out/
cp build/llama_cpp.did  out/

# The release zip ships the fork's model-conversion requirements, and the fork
# only exists inside this image.
mkdir -p out/fork
cp    src/llama_cpp_onicai_fork/requirements.txt out/fork/
cp -r src/llama_cpp_onicai_fork/requirements     out/fork/

# Record what produced these bytes, right next to them.
sha256sum out/llama_cpp.wasm | cut -d ' ' -f 1 > out/llama_cpp.wasm.sha256
cat > out/BUILD-PROVENANCE.txt <<PROVENANCE
wasm_sha256=$(cat out/llama_cpp.wasm.sha256)
fork_repo=${FORK_REPO}
fork_commit=${FORK_COMMIT}
fork_build_number=${FORK_BUILD_NUMBER}
icpp_pro=$(icpp --version | tr '\n' ' ')
wasi_sdk_clang=$("$HOME"/.icpp/wasi-sdk/wasi-sdk-25.0/bin/clang --version | head -1)
PROVENANCE

# The bind mount is owned by root inside the container; make the artifacts
# readable to the host user who will hash them.
chmod -R a+rX out

echo "Wasm hash:"
sha256sum out/llama_cpp.wasm
