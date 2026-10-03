#!/usr/bin/env python3
"""Puts test content on the emulator's SD card: theme, splash and badge zips from Theme Plaza.

  seed_sd.py [themes] [splashes] [badges]     counts, default 14 4 3

Downloads are kept in emulator/fixtures/ so they are fetched only once. Files already on the
emulated SD card are left alone.
"""
import json, os, re, shutil, sys, time, urllib.request, urllib.parse

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SD = os.path.expanduser('~/.var/app/org.azahar_emu.Azahar/data/azahar-emu/sdmc')
FIX = os.path.join(ROOT, 'emulator', 'fixtures')
UA = 'ThemePlaza/0.4.0 (Nintendo 3DS)'
BASE = 'http://themeplaza.art'
KINDS = [('Themes', 1), ('Splashes', 2), ('Badges', 3)]


def get(url):
    req = urllib.request.Request(url, headers={'User-Agent': UA})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read(), r.headers


def main():
    counts = [int(a) for a in sys.argv[1:4]] + [14, 4, 3][len(sys.argv) - 1:]
    for (folder, cat), want in zip(KINDS, counts):
        os.makedirs(os.path.join(FIX, folder), exist_ok=True)
        os.makedirs(os.path.join(SD, folder), exist_ok=True)
        have = sorted(f for f in os.listdir(os.path.join(FIX, folder)) if f.endswith('.zip'))
        page = 1
        while len(have) < want:
            body, _ = get(f'{BASE}/api/anemone/v1/list?page={page}&category={cat}')
            ids = json.loads(body).get('items', [])
            if not ids: break
            for i in ids:
                if len(have) >= want: break
                if any(f'({i}).zip' in f for f in have): continue
                data, hdr = get(f'{BASE}/download/{i}')
                m = re.search(r"filename\*=UTF-8''([^;]+)", hdr.get('Content-Disposition', ''))
                name = urllib.parse.unquote(m.group(1)) if m else f'item ({i}).zip'
                name = re.sub(r'[\\/:*?"<>|]', '_', name)
                with open(os.path.join(FIX, folder, name), 'wb') as f: f.write(data)
                have.append(name)
                print(folder, name, len(data), flush=True)
                time.sleep(1.0)
            page += 1
        for f in have[:want]:
            dst = os.path.join(SD, folder, f)
            if not os.path.exists(dst): shutil.copyfile(os.path.join(FIX, folder, f), dst)
    print('done')


if __name__ == '__main__':
    main()
