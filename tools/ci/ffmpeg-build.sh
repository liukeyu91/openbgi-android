#!/bin/sh
# This file is part of OpenBGI (https://openbgi.net).
# SPDX-License-Identifier: GPL-2.0-only
# Static local-file movie decoding; no GPL/nonfree/external codec components.
set -eu
source_dir=$(cd "${1:?FFmpeg source}" && pwd)
prefix=${2:?install prefix}
target=${3:-android}
mkdir -p "$prefix"
prefix=$(cd "$prefix" && pwd)
common="--disable-everything --disable-autodetect --disable-network --disable-gpl --disable-nonfree --disable-debug --disable-doc --disable-shared --enable-static --enable-pic --disable-programs --disable-avdevice --disable-avfilter --disable-x86asm --disable-iconv --disable-zlib --enable-avformat --enable-avcodec --enable-avutil --enable-swscale --enable-swresample"
decoders="mpeg1video,mpeg2video,mpeg4,h263,h263i,h264,hevc,mjpeg,msmpeg4v1,msmpeg4v2,msmpeg4v3,wmv1,wmv2,wmv3,vc1,vp8,vp9,theora,aac,aac_fixed,ac3,alac,flac,mp1,mp1float,mp2,mp2float,mp3,mp3float,vorbis,opus,wmav1,wmav2,wmapro,wmalossless,pcm_s16le,pcm_s16be,pcm_u8,pcm_s24le,pcm_f32le"
demuxers="mpegps,mpegts,mpegvideo,h264,hevc,m4v,asf,asf_o,avi,mov,matroska,ogg,flv,wav,mp3,aac"
parsers="mpegvideo,mpegaudio,mpeg4video,h264,hevc,h263,vc1,aac,aac_latm,ac3,opus,vorbis,vp8,vp9"
if [ "$target" = android ]; then
    : "${ANDROID_NDK_HOME:?NDK root}"
    llvm="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/${ANDROID_HOST_TAG:-linux-x86_64}/bin"
    set -- --enable-cross-compile --arch=aarch64 --target-os=android \
        "--cc=$llvm/aarch64-linux-android28-clang" "--ar=$llvm/llvm-ar" \
        "--nm=$llvm/llvm-nm" "--ranlib=$llvm/llvm-ranlib" "--strip=$llvm/llvm-strip"
else
    # Host encoders and lavfi are only used to make synthetic test clips.
    decoders="$decoders,wrapped_avframe"
    set -- --cc="${CC:-cc}" --enable-ffmpeg --enable-ffprobe --enable-avdevice --enable-avfilter \
        --enable-indev=lavfi --enable-filter=testsrc2,sine,aresample,format,aformat,anull,null,scale,asetpts,setpts,trim,atrim,select \
        --enable-encoder=mpeg1video,mpeg2video,mpeg4,wmv2,mp2,wmav2,aac,pcm_s16le,rawvideo \
        --enable-muxer=mpeg1system,asf,avi,mp4,matroska,wav,rawvideo
fi
# Component lists are internal fixed tokens; executable paths stay separate args.
# shellcheck disable=SC2086
"$source_dir/configure" $common "$@" --prefix="$prefix" \
    --host-cc="${HOST_CC:-cc}" \
    --enable-decoder="$decoders" --enable-demuxer="$demuxers" --enable-parser="$parsers" \
    --enable-protocol=file --extra-cflags="-fstack-protector-strong -D_FORTIFY_SOURCE=2 ${FFMPEG_EXTRA_CFLAGS:-}" \
    --extra-ldflags="${FFMPEG_EXTRA_LDFLAGS:-}"
make -j"${JOBS:-4}"
make install
cp "$source_dir/COPYING.LGPLv2.1" "$prefix/FFmpeg-LGPLv2.1.txt"
