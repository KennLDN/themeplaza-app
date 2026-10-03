#!/usr/bin/env python3
"""Renders written music to sound: parts of notes played on sampled instruments, mixed in stereo with
room reverb, echo and chorus, and looped without a seam.

The instruments are the FluidR3 GM SoundFont (MIT licence; Fedora package fluid-soundfont-gm), played by
the tinysoundfont module. Both live outside the app: only the rendered sound is shipped.
Run with tools/.venv/bin/python (python -m venv tools/.venv; pip install --no-deps tinysoundfont; pip install numpy scipy).

  song = Song(bpm=84, beats=64, swing=0.0)
  ep = song.part('Rhodes EP', gain=-6, pan=-0.2, reverb=0.25)
  ep.note(beat, length_in_beats, 'F#3', velocity)        # or a MIDI number
  left_right = song.render()                             # float array, RATE samples a second, one loop long
"""
import math, os, wave
import numpy as np
from scipy import signal

RATE = 32000
SOUNDFONT = '/usr/share/soundfonts/FluidR3_GM.sf2'
NAMES = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
_synth = None


def pitch(p):
    """'C4' is 60, 'F#3' 54, 'Bb2' 46; numbers pass through."""
    if isinstance(p, (int, float)): return int(p)
    n = NAMES[p[0]]; rest = p[1:]
    while rest[0] in '#b': n += 1 if rest[0] == '#' else -1; rest = rest[1:]
    return n + 12 * (int(rest) + 1)


def synth():
    global _synth
    if _synth is None:
        import tinysoundfont
        s = tinysoundfont.Synth(samplerate=RATE, gain=0)
        _synth = (s, s.sfload(SOUNDFONT, max_voices=256))
    return _synth


def program(name):
    """(bank, preset, is a drum kit) of an instrument by its name in the SoundFont."""
    s, sf = synth()
    for bank in (0, 128):
        for p in range(128):
            try:
                if s.sfpreset_name(sf, bank, p) == name: return bank, p, bank == 128
            except Exception: pass
    raise ValueError(f'no instrument called {name!r}')


