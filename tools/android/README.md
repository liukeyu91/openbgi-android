# OpenBGI for Android

This build is configured for the Chinese Fureraba HD sample. It uses
profile `1.616`, product `FriendToLoverHD` and GBK display text. The app
loads a user-supplied copy of the active game archives from `files/game`
inside the app's sandbox. On first launch, the system folder picker
lets the user choose the extracted game folder and import its archives.
It also looks for `game` in the app-specific
external files directory when the internal copy is absent. Configuration
is stored in the internal `files/openbgi` directory.

## Install and import

Install the signed APK on an ARM64 device running Android 9 or later.
Keep the app installed when updating from the smoke build to retain
its local game and saves. With a game already imported, the app opens
it directly.

For a new installation, put the extracted Chinese Fureraba HD folder
on the phone, for example in a subfolder of Downloads. Open OpenBGI
and select the game folder itself, which contains `system.arc` and
`Archive/`. The importer copies the game archives to app storage.
Allow enough free space for that copy.

## Build

Use the Android NDK and ARM64 FreeType, libogg and libvorbis. The smoke build was compiled
against API 28 with NDK r27d and FreeType 2.13.3 (commit
`42608f77f20749dd6ddc9e0536788eaad70ea4b5`), libogg 1.3.6 (commit
`be05b13e98b048f0b5a0f5fa8ce514d56db5f822`) and libvorbis 1.3.7 (commit
`0657aee69dec8508a0011f47f3b69d7538e9d262`). FreeType is linked under
its GPLv2 option. The renderer loads Noto CJK from the Android system
font directory. For GBK dialogue it uses the font's typographic cell
height and reserves room for Latin glyphs whose ink extends past the
default half-width cell.

```sh
cmake -S "$FREETYPE_SOURCE" -B "$FT_BUILD" \
    -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DBUILD_SHARED_LIBS=OFF \
    -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_BROTLI=ON -DFT_DISABLE_HARFBUZZ=ON \
    -DFT_DISABLE_PNG=ON -DFT_DISABLE_ZLIB=ON
cmake --build "$FT_BUILD"
for codec in "$OGG_SOURCE" "$VORBIS_SOURCE"; do
    build="$CODEC_BUILD/$(basename "$codec")"
    cmake -S "$codec" -B "$build" \
        -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_INSTALL_PREFIX="$CODEC_PREFIX" -DCMAKE_INSTALL_LIBDIR=lib \
        -DCMAKE_PREFIX_PATH="$CODEC_PREFIX"
    cmake --build "$build"
    cmake --install "$build"
done
make android-native ANDROID_NDK_HOME="$ANDROID_NDK_HOME" \
    FREETYPE_INCLUDE="$FREETYPE_SOURCE/include" FREETYPE_LIB="$FT_BUILD/libfreetype.a" \
    ANDROID_VORBIS_PREFIX="$CODEC_PREFIX"
ANDROID_SDK_ROOT="$ANDROID_SDK_ROOT" FREETYPE_SOURCE="$FREETYPE_SOURCE" \
    OGG_SOURCE="$OGG_SOURCE" VORBIS_SOURCE="$VORBIS_SOURCE" \
    sh tools/android/package-smoke.sh
```

The packager uses SDK Platform 35, Build Tools 35.0.0 and JDK tools.
It writes `bin/android/openbgi.apk` with the workspace-local signing key.
Keep using the same key when installing updates over this build.

## Device check

Tap **从头开始**, keep the default name and tap **结束输入**. Check that
the name reads **青叶／恭介** and the first line reads **想要女朋友！**.
Single taps map to left click, two-finger taps to right click, vertical
swipes to the mouse wheel and Back to Escape. Use `adb logcat -s OpenBGI`
to see boot errors and screen titles.

The mixer uses AAudio for stereo output and libvorbisfile for BW codec 3
music, voice and effects. Audio pauses in the background. Native text
editing and child windows are subsequent work.
