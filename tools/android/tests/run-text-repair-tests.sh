#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT HUP INT TERM
src="$root/tools/android"
"${JAVAC:-javac}" -encoding UTF-8 -d "$out" "$src/GameTextCodec.java" \
    "$src/InitialTextRepair.java" "$src/InitialTextRepairTest.java" "$src/GameTextCodecTest.java"
"${JAVA:-java}" -cp "$out" org.openbgi.port.InitialTextRepairTest
"${JAVA:-java}" -cp "$out" org.openbgi.port.GameTextCodecTest
