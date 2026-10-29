#!/usr/bin/env bash
# Fetch / build everything Rewired loads at run time into $OUT (default build/models):
#   spleeter4.onnx                Deezer Spleeter 4stems (MIT code + weights), converted from the official TF checkpoint by tools/spleeter_to_onnx.py
#   nmp.onnx                      Spotify basic-pitch 0.4.0 note model (Apache-2.0), straight from the PyPI wheel
#   ort/win-x64/onnxruntime.dll   ONNX Runtime 1.23.2 (MIT), CPU only
#   ort/osx/libonnxruntime.1.23.2.dylib   (universal2)
#   ort/linux/libonnxruntime.so.1.23.2    (tests only)
# The conversion needs python3 with tensorflow-cpu + onnx (pip install tensorflow-cpu==2.17.* onnx numpy). Every download is sha256-checked.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=${OUT:-$PWD/build/models}; T=$OUT/.dl; mkdir -p "$T" "$OUT/ort/win-x64" "$OUT/ort/osx" "$OUT/ort/linux"
get() { # url sha file
  [ -f "$T/$3" ] && echo "$2  $T/$3" | sha256sum -c --quiet - 2>/dev/null && return 0
  curl -fsSL --retry 3 -o "$T/$3" "$1"; echo "$2  $T/$3" | sha256sum -c --quiet -; }
get https://github.com/deezer/spleeter/releases/download/v1.4.0/4stems.tar.gz 3adb4a50ad4eb18c7c4d65fcf4cf2367a07d48408a5eb7d03cd20067429dfaa8 4stems.tar.gz
get https://files.pythonhosted.org/packages/py2.py3/b/basic-pitch/basic_pitch-0.4.0-py2.py3-none-any.whl 738adb503aae7fdfc7d1e1511aa0ce35052315f260a19531ef4c356708425db0 basic_pitch-0.4.0-py2.py3-none-any.whl
get https://github.com/microsoft/onnxruntime/releases/download/v1.23.2/onnxruntime-win-x64-1.23.2.zip 0b38df9af21834e41e73d602d90db5cb06dbd1ca618948b8f1d66d607ac9f3cd ort-win.zip
get https://github.com/microsoft/onnxruntime/releases/download/v1.23.2/onnxruntime-osx-universal2-1.23.2.tgz 49ae8e3a66ccb18d98ad3fe7f5906b6d7887df8a5edd40f49eb2b14e20885809 ort-osx.tgz
if [ ! -f "$OUT/spleeter4.onnx" ]; then
  mkdir -p "$T/4stems" && tar -xzf "$T/4stems.tar.gz" -C "$T/4stems"
  python3 tools/spleeter_to_onnx.py "$T/4stems/model" "$OUT/spleeter4.onnx"
fi
unzip -p "$T/basic_pitch-0.4.0-py2.py3-none-any.whl" basic_pitch/saved_models/icassp_2022/nmp.onnx > "$OUT/nmp.onnx"
unzip -p "$T/ort-win.zip" onnxruntime-win-x64-1.23.2/lib/onnxruntime.dll > "$OUT/ort/win-x64/onnxruntime.dll"
tar -xzf "$T/ort-osx.tgz" -C "$T" && cp "$T/onnxruntime-osx-universal2-1.23.2/lib/libonnxruntime.1.23.2.dylib" "$OUT/ort/osx/"
echo "2c3c1d144bfa61ad236e92e169c13535c880469a12a047d4e73451f2c059a0ec  $OUT/nmp.onnx" | sha256sum -c --quiet -
ls -la "$OUT" "$OUT/ort"/*
