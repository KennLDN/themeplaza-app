#!/usr/bin/env python3
"""Fetches a sample of Theme Plaza items spread over the site's whole history and keeps them in
tools/hosttest/out/spread/ (the regular fixtures are all recent uploads; older ones may be packed differently).

  spread.py [COUNT] [SEED]      default 60 ids, seed 1
  spread.py badges [COUNT]      COUNT badge sets from list pages spread from the newest to the oldest (default 10)
  spread.py splashes [COUNT]    the same for splashes

One id is picked at random from each of COUNT equal slices of the id range. Ids that no longer exist are
skipped. Files already there are not fetched again. One request every 2 seconds.
Then run:  python3 tools/hosttest/run.py tools/hosttest/out/spread/*.zip
"""
import os, random, re, sys, time, urllib.error, urllib.parse, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'out', 'spread')
UA = 'ThemePlaza/0.4.0 (Nintendo 3DS)'
BASE = 'http://themeplaza.art'
NEWEST = 149700
def fetch(i, have):
    """One item by id into OUT. Returns True if the file is there afterwards."""
    if any(f'({i}).' in f for f in have): return True
    req = urllib.request.Request(f'{BASE}/download/{i}', headers={'User-Agent': UA})
    try:
        with urllib.request.urlopen(req, timeout=60) as r: data, hdr = r.read(), r.headers
    except urllib.error.HTTPError as e:
        print(f'{i}: HTTP {e.code}', flush=True); return False
    except OSError as e:
        print(f'{i}: {e}', flush=True); return False
    if not data.startswith(b'PK'):
        print(f'{i}: not a zip ({len(data)} bytes, starts {data[:16]!r})', flush=True); return False
    m = re.search(r"filename\*=UTF-8''([^;]+)", hdr.get('Content-Disposition', ''))
    name = urllib.parse.unquote(m.group(1)) if m else f'item ({i}).zip'
    name = re.sub(r'[\\/:*?"<>|]', '_', name)
    if f'({i})' not in name: name = name[:-4] + f' ({i}).zip'
    with open(os.path.join(OUT, name), 'wb') as f: f.write(data)
    print(f'{i}: {name} ({len(data)} bytes)', flush=True)
    return True


def by_category(cat, count):
    """COUNT items of one category (2 splashes, 3 badges), from list pages spread from newest to oldest."""
    import json
    os.makedirs(OUT, exist_ok=True)
    have = os.listdir(OUT)
    def page(p):
        req = urllib.request.Request(f'{BASE}/api/anemone/v1/list?page={p}&category={cat}', headers={'User-Agent': UA})
        with urllib.request.urlopen(req, timeout=60) as r: return json.loads(r.read())
    first = page(1); pages = int(first.get('pages', 1))
    print(f'category {cat}: {pages} pages', flush=True)
    got = 0
    for k in range(count):
        p = 1 + (pages - 1) * k // max(1, count - 1)
        time.sleep(2)
        ids = (first if p == 1 else page(p)).get('items', [])
        if not ids: continue
        time.sleep(2)
        got += fetch(ids[len(ids) // 2], have)
    print(f'{got} of {count} fetched')


def main():
    if len(sys.argv) > 1 and sys.argv[1] in ('splashes', 'badges'):
        return by_category(2 if sys.argv[1] == 'splashes' else 3, int(sys.argv[2]) if len(sys.argv) > 2 else 10)
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 60
    rng = random.Random(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
    os.makedirs(OUT, exist_ok=True)
    have = os.listdir(OUT)
    step = NEWEST // count
    got = missing = 0
    for k in range(count):
        i = k * step + rng.randint(1, step)
        if any(f'({i}).' in f for f in have): got += 1; continue
        ok = fetch(i, have)
        got += ok; missing += not ok
        time.sleep(2)
    print(f'{got} files in {OUT}, {missing} ids without a file')


if __name__ == '__main__':
    main()
