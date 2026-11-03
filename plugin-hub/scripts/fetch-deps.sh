#!/usr/bin/env bash
# Fetch the dependency (JUCE) into ext/ at the versions this plugin was built with.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p ext
if [ ! -d ext/JUCE ]; then
  git clone --depth 1 --branch 9.0.4 https://github.com/juce-framework/JUCE.git ext/JUCE
  # Windows fixes: no static comctl32 import (a TaskDialogIndirect import stops the plug-in loading in FL Studio and other comctl32 v5 hosts) + VBlank thread shutdown hang (Wine, remote displays)
  git -C ext/JUCE apply ../../patches/juce-9.0.4-windows-fixes.patch
fi
