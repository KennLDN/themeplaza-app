#!/usr/bin/env python3
"""Random-input soak test: presses random buttons and taps random places for a while and watches for a
crash, a hang, logged errors and shrinking free memory.

  monkey.py MINUTES [SEED] [--chaos]

--chaos: every few batches the local test server is switched between answering normally, slowly, with
garbage, and with a bot-check page (its /_mode switch), so the error paths get random input too.

The app must be running (emulator/cycle.py "wait 200") and should point at the local test server
(emulator/use_mock.py on, emulator/mock_plaza.py running) so the real site is not loaded with requests.
The "Exit" menu entry is made inactive for the run. Every batch of commands is appended to
emulator/shots/monkey_SEED.txt, so a failure can be replayed with ctl.py --file.
"""
import os, random, re, subprocess, sys, time, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
LOG = os.path.expanduser('~/.var/app/org.azahar_emu.Azahar/data/azahar-emu/sdmc/3ds/Theme Plaza/log.txt')
BUTTONS = ['a'] * 5 + ['b'] * 4 + ['x', 'y', 'y', 'l', 'r', 'r', 'start', 'select'] + ['up', 'down', 'left', 'right'] * 4


def batch(rng):
    cmds = []
    for _ in range(rng.randint(8, 20)):
        r = rng.random()
        if r < 0.62: cmds.append('press ' + rng.choice(BUTTONS))
        elif r < 0.80: cmds.append(f'touch {rng.randint(0, 319)} {rng.randint(0, 239)}')
        elif r < 0.84: cmds.append(f'hold {rng.choice(["up", "down", "left", "right", "r", "l"])} {rng.randint(20, 90)}')
        else: cmds.append(f'wait {rng.choice([2, 5, 10, 30, 60, 120])}')
    return cmds


def perf():
    out = []
    for line in open(LOG, errors='replace'):
        m = re.search(r'([\d.]+) fps.*linear free (\d+) KB, heap free (\d+) KB', line)
        if m: out.append((float(m.group(1)), int(m.group(2)), int(m.group(3))))
    return out


def main():
    chaos = '--chaos' in sys.argv
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    minutes = float(args[0])
    seed = int(args[1]) if len(args) > 1 else int(time.time()) % 100000
    rng = random.Random(seed)
    record = os.path.join(HERE, 'shots', f'monkey_{seed}.txt')
    print('seed', seed, 'record', record, flush=True)
    ctl = ['python3', os.path.join(HERE, 'ctl.py')]
    subprocess.run(ctl + ['monkey 1'])
    end = time.time() + minutes * 60
    n = 0
    while time.time() < end:
        cmds = batch(rng)
        with open(record, 'a') as f: f.write(f'# batch {n}\n' + '\n'.join(cmds) + '\n')
        r = subprocess.run(ctl + cmds, capture_output=True, text=True)
        if r.returncode != 0:
            print(f'FAILED at batch {n}: {r.stderr.strip() or r.stdout.strip()}')
            subprocess.run([os.path.join(HERE, 'screenshot.sh'), os.path.join(HERE, 'shots', f'monkey_fail_{seed}.png')], capture_output=True)
            sys.exit(1)
        n += 1
        if chaos and n % 6 == 0:
            mode = rng.choice(['normal', 'normal', 'normal', 'slow', 'garbage', 'blocked', 'gone'])
            try: urllib.request.urlopen(f'http://127.0.0.1:8377/_mode?set={mode}', timeout=5).read()
            except OSError as e: print('could not switch the test server:', e)
            with open(record, 'a') as f: f.write(f'# test server mode: {mode}\n')
        if n % 20 == 0:
            p = perf()
            if p: print(f'batch {n}: {p[-1][0]:.1f} fps, linear free {p[-1][1]} KB, heap free {p[-1][2]} KB', flush=True)
    if chaos:
        try: urllib.request.urlopen('http://127.0.0.1:8377/_mode?set=normal', timeout=5).read()
        except OSError: pass
    subprocess.run(ctl + ['monkey 0', 'wait 300', f'shot monkey_end_{seed}'])
    p = perf()
    slow = [l.strip() for l in open(LOG, errors='replace') if 'slow:' in l]
    other = [l.strip() for l in open(LOG, errors='replace') if not re.match(r'(perf:|ctl:|slow:|theme plaza start|badges:|console:|memory:|sound|network:|theme data|HOME Menu data|badge data|Luma:|preview of|theme install: .*done|splash install: done)', l)]
    print(f'{n} batches without a crash or hang')
    if p:
        print(f'linear free: first {p[0][1]} KB, lowest {min(x[1] for x in p)} KB, last {p[-1][1]} KB')
        print(f'heap free:   first {p[0][2]} KB, lowest {min(x[2] for x in p)} KB, last {p[-1][2]} KB')
        print(f'lowest frame rate over a 3 s window: {min(x[0] for x in p):.1f} fps')
    cmd = [int(m.group(1)) for m in (re.search(r'command buffer max (\d+)%', l) for l in open(LOG, errors='replace')) if m]
    if cmd: print(f'GPU command buffer: at most {max(cmd)} % used in a frame')
    print(f'{len(slow)} slow-step lines, {len(other)} other log lines')
    for l in other[:40]: print('  ', l)


if __name__ == '__main__':
    main()
