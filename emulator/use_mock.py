#!/usr/bin/env python3
"""Points the app in the emulator at the local test server (emulator/mock_plaza.py), or back at the real site.

  use_mock.py on [PORT]     writes sdmc:/3ds/Theme Plaza/plaza_server.txt
  use_mock.py off           removes it
The app reads the file when it starts.
"""
import os, sys

F = os.path.expanduser('~/.var/app/org.azahar_emu.Azahar/data/azahar-emu/sdmc/3ds/Theme Plaza/plaza_server.txt')
if len(sys.argv) > 1 and sys.argv[1] == 'on':
    port = sys.argv[2] if len(sys.argv) > 2 else '8377'
    open(F, 'w').write(f'http://127.0.0.1:{port}\n')
    print('app will use', open(F).read().strip())
else:
    if os.path.exists(F): os.unlink(F)
    print('app will use the real Theme Plaza')
