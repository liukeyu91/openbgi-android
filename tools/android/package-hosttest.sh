#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# First build with make android-native ANDROID_HOST_TEST=1 and the usual dependencies.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out="$root/bin/android-hosttest"
mkdir -p "$out"
sed -e 's/package="org.openbgi.port"/package="org.openbgi.hosttests"/' \
    -e 's/android:name=".CrashLogs"/android:name="org.openbgi.port.CrashLogs"/' \
    -e 's/android:label="OpenBGI"/android:label="OpenBGI Host Tests"/' \
    "$root/tools/android/AndroidManifest.xml" > "$out/AndroidManifest.xml"
ANDROID_PACKAGE_OUT="$out" ANDROID_PACKAGE_MANIFEST="$out/AndroidManifest.xml" \
    sh "$root/tools/android/package-smoke.sh"
