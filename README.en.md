# OpenBGI

[简体中文](README.md) | English

OpenBGI is a C99 reimplementation of the BGI / Ethornell engine. This repository provides an Android port and preserves the upstream source and commit history.

## Android

Requires Android 9 or later on an ARM64 device. The app provides the engine; you supply the game files.

- Run directly from complete game folders without copying, or use copy import; manage multiple games and custom covers.
- Separate settings and saves for each game.
- Choose Simplified Chinese, Traditional Chinese, English, Japanese, Korean, Spanish, French, or German in About → App language. The interface follows the system language by default.
- Import and export save ZIPs, including global progress.
- Check script instructions after import and save the results.
- Detect text encoding automatically, with manual SJIS, GBK, and UTF-8 selection.
- Touch controls, screen shortcuts, keyboard support, and basic controller mappings.
- In-game text input, system dialogs, and runtime log export.
- Built-in FFmpeg playback for standard movie formats, with audio/video synchronization, tap to skip, and background pause.
- Translation profiles can override runtime fonts, register game-provided fonts, and update generated resources while preserving manual overrides.

## Getting started

1. Download an APK from [Releases](https://github.com/liukeyu91/openbgi-android/releases) and prepare a complete, extracted game directory.
2. Select **Add game** and choose the game root containing `system.arc` or `ipl._bp`. The full APK can play from the original directory. The SAF APK copies the selected folder into app storage using the system picker.
3. Launch the game. If encoding detection is uncertain, preview the text and choose an encoding in **Game settings** (`游戏设置`).

Direct directory mode reads game resources in place and stores saves and extracted translations in the app. Recognized saves are copied on first use. Copy import remains available for a separate copy of the entire game.
Updating with the same package name and signing certificate preserves app data. Export app saves before removing a library entry or uninstalling.

Import automatically extracts supported Chinese translation packages and applies recognized engine and encoding settings. Existing games can use **Process translation patch** (`处理汉化补丁`) from the game-card menu after exiting the game. Resource extraction runs locally on the device and supports external EVB, EVB embedded in an EXE, and CPK v2 with its matching key. Windows EXE and DLL code is not executed.

For MakingHD imports affected by the equal-size compression bug, update to **0.10.21-preview** or later, exit the game, and run **Process translation patch** once. It repairs files that exactly match the previously extracted compression envelopes, while preserving other overrides and saves. Reimporting the game is unnecessary.

The Android port is still under development. Compatibility needs to be verified per title. Static instruction checks and successful resource extraction do not establish full-game compatibility. Streamed BF_Movie formats remain unsupported; see the [movie implementation notes](docs/android-movie.md) (Chinese) for details.

See the [1.0.1-android release notes](docs/android-1.0.1.md) and [known limitations](docs/known-issues.md) for changes and test coverage.

## AI collaboration

The Android port in this repository is developed collaboratively by its maintainer and Codex

## Documentation

- [Android changelog](CHANGELOG.en.md)
- [Android usage and build instructions](tools/android/README.md) (Chinese)
- [Android architecture and compatibility](docs/android.md) (Chinese)
- [Signing, CI, and reproducible builds](docs/build-integrity.md) (Chinese)
- [Engine versions and game profiles](docs/versions.md)
- [Offline translation-resource detection and conversion](tools/translation_compat/README.md) (Chinese)
- [GPL-2.0 license](LICENSE)

The upstream notes below cover the original engine project and its desktop and WebAssembly targets. The game-status table reflects upstream testing; Android compatibility should be checked per title.

## Upstream OpenBGI

OpenBGI is an open-source reimplementation, in C99, of Ethornell's
"Buriko General Interpreter" (BGI), the visual-novel engine.  It is
reverse-engineered function by function from the `tayutama.exe` build
(ver 1.69, build 444.2, with the "Wave Master" sound library) and
extended to run the scripts of the engine's other builds, from ver 1.58
to ver 1.669 (`docs/versions.md`).  Every function keeps the original's
results for the same inputs, including its result codes and quirks.  The
Windows specifics sit behind an OS layer (`inc/bgi/os.h`) with a Win32
back end, a Linux back end (X11, FreeType, ALSA) and a WebAssembly back
end (a canvas, the game's files over a websocket; `docs/wasm.md`).

* Website: [OpenBGI.net](https://openbgi.net)
* Discord: <https://discord.gg/3zFTJWUNt7>

**OpenBGI is a work in progress.**  Expect bugs and missing pieces;
please report bugs and issues in our
[Discord](https://discord.gg/3zFTJWUNt7).

**OpenBGI is not a Visual Novel development platform.**  If you wish to
develop a VN on the Ethornell/BGI platform, then you must contact Buriko
Co., Ltd.

## Games

The games we have been working on, with the engine build each one needs
(`docs/versions.md` has the details of the builds):

| Game | Product name | Engine version | Status |
|---|---|---|---|
| Nursery Rhyme (trial) | `NurseryRhymeTrial` | 1.58 | Very playable |
| Nursery Rhyme | `NurseryRhyme` | 1.64 | Very playable |
| Itsuka, Todoku, Ano Sora ni. | `ItsukaTodokuAnoSorani` | 1.66 | Mostly playable |
| Tayutama -Kiss on my Deity- (trial; the reference build) | `Tayutama` | 1.69 build 444 | Very playable |
| Tayutama -Kiss on my Deity- | `Tayutama` | 1.69 build 451 | Very playable |
| Tayutama -It's happy days- | `TayutamaHD` | 1.69 build 472 | Very playable |
| Prism Rhythm | `PrismRhythm` | 1.494 | Very playable |
| Diamic Days | `DiamicDays` | 1.529 | Very playable |
| Gakuou -THE ROYAL SEVEN STARS- | `Gackoh` | 1.535 | Very playable |
| Gakuou -It's Heartful Days!!- | `Gackoh_HD` | 1.547 | Very playable |
| Hanairo Heptagram | `HanairoHeptagram` | 1.553 | Very playable |
| Magical Charming! | `MagicalCharming!` | 1.573 | Very playable |
| Sekai to Sekai no Mannaka de | `SekaiToSekaiNoMannakaDe` | 1.588 | Very playable |
| Unmei Senjou no Phi | `UnmeisenjouNoPhi` | 1.599 | Very playable |
| Kodomo no Asobi | `KodomoNoAsobi` | 1.616 | Playable |
| Sen no Hatou, Tsukisome no Kouki (trial) | `SenNoHatouTsukisomeNoKouki` | 1.636 | Testing in progress |
| Tayutama 2 -After Stories- | `Tayutama2AS` | 1.640 | Playable |
| Wakabairo no Quartet | `WakabaironoQuartet` | 1.653 | Playable |
| Nekotsuku, Sakura. | `NekoTsukuSakura` | 1.654 | Somewhat playable, glitchy |
| Madohi Shiroki no Kamikakushi | `MadoiShirokinoKamikakushi` | 1.659 | Boots, but crashes in prolouge |
| Yumahorome | `Yumahorome` | 1.662 | Boots, but crashes in prolouge |
| Arcana Alchemia | `ArcanaAlchemia` | 1.667 | Testing in progress |
| Haruka Ao no Hanayome ni | `HarukaAonoHanayomeni` | 1.669 | Testing in progress |

### Definitions

* Verified - Extensive test against all aspects of the game; Visuals, paths, choices, saving, loading, system settings, and a complete 1:1 verification against executed bytecode. Visuals are indistinguishable from the original release.
* Very playable - No noticeable glitches were encountered during gameplay. Skipping, saving, reloading, sound, cutscenes and choices all work as expected through extended gameplay. Not comprehensively tested, but confidently playable from start to finish.
* Mostly playable - Same as very playable, but more limited gameplay testing.
* Playable - Some visual glitches noticed, or incomplete gameplay testing.
* Somewhat playable - Incomplete gameplay testing. Obvious glitches noticed. May crash.
* Boots - Title screen reached, but will crash in-game. Gameplay testing impossible.
* Testing in progress - Runs basic boot routine, but nothing more is known at this time.

### Testing scope

Apart from the trial of Sen no Hatou, Tsukisome no Kouki (August), whose
build is surveyed but not yet playable, Fortune Arterial (August),
which boots (`--no-disc-check`), and Yoake mae yori Ruri iro na
(August, 1.658.2 on the 1.659 profile), whose prologue plays, only Lump
of Sugar games have been tested so far.  We are interested in testing other BGI-based games: if you have one, tell us how it goes in
our [Discord](https://discord.gg/3zFTJWUNt7).

Testing happens on a case-by-case basis, and to exercise newly implemented opcodes and subsystems. The is currently no extensive testkit again individual games, except booting up a game and actually playing it.

## Building

    make              Linux (gcc or clang; X11, Xext, FreeType, optionally
                      ALSA, fontconfig and libvorbisfile via pkg-config)
    make win          Windows through MinGW (i686-w64-mingw32-gcc)
    make android-core Android ARM64 static core (set ANDROID_NDK_HOME)
    make android-native Android NativeActivity library (see tools/android/README.md)
    make wasm         the browser through Emscripten (emcc) -> bin/wasm/openbgi.html;
                      `node tools/wasm/server.js <game dir>` serves it (docs/wasm.md)
    make tools        the command-line tools: bin/bpasm, the assembler /
                      disassembler of the game's programs (docs/asm.md);
                      bin/bgiscn, the decompiler / compiler of the scenario
                      files (docs/scenario.md); bin/bgidump and bin/bgipack,
                      which turn a game's archives into editable files and
                      back, for translations (docs/dump.md)
    make check        syntax-check every translation unit with -pedantic
    make test         build and run tests/
    make clean

Plain `make` only, no autotools / CMake.  Objects go to `obj/<platform>/`,
binaries to `bin/`.  `VORBIS=vorbisfile|stb|none` selects the Vorbis
decoder, `OPT=...` the optimisation flags; `docs/building.md` has the
details, the optional dependencies and the state of the Windows and
WebAssembly builds.

The Android target builds `bin/android/arm64-v8a/libopenbgi-core.a`
with the NDK Clang toolchain on Linux. It defaults to API 28
(`ANDROID_API`). The Android app linking it supplies the platform
services declared in `inc/bgi/os.h`.
The NativeActivity smoke target supplies those services for the initial
Fureraba HD startup test. See [the Android build and test steps](tools/android/README.md).

## Running

    cd <the game's directory>   # ipl._bp, system.arc, sysprg.arc, ...
    /path/to/bin/bgi [arguments as for the original]

or `bin/bgi "<the game's directory>"` from anywhere: the directory given
(or the one holding the archive or program file given) becomes the game
directory, as if the engine were started there.

or just `bin/bgi` from anywhere: started outside a game directory (no
`system.arc` or `ipl._bp` where it runs, no path given) the engine opens
a "Select game" window - the games it has run before, or a directory
to pick, with the game it finds there named; "Advanced" uncovers the
engine profile and extra options, Launch starts it and records the
directory in `games.json` for the next time.

The engine build a game needs is chosen from the game's `ipl._bp` through
`games.json`, which also holds the profiles themselves: the engine writes
the default one next to `bgi` on its first run (a game's own directory
may carry one too) - see `docs/versions.md`.  `--engine=NAME` forces a
profile, `--list-engines` prints them, `--productname=NAME` gives the
product name the game's scripts check for (with `--engine=`, a game that
`games.json` does not know runs without an entry), `--games=PATH` names
another table, `--enable=FAM:OP` / `--disable=FAM:OP` override single
opcodes; these options are removed before the rest of the command line
reaches the original's parser.  The original's own options such as `"Do
not use mutex."` (needed for a second instance in the same directory)
work as before.  A game path with spaces in it may be passed as one
argument (`bin/bgi "$HOME/VN/[August] Fortune Arterial"`), as
may an option's value (`--games="$HOME/VN/my games.json"`).

Use `--text-encoding=gbk` with a patch that stores its display strings
and player names in Windows code page 936. It leaves path and archive name
conversion in CP932. For the Chinese Fureraba HD patch, the Linux smoke
test uses `--engine=1.616 --productname=FriendToLoverHD --text-encoding=gbk`.

A game copied straight from its disc carries the disc marker, a 16-byte
file named like the product (`Tayutama`, `HanairoHeptagram`,
`YoakemaeyoriRuriirona`, ...) that holds `0123456789ABCDEF`, and every
boot program quits at once when it finds that file next to the game -
the disc is meant to be installed, not run.  The engine hides the marker
from the scripts (a file named like the product, or one without an
extension holding those 16 characters), so such a copy starts;
`--disc-marker` gives the original's behaviour back.

Some of the older games (Fortune Arterial among them) check their
registration at boot: `comap.dat`, written for the machine by the
game's registration program `reg.exe`, which wants the disc and runs on
Windows only.  OpenBGI does not start that program, so without a
`comap.dat` made for the machine the boot program asks for the disc;
`--no-disc-check` counts the game as registered.

Movies: the frame-sequence movies of the later builds (`docs/bfmovie.md`)
are decoded by the engine itself.  The opening movies in common formats
(`.mpg`, `.wmv`, ...) go through DirectShow on Windows, as in the
original; on Linux, and on Windows when DirectShow cannot render a file,
the engine plays them through the `ffmpeg` and `ffprobe` programs when it
finds them on the PATH or next to `bgi` (no library is linked: the video
comes in through a pipe, the sound through the engine's own mixer).
Without them the movie is skipped (a note on stderr says so) and the
game goes on, as before.
Android instead links a local-file FFmpeg decoder into the app, with movie
audio routed through the mixer, background pause and tap / Esc / Enter to skip.
See [Android movie playback](docs/android-movie.md) for codecs and limits.

The later builds (1.529 on) open their window at the size their scripts
ask for, which is the game's own resolution scaled down when the
picture would not fit the screen - the picture is scaled into the window
as the original does with the "81 64" instruction.

The engine's own messages (script errors, notices, the installer) are
shown in English; `--lang=ja` or `BGI_LANG=ja` selects the Japanese
originals, which stay in the binary (`tools/messages.txt` holds both, the
text engine's character tables and the player-facing dialog labels are
never translated).  Message boxes raised by the game's own scripts (the
"B0 80 .. 82" instructions and the installer prompts of the `_bp`
programs) carry whatever text the script passes and are marked with a
"(script)" suffix in their caption, so that an untranslated box can be
told from an engine message.  The window title is the game's own
followed by " - OpenBGI".

The same binary looks inside the game's archives: `--list=ARCHIVE`
prints every entry with what it is (a CompressedBG image and its size, a
BW sound and its codec, a program, a compiled scenario, a BF_Movie);
`--extract=ARCHIVE` writes the entries into a directory (`--out=DIR`,
default the archive's name without its extension) as the engine sees
them after the DSC layer, with `--raw` as stored, with `--convert` turned
into common formats (images as .bmp, sounds as .wav - Vorbis only with a
decoder built in -, BF_Movie frames as one .bmp each); `--entry=NAME`
picks one entry.  `bin/bpasm` (`make tools`) disassembles and assembles
the programs (`docs/asm.md`) and decompiles them to the C-like source
they were written in and compiles that back (`docs/bpc.md`; an edited
source may name the engine's memory through declared variables -
`global int gAbort @ 1124;` - and records - `stSprite[i].x` -, keep its
texts as `#define`d constants and share all of it through `#include`);
`bin/bgiscn` does the same for the scenario files of every build (the
1.494+ containers, the headerless files of 1.66 / 1.69, the 16-bit
scenes of 1.64) and repacks the archive (`docs/scenario.md`).

For what the original cannot do - a message longer than its window,
UTF-8 text in every generation, languages written from right to left -
OpenBGI has instructions of its own, the "EF xx" family, which a game's
programs call when they are rebuilt for it (`docs/obgi.md`).

To translate a game, `bin/bgidump GAMEDIR DUMPDIR` dumps every archive at
once - the programs and scenario files decompiled, the pictures as PNG,
the sounds as Ogg Vorbis, the tables as JSON - and `bin/bgipack DUMPDIR
OUTDIR` builds the archives again from the dump, compiling and encoding
what was edited and copying the rest from the originals unchanged
(`docs/dump.md`).

`--debug` opens the debugger with the game (`--debug=pause` holds the
scripts until F5); F12 in the game's window opens and closes it at any
time.  It shows the threads with their stacks and listings, the loaded
programs, the bitmap slots, the display objects, the sound channels, the
memory areas and a log, and pauses, steps and breaks the VM
(`docs/debugger.md`).  Not in the WebAssembly build.

The Windows build needs no Japanese system locale and no locale
emulator: it calls the wide-character API of Windows and converts the
games' Shift-JIS file names, titles, messages and font names itself, and
a game may sit in a directory whose name Shift-JIS cannot spell
(`docs/os_layer.md`, "Strings").

The Linux build keeps its settings (the original's registry values) in
`~/.config/bgi/`.  Fonts: `BGI_FONT_GOTHIC`, `BGI_FONT_MINCHO` or
`BGI_FONT` name a font file to use instead of what fontconfig / the font
directories offer for "ＭＳ ゴシック" / "ＭＳ 明朝".

## Layout

    inc/bgi/        headers, one per module (gfx/ for graphics, snd/ for
                    sound, sysobj/ for the script-visible system objects)
    src/core/       archives, decoders (DSC, CBG, SDC, BF_Movie), files and
                    paths, save files, strings, the loader queue, JSON;
                    arcread.c, the tools' archive reader, and what only
                    the tools use: the DSC, CBG and BF_Movie encoders, PNG
                    and zlib streams
    src/asm/        the assembler / disassembler of the programs (bin/bpasm); the
                    preprocessor of the source languages (#include, #define)
    src/bpc/        the decompiler / compiler of the programs (bin/bpasm -D / -C)
    src/scn/        the scenario files: container, command table scan,
                    decompiler and compiler (bin/bgiscn)
    src/dump/       dumping a game's archives into editable files and
                    packing them again (bin/bgidump, bin/bgipack)
    src/vm/         the script machine: threads, the scheduler, the opcode
                    families (ops_base, ops_sys, ops_sys81, ops_gfx0 .. 2,
                    ops_snd, ops_ext0, ops_ext1), the tracing aids
    src/wait/       wait objects (text output, tweens, menus, loads, ...)
    src/sys/        the engine object, main window, display and presentation,
                    input, cursor, panels, child windows, text entry,
                    dialogs, CPU identification, the game chooser
                    (launcher.c); install/ the installer
    src/sysobj/     the system objects of the "80 8x .. 80 Dx" instructions:
                    names, flags, the global database, message history,
                    rings, locks, dicts, string tables, hit targets, events
    src/gfx/        bitmaps and blitters (blit*.c), the bitmap manager, fonts,
                    display objects (sprite, map, filter, knob, effector),
                    background/ (the twelve background types), mgr/ (the
                    graphics manager), window/ (the message window), text/
                    (the text engine and layout), particle/ and rain, movies
    src/snd/        the sound manager, wm/ (the Wave Master library), bw/
                    (the BW container and its codecs)
    src/dbg/        the debugger (--debug / F12), views/ its panes
    src/os/         the OS layer: os_common.c, posix/, win32/, wasm/
    tools/          messages.txt -> inc/bgi/msg.h (every Shift-JIS string of
                    the binary, with English translations); win32stub/ and
                    wasmstub/ (the syntax-check stand-ins); wasm/ (the asset
                    server, the page, the browser-less simulator)
    tests/          unit tests (make test)
    docs/           Markdown documentation of the subsystems

`CONVENTIONS.md` has the naming, formatting and commenting rules.  Of the
docs, `building.md` covers the builds and their
optional dependencies, `vm.md` the VM, `versions.md` the engine builds
and the profiles, `bfmovie.md` the frame-sequence movies of the later
builds, `sound.md` the Wave Master library and the BW container,
`os_layer.md` the OS interface, `wasm.md` the browser build, `asm.md` the
assembler / disassembler and its listing format, `scenario.md` the
scenario files, their source language and the translation workflow,
`dump.md` the dump and pack tools and the files of a dump,
`debugger.md` the debugger, `panel_system.md`, `particle_rain.md` and
`child_edit_dialogs.md` the subsystems they name.

Development switches (environment): `BGI_MSGBOX_STDERR=1` (message boxes
print and take their default button), `BGI_TRACE`, `BGI_TRACE_FROM`,
`BGI_TRACE_LAST`, `BGI_TRACE_OPS` (instruction traces, see
`src/vm/trace.c`), `BGI_OPSTAT=<file>` (the executed opcodes),
`BGI_INPUT_DEBUG=1`, `BGI_SND_DEBUG=1` (music opens, loop restarts),
`BGI_PANEL_DEBUG=1` (the buttons of every panel at its start and the
events it queues), `BGI_DEBUG_OVERLAY=1` (the debugger over the game's
picture instead of in a window of its own).

## Special Thanks and Honourable Mentions

### Special Thanks

* Darios Sawm / Buriko Co., Ltd.
* [Anonymous]
* minirop
* Galladite
* txt231

### Honourable Mentions

* The BGI tools of the community:
  * [GARbro](https://github.com/morkt/GARbro) (morkt)
  * [arc_unpacker](https://github.com/vn-tools/arc_unpacker) (vn-tools)
  * [EthornellTools](https://github.com/arcusmaximus/EthornellTools)
    (arcusmaximus)
  * [VNTranslationTools](https://github.com/arcusmaximus/VNTranslationTools)
    (arcusmaximus)
  * [Bgi_tools](https://github.com/mchubby/Bgi_tools) (mchubby)
  * [engine_bgi](https://github.com/mnakamura1337/engine_bgi)
    (mnakamura1337)
  * [ethornell-tools](https://github.com/Jair4x/ethornell-tools) (Jair4x)
  * [bgi-extractor](https://github.com/mirinnano/bgi-extractor)
    (mirinnano)
  * [BGIextractor](https://github.com/myzert/BGIextractor) (myzert)
* [BGI](https://github.com/txt231/BGI) by txt231
* [ethornell-rs](https://github.com/xmoezzz/ethornell-rs) (xmoezzz)
* Operation Bellflower - [Adventures in the Ethornell
  Engine](https://www.operationbellflower.com/blog/adventures-in-the-ethornell-engine/part-1)

## License

OpenBGI is licensed under the GNU General Public License, version 2
(GPL-2.0); see `LICENSE`.
