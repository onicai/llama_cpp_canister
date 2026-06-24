---
name: new-feature-development
description: End-to-end lifecycle for adding a new capability to llama_cpp_canister — design, implement, test, document, and verify with no regressions
disable-model-invocation: false
user-invocable: true
allowed-tools: Bash, Read, Edit, Write, AskUserQuestion
---

# New Feature Development for llama_cpp_canister

A repeatable lifecycle for adding a new capability (endpoint, timer, config,
etc.) to the canister. It generalizes the project's "Workflow for Adding
Security Fixes" (see `CLAUDE.md`). Follow the steps in order; **reuse existing
patterns** rather than inventing new ones.

The recurring cycle-balance monitor (`src/cycle_balance.{h,cpp}`,
`native/test_cycle_balance.cpp`, `test/test_cycle_balance.py`) is a reference
example that was built with exactly this lifecycle — copy its shape.

## 1. Explore precedents first

Before writing anything, find the closest existing feature and copy its
structure. Known good templates in this repo:

- **Recurring timer** → `src/cache_cleanup.{h,cpp}` (start/stop, idempotent
  re-arm, worker that does NOT construct its own `IC_API`) and
  `src/cycle_balance.{h,cpp}`.
- **Admin RBAC gating** → `src/auth.h`: `has_admin_query_role` /
  `has_admin_update_role` (+ `*_or_whitelisted` for user-facing endpoints) and
  `send_access_denied_api_error`.
- **Simple endpoints** → `src/health.cpp`, `src/whoami.cpp`,
  `src/max_tokens.cpp`.
- **Persistent state** → plain `static` / global C++ variables (orthogonal
  persistence; see `src/auth.cpp`, `src/max_tokens.cpp`). Note: timer state and
  plain globals are wiped on upgrade unless intentionally persisted.

Use the `Explore` subagent to locate the right precedent if unsure.

## 2. Clarify requirements with the user

Resolve open design questions BEFORE coding (use `AskUserQuestion`):
- Lifecycle: operator-driven start endpoint vs auto-start? (This repo has no
  `canister_post_upgrade`; existing timers are operator-driven.)
- Return shape and error behavior (e.g. what to return when a feature is "off").
- Naming convention (most endpoints are snake_case; RBAC ones are camelCase).

## 3. Create a feature branch

```bash
git checkout -b feature/<name>   # off main
```

## 4. Implement

- New `src/<feature>.{h,cpp}`. Each endpoint is a free function whose name
  matches the `.did` method; export it via `WASM_SYMBOL_EXPORTED("canister_query <name>")`
  or `"canister_update <name>"` in the header.
- First line of every endpoint: `IC_API ic_api(CanisterQuery{std::string(__func__)}, false);`
  (or `CanisterUpdate`). Gate sensitive endpoints with the `has_admin_*` helpers
  and `send_access_denied_api_error`.
- Timer callbacks run inside `canister_global_timer`'s `IC_API` frame and MUST
  NOT construct their own `IC_API` — call raw `ic0.h` imports for system calls,
  and `IC_API::time()` (static) for time.
- Add types + service methods to `src/llama_cpp.did`. Reuse existing result
  types (`ApiError`, `StatusCodeRecordResult`) where they fit.
- `icpp.toml` auto-globs `src/*.cpp` and `native/*.cpp` — no build-config edit
  needed for new files.

## 5. Test

- **Native MockIC**: `native/test_<feature>.{cpp,h}`; wire `#include` + call
  into `native/main.cpp`. For expected Candid hex of error variants, reuse the
  ACTUAL C++ output (the type-table ordering differs from `didc`'s — see the
  `ACCESS_DENIED_API_ERROR` constant). Tip: the MockIC seeds a deterministic
  cycle balance of 3'000'000'000'000 and lets you pin time with
  `ic0mock_set_time_override`.
- **pytest smoke**: `test/test_<feature>.py`; add it to the `test_paths` list in
  `scripts/qa_deploy_and_pytest.py` so it runs in the full QA scenario. Match
  the exact response format the framework returns — e.g. a status record prints
  as `{ status_code = 200 : nat16;}` (trailing `;}`, no space).
- Clean `.canister_cache` between repeated native runs — leftover files make
  filesystem-touching tests (e.g. cache_cleanup) non-deterministic.

## 6. Document

Add a README section explaining what the feature does, its operator-driven
lifecycle, the required admin roles, and copy-pasteable `dfx canister call`
examples (including any "off"/error responses).

## 7. Verify — targeted

```bash
source /opt/miniconda3/etc/profile.d/conda.sh && conda activate llama_cpp_canister

# Format the new files with the repo's clang-format:
"$HOME/.icpp/wasi-sdk/wasi-sdk-25.0/bin/clang-format" --style=file -i src/<feature>.* native/test_<feature>.*

# Native unit tests:
icpp build-native && ./build-native/mockic.exe

# WASM build + local deploy (regenerates declarations):
icpp build-wasm
dfx start --clean --background
dfx deploy --network local

# Exercise the new endpoints:
dfx canister --network local call llama_cpp <new_endpoint> '()'

# New smoke tests:
pytest -vv --network local test/test_<feature>.py
```

## 8. Verify — full regression

```bash
make all-static   # cpp-format + python format/lint/type
make test-llm-native   # native MockIC
make test-llm-wasm     # qa_deploy_and_pytest (deploys tiny model, runs pytest suites)
# or all three at once:
make all-tests
```
`make all-tests` should end with "Congratulations, everything passed!".

Note: `make test-llm-wasm` (and the qwen2 walkthrough below) require
`python -m scripts.upload` to load a model. If that step fails with
`404 .../api/v3/.../query` (an icp_core agent vs dfx replica mismatch), the
upload tooling is broken in your environment — the model-dependent suites can't
run until it's fixed. The native, static, and dfx-based smoke suites
(`call_canister_api` shells out to dfx, so they still work) remain valid.

## 9. Verify — full README walkthrough

Deploy the qwen2 model following the exact README "Getting Started" steps
(download → deploy → fabricate-cycles → upload+verify sha256 → load_model →
set_max_tokens → new_chat → run_update until `generated_eog = true`), then
exercise the new feature on the live, model-loaded canister. Confirms no
regression in the inference path. Also run:
```bash
pytest -vv --network local test/test_canister_functions.py
pytest -vv --network local test/test_qwen2.py
```

## 10. Commit

Per `CLAUDE.md`: single-line commit message, no `Co-Authored-By` trailer, no
self-attribution, never `--no-verify`.

```bash
git add -A && git commit -m "<concise single-line summary>"
```
