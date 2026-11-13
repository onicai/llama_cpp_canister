---
name: llama_cpp_canister-release
description: Create a new GitHub release for llama_cpp_canister
disable-model-invocation: false
user-invocable: true
argument-hint: <new-version> [major|minor|patch|rc|final]
allowed-tools: Bash, Read, Edit, Write, AskUserQuestion, Skill
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

Both CI workflows must be green. They cover different ground and neither covers
the other: `cicd-linux` runs static analysis plus the wasm QA against the
Docker-built wasm (the bytes the release ships); `cicd-mac` runs the native
exact-token tests on x86_64 macOS, the baseline CI enforces.

```bash
for wf in cicd-linux.yml cicd-mac.yml; do
  echo "$wf: $(gh run list --repo onicai/llama_cpp_canister --workflow=$wf --branch main --limit 1 --json status,conclusion --jq '.[0] | "\(.status) \(.conclusion)"')"
done
```

This is the gate in `release.yml`: the newest run on `main` decides, and it must have
finished. Without `--branch main`, the newest run on any branch would decide.

If either result is not `completed success`: while a run is still in progress or queued, wait
for it and check again; otherwise abort with: "CI is not green. Latest <workflow> run: <result>. Fix CI before releasing."

## 3. Determine version

Read the current version from two sources and cross-check:

```bash
cat version.txt
gh release list --repo onicai/llama_cpp_canister --exclude-drafts --limit 1 --json tagName --jq '.[0].tagName' | sed 's/^v//'
```

- `version.txt` is the local source of truth
- The newest published GitHub release tag, prereleases included and drafts excluded, is the remote source of truth
- If they don't match, warn the user and ask how to proceed before continuing. Suggest updating `version.txt` to match the newest release tag.

A version is `X.Y.Z`, or `X.Y.ZrcN` for a release candidate (the form icpp-pro uses,
e.g. `7.0.0rc2`). Parse the confirmed current version into major.minor.patch, and rcN
if present.

Ask the user: "Current version is <current>. Increment major, minor, or patch, or make
a release candidate (rc)?" When the current version is a release candidate, also offer
the final release (final).

Compute the new version based on their choice:
- **major**: X+1.0.0
- **minor**: X.Y+1.0
- **patch**: X.Y.Z+1
- **rc**: the next release candidate. From `X.Y.ZrcN` it is `X.Y.Zrc(N+1)`. From a
  final `X.Y.Z`, ask whether the candidate is for the next major, minor or patch, and
  add `rc1` (e.g. `0.22.0` -> minor -> `0.23.0rc1`).
- **final**: from `X.Y.ZrcN`, drop the suffix: `X.Y.Z`. Only offered from a release
  candidate: `X.Y.Z` is already final.

From a release candidate, major, minor and patch count from `X.Y.Z`, as if it were final.

Show the computed version and tag (e.g., `v0.8.1`, or `v0.23.0rc1`) and ask the user to
confirm before proceeding. A tag with `rc` is published as a prerelease: it does not
become "Latest" on GitHub.

## 4. Write the release notes

Every release has notes; `release.yml` refuses a tag without them. They are written like
upstream llama.cpp's: a summary, then the generated changelog.

1. List the commits since the previous release:

   ```bash
   scripts/make-release-desc.sh v<NEW_VERSION>
   ```

2. Write `docs/release-notes/v<NEW_VERSION>.md` following
   `scripts/make-release-summary.txt`: read each PR and the diff, and fill in the sections
   that apply. Put what an app must change under "Behaviour changes for apps".
3. Show the notes to the user and get their approval before going on. Apply their edits.

The workflow publishes the release body as: these notes, then the output of
`scripts/make-release-desc.sh`, then the "Reproducible build" section.

## 5. Open the release PR

Update `version.txt` to the new version (just the version string, no trailing newline beyond what was there).

Commit it together with the release notes of step 4 on a release branch, push, and open a PR:

```bash
git checkout -b release/v<NEW_VERSION>
git add version.txt docs/release-notes/v<NEW_VERSION>.md
git commit -m "v<NEW_VERSION>"
```

Before the push, run the local reviews of the README's Contributing section on the branch
(`coderabbit review --base main --agent`, `/code-review`, `/security-review`), as for any PR:

```bash
git push -u origin release/v<NEW_VERSION>
gh pr create --repo onicai/llama_cpp_canister --base main --title "v<NEW_VERSION>" --body-file docs/release-notes/v<NEW_VERSION>.md
```

## 6. Review and merge the release PR

1. The PR needs the reviews listed in the README's Contributing section before it is merged.
   It changes no code, so the tests are the CI runs of step 2 on `main`.
2. Ask the user to run `/code-review ultra <PR#>` on it: it is billed and only the user can
   start it.
