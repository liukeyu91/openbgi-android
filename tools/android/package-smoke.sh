#!/bin/sh
# This file is part of OpenBGI (https://openbgi.net).
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
sdk=${ANDROID_SDK_ROOT:?Set ANDROID_SDK_ROOT to an Android SDK directory}
ft=${FREETYPE_SOURCE:?Set FREETYPE_SOURCE to the FreeType source directory used for the library}
ogg=${OGG_SOURCE:?Set OGG_SOURCE to the libogg source directory used for the library}
vorbis=${VORBIS_SOURCE:?Set VORBIS_SOURCE to the libvorbis source directory used for the library}
ffmpeg=${FFMPEG_SOURCE:?Set FFMPEG_SOURCE to the FFmpeg source directory used for the library}
tools="$sdk/build-tools/${ANDROID_BUILD_TOOLS:-35.0.0}"
platform="$sdk/platforms/android-${ANDROID_PLATFORM:-35}/android.jar"
aapt2=${AAPT2_BIN:-"$tools/aapt2"}
zipalign=${ZIPALIGN_BIN:-"$tools/zipalign"}
signer=${APKSIGNER_JAR:-"$tools/lib/apksigner.jar"}
d8=${D8_JAR:-"$tools/lib/d8.jar"}

out=${ANDROID_PACKAGE_OUT:-"$root/bin/android"}
manifest=${ANDROID_PACKAGE_MANIFEST:-"$root/tools/android/AndroidManifest.xml"}
lib="$out/arm64-v8a/libopenbgi.so"
storage=${ANDROID_STORAGE_VARIANT:-full}
case "$storage" in
    full) apk_name=openbgi ;;
    saf)
        if [ -n "${ANDROID_PACKAGE_MANIFEST:-}" ]; then
            echo "The SAF variant uses the main manifest." >&2
            exit 1
        fi
        apk_name=openbgi-saf
        ;;
    *) echo "Unknown Android storage variant: $storage" >&2; exit 1 ;;
esac
# Unsigned CI artifacts and signed release APKs are separate outputs.
mode=${OPENBGI_SIGNING_MODE:-release}
case "$mode" in release|unsigned) ;; *) echo "Unknown signing mode: $mode" >&2; exit 1;; esac
epoch=${SOURCE_DATE_EPOCH:-$(git -C "$root" log -1 --format=%ct 2>/dev/null || true)}
: "${epoch:?Set SOURCE_DATE_EPOCH when building without Git history}"
test -f "$lib"
test -f "$ft/docs/GPLv2.TXT"
mkdir -p "$out"
stage=$(mktemp -d "$out/.package.XXXXXX")
trap 'rm -rf -- "$stage"' EXIT
trap 'exit 1' HUP INT TERM

if [ "$storage" = saf ]; then
    sed -e 's/package="org.openbgi.port"/package="org.openbgi.saf"/' \
        -e 's/android:name=".CrashLogs"/android:name="org.openbgi.port.CrashLogs"/' \
        -e 's/android:label="OpenBGI"/android:label="OpenBGI SAF"/' \
        -e '/android.permission.MANAGE_EXTERNAL_STORAGE/d' \
        -e '/android.permission.READ_EXTERNAL_STORAGE/d' \
        -e '/android.permission.WRITE_EXTERNAL_STORAGE/d' \
        -e '/android:requestLegacyExternalStorage=/d' \
        "$manifest" > "$stage/AndroidManifest.xml"
    manifest="$stage/AndroidManifest.xml"
fi

if [ "$mode" = release ]; then
    key=${OPENBGI_KEYSTORE:?Set OPENBGI_KEYSTORE to the persistent key outside the checkout}
    alias=${OPENBGI_KEY_ALIAS:-openbgi-smoke}
    expected=$(python3 "$root/tools/android/signing.py" --root "$root" --manifest "$manifest" --keystore "$key" --alias "$alias")
fi

"$aapt2" compile --dir "$root/tools/android/res" -o "$stage/resources.zip"
python3 "$root/tools/android/normalize-zip.py" "$stage/resources.zip" "$stage/resources-sorted.zip" --epoch "$epoch"
"$aapt2" link --manifest "$manifest" -I "$platform" --java "$stage" \
    --custom-package org.openbgi.port -o "$stage/unsigned.apk" "$stage/resources-sorted.zip"
