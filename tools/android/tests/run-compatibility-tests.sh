#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT HUP INT TERM
src="$root/tools/android"
sh "$src/generate-profiles.sh" "$out/EngineProfiles.java"
"${JAVAC:-javac}" -encoding UTF-8 -d "$out" "$src/ArchiveTextSamples.java" \
    "$src/ArchiveLanguage.java" "$src/GamePatch.java" "$src/GameImport.java" "$src/GameSettings.java" "$out/EngineProfiles.java" "$src/GameTextCodec.java" "$src/EncodingDetector.java" "$src/EncodingProbeTest.java" \
    "$src/ProgramCompatibility.java" "$src/ProgramCompatibilityTest.java" "$src/Blob.java" "$src/DiscImage.java" "$src/ContainerGame.java" "$src/ResourceLayers.java" "$src/DirectGame.java" "$src/SaveBackup.java" "$src/SaveRestore.java" \
    "$src/PatchDoctor.java" "$src/PatchDoctorTest.java"
"${JAVA:-java}" -cp "$out" org.openbgi.port.ProgramCompatibilityTest
"${JAVA:-java}" -Xmx96m -cp "$out" org.openbgi.port.PatchDoctorTest
