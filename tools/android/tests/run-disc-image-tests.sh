#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# JDK 8+, no SDK, native library, game files or device required: the image is
# a file plus the index lines the native mount reports.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
src="$root/tools/android"
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT HUP INT TERM
sh "$src/generate-profiles.sh" "$out/EngineProfiles.java"
"${JAVAC:-javac}" -encoding UTF-8 -d "$out" \
    "$src/GameImport.java" "$src/DirectGame.java" "$src/ArchiveLanguage.java" "$src/GamePatch.java" \
    "$src/GameSettings.java" "$out/EngineProfiles.java" "$src/GameDataLock.java" "$src/SaveBackup.java" \
    "$src/SaveRestore.java" "$src/Blob.java" "$src/DiscImage.java" "$src/ContainerGame.java" "$src/ResourceLayers.java" \
    "$src/ArchiveTextSamples.java" "$src/EncodingDetector.java" "$src/GameEncoding.java" "$src/GameTextCodec.java" \
    "$src/EncodingProbeTest.java" "$src/DiscImageTest.java"
"${JAVA:-java}" -Xmx128m -cp "$out" org.openbgi.port.DiscImageTest