class Part:
    def __init__(self, song, name, gain=0.0, pan=0.0, reverb=0.2, echo=0.0, echo_beats=0.75, echo_feedback=0.35, chorus=0.0,
                 lowpass=None, highpass=None, humanize=0.006, width=1.0):
        self.song, self.name, self.notes = song, name, []
        self.gain, self.pan, self.reverb, self.echo, self.echo_beats, self.echo_feedback = gain, pan, reverb, echo, echo_beats, echo_feedback
        self.chorus, self.lowpass, self.highpass, self.humanize, self.width = chorus, lowpass, highpass, humanize, width

    def note(self, beat, length, p, velocity=80):
        self.notes.append((float(beat), float(length), pitch(p), int(max(1, min(127, velocity)))))
        return self

    def chord(self, beat, length, pitches, velocity=70, strum=0.0, accent_top=0):
        """Several notes together; strum: beats between one note's start and the next, from the bottom up."""
        for i, p in enumerate(pitches):
            self.note(beat + i * strum, length - i * strum, p, velocity + (accent_top if i == len(pitches) - 1 else 0))
        return self

    def dry(self, seconds, rng, passes=2):
        """The part alone, as the instrument plays it: the notes `passes` times over, and a tail."""
        s, sf = synth()
        bank, preset, drums = program(self.name)
        s.sounds_off(0) if hasattr(s, 'sounds_off') else None
        s.program_select(0, sf, bank, preset, drums)
        song = self.song
        # each note is a little early or late and a little louder or softer, the same on every pass,
        # and the passes are a whole number of samples apart: the loop then joins exactly
        n_loop = int(round(song.seconds * RATE))
        events = []
        for beat, length, p, vel in self.notes:
            t = max(0.0, song.time(beat) + (rng.normal(0, self.humanize) if self.humanize else 0))
            v = int(max(1, min(127, vel + rng.normal(0, 3))))
            a = int(t * RATE); b = a + int(max(0.03, length * 60 / song.bpm) * RATE)
            for rep in range(passes):
                events.append((a + rep * n_loop, 1, p, v)); events.append((b + rep * n_loop, 0, p, 0))
        events.sort(key=lambda e: (e[0], e[1]))
        total = int(seconds * RATE)
        out = np.zeros((total, 2), dtype=np.float32)
        pos = 0
        for t, on, p, v in events:
            at = min(total, t)
            if at > pos:
                out[pos:at] = np.frombuffer(bytes(s.generate(at - pos)), dtype=np.float32).reshape(-1, 2); pos = at
            if on: s.noteon(0, p, v)
            else: s.noteoff(0, p)
        if pos < total: out[pos:] = np.frombuffer(bytes(s.generate(total - pos)), dtype=np.float32).reshape(-1, 2)
        s.notes_off(0) if hasattr(s, 'notes_off') else None
        s.generate(RATE // 2)           # let it die away before the next part
        return out.astype(np.float64)


def _filter(x, kind, hz, order=2):
    sos = signal.butter(order, hz / (RATE / 2), kind, output='sos')
    return signal.sosfilt(sos, x, axis=0)


def _chorus(x, depth, period):
    """Two slowly swept short delays, one per side: thickens electric pianos and pads. The sweeps fit a
    whole number of times into `period` seconds (the loop), so the sound is the same at both ends."""
    n = len(x); t = np.arange(n) / RATE
    out = x.copy()
    for ch, (rate, phase) in enumerate(((0.37, 0.0), (0.31, 1.7))):
        rate = max(1, round(rate * period)) / period
        delay = (0.012 + 0.004 * np.sin(2 * math.pi * rate * t + phase)) * RATE
        idx = np.arange(n) - delay
        i0 = np.clip(np.floor(idx).astype(int), 0, n - 1); frac = idx - np.floor(idx)
        i1 = np.clip(i0 + 1, 0, n - 1)
        wet = x[i0, ch] * (1 - frac) + x[i1, ch] * frac
        out[:, ch] = x[:, ch] * (1 - depth * 0.35) + wet * depth
    return out


def _echo(x, delay_s, feedback, repeats=6):
    """Repeats, each quieter, duller and on the other side."""
    out = np.zeros_like(x)
    d = int(delay_s * RATE)
    cur = x
    for k in range(1, repeats + 1):
        cur = _filter(cur, 'low', 3800 - 250 * k) * feedback
        cur = cur[:, ::-1]                                   # swap sides
        if k * d >= len(x): break
        out[k * d:] += cur[:len(x) - k * d]
    return out


def room(seconds=1.9, seed=7, brightness=5200):
    """The sound of a soft room as two (left, right) impulse responses: a few early echoes, then a dense
    tail that dies away, its top faster than its bottom."""
    rng = np.random.default_rng(seed)
    n = int(seconds * RATE)
    t = np.arange(n) / RATE
    ir = np.zeros((n, 2))
    noise = rng.normal(0, 1, (n, 2))
    for lo, hi, rt in ((120, 500, seconds), (500, 2000, seconds * 0.8), (2000, brightness, seconds * 0.5), (brightness, 9000, seconds * 0.25)):
        band = signal.sosfilt(signal.butter(2, [lo / (RATE / 2), min(0.99, hi / (RATE / 2))], 'band', output='sos'), noise, axis=0)
        ir += band * np.exp(-6.9 * t / rt)[:, None]
    ir *= np.minimum(1.0, t / 0.03)[:, None] ** 1.5              # the tail builds over the first 30 ms
    for k in range(9):                                            # early echoes off nearby walls
        at = int((0.011 + 0.057 * rng.random()) * RATE)
        ir[at, rng.integers(0, 2)] += (0.9 - 0.06 * k) * (1 if rng.random() > 0.5 else -1) * 6
    return ir / np.sqrt((ir ** 2).sum(axis=0))


class Song:
    def __init__(self, bpm, beats, swing=0.0, seed=1, reverb_seconds=1.9, reverb_level=1.0):
        self.bpm, self.beats, self.swing, self.seed = bpm, beats, swing, seed
        self.seconds = beats * 60 / bpm
        self.reverb_seconds, self.reverb_level = reverb_seconds, reverb_level
        self.parts = []

    def time(self, beat):
        """Seconds from the start for a beat; off-beat eighths arrive late by `swing` of an eighth."""
        eighth = beat * 2
        frac = eighth - math.floor(eighth)
        if int(math.floor(eighth)) % 2 == 1: beat += self.swing * 0.5 * (1 - frac)
        elif frac > 0: beat += self.swing * 0.5 * frac
        return beat * 60 / self.bpm

    def part(self, name, **kw):
        p = Part(self, name, **kw); self.parts.append(p); return p

    def render(self, loop=True, tail=0.0):
        """loop: the second of two passes, so what still rings at the end is there at the start.
        Otherwise one pass from silence, `tail` seconds longer than the music."""
        rng = np.random.default_rng(self.seed)
        n_loop = int(round(self.seconds * RATE))
        seconds = self.seconds * 2 + self.reverb_seconds + 0.5 if loop else self.seconds + tail + self.reverb_seconds
        total = int(seconds * RATE)
        mix = np.zeros((total, 2)); send = np.zeros((total, 2))
        for p in self.parts:
            x = p.dry(seconds, rng, 2 if loop else 1)
            if p.highpass: x = _filter(x, 'high', p.highpass)
            if p.lowpass: x = _filter(x, 'low', p.lowpass)
            if p.chorus: x = _chorus(x, p.chorus, n_loop / RATE)
            if p.width != 1.0:
                mid, side = (x[:, 0] + x[:, 1]) / 2, (x[:, 0] - x[:, 1]) / 2 * p.width
                x = np.stack([mid + side, mid - side], axis=1)
            g = 10 ** (p.gain / 20)
            left, right = math.cos((p.pan + 1) * math.pi / 4) * math.sqrt(2), math.sin((p.pan + 1) * math.pi / 4) * math.sqrt(2)
            x = x * [g * left, g * right]
            if p.echo: x = x + _echo(x, p.echo_beats * 60 / self.bpm, p.echo_feedback) * p.echo
            mix += x; send += x * p.reverb
        ir = room(self.reverb_seconds, seed=self.seed + 5)
        wet = np.stack([signal.fftconvolve(send[:, c], ir[:, c])[:total] for c in (0, 1)], axis=1)
        wet = _filter(wet, 'high', 180)
        mix += wet * self.reverb_level
        if loop: return mix[n_loop:2 * n_loop]
        return mix[:int((self.seconds + tail) * RATE)]


def shelf(x, hz, db, high=True):
    """Raises or lowers everything above (or below) a frequency by `db`."""
    a = 10 ** (db / 40); w = 2 * math.pi * hz / RATE; cw, sw = math.cos(w), math.sin(w)
    al = sw / 2 * math.sqrt(2)
    k = 1 if high else -1
    b0 = a * ((a + 1) + k * (a - 1) * cw + 2 * math.sqrt(a) * al)
    b1 = -k * 2 * a * ((a - 1) + k * (a + 1) * cw)
    b2 = a * ((a + 1) + k * (a - 1) * cw - 2 * math.sqrt(a) * al)
    a0 = (a + 1) - k * (a - 1) * cw + 2 * math.sqrt(a) * al
    a1 = k * 2 * ((a - 1) - k * (a + 1) * cw)
    a2 = (a + 1) - k * (a - 1) * cw - 2 * math.sqrt(a) * al
    return signal.lfilter([b0 / a0, b1 / a0, b2 / a0], [1, a1 / a0, a2 / a0], x, axis=0)


def loudness(x):
    """The level of what a small speaker passes and the ear is keenest on: 400 Hz to 6 kHz."""
    y = signal.sosfilt(signal.butter(2, [400 / (RATE / 2), 6000 / (RATE / 2)], 'band', output='sos'), x.mean(axis=1) if x.ndim == 2 else x)
    return float(np.sqrt((y ** 2).mean()))


def master(x, level=0.03, peak=0.92, highpass=45, air=0.0):
    """Evens the level: removes rumble, sets the loudness (see loudness()), rounds off the few peaks that
    would clip. A loop stays a loop: every step works on it joined end to end."""
    n = len(x)
    x = np.concatenate([x, x, x])
    x = _filter(x, 'high', highpass)
    if air: x = shelf(x, 3200, air)
    x = x[n:2 * n]
    x = x * (level / loudness(np.concatenate([x, x]))) 
    over = np.abs(x).max()
    if over > peak:
        # soft knee above 70 % of the peak allowed
        k = peak * 0.7
        a = np.abs(x)
        hot = a > k
        x = np.where(hot, np.sign(x) * (k + (peak - k) * np.tanh((a - k) / (peak - k))), x)
    return x


def fade(x, in_s=0.0, out_s=0.0):
    x = x.copy(); n = len(x)
    if in_s: k = int(in_s * RATE); x[:k] *= np.linspace(0, 1, k)[:, None]
    if out_s: k = int(out_s * RATE); x[n - k:] *= (np.cos(np.linspace(0, math.pi, k)) * 0.5 + 0.5)[:, None]
    return x


def write_wav(path, x, channels=2):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    pcm = to_pcm(x if channels == 2 else x.mean(axis=1, keepdims=True))
    with wave.open(path, 'wb') as w:
        w.setnchannels(channels); w.setsampwidth(2); w.setframerate(RATE); w.writeframes(pcm.tobytes())


def to_pcm(x):
    """16-bit samples, with a trace of noise added first so quiet tails fade smoothly instead of in steps."""
    rng = np.random.default_rng(99)
    d = (rng.random(x.shape) - rng.random(x.shape)) / 32768
    return np.clip(np.round((x + d) * 32767), -32768, 32767).astype('<i2')


def report(name, x):
    """Numbers that stand in for listening: level, peak, how the energy is spread, how well the loop joins."""
    rms = np.sqrt((x ** 2).mean()); peak = np.abs(x).max()
    f, p = signal.welch(x.mean(axis=1), RATE, nperseg=4096)
    tot = p.sum()
    band = lambda lo, hi: 100 * p[(f >= lo) & (f < hi)].sum() / tot
    k = RATE // 50
    seam = np.abs(x[0] - x[-1]).max() / (np.abs(np.diff(x[-k:], axis=0)).max() + 1e-12)
    side = np.sqrt(((x[:, 0] - x[:, 1]) ** 2).mean()) / (np.sqrt(((x[:, 0] + x[:, 1]) ** 2).mean()) + 1e-12)
    print(f'{name}: {len(x) / RATE:.2f} s, rms {rms:.3f}, loudness {loudness(x):.3f}, peak {peak:.2f}; energy below 150 Hz {band(0, 150):.0f} %, 150-600 {band(150, 600):.0f} %, '
          f'600-2500 {band(600, 2500):.0f} %, 2500-6000 {band(2500, 6000):.0f} %, above {band(6000, 16000):.1f} %; stereo width {side:.2f}; '
          f'step across the loop point {seam:.2f} x the largest step nearby')
