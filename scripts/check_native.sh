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
compile_run bc7 tests/test_bc7.c src/gfx/bc7.c
compile_run mix tests/test_audio_mix.c src/audio/mix.c
compile_run renderer tests/test_renderer.c src/audio/mix.c
compile_run output tests/test_audio_output.c -framework AudioToolbox
compile_run vertex tests/test_vertex_state.c
compile_run input tests/test_input.c
compile_run account tests/test_account.c
compile_run shader-layout tests/test_shader_layout.c
compile_run loader-symbols tests/test_loader_symbols.c
compile_run commands tests/test_commands.c
compile_run gpu-time tests/test_gpu_time.c
compile_run texture-layout tests/test_texture_layout.c src/gfx/nvn_tex.c src/gfx/nvn_deswizzle.c
xcrun clang -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
    tests/test_filesystem.c src/runtime/filesystem.c -o "$OUT/filesystem"
fixture=$(mktemp -d "$OUT/fs.XXXXXX")
printf abcd > "$fixture/data.bin"
printf outside > "$OUT/outside.bin"
ln -s ../outside.bin "$fixture/escape"
"$OUT/filesystem" "$fixture"
for name in threads condition renderer audio_output; do
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
    tests/test_metal_pool.m src/gfx/bc7.c src/gfx/nvn_tex.c src/gfx/nvn_deswizzle.c \
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
xcrun clang -std=c11 -Wall -Wextra -Werror -O1 -g -fobjc-arc tests/test_gpu_counter.m \
    -framework Metal -framework Foundation -o "$OUT/gpu-counter"
"$OUT/gpu-counter"
xcrun clang -std=c11 -Wall -Wextra -Werror -O1 -g -fobjc-arc -fsanitize=address,undefined \
    tests/test_command_sync.m src/gfx/bc7.c src/gfx/nvn_tex.c src/gfx/nvn_deswizzle.c \
    -framework Metal -framework Foundation -framework QuartzCore -o "$OUT/command-sync"
"$OUT/command-sync"
xcrun clang -std=c11 -Wall -Wextra -Werror -O1 -g -fobjc-arc -fsanitize=address,undefined \
    tests/test_command_submit.m src/gfx/bc7.c src/gfx/nvn_tex.c src/gfx/nvn_deswizzle.c \
    -framework Metal -framework Foundation -framework QuartzCore -o "$OUT/command-submit"
for opcode in 15 24 13 7 26 999 type; do
    if "$OUT/command-submit" "$opcode" > "$OUT/command-submit-$opcode.log" 2>&1; then
        echo 'FAIL: malformed command accepted' >&2;exit 1
    else
        check_rc=$?;test "$check_rc" -eq 42
        case "$opcode" in
            999) rg -q 'unknown recorded command opcode' "$OUT/command-submit-$opcode.log" ;;
            type) rg -q 'recorded command resource was overwritten, altered or released' "$OUT/command-submit-$opcode.log" ;;
            *) rg -q 'invalid command payload length' "$OUT/command-submit-$opcode.log" ;;
        esac
    fi
done
xcrun clang -std=c11 -Wall -Wextra -Werror -O1 -g -fobjc-arc \
    tests/test_program.m src/gfx/bc7.c src/gfx/nvn_tex.c src/gfx/nvn_deswizzle.c \
    -framework Metal -framework Foundation -framework QuartzCore -o "$OUT/program"
"$OUT/program"
for rejected in code-range stage; do
    if "$OUT/program" "--reject-$rejected" > "$OUT/program-$rejected.log" 2>&1; then
        echo "FAIL: invalid shader $rejected accepted" >&2; exit 1
    else
        check_rc=$?; test "$check_rc" -eq 42
        case "$rejected" in
            code-range) rg -q 'GPU address outside live memory pools' "$OUT/program-$rejected.log" ;;
            stage) rg -q 'shader control and SPH stages disagree' "$OUT/program-$rejected.log" ;;
        esac
    fi
done
xcrun clang -std=c11 -Wall -Wextra -Werror -O1 -g -fobjc-arc -fsanitize=address,undefined \
    tests/test_render_targets.m src/gfx/bc7.c src/gfx/nvn_tex.c src/gfx/nvn_deswizzle.c \
    -framework Metal -framework Foundation -framework QuartzCore -o "$OUT/render-targets"
"$OUT/render-targets"
if "$OUT/render-targets" --reject-uniform-range > "$OUT/uniform-reject.log" 2>&1; then
    echo 'FAIL: out-of-range uniform buffer accepted' >&2;exit 1
else
    check_rc=$?;test "$check_rc" -eq 42
    rg -q 'uniform buffer outside live memory pools' "$OUT/uniform-reject.log"
fi
xcrun clang -std=c11 -Wall -Wextra -Werror -O1 -g -fobjc-arc -fsanitize=address,undefined \
    tests/test_texture_copy.m src/gfx/bc7.c src/gfx/nvn_tex.c src/gfx/nvn_deswizzle.c \
    -framework Metal -framework Foundation -framework QuartzCore -o "$OUT/texture-copy"
"$OUT/texture-copy"
if "$OUT/texture-copy" --reject-copy-range > "$OUT/texture-copy-reject.log" 2>&1; then
    echo 'FAIL: out-of-bounds texture copy accepted' >&2;exit 1
else
    check_rc=$?;test "$check_rc" -eq 42
    rg -q 'texture copy region outside image bounds' "$OUT/texture-copy-reject.log"
fi
xcrun clang -std=c11 -Wall -Wextra -Werror -O1 -g -fobjc-arc tests/test_bc7_metal.m src/gfx/bc7.c \
    -framework Metal -framework Foundation -o "$OUT/bc7-metal"
"$OUT/bc7-metal"
if "$OUT/bc7-metal" --negative-control > "$OUT/bc7-negative.log" 2>&1; then
    if ! rg -q 'SKIP: native BC7 reference unavailable' "$OUT/bc7-negative.log"; then
        echo 'FAIL: BC7 negative control accepted' >&2;exit 1
    fi
else
    rg -q 'BC7 reference mismatch: 1 bytes' "$OUT/bc7-negative.log"
fi
sh tests/test_runtime.sh
echo 'PASS: native synthetic regressions, sanitizer checks and real Metal checks'
