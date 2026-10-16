#!/bin/sh
# Compile the real resource parser, not an App or a game runtime.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT="$ROOT/.local/native"
mkdir -p "$OUT"
xcrun clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I "$ROOT/src" "$ROOT/src/psfs.c" "$ROOT/tests/test_psfs.c" -o "$OUT/test_psfs"
"$OUT/test_psfs"
xcrun clang -std=c11 -Wall -Wextra -Werror -dynamiclib \
  "$ROOT/src/psfs.c" -o "$OUT/libpsfs.dylib"
SDK=$(xcrun --sdk iphoneos --show-sdk-path)
xcrun --sdk iphoneos clang -target arm64-apple-ios15.0 -isysroot "$SDK" \
  -std=c11 -Wall -Wextra -Werror -O2 -c "$ROOT/src/psfs.c" -o "$OUT/psfs-ios.o"
xcrun libtool -static -o "$OUT/libhayarigami_resources_ios.a" "$OUT/psfs-ios.o"
file "$OUT/libhayarigami_resources_ios.a" "$OUT/psfs-ios.o"
