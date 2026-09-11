ARG IMAGE
FROM --platform=linux/amd64 ${IMAGE}

# The WORKDIR is part of the artifact, so treat it as pinned.
#
# icpp-pro compiles every translation unit with an ABSOLUTE path (icpp_toml's
# expand_paths() resolves patterns against the icpp.toml directory), and ggml.h
# defines GGML_ABORT(...) as ggml_abort(__FILE__, ...), which stays live under
# -DNDEBUG. So this directory string is embedded in llama_cpp.wasm's data
# section. Changing it changes the hash.
WORKDIR /app/llama_cpp_canister

# ---------------------------------------------------------------------------
# The llama.cpp fork, at the commit pinned in version_fork.env.
#
# It is a SEPARATE, gitignored repository, so it is cloned here rather than
# copied from the working tree (.dockerignore excludes it). That is what lets
# someone with nothing but this repo reproduce the wasm.
#
# Fetching a bare SHA needs uploadpack.allowReachableSHA1InWant, which GitHub
# allows. The fork's .gitmodules is empty (0 bytes), so there are no submodules
# to init.
ARG FORK_REPO
ARG FORK_COMMIT
RUN set -eux; \
    mkdir -p src/llama_cpp_onicai_fork; \
    cd src/llama_cpp_onicai_fork; \
    git init -q .; \
    git remote add origin "https://github.com/${FORK_REPO}.git"; \
    git fetch -q --depth 1 origin "${FORK_COMMIT}"; \
    git checkout -q FETCH_HEAD; \
    test "$(git rev-parse HEAD)" = "${FORK_COMMIT}"

# ---------------------------------------------------------------------------
# Only what the build reads. native/ is included because icpp-pro parses the
# whole icpp.toml, including [build-native]'s paths.
COPY icpp.toml Makefile version.txt version_clang.txt version_fork.env ./
COPY wasi-shims ./wasi-shims/
COPY scripts ./scripts/
COPY src ./src/
COPY native ./native/

CMD ["/bin/bash"]
