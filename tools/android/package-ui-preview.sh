#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Retain the independent UI preview package so its imported games/saves can
# receive in-place updates. No fixture games are created or packaged.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out="$root/bin/android-ui-preview"
mkdir -p "$out/arm64-v8a"
cp "$root/bin/android/arm64-v8a/libopenbgi.so" "$out/arm64-v8a/"
sed -e 's/package="org.openbgi.port"/package="org.openbgi.uipreview"/' \
    -e 's/android:name=".CrashLogs"/android:name="org.openbgi.port.CrashLogs"/' \
    -e 's/android:label="OpenBGI"/android:label="OpenBGI UI Preview"/' \
    "$root/tools/android/AndroidManifest.xml" > "$out/AndroidManifest.xml"
ANDROID_PACKAGE_OUT="$out" ANDROID_PACKAGE_MANIFEST="$out/AndroidManifest.xml" \
    sh "$root/tools/android/package-smoke.sh"
