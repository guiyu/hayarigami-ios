"""Local bounded reads for this title's PFS0/NCA3/CTR/RomFS containers.

Format references: SciresM/hactool nca.h, nca.c, aes.c, ivfc.h.
No key values, decrypted headers or game bytes are logged.
This is an inspection reader, not a signature/authenticity verifier.
"""
import hashlib
import struct
from pathlib import PurePosixPath


def safe_name(name):
    if not name or name in ('.', '..') or '/' in name or '\\' in name:
        raise ValueError('unsafe container filename')
    return name


class Slice:
    def __init__(self, file, offset, size):
        self.file, self.offset, self.size = file, offset, size

    def read(self, offset, size):
        if offset < 0 or size < 0 or offset + size > self.size:
            raise ValueError('read outside container bounds')
        self.file.seek(self.offset + offset)
        data = self.file.read(size)
        if len(data) != size:
            raise ValueError('truncated container')
        return data


def pfs_entries(view):
    magic, count, names_size, _ = struct.unpack('<4sIII', view.read(0, 16))
    if magic != b'PFS0' or count > 100000 or names_size > 16 * 1024 * 1024:
        raise ValueError('invalid PFS0 header')
    table = view.read(16, count * 24)
    names = view.read(16 + count * 24, names_size)
    start = 16 + count * 24 + names_size
    result = []
    for i in range(count):
        offset, size, name_off, _ = struct.unpack_from('<QQII', table, i * 24)
        end = names.find(b'\0', name_off)
        if name_off >= len(names) or end < 0 or start + offset + size > view.size:
            raise ValueError('invalid PFS0 entry')
        name = safe_name(names[name_off:end].decode('utf-8'))
        if any(e['name'] == name for e in result):
            raise ValueError('duplicate PFS0 filename')
        result.append(dict(name=name, offset=start + offset, size=size))
    spans = sorted((e['offset'], e['offset'] + e['size']) for e in result)
    if any(a[1] > b[0] for a, b in zip(spans, spans[1:])):
        raise ValueError('overlapping PFS0 entries')
    return result


def load_keys(path):
    result = {}
    for line in path.read_text().splitlines():
        if '=' not in line or line.lstrip().startswith('#'):
            continue
        name, value = line.split('=', 1)
        # Only retain the two classes needed for the observed standard crypto.
        name = name.strip()
        if name == 'header_key' or name.startswith('key_area_key_'):
            try:
                result[name] = bytes.fromhex(value.strip())
            except ValueError:
                raise ValueError('invalid local key encoding') from None
    return result


def aes_decrypt(key, mode, data):
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms
    ctx = Cipher(algorithms.AES(key), mode).decryptor()
    return ctx.update(data) + ctx.finalize()


class NCA:
    def __init__(self, view, keys):
        from cryptography.hazmat.primitives.ciphers import modes
        self.view = view
        raw = view.read(0, 0xc00)
        if 'header_key' not in keys:
            raise ValueError('missing header_key in local key file')
        header = b''.join(aes_decrypt(keys['header_key'], modes.XTS(i.to_bytes(16, 'big')),
                                     raw[i * 512:(i + 1) * 512]) for i in range(6))
        if header[0x200:0x204] != b'NCA3':
            raise ValueError('unsupported NCA or incorrect header key')
        if struct.unpack_from('<Q', header, 0x208)[0] != view.size:
            raise ValueError('NCA size mismatch')
        if any(header[0x230:0x240]):
            raise ValueError('rights-managed NCA requires a separately supported title-key path')
        generation = max(max(header[0x206], header[0x220]) - 1, 0)
        area = header[0x207]
        if area > 2:
            raise ValueError('unsupported key area')
        key_name = f'key_area_key_{("application", "ocean", "system")[area]}_{generation:02x}'
        if key_name not in keys:
            raise ValueError('missing local key: ' + key_name)
        self._content_key = aes_decrypt(keys[key_name], modes.ECB(), header[0x300:0x340])[32:48]
        self.title_id = f'{struct.unpack_from("<Q", header, 0x210)[0]:016X}'
        self.content_type = header[0x205]
        self.sdk = '.'.join(map(str, reversed(header[0x21c:0x220])))
        self.generation = generation
        self.signature_zero = not any(header[:0x100])
        self.sections = []
        for i in range(4):
            lo, hi = struct.unpack_from('<II', header, 0x240 + 16 * i)
            if not lo:
                continue
            if hi <= lo or hi * 512 > view.size:
                raise ValueError('invalid NCA section bounds')
            fs = header[0x400 + 512 * i:0x600 + 512 * i]
            if hashlib.sha256(fs).digest() != header[0x280 + i * 32:0x2a0 + i * 32]:
                raise ValueError('NCA filesystem header hash mismatch')
            self.sections.append(Section(self, lo * 512, (hi - lo) * 512, fs))


