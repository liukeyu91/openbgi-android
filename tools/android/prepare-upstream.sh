#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${1:-"$root/upstream-core"}
base=257a20dffb973b6cd4354322b133328bee7229c4
archive=$(mktemp)
trap 'rm -f -- "$archive"' EXIT
git -c safe.directory="$root" -C "$root" archive --format=tar "$base" > "$archive"
mkdir -- "$out"
tar -xf "$archive" -C "$out"
(
    cd "$out"
    GIT_CEILING_DIRECTORIES=$(dirname "$PWD")
    export GIT_CEILING_DIRECTORIES
    git -c apply.whitespace=nowarn apply "$root/tools/android/upstream-1.0.patch"
)
