#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
test "$#" -eq 3 || { echo 'usage: check_game_replay.sh MODULES SHADER_SOURCE METAL_CACHE' >&2;exit 2; }
OUT="$ROOT/.local/runtime"
mkdir -p "$OUT"
python3 scripts/generate_nvn_gates.py --program .local/run1/main.bin --out .local/gfx-generated
modules=$1
source_dir=$2
cache_dir=$3
for probe in nvn copy_cpu movie_layout replay_draw; do
    if [ "$probe" = replay_draw ]; then
        extension=m
        set --
    else
        extension=c
        set -- src/gfx/platform.m
    fi
    xcrun clang -D_DARWIN_C_SOURCE -std=c11 -Wall -Wextra -Werror -O1 -g -arch arm64 -fobjc-arc \
        src/runtime/loader.c src/runtime/kernel.c src/runtime/account.c src/runtime/services.c src/runtime/input.c \
        src/runtime/threads.c src/runtime/filesystem.c src/runtime/gate.S \
        src/audio/renderer.c src/audio/output.c src/audio/pcm.c src/audio/mix.c -framework AudioToolbox \
        src/media/decoder.m src/gfx/bc7.c src/gfx/nvn_tex.c src/gfx/nvn_deswizzle.c \
        .local/gfx-generated/nvn_names.c .local/gfx-generated/nvn_gates.S \
        -framework VideoToolbox -framework CoreMedia -framework CoreVideo -framework Metal -framework Foundation -framework QuartzCore \
        "tests/probe_$probe.$extension" "$@" -o "$OUT/probe-$probe"
    if [ "$probe" = replay_draw ]; then
        "$OUT/probe-$probe" "$modules" "$source_dir" "$cache_dir"
    else
        "$OUT/probe-$probe" "$modules"
    fi
done
