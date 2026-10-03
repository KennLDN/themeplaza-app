#!/usr/bin/env python3
"""Fills the emulated SD card's /Themes with many small themes to test a large Collection, or removes them.

  stress_sd.py add 150      writes "Stress 001.zip" ... made from the fixtures (body and SMDH only, renamed)
  stress_sd.py remove       deletes every "Stress NNN.zip" again
"""
import glob, os, re, sys, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SD = os.path.expanduser('~/.var/app/org.azahar_emu.Azahar/data/azahar-emu/sdmc/Themes')


def main():
    if sys.argv[1] == 'remove':
        n = 0
        for f in os.listdir(SD):
            if re.fullmatch(r'Stress \d{3}\.zip', f): os.unlink(os.path.join(SD, f)); n += 1
        print('removed', n); return
    count = int(sys.argv[2])
    src = sorted(glob.glob(os.path.join(ROOT, 'emulator', 'fixtures', 'Themes', '*.zip')))
    for i in range(count):
        z = zipfile.ZipFile(src[i % len(src)])
        smdh = bytearray(z.read('info.smdh'))
        title = f'Stress {i + 1:03d}'.encode('utf-16le')
        for lang in range(12):   # rename, so each copy sorts and shows separately
            o = 8 + lang * 0x200
            smdh[o:o + 0x80] = title.ljust(0x80, b'\0')
        with zipfile.ZipFile(os.path.join(SD, f'Stress {i + 1:03d}.zip'), 'w', zipfile.ZIP_DEFLATED) as out:
            out.writestr('info.smdh', bytes(smdh)); out.writestr('body_LZ.bin', z.read('body_LZ.bin'))
    print('added', count)


if __name__ == '__main__':
    main()
