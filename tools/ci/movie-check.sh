#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${1:?empty work directory}; mkdir -p "$out"; out=$(cd "$out" && pwd)
python3 "$root/tools/ci/fetch.py" "$out/cache" ffmpeg
tar -xJf "$out/cache/ffmpeg-8.1.3.tar.xz" -C "$out"
mkdir "$out/build"
(cd "$out/build" && CC=clang sh "$root/tools/ci/ffmpeg-build.sh" "$out/ffmpeg-8.1.3" "$out/host" host)
CC=clang sh "$root/tools/ci/test-movieav.sh" "$out/host" "$out/tests"
CC=clang SANITIZERS=address,undefined ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
 sh "$root/tools/ci/test-movieav.sh" "$out/host" "$out/sanitized"
