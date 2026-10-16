#!/usr/bin/env python3
"""Compare native DSP ADPCM decoding to independent vgmstream on local samples."""
import argparse
import ctypes
import hashlib
import json
import struct
import subprocess
import wave
from pathlib import Path
from analyze import archive_entries
from local_output import local_output
from nx_read import Slice


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--romfs', type=Path, required=True)
    ap.add_argument('--library', type=Path, required=True)
    ap.add_argument('--reference', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    out = local_output(args.out)
    out.mkdir(parents=True, exist_ok=False)
    decode = ctypes.CDLL(str(args.library.resolve())).hy_dsp_decode
    decode.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]
    decode.restype = ctypes.c_int
    results = []
    for archive in sorted(args.romfs.glob('FOAF*/sound/sound_se.dat')):
        with archive.open('rb') as f:
            members = [e for e in archive_entries(Slice(f, 0, archive.stat().st_size)) if e['name'].endswith('.adpcm')]
            for index in sorted({0, len(members) // 2, len(members) - 1}):
                member = members[index]
                f.seek(member['offset'])
                raw = f.read(member['size'])
                if len(raw) < 120 or struct.unpack_from('<I', raw)[0] != 8:
                    raise ValueError('unexpected Nmpl audio header')
                samples, nibbles, rate = struct.unpack_from('<3I', raw, 24)
                coefficients = struct.unpack_from('<16h', raw, 52)
                histories = struct.unpack_from('<2h', raw, 88)
                if histories != (0, 0):
                    raise ValueError('GENH comparison requires zero initial history')
                payload = raw[120:120 + (samples + 13) // 14 * 8]
                if len(payload) * 2 > nibbles + 16:
                    raise ValueError('inconsistent DSP payload')
                native = (ctypes.c_int16 * samples)()
                history = (ctypes.c_int16 * 2)(*histories)
                coeff = (ctypes.c_int16 * 16)(*coefficients)
                if decode(payload, len(payload), coeff, history, native, samples):
                    raise ValueError('native decode failed')
                header = bytearray(128)
                struct.pack_into('<15I', header, 0, 0x484e4547, 1, 8, rate, 0, samples, 12, 128, 128, 96, 0, 0, 0, 0, 0)
                struct.pack_into('>16h', header, 96, *coefficients)
                fixture = out / 'comparison.genh'
                fixture.write_bytes(header + payload)
                wav = out / 'comparison.wav'
                subprocess.run([str(args.reference.resolve()), '-i', '-o', str(wav), str(fixture)],
                               check=True, capture_output=True)
                with wave.open(str(wav), 'rb') as stream:
                    if stream.getnchannels() != 1 or stream.getsampwidth() != 2 or stream.getframerate() != rate:
                        raise ValueError('unexpected reference PCM format')
                    reference = stream.readframes(stream.getnframes())
                if reference != bytes(native):
                    raise ValueError(f"PCM mismatch: {archive.parent.parent.name}/{member['name']}")
                results.append(dict(archive=str(archive.relative_to(args.romfs)), member=member['name'], samples=samples, bit_exact=True))
    if len(results) != 9:
        raise ValueError('incomplete comparison coverage')
    (out / 'comparison.genh').unlink()
    (out / 'comparison.wav').unlink()
    report = dict(reference='vgmstream', reference_sha256=hashlib.sha256(args.reference.read_bytes()).hexdigest(),
                  library_sha256=hashlib.sha256(args.library.read_bytes()).hexdigest(), comparisons=results)
    (out / 'report.json').write_text(json.dumps(report, indent=2))
    print(f"PASS: {len(results)} complete ADPCM streams, {sum(r['samples'] for r in results)} PCM values match vgmstream bit-for-bit")


if __name__ == '__main__':
    main()
