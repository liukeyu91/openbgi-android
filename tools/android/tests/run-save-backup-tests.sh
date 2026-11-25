#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT HUP INT TERM
sh "$root/tools/android/generate-profiles.sh" "$out/EngineProfiles.java"
"${JAVAC:-javac}" -encoding UTF-8 -d "$out" "$root/tools/android/SaveBackup.java" "$root/tools/android/SaveBackupTest.java" \
    "$root/tools/android/SaveRestore.java" "$root/tools/android/SaveRestoreTest.java" "$root/tools/android/GameDataLock.java" \
    "$root/tools/android/Blob.java" "$root/tools/android/DiscImage.java" "$root/tools/android/ContainerGame.java" "$root/tools/android/ResourceLayers.java" \
    "$root/tools/android/ArchiveLanguage.java" "$root/tools/android/GamePatch.java" "$root/tools/android/DirectGame.java" \
    "$root/tools/android/GameImport.java" "$root/tools/android/GameSettings.java" "$out/EngineProfiles.java"
"${JAVA:-java}" -cp "$out" org.openbgi.port.SaveBackupTest
"${JAVA:-java}" -cp "$out" org.openbgi.port.SaveRestoreTest
