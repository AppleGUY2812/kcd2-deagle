#!/usr/bin/env python3
"""Reads a recording of tools/flashlag.lua: for each trial, the game frames from the gun's flash card to the candidate
particle flash 0.3 m left of it (0 = the same frame, as the card).
usage: flashlag_read.py <video> [--sheet out.png] [--trials 12] [--blocks mesh,new,seed,restart,live] [--passes 2]
Game frames are counted by the marker the test draws top left ("#" in slot frame id % 4), not by video frames: the
capture repeats a game frame or skips one. The card and the candidate areas are found from the video itself: the card
from the first block (card only), the candidate from the second (the new-emitter control, card + candidate)."""
import argparse, json, subprocess, sys
import numpy as np

ap = argparse.ArgumentParser()
ap.add_argument("video")
ap.add_argument("--sheet")
ap.add_argument("--trials", type=int, default=12)
ap.add_argument("--blocks", default="mesh,new,seed,restart,live")
ap.add_argument("--passes", type=int, default=2)
ap.add_argument("--json")
a = ap.parse_args()
BLOCKS = a.blocks.split(",")

def probe(path):
    out = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries", "stream=width,height",
                          "-of", "json", path], capture_output=True, text=True, check=True).stdout
    s = json.loads(out)["streams"][0]
    return s["width"], s["height"]

def frames(path, vf, w, h):
    p = subprocess.Popen(["ffmpeg", "-v", "error", "-i", path, "-vf", vf + ",format=gray", "-f", "rawvideo", "-"],
                         stdout=subprocess.PIPE)
    n = w * h
    out = []
    while True:
        b = p.stdout.read(n)
        if len(b) < n: break
        out.append(np.frombuffer(b, np.uint8).reshape(h, w))
    p.wait()
    return np.stack(out)

W, H = probe(a.video)
k = W / 1920.0                                   # the test's marker is placed in pixels of the game's viewport
mx1, my1 = int(460 * k) // 2 * 2, int(160 * k) // 2 * 2   # even: yuv420 video crops to even sizes
mark = frames(a.video, f"crop={mx1}:{my1}:0:0", mx1, my1)
sw, sh = 480, int(round(480 * H / W / 2)) * 2
small = frames(a.video, f"scale={sw}:{sh}", sw, sh).astype(np.int16)
n = min(len(mark), len(small))
mark, small = mark[:n], small[:n]

# marker slot per video frame: the bright glyph's centre x -> slot; -1 when none is seen
slots = np.full(n, -1)
for i in range(n):
    ys, xs = np.nonzero(mark[i] > 200)
    if len(xs) > 8:
        slots[i] = int(round((xs.mean() / k - 40 - 18) / 90))
# game frame index per video frame: a new game frame whenever the slot changes (forward by 1-3 mod 4)
game = np.full(n, -1)
g, last = 0, None
for i in range(n):
    s = slots[i]
    if s < 0 or s > 3: continue
    if last is not None and s != last: g += (s - last) % 4
    game[i], last = g, s
seen = game[game >= 0]
print(f"{n} video frames, {len(np.unique(seen))} game frames seen of {int(seen.max()) + 1 if len(seen) else 0} "
      f"({(slots < 0).sum()} video frames without a marker)")

# one value per game frame: the last video frame showing it
ids = np.unique(seen)
pick = {gid: np.nonzero(game == gid)[0][-1] for gid in ids}
G = np.array([small[pick[gid]] for gid in ids])
bg = np.median(G, axis=0)
diff = np.clip(G - bg, 0, None)
diff[:, : int(160 * k * sh / H) + 2, : int(460 * k * sw / W) + 2] = 0   # not the marker

# events: game frames where something lights up after a quiet one
act = diff.reshape(len(G), -1).max(axis=1)
thr = max(40, np.percentile(act, 50) + 30)
on = act > thr
starts = [i for i in range(1, len(on)) if on[i] and not on[i - 1]]
need = a.trials * len(BLOCKS) * a.passes
print(f"{len(starts)} flashes seen, {need} trials run")

