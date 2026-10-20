#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
OUT="$ROOT/.local/media-checks"
mkdir -p "$OUT"
ffmpeg -hide_banner -loglevel error -f lavfi -i color=red:s=32x32:r=30 -frames:v 1 -c:v libx264 -preset ultrafast -tune zerolatency -f h264 -y "$OUT/red.h264"
ffmpeg -hide_banner -loglevel error -f lavfi -i sine=frequency=440:sample_rate=48000 -t 0.1 -ac 2 -c:a aac -f adts -y "$OUT/sine.aac"
ffmpeg -hide_banner -loglevel error -f lavfi -i testsrc2=s=32x32:r=30 -frames:v 12 -c:v libx264 -bf 2 -b_strategy 0 -g 30 -y "$OUT/bframes.mp4"
ffmpeg -hide_banner -loglevel error -i "$OUT/bframes.mp4" -c copy -bsf:v h264_mp4toannexb -f h264 -y "$OUT/bframes.h264"
python3 - "$OUT" <<'PY'
import json, pathlib, struct, subprocess, sys
root=pathlib.Path(sys.argv[1])
packets=json.loads(subprocess.check_output(['ffprobe','-v','error','-select_streams','v:0','-show_packets','-show_data','-of','json',str(root/'bframes.mp4')]))['packets']
with (root/'bframes.packets').open('wb') as output:
    for packet in packets:
        data=b''.join(bytes.fromhex(line.split(':',1)[1].split('  ')[0].strip()) for line in packet['data'].splitlines() if ':' in line)
        assert len(data)==int(packet['size'])
        output.write(struct.pack('<qI',round(float(packet['pts_time'])*1000000),len(data)))
        output.write(data)
PY
for name in movie movie_lifetime; do
    xcrun clang -O1 -g -Wall -Wextra -Werror -fobjc-arc "tests/test_$name.m" \
        -framework Foundation -framework VideoToolbox -framework AudioToolbox -framework CoreMedia -framework CoreVideo -o "$OUT/$name"
done
"$OUT/movie" "$OUT/red.h264" "$OUT/sine.aac" "$OUT/bframes.h264" "$OUT/bframes.packets"
"$OUT/movie_lifetime"
