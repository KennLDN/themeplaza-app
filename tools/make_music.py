#!/usr/bin/env python3
"""The app's music and the banner's sound, written here as notes and rendered by tools/music_engine.py.

  tools/.venv/bin/python tools/make_music.py [NAME...]     default: everything
      -> app/build/music/NAME.wav   to listen to (stereo, 32 kHz)
  tools/.venv/bin/python tools/make_music.py install       renders everything and puts it where it is used:
      app/romfs/audio/track0..2.bcstm   the app's loops (tide, shop, hours), as the app streams them
      app/meta/audio.wav                the banner's sound

Three loops for the app, in the manner of the 3DS and Wii U system music: electric piano, vibraphone,
soft bass and light percussion in a room, rather than the bare square and triangle waves they replace.
  tide     "Tide Pool"     calm and open, the default
  shop     "Shop Window"   a light bossa
  hours    "After Hours"   slow, for the night
  jingle   the banner's sound: under 3 seconds, played when the app's icon is selected on the HOME Menu
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from music_engine import *

OUT = os.path.join(ROOT, 'app', 'build', 'music')
at = lambda bar, beat=1.0: (bar - 1) * 4 + (beat - 1)          # bars and beats count from 1


def melody(part, bars, velocity=78, first_bar=1, transpose=0):
    """bars: for each bar a list of (beat, length, note[, velocity change])."""
    for i, notes in enumerate(bars):
        for n in notes:
            part.note(at(first_bar + i, n[0]), n[1], pitch(n[2]) + transpose, velocity + (n[3] if len(n) > 3 else 0))


def tide():
    s = Song(bpm=84, beats=64, seed=11, reverb_seconds=2.2)
    CH = {'Dmaj9': ['F#3', 'A3', 'C#4', 'E4'], 'Bm9': ['A3', 'C#4', 'D4', 'F#4'], 'Gmaj9': ['F#3', 'A3', 'B3', 'D4'], 'Em9': ['G3', 'B3', 'D4', 'F#4'],
          'A13': ['G3', 'B3', 'C#4', 'F#4'], 'A13sus': ['G3', 'B3', 'D4', 'F#4'], 'F#m7': ['A3', 'C#4', 'E4'], 'D/F#': ['A3', 'C#4', 'E4', 'F#4']}
    ROOT_ = {'Dmaj9': 'D2', 'Bm9': 'B1', 'Gmaj9': 'G1', 'Em9': 'E2', 'A13': 'A1', 'A13sus': 'A1', 'F#m7': 'F#2', 'D/F#': 'F#2'}
    PAD = {'Dmaj9': ['D3', 'A3', 'E4'], 'Bm9': ['B2', 'F#3', 'D4'], 'Gmaj9': ['G2', 'D3', 'B3'], 'Em9': ['E3', 'B3', 'F#4'], 'A13': ['A2', 'G3', 'C#4'],
           'A13sus': ['A2', 'G3', 'D4'], 'F#m7': ['F#2', 'E3', 'A3'], 'D/F#': ['F#2', 'D3', 'A3']}
    bars = ['Dmaj9', 'Dmaj9', 'Bm9', 'Bm9', 'Gmaj9', 'Gmaj9', 'Em9', 'A13',
            'F#m7', 'Bm9', 'Gmaj9', 'A13sus', 'D/F#', 'Gmaj9', 'Em9', 'A13sus']
    ep = s.part('Rhodes EP', gain=-9, pan=-0.18, reverb=0.30, chorus=0.5, lowpass=7000, highpass=140)
    pad = s.part('Warm Pad', gain=-21, reverb=0.45, lowpass=3600, highpass=260, width=1.4)
    bass = s.part('Fretless Bass', gain=-15, reverb=0.05, lowpass=2400, highpass=55, humanize=0.004)
    vib = s.part('Vibraphone', gain=3, pan=0.2, reverb=0.42, echo=0.22, echo_beats=1.5, echo_feedback=0.4)
    cel = s.part('Celesta', gain=-7, pan=-0.45, reverb=0.5, echo=0.35, echo_beats=0.75)
    harp = s.part('Harp', gain=-8, pan=0.45, reverb=0.45)
    kit = s.part('Standard', gain=-9, reverb=0.22, highpass=150)
    for i, name in enumerate(bars):
        bar = i + 1
        ep.chord(at(bar, 1), 2.6, CH[name], 62, strum=0.03, accent_top=6)
        ep.chord(at(bar, 3.5), 1.4, CH[name][1:], 48, strum=0.02)
        pad.chord(at(bar, 1), 4.05, PAD[name], 58)
        bass.note(at(bar, 1), 2.4, ROOT_[name], 84)
        nxt = ROOT_[bars[(i + 1) % 16]]
        bass.note(at(bar, 3.5), 0.9, pitch(ROOT_[name]) + 7 if pitch(ROOT_[name]) < 40 else pitch(ROOT_[name]) - 5, 66)
        if bar % 4 == 0: bass.note(at(bar, 4.5), 0.5, pitch(nxt) - 2 if pitch(nxt) > 33 else pitch(nxt) + 2, 60)
        # percussion: a shaker in eighths, a soft kick, a stick on the third beat
        for e in range(8):
            kit.note(at(bar, 1 + e / 2), 0.2, 82, (46 if e % 2 == 0 else 30) + (6 if e == 4 else 0))
        kit.note(at(bar, 1), 0.3, 36, 60); kit.note(at(bar, 2.5), 0.3, 36, 40)
        kit.note(at(bar, 3), 0.3, 37, 46)
        if bar % 8 == 0: kit.note(at(bar, 4.5), 0.5, 81, 34)
    tune = [
        [(1, 1.5, 'F#5'), (2.5, 0.5, 'A5'), (3, 2, 'E5')],
        [(2, 1, 'D5'), (3, 0.5, 'C#5'), (3.5, 1.5, 'A4')],
        [(1, 1.5, 'D5'), (2.5, 0.5, 'F#5'), (3, 2, 'C#5')],
        [(2, 1, 'B4'), (3, 0.5, 'A4'), (3.5, 1.5, 'F#4')],
        [(1, 1, 'B4'), (2, 1, 'D5'), (3, 1.5, 'A5'), (4.5, 0.5, 'F#5')],
        [(1, 3, 'E5'), (4, 1, 'D5')],
        [(1, 1.5, 'G5'), (2.5, 0.5, 'F#5'), (3, 1, 'E5'), (4, 1, 'D5')],
        [(1, 2, 'C#5'), (3, 1, 'E5'), (4, 1, 'A4')],
        [(1, 1.5, 'A5'), (2.5, 0.5, 'F#5'), (3, 2, 'E5')],
        [(1, 1, 'D5'), (2, 1, 'C#5'), (3, 2, 'B4')],
        [(1, 0.5, 'B4'), (1.5, 0.5, 'D5'), (2, 1.5, 'G5'), (3.5, 0.5, 'F#5'), (4, 1, 'D5')],
        [(1, 3, 'E5'), (4, 0.5, 'F#5'), (4.5, 0.5, 'A5')],
        [(1, 1.5, 'B5'), (2.5, 0.5, 'A5'), (3, 2, 'F#5')],
        [(1, 1, 'G5'), (2, 1, 'F#5'), (3, 2, 'D5')],
        [(1, 1.5, 'E5'), (2.5, 0.5, 'D5'), (3, 1, 'B4'), (4, 1, 'D5')],
        [(1, 3, 'C#5')],
    ]
    melody(vib, tune, 76)
    # answers in the gaps: a few high notes of the chord, rising
    for bar, notes in ((2, ['A5', 'C#6', 'E6']), (4, ['F#5', 'A5', 'D6']), (6, ['B5', 'D6', 'F#6']), (10, ['F#5', 'A5', 'C#6']),
                       (14, ['B5', 'D6', 'G6']), (16, ['E5', 'G5', 'B5', 'D6'])):
        for k, n in enumerate(notes): cel.note(at(bar, 1) + (0 if bar != 16 else 2) + k * 0.5 + (0.5 if bar in (6, 14) else 0), 1.2, n, 56 + 4 * k)
    for bar in (8, 12, 16):
        up = [pitch(n) + 12 for n in CH[bars[bar - 1]]] + [pitch(CH[bars[bar - 1]][0]) + 24]
        for k, n in enumerate(up): harp.note(at(bar, 3) + k * 0.25, 1.5, n, 50 + 3 * k)
    return master(s.render(), air=3.0)


def shop():
    s = Song(bpm=116, beats=128, swing=0.08, seed=23, reverb_seconds=1.5, reverb_level=0.9)
    CH = {'Fmaj9': ['A3', 'C4', 'E4', 'G4'], 'Gm9': ['Bb3', 'D4', 'F4', 'A4'], 'C13': ['Bb3', 'D4', 'E4', 'A4'], 'Am7': ['G3', 'A3', 'C4', 'E4'],
          'Dm9': ['F3', 'A3', 'C4', 'E4'], 'Bbmaj9': ['A3', 'C4', 'D4', 'F4'], 'Bbm6': ['Bb3', 'Db4', 'F4', 'G4'], 'F69': ['A3', 'C4', 'D4', 'G4']}
    ROOT_ = {'Fmaj9': ('F2', 'C3'), 'Gm9': ('G2', 'D3'), 'C13': ('C2', 'G2'), 'Am7': ('A2', 'E3'), 'Dm9': ('D2', 'A2'), 'Bbmaj9': ('Bb1', 'F2'),
             'Bbm6': ('Bb1', 'F2'), 'F69': ('F2', 'C3')}
    A = [['Fmaj9'], ['Fmaj9'], ['Gm9'], ['C13'], ['Am7'], ['Dm9'], ['Gm9'], ['C13']]
    A2 = [['Fmaj9'], ['Fmaj9'], ['Gm9'], ['C13'], ['Am7'], ['Dm9'], ['Gm9', 'C13'], ['F69']]
    B = [['Bbmaj9'], ['Bbmaj9'], ['Am7'], ['Dm9'], ['Bbmaj9'], ['Bbm6'], ['Am7', 'Dm9'], ['Gm9', 'C13']]
    bars = A + A2 + B + A2
    gtr = s.part('Nylon String Guitar', gain=-12, pan=-0.3, reverb=0.2, highpass=140)
    ep = s.part('Rhodes EP', gain=-14, pan=0.28, reverb=0.25, chorus=0.45, lowpass=6000, highpass=140)
    bass = s.part('Acoustic Bass', gain=-18, reverb=0.04, lowpass=2600, highpass=55, humanize=0.004)
    vib = s.part('Vibraphone', gain=2, pan=0.12, reverb=0.35, echo=0.12, echo_beats=1.5)
    flute = s.part('Flute', gain=-12, pan=-0.12, reverb=0.4, lowpass=7000)
    pizz = s.part('Pizzicato Section', gain=-9, pan=0.4, reverb=0.4)
    kit = s.part('Brush', gain=-8, reverb=0.18, highpass=90)
    for i, chords in enumerate(bars):
        bar = i + 1
        section = i // 8                                  # 0 A, 1 A', 2 B, 3 A''
        first, last = chords[0], chords[-1]
        # the guitar's bossa comping, a two-bar figure: 1, 2&, 4 | 1&, 3
        hits = ((1, 0.9, first), (2.5, 1.2, first), (4, 0.8, last)) if bar % 2 else ((1.5, 1.2, first), (3, 1.4, last))
        for beat, length, name in hits:
            gtr.chord(at(bar, beat), length, CH[name], 60, strum=0.018, accent_top=5)
        if section in (1, 3) or (section == 2 and bar % 2 == 1):
            ep.chord(at(bar, 1), 3.2, CH[first], 46, strum=0.03)
            if last != first: ep.chord(at(bar, 3), 1.8, CH[last], 44, strum=0.03)
        # the bass: root, fifth just before the third beat, root, fifth just before the next bar
        r1, f1 = ROOT_[first]; r2, f2 = ROOT_[last]
        bass.note(at(bar, 1), 1.4, r1, 88); bass.note(at(bar, 2.5), 0.5, f1, 68)
        bass.note(at(bar, 3), 1.4, r2, 82); bass.note(at(bar, 4.5), 0.5, f2, 66)
        # brushes: a soft kick with the bass, the stick's bossa figure, the hat in eighths
        kit.note(at(bar, 1), 0.3, 36, 58); kit.note(at(bar, 2.5), 0.3, 36, 38); kit.note(at(bar, 3), 0.3, 36, 52); kit.note(at(bar, 4.5), 0.3, 36, 36)
        for beat in ((1, 2.5, 4) if bar % 2 else (2, 3.5)): kit.note(at(bar, beat), 0.3, 37, 54)
        for e in range(8): kit.note(at(bar, 1 + e / 2), 0.2, 42, (38 if e % 2 == 0 else 26))
        for e in range(16): kit.note(at(bar, 1 + e / 4), 0.1, 82, 22 if e % 2 else 30)
        if bar % 8 == 0: kit.note(at(bar, 4), 0.5, 40, 44); kit.note(at(bar, 4.5), 0.5, 40, 50)
    a_tune = [
        [(1, 1, 'A4'), (2, 0.5, 'C5'), (2.5, 1.5, 'E5'), (4, 0.5, 'D5'), (4.5, 1.4, 'C5')],
        [(2, 0.5, 'A4'), (2.5, 0.5, 'C5'), (3, 1.5, 'D5'), (4.5, 0.5, 'C5')],
        [(1, 1, 'Bb4'), (2, 0.5, 'D5'), (2.5, 1.5, 'F5'), (4, 0.5, 'E5'), (4.5, 1.4, 'D5')],
        [(2, 0.5, 'Bb4'), (2.5, 0.5, 'D5'), (3, 1, 'E5'), (4, 1, 'G5')],
        [(1, 1.5, 'E5'), (2.5, 0.5, 'C5'), (3, 1, 'A4'), (4, 0.5, 'C5'), (4.5, 1, 'E5')],
        [(1, 1.5, 'F5'), (2.5, 0.5, 'E5'), (3, 2, 'D5')],
        [(1, 0.5, 'Bb4'), (1.5, 0.5, 'C5'), (2, 0.5, 'D5'), (2.5, 1.5, 'F5'), (4, 1, 'A5')],
        [(1, 2, 'G5'), (3, 0.5, 'E5'), (3.5, 0.5, 'D5'), (4, 1, 'C5')],
    ]
    a_end = a_tune[:6] + [[(1, 1, 'D5'), (2, 1, 'F5'), (3, 1, 'E5'), (4, 1, 'G5')], [(1, 3, 'F5')]]
    b_tune = [
        [(1, 1.5, 'D5'), (2.5, 0.5, 'F5'), (3, 2, 'A5')],
        [(1, 1, 'G5'), (2, 1, 'F5'), (3, 2, 'D5')],
        [(1, 1.5, 'C5'), (2.5, 0.5, 'E5'), (3, 2, 'G5')],
        [(1, 1, 'F5'), (2, 1, 'E5'), (3, 2, 'A4')],
        [(1, 1.5, 'D5'), (2.5, 0.5, 'F5'), (3, 2, 'A5')],
        [(1, 1, 'G5'), (2, 1, 'F5'), (3, 2, 'Db5')],
        [(1, 1, 'E5'), (2, 1, 'C5'), (3, 1, 'F5'), (4, 1, 'D5')],
        [(1, 1.5, 'Bb4'), (2.5, 0.5, 'D5'), (3, 1, 'E5'), (4, 1, 'G5')],
    ]
    melody(vib, a_tune, 80, 1); melody(vib, a_end, 80, 9)
    melody(flute, b_tune, 74, 17, transpose=12)
    melody(vib, a_end, 80, 25); melody(flute, a_end, 62, 25, transpose=12)
    # plucked strings mark the turns and answer the tune
    for bar in (8, 16, 24, 32):
        for k, n in enumerate(['C5', 'E5', 'G5', 'Bb5'] if bar != 16 and bar != 32 else ['A4', 'C5', 'D5', 'G5']):
            pizz.note(at(bar, 3) + k * 0.5, 0.4, n, 60 + 4 * k)
    for bar in (2, 6, 10, 14, 26, 30):
        pizz.note(at(bar, 1), 0.4, CH[bars[bar - 1][0]][3], 54); pizz.note(at(bar, 1.5), 0.4, pitch(CH[bars[bar - 1][0]][2]) + 12, 50)
    return master(s.render(), air=3.0)


def hours():
    s = Song(bpm=66, beats=64, seed=31, reverb_seconds=2.8, reverb_level=1.15)
    CH = {'Dbmaj9': ['F3', 'Ab3', 'C4', 'Eb4'], 'Bbm9': ['Ab3', 'C4', 'Db4', 'F4'], 'Gbmaj9': ['F3', 'Ab3', 'Bb3', 'Db4'], 'Ebm9': ['Gb3', 'Bb3', 'Db4', 'F4'],
          'Ab13sus': ['Gb3', 'Bb3', 'Db4', 'F4'], 'Fm7': ['Eb3', 'Ab3', 'C4']}
    ROOT_ = {'Dbmaj9': 'Db2', 'Bbm9': 'Bb1', 'Gbmaj9': 'Gb1', 'Ebm9': 'Eb2', 'Ab13sus': 'Ab1', 'Fm7': 'F2'}
    PAD = {'Dbmaj9': ['Db3', 'Ab3', 'Eb4', 'F4'], 'Bbm9': ['Bb2', 'F3', 'C4', 'Db4'], 'Gbmaj9': ['Gb2', 'Db3', 'Bb3', 'F4'], 'Ebm9': ['Eb3', 'Bb3', 'Db4', 'F4'],
           'Ab13sus': ['Ab2', 'Gb3', 'Bb3', 'Db4'], 'Fm7': ['F2', 'C3', 'Ab3', 'Eb4']}
    bars = ['Dbmaj9', 'Dbmaj9', 'Bbm9', 'Bbm9', 'Gbmaj9', 'Gbmaj9', 'Ebm9', 'Ab13sus',
            'Dbmaj9', 'Fm7', 'Bbm9', 'Bbm9', 'Gbmaj9', 'Gbmaj9', 'Ebm9', 'Ab13sus']
    pad = s.part('Warm Pad', gain=-15, reverb=0.5, lowpass=3200, highpass=240, width=1.5)
    halo = s.part('Halo Pad', gain=-22, reverb=0.6, lowpass=5200, highpass=400, width=1.5)
    ep = s.part('Rhodes EP', gain=-10, pan=-0.2, reverb=0.4, chorus=0.6, lowpass=5600, highpass=140)
    bass = s.part('Fretless Bass', gain=-16, reverb=0.08, lowpass=2000, highpass=55)
    box = s.part('Music Box', gain=-4, pan=0.25, reverb=0.5, echo=0.4, echo_beats=0.75, echo_feedback=0.45)
    cel = s.part('Celesta', gain=-10, pan=-0.4, reverb=0.55, echo=0.3, echo_beats=1.5)
    for i, name in enumerate(bars):
        bar = i + 1
        pad.chord(at(bar, 1), 4.1, PAD[name], 54)
        if bar % 2 == 1: halo.chord(at(bar, 1), 8.1, [pitch(n) + 12 for n in PAD[name][1:3]], 44)
        # the electric piano spells each chord slowly, bottom to top, and lets it ring
        for k, n in enumerate(CH[name]): ep.note(at(bar, 1) + k * 0.5, 3.6 - k * 0.5, n, 58 - 3 * k)
        if bar % 2 == 0: ep.note(at(bar, 3.5), 1.2, pitch(CH[name][-1]) + 12, 44)
        bass.note(at(bar, 1), 3.2, ROOT_[name], 80)
        if bar % 4 == 0: bass.note(at(bar, 4), 0.9, pitch(ROOT_[name]) + 7, 58)
    tune = [
        [(1, 2, 'Ab5'), (3, 1, 'F5'), (4, 1, 'Eb5')],
        [(1, 3, 'F5'), (4, 1, 'Db5')],
        [(1, 2, 'Eb5'), (3, 2, 'C5')],
        [(2, 1, 'Db5'), (3, 2, 'F5')],
        [(1, 2, 'Bb5'), (3, 1, 'Ab5'), (4, 1, 'F5')],
        [(1, 4, 'Ab5')],
        [(1, 1.5, 'Gb5'), (2.5, 0.5, 'F5'), (3, 2, 'Db5')],
        [(1, 3, 'Eb5'), (4, 1, 'F5')],
        [(1, 2, 'Ab5'), (3, 1, 'C6'), (4, 1, 'Bb5')],
        [(1, 3, 'Ab5'), (4, 1, 'F5')],
        [(1, 2, 'Eb5'), (3, 1, 'F5'), (4, 1, 'Ab5')],
        [(1, 4, 'F5')],
        [(1, 2, 'Db6'), (3, 1, 'Bb5'), (4, 1, 'Ab5')],
        [(1, 2, 'F5'), (3, 2, 'Bb5')],
        [(1, 1.5, 'Gb5'), (2.5, 0.5, 'F5'), (3, 2, 'Eb5')],
        [(1, 4, 'Eb5')],
    ]
    melody(box, tune, 70)
    for bar, notes in ((4, ['F5', 'Ab5', 'C6']), (6, ['Db6', 'F6']), (12, ['Ab5', 'C6', 'Eb6']), (16, ['Bb5', 'Db6', 'F6', 'Ab6'])):
        for k, n in enumerate(notes): cel.note(at(bar, 1) + (2 if bar == 6 else 0) + k * 0.75, 2, n, 48 + 4 * k)
    return master(s.render(), level=0.027, air=2.0)


def jingle():
    """Under 3 seconds: a soft chord swells, a harp runs up it, and two bright notes answer (the same rising
    step the app's first tune opens with), ringing out."""
    s = Song(bpm=120, beats=4, seed=5, reverb_seconds=1.6, reverb_level=1.1)
    pad = s.part('Warm Pad', gain=-13, reverb=0.5, lowpass=3200, highpass=150, width=1.4)
    harp = s.part('Harp', gain=-6, pan=-0.2, reverb=0.45)
    mar = s.part('Marimba', gain=-5, pan=0.15, reverb=0.35)
    vib = s.part('Vibraphone', gain=-1, pan=0.1, reverb=0.45)
    cel = s.part('Celesta', gain=-6, pan=-0.1, reverb=0.5, echo=0.25, echo_beats=0.5)
    bass = s.part('Fretless Bass', gain=-15, reverb=0.1, lowpass=2000, highpass=55)
    pad.chord(0, 4.2, ['D3', 'A3', 'E4', 'F#4'], 62)
    bass.note(0, 3.5, 'D2', 76)
    for k, n in enumerate(['D4', 'F#4', 'A4', 'C#5', 'E5', 'A5']): harp.note(0.04 + k * 0.125, 2.0, n, 62 + 5 * k)
    for k, n in enumerate(['A4', 'D5']): mar.note(0.04 + k * 0.5, 0.5, n, 70)
    vib.note(0.95, 1.2, 'F#5', 86); vib.note(1.45, 3.0, 'A5', 92); vib.note(1.45, 3.0, 'D5', 70)
    cel.note(1.95, 2.5, 'D6', 70); cel.note(2.2, 2.5, 'F#6', 62); cel.note(2.45, 2.5, 'A6', 58)
    x = s.render(loop=False, tail=0.92)
    x = master(x[:int(2.92 * RATE)], level=0.06, peak=0.8, air=2.0)
    return fade(x, 0.004, 0.9)


TRACKS = {'tide': tide, 'shop': shop, 'hours': hours, 'jingle': jingle}


def main():
    install = sys.argv[1:] == ['install']
    names = list(TRACKS) if install or not sys.argv[1:] else sys.argv[1:]
    for name in names:
        x = TRACKS[name]()
        wav = os.path.join(OUT, name + '.wav')
        write_wav(wav, x)
        report(name, x)
        if not install: continue
        import shutil
        import music_encode
        if name == 'jingle':
            shutil.copyfile(wav, os.path.join(ROOT, 'app', 'meta', 'audio.wav'))
            print('    -> app/meta/audio.wav')
        else:
            out = os.path.join(ROOT, 'app', 'romfs', 'audio', f'track{list(TRACKS).index(name)}.bcstm')
            snr, size = music_encode.write_bcstm(out, to_pcm(x), RATE)
            dec, _ = music_encode.decode_with_app(out)
            same = len(dec) == len(x)
            print(f'    -> {os.path.relpath(out, ROOT)} ({size} bytes, {min(snr):.0f} dB above the coding noise; the app\'s decoder reads '
                  f'{len(dec)} frames{"" if same else " (EXPECTED " + str(len(x)) + ")"})')


if __name__ == '__main__':
    main()
