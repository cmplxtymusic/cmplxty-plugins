#!/usr/bin/env bash
# Builds build/tools-mac/ffmpeg: a small audio-only LGPL FFmpeg 7.1.1 + LAME 3.100 for macOS, universal (arm64 + x86_64).
# Same feature set as build-ffmpeg-min.sh (the Windows one); the only change is Apple's Secure Transport for https instead of Windows' schannel.
# Needs a Mac with Xcode command line tools (clang, lipo, codesign), make, curl, tar. Run on macOS only (the CI job does this).
# Not run on a Mac yet: the first CI run is the first real test of this script.
set -euo pipefail
[ "$(uname -s)" = "Darwin" ] || { echo "build-ffmpeg-min-mac.sh must run on macOS" >&2; exit 1; }
HERE="$(cd "$(dirname "$0")" && pwd)"
W="${W:-$HERE/.ffbuild-mac}"; mkdir -p "$W"; cd "$W"
MINOS=13.0
NCPU="$(sysctl -n hw.ncpu)"
FFV=7.1.1; LAMEV=3.100
# pinned download hashes (sha256, taken from the first download of each file), so a changed download is refused
FF_SHA=733984395e0dbbe5c046abda2dc49a5544e7e0e1e2366bba849222ae9e3a03b1
LAME_SHA=ddfe36cab873794038ae2c1210557ad34857a4b6bdc515785d1da9e175b1da1e
[ -f ffmpeg.tar.xz ] || curl -fsSL -o ffmpeg.tar.xz "https://ffmpeg.org/releases/ffmpeg-$FFV.tar.xz"
[ -f lame.tar.gz ] || curl -fsSL -o lame.tar.gz "https://downloads.sourceforge.net/project/lame/lame/$LAMEV/lame-$LAMEV.tar.gz"
echo "$FF_SHA  ffmpeg.tar.xz" | shasum -a 256 -c -
echo "$LAME_SHA  lame.tar.gz" | shasum -a 256 -c -

build_arch() {
  local arch="$1" triple="$2" P="$W/prefix-$1"
  rm -rf "$P" "lame-$LAMEV-$arch" "ffmpeg-$FFV-$arch"; mkdir -p "$P"
  mkdir "lame-$LAMEV-$arch" && tar xzf lame.tar.gz -C "lame-$LAMEV-$arch" --strip-components=1
  mkdir "ffmpeg-$FFV-$arch" && tar xJf ffmpeg.tar.xz -C "ffmpeg-$FFV-$arch" --strip-components=1
  local CC="clang -arch $arch -mmacosx-version-min=$MINOS"
  (cd "lame-$LAMEV-$arch" && CC="$CC" ./configure --host="$triple" --prefix="$P" --disable-shared --enable-static --disable-frontend --disable-decoder >/dev/null && make -j"$NCPU" >/dev/null && make install >/dev/null)
  (cd "ffmpeg-$FFV-$arch" && PKG_CONFIG_PATH="$P/lib/pkgconfig" ./configure --arch="$arch" --target-os=darwin --enable-cross-compile --cc="$CC" --pkg-config-flags=--static \
    --extra-cflags="-I$P/include" --extra-ldflags="-L$P/lib" \
    --disable-everything --disable-autodetect --disable-doc --disable-debug --disable-ffplay --disable-ffprobe \
    --enable-ffmpeg --enable-network --enable-securetransport --enable-libmp3lame \
    --enable-protocol=file,pipe,http,https,tcp,tls,crypto,hls,cache,data \
    --enable-demuxer=mov,matroska,wav,mp3,aac,ogg,flac,mpegts,hls,concat \
    --enable-decoder=aac,aac_fixed,opus,vorbis,mp3,mp3float,pcm_s16le,pcm_f32le,flac,pcm_s24le \
    --enable-parser=aac,aac_latm,opus,vorbis,mpegaudio,flac \
    --enable-muxer=wav,mp3,ipod,mp4,mov,matroska,webm,adts,ogg,null,pcm_s16le \
    --enable-encoder=pcm_s16le,aac,libmp3lame \
    --enable-bsf=aac_adtstoasc,extract_extradata,null,opus_metadata \
    --enable-filter=aresample,afade,atrim,aformat,anull,volume,anullsink,asetpts,apad \
    && make -j"$NCPU" ffmpeg >/dev/null)
  cp "ffmpeg-$FFV-$arch/ffmpeg" "ffmpeg-$arch"
  strip -x "ffmpeg-$arch"
}
build_arch arm64 aarch64-apple-darwin
build_arch x86_64 x86_64-apple-darwin
mkdir -p "$HERE/build/tools-mac"
{ echo "FFmpeg $FFV (audio-only build, configured without --enable-gpl or --enable-nonfree) is licensed under the GNU LGPL version 2.1 or later."; echo "It is statically linked with LAME $LAMEV (LGPL 2.0 or later). Source code: https://ffmpeg.org/releases/ffmpeg-$FFV.tar.xz and https://lame.sourceforge.io/"; echo "Build script: build-ffmpeg-min-mac.sh in the Yoink source."; echo; cat "ffmpeg-$FFV-arm64/COPYING.LGPLv2.1"; echo; echo "----- LAME -----"; cat "lame-$LAMEV-arm64/COPYING"; } > "$HERE/build/tools-mac/ffmpeg-LICENSE.txt"
lipo -create ffmpeg-arm64 ffmpeg-x86_64 -output "$HERE/build/tools-mac/ffmpeg"
chmod 755 "$HERE/build/tools-mac/ffmpeg"
# lipo drops signatures and Apple Silicon will not run unsigned code, so sign it again (ad-hoc here; a real signing pass can replace it later)
codesign --force -s - "$HERE/build/tools-mac/ffmpeg"
lipo -info "$HERE/build/tools-mac/ffmpeg"
# quick self-test (this build has no test-signal generators, so only check what it was built with)
F="$HERE/build/tools-mac/ffmpeg"
"$F" -hide_banner -version | head -1
"$F" -hide_banner -encoders 2>/dev/null | grep -q libmp3lame || { echo "ERROR: mp3 encoder missing" >&2; exit 1; }
"$F" -hide_banner -protocols 2>/dev/null | grep -qE '^ +https$' || { echo "ERROR: https protocol missing (Secure Transport not enabled)" >&2; exit 1; }
echo "ffmpeg ok: mp3 encoder and https present"
