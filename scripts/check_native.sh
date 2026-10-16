#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
OUT="$ROOT/.local/checks"
mkdir -p "$OUT"
compile_run() {
    name=$1; shift
    xcrun clang -std=c11 -Wall -Wextra -Werror -O1 -g -arch arm64 \
        -fsanitize=address,undefined "$@" -o "$OUT/$name"
    "$OUT/$name"
}
compile_run dsp tests/test_dsp_adpcm.c src/audio/dsp_adpcm.c
compile_run mix tests/test_audio_mix.c src/audio/mix.c
compile_run renderer tests/test_renderer.c src/audio/mix.c
compile_run output tests/test_audio_output.c -framework AudioToolbox
compile_run vertex tests/test_vertex_state.c
compile_run commands tests/test_commands.c
compile_run texture-layout tests/test_texture_layout.c src/gfx/nvn_tex.c src/gfx/nvn_deswizzle.c
xcrun clang -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
    tests/test_filesystem.c src/runtime/filesystem.c -o "$OUT/filesystem"
fixture=$(mktemp -d "$OUT/fs.XXXXXX")
printf abcd > "$fixture/data.bin"
printf outside > "$OUT/outside.bin"
ln -s ../outside.bin "$fixture/escape"
"$OUT/filesystem" "$fixture"
for name in threads renderer audio_output; do
    case "$name" in
        renderer) set -- src/audio/mix.c ;;
        audio_output) set -- -framework AudioToolbox ;;
        *) set -- ;;
    esac
    xcrun clang -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=thread \
        "tests/test_$name.c" "$@" -o "$OUT/tsan-$name"
    "$OUT/tsan-$name"
done
xcrun clang -std=c11 -Wall -Wextra -Werror -O1 -g tests/test_kernel.c -o "$OUT/kernel"
"$OUT/kernel"
xcrun clang -std=c11 -Wall -Wextra -Werror -O1 -g -fobjc-arc \
    tests/test_metal_pool.m src/gfx/nvn_tex.c src/gfx/nvn_deswizzle.c \
    -framework Metal -framework Foundation -framework QuartzCore -o "$OUT/metal"
if "$OUT/metal" --reject-layout > "$OUT/metal-reject.log" 2>&1; then
    echo 'FAIL: incompatible texture layouts accepted' >&2; exit 1
else
    check_rc=$?
    test "$check_rc" -eq 42
    rg -q 'unsupported overlapping texture layouts' "$OUT/metal-reject.log"
fi
"$OUT/metal"
if "$OUT/metal" --reject-layout > "$OUT/metal-reject.log" 2>&1; then
    echo 'FAIL: incompatible texture layouts accepted' >&2; exit 1
else
    check_rc=$?
    test "$check_rc" -eq 42
    rg -q 'unsupported overlapping texture layouts' "$OUT/metal-reject.log"
fi
for rejected in buffer-range live-buffer; do
    if "$OUT/metal" "--reject-$rejected" > "$OUT/metal-$rejected.log" 2>&1; then
        echo "FAIL: invalid $rejected accepted" >&2; exit 1
    else
        check_rc=$?; test "$check_rc" -eq 42
        case "$rejected" in
            buffer-range) rg -q 'buffer flush out of range' "$OUT/metal-$rejected.log" ;;
            live-buffer) rg -q 'memory pool finalized with live resources' "$OUT/metal-$rejected.log" ;;
        esac
    fi
done
xcrun clang -std=c11 -Wall -Wextra -Werror -O1 -g -fobjc-arc tests/test_metal_sampler.m \
    -framework Metal -framework Foundation -o "$OUT/sampler"
"$OUT/sampler"
sh tests/test_runtime.sh
echo 'PASS: native synthetic regressions, sanitizer checks and real Metal checks'
