#!/usr/bin/env python3
"""The app's start-up logo (app/meta/logo.bcma.lz): the short animation the HOME Menu plays while the
installed app loads. Written from nothing with tools/logo_scene.py; no part of another logo is reused.

  make_logo.py            writes app/build/logo/design.bcma.lz, a frame sheet and a GIF of it, and prints its size
  make_logo.py install    the same, and copies it to app/meta/logo.bcma.lz (what the build packs into the CIA)

What it shows
  both screens   the icon's blue gradient, running on from the top screen into the bottom one, with the app's
                 rosettes turning slowly in it
  top            the TP mark, built like the icon's: each letter's shape three times (dark shadow, lavender
                 underside, white face). (The icon also strokes a thin dark line where the T meets the P; at this
                 size it covers no pixel, because the P's outline already leaves that gap.) A: the letters pop
                 in one after the other. B (loops while the app loads): the mark floats up and down over its shadow.
  bottom         "Theme Plaza" and "Customize your 3DS!"
  C              everything fades out

The mark's shapes come straight from app/meta/brand/tp-icon-dsi-blue.svg, drawn by Chromium at the exact
size they have on the screen (SCALE pixels per SVG unit), so nothing is resampled.
"""
import math, os, re, subprocess, sys, tempfile
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import logo_build as B, logo_scene as S, logo_preview as P

OUT = os.path.join(ROOT, 'app', 'build', 'logo')
ICON_SVG = os.path.join(ROOT, 'app', 'meta', 'brand', 'tp-icon-dsi-blue.svg')
FONTS = os.path.join(HERE, 'fonts')
LEN = {'A': 48, 'B': 120, 'C': 15}

# The part of the icon's 1600-unit drawing that holds the mark with its underside and shadow, and its size on
# the screen: 2/15 px per unit makes the underside (30 units) exactly 4 px and the region 174 x 132 px.
VIEW = (150, 365, 1305, 990)
SCALE = 2 / 15
MARK_W, MARK_H = round(VIEW[2] * SCALE), round(VIEW[3] * SCALE)
BELT = 30 * SCALE                       # px the underside reaches below the face
SHADOW = (4 * SCALE, 58 * SCALE)        # px the shadow lies right of and below the face
COL_SHADOW, COL_BELT, COL_FACE_TOP, COL_FACE_BOTTOM = '#03416a', '#c3caf1', '#ffffff', '#f1f3fb'
BG = ['#46aafb', '#157cc4', '#00496e']  # the icon's gradient: its top, middle and bottom colours
# "Theme Plaza" on the bottom screen: a blue gradient inside a white outline. The picture is 32 px tall and
# the letters fill its middle 22, so the colours named here are a little beyond what the letters reach.
NAME_TOP, NAME_BOTTOM, NAME_OUTLINE = '#6fd2ff', '#0f6fc4', 2


def chromium(svg_body, w, h):
    """An RGBA picture of an SVG drawn at w x h pixels."""
    with tempfile.TemporaryDirectory() as tmp:
        page, shot = os.path.join(tmp, 'p.html'), os.path.join(tmp, 's.png')
        open(page, 'w').write(f'<!doctype html><html><body style="margin:0;background:transparent;overflow:hidden">{svg_body}</body></html>')
        r = subprocess.run(['chromium-browser', '--headless=new', '--disable-gpu', '--hide-scrollbars', '--force-device-scale-factor=1',
                            f'--window-size={max(w, 500)},{max(h, 500)}', '--default-background-color=00000000', f'--screenshot={shot}', f'file://{page}'],
                           capture_output=True, text=True, timeout=120)
        if not os.path.exists(shot): sys.exit('chromium did not write a screenshot: ' + r.stderr[-400:])
        return Image.open(shot).convert('RGBA').crop((0, 0, w, h))


