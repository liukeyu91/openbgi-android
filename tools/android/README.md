# OpenBGI for Android

Android host for OpenBGI, using Activity + SurfaceView + JNI.
Supports ARM64 devices running Android 9 or newer. Game resources are supplied
by the player and are not bundled with the application.

The launcher provides a game library, directory import, per-game settings,
search, sorting and user-selected covers. The main application and independent
UI Preview package keep separate data and signing keys.

## Text input

Script text fields use Android input controls. Long-press a supported field
and choose **初始文字编码** to preview its original bytes as SJIS, GBK or UTF-8.
A valid selection replaces that field using the game's selected encoding.
Input remains subject to the script's byte and character limits.

Release-specific changes and test results are linked from the
[Android update history](../../docs/android-release-history.md).

## Install and import

ARM64, Android 9 or newer. Upgrade using the same package and signing key;
do not uninstall if you want to retain app-local saves.

The launcher lists imported games. Choose **添加游戏** and select the
extracted game's root containing `system.arc` or `ipl._bp`. Import copies
the complete directory, including subdirectories, empty directories, non-ARC
files and `games.json`, retaining original names and bytes. It needs enough
space for a second copy. Keep the app in the foreground until import completes.
The app reserves `.openbgi` at the root for its private configuration; choose
the original PC game folder rather than another installation's internal folder.

Each new import has a UUID and lives directly at `files/games/<id>/`.
Relative saves stay with the game; registry and app-data paths are isolated in
that game's `.openbgi/`. Settings are saved atomically in `.openbgi/settings.ini`;
legacy `.openbgi/encoding.txt` is used when no encoding override exists.
New imports use **自动识别**: the bounded scene sampler runs after copying,
and the selected `sjis`, `gbk` or `utf8` value is saved for the native engine.
No-text, uncertain and mixed results remain unresolved and cannot start the VM
until an encoding is selected. Results and the automatic/manual mode are stored
in `settings.ini`; scans do not repeat on each line or every launch.
Legacy `encoding.txt=sjis` without an explicit saved encoding is re-evaluated on
first launch. Explicit `settings.ini` encodings and legacy GBK/UTF-8 overrides
are retained. To re-evaluate a previously manual setting, select **自动识别**
in **游戏设置**, save, and launch the game.
Manual choices remain `sjis` for Japanese originals, `gbk` for the tested Chinese
Fureraba HD, and `utf8` for UTF-8 patches. Automatic detection reads imported
resources, not translation overlays packed inside a Windows EXE; those overlays
must first be extracted into the game directory.
The **编码预览** button reads a bounded selection of scene strings,
offers a suggestion and shows the same bytes under all three encodings.
Choose a sample from the selector, compare its columns, select an encoding and
then save the settings page. Selecting or cancelling the preview alone never
writes settings. Ambiguous/unsupported samples require manual selection.
The preview does not execute the game or guarantee its actual first line.
Default-name/menu mixed encodings still need further work. See
[0.8.0 encoding evidence](../../docs/android-encoding-0.8.md).

The default profile is automatic; no fixed engine profile or product name is passed. The engine
uses the game's `games.json`, then its built-in game table. If no game-local
table exists, the launcher supplies a table that adds `FriendToLoverHD` with
profile `1.616`; built-in entries remain available. Existing tables are preserved.
Unknown games can use an appropriate `games.json` entry. A manual profile override
in the settings page passes only the selected built-in `--engine` option; product
detection remains automatic. See `docs/versions.md` and `src/core/version.c` for the schema.

Settings also offer aspect-fit, stretch and integer scaling (aspect-fit downscale
when one integer multiple cannot fit), reading/swipe-wheel or left-button-drag
touch presets, the screen key toolbar and gamepad A/B swap. Single tap is left
click, long press or two-finger tap is right click. Drag releases on cancellation,
focus loss and backgrounding, including release outside the game viewport.
The renderer, touch mapping and IME share the same viewport calculation.
The default screen keys are Escape/menu, wheel-up/backlog, S/save and L/load;
these send input to the game, whose shortcut configuration determines the action.
Each can be rebound in the settings page. Settings take effect next session.
Keyboard letters, digits, F1-F12, modifiers, navigation keys and basic gamepad
buttons/axes are mapped to engine Windows VK codes. See the tested scope in
[0.6.0 validation](../../docs/android-input-settings-0.6.md).

Upgrading migrates `files/game` to `files/games/legacy`, together with the old
`files/openbgi` configuration and saves, and defaults that entry to GBK. A legacy
external `files/game` copy is also recognized, with isolated internal configuration.
These migrations still need device validation; the APK signature matches 0.3.

Each game session uses a separate `:engine` process, terminated after engine
shutdown so the VM globals are not reused for another game. Exit the running
game before switching or deleting. Deletion prompts explicitly because it also
deletes that entry's app-local saves. The legacy external source directory is
retained when deleting its library entry.

