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
python3 tools/ci/check-elf.py bin/android/arm64-v8a/libopenbgi.so "$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf" > bin/android/hardening.json
sh tools/android/package-smoke.sh
sh tools/android/package-ui-preview.sh
