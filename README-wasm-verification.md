# Wasm Verification (pre onicai SNS)

> **NOTE:** This workflow was created for the **pre onicai SNS verification
> process** ([NNS Proposal 140268](https://dashboard.internetcomputer.org/proposal/140268)).
> It pins the build environment to icpp-pro 5.3.0 / Rust 1.86.0 to reproduce the
> exact wasm from the v0.7.3 release that is currently deployed to the funnAI LLM
> canisters. **Post onicai SNS, the build process and pinned versions must be
> updated** to match the then-current release and toolchain.

The GitHub Actions workflow [verify-funnAI-LLMs](.github/workflows/verify-funnAI-LLMs.yml) verifies that the `llama_cpp.wasm` built from this repo matches the wasm deployed to the funnAI LLM canisters on the Internet Computer mainnet.

Anyone can independently verify that the on-chain LLM canisters are running the exact code from this open-source repo.

**What it does:**

1. Builds `llama_cpp.wasm` from source (same build steps as the release workflow)
2. Computes the sha256 hash of the built wasm
3. Queries the module hash of each deployed funnAI LLM canister on IC mainnet via `icp canister status`
4. Compares the hashes and reports pass/fail for each canister

**Canisters verified (30 total):**

| Category                        | Count | Description                                |
| ------------------------------- | ----- | ------------------------------------------ |
| funnAI Challenger LLM           | 1     | Generates challenges for the funnAI game   |
| funnAI Judge LLMs               | 16    | Judge responses in the funnAI game         |
| funnAI mAIner ShareService LLMs | 13    | Provide LLM inference for mAIner services  |

**How to run:**

Trigger the workflow manually from the Actions tab on GitHub (`workflow_dispatch`).
