"""Test pack 3: echoes made from what lines find around you - no fixed list of places.

Each place is a small 3D scene. Lines go out from your head (32 a second; the echo uses the last 4 seconds,
so 128 directions, from 15 deg below level to straight up). Each line returns only what KCD2's
RayWorldIntersection returns in game: distance, the surface's facing direction and its surface type.
The echo maker never sees the scene itself.

From those hits it builds:
  1. clean echoes: a surface facing you sends back one sharp copy (found with up to 3 extra lines per surface)
  2. bounces between two facing walls (ravine, tunnel, room): repeating copies (image sources)
  3. scattered echoes: every hit sends back a soft, smeared copy (trees and leaves: almost all of it)
  4. a fading tail: its length and loudness come from the space's size and materials (Eyring / Sabine)
High sounds fade with distance through air (ISO 9613-1 numbers), so far echoes come back dull, not hissy.
The flat ground under you is ignored: the bang recording already has it.
"""
import json, os, sys, wave
import numpy as np
import argparse
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
ap.add_argument("out", help="output folder for the WAVs (e.g. Desktop/Deagle Progress/<NN> Gun sound test <n>)")
ap.add_argument("--decho", default=os.path.join(REPO, "assets/build/sounds/decho"),
                help="folder with the Deagle shot decho-1.wav (Call of Duty, gitignored build input)")
ap.add_argument("--ron", default="/mnt/e/Tools/deagle-work/sound/ron_python",
                help="Ready or Not Colt Python WAVs (Python_Fire_*, Python_Reflection_*), made by tools/ron/fsbx.py --wav")
ap.add_argument("--test2", default="/mnt/c/Users/<user>/Desktop/Deagle Progress/08 Gun sound test 2",
                help="test 2 output folder (A2 bang and B1-B6 are reused for the blind test)")
args = ap.parse_args()

SR, C = 44100, 343.0
DE, RON, T2, OUT = args.decho, args.ron, args.test2, args.out
os.makedirs(f"{OUT}/blind test files", exist_ok=True)

BANDS = np.array([125, 250, 500, 1000, 2000, 4000, 8000, 16000.])
AIR = np.array([.0004, .001, .0019, .0037, .0097, .0328, .117, .38])     # dB per metre, ISO 9613-1, 20 C, 50 % RH
MID = 3                                                                  # the 1 kHz band
# absorption per band (125 Hz .. 16 kHz), scattering (0 = mirror, 1 = all scattered), smear of a scattered echo (ms)
MAT = {
    "rock":   ([.02, .03, .04, .05, .06, .07, .08, .09], .15, 2),    # cliff faces, ravine walls, rock tunnel
    "cave":   ([.20, .20, .22, .25, .27, .30, .33, .36], .50, 4),    # cave chamber: rough walls, rubble, side passages
    "hall":   ([.08, .08, .09, .12, .13, .14, .15, .16], .25, 1),    # stone hall with wooden tables, benches, hangings
    "cellar": ([.05, .06, .07, .08, .09, .10, .11, .12], .30, 1.5),  # stone cellar, dirt floor, barrels, sacks
    "house":  ([.25, .18, .15, .17, .22, .28, .32, .35], .40, 2),    # plaster + timber room with furniture, cloth, hay
    "wall":   ([.03, .04, .05, .06, .07, .08, .09, .10], .25, 2),    # village house fronts
    "trunk":  ([.05, .05, .06, .08, .10, .12, .15, .18], .90, 6),    # tree trunks: small and round, scatter only
    "leaves": ([.45, .50, .60, .70, .75, .80, .85, .90], .95, 40),   # foliage: scatters, lets most of it out to the sky
    "slope":  ([.10, .15, .25, .35, .40, .45, .50, .55], .85, 25),   # grassy, rocky hillside
    "ground": ([.10, .20, .35, .50, .55, .60, .65, .70], .50, 3),    # soil, grass
}
# scattered copies per second: hard rock gives a few crisp clicks, leaves and grass a dense soft wash
DENS = {"rock": 250, "cave": 500, "hall": 500, "cellar": 500, "house": 500, "wall": 300, "trunk": 600, "leaves": 1500,
        "slope": 1500, "ground": 1500}
WORD = {"rock": "rock", "cave": "cave wall", "hall": "stone wall", "cellar": "stone wall", "house": "wall",
        "wall": "house wall", "trunk": "trees", "leaves": "leaves", "slope": "hillside", "ground": "ground"}
G0 = -1.7                                     # ground height (your head is at 0)
N_RAYS, LOW_DEG, D_ROOM, T_MAX = 128, -15, 80.0, 400.0
rng = np.random.default_rng(3)

def unit(v): v = np.asarray(v, float); return v / np.linalg.norm(v)

