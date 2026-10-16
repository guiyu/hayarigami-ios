#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$ROOT/.local/runtime"
python3 "$ROOT/scripts/generate_nvn_gates.py" --program "$ROOT/.local/run1/main.bin" --out "$ROOT/.local/gfx-generated"
xcrun clang -D_DARWIN_C_SOURCE -std=c11 -Wall -Wextra -Werror -O1 -g -arch arm64 -fobjc-arc \
    "$ROOT/src/runtime/loader.c" "$ROOT/src/runtime/kernel.c" \
    "$ROOT/src/runtime/services.c" "$ROOT/src/runtime/input.c" \
    "$ROOT/src/runtime/threads.c" "$ROOT/src/runtime/filesystem.c" "$ROOT/src/runtime/gate.S" \
    "$ROOT/src/audio/renderer.c" "$ROOT/src/audio/output.c" "$ROOT/src/audio/mix.c" -framework AudioToolbox \
    "$ROOT/src/gfx/platform.m" "$ROOT/src/gfx/nvn_tex.c" "$ROOT/src/gfx/nvn_deswizzle.c" \
    "$ROOT/.local/gfx-generated/nvn_names.c" "$ROOT/.local/gfx-generated/nvn_gates.S" -framework Metal -framework Foundation -framework QuartzCore \
    "$ROOT/tests/probe_audio.c" -o "$ROOT/.local/runtime/probe-audio"
