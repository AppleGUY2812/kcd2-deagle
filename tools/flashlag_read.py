#!/usr/bin/env python3
"""Reads a recording of tools/flashlag.lua: for each trial, the game frame the gun's flash card first shows and the
frame the candidate particle flash 0.3 m left of it first shows, both counted from the frame the trial ran in Lua
(0 = drawn in that same frame).
usage: flashlag_read.py <video> <log> [--sheet out.png] [--json out.json]
  <log>: the run's "FLASHLAG" lines from kcd.log (from its "FLASHLAG start" line on).
Game frames are counted by the marker the test draws top left (a "#" in slot frame id % 4), not by video frames: the
capture repeats a game frame or misses one. The video's frame count is tied to the logged engine frame ids by finding
the offset at which the trials line up with what changes on screen. The card and candidate areas come from averaging
each block's trials on their own frames (a moving scene: wind, light, the gun's idle sway, averages out)."""
import argparse, json, re, subprocess, sys
import numpy as np

ap = argparse.ArgumentParser()
ap.add_argument("video")
ap.add_argument("log")
ap.add_argument("--sheet")
ap.add_argument("--json")
a = ap.parse_args()

trials = []   # (pass, block, i, frame id)
for line in open(a.log, encoding="utf-8", errors="replace"):
    m = re.search(r"FLASHLAG (p\d) (\w+) (\d+) frame (\d+)", line)
    if m: trials.append((m.group(1), m.group(2), int(m.group(3)), int(m.group(4))))
if not trials: sys.exit("no trial lines in " + a.log)

def probe(path):
    out = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries", "stream=width,height",
                          "-of", "json", path], capture_output=True, text=True, check=True).stdout
    s = json.loads(out)["streams"][0]
    return s["width"], s["height"]

def frames(path, vf, w, h, rgb=False):
    p = subprocess.Popen(["ffmpeg", "-v", "error", "-i", path, "-vf", vf + (",format=rgb24" if rgb else ",format=gray"),
                          "-f", "rawvideo", "-"], stdout=subprocess.PIPE)
    shape = (h, w, 3) if rgb else (h, w)
    n = int(np.prod(shape))
    out = []
    while True:
        b = p.stdout.read(n)
        if len(b) < n: break
        out.append(np.frombuffer(b, np.uint8).reshape(shape))
    p.wait()
    return np.stack(out)

W, H = probe(a.video)
k = W / 1920.0                                   # the test's marker is placed in pixels of the game's viewport
mx1, my1 = int(460 * k) // 2 * 2, int(160 * k) // 2 * 2   # even: yuv420 video crops to even sizes
mark = frames(a.video, f"crop={mx1}:{my1}:0:0", mx1, my1, rgb=True).astype(np.int16)
sw, sh = 640, int(round(640 * H / W / 2)) * 2
small = frames(a.video, f"scale={sw}:{sh}", sw, sh).astype(np.float32)
n = min(len(mark), len(small))
mark, small = mark[:n], small[:n]

# marker slot per video frame: the glyph's centre x -> slot; -1 when none is seen. KCD2 draws the "#" magenta and about
# 8 px wide at size 5 (its left edge 2 px right of x; in game 2026-10-18), so it is found by colour, not brightness
# (sunlit roofs top left are as bright).
slots = np.full(n, -1)
for i in range(n):
    R, Gc, B = mark[i][..., 0], mark[i][..., 1], mark[i][..., 2]
    ys, xs = np.nonzero((R - Gc > 70) & (B - Gc > 50))
    if len(xs) > 8:
        slots[i] = int(round((xs.mean() / k - 45.5) / 90)) % 4
# game frame index per video frame: a new game frame whenever the slot changes (forward by 1-3 mod 4)
game = np.full(n, -1)
g, last = 0, None
for i in range(n):
    s = slots[i]
    if s < 0: continue
    if last is not None and s != last: g += (s - last) % 4
    game[i], last = g, s
first = next(i for i in range(n) if game[i] >= 0)
slot0 = slots[first]                             # frame id % 4 of game frame 0
ids = np.unique(game[game >= 0])
pick = {int(gid): int(np.nonzero(game == gid)[0][-1]) for gid in ids}   # the last video frame showing it
print(f"{n} video frames, {len(ids)} game frames seen of {int(ids.max()) + 1} (the rest never reached the capture)")

def img(gid):                                    # a game frame's picture, or None if the capture missed it
    v = pick.get(int(gid))
    return None if v is None else small[v]

