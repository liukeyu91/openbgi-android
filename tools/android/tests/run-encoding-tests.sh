#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# JDK 8+, no SDK, game files or device required.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT HUP INT TERM
src="$root/tools/android"
sh "$src/generate-profiles.sh" "$out/EngineProfiles.java"
"${JAVAC:-javac}" -encoding UTF-8 -d "$out" "$src/ArchiveTextSamples.java" \
    "$src/ArchiveLanguage.java" "$src/GamePatch.java" "$src/GameImport.java" "$src/GameTextCodec.java" "$src/EncodingDetector.java" "$src/EncodingProbeTest.java" "$out/EngineProfiles.java" \
    "$src/GameSettings.java" "$src/GameEncoding.java" "$src/Blob.java" "$src/DiscImage.java" "$src/ContainerGame.java" "$src/ResourceLayers.java" "$src/GameEncodingTest.java" "$src/DirectGame.java" "$src/SaveBackup.java"
"${JAVA:-java}" -cp "$out" org.openbgi.port.EncodingProbeTest
"${JAVA:-java}" -cp "$out" org.openbgi.port.GameEncodingTest