# ---------------- scene pieces (the game's world; the echo maker only sees ray hits) ----------------
class Rect:                                   # flat rectangle: centre, normal, in-plane axis u, half sizes
    def __init__(s, c, n, u, hu, hv, mat):
        s.c, s.n, s.u = np.array(c, float), unit(n), unit(u); s.v = np.cross(s.n, s.u); s.hu, s.hv, s.mat = hu, hv, mat
    def hit(s, o, d):
        dn = d @ s.n
        if abs(dn) < 1e-9: return None
        t = ((s.c - o) @ s.n) / dn
        if t <= 1e-6: return None
        p = o + t * d - s.c
        if abs(p @ s.u) > s.hu or abs(p @ s.v) > s.hv: return None
        return t, (s.n if dn < 0 else -s.n), s.mat
    def outline(s):
        k = [s.c + a * s.u * s.hu + b * s.v * s.hv for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1), (-1, -1))]
        return np.array(k)[:, :2]

class Ground:                                 # endless flat floor at G0
    def __init__(s, mat): s.mat = mat
    def hit(s, o, d):
        if d[2] >= -1e-9: return None
        return (G0 - o[2]) / d[2], np.array([0, 0, 1.]), s.mat

class SphereIn:                               # inside a ball (round cave)
    def __init__(s, c, R, mat): s.c, s.R, s.mat = np.array(c, float), R, mat
    def hit(s, o, d):
        oc = o - s.c; b = oc @ d; disc = b * b - (oc @ oc - s.R ** 2)
        if disc < 0: return None
        t = -b + np.sqrt(disc)
        return (t, (s.c - (o + t * d)) / s.R, s.mat) if t > 1e-6 else None

class TubeIn:                                 # inside a pipe along y, open ends
    def __init__(s, cx, cz, R, y0, y1, mat): s.cx, s.cz, s.R, s.y0, s.y1, s.mat = cx, cz, R, y0, y1, mat
    def hit(s, o, d):
        ox, oz, dx, dz = o[0] - s.cx, o[2] - s.cz, d[0], d[2]; a = dx * dx + dz * dz
        if a < 1e-12: return None
        b = ox * dx + oz * dz; disc = b * b - a * (ox * ox + oz * oz - s.R ** 2)
        if disc < 0: return None
        t = (-b + np.sqrt(disc)) / a; y = o[1] + t * d[1]
        if t <= 1e-6 or not s.y0 <= y <= s.y1: return None
        p = o + t * d
        return t, np.array([s.cx - p[0], 0, s.cz - p[2]]) / s.R, s.mat

class Trunks:                                 # many upright tree trunks, hit from outside
    def __init__(s, x, y, r, top, mat): s.x, s.y, s.r, s.top, s.mat = x, y, r, top, mat
    def hit(s, o, d):
        a = d[0] ** 2 + d[1] ** 2
        if a < 1e-12: return None
        ox, oy = o[0] - s.x, o[1] - s.y; b = ox * d[0] + oy * d[1]; disc = b * b - a * (ox * ox + oy * oy - s.r ** 2)
        ok = disc >= 0; t = (-b - np.sqrt(np.where(ok, disc, 0))) / a; z = o[2] + t * d[2]
        ok &= (t > 1e-6) & (z <= s.top) & (z >= G0)
        if not ok.any(): return None
        i = int(np.argmin(np.where(ok, t, np.inf))); p = o + t[i] * d
        return t[i], np.array([p[0] - s.x[i], p[1] - s.y[i], 0]) / s.r[i], s.mat

class Canopy:                                 # layer of leaves overhead: a line gets stopped somewhere inside it
    def __init__(s, z0, z1, lam, mat): s.z0, s.z1, s.lam, s.mat = z0, z1, lam, mat
    def hit(s, o, d):
        if s.z0 <= o[2] <= s.z1:                 # you are inside the layer (bushes): stopped somewhere along the way out
            ex = [(z - o[2]) / d[2] for z in (s.z0, s.z1) if abs(d[2]) > 1e-6 and (z - o[2]) / d[2] > 0]
            t0, t1 = 0, min(ex) if ex else np.inf
        elif d[2] <= 1e-3: return None
        else: t0, t1 = max((s.z0 - o[2]) / d[2], 0), (s.z1 - o[2]) / d[2]
        u = np.random.default_rng(abs(hash(tuple(np.round(d, 6)))) % 2 ** 32).random()
        t = t0 - s.lam * np.log(1 - u)
        return (t, -d, s.mat) if t < t1 else None

class Scene:
    def __init__(s, name, parts, view, note=""): s.name, s.parts, s.view, s.note = name, parts, view, note
    def cast(s, o, d):
        best = None
        for p in s.parts:
            h = p.hit(o, d)
            if h and h[0] < T_MAX and (best is None or h[0] < best[0]): best = h
        return best

def wall_x(x, y0, y1, h, mat): return Rect((x, (y0 + y1) / 2, G0 + h / 2), (1, 0, 0), (0, 1, 0), (y1 - y0) / 2, h / 2, mat)
def wall_y(y, x0, x1, h, mat): return Rect(((x0 + x1) / 2, y, G0 + h / 2), (0, 1, 0), (1, 0, 0), (x1 - x0) / 2, h / 2, mat)
def ceiling(x0, x1, y0, y1, h, mat): return Rect(((x0 + x1) / 2, (y0 + y1) / 2, G0 + h), (0, 0, 1), (1, 0, 0), (x1 - x0) / 2, (y1 - y0) / 2, mat)
def room(x0, x1, y0, y1, h, mat):
    return [wall_x(x0, y0, y1, h, mat), wall_x(x1, y0, y1, h, mat), wall_y(y0, x0, x1, h, mat), wall_y(y1, x0, x1, h, mat),
            ceiling(x0, x1, y0, y1, h, mat), Ground(mat)]

