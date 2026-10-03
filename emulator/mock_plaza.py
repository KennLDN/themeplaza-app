#!/usr/bin/env python3
"""A stand-in for Theme Plaza built from the zips in emulator/fixtures, for testing the app's client
without the live site: the v1 API as the site has it, the per-item resources, and (with --v2) the
v2 endpoints the app can use when a server offers them. It can also misbehave on request.

  mock_plaza.py [--port 8377] [--v2] [--page-size 24]

Point the app at it by writing its address into the emulated SD card:
  sdmc:/3ds/Theme Plaza/plaza_server.txt  containing  http://127.0.0.1:8377
(emulator/use_mock.py on / off does that.)

Behaviour can be switched while it runs:
  curl "http://127.0.0.1:8377/_mode?set=normal"       as the real site
  curl ".../_mode?set=blocked"                     every answer is a 403 challenge page
  curl ".../_mode?set=garbage"                        list requests answer 200 with a web page
  curl ".../_mode?set=slow"                           every answer takes 1.5 s
  curl ".../_mode?set=gone"                           downloads answer 200 with a web page (an item the site removed)
  curl ".../_stats"                                   request counts since the last call
"""
import argparse, glob, hashlib, json, os, re, struct, time, zipfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIX = os.path.join(ROOT, 'emulator', 'fixtures')
ITEMS = {}
STATE = {'mode': 'normal', 'v2': False, 'page': 24, 'counts': {}}
TAGS = ['anime', 'pokemon', 'cute', 'nintendo', 'mario', 'zelda', 'sonic', 'music', 'retro', 'dark']


def smdh_text(d, off, units):
    return d[off:off + units * 2].decode('utf-16le', 'replace').split('\0')[0].strip()


def load():
    for cat, folder in ((1, 'Themes'), (2, 'Splashes'), (3, 'Badges')):
        for path in sorted(glob.glob(os.path.join(FIX, folder, '*.zip'))):
            m = re.search(r'\((\d+)\)\.zip$', path)
            if not m: continue
            i = int(m.group(1))
            base = os.path.basename(path)[:-len(m.group(0))].strip()
            title, _, author = base.rpartition(' by ')
            z = zipfile.ZipFile(path)
            names = {os.path.basename(n).lower(): n for n in z.namelist()}
            item = {'id': i, 'category': cat, 'path': path, 'title': title or base, 'author': author, 'description': '', 'zip': z, 'names': names}
            if 'info.smdh' in names:
                d = z.read(names['info.smdh'])
                item['smdh'] = d
                t = smdh_text(d, 8 + 0x200, 64) or smdh_text(d, 8, 64)
                if t: item['title'] = t
                item['description'] = smdh_text(d, 8 + 0x200 + 0x80, 128) or smdh_text(d, 8 + 0x80, 128)
                item['author'] = smdh_text(d, 8 + 0x200 + 0x180, 64) or item['author']
            h = int(hashlib.md5(str(i).encode()).hexdigest(), 16)
            item['downloads'] = h % 50000; item['likes'] = (h >> 20) % 2000
            item['tags'] = [TAGS[h % 10], TAGS[(h >> 8) % 10]]
            item['bgm'] = 'bgm.bcstm' in names
            ITEMS[i] = item


def make_smdh(item):
    """Badge zips have no SMDH; the site generates one. Do the same, with a flat-colour icon."""
    if 'smdh' in item: return item['smdh']
    d = bytearray(0x36C0); d[0:4] = b'SMDH'
    for lang in range(12):
        o = 8 + lang * 0x200
        d[o:o + len(item['title']) * 2] = item['title'].encode('utf-16le')[:0x7e]
        a = item['author'].encode('utf-16le')[:0x7e]; d[o + 0x180:o + 0x180 + len(a)] = a
    colour = struct.pack('<H', (item['id'] * 2654435761) & 0xffff)
    d[0x24C0:0x24C0 + 0x1200] = colour * 0x900
    return bytes(d)


def matches(item, query, tags):
    hay = (item['title'] + ' ' + item['description']).lower()
    for word in query.split():
        if word.startswith('tag:'):
            if not any(t in item['tags'] for t in word[4:].lower().split(',')): return False
        elif word.startswith('user:'):
            if item['author'].lower() != word[5:].lower(): return False
        elif word.lower() not in hay: return False
    return all(t in item['tags'] for t in tags)


