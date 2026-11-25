#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
src="$root/tools/android"
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT HUP INT TERM
sh "$src/generate-profiles.sh" "$out/EngineProfiles.java"
"${JAVAC:-javac}" -encoding UTF-8 -d "$out" "$src/GameImport.java" "$src/DirectGame.java" "$src/DirectGameTest.java" \
    "$src/ArchiveLanguage.java" "$src/GamePatch.java" "$src/GamePatchTest.java" "$src/GameSettings.java" "$out/EngineProfiles.java" "$src/GameDataLock.java" "$src/SaveBackup.java" "$src/SaveRestore.java" \
    "$src/RunReport.java" "$src/RunReportTest.java" "$src/Blob.java" "$src/DiscImage.java" "$src/ContainerGame.java" "$src/ResourceLayers.java"
"${JAVA:-java}" -Xmx96m -cp "$out" org.openbgi.port.DirectGameTest
"${JAVA:-java}" -Xmx96m -cp "$out" org.openbgi.port.GamePatchTest
"${JAVA:-java}" -cp "$out" org.openbgi.port.RunReportTest
${CC:-cc} -D_GNU_SOURCE -I"$root/inc" "$root/tools/tests/androidstorage.c" -o "$out/androidstorage"
"$out/androidstorage"
