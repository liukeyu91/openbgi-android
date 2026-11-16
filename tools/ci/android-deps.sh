#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
deps=${1:?dependency directory}; mkdir -p "$deps"; deps=$(cd "$deps" && pwd)
python3 "$root/tools/ci/fetch.py" "$deps/cache"
cd "$deps"
unzip -qo cache/android-ndk-r27d-linux.zip
mkdir -p sdk/build-tools/35.0.0 sdk/platforms/android-35
unzip -qo cache/build-tools_r35_linux.zip -d sdk
cp -a sdk/android-15/. sdk/build-tools/35.0.0/
unzip -qo cache/platform-35-ext15_r01.zip -d sdk
cp -a sdk/android-35-ext15/. sdk/platforms/android-35/
for file in freetype-2.13.3 libogg-1.3.6 libvorbis-1.3.7; do tar -xzf "cache/$file.tar.gz"; done
export ANDROID_NDK_HOME="$deps/android-ndk-r27d"
cmake_android() {
 cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF "$@"
}
cmake_android -S freetype-2.13.3 -B ft-build -DCMAKE_INSTALL_PREFIX="$deps/arm64" -DFT_DISABLE_ZLIB=TRUE -DFT_DISABLE_BZIP2=TRUE -DFT_DISABLE_PNG=TRUE -DFT_DISABLE_HARFBUZZ=TRUE -DFT_DISABLE_BROTLI=TRUE
cmake --build ft-build --parallel 4; cmake --install ft-build
cmake_android -S libogg-1.3.6 -B ogg-build -DCMAKE_INSTALL_PREFIX="$deps/arm64" -DBUILD_TESTING=OFF
cmake --build ogg-build --parallel 4; cmake --install ogg-build
cmake_android -S libvorbis-1.3.7 -B vorbis-build -DCMAKE_INSTALL_PREFIX="$deps/arm64" -DCMAKE_PREFIX_PATH="$deps/arm64" -DBUILD_TESTING=OFF
cmake --build vorbis-build --parallel 4; cmake --install vorbis-build
