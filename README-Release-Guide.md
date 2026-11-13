# Release Guide

How to create a new release of `llama_cpp_canister`.

## Prerequisites

- The latest `cicd-linux` AND `cicd-mac` workflow runs on `main` must both have
  succeeded. The release workflow checks both automatically and will fail if
  either is red. They cover different ground: `cicd-linux` runs static analysis
  and the wasm QA against the Docker-built wasm (the bytes this release ships),
  `cicd-mac` runs the native exact-token tests on x86_64 macOS, the baseline CI enforces.
- All changes intended for the release are merged into `main`.

## Steps

### 1. Open the release pull request and the review pull request

On a branch `release/v<version>` off `main`:

- update `version.txt` in the repo root to the release version (e.g. `0.8.0`);
- add the release notes, `docs/release-notes/v<version>.md`, written with
  `scripts/make-release-summary.txt` (the release workflow refuses a tag without them).

Commit both, and open two pull requests from the branch:

- the **release pull request** into `main`;
- a draft **review pull request** into `release-base/<prev>`, a branch pushed at the previous
  release tag (rc included). Its diff is everything the release ships. It is for review only,
  and is closed, not merged.

### 2. Review and merge the release pull request

- The release pull request gets the reviews in the README's [Contributing](README.md#contributing)
  section.
- The review pull request gets a deep code review, `/code-review ultra`, started by a
  maintainer (it is billed), and a Claude Security scan, `/claude-security` "Scan changes" at
  high effort. Record the `main` commit the scan covered (`scanned base: <sha>`) on it.

Fix their findings before merging; a code fix goes through its own pull request into `main`,
the release branch is then rebased on it, and what the rebase brought in is scanned too. The
release pull request lists what each review found and what was done about it.

Squash-merge with the commit subject exactly `v<version>`, without the `(#NN)` suffix:
`scripts/make-release-desc.sh` leaves bare version commits out of the next changelog. Close the
review pull request and delete `release-base/<prev>`.

### 3. Trigger the release workflow

Take the head of `main` as `S`. Everything between the scanned base and `S`, other than the
`v<version>` commit, must have been scanned; scan what merged after the scan. Wait until the
`cicd-linux` and `cicd-mac` runs on `main` have finished and succeeded, then trigger the
workflow from `main` with `S`. It refuses to build when `main` has moved past `S`, so the
release contains only scanned code:

1. Go to **Actions** > **Release llama_cpp_canister** in the GitHub UI.
2. Click **Run workflow**.
3. Enter the tag for the release (e.g. `v0.8.0`) and the full sha of `S`.
4. Click **Run workflow** to start.

### 4. What the workflow does

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

### 5. Post-release verification

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
4. Optionally deploy and run smoke tests. Since icpp-pro 6.0.0 pytest must be
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
