#!/bin/sh
# Capture only the Azahar window (Hyprland + grim). Usage: screenshot.sh out.png
OUT="${1:-azahar.png}"
GEOM=$(hyprctl clients -j | python3 -c "
import json, sys
for c in json.load(sys.stdin):
    if c.get('class') == 'org.azahar_emu.Azahar' and c['title'].startswith('Azahar'):
        print(f\"{c['at'][0]},{c['at'][1]} {c['size'][0]}x{c['size'][1]}\"); break
")
[ -n "$GEOM" ] || { echo "Azahar window not found" >&2; exit 1; }
exec grim -g "$GEOM" "$OUT"
