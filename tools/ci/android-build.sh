#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
deps=${1:?dependency directory}; deps=$(cd "$deps" && pwd)
export ANDROID_NDK_HOME="$deps/android-ndk-r27d" ANDROID_SDK_ROOT="$deps/sdk"
export FREETYPE_SOURCE="$deps/freetype-2.13.3" OGG_SOURCE="$deps/libogg-1.3.6" VORBIS_SOURCE="$deps/libvorbis-1.3.7"
export FFMPEG_SOURCE="$deps/ffmpeg-8.1.3" ANDROID_FFMPEG_PREFIX="$deps/ffmpeg-arm64"
export OPENBGI_SIGNING_MODE=unsigned
: "${SOURCE_DATE_EPOCH:?Set SOURCE_DATE_EPOCH to the source commit timestamp}"
cd "$root"
make -j4 android-native OPT='-O2 -g' "EXTRA_CFLAGS=-ffile-prefix-map=$root=/src -fdebug-prefix-map=$root=/src -fmacro-prefix-map=$root=/src" "FREETYPE_INCLUDE=$deps/arm64/include/freetype2" "FREETYPE_LIB=$deps/arm64/lib/libfreetype.a" "ANDROID_VORBIS_PREFIX=$deps/arm64"
llvm="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin"
"$llvm/llvm-objcopy" --only-keep-debug bin/android/arm64-v8a/libopenbgi.so bin/android/arm64-v8a/libopenbgi.so.debug
"$llvm/llvm-strip" --strip-debug bin/android/arm64-v8a/libopenbgi.so
python3 tools/ci/check-elf.py bin/android/arm64-v8a/libopenbgi.so "$llvm/llvm-readelf" > bin/android/hardening.json
upstream="$root/upstream-core"
make -C "$upstream" -j4 PLATFORM=android ARCH=arm64 "ANDROID_NDK=$ANDROID_NDK_HOME" "ANDROID_DEPS=$deps" \
    "LIB_LINK=-shared -Wl,-soname,libopenbgi-arm64.so,--exclude-libs,ALL,-z,max-page-size=16384" \
    OPT=-O2 VORBIS=stb MOVIES=none EMBED_FONTS= lib
sh tools/android/host-spike/package-compare.sh "$upstream" bin/android bin/android-full full
export ANDROID_COMPARE_LIB_DIR="$root/bin/android-full/arm64-v8a"
ANDROID_PACKAGE_OUT="$root/bin/android-full" ANDROID_STORAGE_VARIANT=saf sh tools/android/package-smoke.sh
sh tools/android/package-ui-preview.sh
