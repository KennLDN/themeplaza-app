#!/usr/bin/env python3
"""Renders the HOME Menu icon (app/meta/icon.png, 48x48) from the vector artwork
app/meta/brand/tp-icon-dsi-blue.svg, at its final size in headless Chromium, so edges are drawn
for 48 pixels rather than shrunk from a large bitmap.

  make_icon.py          writes app/meta/icon.png (and app/build/icon_check.png, enlarged, to look at)

Over the artwork it draws the slim frame the system's own icons have (measured on the icons of the titles
in the emulator: Nintendo 3DS Sound, Camera, eShop, System Settings): 2 pixels round the edge in a darker
shade of the icon's own colour, lighter at the top than at the bottom, the picture inside it with its
corners rounded by about 2.5 pixels. The frame runs square into the icon's outer corners, as theirs do.
"""
import os, subprocess, sys, tempfile
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SVG = os.path.join(ROOT, 'app', 'meta', 'brand', 'tp-icon-dsi-blue.svg')
OUT = os.path.join(ROOT, 'app', 'meta', 'icon.png')
SIZE = 48
FRAME, CORNER = 2, 2.5
# the artwork's background gradient (top to bottom), which the frame follows at 78 % of its brightness
BACKGROUND = ['#46AAFB', '#3DA4F2', '#339BE8', '#238DD8', '#157CC4', '#086EAD', '#035E91', '#02527B', '#00496E']
SHADE = 0.78


def frame_svg():
    stops = ''
    for i, c in enumerate(BACKGROUND):
        r, g, b = (int(int(c[k:k + 2], 16) * SHADE) for k in (1, 3, 5))
        stops += f'<stop offset="{i / (len(BACKGROUND) - 1):.4f}" stop-color="#{r:02x}{g:02x}{b:02x}"/>'
    f, s, c = FRAME, SIZE - 2 * FRAME, CORNER
    inner = (f'M{f + c},{f} h{s - 2 * c} a{c},{c} 0 0 1 {c},{c} v{s - 2 * c} a{c},{c} 0 0 1 {-c},{c} h{-(s - 2 * c)} '
             f'a{c},{c} 0 0 1 {-c},{-c} v{-(s - 2 * c)} a{c},{c} 0 0 1 {c},{-c} z')
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{SIZE}" height="{SIZE}" viewBox="0 0 {SIZE} {SIZE}" style="position:absolute;left:0;top:0">'
            f'<defs><linearGradient id="fr" x1="0" y1="0" x2="0" y2="1">{stops}</linearGradient></defs>'
            f'<path fill="url(#fr)" fill-rule="evenodd" d="M0,0 h{SIZE} v{SIZE} h{-SIZE} z {inner}"/></svg>')


def main():
    with tempfile.TemporaryDirectory() as tmp:
        page = os.path.join(tmp, 'icon.html')
        shot = os.path.join(tmp, 'shot.png')
        open(page, 'w').write(f'<!doctype html><html><body style="margin:0;background:#000;overflow:hidden">'
                              f'<img src="file://{SVG}" width="{SIZE}" height="{SIZE}" style="display:block">{frame_svg()}</body></html>')
        r = subprocess.run(['chromium-browser', '--headless=new', '--disable-gpu', '--hide-scrollbars', '--force-device-scale-factor=1',
                            f'--window-size={SIZE},{SIZE}', '--default-background-color=00000000', f'--screenshot={shot}', f'file://{page}'],
                           capture_output=True, text=True, timeout=60)
        if not os.path.exists(shot): sys.exit('chromium did not write a screenshot: ' + r.stderr[-400:])
        im = Image.open(shot).convert('RGB').crop((0, 0, SIZE, SIZE))
    im.save(OUT)
    os.makedirs(os.path.join(ROOT, 'app', 'build'), exist_ok=True)
    check = Image.new('RGB', (SIZE * 8 + 20 + SIZE, SIZE * 8), (40, 40, 40))
    check.paste(im.resize((SIZE * 8, SIZE * 8), Image.NEAREST), (0, 0)); check.paste(im, (SIZE * 8 + 20, 0))
    check.save(os.path.join(ROOT, 'app', 'build', 'icon_check.png'))
    print(f'{OUT}: {im.size}')


if __name__ == '__main__':
    main()
