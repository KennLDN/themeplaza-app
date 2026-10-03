#!/usr/bin/env python3
"""Sets the title version of a built CIA (TMD and ticket), because the makerom in tools/bin ignores -ver,
-major and -minor when it builds a CIA from an ELF (the TMD says version 0 whatever is passed).

The HOME Menu caches each title's name and icon by title id and version (3dbrew: Home Menu, Cache.dat),
so without a new version a console keeps showing the old ones after an update.

  set_cia_version.py FILE.cia MAJOR.MINOR.MICRO
Safe to patch: makerom's signatures are placeholders that the installers accept anyway, and the TMD's
content hashes cover the content records, not the header field changed here.
"""
import struct, sys

SIG_LEN = {0x10003: 0x200, 0x10004: 0x100, 0x10005: 0x3C}


def sig_body(buf, off):
    """Offset of the signed body after a signature block (type, signature, padding to 0x40)."""
    t = struct.unpack_from('>I', buf, off)[0]
    n = 4 + SIG_LEN[t]
    return off + (n + 0x3F) // 0x40 * 0x40


def main():
    path, ver = sys.argv[1], sys.argv[2]
    major, minor, micro = (int(x) for x in ver.split('.'))
    assert major < 64 and minor < 64 and micro < 16, 'major and minor up to 63, micro up to 15'
    v = major << 10 | minor << 4 | micro
    c = bytearray(open(path, 'rb').read())
    hdr, cert, tik, tmd = struct.unpack_from('<IxxxxIII', c, 0)
    al = lambda n: (n + 63) // 64 * 64
    tk = al(hdr) + al(cert); tm = tk + al(tik)
    t, k = sig_body(c, tm), sig_body(c, tk)
    title = c[t + 0x4C:t + 0x54]
    assert c[k + 0x9C:k + 0xA4] == title, 'ticket and TMD name different titles'
    struct.pack_into('>H', c, t + 0x9C, v)        # TMD header: title version
    struct.pack_into('>H', c, k + 0xA6, v)        # ticket: title version
    open(path, 'wb').write(c)
    print(f'{path}: title {title.hex()} version {major}.{minor}.{micro} ({v})')


if __name__ == '__main__':
    main()
