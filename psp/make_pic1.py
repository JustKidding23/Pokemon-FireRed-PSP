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

cv2.setRNGSeed(0)       # GrabCut starts from random guesses: the same ones every run
cv2.setNumThreads(1)
img = cv2.imread(sys.argv[1])
H, W = img.shape[:2]
b, g, r = [img[:, :, i].astype(int) for i in range(3)]

# Charizard, cut out with GrabCut at half size from hints: where he surely is
# (belly, chest, head, wings, flame, tail, feet, jaw), where he may be, and
# the swirl showing between his wing and tail
im = cv2.resize(img, None, fx=0.5, fy=0.5, interpolation=cv2.INTER_AREA)
mask = np.full(im.shape[:2], cv2.GC_BGD, np.uint8)
mask[60:480, 0:495] = cv2.GC_PR_BGD   # from above the top of his wing
cv2.ellipse(mask, (250, 280), (240, 190), 0, 0, 360, cv2.GC_PR_FGD, -1)
for centre, axes in (((300, 365), (55, 40)), ((290, 300), (30, 40)), ((410, 195), (40, 25)),
                     ((150, 200), (60, 40)), ((330, 180), (15, 30)), ((92, 270), (14, 22)),
                     ((160, 380), (30, 12)), ((240, 450), (25, 12)), ((405, 375), (15, 15)),
                     ((412, 287), (8, 10)), ((405, 268), (6, 6)), ((94, 318), (6, 8)), ((120, 352), (8, 6)),
                     ((185, 255), (14, 8))):
    cv2.ellipse(mask, centre, axes, 0, 0, 360, cv2.GC_FGD, -1)
for centre, axes in (((125, 290), (14, 16)), ((458, 322), (16, 20)), ((100, 205), (12, 14)), ((343, 200), (5, 7))):
    cv2.ellipse(mask, centre, axes, 0, 0, 360, cv2.GC_BGD, -1)
bgd, fgd = np.zeros((1, 65)), np.zeros((1, 65))
cv2.grabCut(im, mask, None, bgd, fgd, 8, cv2.GC_INIT_WITH_MASK)
fg = np.where((mask == cv2.GC_FGD) | (mask == cv2.GC_PR_FGD), 1, 0).astype(np.uint8)
count, labels, stats, _ = cv2.connectedComponentsWithStats(fg, connectivity=8)
fg = (labels == 1 + np.argmax(stats[1:, cv2.CC_STAT_AREA])).astype(np.uint8)
fg = cv2.resize(fg * 255, (W, H), interpolation=cv2.INTER_LINEAR) > 127

# Clean up his edge. His body is closed off by his outline, so swirl and white
# glow that GrabCut kept (round his flame, under his arm) are what can be
# reached from outside him without crossing the outline.
lum = 0.3 * r + 0.59 * g + 0.11 * b
swirlish = (r > 185) & (r - g > 85)
glowish = (r > 215) & (g > 140) & (b > g - 25)
outline = (lum < 125) & ~swirlish
# the swirl seen through his glow under his arm. Only there: elsewhere the
# same pale orange is his flame or the highlights on his wing's arm.
pale_swirl = np.zeros((H, W), bool)
pale_swirl[540:780, 640:900] = ((lum > 160) & (r > 220) & (r - g > 60) & (b < 160))[540:780, 640:900]
passable = fg & (swirlish | glowish | pale_swirl) & ~cv2.dilate(outline.astype(np.uint8), np.ones((3, 3), np.uint8)).astype(bool)
count, labels = cv2.connectedComponents((passable | ~fg).astype(np.uint8), connectivity=4)
outside = np.unique(np.concatenate([labels[0], labels[-1], labels[:, 0], labels[:, -1]]))
fg &= ~(passable & np.isin(labels, outside))
# and pockets of pure glow he closes in (between his horn and his face): big,
# very light, and not his teeth, claws or eye, which are whiter or bluer
pockets = np.zeros((H, W), bool)   # only between his horn and his face, not his eye
pockets[345:400, 590:700] = (fg & glowish & (lum > 200) & (b <= g + 4) & ~outline)[345:400, 590:700]
count, labels, stats, _ = cv2.connectedComponentsWithStats(pockets.astype(np.uint8), connectivity=4)
for i in range(1, count):
    if stats[i, cv2.CC_STAT_AREA] > 120:
        fg[labels == i] = False