class Section:
    def __init__(self, nca, offset, size, header):
        self.nca, self.offset, self.size, self.header = nca, offset, size, header
        self.kind, self.crypto = header[2], header[4]
        if self.crypto not in (1, 3):
            raise ValueError('unsupported section crypto (patches are not accepted)')

    def read(self, offset, size):
        from cryptography.hazmat.primitives.ciphers import modes
        if offset < 0 or size < 0 or offset + size > self.size:
            raise ValueError('read outside NCA section')
        absolute = self.offset + offset
        if self.crypto == 1:
            return self.nca.view.read(absolute, size)
        aligned = absolute & ~15
        skip = absolute - aligned
        ctr = self.header[0x140:0x148][::-1] + (aligned // 16).to_bytes(8, 'big')
        data = self.nca.view.read(aligned, skip + size)
        return aes_decrypt(self.nca._content_key, modes.CTR(ctr), data)[skip:]

    def filesystem(self):
        if self.kind == 1:
            off, size = struct.unpack_from('<QQ', self.header, 0x40)
        elif self.kind == 0 and self.header[8:12] == b'IVFC':
            # Observed six-level IVFC. Do not silently treat a new layout as this one.
            if struct.unpack_from('<I', self.header, 0x14)[0] != 7:
                raise ValueError('unsupported IVFC level count')
            off, size = struct.unpack_from('<QQ', self.header, 0x18 + 5 * 24)
        else:
            raise ValueError('unsupported filesystem')
        if off + size > self.size:
            raise ValueError('filesystem exceeds section')
        return Subview(self, off, size)


class Subview:
    def __init__(self, parent, offset, size):
        self.parent, self.offset, self.size = parent, offset, size

    def read(self, offset, size):
        if offset < 0 or size < 0 or offset + size > self.size:
            raise ValueError('read outside filesystem')
        return self.parent.read(self.offset + offset, size)


def romfs_entries(view):
    h = struct.unpack('<10Q', view.read(0, 80))
    if h[0] != 80 or h[4] > 64 * 1024 * 1024 or h[8] > 64 * 1024 * 1024:
        raise ValueError('invalid RomFS header')
    dirs, files = view.read(h[3], h[4]), view.read(h[7], h[8])
    result, seen_dirs, seen_files = [], set(), set()

    def visit(off, parent):
        if off in seen_dirs or off + 24 > len(dirs):
            raise ValueError('invalid or cyclic RomFS directory')
        seen_dirs.add(off)
        _, sibling, child, file, _, n = struct.unpack_from('<6I', dirs, off)
        if off + 24 + n > len(dirs):
            raise ValueError('truncated directory name')
        name = dirs[off + 24:off + 24 + n].decode('utf-8')
        path = parent / safe_name(name) if name else parent
        while file != 0xffffffff:
            if file in seen_files or file + 32 > len(files):
                raise ValueError('invalid or cyclic RomFS file')
            seen_files.add(file)
            _, nxt, data_off, size, _, n = struct.unpack_from('<IIQQII', files, file)
            if file + 32 + n > len(files) or h[9] + data_off + size > view.size:
                raise ValueError('invalid RomFS file bounds')
            name = safe_name(files[file + 32:file + 32 + n].decode('utf-8'))
            result.append(dict(name=str(path / name), offset=h[9] + data_off, size=size))
            file = nxt
        if child != 0xffffffff:
            visit(child, path)
        if sibling != 0xffffffff:
            visit(sibling, parent)

    visit(0, PurePosixPath())
    return result
