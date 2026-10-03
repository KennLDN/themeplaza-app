#!/usr/bin/env python3
"""Finds the key the HOME Menu uses to check a title's start-up logo, in the HOME Menu program on this
machine's emulator, and saves it to ~/.3ds/logo_hmackey_text (outside the project: it is Nintendo's).

A logo is a DARC archive followed by HMAC-SHA256(key, archive), packed with LZ11 and padded to 0x2000
bytes; the HOME Menu stops with a panic when the HMAC does not match (see yellows8's ctr-logobuilder,
buildlogo.sh). makerom's own logo (app/meta/logo_base.darc, which still carries its HMAC) is signed with
that key, so the key is whichever 32 bytes of the HOME Menu's code reproduce that HMAC.

  find_logo_key.py
"""
import hashlib, hmac, os, struct, sys

HOME_MENU = os.path.expanduser('~/.var/app/org.azahar_emu.Azahar/data/azahar-emu/nand/00000000000000000000000000000000/'
                               'title/00040030/00009802/content/00000089.app')
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KEY_FILE = os.path.expanduser('~/.3ds/logo_hmackey_text')


def blz_decompress(data):
    """The backward LZ77 that ExeFS .code is packed with when the exheader says so."""
    off_size, add_size = struct.unpack_from('<II', data, len(data) - 8)
    hdr, enc = off_size >> 24, off_size & 0xFFFFFF
    out = bytearray(data) + bytearray(add_size)
    src, end, dst = len(data) - hdr, len(data) - enc, len(out)
    while src > end:
        src -= 1; flags = data[src]
        for _ in range(8):
            if src <= end: break
            if flags & 0x80:
                src -= 2; v = data[src] | data[src + 1] << 8
                n, disp = (v >> 12) + 3, (v & 0xFFF) + 3
                for _ in range(n): dst -= 1; out[dst] = out[dst + disp]
            else:
                src -= 1; dst -= 1; out[dst] = data[src]
            flags = flags << 1 & 0xFF
    return bytes(out)


def home_menu_code():
    f = open(HOME_MENU, 'rb')
    h = f.read(0x200 + 0x400)
    assert h[0x100:0x104] == b'NCCH' and h[0x18F] & 4, 'HOME Menu NCCH missing or encrypted'
    compressed = h[0x200 + 0x0D] & 1
    exefs = struct.unpack_from('<I', h, 0x1A0)[0] * 0x200
    f.seek(exefs); eh = f.read(0x200)
    for i in range(8):
        name, off, size = struct.unpack_from('<8sII', eh, i * 16)
        if name.rstrip(b'\0') == b'.code':
            f.seek(exefs + 0x200 + off); code = f.read(size)
            return blz_decompress(code) if compressed else code
    sys.exit('no .code in the HOME Menu ExeFS')


def main():
    logo = open(os.path.join(ROOT, 'app', 'meta', 'logo_base.darc'), 'rb').read()
    size = struct.unpack_from('<I', logo, 0x0C)[0]
    darc, want = logo[:size], logo[size:size + 32]
    code = home_menu_code()
    print(f'HOME Menu code: {len(code)} bytes; looking for the key that gives {want[:4].hex()}...', flush=True)
    for step in (4, 1):
        for i in range(0, len(code) - 32, step):
            if step == 1 and i % 4 == 0: continue
            if hmac.new(code[i:i + 32], darc, hashlib.sha256).digest() == want:
                os.makedirs(os.path.dirname(KEY_FILE), exist_ok=True)
                open(KEY_FILE, 'w').write(code[i:i + 32].hex() + '\n')
                print(f'found at code offset {i:#x}; saved to {KEY_FILE}')
                return
    sys.exit('not found')


if __name__ == '__main__':
    main()
