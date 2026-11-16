#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
export LANG=C.UTF-8 LC_ALL=C.UTF-8
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
src="$root/tools/android"
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT HUP INT TERM
sh "$src/generate-profiles.sh" "$out/EngineProfiles.java"
python3 "$src/generate-patch-profiles.py" "$out/PatchProfiles.java"
"${JAVAC:-javac}" -encoding UTF-8 -d "$out" "$src/GameImport.java" "$src/GameSettings.java" \
    "$out/EngineProfiles.java" "$out/PatchProfiles.java" "$src/PatchImport.java" "$src/PatchEvb.java" \
    "$src/PatchAplib.java" "$src/PatchCamellia.java" "$src/PatchCpk.java" "$src/PatchCpkTest.java" \
    "$src/PatchImportTest.java" "$src/tests/P0ImportTest.java"
"${JAVA:-java}" -Xmx96m -cp "$out" org.openbgi.port.PatchCpkTest
"${JAVA:-java}" -Xmx96m -cp "$out" org.openbgi.port.PatchImportTest
"${JAVA:-java}" -Xmx96m -cp "$out" org.openbgi.port.P0ImportTest "$out"
