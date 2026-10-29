#!/usr/bin/env bash
# Rebuilds build/tools/ffmpeg.exe: a ~4 MB audio-only LGPL FFmpeg for Windows x64 (cross-compiled with mingw-w64 on Linux).
# Needs: x86_64-w64-mingw32-gcc, make, curl, tar, pkg-config.
set -euo pipefail
W="${W:-/workspace/ffbuild}"; mkdir -p "$W/prefix"; cd "$W"
[ -f ffmpeg.tar.xz ] || curl -sSL -o ffmpeg.tar.xz https://ffmpeg.org/releases/ffmpeg-7.1.1.tar.xz
[ -f lame.tar.gz ] || curl -sSL -o lame.tar.gz https://downloads.sourceforge.net/project/lame/lame/3.100/lame-3.100.tar.gz
tar xzf lame.tar.gz; tar xJf ffmpeg.tar.xz
(cd lame-3.100 && ./configure --host=x86_64-w64-mingw32 --prefix="$W/prefix" --disable-shared --enable-static --disable-frontend --disable-decoder && make -j8 && make install)
cd ffmpeg-7.1.1
export PKG_CONFIG_PATH="$W/prefix/lib/pkgconfig"
./configure --arch=x86_64 --target-os=mingw32 --cross-prefix=x86_64-w64-mingw32- --pkg-config=pkg-config --pkg-config-flags=--static \
 --extra-cflags="-I$W/prefix/include" --extra-ldflags="-L$W/prefix/lib -static -static-libgcc" \
 --disable-everything --disable-autodetect --disable-doc --disable-debug --disable-ffplay --disable-ffprobe \
 --enable-ffmpeg --enable-network --enable-schannel --enable-libmp3lame \
 --enable-protocol=file,pipe,http,https,tcp,tls,crypto,hls,cache,data \
 --enable-demuxer=mov,matroska,wav,mp3,aac,ogg,flac,mpegts,hls,concat \
 --enable-decoder=aac,aac_fixed,opus,vorbis,mp3,mp3float,pcm_s16le,pcm_f32le,flac,pcm_s24le \
 --enable-parser=aac,aac_latm,opus,vorbis,mpegaudio,flac \
 --enable-muxer=wav,mp3,ipod,mp4,mov,matroska,webm,adts,ogg,null,pcm_s16le \
 --enable-encoder=pcm_s16le,aac,libmp3lame \
 --enable-bsf=aac_adtstoasc,extract_extradata,null,opus_metadata \
 --enable-filter=aresample,afade,atrim,aformat,anull,volume,anullsink,asetpts,apad
make -j8
x86_64-w64-mingw32-strip -o "$(dirname "$0")/build/tools/ffmpeg.exe" ffmpeg.exe