def forest():
    n = int(np.pi * 70 ** 2 * .06); r = 70 * np.sqrt(rng.random(n)); a = rng.random(n) * 2 * np.pi
    x, y = r * np.cos(a), r * np.sin(a); keep = np.hypot(x, y) > 2.5
    return [Trunks(x[keep], y[keep], rng.uniform(.12, .3, keep.sum()), G0 + 20, "trunk"),
            Canopy(G0 + 8, G0 + 20, 5.0, "leaves"), Canopy(G0 + .3, G0 + 2.5, 18.0, "leaves"), Ground("ground")]

s35, c35 = np.sin(np.radians(35)), np.cos(np.radians(35))
SCENES = [
    Scene("Open field", [Ground("ground")], 60, "nothing around you"),
    Scene("Field + tree line + hill", [Rect((0, 60, G0 + 7.5), (0, -1, 0), (1, 0, 0), 300, 7.5, "leaves"),
          Rect((240 + 40 / np.tan(np.radians(35)), 0, G0 + 40), (-s35, 0, c35), (0, 1, 0), 300, 40 / s35, "slope"),
          Ground("ground")], 300, "tree line 60 m ahead, hillside 240 m right"),
    Scene("Forest", forest(), 45, "trees all around, bushes, leaves overhead"),
    Scene("Village yard", [wall_x(-7, -6, 12, 7, "wall"), wall_x(9, -6, 2, 7, "wall"), wall_x(9, 5, 12, 7, "wall"),
          wall_y(12, -7, 0, 7, "wall"), wall_y(12, 4, 9, 7, "wall"), wall_y(-6, -7, 9, 7, "wall"), Ground("ground")],
          16, "houses around you, two streets out"),
    Scene("Next to a cliff", [wall_x(-20, -150, 150, 50, "rock"), Ground("ground")], 40, "rock cliff 20 m to your left"),
    Scene("Ravine", [wall_x(-8, -150, 150, 15, "rock"), wall_x(10, -150, 150, 15, "rock"), Ground("ground")], 25,
          "15 m rock walls, 8 m left and 10 m right"),
    Scene("House room", room(-1.7, 2.3, -2, 3, 2.6, "house"), 4, "4 x 5 m room, furniture"),
    Scene("Stone cellar", room(-2.2, 2.8, -3, 4, 2.4, "cellar"), 5, "5 x 7 m, stone, barrels"),
    Scene("Castle hall", room(-4, 8, -8, 16, 9, "hall"), 18, "12 x 24 m, 9 m high, stone, tables"),
    Scene("Tunnel", [TubeIn(-.6, 1.0, 3, -40, 110, "rock"), Ground("rock")], 20, "6 m wide pipe of rock, 40 m in"),
    Scene("Round cave", [SphereIn((-3, 4, G0 + 6), 15, "cave"), Ground("cave")], 20, "30 m across"),
    Scene("Bigger round cave", [SphereIn((-5, 8, G0 + 12), 30, "cave"), Ground("cave")], 38, "60 m across"),
]

# ---------------- the echo maker: only uses what the lines return ----------------
z0 = np.sin(np.radians(LOW_DEG)); i = np.arange(N_RAYS) + .5; zz = z0 + (1 - z0) * i / N_RAYS
DIRS = np.stack([np.sqrt(1 - zz ** 2) * np.cos(i * np.pi * (3 - np.sqrt(5))),
                 np.sqrt(1 - zz ** 2) * np.sin(i * np.pi * (3 - np.sqrt(5))), zz], 1)
DOMEGA = 2 * np.pi * (1 - z0) / N_RAYS
O = np.zeros(3)
def absorb(m): return np.array(MAT[m][0])
def floorlike(n, p): return n[2] > .9 and p[2] < -.5        # level ground under you

def survey(sc):
    hits, casts = [], N_RAYS
    for d in DIRS:
        h = sc.cast(O, d)
        if h is None: hits.append(("open", d)); continue
        t, n, m = h
        hits.append(("floor", d, t) if floorlike(n, t * d) else ("hit", d, t, n, m))
    # clean echoes: group hits by the surface they lie on, then find each surface's mirror point
    groups = []
    for h in hits:
        if h[0] != "hit" or MAT[h[4]][1] >= .9: continue
        _, d, t, n, m = h; p = -(t * d) @ n
        if p < .3: continue
        for g in groups:
            if g["n"] @ n > .97 and abs(g["p"] - p) < max(1, .05 * p): g["k"] += 1; break
        else: groups.append({"n": n, "p": p, "k": 1, "d": d})
    groups.sort(key=lambda g: -g["k"] / g["p"] ** 2)
    mirrors = []
    def add_mirror(u, t, m, q):
        if all(u @ f["u"] < .966 or abs(t - f["p"]) > 1 + .1 * t for f in mirrors):
            mirrors.append({"u": u, "p": t, "m": m, "q": q}); return True
        return False
    def walk(u, sign, eta, steps):            # step along the surface until it faces you straight on
        nonlocal casts
        for _ in range(steps):
            h = sc.cast(O, u); casts += 1
            if h is None: return None
            t, n, m = h; c = -(n @ u)
            if floorlike(n, t * u): return None
            if c > .999: return u, t, m
            u = unit(u + sign * eta * (n - (n @ u) * u) / max(c, .2))
        return None
    for g in groups[:10]:
        f = walk(-g["n"], -1, 1.0, 5) or walk(g["d"], +1, .8, 14)   # nearest point first, else farthest (domes)
        if f: add_mirror(f[0], f[1], f[2], f[1] * f[0])
    # bounces: is there a wall straight behind you facing this one?
    pairs = []
    for f in list(mirrors):
        h = sc.cast(O, -f["u"]); casts += 1
        if h is None or h[1] @ f["u"] < .97: continue
        t, n, m = h; q = -t * f["u"]
        if any(np.linalg.norm(q - b["q"]) < 1 and np.linalg.norm(f["q"] - a["q"]) < 1 for b, a in pairs): continue
        other = {"u": -f["u"], "p": t, "m": m, "q": q}
        if not floorlike(n, q): add_mirror(other["u"], t, m, q)
        pairs.append((f, other))
    return hits, mirrors, pairs, casts

