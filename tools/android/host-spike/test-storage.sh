#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$src/../../.." && pwd)
upstream=$(CDPATH= cd -- "${1:?Pass the upstream v1.0 checkout}" && pwd)
shift
core="${UPSTREAM_HOST_LIB:-$upstream/bin/linux/libopenbgi-x86_64.so}"
out=$(mktemp -d)
trap 'rm -rf -- "$out"' EXIT
storage="$root/src/os"
host="$root/src/host/android"
stubs="$root/tools/android/tests/stubs"
font_cflags=${FREETYPE_CFLAGS:-$(pkg-config --cflags freetype2)}
font_libs=${FREETYPE_LIBS:-$(pkg-config --libs freetype2)}
${CC:-cc} -D_GNU_SOURCE -DBGI_ANDROID -std=c99 -O1 -I"$root/inc" -I"$stubs" \
    -c "$storage/posix/audio.c" -o "$out/audio.o"
${CC:-cc} -D_GNU_SOURCE -DBGI_ANDROID -std=c99 -O1 -I"$root/inc" $font_cflags \
    -c "$storage/posix/font.c" -o "$out/font.o"
${CC:-cc} -D_GNU_SOURCE -std=c99 -O1 -Dclock_gettime=TestClockGetTime \
    -c "$storage/android/clock.c" -o "$out/clock.o"
