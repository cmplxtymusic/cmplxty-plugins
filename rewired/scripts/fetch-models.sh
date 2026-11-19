#!/usr/bin/env bash
# Fetch / build everything Rewired loads at run time into $OUT (default build/models):
#   spleeter4.onnx                Deezer Spleeter 4stems (MIT code + weights), converted from the official TF checkpoint by tools/spleeter_to_onnx.py
#   nmp.onnx                      Spotify basic-pitch 0.4.0 note model (Apache-2.0), straight from the PyPI wheel
#   ort/win-x64/onnxruntime.dll   ONNX Runtime 1.23.2 (MIT), CPU only, from the Microsoft.ML.OnnxRuntime 1.23.2 NuGet package
#   ort/osx/libonnxruntime.1.23.2.dylib   universal2, made on macOS with lipo from the NuGet osx-arm64 and osx-x64 dylibs
#   ort/linux/libonnxruntime.so.1.23.2    (tests only)
# The Spleeter conversion needs python3 with tensorflow-cpu==2.17.1 and onnx==1.16.2 (a newer onnx
# imports ml_dtypes.float4_e2m1fn, which that TensorFlow does not ship). CI converts once on Linux and
# drops spleeter4.onnx next to an optional spleeter4.onnx.sha256; if that file is already present and
# matches, this script does not import TensorFlow. Checksums use sha256sum or shasum so macOS works.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=${OUT:-$PWD/build/models}; T=$OUT/.dl; mkdir -p "$T" "$OUT/ort/win-x64" "$OUT/ort/osx" "$OUT/ort/linux"
sha_file() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | awk '{print $1}'; else shasum -a 256 "$1" | awk '{print $1}'; fi
}
get() { # url sha file
  if [ -f "$T/$3" ] && [ "$(sha_file "$T/$3")" = "$2" ]; then return 0; fi
  curl -fsSL --retry 3 -o "$T/$3" "$1"
  local got; got=$(sha_file "$T/$3")
  [ "$got" = "$2" ] || { echo "sha256 mismatch for $3 (got $got)" >&2; exit 1; }
}
get https://github.com/deezer/spleeter/releases/download/v1.4.0/4stems.tar.gz 3adb4a50ad4eb18c7c4d65fcf4cf2367a07d48408a5eb7d03cd20067429dfaa8 4stems.tar.gz
get https://files.pythonhosted.org/packages/py2.py3/b/basic-pitch/basic_pitch-0.4.0-py2.py3-none-any.whl 738adb503aae7fdfc7d1e1511aa0ce35052315f260a19531ef4c356708425db0 basic_pitch-0.4.0-py2.py3-none-any.whl
# microsoft/onnxruntime GitHub release assets are 404 (the repo was made private). The same 1.23.2 CPU binaries are in this NuGet package.
get "https://www.nuget.org/api/v2/package/Microsoft.ML.OnnxRuntime/1.23.2" 25fe172f7fcdf34f2b5b02c8b997f6b88abc282956849d4af4335af23d0c4e4a Microsoft.ML.OnnxRuntime.1.23.2.nupkg
if [ -f "$OUT/spleeter4.onnx" ] && [ -f "$OUT/spleeter4.onnx.sha256" ]; then
  exp=$(awk 'NF{print $1; exit}' "$OUT/spleeter4.onnx.sha256")
  got=$(sha_file "$OUT/spleeter4.onnx")
  if [ "$got" != "$exp" ]; then
    echo "spleeter4.onnx sha256 mismatch (got $got expected $exp)" >&2
    rm -f "$OUT/spleeter4.onnx"
  fi
fi
if [ ! -f "$OUT/spleeter4.onnx" ]; then
  mkdir -p "$T/4stems" && tar -xzf "$T/4stems.tar.gz" -C "$T/4stems"
  python3 tools/spleeter_to_onnx.py "$T/4stems/model" "$OUT/spleeter4.onnx"
fi
unzip -p "$T/basic_pitch-0.4.0-py2.py3-none-any.whl" basic_pitch/saved_models/icassp_2022/nmp.onnx > "$OUT/nmp.onnx"
unzip -p "$T/Microsoft.ML.OnnxRuntime.1.23.2.nupkg" runtimes/win-x64/native/onnxruntime.dll > "$OUT/ort/win-x64/onnxruntime.dll"
if [ "$(uname -s)" = "Darwin" ]; then
  unzip -p "$T/Microsoft.ML.OnnxRuntime.1.23.2.nupkg" runtimes/osx-arm64/native/libonnxruntime.dylib > "$T/libonnxruntime-arm64.dylib"
  unzip -p "$T/Microsoft.ML.OnnxRuntime.1.23.2.nupkg" runtimes/osx-x64/native/libonnxruntime.dylib > "$T/libonnxruntime-x64.dylib"
  lipo -create "$T/libonnxruntime-arm64.dylib" "$T/libonnxruntime-x64.dylib" -output "$OUT/ort/osx/libonnxruntime.1.23.2.dylib"
fi
[ "$(sha_file "$OUT/nmp.onnx")" = "2c3c1d144bfa61ad236e92e169c13535c880469a12a047d4e73451f2c059a0ec" ] || { echo "nmp.onnx sha256 mismatch" >&2; exit 1; }
ls -la "$OUT" "$OUT/ort"/*
