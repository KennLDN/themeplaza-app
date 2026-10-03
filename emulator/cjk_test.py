#!/usr/bin/env python3
"""Test themes for text the console's standard system font or the app's line wrapping could get wrong:
copies of one small theme with names in Korean, Traditional and Simplified Chinese (taken from the file
name), and with long descriptions in Japanese and Chinese that have no spaces to wrap at.

  cjk_test.py add       writes them to /Themes on the emulated SD card
  cjk_test.py remove    deletes them again (clean_sd.py does the same)

Needs tools/hosttest/out/spread/ (tools/hosttest/spread.py) for the theme they are copies of.
"""
import glob, os, sys, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
THEMES = os.path.expanduser('~/.var/app/org.azahar_emu.Azahar/data/azahar-emu/sdmc/Themes')
BY_FILE_NAME = ['한국어 테마 시험 by 테스트.zip', '繁體中文測試 鬱龜 by 測試.zip', '简体中文 乐园 by 测试.zip']
BY_SMDH = {
    'cjk wrap test ja.zip': ('折り返しテスト', 'テスト', '夕暮れの空を背景にした、凧と雲のテーマです。下画面には小さな星が流れ、フォルダーの色も合わせてあります。音楽は静かなピアノ曲。'),
    'cjk wrap test zh.zip': ('换行测试', '测试', '这是一个以傍晚天空为背景的主题，上屏是风筝和云，下屏有流动的小星星，文件夹的颜色也做了搭配。音乐是一首安静的钢琴曲！'),
    'cjk wrap test mixed.zip': ('Mixed wrap test', 'tester', 'Night Kites テーマ: 夕暮れの空と凧。A calm piano loop plays, 文件夹的颜色也做了搭配 and the rest is plain English text.'),
}
NAMES = BY_FILE_NAME + list(BY_SMDH)


def smdh_with(smdh, name, author, desc):
    """The SMDH with its short title, long title and publisher replaced in all 16 languages."""
    out = bytearray(smdh)
    def put(off, text, units):
        raw = text.encode('utf-16-le')[:(units - 1) * 2]
        out[off:off + units * 2] = raw + bytes(units * 2 - len(raw))
    for lang in range(16):
        base = 8 + lang * 0x200
        put(base, name, 0x40); put(base + 0x80, desc, 0x80); put(base + 0x180, author, 0x40)
    return bytes(out)


def main():
    what = sys.argv[1] if len(sys.argv) > 1 else ''
    if what == 'remove':
        for n in NAMES:
            p = os.path.join(THEMES, n)
            if os.path.exists(p): os.unlink(p); print('removed', n)
        return
    if what != 'add': sys.exit(__doc__)
    src = glob.glob(os.path.join(ROOT, 'tools', 'hosttest', 'out', 'spread', '*(48343).zip'))
    if not src: sys.exit('run tools/hosttest/spread.py first')
    with zipfile.ZipFile(src[0]) as a:
        entries = [(i, a.read(i)) for i in a.infolist()]
    for n in NAMES:
        with zipfile.ZipFile(os.path.join(THEMES, n), 'w', zipfile.ZIP_DEFLATED) as b:
            for i, data in entries:
                if i.filename == 'info.smdh':
                    if n in BY_FILE_NAME: continue          # no SMDH: the app takes the name from the file name
                    data = smdh_with(data, *BY_SMDH[n])
                b.writestr(i.filename, data)
        print('wrote', n)


if __name__ == '__main__':
    main()
