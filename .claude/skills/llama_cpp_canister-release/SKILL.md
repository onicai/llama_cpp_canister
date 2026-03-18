---
name: llama_cpp_canister-release
description: Create a new GitHub release for llama_cpp_canister
disable-model-invocation: false
user-invocable: true
argument-hint: <new-version> [major|minor|patch]
allowed-tools: Bash, Read, Edit, Write, AskUserQuestion
---

# Release llama_cpp_canister

Follow these steps exactly in order. Abort and report on any failure.

## 1. Pre-flight checks

### Verify GitHub CLI account

Check the current `gh` auth status:

```bash
gh auth status
```

The active account must be `icppWorld` (per CLAUDE.md instructions for the onicai project). If not:

```bash
gh auth switch --user icppWorld
```

If the correct account is not known from CLAUDE.md context, ask the user which GitHub account to use.

### Verify branch and working tree

```bash
git branch --show-current
git status --short
git fetch origin main
git diff origin/main --stat
```

- Must be on `main` branch
- Working tree must be clean (no uncommitted changes)
- Must be up to date with `origin/main`

If any check fails, report the issue and abort.

## 2. Check CI status

```bash
gh run list --repo onicai/llama_cpp_canister --workflow=cicd-mac.yml --limit 1 --json conclusion --jq '.[0].conclusion'
```

If the result is not `success`, abort immediately with: "CI is not green. Latest cicd-mac.yml conclusion: <result>. Fix CI before releasing."

## 3. Determine version

Read the current version from two sources and cross-check:

```bash
cat version.txt
gh release view --repo onicai/llama_cpp_canister --json tagName --jq '.tagName' | sed 's/^v//'
```

- `version.txt` is the local source of truth
- The latest GitHub release tag is the remote source of truth
- If they don't match, warn the user and ask how to proceed before continuing. Suggest updating `version.txt` to match the latest release tag.

Parse the confirmed current version into major.minor.patch components.

Ask the user: "Current version is X.Y.Z. Increment major, minor, or patch?"

Compute the new version based on their choice:
- **major**: X+1.0.0
- **minor**: X.Y+1.0
- **patch**: X.Y.Z+1

Show the computed version and tag (e.g., `v0.8.1`) and ask the user to confirm before proceeding.

## 4. Bump version

Update `version.txt` to the new version (just the version string, no trailing newline beyond what was there).

Commit and push:

```bash
git add version.txt
git commit -m "v<NEW_VERSION>"
git push origin main
```

## 5. Trigger release workflow

```bash
gh workflow run release.yml --repo onicai/llama_cpp_canister -f tag=v<NEW_VERSION>
```

Wait 10 seconds, then find the run ID:

```bash
sleep 10
gh run list --repo onicai/llama_cpp_canister --workflow=release.yml --limit 1 --json databaseId,status --jq '.[0]'
```

## 6. Monitor release workflow

Poll `gh run view <run_id> --repo onicai/llama_cpp_canister` every 60 seconds for up to 30 minutes.

At each check, report the current status to the user.

If the run fails, report the failure and abort. Include the URL to the failed run.

If the run succeeds, proceed to step 7.

## 7. Verify release

```bash
gh release view v<NEW_VERSION> --repo onicai/llama_cpp_canister
```

Print:
- The release URL
- The zip download URL

## 8. Summary

Print a summary of what was done:
- Version bumped from OLD to NEW
- Tag: v<NEW_VERSION>
- Release URL
- Zip download URL
