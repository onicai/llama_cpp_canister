#!/bin/sh
# Generate ggml/src/ggml-version.h and src/llama-version.h in the fork.
#
# Upstream llama.cpp (since the switch to semantic versions, v0.x) includes
# these headers from ggml.c and llama.cpp and generates them via CMake
# configure_file() from the *.h.in templates. icpp-pro does not use CMake, so we
# fill in the templates here, with the same values CMake would use:
#   GGML_VERSION  = GGML_VERSION_{MAJOR,MINOR,PATCH} from ggml/CMakeLists.txt
#   LLAMA_VERSION = LLAMA_VERSION_{MAJOR,MINOR,PATCH} from CMakeLists.txt, plus
#                   "-dev" (LLAMA_BUILD_IS_DEV defaults to ON)
#   *_COMMIT      = the fork commit (scripts/fork-commit.sh)
#
# Usage: sh scripts/version-headers.sh

set -e

SCRIPTS_DIR=$(dirname "$0")
FORK_DIR=$SCRIPTS_DIR/../src/llama_cpp_onicai_fork

commit=$(sh "$SCRIPTS_DIR/fork-commit.sh")

# Read "set(<PREFIX>_VERSION_<PART> <n>)" from a CMakeLists.txt
cmake_version() {
    file=$1
    prefix=$2
    major=$(sed -n "s/^set(${prefix}_VERSION_MAJOR \([0-9]*\))/\1/p" "$file")
    minor=$(sed -n "s/^set(${prefix}_VERSION_MINOR \([0-9]*\))/\1/p" "$file")
    patch=$(sed -n "s/^set(${prefix}_VERSION_PATCH \([0-9]*\))/\1/p" "$file")
    if [ -z "$major" ] || [ -z "$minor" ] || [ -z "$patch" ]; then
        echo "version-headers.sh: no ${prefix}_VERSION_{MAJOR,MINOR,PATCH} in $file" >&2
        exit 1
    fi
    printf '%s.%s.%s\n' "$major" "$minor" "$patch"
}

ggml_version=$(cmake_version "$FORK_DIR/ggml/CMakeLists.txt" GGML)
llama_version="$(cmake_version "$FORK_DIR/CMakeLists.txt" LLAMA)-dev"

sed -e "s/@GGML_VERSION@/${ggml_version}/" -e "s/@GGML_BUILD_COMMIT@/${commit}/" \
    "$FORK_DIR/ggml/src/ggml-version.h.in" > "$FORK_DIR/ggml/src/ggml-version.h"
sed -e "s/@LLAMA_VERSION@/${llama_version}/" -e "s/@LLAMA_BUILD_COMMIT@/${commit}/" \
    "$FORK_DIR/src/llama-version.h.in" > "$FORK_DIR/src/llama-version.h"

echo "Generated ggml-version.h (${ggml_version}, ${commit}) and llama-version.h (${llama_version}, ${commit})"
