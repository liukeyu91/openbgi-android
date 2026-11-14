#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT HUP INT TERM
src="$root/tools/android"
"${JAVAC:-javac}" -encoding UTF-8 -d "$out" "$src/ArchiveTextSamples.java" \
    "$src/EncodingDetector.java" "$src/EncodingProbeTest.java" \
    "$src/ProgramCompatibility.java" "$src/ProgramCompatibilityTest.java"
"${JAVA:-java}" -cp "$out" org.openbgi.port.ProgramCompatibilityTest
