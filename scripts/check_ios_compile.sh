#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT="$ROOT/.local/ios-compile"
mkdir -p "$OUT"
SDK=$(xcrun --sdk iphoneos --show-sdk-path)
python3 "$ROOT/scripts/generate_nvn_gates.py" --program "$ROOT/.local/run1/main.bin" --out "$ROOT/.local/gfx-generated"
for source in loader kernel services input account threads filesystem signed_image; do
    xcrun --sdk iphoneos clang -arch arm64 -isysroot "$SDK" -miphoneos-version-min=26.0 \
        -std=c11 -Wall -Wextra -Werror -O1 -c "$ROOT/src/runtime/$source.c" -o "$OUT/$source.o"
done
xcrun --sdk iphoneos clang -arch arm64 -isysroot "$SDK" -miphoneos-version-min=26.0 \
    -std=c11 -Wall -Wextra -Werror -O1 -fobjc-arc -c "$ROOT/src/gfx/platform.m" -o "$OUT/platform.o"
xcrun --sdk iphoneos clang -arch arm64 -isysroot "$SDK" -miphoneos-version-min=26.0 \
    -c "$ROOT/.local/gfx-generated/nvn_names.c" -o "$OUT/nvn_names.o"
xcrun --sdk iphoneos clang -arch arm64 -isysroot "$SDK" -miphoneos-version-min=26.0 \
    -c "$ROOT/.local/gfx-generated/nvn_gates.S" -o "$OUT/nvn_gates.o"
for source in dsp_adpcm renderer output mix pcm; do
    xcrun --sdk iphoneos clang -arch arm64 -isysroot "$SDK" -miphoneos-version-min=26.0 \
        -std=c11 -Wall -Wextra -Werror -O1 -c "$ROOT/src/audio/$source.c" -o "$OUT/$source.o"
done
for source in bc7 nvn_tex nvn_deswizzle; do
    xcrun --sdk iphoneos clang -arch arm64 -isysroot "$SDK" -miphoneos-version-min=26.0 \
        -std=c11 -Wall -Wextra -Werror -O1 -c "$ROOT/src/gfx/$source.c" -o "$OUT/$source.o"
done
xcrun --sdk iphoneos clang -arch arm64 -isysroot "$SDK" -miphoneos-version-min=26.0 \
    -c "$ROOT/src/runtime/gate.S" -o "$OUT/gate.o"
xcrun --sdk iphoneos clang -arch arm64 -isysroot "$SDK" -miphoneos-version-min=26.0 \
    -std=c11 -Wall -Wextra -Werror -O1 -fobjc-arc -c "$ROOT/src/media/decoder.m" -o "$OUT/decoder.o"
xcrun --sdk iphoneos clang -arch arm64 -isysroot "$SDK" -miphoneos-version-min=26.0 \
    -std=c11 -Wall -Wextra -Werror -O1 -fobjc-arc -c "$ROOT/src/ios/main.m" -o "$OUT/ios-main.o"
xcrun --sdk iphoneos libtool -static -o "$OUT/libhayarigami-runtime.a" \
    "$OUT/loader.o" "$OUT/kernel.o" "$OUT/services.o" "$OUT/input.o" \
    "$OUT/account.o" "$OUT/threads.o" "$OUT/filesystem.o" "$OUT/signed_image.o" "$OUT/gate.o" \
    "$OUT/decoder.o" "$OUT/pcm.o" "$OUT/dsp_adpcm.o" "$OUT/renderer.o" "$OUT/output.o" "$OUT/mix.o" \
    "$OUT/platform.o" "$OUT/bc7.o" "$OUT/nvn_tex.o" "$OUT/nvn_deswizzle.o" "$OUT/nvn_names.o" "$OUT/nvn_gates.o"
echo 'PASS: runtime sources compile for iOS arm64 (compile check only; not an App or IPA)'
