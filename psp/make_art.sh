#!/bin/bash
# Makes the PSP menu's art and music for the EBOOT, as the owner chose them
# (2026-10-21, research/mockups/X1-xmb-art.png in the port's folder):
#   psp/ICON0.PNG (144x80): the US box's "POKéMON FIRERED VERSION" logo, cut
#                           out of the cover onto transparency
#   psp/PIC1.PNG (480x272): the box art's Charizard, whole, on its swirl, from
#                           Game Rant's 1920x1080 key art (make_pic1.py;
#                           without it or OpenCV, a crop of the box below the logo)
#   psp/SND0.AT3:           the title screen's music (Charizard), from the ROM
# They're made from the box art and the game, so don't share them; .gitignore
# leaves them out. `make psp` puts them in the EBOOT if they're here.
#
# usage: psp/make_art.sh BOXART ROM [GBAREF] [ATRACDENC]
#   KEYART (environment, optional): Game Rant's key art for the background,
#     https://static0.gamerantimages.com/wordpress/wp-content/uploads/2026/03/pokemon-firered-version.jpg
#   BOXART: the US cover, 512x512, from libretro-thumbnails:
#     https://raw.githubusercontent.com/libretro-thumbnails/Nintendo_-_Game_Boy_Advance/master/Named_Boxarts/Pokemon%20-%20FireRed%20Version%20(USA).png
#   ROM: Pokémon FireRed (USA), for the music
#   GBAREF: ~/Projects/PSP-Ports/Tools/gbaref/gbaref (records the ROM's sound)
#   ATRACDENC: https://github.com/dcherednik/atracdenc release 0.2.3. A PSP
#              3001 played its files in the menu but not those of later versions.
# Needs Python 3 with Pillow, ffmpeg, and for the background OpenCV; PYTHON
# names a Python that has OpenCV if the default one doesn't.
set -euo pipefail
boxart=$(realpath "$1")
rom=$(realpath "$2")
gbaref=${3:-$HOME/Projects/PSP-Ports/Tools/gbaref/gbaref}
atracdenc=${4:-$(command -v atracdenc || true)}
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

python3 -I - "$boxart" psp/ICON0.PNG psp/PIC1.PNG "$work/logo_mask.png" <<'EOF'
import sys
from collections import deque
from PIL import Image

cover = Image.open(sys.argv[1]).convert('RGB')
icon_path, pic_path, mask_path = sys.argv[2], sys.argv[3], sys.argv[4]

# The background: Charizard below the logo, between the GBA strip on the left
# and the rating badges at the bottom
cover.crop((112, 165, 512, 392)).resize((480, 272), Image.LANCZOS).save(pic_path)

# The icon: the logo, cut out by filling the background in from the edges up
# to the logo's dark outlines (navy around POKéMON, black around the badge)
logo = cover.crop((130, 5, 495, 210))
W, H = logo.size
px = logo.load()

def lum(p):
    return 0.3 * p[0] + 0.59 * p[1] + 0.11 * p[2]

def outline(p):
    r, g, b = p
    return lum(p) < 75 or (b > r + 40 and b > g + 10 and lum(p) < 125)

background = [[False] * W for _ in range(H)]
queue = deque([(x, 0) for x in range(W)] + [(x, H - 1) for x in range(W)]
              + [(0, y) for y in range(H)] + [(W - 1, y) for y in range(H)])
while queue:
    x, y = queue.popleft()
    if x < 0 or y < 0 or x >= W or y >= H or background[y][x] or outline(px[x, y]):
        continue
    background[y][x] = True
    queue.extend(((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)))

seen = [[False] * W for _ in range(H)]
parts = []
for y in range(H):
    for x in range(W):
        if background[y][x] or seen[y][x]:
            continue
        part, queue = [], deque([(x, y)])
        seen[y][x] = True
        while queue:
            a, b = queue.popleft()
            part.append((a, b))
            for u, v in ((a + 1, b), (a - 1, b), (a, b + 1), (a, b - 1)):
                if 0 <= u < W and 0 <= v < H and not seen[v][u] and not background[v][u]:
                    seen[v][u] = True
                    queue.append((u, v))
        parts.append(part)
parts = [p for p in parts if len(p) > 1500]

# The lower part is the black and white FIRERED VERSION badge. Charizard's
# wing touches it, so only its grey pixels near its white letters are kept.
badge = max(parts, key=lambda p: sum(y for _, y in p) / len(p))
whites = [(x, y) for x, y in badge if lum(px[x, y]) > 200]
top = min(y for _, y in whites) - 7
bottom = max(y for _, y in whites) + 7

out = Image.new('RGBA', (W, H))
o = out.load()
for part in parts:
    for x, y in part:
        r, g, b = px[x, y]
        if part is badge:
            near = [wx for wx, wy in whites if abs(wy - y) <= 6]
            if not near or not (min(near) - 6 <= x <= max(near) + 6 and top <= y <= bottom) \
                    or max(r, g, b) - min(r, g, b) >= 40:
                continue
        o[x, y] = (r, g, b, 255)
# The logo's place on the cover, for make_pic1.py to leave it out
full = Image.new('RGBA', cover.size)
full.paste(out, (130, 5))
full.save(mask_path)
out = out.crop(out.getbbox())
scale = min(144 / out.width, 80 / out.height)
small = out.convert('RGBa').resize((round(out.width * scale), round(out.height * scale)), Image.LANCZOS).convert('RGBA')
icon = Image.new('RGBA', (144, 80))
icon.paste(small, ((144 - small.width) // 2, (80 - small.height) // 2))
icon.save(icon_path)
EOF
if [ -n "${KEYART:-}" ]; then
    "${PYTHON:-python3}" psp/make_pic1.py "$KEYART" psp/PIC1.PNG \
        || echo "No OpenCV: PIC1.PNG is the plain crop of the box"
fi

# 20 seconds of the title music, from where it starts (24.4 s into the intro
# with no save, after a second of silence), about 330 KB as ATRAC3, inside
# the PSP menu's limit of about 500 KB
if [ -n "$atracdenc" ]; then
    printf '2700 -\n' > "$work/input.txt"
    GBAREF_AUDIO="$work/rec.wav" "$gbaref" "$rom" "$work/input.txt" "$work" > /dev/null
    # -ss before -i, so the fades' times count from the clip's start
    ffmpeg -hide_banner -loglevel error -y -ss 24.4 -t 20 -i "$work/rec.wav" \
        -af "afade=t=in:d=0.05,afade=t=out:st=17.5:d=2.5" -ac 2 -ar 44100 -c:a pcm_s16le "$work/title44.wav"
    # atracdenc picks the RIFF container from the lowercase .at3. It writes the
    # RIFF size wrong; the files that played had it set to the file's size
    # less 8, as RIFF has it.
    "$atracdenc" -e atrac3 -i "$work/title44.wav" -o "$work/snd0.at3" > /dev/null 2>&1
    python3 -c 'import os, struct, sys
f = open(sys.argv[1], "r+b"); f.seek(4); f.write(struct.pack("<I", os.path.getsize(sys.argv[1]) - 8))' "$work/snd0.at3"
    cp "$work/snd0.at3" psp/SND0.AT3
else
    echo "atracdenc not found, so there's no SND0.AT3"
fi
ls -l psp/ICON0.PNG psp/PIC1.PNG psp/SND0.AT3 2>/dev/null || true