${CC:-cc} -D_GNU_SOURCE -std=c99 -O1 -fvisibility=hidden -ffunction-sections -Wl,--gc-sections \
    -I"$upstream/include" -I"$root/inc" -I"$stubs" -I"$host" -o "$out/test" \
    "$src/storage-test.c" "$host"/*.c "$out/audio.o" "$out/clock.o" "$out/font.o" \
    "$storage/android/audio_output.c" "$stubs/aaudio/mock.c" "$storage/android/storage_paths.c" \
    "$storage/android/storage_settings.c" "$storage/android/sjis.c" \
    "$storage/posix/files.c" "$storage/posix/paths.c" "$storage/posix/process.c" "$storage/posix/native.c" \
    "$storage/posix/disc.c" "$storage/posix/iso.c" "$storage/posix/udf.c" "$storage/posix/zip.c" \
    "$storage/os_common.c" "$root/src/core/deflate.c" "$core" $font_libs -pthread -lm
export LD_LIBRARY_PATH="$(dirname -- "$core")${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
python3 "$src/make-demo.py" "$out/demo.zip"
python3 "$src/make-demo.py" "$out/confirm.zip" --confirm
python3 "$src/make-demo.py" "$out/pause-demo.zip" --pause-test
python3 "$src/make-demo.py" "$out/archive-demo.zip" --archive-test
if [ -n "${BGI_TEST_CJK_FONT:-}" ]; then
    for encoding in cp932 utf8 gbk big5; do
        python3 "$src/make-demo.py" "$out/text-$encoding-fixture.zip" --text="$encoding" --sample='Wi 文中'
    done
    for encoding in utf8 gbk; do
        python3 "$src/make-demo.py" "$out/text-$encoding-8140-fixture.zip" --text="$encoding" --sample='Wi 丂中'
    done
fi
python3 - "$out" "$@" <<'PY'
from pathlib import Path
import struct
import sys
from zipfile import ZIP_DEFLATED, ZipFile

root = Path(sys.argv[1])
with ZipFile(root / "demo.zip") as archive:
    files = {name: archive.read(name) for name in archive.namelist()}
files.update({"events.arc": b"default", "events.tc.arc": b"traditional", "override.txt": b"base"})

def arc_with_name(name):
    return struct.pack("<12sI96sII24x", b"BURIKO ARC20", 1, name, 0, 0)

files["names.arc"] = arc_with_name(b"Base archive")
source_dir = root / "source"
source_dir.mkdir()
with ZipFile(root / "source.zip", "w", ZIP_DEFLATED) as archive:
    for name, data in files.items():
        (source_dir / name).write_bytes(data)
        archive.writestr("Game/" + name, data)

def make_variant(name, demo_file):
    source = root / f"{name}.zip"
    with ZipFile(demo_file) as demo, ZipFile(source, "w", ZIP_DEFLATED) as archive:
        for entry, data in files.items():
            archive.writestr("Game/" + entry, demo.read(entry) if entry == "ipl._bp" else data)
    return (name, source, "source.txt")

_, choice, _ = make_variant("choice", root / "confirm.zip")
sources = [("direct", source_dir, "source-path.txt"),
           ("container", root / "source.zip", "source.txt"),
           ("confirm-yes", choice, "source.txt"),
           ("confirm-no", choice, "source.txt"),
           make_variant("pause", root / "pause-demo.zip"),
           make_variant("archive", root / "archive-demo.zip")]
text_modes = {"text-cp932": "sjis", "text-utf8": "utf8", "text-gbk": "gbk", "text-big5": "big5",
              "text-utf8-8140": "utf8", "text-gbk-8140": "gbk"}
for name in text_modes:
    demo = root / f"{name}-fixture.zip"
    if demo.exists():
        sources.append(make_variant(name, demo))
images = [(f"image{i}", Path(path).resolve(strict=True), "source.txt")
          for i, path in enumerate(sys.argv[2:])]
for kind, source, marker in sources + images:
    internal = root / kind
    config = internal / "games" / "demo" / ".openbgi"
    data_config = config / "data" / ".openbgi"
    data_config.mkdir(parents=True)
    (data_config / "original-saves.txt").touch()
    patch_id = "00000000-0000-0000-0000-000000000000"
    patch = config / "patches" / patch_id
    patch.mkdir(parents=True)
    (patch / "override.txt").write_bytes(b"patch")
    if kind == "archive":
        (patch / "names.arc").write_bytes(arc_with_name(b"OpenBGI host spike ready"))
    if kind.startswith("image"):
        (patch / "events.arc").write_bytes(files["events.arc"])
        (patch / "events.tc.arc").write_bytes(files["events.tc.arc"])
    (config / "patches" / "active").write_text(patch_id)
    (config / marker).write_text(str(source))
    profile = "auto" if kind == "direct" else "1.659"
    encoding = text_modes.get(kind, "sjis")
    scale = 2 if kind == "container" else 0
    (config / "settings.ini").write_text(
        f"profile={profile}\nencoding={encoding}\narchiveLanguage=tc\nscale={scale}\n"
    )
    (internal / "active-game.txt").write_text("demo")
PY
before=$(sha256sum "$out/source.zip" "$out/choice.zip" "$out/archive.zip" "$out/source/"* "$@")
for entry in "$out/direct" "$out/container" "$out"/image*; do
    [ -d "$entry" ] || continue
    "$out/test" "$entry" > "$out/run.log" 2>&1 || { cat "$out/run.log"; exit 1; }
    cat "$out/run.log"
    case "$entry" in
        "$out/direct") grep -F 'HOST_OPTIONS: . "Do not use mutex.", archive tc, scale 0' "$out/run.log" >/dev/null ;;
        "$out/container") grep -F 'HOST_OPTIONS: --engine=1.659 . "Do not use mutex.", archive tc, scale 2' "$out/run.log" >/dev/null ;;
    esac
    data="$entry/games/demo/.openbgi/data"
    [ "$(cat "$data/UserData/slot.sud")" = saved ]
    [ "$(cat "$data/override.txt")" = Patch ]
    test ! -e "$data/.openbgi-write-test"
done
"$out/test" "$out/confirm-yes" 6
"$out/test" "$out/confirm-no" 7
"$out/test" "$out/pause" 0 --pause
"$out/test" "$out/archive"
echo 'HOST_ARCHIVE_NAMES: 81 39 read the active patch entry name over the base ZIP'
if [ -n "${BGI_TEST_CJK_FONT:-}" ]; then
    for encoding in cp932 utf8 gbk big5 utf8-8140 gbk-8140; do
        BGI_FONT="$BGI_TEST_CJK_FONT" "$out/test" "$out/text-$encoding" 0 "$out/$encoding.rgba"
    done
    for encoding in utf8 gbk big5; do
        cmp "$out/cp932.rgba" "$out/$encoding.rgba"
    done
    cmp "$out/utf8-8140.rgba" "$out/gbk-8140.rgba"
    echo 'HOST_TEXT: CP932, UTF-8, GBK and CP950 frames match; GBK 0x8140 draws U+4E02'
fi
[ "$before" = "$(sha256sum "$out/source.zip" "$out/choice.zip" "$out/archive.zip" "$out/source/"* "$@")" ]
