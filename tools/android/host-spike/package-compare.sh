#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
upstream=$(CDPATH= cd -- "${1:?Pass the upstream v1.0 checkout}" && pwd)
formal=$(CDPATH= cd -- "${2:?Pass the Android build output containing arm64-v8a}" && pwd)
mkdir -p "${3:?Pass a separate comparison build directory}"
out=$(CDPATH= cd -- "$3" && pwd)
test "$formal" != "$out"
variant=${4:-compare}
case "$variant" in
    compare|full|saf) ;;
    *) echo "Unknown Android package variant: $variant" >&2; exit 1 ;;
esac
core="$upstream/bin/android/libopenbgi-arm64.so"
lib="$formal/arm64-v8a/libopenbgi.so"
test -f "$core" && test -f "$lib"
host="$root/src/host/android"
cc=${ANDROID_CC:-"${ANDROID_NDK_HOME:?}/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android28-clang"}
mkdir -p "$out/arm64-v8a"
cp "$lib" "$core" "$out/arm64-v8a/"
obj=$(mktemp -d "$out/.bridge.XXXXXX")
trap 'rm -rf -- "$obj"' EXIT
set --
for source in "$host"/*.c "$host/surface/session.c"; do
    object="$obj/$(basename "$source" .c).o"
    "$cc" -std=c99 -O2 -D_GNU_SOURCE -DBGI_ANDROID -fPIC -fvisibility=hidden -ffunction-sections \
        -I"$root/inc" -I"$upstream/include" -c "$source" -o "$object"
    set -- "$@" "$object"
done
"$cc" ${ANDROID_LDFLAGS:-} \
    -shared -Wl,--no-undefined,--gc-sections,-soname,libopenbgi_compare.so,-z,max-page-size=16384 \
    "$@" "$lib" "$core" -landroid -llog \
    -o "$out/arm64-v8a/libopenbgi_compare.so"
if [ "$variant" != compare ]; then
    ANDROID_PACKAGE_OUT="$out" ANDROID_STORAGE_VARIANT="$variant" \
        ANDROID_COMPARE_LIB_DIR="$out/arm64-v8a" sh "$root/tools/android/package-smoke.sh"
    exit 0
fi
sed -e 's/package="org.openbgi.port"/package="org.openbgi.hostcompare"/' \
    -e 's/android:name=".CrashLogs"/android:name="org.openbgi.port.CrashLogs"/' \
    -e 's/android:label="OpenBGI"/android:label="OpenBGI Compare"/' \
    "$root/tools/android/AndroidManifest.xml" > "$out/AndroidManifest.xml"
ANDROID_PACKAGE_OUT="$out" ANDROID_PACKAGE_MANIFEST="$out/AndroidManifest.xml" \
    ANDROID_STORAGE_VARIANT=full ANDROID_COMPARE_LIB_DIR="$out/arm64-v8a" OPENBGI_SIGNING_MODE=unsigned \
    sh "$root/tools/android/package-smoke.sh"
