#!/bin/bash
# Fail unless the release tag does not exist on origin yet. release.yml runs it
# before the build and again right before publishing: an existing tag may point
# at another commit, and the release would publish this build under it.
#
# Usage: scripts/check-tag-new.sh <tag>
set -uo pipefail

if [[ $# -ne 1 ]]; then
    echo "Usage: $(basename "$0") <tag>" >&2
    exit 1
fi
TAG="$1"

# --exit-code: 0 = found, 2 = not found, anything else = the lookup failed.
rc=0
git ls-remote --exit-code --tags origin "refs/tags/${TAG}" >/dev/null || rc=$?
if [[ "$rc" -eq 0 ]]; then
    echo "ERROR: tag ${TAG} already exists." >&2
    exit 1
elif [[ "$rc" -ne 2 ]]; then
    echo "ERROR: could not look up tag ${TAG} (git ls-remote exit ${rc})." >&2
    exit 1
fi
