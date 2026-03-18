# Release Guide

How to create a new release of `llama_cpp_canister`.

## Prerequisites

- The latest `cicd-mac` workflow run on the `main` branch must have succeeded.
  The release workflow checks this automatically and will fail if CI/CD is red.
- All changes intended for the release are merged into `main`.

## Steps

### 1. Bump the version

Update `version.txt` in the repo root to the desired release version (e.g. `0.8.0`).
Commit and push to `main`.

### 2. Trigger the release workflow

1. Go to **Actions** > **Release llama_cpp_canister** in the GitHub UI.
2. Click **Run workflow**.
3. Enter the tag for the release (e.g. `v0.8.0`).
4. Click **Run workflow** to start.

### 3. What the workflow does

| Step                        | Description                                                                   |
| --------------------------- | ----------------------------------------------------------------------------- |
| **check-cicd-mac-status**   | Verifies the latest `cicd-mac.yml` run succeeded                              |
| **install & build**         | Sets up miniconda, installs toolchains, builds the Wasm canister              |
| **zip release files**       | Packages `build/`, `scripts/`, `test/`, `dfx.json`, `version.txt`, etc.       |
| **create GitHub release**   | Creates a GitHub release with tag and attaches `llama_cpp_canister_<tag>.zip` |

### 4. Post-release verification

After the workflow completes:

1. Download the zip artifact from the GitHub release page.
2. Unzip and verify the contents include:
   - `build/llama_cpp.wasm` and `build/llama_cpp.did`
   - `scripts/` with upload/download tooling
   - `test/` with smoke tests
   - `dfx.json`, `version.txt`, `requirements.txt`
3. Optionally deploy and run smoke tests:
   ```bash
   dfx start --clean --background
   dfx deploy --network local
   pytest -vv --network local test/
   ```