def mark_layers():
    """The mark's shapes as masks over the 174 x 132 region, and the icon's own drawing of it for comparison."""
    svg = open(ICON_SVG).read()
    paths = ''.join(re.findall(r'<path id="[TP]"[^>]*/>', svg))
    assert paths.count('<path') == 2
    head = f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" viewBox="{VIEW[0]} {VIEW[1]} {VIEW[2]} {VIEW[3]}" width="{MARK_W}" height="{MARK_H}" style="display:block"><defs>{paths}'
    clip = '<clipPath id="Pclip"><use href="#P"/><use href="#P" y="15"/><use href="#P" y="30"/></clipPath>'
    one = lambda body: chromium(head + clip + '</defs>' + body + '</svg>', MARK_W, MARK_H)
    masks = {
        't': one('<use href="#T" fill="#fff"/>').getchannel('A'),
        'p': one('<use href="#P" fill="#fff"/>').getchannel('A'),
    }
    # the icon's own layers, minus its background
    body = svg[svg.index('<g fill="#03416A">'):svg.rindex('</svg>')]
    grad = '<linearGradient id="face" x1="0" y1="1" x2="0" y2="0"><stop offset="0" stop-color="#FFFFFF"/><stop offset="1" stop-color="#F1F3FB"/></linearGradient>'
    reference = chromium(head + clip + grad + '</defs>' + body + '</svg>', MARK_W, MARK_H)
    return masks, reference


def pot(n):
    p = 8
    while p < n: p *= 2
    return p


def tight(mask):
    """(texture: the mask's used part in a power-of-two picture, x and y of that picture's top left corner in the region)."""
    x0, y0, x1, y1 = mask.getbbox()
    w, h = pot(x1 - x0), pot(y1 - y0)
    tex = Image.new('L', (w, h), 0); tex.paste(mask.crop((x0, y0, x1, y1)), (0, 0))
    return tex, x0, y0


def rosette_mask(size=64):
    """The app's rosette, as drawn in its sprites: eight rounded bars, each 0.34r wide, from 0.4r to r out."""
    ss = 8; r = size / 2 * ss; c = size / 2 * ss
    im = Image.new('L', (size * ss, size * ss), 0); d = ImageDraw.Draw(im)
    w = r * 0.17
    for i in range(8):
        a = math.radians(i * 45); sx, sy = math.sin(a), -math.cos(a); px, py = -sy, sx
        x0, y0 = c + sx * (r - w), c + sy * (r - w); x1, y1 = c + sx * (0.4 * r + w), c + sy * (0.4 * r + w)
        d.polygon([(x0 + px * w, y0 + py * w), (x1 + px * w, y1 + py * w), (x1 - px * w, y1 - py * w), (x0 - px * w, y0 - py * w)], fill=255)
        for x, y in ((x0, y0), (x1, y1)): d.ellipse([x - w, y - w, x + w, y + w], fill=255)
    return im.resize((size, size), Image.LANCZOS)


