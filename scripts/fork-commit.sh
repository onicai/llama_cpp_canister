#!/bin/sh
# Print the short llama_cpp_onicai_fork commit that is compiled INTO the wasm.
#
# Shared by scripts/build-info-cpp.sh and scripts/version-headers.sh so that
# build-info.cpp, ggml-version.h and llama-version.h always agree.
#
# BUILD_COMMIT may be supplied by the caller. The reproducible Docker build does
# exactly that (scripts/build_wasm_docker.sh), because `git rev-parse --short`
# picks its abbreviation length from how many objects the clone holds, so a
# shallow clone can abbreviate the same commit differently. The pinned value
# lives in version_fork.env.

FORK_DIR=$(dirname "$0")/../src/llama_cpp_onicai_fork

if [ -n "${BUILD_COMMIT:-}" ]; then
    printf '%s\n' "$BUILD_COMMIT"
elif out=$(git -C "$FORK_DIR" rev-parse --short HEAD 2>/dev/null); then
    printf '%s\n' "$out"
else
    printf 'unknown\n'
fi
