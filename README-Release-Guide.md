# Release Guide

How to create a new release of `llama_cpp_canister`.

## Prerequisites

- The latest `cicd-linux` AND `cicd-mac` workflow runs on `main` must both have
  succeeded. The release workflow checks both automatically and will fail if
  either is red. They cover different ground: `cicd-linux` runs static analysis
  and the wasm QA against the Docker-built wasm (the bytes this release ships),
  `cicd-mac` runs the native exact-token tests, which only build on x86_64 macOS.
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

| Step                      | Description                                                                                  |
| ------------------------- | -------------------------------------------------------------------------------------------- |
| **check-ci-status**       | Verifies the latest `cicd-linux.yml` AND `cicd-mac.yml` runs both succeeded                    |
| **docker build**          | Builds the wasm in the pinned `linux/amd64` image (`make docker-build-base`, `docker-build-wasm`) |
| **compute hashes**        | sha256 of `out/llama_cpp.wasm`; also written to the run summary                                 |
| **zip release files**     | Packages `build/`, `scripts/`, `test/`, `icp.yaml`, `version.txt`, `BUILD-PROVENANCE.txt`, etc. |
| **create GitHub release** | Attaches the zip, the bare `llama_cpp.wasm` and `llama_cpp.wasm.sha256`                        |

The build runs on `ubuntu-22.04` inside Docker, not on a macOS runner: the point is that
anyone can reproduce the artifact. The tests run in `cicd-linux.yml` (the wasm QA, against
a Docker build of this same commit) and `cicd-mac.yml` (the native exact-token tests), both
of which this workflow is gated on.

The release page shows, at the top, the wasm sha256 and the two commits it was built from
(this repo, and the pinned `llama_cpp_onicai_fork`), plus the commands to reproduce it.

### 4. Post-release verification

After the workflow completes:

1. Download the zip artifact from the GitHub release page.
2. Unzip and verify the contents include:
   - `build/llama_cpp.wasm` and `build/llama_cpp.did`
   - `scripts/` with upload/download tooling
   - `test/` with smoke tests
   - `icp.yaml`, `version.txt`, `requirements.txt`
3. Confirm the wasm in the zip matches what the release page advertises:
   ```bash
   shasum -a 256 build/llama_cpp.wasm   # must equal the sha256 in the release title/body
   ```
4. Record the rollout in `funnAI/WASM-HASHES.md` once the canisters have been
   upgraded to this release — hash, plus the commit it was built from.
5. Optionally deploy and run smoke tests. Since icpp-pro 6.0.0 pytest must be
   told which icp identity to run as, and it has to be the identity that
   deployed the canister (most endpoints are controller-only):
   ```bash
   # once, if you do not have them yet
   icp identity new llama-cpp-testing --storage plaintext
   icp identity new llama-cpp-other-user --storage plaintext  # non-controller, for test_files.py

   icp network start -d
   icp deploy -e local -y --identity llama-cpp-testing
   pytest -vv --network local --identity llama-cpp-testing test/
   ```