def text_mask(text, font_file, px, w, h, outline=0):
    """Text centred in a w x h mask, and its drawn width. outline: the letters grown by that many pixels all
    round (the shape of an outline drawn behind them)."""
    ss = 4
    im = Image.new('L', (w * ss, h * ss), 0); d = ImageDraw.Draw(im)
    font = ImageFont.truetype(os.path.join(FONTS, font_file), px * ss)
    box = d.textbbox((0, 0), text, font=font)
    assert box[2] - box[0] <= w * ss and box[3] - box[1] <= h * ss, f'"{text}" at {px}px needs {(box[2] - box[0]) / ss:.0f} x {(box[3] - box[1]) / ss:.0f}'
    d.text(((w * ss - (box[2] - box[0])) // 2 - box[0], (h * ss - (box[3] - box[1])) // 2 - box[1]), text, font=font, fill=255,
           stroke_width=int(outline * ss), stroke_fill=255)
    return im.resize((w, h), Image.LANCZOS), (box[2] - box[0]) / ss


# The moving gradient. The two screens show one tall gradient (top of the top screen, the seam, bottom of the
# bottom screen); each of its six corner points swings a little lighter and darker, each a quarter turn behind
# the last going round, so the light travels round the picture once per loop of B.
SWING = [(20, 16, 4), (16, 18, 16), (10, 20, 26)]          # how far each row's colour swings, per channel
PHASE = {(0, 0): 0.0, (0, 1): 0.5, (1, 1): 0.75, (2, 1): 1.0, (2, 0): 1.5, (1, 0): 1.75}     # (row, side) -> turns of pi


def corner_colour(row, side, turns):
    """The colour of a point of the tall gradient (row 0..2, side 0 left / 1 right of the 400 px width) at a time in loops of B."""
    base = S.rgb(BG[row])
    w = math.sin(2 * math.pi * turns + math.pi * PHASE[(row, side)])
    return [base[k] + SWING[row][k] * w for k in range(3)]


def wave_keys(value, first, last, count):
    """Keys for a smooth function of the frame number, with slopes taken from the function itself."""
    out = []
    for i in range(count + 1):
        f = first + (last - first) * i / count
        out.append((f - first, value(f), (value(f + 0.01) - value(f - 0.01)) / 0.02))
    return out


def backdrop(s, top):
    g = s.null('all', alpha=0)
    rows = (0, 1) if top else (1, 2)
    inset = 0.0 if top else 40 / 400                     # the bottom screen is 320 wide, centred under the 400 of the top one
    def at(row, right, turns):
        l, r = corner_colour(row, 0, turns), corner_colour(row, 1, turns)
        t = 1 - inset if right else inset
        return [l[k] * (1 - t) + r[k] * t for k in range(3)]
    start = [at(rows[c // 2], c % 2, -LEN['A'] / LEN['B']) for c in range(4)]
    s.pic('bg', 'px', 0, 0, s.width, 240, parent=g, corners=[bytes(max(0, min(255, round(v))) for v in col) + b'\xff' for col in start])
    for c in range(4):
        for k, ch in enumerate('rgb'):
            fn = lambda f, c=c, k=k: at(rows[c // 2], c % 2, f / LEN['B'])[k]
            s.anim('A', 'bg', f'c{c}{ch}', wave_keys(lambda f: fn(f - LEN['A']), 0, LEN['A'], 2))     # arriving at B's first frame in step
            s.anim('B', 'bg', f'c{c}{ch}', wave_keys(fn, 0, LEN['B'], 4))
    # the app's own background rosettes, at the places and sizes it draws them (ui/draw_top.cpp, ui/draw_bot.cpp)
    spots = [(220, 12, 14, 1), (394, 236, 44, -1), (2, 158, 18, 1)] if top else [(284, 72, 58, -1), (12, 90, 34, 1), (138, 170, 20, 1)]
    for i, (x, y, r, turn) in enumerate(spots):
        n = f'ros{i}'
        s.pic(n, 'ros', x - s.width / 2, 120 - y, r * 2, r * 2, parent=g, alpha=28 if top else 22)
        # an eighth of a turn per loop brings the eight-armed shape back onto itself, so the loop has no jump
        per = 45 / LEN['B']
        s.anim('A', n, 'rot', S.linear([(0, -turn * per * LEN['A']), (LEN['A'], 0)]))
        s.anim('B', n, 'rot', S.linear([(0, 0), (LEN['B'], turn * 45)]))
    s.anim('A', 'all', 'alpha', S.linear([(0, 0), (8, 255)]))
    # B does not touch 'all' (it keeps the 255 that A left). Nintendo's own logo is built this way: the pane its
    # fade-out works on is in the groups of A and C only. With a hold in B, the loop's 255 comes back for one frame
    # after C has finished: a full-bright flash before the screens go black.
    # Black is reached five frames before the end and held. The console alternates two picture buffers per screen;
    # with black only on the very last frame the buffer before it still holds a faint logo, which shows once more
    # as the HOME Menu hands over (black, the last frame again, black).
    s.anim('C', 'all', 'alpha', [(0, 255, -255 / (LEN['C'] - 5)), (LEN['C'] - 5, 0, 0), (LEN['C'] - 1, 0, 0)])
    return g


def top_screen(tex):
    s = S.Screen(400)
    g = backdrop(s, True)
    cy = 6                                             # the mark's region sits this far above the middle of the screen
    def centre(x0, y0, t):                             # a texture's centre on the screen, from its top left corner in the region
        return x0 + t.width / 2 - MARK_W / 2, cy - (y0 + t.height / 2 - MARK_H / 2)
    (tt, tx, ty), (tp, px_, py_) = tex['_t'], tex['_p']
    ctx, cty = centre(tx, ty, tt); cpx, cpy = centre(px_, py_, tp)
    # shadows first, under both letters, as in the icon
    for n, t, x, y in (('shP', 'p', cpx, cpy), ('shT', 't', ctx, cty)):
        im = tex[t]
        s.pic(n, t, x + SHADOW[0], y - SHADOW[1], im.width, im.height, colour=COL_SHADOW, parent=g, alpha=0)
    m = s.null('mark', 0, 0, parent=g)
    face = [COL_FACE_TOP, COL_FACE_TOP, COL_FACE_BOTTOM, COL_FACE_BOTTOM]
    for n, t, x, y in (('P', 'p', cpx, cpy), ('T', 't', ctx, cty)):
        im = tex[t]
        grp = s.null('g' + n, x, y, parent=m, alpha=0)                 # each letter grows from its own middle
        s.pic('belt' + n, t, 0, -BELT, im.width, im.height, colour=COL_BELT, parent=grp)
        s.pic('face' + n, t, 0, 0, im.width, im.height, parent=grp, corners=face)
    # A: the T pops in, then the P; shadows and the dividing line follow
    for n, t0 in (('gT', 6), ('gP', 14)):
        for prop in ('sx', 'sy'): s.anim('A', n, prop, [(t0, 0.3), (t0 + 12, 1.1), (t0 + 20, 1.0)])
        s.anim('A', n, 'alpha', S.linear([(t0, 0), (t0 + 5, 255)]))
        for prop in ('sx', 'sy'): s.hold('B', n, prop, 1.0)
        s.hold('B', n, 'alpha', 255)
    for n, t0 in (('shT', 14), ('shP', 22)):
        s.anim('A', n, 'alpha', S.linear([(t0, 0), (t0 + 10, 255)]))
        s.hold('B', n, 'alpha', 255)
    s.hold('A', 'mark', 'y', 0)
    s.anim('B', 'mark', 'y', [(0, 0), (LEN['B'] / 2, 3), (LEN['B'], 0)])     # floating over its shadow
    return s


def bottom_screen(tex):
    s = S.Screen(320)
    g = backdrop(s, False)
    # the name: blue letters, lighter at the top, with a white outline, over the outline's shadow
    n, t = tex['name'], tex['tag']
    s.pic('nameSh', 'nameO', 0, 10 - 2, n.width, n.height, colour=COL_SHADOW, parent=g, alpha=0)
    s.pic('nameO', 'nameO', 0, 10, n.width, n.height, parent=g, alpha=0)
    s.pic('name', 'name', 0, 10, n.width, n.height, corners=[NAME_TOP, NAME_TOP, NAME_BOTTOM, NAME_BOTTOM], parent=g, alpha=0)
    s.pic('tag', 'tag', 0, -16, t.width, t.height, colour='#d6ecff', parent=g, alpha=0)
    for pane, t0, y in (('name', 22, 10), ('nameO', 22, 10), ('nameSh', 22, 8), ('tag', 30, -16)):
        s.anim('A', pane, 'alpha', S.linear([(t0, 0), (t0 + 12, 255 if pane != 'nameSh' else 150)]))
        s.anim('A', pane, 'y', [(t0, y - 6), (t0 + 14, y)])
        s.hold('B', pane, 'alpha', 255 if pane != 'nameSh' else 150)
        s.hold('B', pane, 'y', y)
    return s


def compare_mark(tex, masks, reference):
    """How far three flat layers per letter are from the icon's own drawing of the mark (which sweeps 15 copies)."""
    import numpy as np
    def layer(mask, colour, dx=0.0, dy=0.0, grad=None):
        m = mask if (dx, dy) == (0, 0) else mask.transform(mask.size, Image.AFFINE, (1, 0, -dx, 0, 1, -dy), Image.BILINEAR)
        a = np.asarray(m).astype(float)[..., None] / 255
        c = np.array(list(S.rgb(colour)), float)[None, None, :] * np.ones((MARK_H, MARK_W, 1))
        if grad:
            y0, y1 = mask.getbbox()[1], mask.getbbox()[3]
            t = np.clip((np.arange(MARK_H) - y0) / max(1, y1 - y0), 0, 1)[:, None, None]
            c = np.array(list(S.rgb(grad[0])), float) * (1 - t) + np.array(list(S.rgb(grad[1])), float) * t
            c = c * np.ones((MARK_H, MARK_W, 1))
        return c, a
    out = np.zeros((MARK_H, MARK_W, 3)); cov = np.zeros((MARK_H, MARK_W, 1))
    order = [layer(masks['p'], COL_SHADOW, *SHADOW), layer(masks['t'], COL_SHADOW, *SHADOW), layer(masks['p'], COL_BELT, 0, BELT),
             layer(masks['p'], '#ffffff', grad=(COL_FACE_TOP, COL_FACE_BOTTOM)), layer(masks['t'], COL_BELT, 0, BELT),
             layer(masks['t'], '#ffffff', grad=(COL_FACE_TOP, COL_FACE_BOTTOM))]
    for c, a in order:
        out = out * (1 - a) + c * a; cov = cov + a * (1 - cov)
    ref = np.asarray(reference).astype(float); ra = ref[..., 3:] / 255
    bgc = np.array([0x23, 0x8d, 0xd8], float)
    ours = out + bgc * (1 - cov); theirs = ref[..., :3] * ra + bgc * (1 - ra)
    diff = np.abs(ours - theirs).max(axis=2)
    side = Image.new('RGB', (MARK_W * 3 + 16, MARK_H), (40, 40, 40))
    side.paste(Image.fromarray(theirs.astype('uint8')), (0, 0)); side.paste(Image.fromarray(ours.astype('uint8')), (MARK_W + 8, 0))
    side.paste(Image.fromarray(np.clip(diff * 4, 0, 255).astype('uint8')).convert('RGB'), (2 * MARK_W + 16, 0))
    side.resize((side.width * 3, side.height * 3), Image.NEAREST).save(os.path.join(OUT, 'design_mark_compare.png'))
    return float(diff.mean()), int((diff > 24).sum())


def main():
    os.makedirs(OUT, exist_ok=True)
    masks, reference = mark_layers()
    tex = {'px': Image.new('L', (8, 8), 255), 'ros': rosette_mask(64)}
    for k in ('t', 'p'):
        tex['_' + k] = tight(masks[k]); tex[k] = tex['_' + k][0]
    tex['name'], name_w = text_mask('Theme Plaza', 'MPLUS1p-ExtraBold.ttf', 24, 256, 32)
    tex['nameO'], _ = text_mask('Theme Plaza', 'MPLUS1p-ExtraBold.ttf', 24, 256, 32, outline=NAME_OUTLINE)
    tex['tag'], tag_w = text_mask('Customize your 3DS!', 'MPLUS1p-Medium.ttf', 12, 128, 16)
    mean, far = compare_mark(tex, masks, reference)
    print(f'mark: {MARK_W} x {MARK_H} px; textures t {tex["t"].size}, p {tex["p"].size}; '
          f'against the icon\'s own drawing: mean difference {mean:.2f} of 255, {far} pixels off by more than 24')
    textures = {k: v for k, v in tex.items() if not k.startswith('_')}
    files = S.build(top_screen(tex), bottom_screen(tex), textures, LEN)
    for folder, name, data in files:
        if folder == 'timg': print(f'  {name:14s} {len(data) - 0x28:6d} bytes raw, {len(B.L.lz11_compress(data)):5d} packed alone')
    data, size = B.finish(files)
    print(f'packed: {size} bytes of {B.LIMIT} ({size * 100 // B.LIMIT} %)')
    if data is None: sys.exit('over the limit: nothing written')
    path = os.path.join(OUT, 'design.bcma.lz')
    open(path, 'wb').write(data)
    fr = P.frames(P.load(path), 1)
    pick = [fr[i] for i in (0, 6, 10, 14, 18, 22, 26, 30, 36, 44, 48 + 30, 48 + 60, 48 + 90, 48 + 120 + 5, 48 + 120 + 12)]
    sheet = Image.new('RGB', (5 * 404, 3 * 484), (255, 0, 255))
    for k, (label, im) in enumerate(pick): sheet.paste(im, ((k % 5) * 404, (k // 5) * 484))
    sheet.save(os.path.join(OUT, 'design_sheet.png'))
    ims = [im for _, im in P.frames(P.load(path), 2)[::2]]
    ims[0].save(os.path.join(OUT, 'design.gif'), save_all=True, append_images=ims[1:], duration=33, loop=0)
    print(f'preview: {os.path.join(OUT, "design_sheet.png")} and design.gif')
    if len(sys.argv) > 1 and sys.argv[1] == 'install':
        open(os.path.join(ROOT, 'app', 'meta', 'logo.bcma.lz'), 'wb').write(data); print('copied to app/meta/logo.bcma.lz')


if __name__ == '__main__':
    main()
