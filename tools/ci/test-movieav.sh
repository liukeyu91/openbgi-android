#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
prefix=${1:?host FFmpeg installation}
out=${2:?test output directory}
mkdir -p "$out"; out=$(cd "$out" && pwd)
ffmpeg="$prefix/bin/ffmpeg"
clip() {
 "$ffmpeg" -hide_banner -loglevel error -f lavfi -i testsrc2=size=160x90:rate=25 \
  -f lavfi -i sine=frequency=1000:sample_rate=48000 -t 2 "$@"
}
clip -c:v mpeg2video -bf 2 -c:a mp2 -b:a 128k -y "$out/av.mpg"
clip -c:v wmv2 -c:a wmav2 -ar 44100 -y "$out/av.wmv"
clip -c:v mpeg4 -bf 2 -c:a aac -y "$out/av.mp4"
clip -an -vf 'select=not(mod(n\,2))' -fps_mode vfr -c:v mpeg4 -y "$out/vfr.mp4"
printf '%s' 'not a video' > "$out/bad.bin"
flags="${SANITIZERS:+-fsanitize=$SANITIZERS -fno-omit-frame-pointer -fno-sanitize-recover=all}"
# shellcheck disable=SC2086
"${CC:-cc}" -std=c99 -O1 -g -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L -DBGI_HAVE_FFMPEG \
 -I"$root/inc" -I"$prefix/include" $flags "$root/tools/ci/test-movieav.c" "$root/src/sys/movieav.c" \
 "$prefix/lib/libavformat.a" "$prefix/lib/libavcodec.a" "$prefix/lib/libswscale.a" \
 "$prefix/lib/libswresample.a" "$prefix/lib/libavutil.a" -pthread -lm -o "$out/test-movieav"
"$out/test-movieav" "$out/av.mpg" "$out/av.wmv" "$out/av.mp4" "$out/vfr.mp4" "$out/bad.bin"