The launcher provides **导出运行日志** even before importing a game. It exports
engine logs, Java exceptions, native fatal signal records and available Android
11+ system exit information. Recorded fatal native signals exit directly to
avoid handler recursion and do not guarantee a system tombstone/full stack.
Oversized legacy signal logs reset at engine startup. See the 0.9.1 report.
No root is needed. GameActivity + SurfaceView + JNI now hosts the engine.
The single-line OS_Edit control supports the Android IME, byte limits in the
selected encoding, selection, colours and viewport positioning. Single/two-field
script dialogs and the name/birthday profile also use native Android forms.
OS_MessageBox waits for the actual button result. Backgrounding blocks the VM
at its message pump, suspends the existing audio mixer and freezes both engine
clocks; wall-clock save timestamps continue normally. AudioFocusRequest now gates VM, audio and engine clocks together. Transient and
duck requests pause and resume automatically on gain. Permanent loss or a noisy
output-device broadcast keeps a visible pause overlay until the player chooses
**继续游戏**. Backgrounding abandons focus; stale callbacks cannot restart the
engine. No phone/Bluetooth runtime permission is requested. Actual cellular
calls and physical headset/Bluetooth disconnections still need hardware testing.
See docs/android-audio-focus-0.5.1.md for the tested scope. See docs/android-device-validation-0.5.md.
This release does not claim compatibility with every upstream game.

## Validation

`sh tools/android/tests/run-text-repair-tests.sh` checks 29 conversion cases
and the existing 10 input-filter cases without Android or game data.
Device checks cover the operation menu, cancellation, per-field repair,
backgrounding, stale previews, strict limits and a single physical Enter submit.
Real-game entry and IME repaint remain open; see the 0.9.1 report.
Isolated host fixtures are in tools/android/hosttest.c and link only with
ANDROID_HOST_TEST=1.

`sh tools/android/tests/run-encoding-tests.sh` runs 39 checks with JDK 8+.
It uses self-authored scene/archive fixtures, including DSC compression,
loose overrides, invalid data, ambiguous text and cancellation. No game is needed.

The presentation backend now uses EGL + GLES2: a persistent logical canvas
texture, merged dirty-region uploads, GPU nearest-neighbor scaling and a full
GPU quad/black-border redraw. It no longer locks/scales the physical window
buffer on the CPU. CPU game compositing and source pixel conversion remain.
Choreographer grants at most one presentation per frame callback, with EGL
swap interval 1; unchanged frames do not upload or swap. The UI/lifecycle mutex
is released before any EGL/GLES driver call. Surface generations invalidate
the renderer, and background/resume rebuilds from the retained CPU canvas.
Driver failures are logged and retried with a one-second backoff; graphics
calls and cleanup stay on the VM thread. See [0.7.0 device evidence](../../docs/android-egl-0.7.md).

Run `sh tools/android/tests/run-input-tests.sh` with JDK 8+ and a host C99 compiler
for 48 input/settings checks and 270 native/Java viewport comparisons. The script
accepts JAVA, JAVAC and CC executable overrides. Test sources are excluded from
the production APK. Physical controller/mouse testing remains pending.

Host tests cover recursive copies, original filename case, Unicode paths, empty
directories, two game save directories, traversal rejection, duplicate paths,
short reads, interrupted-import retry and preserving an existing game's saves.
The engine tests cover the Fureraba JSON entry, another built-in game identifier,
and explicit GBK / UTF-8 / SJIS options. These are not two full-game playthroughs.
The current host suite has the same two failing test groups as its earlier baseline:
`bpc` and `win32wide`. Native/Java compilation, APK alignment and signing checks pass.
Subsequent device validation on Xiaomi Mi 8 (Android 10) passed SAF full-directory
import (all 75 original files matched SHA-256), Fureraba startup, imported autosave
loading, new normal save creation, cold-start loading, GBK persistence, engine
process exit/restart and actual ZIP export after a controlled SIGABRT. See
`docs/android-device-validation-0.4.md` in the publish checkout. Kanojo Step was subsequently prepared and tested; see `docs/android-kanojostep.md`. Legacy migration remains unverified because the device's
old app installation contained no game data or saves.

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
Single taps map to left click, long press/two-finger taps to right click, vertical
swipes to the mouse wheel in the reading preset, and Back to Escape. Use `adb logcat -s OpenBGI`
to see boot errors and screen titles.

The mixer uses AAudio for stereo output and libvorbisfile for BW codec 3
music, voice and effects. Audio pauses in the background. Android IME editing
and native input/dialog forms are implemented; video remains pending.

## Kanojo Step (local 0.4.1 validation)

See [Kanojo Step preparation and validation](../../docs/android-kanojostep.md).
Its Chinese package embeds translated loose resources inside a Windows EXE;
these must be statically extracted into a separate game copy before importing.
The game profile is 1.636 and its translated display text uses GBK.
Startup, early Chinese dialogue, quick save/load, cold-start quick load and
switching back to the Fureraba normal save were verified on Mi 8. This is
not a complete-playthrough compatibility claim.