# take back the outline where the cut ran through it, then smooth the edge
near = cv2.dilate(fg.astype(np.uint8), np.ones((9, 9), np.uint8)).astype(bool)
fg |= near & outline
# small dents and bumps left by the cleanup go. Closing fills dents, but it
# could also shut a whole gap (between his arm and his body), so only small
# fills are kept.
closed = cv2.morphologyEx(fg.astype(np.uint8), cv2.MORPH_CLOSE, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (11, 11))).astype(bool)
count, labels, stats, _ = cv2.connectedComponentsWithStats((closed & ~fg).astype(np.uint8), connectivity=8)
small = np.isin(labels, [i for i in range(1, count) if stats[i, cv2.CC_STAT_AREA] <= 250])
fg = fg | small
fg = cv2.morphologyEx(fg.astype(np.uint8), cv2.MORPH_OPEN, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (5, 5))).astype(bool)
fg = cv2.GaussianBlur(fg.astype(np.float32), (0, 0), 1.8) > 0.5
count, labels, stats, _ = cv2.connectedComponentsWithStats(fg.astype(np.uint8), connectivity=8)
fg = labels == 1 + np.argmax(stats[1:, cv2.CC_STAT_AREA])

# One even outline round him, in his line art's colour, except round his flame,
# which has none in the art
inside = cv2.distanceTransform(fg.astype(np.uint8), cv2.DIST_L2, 5)
stroke = np.where(fg, np.clip(3.4 - inside, 0, 1), 0)
flame = (r > 225) & (g > 150) & (g - b > 35)
flame_area = np.zeros((H, W), bool)
flame_area[380:700, 100:330] = True
flame = cv2.dilate((flame & flame_area & fg).astype(np.uint8), np.ones((15, 15), np.uint8)).astype(bool)
stroke[flame] = 0
line = np.array([62, 66, 92], np.float32)  # BGR: a dark brown-grey like his lines
art_src = (img.astype(np.float32) * (1 - stroke[..., None]) + line * stroke[..., None]).astype(np.uint8)
alpha = cv2.GaussianBlur(fg.astype(np.float32), (0, 0), 0.8)

# The swirl without him or the logo. It turns round (947, 659), with its
# streaks running round that point, so each ring is filled from the swirl
# showing elsewhere on the same ring.
unknown = np.zeros((H, W), np.uint8)
x, y, w, h = cv2.boundingRect((alpha > 0.15).astype(np.uint8))
unknown[max(0, y - 50):y + h + 50, max(0, x - 50):x + w + 50] = 1
swirl = (r > 150) & (r - b > 60) & (g < 200) & (r - g > 25)
logo = np.zeros((H, W), np.uint8)
logo[270:820, 960:1900] = 1
unknown |= cv2.dilate((logo & ~swirl).astype(np.uint8), np.ones((31, 31), np.uint8))
cx, cy = 947.0, 659.0
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
# The box's swirl isn't quite circles, so mixing it with the filled rings shows
# where the streaks change course. All of it is smoothed a little along the
# rings instead, so every streak follows the same circles.
rings = cv2.GaussianBlur(np.vstack([rings] * 3), (0, 0), sigmaX=0.1, sigmaY=22)[A:2 * A]
background = cv2.GaussianBlur(cv2.warpPolar(rings, (W, H), (cx, cy), R, polar + cv2.INTER_LINEAR + cv2.WARP_INVERSE_MAP), (0, 0), 1.5)

# Him, a little bigger, on the right (the menu shows its icon and the name on
# the left), with his white glow
x0, y0, x1, y1 = max(0, x - 60), max(0, y - 60), min(W, x + w + 60), min(H, y + h + 60)
art = cv2.resize(art_src[y0:y1, x0:x1].astype(np.float32), None, fx=1.15, fy=1.15, interpolation=cv2.INTER_CUBIC)
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
