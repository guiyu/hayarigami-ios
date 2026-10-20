#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
test "$#" -eq 2 || { echo 'usage: check_shader_draw.sh SHADER_SOURCE METAL_CACHE' >&2;exit 2; }
source_dir=$1
cache_dir=$2
OUT="$ROOT/.local/checks"
mkdir -p "$OUT"
xcrun clang -std=c11 -Wall -Wextra -Werror -O1 -g -fobjc-arc -fsanitize=address,undefined \
    tests/probe_draw_shader.m src/gfx/bc7.c src/gfx/nvn_tex.c src/gfx/nvn_deswizzle.c \
    -framework Metal -framework Foundation -framework QuartzCore -o "$OUT/probe-draw-shader"
"$OUT/probe-draw-shader" "$source_dir" "$cache_dir"
for mode in bad-code bad-reference; do
    if "$OUT/probe-draw-shader" "$source_dir" "$cache_dir" "--$mode" > "$OUT/shader-$mode.log" 2>&1; then
        echo "FAIL: $mode accepted" >&2;exit 1
    else
        result=$?;test "$result" -eq 42
        case "$mode" in
            bad-code) rg -q 'Metal shader source/profile mismatch' "$OUT/shader-$mode.log" ;;
            bad-reference) rg -q 'unsupported Metal shader specialization' "$OUT/shader-$mode.log" ;;
        esac
    fi
done
fixture=$(mktemp -d "$OUT/shader-integrity.XXXXXX")
python3 - "$cache_dir" "$fixture" <<'PY'
import json,pathlib,sys
root,out=map(pathlib.Path,sys.argv[1:])
key='8b48fccc06e7316ced2cffd9e0d2467a3526bec08b167bdf06cc98007b30878b'
info=json.loads((root/f'{key}.json').read_text())
binary=(root/f'{key}.macosx.metallib').read_bytes()
for mode in ('library','profile','stage','metadata'):
    folder=out/mode;folder.mkdir()
    data=dict(info)
    if mode=='profile':data['Profile']='unverified'
    if mode=='stage':data['Stage']=5
    (folder/f'{key}.json').write_text(json.dumps([] if mode=='metadata' else data))
    changed=bytes([binary[0]^1])+binary[1:] if mode=='library' else binary
    (folder/f'{key}.macosx.metallib').write_bytes(changed)
PY
for mode in library profile stage metadata; do
    if "$OUT/probe-draw-shader" "$source_dir" "$fixture/$mode" > "$OUT/shader-$mode.log" 2>&1; then
        echo "FAIL: invalid shader $mode accepted" >&2;exit 1
    else
        result=$?;test "$result" -eq 42
        case "$mode" in
            library) rg -q 'Metal library hash mismatch' "$OUT/shader-$mode.log" ;;
            metadata) rg -q 'Metal shader metadata unavailable or malformed' "$OUT/shader-$mode.log" ;;
            *) rg -q 'Metal shader source/profile mismatch' "$OUT/shader-$mode.log" ;;
        esac
    fi
done
echo 'PASS: original first-draw GPU pixels and six shader integrity/profile rejection cases'