def side(u): return float(np.clip(u[0], -1, 1))
# The deep boom you hear when you shoot stays at the muzzle; what reaches walls and hills is the gun's far-away sound.
# Measured from Ready or Not: its echo files vs its close bang, indoors and outdoors alike, lows below 500 Hz cut this much.
TONE = 10 ** (np.array([-8.5, -4.0, -.5, 0, 0, 0, 0, 0]) / 20)
def air(path): return TONE * 10 ** (-AIR * path / 20)

def taps_and_tail(hits, mirrors, pairs):
    taps = []                                  # (delay s, amplitude per band, x-direction, smear ms, kind)
    for f in mirrors:
        a, s, _ = MAT[f["m"]]
        taps.append((2 * f["p"] / C, np.sqrt((1 - s) * (1 - np.array(a))) / (2 * max(f["p"], .75)) * air(2 * f["p"]),
                     side(f["u"]), 0, "clean", 0))
    for h in hits:                             # scattered copies
        if h[0] != "hit": continue
        _, d, t, n, m = h; a, s, sm = MAT[m]; t = max(t, .75); cos = abs(d @ n)
        e = s * (1 - np.array(a)) * cos * cos * 1.5 * DOMEGA / (np.pi * t * t)
        spread = 2 * t * np.sqrt(DOMEGA) * np.sqrt(max(0, 1 - cos * cos)) / C * 1000
        taps.append((2 * t / C, np.sqrt(e) * air(2 * t), side(d), min(sm + spread, 450), "scatter", DENS[m]))
    e_bounce = np.zeros(8)
    for A, B in pairs:                         # 2nd order and up between two facing walls
        for first, other in ((A, B), (B, A)):
            a, b = first["p"], other["p"]
            ra = np.sqrt((1 - MAT[first["m"]][1]) * (1 - absorb(first["m"])))
            rb = np.sqrt((1 - MAT[other["m"]][1]) * (1 - absorb(other["m"])))
            for k in range(2, 80):
                path = (k + 1) * a + (k - 1) * b if k % 2 else k * (a + b)
                if path / C > 3: break
                amp = ra ** ((k + 1) // 2) * rb ** (k // 2) / path * air(path)
                if amp.max() < 3e-5: break
                taps.append((path / C, amp, side(first["u"] if k % 2 else other["u"]), 0, "bounce", 0)); e_bounce += amp ** 2
    # fading tail from the space around you
    room = [h for h in hits if h[0] == "hit" and h[2] <= D_ROOM]
    n_open = sum(1 for h in hits if h[0] == "open" or (h[0] == "hit" and h[2] > D_ROOM))
    tail = None
    closed = len(room) / max(len(room) + n_open, 1)
    if room:
        ts = np.array([h[2] for h in room]); cs = np.array([max(abs(h[1] @ h[3]), .1) for h in room])
        S = (ts ** 2 / cs).sum(); V = (ts ** 3).sum() / 3        # surface and volume the hits stand for (x solid angle)
        mfp = 4 * V / S                                         # mean distance sound travels between bounces
        abar = (sum(absorb(h[4]) for h in room) + n_open) / (len(room) + n_open)   # lines out into the open: all lost
        grow = 4 * np.pi / N_RAYS * (len(room) + n_open) / len(room)
        S_full, V_full = S * grow, V * grow
        m = AIR / 4.343; al = -np.log(1 - np.minimum(abar, .999))
        e_rev = 16 * np.pi * (1 - abar) / (S_full * al + 4 * m * V_full) * np.clip((closed - .3) / .4, 0, 1) ** 2
        if abar[MID] < .98 and e_rev[MID] > 1e-4:   # quieter than -40 dB: no tail
            t60 = .04025 * mfp / (al + m * mfp)
            e_taps = sum(t[1] ** 2 for t in taps)
            w = np.array([1 / h[2] ** 2 for h in room]); xs = np.array([side(h[1]) for h in room])
            tail = {"t60": t60, "e": np.maximum(e_rev - e_taps, .4 * e_rev), "e_rev": e_rev, "mfp": mfp,
                    "start": 2 * ts.min() / C, "full": 2 * np.median(ts) / C + .005,
                    "x": float((w * xs).sum() / w.sum()), "abar": abar, "closed": closed}
    return taps, tail, n_open

# ---------------- turning taps + tail into a stereo echo (impulse response) ----------------
K, PRE = 8192, 256
F = np.fft.rfftfreq(K, 1 / SR); LF = np.log2(np.maximum(F, 20))
def curve(g, f=LF): return 10 ** (np.interp(f, np.log2(BANDS), 20 * np.log10(np.maximum(g, 1e-9))) / 20)
def pan(x): return np.sqrt(1 - x), np.sqrt(1 + x)     # centre = 1, 1 (same as the bang in both ears)

def render_ir(taps, tail, seconds):
    L = int(seconds * SR); ir = np.zeros((L + (1 << 16), 2)); r = np.random.default_rng(7)
    for delay, amp, x, smear, _, dens in taps:
        n = max(1, int(smear / 1000 * SR)); KK = max(K, 1 << int(np.ceil(np.log2(n + 4096))))
        src = np.zeros(KK)
        if smear < .5: src[0] = 1
        else:
            tt = np.arange(n) / n
            env = np.exp(-3 * tt) if smear < 60 else np.sin(np.pi / 2 * np.minimum(tt / .35, 1)) ** 2 * np.exp(-2.5 * tt)
            cnt = max(3, int(dens * smear / 1000)); pos = r.integers(0, n, cnt)
            b = np.zeros(n); np.add.at(b, pos, r.choice([-1., 1.], cnt) * r.uniform(.4, 1, cnt) * env[pos])
            src[:n] = b / np.sqrt((b ** 2).sum())
        FK = np.log2(np.maximum(np.fft.rfftfreq(KK, 1 / SR), 20))
        k = np.roll(np.fft.irfft(np.fft.rfft(src) * curve(amp, FK), KK), PRE)
        gl, gr = pan(x); itd = int(abs(x) * .00066 * SR)
        i0 = int(round(delay * SR)) - PRE
        for ch, g, extra in ((0, gl, itd if x > 0 else 0), (1, gr, itd if x < 0 else 0)):
            j = i0 + extra
            if 0 <= j < L: ir[j:j + KK, ch] += g * k
    if tail is not None:
        t = np.arange(L) / SR; FL = np.log2(np.maximum(np.fft.rfftfreq(L, 1 / SR), 20)); lb = np.log2(BANDS)
        ramp = np.clip((t - tail["start"]) / max(tail["full"] - tail["start"], .002), 0, 1)
        gl, gr = pan(.6 * tail["x"])
        for b in range(8):                      # each band fades at its own speed (highs faster)
            if b == 0: w = (FL <= lb[1]) * 1.0
            elif b == 7: w = (FL >= lb[6]) * 1.0
            else: w = np.zeros_like(FL)
            for j in (b - 1, b):                # raised-cosine crossovers, one octave wide
                if 0 <= j < 7:
                    xx = np.clip(FL - lb[j], 0, 1); inside = (FL >= lb[j]) & (FL <= lb[j + 1])
                    w = np.where(inside, np.cos(np.pi / 2 * xx) ** 2 if j == b else np.sin(np.pi / 2 * xx) ** 2, w)
            env2 = ramp * np.exp(-13.82 * np.clip(t - tail["start"], 0, None) / tail["t60"][b])
            env2 *= tail["e"][b] * TONE[b] ** 2 / env2.sum()
            for ch, g in ((0, gl), (1, gr)):
                nz = np.fft.irfft(np.fft.rfft(r.standard_normal(L)) * w, L)
                ir[:L, ch] += g * nz * np.sqrt(env2)
    return ir[:L]

# ---------------- audio helpers ----------------
def rd(p):
    w = wave.open(p); sr = w.getframerate(); ch = w.getnchannels()
    x = np.frombuffer(w.readframes(w.getnframes()), np.int16).astype(float).reshape(-1, ch) / 32768
    if ch == 1: x = np.repeat(x, 2, 1)
    if sr != SR:
        n = int(round(len(x) * SR / sr)); X = np.fft.rfft(x, axis=0); mm = n // 2 + 1
        Y = np.zeros((mm, 2), complex); k = min(mm, len(X)); Y[:k] = X[:k]; x = np.fft.irfft(Y, n, axis=0) * n / len(x)
    return x
def conv(a, b):
    n = len(a) + len(b) - 1; N = 1 << (n - 1).bit_length()
    return np.fft.irfft(np.fft.rfft(a, N, axis=0) * np.fft.rfft(b, N, axis=0), N, axis=0)[:n]
def energy(x): return float((x ** 2).sum())
def pad(x, n): return np.vstack([x, np.zeros((max(0, n - len(x)), 2))])[:n]
def cut(x, full, end):
    a, b = int(full * SR), int(end * SR); g = np.ones(b); g[a:b] = .5 * (1 + np.cos(np.linspace(0, np.pi, b - a)))
    return x[:b] * g[:, None]
def db(x): return 10 * np.log10(x)
def _kw():   # ITU-R BS.1770 K-weighting (how loud it sounds, not raw energy): high shelf + low cut
    def rbj(kind, f0, q, gdb=0):
        A = 10 ** (gdb / 40); w = 2 * np.pi * f0 / SR; cs, al = np.cos(w), np.sin(w) / (2 * q)
        if kind == "shelf":
            sq = 2 * np.sqrt(A) * al
            return ([A * ((A + 1) + (A - 1) * cs + sq), -2 * A * ((A - 1) + (A + 1) * cs), A * ((A + 1) + (A - 1) * cs - sq)],
                    [(A + 1) - (A - 1) * cs + sq, 2 * ((A - 1) - (A + 1) * cs), (A + 1) - (A - 1) * cs - sq])
        return [(1 + cs) / 2, -(1 + cs), (1 + cs) / 2], [1 + al, -2 * cs, 1 - al]
    def resp(f):
        z = np.exp(-2j * np.pi * f / SR); h = 1
        for b, a in (rbj("shelf", 1681.97, .70718, 3.99984), rbj("hp", 38.1355, .50033)):
            h = h * (b[0] + b[1] * z + b[2] * z * z) / (a[0] + a[1] * z + a[2] * z * z)
        return np.abs(h) ** 2
    return resp
KW = _kw()
def loud(x):    # K-weighted energy, all channels
    X = np.fft.rfft(x, axis=0); w = KW(np.fft.rfftfreq(len(x), 1 / SR))
    return float(((np.abs(X) ** 2) * (w[:, None] if X.ndim > 1 else w)).sum())

dry = cut(rd(f"{DE}/decho-1.wav"), .55, .80)                        # bang A2 from test 2
# Echoes are made from the bang's punch only (first 90 ms, faded by 130 ms). The rest of the file is Call of Duty's
# own baked-in tail; echoing that too made every echo drag a copy of CoD's tail behind it.
mono = cut(dry, .09, .13).mean(1)

# ---------------- run every place ----------------
results = []
for sc in SCENES:
    hits, mirrors, pairs, casts = survey(sc)
    taps, tail, n_open = taps_and_tail(hits, mirrors, pairs)
    secs = 1.2 if tail is None else min(8, 1.3 * tail["t60"].max() + .3)
    secs = max(secs, max(t[0] for t in taps) + .3 if taps else 0)
    ir = render_ir(taps, tail, secs)
    wet = np.stack([conv(mono, ir[:, 0]), conv(mono, ir[:, 1])], 1)
    phys = db(loud(wet) / loud(dry)) if energy(wet) > 0 else -np.inf
    results.append(dict(sc=sc, hits=hits, mirrors=mirrors, pairs=pairs, casts=casts, taps=taps, tail=tail,
                        n_open=n_open, wet=wet, phys=phys, ir=ir))

# ---------------- echo volume: match Ready or Not's levels on average, keep the physics order ----------------
fire = rd(f"{RON}/Python_Fire_01.wav")
REF = {"Village yard": "Ext_Small", "Forest": "Ext_Base", "Field + tree line + hill": "Ext_Large",
       "House room": "Int_Small", "Stone cellar": "Int_Base", "Castle hall": "Int_Big"}
ron = {k: rd(f"{RON}/Python_Reflection_{v}_01.wav") for k, v in REF.items()}
ron_db = {k: db(loud(x) / loud(fire)) for k, x in ron.items()}
def roof(r):     # share of near-overhead lines (> 65 deg up) that hit something hard within 80 m
    up = [h for h in r["hits"] if h[1][2] > np.sin(np.radians(65)) and h[0] != "floor"]
    return sum(1 for h in up if h[0] == "hit" and h[2] < D_ROOM and MAT[h[4]][1] < .9) / max(len(up), 1)
for r in results:
    r["roof"] = roof(r); r["size"] = np.log2(max(r["tail"]["mfp"] if r["tail"] else 2.2, 1))
byname = {r["sc"].name: r for r in results}
out_lvl = float(np.mean([ron_db[k] for k in REF if byname[k]["roof"] < .5]))           # outdoors: one level, like Ready or Not
ins = [k for k in REF if byname[k]["roof"] >= .5]
k_in, g_in = np.polyfit([byname[k]["size"] for k in ins], [ron_db[k] for k in ins], 1)  # indoors: bigger = louder, like Ready or Not
top = max(ron_db.values()) + 1
def rule(r): return (1 - r["roof"]) * out_lvl + r["roof"] * min(g_in + k_in * r["size"], top)
print(f"rule: outdoors {out_lvl:+.1f} dB; indoors {g_in:+.1f} + {k_in:.2f} x log2(size), max {top:+.1f} dB  (sounding loudness, echo vs bang)")
print(f"{'place':26s} {'roof':>4s} {'physics':>8s} {'final':>7s} {'Ready or Not':>12s}")
for r in results:
    r["final"] = rule(r) if np.isfinite(r["phys"]) else -np.inf
    if np.isfinite(r["phys"]): r["wet"] = r["wet"] * 10 ** ((r["final"] - r["phys"]) / 20)
    print(f"{r['sc'].name:26s} {r['roof']:4.1f} {r['phys']:8.1f} {r['final']:7.1f} {ron_db.get(r['sc'].name, float('nan')):12.1f}")
g_db, k_sq = out_lvl, k_in

# ---------------- write the files ----------------
def trim(y, floor_db=-75):
    e = (y ** 2).sum(1); rem = np.cumsum(e[::-1])[::-1]; end = int(np.argmax(rem < rem[0] * 10 ** (floor_db / 10))) or len(y)
    end = max(end, len(dry)); y = y[:end].copy(); f = min(int(.05 * SR), end // 4)
    y[-f:] *= np.linspace(1, 0, f)[:, None]; return y

outs = []                                       # (path, signal)
for idx, r in enumerate(results, 1):
    y = pad(dry, len(r["wet"])) + r["wet"]
    r["file"] = f"{idx:02d} {r['sc'].name}.wav"; outs.append((r["file"], trim(y)))
blind = []
# Ready or Not's echo files are its Colt's echo, so against them both sides use the Colt: its bang, and our echo made
# from the Colt's punch at the same loudness as Ready or Not's echo. Only how the echo was made differs.
colt_punch = cut(fire, .09, .13).mean(1)
for k, v in REF.items():
    r = byname[k]
    ours = np.stack([conv(colt_punch, r["ir"][:, 0]), conv(colt_punch, r["ir"][:, 1])], 1)
    ours *= np.sqrt(loud(ron[k]) / loud(ours))
    pair = []
    for tag, e in (("ours", ours), ("ron", ron[k])):
        n = max(len(fire), len(e)); f = f"blind test files/colt {tag} {v}.wav"
        outs.append((f, trim(pad(fire, n) + pad(e, n)))); pair.append(f)
    blind.append({"place": k, "kind_a": "new", "a": pair[0], "kind_b": "ron", "b": pair[1], "b_label": f"Ready or Not {v}"})
a2_t2 = rd(f"{T2}/A2 bang - longer cut.wav"); g_t2 = np.sqrt(energy(a2_t2[:int(.3 * SR)]) / energy(dry[:int(.3 * SR)]))
OLD = {"Open field": "B2", "Forest": "B1", "Village yard": "B3", "House room": "B4", "Stone cellar": "B5", "Round cave": "B6"}
for k, code in OLD.items():                     # round 2's version, set to the same bang loudness
    src = next(f for f in os.listdir(T2) if f.startswith(code + " "))
    f = f"blind test files/old {code}.wav"; outs.append((f, trim(rd(f"{T2}/{src}") / g_t2)))
    blind.append({"place": k, "kind_a": "new", "a": next(r["file"] for r in results if r["sc"].name == k),
                  "kind_b": "old", "b": f, "b_label": f"round 2 {code}"})
scale = .95 / max(np.abs(y).max() for _, y in outs)
for f, y in outs:
    w = wave.open(f"{OUT}/{f}", "wb"); w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR)
    w.writeframes((np.clip(y * scale, -1, 1) * 32767).astype(np.int16).tobytes()); w.close()

# ---------------- plain-words summary per place ----------------
def where(u):
    if u[2] > .7: return "above"
    names = ["ahead", "ahead-right", "right", "behind-right", "behind", "behind-left", "left", "ahead-left"]
    return names[int(((np.degrees(np.arctan2(u[0], u[1])) + 22.5) % 360) // 45)]
places = []
for idx, r in enumerate(results):
    hits = r["hits"]; nv = sum(1 for h in hits if h[0] != "floor")
    closed = sum(1 for h in hits if h[0] == "hit" and h[2] <= D_ROOM) / max(nv, 1)
    ms = sorted(r["mirrors"], key=lambda f: f["p"])
    echoes = ", ".join(f"{'ceiling' if where(f['u']) == 'above' and f['p'] < 20 else WORD[f['m']]} {f['p']:.0f} m {where(f['u'])}"
                       for f in ms[:3])
    if len(ms) > 3: echoes += f" +{len(ms) - 3} more"
    far = [h for h in hits if h[0] == "hit" and h[2] > D_ROOM]
    scat = {}
    for h in hits:
        if h[0] == "hit" and MAT[h[4]][1] >= .8: scat.setdefault(WORD[h[4]], []).append(h[2])
    soft = ", ".join(f"{k} {np.median(v):.0f} m" for k, v in scat.items())
    tl = r["tail"]
    line1 = f"Lines hitting something close: {closed * 100:.0f}%"
    line2 = "Clean echoes: " + (echoes or "none")
    if soft: line2 += " · Soft echoes: " + soft
    if r["pairs"]: line2 += " · Bounces between facing walls"
    line3 = f"Tail: {tl['t60'][MID]:.1f} s" if tl else "Tail: none"
    if np.isfinite(r["final"]): line3 += f" · echo {r['final']:+.0f} dB vs the bang (how loud it sounds)"
    if r["sc"].name in ron_db: line3 += f", Ready or Not's {REF[r['sc'].name]}: {ron_db[r['sc'].name]:+.0f} dB"
    places.append({"file": r["file"], "name": r["sc"].name, "note": r["sc"].note, "lines": [line1, line2, line3],
                   "casts": r["casts"]})
    lr = db(energy(r['wet'][:, 0]) / energy(r['wet'][:, 1])) if energy(r['wet']) > 0 else 0
    print(f"{r['sc'].name:26s} echo L-R {lr:+5.1f} dB casts {r['casts']:3d} mirrors {len(r['mirrors'])} pairs {len(r['pairs'])} taps {len(r['taps']):3d} "
          + (f"T60 1k {tl['t60'][MID]:.2f}s 8k {tl['t60'][6]:.2f}s mfp {tl['mfp']:.1f} abar {tl['abar'][MID]:.2f} closed {tl['closed']:.2f} rev {db(tl['e_rev'][MID]):+.1f}dB" if tl else "no tail"))
json.dump({"places": places, "blind": blind, "calib": {"g_db": g_db, "k": k_sq}}, open(f"{OUT}/manifest.json", "w"), indent=1)

# ---------------- picture: what the lines see, from above ----------------
import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
COL = {"rock": "#c9b79c", "cave": "#a48b6a", "hall": "#b8b8c8", "cellar": "#9a9aa8", "house": "#d9a066", "wall": "#e0c080",
       "trunk": "#8b6b4a", "leaves": "#5fae5a", "slope": "#9bb06a", "ground": "#666"}
fig, axs = plt.subplots(3, 4, figsize=(20, 15.5), facecolor="#15171a")
for num, (ax, r) in enumerate(zip(axs.flat, results), 1):
    sc, V = r["sc"], r["sc"].view; ax.set_facecolor("#1d2024")
    for p in sc.parts:
        if isinstance(p, Rect):
            o = p.outline(); ax.plot(o[:, 0], o[:, 1], color=COL[p.mat], lw=2.5 if abs(p.n[2]) < .5 else .6,
                                     alpha=1 if abs(p.n[2]) < .5 else .35)
        elif isinstance(p, SphereIn):
            ax.add_patch(plt.Circle(p.c[:2], p.R, fill=False, color=COL[p.mat], lw=2.5))
        elif isinstance(p, TubeIn):
            for x in (p.cx - p.R, p.cx + p.R): ax.plot([x, x], [p.y0, p.y1], color=COL[p.mat], lw=2.5)
        elif isinstance(p, Trunks):
            ax.scatter(p.x, p.y, s=(p.r * 40) ** 2 / 4, color=COL["trunk"], zorder=2)
        elif isinstance(p, Canopy):
            ax.add_patch(plt.Rectangle((-V * 2, -V * 2), V * 4, V * 4, color=COL["leaves"], alpha=.07))
    for h in r["hits"]:
        d = h[1]; dxy = d[:2]
        if h[0] == "open" or (h[0] == "hit" and h[2] > V * 2):
            e = dxy / max(np.linalg.norm(dxy), 1e-6) * V * 1.5 * np.linalg.norm(dxy); ax.plot([0, e[0]], [0, e[1]], color="#555", lw=.5, alpha=.5)
        elif h[0] == "floor": continue
        else:
            e = h[2] * dxy; ax.plot([0, e[0]], [0, e[1]], color=COL[h[4]], lw=.7, alpha=.55)
            ax.scatter([e[0]], [e[1]], s=6, color=COL[h[4]], zorder=3)
    for f in r["mirrors"]:
        q = f["q"]; ax.plot([0, q[0]], [0, q[1]], color="#fff", lw=1.6, zorder=4)
        ax.scatter([q[0]], [q[1]], s=90, facecolor="none", edgecolor="#fff", lw=2, zorder=5)
    for A, B in r["pairs"]:
        ax.plot([A["q"][0], B["q"][0]], [A["q"][1], B["q"][1]], color="#e0a84a", lw=2, ls="--", zorder=4)
    ax.scatter([0], [0], marker="^", s=160, color="#e0a84a", zorder=6)
    ax.set_xlim(-V, V); ax.set_ylim(-V, V); ax.set_aspect("equal"); ax.set_xticks([]); ax.set_yticks([])
    for s_ in ax.spines.values(): s_.set_color("#2c3036")
    bar = [1, 2, 5, 10, 20, 50, 100][int(np.searchsorted([1, 2, 5, 10, 20, 50, 100], V / 3))] if V / 3 <= 100 else 100
    ax.plot([-V * .9, -V * .9 + bar], [-V * .9] * 2, color="#9a9890", lw=3); ax.text(-V * .9, -V * .84, f"{bar} m", color="#9a9890", fontsize=11)
    tl = r["tail"]; sub = f"{len(r['mirrors'])} clean echo{'es' if len(r['mirrors']) != 1 else ''} · " + (f"tail {tl['t60'][MID]:.1f} s" if tl else "no tail")
    ax.set_title(f"{num}. {sc.name}\n{sub}", color="#e8e6e1", fontsize=14, loc="left")
fig.suptitle("What the lines see, from above.  You are the orange arrow, facing up.  Thin lines: the 128 lines (grey = went out into the open).\n"
             "White circles: clean echoes (surfaces that face you).  Dashed orange: sound bouncing between two facing walls.",
             color="#e8e6e1", fontsize=15, x=.02, ha="left")
fig.tight_layout(rect=(0, 0, 1, .94)); fig.savefig(f"{OUT}/what the lines see.png", dpi=72, facecolor=fig.get_facecolor())
print("wrote", len(outs), "wavs")