# the offset: engine frame id = game index + base, base = slot0 (mod 4); the right one makes the trial frames the ones
# where the most changes from the frame before, in the lower half (the gun)
lo = small[:, sh // 2:, :]
chg = np.zeros(int(ids.max()) + 2)
for gid in ids:
    p = pick.get(int(gid) - 1)
    if p is not None: chg[gid] = np.abs(lo[pick[int(gid)]] - lo[p]).mean()
F = np.array([t[3] for t in trials])
best = None
for base in range(F.max() - int(ids.max()) - 8, F.min() + 8):
    if (base - slot0) % 4: continue
    gi = F - base
    if gi.min() < 0 or gi.max() + 3 >= len(chg): continue
    score = sum(max(chg[x], chg[x + 1]) for x in gi)
    if best is None or score > best[0]: best = (score, base)
if best is None: sys.exit("could not line the trials up with the video")
base = best[1]
print(f"engine frame id = game index + {base}")

# areas: per block, the trials' frames 0..+3 minus the frame before, averaged (only what the trials light up stays)
def lift(block, upto=4):
    acc, m = np.zeros((sh, sw), np.float32), 0
    for p_, b, i, f in trials:
        if b != block or (block == "restart" and i == 1): continue
        g0 = f - base
        before = img(g0 - 1)
        if before is None: continue
        after = [img(g0 + d) for d in range(upto)]
        after = [x for x in after if x is not None]
        if not after: continue
        acc += np.clip(np.max(after, axis=0) - before, 0, None); m += 1
    return acc / max(m, 1)

def area(mask):
    ys, xs = np.nonzero(mask)
    if len(xs) == 0: return None
    # the main blob's box: pixels within 2 sigma of the median
    cy, cx = np.median(ys), np.median(xs)
    sy, sx = max(3, ys.std()), max(3, xs.std())
    keep = (np.abs(ys - cy) < 2 * sy) & (np.abs(xs - cx) < 2 * sx)
    ys, xs = ys[keep], xs[keep]
    return int(ys.min()), int(ys.max()) + 1, int(xs.min()), int(xs.max()) + 1

Lm = lift("mesh")
card = area(Lm > max(12, np.percentile(Lm, 99.7)))
Ln = lift("new", 6)
y0, y1, x0, x1 = card
Ln2 = Ln.copy(); Ln2[max(0, y0 - 6):y1 + 6, max(0, x0 - 6):x1 + 6] = 0
cand = area(Ln2 > max(12, np.percentile(Ln2, 99.7)))
print("card area (y0, y1, x0, x1 at %dx%d)" % (sw, sh), card, "candidate area", cand)
if cand is None: sys.exit("no candidate area found in the new-emitter block")

def level(b, gid):
    x = img(gid)
    if x is None: return None
    y0, y1, x0, x1 = b
    return float(x[y0:y1, x0:x1].mean())

rows, res = [], {}
for p_, blk, i, f in trials:
    g0 = f - base
    ref_c, ref_k = level(card, g0 - 1), level(cand, g0 - 1)
    if ref_c is None or ref_k is None:
        rows.append((p_, blk, i, f, None, None)); continue
    def onset(b, ref, rng):
        for d in rng:
            v = level(b, g0 + d)
            if v is not None and v - ref > 10: return d
        return None
    dc = onset(card, ref_c, range(-1, 4))
    dk = onset(cand, ref_k, range(-1, 8)) if blk != "mesh" else None
    rows.append((p_, blk, i, f, dc, dk))
    if not (blk == "restart" and i == 1): res.setdefault(f"{blk}/{p_}", []).append((dc, dk))
for key, v in res.items():
    hc = {}; hk = {}
    for dc, dk in v:
        hc[dc] = hc.get(dc, 0) + 1; hk[dk] = hk.get(dk, 0) + 1
    print(f"{key:12s} card first seen at {dict(sorted(hc.items(), key=lambda x: (x[0] is None, x[0] or 0)))}   "
          f"candidate at {dict(sorted(hk.items(), key=lambda x: (x[0] is None, x[0] or 0)))}")
if a.json:
    json.dump({"base": base, "card": card, "cand": cand, "rows": rows}, open(a.json, "w"))

if a.sheet:
    from PIL import Image, ImageDraw
    yy0 = min(card[0], cand[0]); yy1 = max(card[1], cand[1]); xx0 = min(card[2], cand[2]); xx1 = max(card[3], cand[3])
    pad = 16
    yy0, xx0 = max(0, yy0 - pad), max(0, xx0 - pad); yy1, xx1 = min(sh, yy1 + pad), min(sw, xx1 + pad)
    sc = max(1, min(4, 300 // max(1, yy1 - yy0)))
    cw, ch = (xx1 - xx0) * sc, (yy1 - yy0) * sc
    keys = list(res)
    sheet = Image.new("RGB", (170 + 6 * (cw + 6), len(keys) * (ch + 24) + 34), (20, 20, 20))
    d = ImageDraw.Draw(sheet)
    d.text((10, 8), "game frames from the trial's own frame (0 = drawn in the frame it was asked for)", fill=(220, 220, 220))
    for r, key in enumerate(keys):
        blk, p_ = key.split("/")
        t = next(t for t in trials if t[0] == p_ and t[1] == blk and t[2] == 3)
        g0 = t[3] - base
        y = 34 + r * (ch + 24)
        d.text((10, y + ch // 2), key, fill=(240, 200, 120))
        for c in range(6):
            x = img(g0 - 1 + c)
            if x is None: continue
            fr = x[yy0:yy1, xx0:xx1].clip(0, 255).astype(np.uint8)
            im = Image.fromarray(fr).resize((cw, ch), Image.NEAREST).convert("RGB")
            dd = ImageDraw.Draw(im)
            for (b0, b1, c0, c1), col in ((card, (90, 200, 255)), (cand, (255, 160, 60))):
                dd.rectangle([(c0 - xx0) * sc, (b0 - yy0) * sc, (c1 - xx0) * sc - 1, (b1 - yy0) * sc - 1], outline=col)
            sheet.paste(im, (170 + c * (cw + 6), y))
            d.text((170 + c * (cw + 6) + 4, y + ch + 4), f"{c - 1:+d}", fill=(200, 200, 200))
    sheet.save(a.sheet)
    print("sheet", a.sheet, "(blue box: card, orange: candidate; trial 3 of each block)")