mkdir -p "$stage/classes"
sh "$root/tools/android/generate-profiles.sh" "$stage/EngineProfiles.java"
python3 "$root/tools/android/generate-patch-profiles.py" "$stage/PatchProfiles.java"
javac --release 8 -encoding UTF-8 -classpath "$platform" -d "$stage/classes" \
    "$stage/org/openbgi/port/R.java" "$root/tools/android/AppLanguage.java" "$root/tools/android/AppMessages.java" \
    "$root/tools/android/Ui.java" "$root/tools/android/LibraryCover.java" "$root/tools/android/SaveBackup.java" "$root/tools/android/SaveRestore.java" "$root/tools/android/GameDataLock.java" \
    "$root/tools/android/InitialTextRepair.java" "$root/tools/android/InitialTextRepairDialog.java" \
    "$root/tools/android/ArchiveTextSamples.java" "$root/tools/android/EncodingDetector.java" "$root/tools/android/EncodingPreview.java" "$root/tools/android/GameEncoding.java" "$root/tools/android/ArchiveLanguage.java" \
    "$root/tools/android/ProgramCompatibility.java" "$root/tools/android/NativeProgramInspector.java" "$root/tools/android/DirectGame.java" "$root/tools/android/GameStorage.java" \
    "$root/tools/android/Blob.java" "$root/tools/android/DiscImage.java" "$root/tools/android/ContainerGame.java" "$root/tools/android/ResourceLayers.java" \
    "$stage/PatchProfiles.java" "$root/tools/android/PatchImport.java" "$root/tools/android/PatchDoctor.java" "$root/tools/android/TranslationRuntime.java" "$root/tools/android/PatchEvb.java" "$root/tools/android/PatchAplib.java" "$root/tools/android/PatchCpk.java" "$root/tools/android/PatchCamellia.java" \
    "$stage/EngineProfiles.java" "$root/tools/android/TouchInput.java" "$root/tools/android/InputKeys.java" "$root/tools/android/GameSettings.java" "$root/tools/android/SettingsDialog.java" "$root/tools/android/Viewport.java" "$root/tools/android/AudioFocusController.java" "$root/tools/android/GameAudioFocus.java" "$root/tools/android/InputDialogs.java" "$root/tools/android/GameTextCodec.java" "$root/tools/android/TextInput.java" "$root/tools/android/GameActivity.java" "$root/tools/android/LauncherActivity.java" "$root/tools/android/GameImport.java" "$root/tools/android/GamePatch.java" "$root/tools/android/CrashLogs.java" "$root/tools/android/RunReport.java"
jar cf "$stage/launcher.jar" -C "$stage/classes" .
python3 "$root/tools/android/normalize-zip.py" "$stage/launcher.jar" "$stage/launcher-sorted.jar" --epoch "$epoch"
java -cp "$d8" com.android.tools.r8.D8 --release --min-api 28 --lib "$platform" \
    --output "$stage" "$stage/launcher-sorted.jar"
mkdir -p "$stage/lib/arm64-v8a" "$stage/assets/licenses"
cp "$root/tools/android/build-info.txt" "$stage/assets/build-info.txt"
cp "$lib" "$stage/lib/arm64-v8a/"
cp "$root/LICENSE" "$stage/assets/licenses/OpenBGI-GPLv2.txt"
cp "$ft/docs/GPLv2.TXT" "$stage/assets/licenses/FreeType-GPLv2.txt"
cp "$ogg/COPYING" "$stage/assets/licenses/libogg.txt"
cp "$vorbis/COPYING" "$stage/assets/licenses/libvorbis.txt"
cp "$ffmpeg/COPYING.LGPLv2.1" "$stage/assets/licenses/FFmpeg-LGPLv2.1.txt"
cp "$root/tools/android/crypto/BouncyCastle-MIT.txt" "$stage/assets/licenses/BouncyCastle-MIT.txt"
awk '/^ \*\// {exit} /^ \*/ {sub(/^ \* ?/, ""); print}' \
    "$root/src/os/cp936.inc" > "$stage/assets/licenses/Unicode.txt"
awk '/^ \*\// {exit} /^ \*/ {sub(/^ \* ?/, ""); print}' \
    "$root/src/os/android/cp932.inc" >> "$stage/assets/licenses/Unicode.txt"
jar uf0 "$stage/unsigned.apk" -C "$stage" classes.dex -C "$stage" lib -C "$stage" assets
python3 "$root/tools/android/normalize-zip.py" "$stage/unsigned.apk" "$stage/canonical.apk" --epoch "$epoch"
"$zipalign" -f -P 16 4 "$stage/canonical.apk" "$stage/aligned.apk"
"$zipalign" -c -P 16 4 "$stage/aligned.apk"
if [ "$mode" = unsigned ]; then
    cp "$stage/aligned.apk" "$out/$apk_name-unsigned.apk"
    echo "Unsigned CI artifact: $out/$apk_name-unsigned.apk"
    exit 0
fi

# A release update must preserve the existing certificate. Passwords are
# supplied through process environment, never command-line arguments/logs.
export OPENBGI_KEY_PASS=${OPENBGI_KEY_PASS:-$OPENBGI_STORE_PASS}
java -jar "$signer" sign --ks "$key" --ks-pass env:OPENBGI_STORE_PASS --key-pass env:OPENBGI_KEY_PASS --ks-key-alias "$alias" \
    --v1-signing-enabled false --v2-signing-enabled true --v3-signing-enabled true --v4-signing-enabled false \
    --out "$stage/signed.apk" "$stage/aligned.apk"
java -jar "$signer" verify --verbose --print-certs "$stage/signed.apk" > "$stage/certificate.txt"
actual=$(sed -n 's/^Signer #1 certificate SHA-256 digest: //p' "$stage/certificate.txt")
if [ "$actual" != "$expected" ]; then
    echo "Signed APK certificate does not match the release identity" >&2
    exit 1
fi
"$zipalign" -c -P 16 4 "$stage/signed.apk"
cp "$stage/signed.apk" "$out/$apk_name.apk"
echo "Verified release APK: $out/$apk_name.apk"
