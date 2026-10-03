#!/usr/bin/env python3
"""Records what the emulator plays for a few seconds and reports whether there was sound.

  record.py SECONDS OUT.wav

Captures the output of the sink Azahar plays into (through PipeWire), then prints the peak and RMS
level and the strongest frequencies of each second, which is enough to tell silence, UI blips and
music apart without listening.
"""
import subprocess, sys, wave
import numpy as np

secs, out = float(sys.argv[1]), sys.argv[2]
p = subprocess.Popen(['pw-record', '--target', 'effect_input.eq10', '-P', '{ stream.capture.sink = true }', '--rate', '32000', '--channels', '1', '--format', 's16', out])
try:
    p.wait(timeout=secs)
except subprocess.TimeoutExpired:
    p.terminate(); p.wait()
w = wave.open(out)
rate = w.getframerate()
d = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float32) / 32768
print(f'{len(d) / rate:.1f} s at {rate} Hz, peak {np.abs(d).max() if len(d) else 0:.3f}')
for i in range(0, len(d) - rate + 1, rate):
    seg = d[i:i + rate]
    spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
    top = np.argsort(spec)[-3:][::-1]
    print(f'  second {i // rate}: rms {np.sqrt((seg ** 2).mean()):.4f}  strongest {", ".join(str(int(t)) + " Hz" for t in top)}')