def box(mask):
    ys, xs = np.nonzero(mask)
    return (ys.min(), ys.max() + 1, xs.min(), xs.max() + 1) if len(xs) else None

T = a.trials
card = box(np.max([diff[s:s + 2].max(axis=0) for s in starts[:T]], axis=0) > thr)
both = np.max([diff[s:s + 4].max(axis=0) for s in starts[T:2 * T]], axis=0) > thr
y0, y1, x0, x1 = card
both[max(0, y0 - 4):y1 + 4, max(0, x0 - 4):x1 + 4] = False
cand = box(both)
print("card area", card, "candidate area", cand)
if cand is None: sys.exit("no candidate area found in the second block")

def level(b):
    y0, y1, x0, x1 = b
    return diff[:, y0:y1, x0:x1].reshape(len(G), -1).max(axis=1)
lc, lk = level(card), level(cand)
tc, tk = max(40, np.percentile(lc, 50) + 30), max(40, np.percentile(lk, 50) + 30)

res = {}
rows = []
for j, s in enumerate(starts[:need]):
    blk = BLOCKS[(j // T) % len(BLOCKS)] + ("" if a.passes == 1 else f"/p{j // (T * len(BLOCKS)) + 1}")
    f0 = next((i for i in range(max(0, s - 1), min(len(G), s + 4)) if lc[i] > tc), None)
    f1 = next((i for i in range(max(0, s - 1), min(len(G), s + 8)) if lk[i] > tk), None)
    lag = None if f0 is None or f1 is None else int(ids[f1] - ids[f0])
    res.setdefault(blk, []).append(lag)
    rows.append((blk, j % T + 1, s, lag))
for blk, lags in res.items():
    got = [x for x in lags if x is not None]
    hist = {v: got.count(v) for v in sorted(set(got))}
    print(f"{blk:12s} lag in game frames {hist}  missing {lags.count(None)} of {len(lags)}")
if a.json:
    json.dump({"rows": rows, "card": [int(v) for v in card], "cand": [int(v) for v in cand]}, open(a.json, "w"))

if a.sheet:
    from PIL import Image, ImageDraw
    y0 = min(card[0], cand[0]); y1 = max(card[1], cand[1]); x0 = min(card[2], cand[2]); x1 = max(card[3], cand[3])
    pad = 12
    y0, x0 = max(0, y0 - pad), max(0, x0 - pad); y1, x1 = min(sh, y1 + pad), min(sw, x1 + pad)
    sc = 3
    cw, ch = (x1 - x0) * sc, (y1 - y0) * sc
    lines = []
    for blk in res:
        j = next(i for i, r in enumerate(rows) if r[0] == blk and r[1] == 3)   # the third trial: the restart's first doesn't count
        lines.append((blk, rows[j][2]))
    img = Image.new("RGB", (160 + 5 * (cw + 6), len(lines) * (ch + 22) + 30), (20, 20, 20))
    d = ImageDraw.Draw(img)
    d.text((10, 8), "game frame from the card (0 = the card's frame); left box: candidate, right: card", fill=(220, 220, 220))
    for r, (blk, s) in enumerate(lines):
        yy = 30 + r * (ch + 22)
        d.text((10, yy + ch // 2), blk, fill=(240, 200, 120))
        f0 = next((i for i in range(max(0, s - 1), min(len(G), s + 4)) if lc[i] > tc), s)
        for c in range(5):
            gi = f0 - 1 + c
            if not 0 <= gi < len(G): continue
            fr = small[pick[ids[gi]]][y0:y1, x0:x1].clip(0, 255).astype(np.uint8)
            im = Image.fromarray(fr).resize((cw, ch), Image.NEAREST).convert("RGB")
            img.paste(im, (160 + c * (cw + 6), yy))
            d.text((160 + c * (cw + 6) + 4, yy + ch + 4), f"{c - 1:+d}", fill=(200, 200, 200))
    img.save(a.sheet)
    print("sheet", a.sheet)
