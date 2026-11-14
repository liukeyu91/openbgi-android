#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# JDK 8+ and a host C99 compiler; no SDK, device or game files required.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT HUP INT TERM
src="$root/tools/android"
sh "$src/generate-profiles.sh" "$out/EngineProfiles.java"
"${JAVAC:-javac}" -encoding UTF-8 -d "$out" "$out/EngineProfiles.java" \
    "$src/GameSettings.java" "$src/Viewport.java" "$src/InputKeys.java" \
    "$src/TouchInput.java" "$src/InputContractTest.java"
"${JAVA:-java}" -cp "$out" org.openbgi.port.InputContractTest
"${CC:-cc}" -std=c99 -Wall -Wextra -Werror "$src/tests/viewport.c" -o "$out/viewport"
"$out/viewport" > "$out/native.txt"
"${JAVA:-java}" -cp "$out" org.openbgi.port.InputContractTest vectors > "$out/java.txt"
cmp "$out/native.txt" "$out/java.txt"
echo '270 native/Java viewport vectors match'
