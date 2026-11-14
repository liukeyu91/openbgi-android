#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Optional separate test APK; never included in the emulator APK.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
sdk=${ANDROID_SDK_ROOT:?Set ANDROID_SDK_ROOT}
tools="$sdk/build-tools/${ANDROID_BUILD_TOOLS:-35.0.0}"
platform="$sdk/platforms/android-${ANDROID_PLATFORM:-35}/android.jar"
aapt2=${AAPT2_BIN:-"$tools/aapt2"}
zipalign=${ZIPALIGN_BIN:-"$tools/zipalign"}
signer=${APKSIGNER_JAR:-"$tools/lib/apksigner.jar"}
d8=${D8_JAR:-"$tools/lib/d8.jar"}
out="$root/bin/android-focus-probe"
key="$root/bin/android/.smoke-keystore.p12"
test -f "$key" # Build the local test engine APK first, using the same local test key.
mkdir -p "$out/classes" "$out/dex" "$out/assets/licenses"
javac --release 8 -encoding UTF-8 -classpath "$platform" -d "$out/classes" "$root/tools/android/tests/focus-probe/FocusProbeService.java"
jar cf "$out/probe.jar" -C "$out/classes" .
java -cp "$d8" com.android.tools.r8.D8 --release --min-api 28 --lib "$platform" --output "$out/dex" "$out/probe.jar"
"$aapt2" link --manifest "$root/tools/android/tests/focus-probe/AndroidManifest.xml" -I "$platform" -o "$out/unsigned.apk"
cp "$root/LICENSE" "$out/assets/licenses/OpenBGI-GPLv2.txt"
jar uf0 "$out/unsigned.apk" -C "$out/dex" classes.dex -C "$out" assets
"$zipalign" -f -P 16 4 "$out/unsigned.apk" "$out/aligned.apk"
java -jar "$signer" sign --ks "$key" --ks-pass pass:android --ks-key-alias openbgi-smoke --out "$out/focus-probe.apk" "$out/aligned.apk"
java -jar "$signer" verify "$out/focus-probe.apk"