class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *a): pass

    def send(self, status, ctype, body, extra=None):
        self.send_response(status)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(body)))
        for k, v in (extra or {}).items(): self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def js(self, obj, status=200): self.send(status, 'application/json', json.dumps(obj).encode())

    def do_GET(self):
        u = urlparse(self.path); q = {k: v[0] for k, v in parse_qs(u.query).items()}
        path = u.path
        if path == '/_mode':
            STATE['mode'] = q.get('set', 'normal'); return self.js({'mode': STATE['mode']})
        if path == '/_stats':
            c = STATE['counts']; STATE['counts'] = {}; return self.js(c)
        key = re.sub(r'\d+', 'N', path)
        STATE['counts'][key] = STATE['counts'].get(key, 0) + 1
        print(time.strftime('%H:%M:%S'), self.path, self.headers.get('User-Agent', '-'), flush=True)
        mode = STATE['mode']
        if mode == 'slow': time.sleep(1.5)
        if mode == 'blocked' or not self.headers.get('User-Agent'):
            return self.send(403, 'text/html', b'<html><title>Just a moment...</title></html>', {'cf-mitigated': 'challenge'})
        if mode == 'garbage' and '/list' in path: return self.send(200, 'text/html', b'<html>maintenance</html>')

        if path in ('/api/anemone/v1/list', '/api/anemone/v2/list'):
            v2 = path.endswith('v2/list')
            if v2 and not STATE['v2']: return self.send(404, 'text/html', b'<html>404</html>')
            cat = int(q.get('category', '1') or 1); page = max(1, int(q.get('page', '1') or 1))
            tags = [t for t in q.get('tags', '').split(',') if t] if v2 else []
            items = [it for it in ITEMS.values() if it['category'] == cat and matches(it, q.get('query', ''), tags)]
            sort = q.get('sort', 'new') if v2 else 'new'
            items.sort(key=lambda it: -(it['downloads'] if sort == 'downloads' else it['likes'] if sort == 'likes' else it['id']))
            n = STATE['page']; pages = (len(items) + n - 1) // n
            rows = items[(page - 1) * n:page * n]
            if not v2:
                if not rows: return self.js({'success': False, 'message': 'No items found'})
                return self.js({'success': True, 'pages': pages, 'items': [it['id'] for it in rows]})
            return self.js({'success': True, 'page': page, 'pages': pages, 'total': len(items), 'items': [
                {'id': it['id'], 'title': it['title'], 'author': it['author'], 'description': it['description'], 'downloads': it['downloads'],
                 'likes': it['likes'], 'bgm': it['bgm'], 'tags': it['tags'], 'size': os.path.getsize(it['path'])} for it in rows]})
        if path == '/api/anemone/v2/icons':
            if not STATE['v2']: return self.send(404, 'text/html', b'<html>404</html>')
            out = b''
            for s in q.get('ids', '').split(','):
                it = ITEMS.get(int(s)) if s.isdigit() else None
                out += make_smdh(it)[0x24C0:0x24C0 + 0x1200] if it else bytes(0x1200)
            return self.send(200, 'application/octet-stream', out, {'Cache-Control': 'max-age=604800'})
        if path == '/api/anemone/v1/query':
            it = ITEMS.get(int(q.get('item_id', '0') or 0))
            if not it: return self.js({'success': False, 'message': 'Theme not found'})
            return self.js({'success': True, 'title': it['title'], 'description': it['description'], 'download_count': it['downloads'], 'likes': it['likes'], 'nsfw': 0,
                            'metadata': {'enable_bgm': 1 if it['bgm'] else 0} if it['category'] == 1 else None, 'upload_date': '2026-10-01 20:36:16', 'author': it['author'], 'tags': it['tags']})
        m = re.match(r'^/download/(\d+)(/.*)?$', path)
        if m:
            it = ITEMS.get(int(m.group(1))); rest = m.group(2) or ''
            if not it: return self.js({'message': 'Not Found'}, 404)
            z, names = it['zip'], it['names']
            # what the real site does for an item it no longer has: one of its own pages, status 200
            if rest == '' and mode == 'gone': return self.send(200, 'text/html', b'<!doctype html>\n<html><title>Themes | Theme Plaza</title>' + b'<p>list</p>' * 2000 + b'</html>')
            if rest == '': return self.send(200, 'application/zip', open(it['path'], 'rb').read())
            if rest == '/smdh':
                d = make_smdh(it); rng = self.headers.get('Range')
                if rng and (r := re.match(r'bytes=(\d+)-(\d+)', rng)):
                    a, b = int(r.group(1)), int(r.group(2))
                    return self.send(206, 'application/octet-stream', d[a:b + 1], {'Content-Range': f'bytes {a}-{b}/{len(d)}'})
                return self.send(200, 'application/octet-stream', d)
            if rest == '/bgm' and 'bgm.ogg' in names: return self.send(200, 'audio/ogg', z.read(names['bgm.ogg']))
            pick = {'/preview': ['preview.png'], '/preview/top': ['pt_top.png', 'preview.png'], '/preview/bottom': ['pt_bottom.png', 'preview.png'], '/preview/icon': ['icon.png', '_seticon.png']}.get(rest)
            if pick:
                for n in pick:
                    if n in names: return self.send(200, 'image/png', z.read(names[n]))
                if it['category'] == 3 and rest == '/preview':   # the site builds a sheet of the badges; send the first one
                    pngs = [n for n in z.namelist() if n.lower().endswith('.png') and not n.startswith('_')]
                    if pngs: return self.send(200, 'image/png', z.read(pngs[0]))
            return self.js({'message': 'Not Found'}, 404)
        return self.send(404, 'text/html', b'<html>404</html>')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', type=int, default=8377); ap.add_argument('--v2', action='store_true'); ap.add_argument('--page-size', type=int, default=24)
    a = ap.parse_args()
    STATE['v2'] = a.v2; STATE['page'] = a.page_size
    load()
    print(f'{len(ITEMS)} items; v2 {"on" if a.v2 else "off"}; page size {a.page_size}; http://127.0.0.1:{a.port}', flush=True)
    ThreadingHTTPServer(('127.0.0.1', a.port), Handler).serve_forever()


if __name__ == '__main__':
    main()
