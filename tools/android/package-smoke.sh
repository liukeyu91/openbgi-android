#!/bin/sh
# This file is part of OpenBGI (https://openbgi.net).
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
sdk=${ANDROID_SDK_ROOT:?Set ANDROID_SDK_ROOT to an Android SDK directory}
ft=${FREETYPE_SOURCE:?Set FREETYPE_SOURCE to the FreeType source directory used for the library}
ogg=${OGG_SOURCE:?Set OGG_SOURCE to the libogg source directory used for the library}
vorbis=${VORBIS_SOURCE:?Set VORBIS_SOURCE to the libvorbis source directory used for the library}
tools="$sdk/build-tools/${ANDROID_BUILD_TOOLS:-35.0.0}"
platform="$sdk/platforms/android-${ANDROID_PLATFORM:-35}/android.jar"
aapt2=${AAPT2_BIN:-"$tools/aapt2"}
zipalign=${ZIPALIGN_BIN:-"$tools/zipalign"}
signer=${APKSIGNER_JAR:-"$tools/lib/apksigner.jar"}
d8=${D8_JAR:-"$tools/lib/d8.jar"}

out="$root/bin/android"
lib="$out/arm64-v8a/libopenbgi.so"
test -f "$lib"
test -f "$ft/docs/GPLv2.TXT"
mkdir -p "$out"
stage=$(mktemp -d "$out/.package.XXXXXX")
trap 'rm -rf -- "$stage"' EXIT
trap 'exit 1' HUP INT TERM

"$aapt2" link --manifest "$root/tools/android/AndroidManifest.xml" -I "$platform" -o "$stage/unsigned.apk"
mkdir -p "$stage/classes"
javac --release 8 -encoding UTF-8 -classpath "$platform" -d "$stage/classes" \
    "$root/tools/android/LauncherActivity.java" "$root/tools/android/GameImport.java"
jar cf "$stage/launcher.jar" -C "$stage/classes" .
java -cp "$d8" com.android.tools.r8.D8 --release --min-api 28 --lib "$platform" \
    --output "$stage" "$stage/launcher.jar"
mkdir -p "$stage/lib/arm64-v8a" "$stage/assets/licenses"
cp "$lib" "$stage/lib/arm64-v8a/"
cp "$root/LICENSE" "$stage/assets/licenses/OpenBGI-GPLv2.txt"
cp "$ft/docs/GPLv2.TXT" "$stage/assets/licenses/FreeType-GPLv2.txt"
cp "$ogg/COPYING" "$stage/assets/licenses/libogg.txt"
cp "$vorbis/COPYING" "$stage/assets/licenses/libvorbis.txt"
awk '/^ \*\// {exit} /^ \*/ {sub(/^ \* ?/, ""); print}' \
    "$root/src/os/cp936.inc" > "$stage/assets/licenses/Unicode.txt"
awk '/^ \*\// {exit} /^ \*/ {sub(/^ \* ?/, ""); print}' \
    "$root/src/os/android/cp932.inc" >> "$stage/assets/licenses/Unicode.txt"
jar uf0 "$stage/unsigned.apk" -C "$stage" classes.dex -C "$stage" lib -C "$stage" assets
"$zipalign" -f -P 16 4 "$stage/unsigned.apk" "$stage/aligned.apk"

# Keep using the previous local signing key so this build upgrades the
# installed test app and retains its game and saves.
key="$out/.smoke-keystore.p12"
if [ ! -f "$key" ]; then
    keytool -genkeypair -keystore "$key" -storetype PKCS12 -storepass android \
        -keypass android -alias openbgi-smoke -keyalg RSA -keysize 2048 \
        -validity 3650 -dname "CN=OpenBGI Smoke Test"
fi
java -jar "$signer" sign --ks "$key" --ks-pass pass:android --ks-key-alias openbgi-smoke \
    --out "$out/openbgi.apk" "$stage/aligned.apk"
java -jar "$signer" verify --verbose "$out/openbgi.apk"
"$zipalign" -c -P 16 4 "$out/openbgi.apk"
