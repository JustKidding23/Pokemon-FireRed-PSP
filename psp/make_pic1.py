#!/usr/bin/env python3
"""The PSP menu's background (PIC1.PNG, 480x272): the box art's Charizard,
whole, on the box's swirl, without the logo (picked by the owner, 2026-10-21).

usage: make_pic1.py ART OUT
  ART: the 1920x1080 key art with Charizard on the left and the logo on the
       right, from Game Rant:
       https://static0.gamerantimages.com/wordpress/wp-content/uploads/2026/03/pokemon-firered-version.jpg
Needs OpenCV (opencv-python) and numpy.
"""
import sys

import cv2
import numpy as np

img = cv2.imread(sys.argv[1])
H, W = img.shape[:2]
b, g, r = [img[:, :, i].astype(int) for i in range(3)]

# Charizard, cut out with GrabCut at half size from hints: where he surely is
# (belly, chest, head, wings, flame, tail, feet), where he may be, and the
# swirl showing between his wing and tail
im = cv2.resize(img, None, fx=0.5, fy=0.5, interpolation=cv2.INTER_AREA)
mask = np.full(im.shape[:2], cv2.GC_BGD, np.uint8)
mask[85:480, 0:495] = cv2.GC_PR_BGD
cv2.ellipse(mask, (250, 280), (240, 190), 0, 0, 360, cv2.GC_PR_FGD, -1)
for centre, axes in (((300, 365), (55, 40)), ((290, 300), (30, 40)), ((410, 195), (40, 25)),
                     ((150, 200), (60, 40)), ((330, 180), (15, 30)), ((92, 270), (14, 22)),
                     ((160, 380), (30, 12)), ((240, 450), (25, 12)), ((405, 375), (15, 15))):
    cv2.ellipse(mask, centre, axes, 0, 0, 360, cv2.GC_FGD, -1)
for centre, axes in (((175, 270), (25, 30)), ((440, 300), (30, 35)), ((100, 205), (12, 14)), ((343, 200), (5, 7))):
    cv2.ellipse(mask, centre, axes, 0, 0, 360, cv2.GC_BGD, -1)
bgd, fgd = np.zeros((1, 65)), np.zeros((1, 65))
cv2.grabCut(im, mask, None, bgd, fgd, 8, cv2.GC_INIT_WITH_MASK)
fg = np.where((mask == cv2.GC_FGD) | (mask == cv2.GC_PR_FGD), 1, 0).astype(np.uint8)
count, labels, stats, _ = cv2.connectedComponentsWithStats(fg, connectivity=8)
fg = (labels == 1 + np.argmax(stats[1:, cv2.CC_STAT_AREA])).astype(np.uint8)
alpha = cv2.resize(fg * 255, (W, H), interpolation=cv2.INTER_LINEAR).astype(np.float32) / 255

# The swirl without him or the logo. It turns round (1020, 790), with its
# streaks running round that point, so each ring is filled from the swirl
# showing elsewhere on the same ring.
unknown = np.zeros((H, W), np.uint8)
x, y, w, h = cv2.boundingRect((alpha > 0.15).astype(np.uint8))
unknown[max(0, y - 50):y + h + 50, max(0, x - 50):x + w + 50] = 1
swirl = (r > 150) & (r - b > 60) & (g < 200) & (r - g > 25)
logo = np.zeros((H, W), np.uint8)
logo[270:820, 960:1900] = 1
unknown |= cv2.dilate((logo & ~swirl).astype(np.uint8), np.ones((31, 31), np.uint8))
cx, cy = 1020.0, 790.0
R = int(np.hypot(max(cx, W - cx), max(cy, H - cy))) + 2
A = 2048
polar = cv2.WARP_POLAR_LINEAR
rings = cv2.warpPolar(img.astype(np.float32), (R, A), (cx, cy), R, polar + cv2.INTER_LINEAR)
holes = cv2.warpPolar(unknown.astype(np.float32), (R, A), (cx, cy), R, polar + cv2.INTER_NEAREST)
inside = cv2.warpPolar(np.ones((H, W), np.float32), (R, A), (cx, cy), R, polar + cv2.INTER_NEAREST)
known = (holes < 0.5) & (inside > 0.5)
angles = np.arange(A)
first = None
for rr in range(R):
    k = known[:, rr]
    if k.sum() < 4:
        continue
    first = rr if first is None else first
    ka = angles[k]
    for c in range(3):
        v = rings[k, rr, c]
        rings[~k, rr, c] = np.interp(angles[~k], np.concatenate([ka - A, ka, ka + A]), np.concatenate([v, v, v]))
rings[:, :first] = rings[:, first:first + 1].mean(0, keepdims=True)
fill = cv2.GaussianBlur(cv2.warpPolar(rings, (W, H), (cx, cy), R, polar + cv2.INTER_LINEAR + cv2.WARP_INVERSE_MAP), (0, 0), 1.5)
soft = cv2.GaussianBlur(unknown.astype(np.float32), (0, 0), 6)[..., None]
background = img.astype(np.float32) * (1 - soft) + fill * soft

# Him, a little bigger, on the right (the menu shows its icon and the name on
# the left), with his white glow
x0, y0, x1, y1 = max(0, x - 60), max(0, y - 60), min(W, x + w + 60), min(H, y + h + 60)
art = cv2.resize(img[y0:y1, x0:x1].astype(np.float32), None, fx=1.15, fy=1.15, interpolation=cv2.INTER_CUBIC)
cut = cv2.GaussianBlur(cv2.resize(alpha[y0:y1, x0:x1], None, fx=1.15, fy=1.15, interpolation=cv2.INTER_LINEAR), (0, 0), 1.0)
glow = cv2.GaussianBlur(cv2.dilate((cut > 0.5).astype(np.float32), np.ones((25, 25), np.uint8)), (0, 0), 14)
glow = np.clip(glow * 1.3, 0, 1) * 0.85
hh, ww = cut.shape
ox, oy = W - ww - 10, (H - hh) // 2 + 10
under = background[oy:oy + hh, ox:ox + ww]
under = under * (1 - glow[..., None]) + 255 * glow[..., None]
background[oy:oy + hh, ox:ox + ww] = art * cut[..., None] + under * (1 - cut[..., None])
full = np.clip(background, 0, 255).astype(np.uint8)
cv2.imwrite(sys.argv[2], cv2.resize(full, (482, 272), interpolation=cv2.INTER_AREA)[:, 1:481])