3. Fix the findings like the other reviews'. A code fix goes through its own PR into `main`.
   Then rebase the release branch on it, and update the notes if the fix changes them:

   ```bash
   git checkout release/v<NEW_VERSION>
   git fetch origin
   git rebase origin/main
   ```

   When the notes changed, run the local reviews again before the push. Push, and refresh the
   PR body:

   ```bash
   git push --force-with-lease origin release/v<NEW_VERSION>
   gh pr edit <PR#> --repo onicai/llama_cpp_canister --body-file docs/release-notes/v<NEW_VERSION>.md
   ```
4. Post what each review found and what was done about it as a PR comment
   (`gh pr comment <PR#> --body-file <file>`).
5. Do not merge it yourself unless the user says so. Squash-merge with the bare tag as the
   subject, so `scripts/make-release-desc.sh` leaves it out of the next changelog. When the
   user merges in the GitHub UI instead, tell them to replace the default subject
   `v<NEW_VERSION> (#NN)` with `v<NEW_VERSION>`.

   ```bash
   gh pr merge <PR#> --repo onicai/llama_cpp_canister --squash --subject "v<NEW_VERSION>" --body "" --delete-branch
   ```

   If the merged subject still ends in `(#NN)`, tell the user: that commit will show up in
   the next release's changelog and has to be removed from it by hand.

## 7. Scan the release and trigger the release workflow

A resumed release runs step 1's account check. Before the merge it continues at step 6, on the
release branch (`git checkout release/v<NEW_VERSION>`). After the merge it runs all of step 1
and continues here: when the release PR has a `scanned: <S>` comment, the latest one is `S`,
and the commits on `main` after it still need the scan below (`<S>..HEAD`).

```bash
git checkout main
git pull origin main
git rev-parse HEAD   # S: the commit to scan and release
```

Run `/claude-security` "Scan changes" at high effort on `<PREV>..<S>`, where `<PREV>` is the
previous release named in the changelog heading of `scripts/make-release-desc.sh v<NEW_VERSION>`.
When there is no previous release, run "Scan codebase" at high effort instead. Record
`scanned: <S>` as a comment on the release PR.

Fix its findings like the other reviews', after asking the user: each fix goes into `main`
through its own PR. Then pull `main` again, take its head as the new `S`, scan
`<old S>..<new S>`, and record the new `S`.

Repeat step 2. Then dispatch with the full sha of the scanned commit; `release.yml` refuses to
build when `main` has moved past it, or while CI on `main` is still running:

```bash
gh workflow run release.yml --repo onicai/llama_cpp_canister -f tag=v<NEW_VERSION> -f expected_sha=<S>
```

If the run fails on that check, something merged after the scan: scan it as above and
dispatch again with the new `S`.

Wait 10 seconds, then find the run ID:

```bash
sleep 10
gh run list --repo onicai/llama_cpp_canister --workflow=release.yml --limit 1 --json databaseId,status --jq '.[0]'
```

## 8. Monitor release workflow

Poll `gh run view <run_id> --repo onicai/llama_cpp_canister` every 60 seconds for up to 45 minutes.

The workflow builds the wasm inside the pinned Docker image (`make docker-build-base`
then `make docker-build-wasm`), which is slower than the old on-runner build. The base
image step alone takes ~10 minutes on a cold cache.

At each check, report the current status to the user.

If the run fails, report the failure and abort. Include the URL to the failed run.

If the run succeeds, proceed to step 9.

## 9. Verify release

```bash
gh release view v<NEW_VERSION> --repo onicai/llama_cpp_canister
```

Print:
- The release URL
- The zip download URL
- The wasm sha256, which the release title and body both carry

Confirm the published wasm matches the hash the release advertises:

```bash
curl -sL https://github.com/onicai/llama_cpp_canister/releases/download/v<NEW_VERSION>/llama_cpp.wasm -o /tmp/rel.wasm
shasum -a 256 /tmp/rel.wasm
curl -sL https://github.com/onicai/llama_cpp_canister/releases/download/v<NEW_VERSION>/llama_cpp.wasm.sha256
```

The two must be equal, and must equal the hash in the release title. If they differ,
abort and report -- the release is not trustworthy.

Check the release body: it must start with the notes of step 4, then the changelog, then
the "Reproducible build" section:

```bash
gh release view v<NEW_VERSION> --repo onicai/llama_cpp_canister --json body --jq .body > /tmp/rel-body.md
head -n "$(wc -l < docs/release-notes/v<NEW_VERSION>.md)" /tmp/rel-body.md | diff - docs/release-notes/v<NEW_VERSION>.md && echo "notes ok"
grep -n -E '^## (Changelog|Reproducible build)' /tmp/rel-body.md
```

`diff` prints nothing, and `grep` shows the changelog heading before "Reproducible build".

## 10. Summary

Print a summary of what was done:
- Version bumped from OLD to NEW
- Tag: v<NEW_VERSION>, and whether it is a prerelease
- The release PR URL, and what each review found and what was done about it
- Release URL
- Zip download URL
- wasm sha256, the commit it was built from (the scanned `S`), and the pinned fork commit
