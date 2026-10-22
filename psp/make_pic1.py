#!/usr/bin/env python3
"""The PSP menu's background (PIC1.PNG, 480x272): the US box's Charizard, cut
out whole, on a swirl like the box's, without the box's logo and badges.

usage: make_pic1.py BOXART LOGO_MASK OUT
  BOXART:    the US cover, 512x512 (see make_art.sh)
  LOGO_MASK: the cover-sized image whose opaque pixels are the logo (make_art.sh)
Needs OpenCV (opencv-python) and numpy.
"""
import sys

import cv2
import numpy as np

cover = cv2.imread(sys.argv[1])
logo = cv2.imread(sys.argv[2], cv2.IMREAD_UNCHANGED)[:, :, 3]
H, W = cover.shape[:2]

# Charizard, cut out with GrabCut from hints: where he surely is, where he may
# be, and what surely isn't him (the logo with its glow, the badges, the swirl
# showing between his wings and tail)
mask = np.full((H, W), cv2.GC_BGD, np.uint8)
mask[138:470, 104:500] = cv2.GC_PR_BGD
cv2.ellipse(mask, (300, 300), (185, 140), 0, 0, 360, cv2.GC_PR_FGD, -1)
for centre, axes in (((335, 355), (40, 30)), ((330, 280), (25, 40)), ((445, 255), (30, 22)),
                     ((200, 235), (40, 25)), ((168, 310), (14, 18))):
    cv2.ellipse(mask, centre, axes, 0, 0, 360, cv2.GC_FGD, -1)
for point in ((176, 338), (190, 358), (210, 374), (235, 385), (255, 388)):  # his tail
    cv2.circle(mask, point, 5, cv2.GC_FGD, -1)
mask[cv2.dilate((logo > 0).astype(np.uint8), np.ones((7, 7), np.uint8)) > 0] = cv2.GC_BGD
cv2.circle(mask, (214, 435), 38, cv2.GC_BGD, -1)   # GET CONNECTED
cv2.circle(mask, (470, 470), 104, cv2.GC_BGD, -1)  # INCLUDES
mask[395:470, 104:175] = cv2.GC_BGD                # the rating
cv2.ellipse(mask, (220, 322), (32, 16), 20, 0, 360, cv2.GC_BGD, -1)
cv2.ellipse(mask, (305, 240), (10, 14), 0, 0, 360, cv2.GC_BGD, -1)
cv2.ellipse(mask, (150, 272), (22, 10), 25, 0, 360, cv2.GC_BGD, -1)
bgd, fgd = np.zeros((1, 65), np.float64), np.zeros((1, 65), np.float64)
cv2.grabCut(cover, mask, None, bgd, fgd, 8, cv2.GC_INIT_WITH_MASK)
fg = np.where((mask == cv2.GC_FGD) | (mask == cv2.GC_PR_FGD), 1, 0).astype(np.uint8)
count, labels, stats, _ = cv2.connectedComponentsWithStats(fg, connectivity=8)
fg = (labels == 1 + np.argmax(stats[1:, cv2.CC_STAT_AREA])).astype(np.uint8)

# The swirl, in the box's colours: bands spiralling out from Charizard's chest,
# streaked like brush strokes, a little darker at the far left where the menu
# shows the icon and the name
pixels = cover[200:380, 440:505].reshape(-1, 3).astype(np.float32)
lum = pixels.sum(1)
dark = pixels[lum < np.percentile(lum, 20)].mean(0)
light = pixels[lum > np.percentile(lum, 80)].mean(0)
OW, OH = 480, 272
yy, xx = np.mgrid[0:OH, 0:OW].astype(np.float32)
dx, dy = xx - 330, yy - 140
r = np.sqrt(dx * dx + dy * dy) + 1e-3
a = np.arctan2(dy, dx)
noise = np.random.default_rng(4).standard_normal((64, 720)).astype(np.float32)
noise = cv2.GaussianBlur(np.hstack([noise] * 3), (0, 0), sigmaX=9, sigmaY=0.8)[:, 720:1440]
streak = noise[np.clip(r / 400 * 63, 0, 63).astype(np.int32), ((a + np.pi) / (2 * np.pi) * 719).astype(np.int32)]
t = cv2.GaussianBlur(np.sin(r / 15.0 - a * 2.0 + 0.8 * np.sin(a * 3 + r / 55.0) + 1.6 * streak), (0, 0), 1.0)
w = np.clip(0.5 + 0.6 * t, 0, 1)[..., None]
background = (dark * (1 - w) + light * w) * (0.9 + 0.1 * np.clip((xx - 20) / 300, 0, 1)[..., None])
background = np.clip(background, 0, 255).astype(np.uint8)

# Charizard, whole, filling the height on the right
x, y, w0, h0 = cv2.boundingRect(fg)
scale = 262 / h0
nw, nh = int(w0 * scale), int(h0 * scale)
art = cv2.resize(cover[y:y + h0, x:x + w0], (nw, nh), interpolation=cv2.INTER_AREA)
alpha = cv2.resize((fg[y:y + h0, x:x + w0] * 255).astype(np.uint8), (nw, nh), interpolation=cv2.INTER_AREA)
alpha = cv2.GaussianBlur(alpha, (0, 0), 0.7).astype(np.float32)[..., None] / 255
ox, oy = OW - nw - 6, (OH - nh) // 2 + 2
under = background[oy:oy + nh, ox:ox + nw].astype(np.float32)
background[oy:oy + nh, ox:ox + nw] = (art * alpha + under * (1 - alpha)).astype(np.uint8)
cv2.imwrite(sys.argv[3], background)
