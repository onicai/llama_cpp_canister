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
  echo "$wf: $(gh run list --repo onicai/llama_cpp_canister --workflow=$wf --branch main --limit 1 --json status,conclusion --jq '.[0] // empty | "\(.status) \(.conclusion)"')"
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

## 5. Open the release PR and the review PR

Update `version.txt` to the new version (just the version string, no trailing newline beyond what was there).

Commit it together with the release notes of step 4 on a release branch:

```bash
git checkout -b release/v<NEW_VERSION>
git add version.txt docs/release-notes/v<NEW_VERSION>.md
git commit -m "v<NEW_VERSION>"
```

Before the push, run the local reviews of the README's Contributing section on the branch
(`coderabbit review --base main --agent`, `/code-review`, `/security-review`), as for any PR.
Then push, and open two PRs from the release branch:

- the **release PR** into `main`, which is merged;
- a draft **review PR** into a branch at the previous release, `<PREV>`: the nearest release
  tag on `main`, rc included (`git fetch --tags origin`, then
  `git describe --tags --abbrev=0 --match 'v*' origin/main`).
  Its diff is everything since that release, for the deep reviews of step 6. It is never merged.

```bash
git push -u origin release/v<NEW_VERSION>
gh pr create --repo onicai/llama_cpp_canister --base main --title "v<NEW_VERSION>" --body-file docs/release-notes/v<NEW_VERSION>.md
git push origin "<PREV>^{commit}:refs/heads/release-base/<PREV>"
gh pr create --repo onicai/llama_cpp_canister --draft --base release-base/<PREV> --title "Review v<NEW_VERSION> (do not merge)" --body "Review-only PR: the changes since <PREV> that v<NEW_VERSION> ships."
```

## 6. Review and merge the release PR

A resumed release runs step 1's account check, and continues here on the release branch
(`git checkout release/v<NEW_VERSION>`).

1. The release PR needs the reviews listed in the README's Contributing section. It changes no
   code, so its tests are the CI runs of step 2 on `main`.
2. Ask the user to run `/code-review ultra <review PR#>`: it is billed and only the user can
   start it.
3. Note the commit of `main` the release branch is based on
   (`git merge-base origin/main origin/release/v<NEW_VERSION>`) before you start, then run
   `/claude-security` "Scan changes" at high effort on the review PR. Record the noted sha as
   `scanned base: <sha>` in a comment on the release PR.
4. Fix the findings of both like the other reviews'. A code fix goes through its own PR into
   `main`; it leaves the release notes alone, which live on the release branch. Then rebase the
   release branch on it, and update the notes there when the fix changes what an app must know:

   ```bash
   git checkout release/v<NEW_VERSION>
   git fetch origin
   git rebase origin/main
   ```

   Note `git rev-parse origin/main`, run the `/claude-security` scan on what the rebase brought
   in (`<scanned base>..<that sha>`), fix its findings the same way, and record that sha as the
   new scanned base on the release PR. Run the local
   reviews again, then push (both PRs follow the branch) and refresh the release PR body:

   ```bash
   git push --force-with-lease origin release/v<NEW_VERSION>
   gh pr edit <release PR#> --repo onicai/llama_cpp_canister --body-file docs/release-notes/v<NEW_VERSION>.md
   ```
5. Post what each review found and what was done about it as a comment on the release PR
   (`gh pr comment <release PR#> --body-file <file>`).
6. Do not merge it yourself unless the user says so. Squash-merge with the bare tag as the
   subject, so `scripts/make-release-desc.sh` leaves it out of the next changelog. When the
   user merges in the GitHub UI instead, tell them to replace the default subject
   `v<NEW_VERSION> (#NN)` with `v<NEW_VERSION>`.

   Merge, then close the review PR and delete both branches; skip what GitHub already closed
   or deleted:

   ```bash
   gh pr merge <release PR#> --repo onicai/llama_cpp_canister --squash --subject "v<NEW_VERSION>" --body ""
   gh pr close <review PR#> --repo onicai/llama_cpp_canister
   git push origin --delete release/v<NEW_VERSION>
   git push origin --delete release-base/<PREV>
   ```

   If the merged subject still ends in `(#NN)`, tell the user: that commit will show up in
   the next release's changelog and has to be removed from it by hand.

## 7. Trigger release workflow

A resumed release checks out `main` (`git checkout main`), runs all of step 1, and continues
here.

```bash
git checkout main
git pull origin main
git rev-parse HEAD                                   # S: the commit to release
git log --oneline <scanned base>..HEAD               # must list only the v<NEW_VERSION> commit
```

`<scanned base>` is the latest one recorded on the release PR. When anything else is listed, it
merged after the scan: run `/claude-security` "Scan changes" at high effort on
`<scanned base>..HEAD`, record `HEAD` as the new scanned base on the release PR, and fix the
findings like the other reviews', after asking the user. Each fix goes into `main` through its
own PR; the release branch is gone by now, so a fix that changes what an app must know also
updates `docs/release-notes/v<NEW_VERSION>.md` in that PR. Then pull again, take the new head
as `S`, and scan it the same way.

Then, in this order:

1. When a fix merged, wait until the CI runs it triggers are listed
   (`gh run list --repo onicai/llama_cpp_canister --commit <S>`): until then step 2 still sees
   the previous run. The path filters decide which runs; a fix outside them triggers none.
2. Repeat step 2.
3. Dispatch with the full sha of `S`. `release.yml` refuses to build when `main` has moved
   past it, or while CI on `main` is still running:

```bash
gh workflow run release.yml --repo onicai/llama_cpp_canister -f tag=v<NEW_VERSION> -f expected_sha=<S>
```

If the run fails on that check, something merged after `S`: scan it as above and dispatch
again with the new `S`.

Wait 10 seconds, then find the run by its name and commit, newest first. When nothing is
listed yet, wait and look again:

```bash
sleep 10
gh run list --repo onicai/llama_cpp_canister --workflow=release.yml --limit 5 --json databaseId,status,conclusion,displayTitle,headSha --jq '.[] | select(.displayTitle == "Release v<NEW_VERSION>" and .headSha == "<S>")' | head -1
```

## 8. Monitor release workflow

Poll `gh run view <run_id> --repo onicai/llama_cpp_canister` every 60 seconds for up to 45 minutes.

The workflow builds the wasm inside the pinned Docker image (`make docker-build-base`
then `make docker-build-wasm`), which is slower than the old on-runner build. The base
image step alone takes ~10 minutes on a cold cache.

At each check, report the current status to the user.

If the run fails, report the failure and abort. Include the URL to the failed run. When it
failed after the release was created, the tag exists and a new run refuses it: ask the user
before removing the partial release with `gh release delete v<NEW_VERSION> --repo onicai/llama_cpp_canister --cleanup-tag`.

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
- The release PR and review PR URLs, and what each review found and what was done about it
- Release URL
- Zip download URL
- wasm sha256, the commit it was built from (`S`), and the pinned fork commit
