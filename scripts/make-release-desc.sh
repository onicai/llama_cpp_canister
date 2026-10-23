#!/bin/bash
# Generate the changelog part of a release's notes: the commits since the
# previous release, one line per commit, with PR numbers linked. Modelled on
# upstream llama.cpp's scripts/make-release-desc.sh.
#
# Usage: scripts/make-release-desc.sh <tag>
#   <tag>: the release tag, vX.Y.Z or vX.Y.ZrcN (the leading v is optional)
#
# The previous release:
#   - for vX.Y.ZrcN: vX.Y.Zrc(N-1), or for rc1 the highest final below X.Y.Z;
#   - for a final vX.Y.Z: the highest final below it, so its notes cover the
#     whole rc cycle.
#
# The release commit is the commit <tag> points at when the tag exists, HEAD
# otherwise (release.yml runs before the tag is created).
#
# The hand-written summary that comes before this part is
# docs/release-notes/<tag>.md, written with scripts/make-release-summary.txt.
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "Usage: $(basename "$0") <tag>" >&2
    exit 1
fi
TAG="$1"
[[ "${TAG}" == v* ]] || TAG="v${TAG}"
if [[ ! "${TAG}" =~ ^v([0-9]+)\.([0-9]+)\.([0-9]+)(rc([0-9]+))?$ ]]; then
    echo "Error: invalid tag '${TAG}' (expected vX.Y.Z or vX.Y.ZrcN)" >&2
    exit 1
fi
BASE="v${BASH_REMATCH[1]}.${BASH_REMATCH[2]}.${BASH_REMATCH[3]}"
RC="${BASH_REMATCH[5]}"

REPO_URL="https://github.com/onicai/llama_cpp_canister"

# Make sure all release tags are available (skipped when there is no origin)
git fetch -q --tags origin 2>/dev/null || true

# The highest final release strictly below BASE. sort -V on finals only, since
# it would put v0.23.0 before v0.23.0rc1.
highest_final_below() {
    { git tag --list; echo "$1"; } |
        grep -E '^v[0-9]+\.[0-9]+\.[0-9]+$' |
        sort -V -u |
        awk -v cur="$1" '$0 == cur { exit } { prev = $0 } END { print prev }'
}

if [[ -n "${RC}" && "${RC}" -gt 1 ]]; then
    PREV="${BASE}rc$((RC - 1))"
else
    PREV="$(highest_final_below "${BASE}")"
fi

if ! RELEASE_COMMIT="$(git rev-parse -q --verify "refs/tags/${TAG}^{commit}")"; then
    RELEASE_COMMIT="$(git rev-parse HEAD)"
fi

if [[ -z "${PREV}" ]] || ! git rev-parse -q --verify "refs/tags/${PREV}" >/dev/null; then
    echo "## Changelog"
    echo
    echo "(no previous release tag found)"
    exit 0
fi

echo "## Changelog since [${PREV}](${REPO_URL}/releases/tag/${PREV})"
echo
# One bullet per commit; the version-bump commits (a bare tag) are left out.
git log --format='%h %s' "${PREV}..${RELEASE_COMMIT}" |
    grep -v -E '^[0-9a-f]+ v[0-9]+\.[0-9]+\.[0-9]+(rc[0-9]+)?$' |
    sed -E "s|\(#([0-9]+)\)|([#\1](${REPO_URL}/pull/\1))|g; s|^([0-9a-f]+) |- [\`\1\`](${REPO_URL}/commit/\1) |" ||
    true
