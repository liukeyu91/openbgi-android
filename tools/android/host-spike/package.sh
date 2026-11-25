#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ "$#" -ne 2 ]; then
    echo "Usage: $0 UPSTREAM_CHECKOUT BUILD_DIRECTORY" >&2
    exit 2
fi
upstream=$(CDPATH= cd -- "$1" && pwd)
mkdir -p "$2"
out=$(CDPATH= cd -- "$2" && pwd)
: "${ANDROID_NDK_HOME:?Set ANDROID_NDK_HOME to an Android NDK}"
: "${ANDROID_SDK_ROOT:?Set ANDROID_SDK_ROOT to an Android SDK}"
bin="$ANDROID_SDK_ROOT/build-tools/35.0.0"
toolchain="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin"
glue="${NATIVE_APP_GLUE:-$ANDROID_NDK_HOME/sources/android/native_app_glue}"
sdk="$ANDROID_SDK_ROOT/platforms/android-35/android.jar"
core="$upstream/bin/android/libopenbgi-arm64.so"
aapt="${AAPT2:-$bin/aapt2}"
align="${ZIPALIGN:-$bin/zipalign}"
sign="${APKSIGNER:-$bin/apksigner}"

mkdir -p "$out/assets" "$out/lib/arm64-v8a"
python3 "$src/make-demo.py" "$out/assets/host-spike.zip"
"$aapt" link -I "$sdk" --manifest "$src/AndroidManifest.xml" -A "$out/assets" -0 zip -o "$out/base.apk"

cp "$core" "$out/lib/arm64-v8a/libopenbgi-arm64.so"
cc="${ANDROID_CC:-$toolchain/aarch64-linux-android28-clang}"
"$cc" -std=c99 -D_POSIX_C_SOURCE=200809L -O2 -Wall -Wextra -Werror -fPIC \
    -I"$upstream/include" -I"$glue" -c "$src/host.c" -o "$out/host.o"
"$cc" -std=c99 -D_POSIX_C_SOURCE=200809L -O2 -fPIC -I"$glue" \
    -c "$glue/android_native_app_glue.c" -o "$out/native_app_glue.o"
"$cc" ${ANDROID_LDFLAGS:-} -shared -Wl,--no-undefined -Wl,-z,max-page-size=16384 \
    "$out/host.o" "$out/native_app_glue.o" "$out/lib/arm64-v8a/libopenbgi-arm64.so" \
    -landroid -laaudio -llog -o "$out/lib/arm64-v8a/libopenbgi_host_spike.so"

python3 - "$out" <<'PY'
from pathlib import Path
from sys import argv
from zipfile import ZipFile

out = Path(argv[1])
with ZipFile(out / "base.apk", "a") as apk:
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

