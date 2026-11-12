#!/usr/bin/env python3
"""Optional helpers for inspecting WAVs written by RiggedTest. The C++ harness is the source of truth."""
import json, sys
from pathlib import Path

def main():
    if len(sys.argv) < 2:
        print("usage: audio_analysis.py <results.json>")
        return 2
    p = Path(sys.argv[1])
    print(json.dumps(json.loads(p.read_text()), indent=2))
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
