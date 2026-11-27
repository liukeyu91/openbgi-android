#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$src/../../.." && pwd)
if [ "$#" -ne 2 ]; then
    echo "Usage: $0 UPSTREAM_CHECKOUT BUILD_DIRECTORY" >&2
    exit 2
fi
upstream=$(CDPATH= cd -- "$1" && pwd)
mkdir -p "$2"
out=$(CDPATH= cd -- "$2" && pwd)
: "${ANDROID_NDK_HOME:?Set ANDROID_NDK_HOME to an Android NDK}"
: "${ANDROID_SDK_ROOT:?Set ANDROID_SDK_ROOT to an Android SDK}"
: "${FREETYPE_INCLUDE:?Set FREETYPE_INCLUDE to the ARM64 FreeType headers}"
: "${FREETYPE_LIB:?Set FREETYPE_LIB to the ARM64 static FreeType library}"
bin="$ANDROID_SDK_ROOT/build-tools/35.0.0"
toolchain="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin"
glue="${NATIVE_APP_GLUE:-$ANDROID_NDK_HOME/sources/android/native_app_glue}"
sdk="$ANDROID_SDK_ROOT/platforms/android-35/android.jar"
d8="${D8_JAR:-$bin/lib/d8.jar}"
core="$upstream/bin/android/libopenbgi-arm64.so"
host="$root/src/host/android"
aapt="${AAPT2:-$bin/aapt2}"
align="${ZIPALIGN:-$bin/zipalign}"
sign="${APKSIGNER:-$bin/apksigner}"

mkdir -p "$out/assets" "$out/lib/arm64-v8a"
stage=$(mktemp -d "$out/java.XXXXXX")
trap 'rm -rf -- "$stage"' EXIT
python3 "$src/make-demo.py" "$out/assets/host-spike.zip" --text=cp932
"$aapt" compile --dir "$src/res" -o "$stage/res.zip"
"$aapt" link -I "$sdk" --manifest "$src/AndroidManifest.xml" -A "$out/assets" -0 zip \
    --java "$stage" -o "$out/base.apk" "$stage/res.zip"
mkdir -p "$stage/classes"
javac --release 8 -encoding UTF-8 -classpath "$sdk" -d "$stage/classes" \
    "$src/HostActivity.java" "$stage/org/openbgi/hostspike/R.java"
jar cf "$stage/host.jar" -C "$stage/classes" .
java -cp "$d8" com.android.tools.r8.D8 --min-api 28 --lib "$sdk" --output "$stage" "$stage/host.jar"

cp "$core" "$out/lib/arm64-v8a/libopenbgi-arm64.so"
cc="${ANDROID_CC:-$toolchain/aarch64-linux-android28-clang}"
"$cc" -std=c99 -D_POSIX_C_SOURCE=200809L -O2 -Wall -Wextra -Werror -fPIC \
    -I"$upstream/include" -I"$host" -I"$glue" -c "$src/activity.c" -o "$out/activity.o"
"$cc" -std=c99 -D_POSIX_C_SOURCE=200809L -O2 -fPIC -I"$glue" \
    -c "$glue/android_native_app_glue.c" -o "$out/native_app_glue.o"
storage="$root/src/os"
set --
for file in \
    "$host"/*.c "$storage/android/audio_output.c" "$storage/android/clock.c" \
    "$storage/android/renderer.c" "$storage/android/storage_paths.c" "$storage/android/storage_settings.c" \
    "$storage/android/sjis.c" "$storage/posix/audio.c" \
    "$storage/posix/files.c" "$storage/posix/font.c" "$storage/posix/paths.c" "$storage/posix/process.c" "$storage/posix/native.c" \
    "$storage/posix/disc.c" "$storage/posix/iso.c" "$storage/posix/udf.c" "$storage/posix/zip.c" \
    "$storage/os_common.c" "$root/src/core/deflate.c"
do
    relative=${file#"$root/"}
    object="$out/$(printf '%s' "${relative%.c}" | tr / _).o"
    "$cc" -std=c99 -O2 -D_GNU_SOURCE -DBGI_ANDROID -fPIC -fvisibility=hidden -ffunction-sections \
        -I"$upstream/include" -I"$root/inc" -I"$FREETYPE_INCLUDE" -c "$file" -o "$object"
    set -- "$@" "$object"
done
"$cc" ${ANDROID_LDFLAGS:-} -shared -Wl,--gc-sections,--no-undefined,--exclude-libs,ALL,-z,max-page-size=16384 "$@" \
    "$out/activity.o" "$out/native_app_glue.o" "$out/lib/arm64-v8a/libopenbgi-arm64.so" \
    "$FREETYPE_LIB" -landroid -laaudio -llog -lEGL -lGLESv2 -lm -o "$out/lib/arm64-v8a/libopenbgi_host_spike.so"

python3 - "$out" "$stage" <<'PY'
from pathlib import Path
from sys import argv
from zipfile import ZipFile

out = Path(argv[1])
with ZipFile(out / "base.apk", "a") as apk:
    apk.write(Path(argv[2]) / "classes.dex", "classes.dex")
    for library in sorted((out / "lib" / "arm64-v8a").glob("*.so")):
        apk.write(library, library.relative_to(out).as_posix())
PY
"$align" -P 16 -f 4 "$out/base.apk" "$out/aligned.apk"
key="$out/spike-debug.jks"
if [ ! -f "$key" ]; then
    keytool -genkeypair -keystore "$key" -storepass android -keypass android \
        -alias spike -dname 'CN=OpenBGI Host Spike' -keyalg RSA -keysize 2048 -validity 365 >/dev/null
fi
"$sign" sign --ks "$key" --ks-key-alias spike --ks-pass pass:android \
    --out "$out/OpenBGI-Host-Spike-arm64.apk" "$out/aligned.apk"
"$sign" verify "$out/OpenBGI-Host-Spike-arm64.apk"
"$align" -c -P 16 4 "$out/OpenBGI-Host-Spike-arm64.apk"
printf '%s\n' "$out/OpenBGI-Host-Spike-arm64.apk"

