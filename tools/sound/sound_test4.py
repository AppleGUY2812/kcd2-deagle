"""Test pack 4: rays decide where echoes come from and when; real recorded places give the tail its texture;
levels are tuned to Ready or Not band by band and window by window before anyone listens.

Changes from test 3 (why test 3 lost):
  1. near scattered echoes (trees, walls under 40 m) and the made-up noise tail are gone. In their place:
     a real impulse response (OpenAIR, measured in a real forest / gorge / stone room), its direct sound cut off,
     its decay reshaped to the place's length (highs keep dying first, as recorded). Past the recording's own
     noise floor each band carries on as noise in that band at the same decay rate.
  2. a far surface (hill, cliff ends, tree line) is one continuous echo: sharp 3 ms start at its nearest point,
     rolling on while farther parts answer. Test 3 made slow 450 ms swells; the first test 4 chopped it into
     0.25 s pieces with silence between (heard as cutting out).
  3. each of the 6 places Ready or Not has is tuned to Ready or Not's echo: 3 bands x 4 time windows, by smooth
     gain slopes (steps pumped). Windows the rays leave physically empty stay empty.
  4. test 3's loud() grew with file length; fixed. The Deagle bang no longer collapses at 0.6-0.75 s.
Needs: ~/.venvs/deagle-audio (numpy scipy soundfile matplotlib), tools/sound/get_openair.sh, the Ready or Not Colt
WAVs (tools/ron/fsbx.py, see docs/ron_extraction.md). Run: ~/.venvs/deagle-audio/bin/python sound_test4.py <out dir>
"""
import json, os, sys, wave
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
OUT4 = sys.argv[1]
sys.argv = ["x", OUT4]
exec(open(f"{HERE}/sound_test3.py").read().split("# ---------------- run every place")[0])   # scenes, rays, helpers
OUT = OUT4
os.makedirs(f"{OUT}/blind test files", exist_ok=True)
import irlib, measure4
from irlist import F as IRF

_loud_raw = loud
def loud(x): return _loud_raw(x) / len(x)        # test 3's loud() grew with file length (unnormalised FFT): fixed

# Deagle bang: the CoD file carries its own echo for over a second. Test 3's cut faded it out between 0.55 and 0.8 s,
# a 27 dB drop in 0.15 s that sounds like the audio cutting out. Now it dies away like a real shot:
# the boom (under ~250-500 Hz) holds to 0.25 s, then fades (0.12 s time constant); everything above fades from 0.12 s.
def _shape_bang(x):
    t = np.arange(len(x)) / SR; Xf = np.fft.rfft(x, axis=0); f = np.fft.rfftfreq(len(x), 1 / SR)
    lowm = np.clip((np.log2(500) - np.log2(np.maximum(f, 1))), 0, 1)    # 1-octave crossover, 250-500 Hz
    lo = np.fft.irfft(Xf * lowm[:, None], len(x), axis=0); hi = x - lo
    glo = np.exp(-np.clip(t - .25, 0, None) / .12); ghi = np.exp(-np.clip(t - .12, 0, None) / .09)
    y = lo * glo[:, None] + hi * ghi[:, None]; n = int(.9 * SR); y = y[:n]
    f5 = int(.05 * SR); y[-f5:] *= np.linspace(1, 0, f5)[:, None]
    return y
# Test 4b used _shape_bang; the Deagle rounds then lost to test 3 5-0, but the bang and the echo had both changed.
# BANG = "test3" keeps test 3's exact bang (A2) so a round compares echoes only; "shaped" is 4b's.
BANG = os.environ.get("BANG", "test3")
if BANG == "shaped": dry = _shape_bang(rd(f"{DE}/decho-1.wav")); mono = cut(dry, .09, .13).mean(1)
# GUN=mk5 (test 5): Ready or Not's MK V (.50 AE, "Grizzly") replaces both the Colt and our Deagle. Its shots are
# clean (no baked echo), so every place uses the MK V shot, and the blind test is MK V + Ready or Not's MK V echo
# against MK V + our computed echo, 2 takes per place (shot 1 + echo _01, shot 2 + echo _02).
GUN = os.environ.get("GUN", "colt")
GRIZ = os.environ.get("RON_GRIZZLY", "/mnt/e/Tools/deagle-work/sound/ron_grizzly")
if GUN == "mk5": dry = rd(f"{GRIZ}/Grizzly-1.wav"); mono = cut(dry, .09, .13).mean(1)

# ---------------- real tails ----------------
# place -> (impulse response, cut ms after its direct sound, decay at 1 kHz (s) or None = the rays' estimate,
#           shift s (when the tail starts), level vs the rays' tail estimate (dB) or None = from the rays)
REAL = {
    "Forest":                   ("koli s2", 3, 1.7, 0, None),
    "Field + tree line + hill": ("koli s4", 3, 1.7, 2 * 60 / C, -6),     # the forest behind the tree line rings back
    "Village yard":             ("tyndall", 30, 1.0, 0, None),
    "Ravine":                   ("trollers s2", 20, None, 0, None),
    "House room":               ("elveden smoking", 8, .8, 0, None),
    "Stone cellar":             ("bottle dungeon", 8, None, 0, None),
    "Castle hall":              ("elveden marble", 40, 2.0, 0, None),
    "Tunnel":                   ("elveden marble", 15, 2.5, 0, None),
    "Round cave":               ("elveden marble", 40, 2.4, 0, None),
    "Bigger round cave":        ("elveden marble", 60, 3.5, 0, None),
}
# Outdoors there is always land somewhere within a kilometre: a dark, late rumble from far hills and forest edges.
# Its level is a guess (no recording to check it against); it scales with how much of the horizon is open.
HORIZON = {"Open field": -14, "Field + tree line + hill": -20, "Next to a cliff": -20, "Village yard": -30}

def split8(x):
    """8 bands that add back up to x (raised-cosine crossovers one octave wide at 125 Hz .. 16 kHz)."""
    L = len(x); FL = np.log2(np.maximum(np.fft.rfftfreq(L, 1 / SR), 20)); lb = np.log2(BANDS); X = np.fft.rfft(x, axis=0)
    out = []
    for b in range(8):
        if b == 0: w = (FL <= lb[1]) * 1.0
        elif b == 7: w = (FL >= lb[6]) * 1.0
        else: w = np.zeros_like(FL)
        for j in (b - 1, b):
            if 0 <= j < 7:
                xx = np.clip(FL - lb[j], 0, 1); inside = (FL >= lb[j]) & (FL <= lb[j + 1])
                w = np.where(inside, np.cos(np.pi / 2 * xx) ** 2 if j == b else np.sin(np.pi / 2 * xx) ** 2, w)
        out.append(np.fft.irfft(X * (w[:, None] if X.ndim > 1 else w), L, axis=0))
    return out

def band_decay(y):
    """Late decay time (s) of one band and the time its envelope reaches the noise floor + 10 dB."""
    e = (y ** 2).sum(1); k = int(.02 * SR); sm = np.convolve(e, np.ones(k) / k, "same") + 1e-30
    noise = sm[int(len(sm) * .9):].mean(); db_ = 10 * np.log10(sm)
    t = np.arange(len(sm)) / SR; a = int(.03 * SR)
    over = np.where(db_ > 10 * np.log10(noise) + 10)[0]; b = over[-1] if len(over) else a + 1
    b = max(b, a + int(.05 * SR))
    stop = np.argmax(db_[a:b] < db_[a] - 40); b = a + stop if stop > 0 else b
    T = -60 / np.polyfit(t[a:b], db_[a:b], 1)[0]
    return float(np.clip(T, .05, 20)), t[b]

def real_tail(name, cut_ms, T_mid, shift, L, rise=.004):
    ir = irlib.load(IRF[name]); d = irlib.direct(ir); y = ir[d:].copy()
    c = int(cut_ms / 1000 * SR); y[:c] = 0; f = int(rise * SR); y[c:c + f] *= (np.sin(np.linspace(0, np.pi / 2, f)) ** 2)[:, None]
    parts = split8(y); Ti = [band_decay(p) for p in parts]
    T_ir_mid = Ti[MID][0]; s = (T_mid / T_ir_mid) if T_mid else 1.0
    t = np.arange(len(y)) / SR; out = np.zeros_like(y); info = []
    # A loudspeaker is ~50 dB quieter than a gunshot, so the recording's tail sinks into its own hiss long before a
    # gun's would. Past that point each band carries on as noise in the same band, decaying at the same rate
    # (statistically that is what a late reverb tail is), crossfaded over 50 ms.
    noise = split8(np.random.default_rng(9).standard_normal((len(y), 2)))
    for b, (p, (T, tn)) in enumerate(zip(parts, Ti)):
        Tt = T * min(s, 1.6)                                           # lengthening is capped
        g = np.exp(-6.91 * t * (1 / Tt - 1 / T))
        end = max(tn - .05, .1); fade = np.clip((end + .05 - t) / .05, 0, 1)
        q = p * g[:, None]; i0, i1 = int((end - .05) * SR), int(end * SR)
        rms = np.sqrt((q[i0:i1] ** 2).mean() + 1e-30); nz = noise[b] / np.sqrt((noise[b] ** 2).mean())
        cont = nz * (rms * np.exp(-6.91 * np.clip(t - end + .025, 0, None) / Tt))[:, None]
        out += q * fade[:, None] + cont * (1 - fade)[:, None]; info.append(Tt)
    k = int(shift * SR); res = np.zeros((L, 2)); n = min(len(out), L - k)
    if n > 0: res[k:k + n] = out[:n]
    return res, np.array(info), T_ir_mid

def burst(delays, energies, decay_ms, rng):
    """One far surface = one continuous echo. Every ray hit on it adds a 3 ms rise and a decay_ms fade at its own
    arrival time; the sum is the envelope, filled with dense random taps. Starts sharp at the nearest point,
    rolls on while farther parts of the surface answer, never chopped into pieces. Unit energy."""
    d = np.asarray(delays) - min(delays); n = int((d.max() + 6 * decay_ms / 1000) * SR) + 8; tt = np.arange(n) / SR
    env = np.zeros(n)
    for di, ei in zip(d, energies):
        tau = np.clip(tt - di, 0, None); env += ei * (tt >= di) * np.minimum(tau / .003, 1) * np.exp(-tau / (decay_ms / 1000))
    amp = np.sqrt(env / env.max())
    cnt = max(4, int(3000 * n / SR)); pos = rng.integers(0, n, cnt)
    b = np.zeros(n); np.add.at(b, pos, rng.choice([-1., 1.], cnt) * rng.uniform(.3, 1, cnt) * amp[pos])
    return b / np.sqrt((b ** 2).sum())

def place_tap(ir, src, delay, amp, x):
    """Filter src by the band gains amp, pan it to side x, add at delay."""
    L = len(ir); K2 = 1 << int(np.ceil(np.log2(len(src) + 8192))); s = np.zeros(K2); s[:len(src)] = src
    FK = np.log2(np.maximum(np.fft.rfftfreq(K2, 1 / SR), 20))
    k = np.roll(np.fft.irfft(np.fft.rfft(s) * curve(amp, FK), K2), PRE)
    gl, gr = pan(x); itd = int(abs(x) * .00066 * SR); i0 = int(round(delay * SR)) - PRE
    for ch, g, extra in ((0, gl, itd if x > 0 else 0), (1, gr, itd if x < 0 else 0)):
        j = i0 + extra
        if j < 0: continue
        m = min(K2, L - j)
        if m > 0: ir[j:j + m, ch] += g * k[:m]

FAR = 40.0
def build_ir(r, rng):
    hits, mirrors, pairs = r["hits"], r["mirrors"], r["pairs"]
    taps, tail, n_open = taps_and_tail(hits, mirrors, pairs)
    name = r["sc"].name
    clusters = {}
    for d, a, x, sm, kind, dens in taps:
        t = d * C / 2
        if kind == "scatter" and t <= FAR: continue                   # near scatter: the real tail has it
        # Wall-to-wall bounce trains repeat at an exact period (16.2 ms in the cellar): a buzz heard as "robot"
        # in test 4b. The real recordings carry their own, irregular flutter, so the trains are dropped.
        if kind == "bounce": continue
        if kind == "scatter" or (kind == "clean" and t > FAR):         # far: one continuous echo per surface type
            m = next(h[4] for h in hits if h[0] == "hit" and abs(h[2] - t) < 1e-6) if kind == "scatter" else \
                next(f["m"] for f in mirrors if abs(f["p"] - t) < 1e-6)
            g = clusters.setdefault((m, "far"), {"d": [], "e": [], "x": [], "m": m})
            g["d"].append(d); g["e"].append(a ** 2); g["x"].append(x)
        else:                                                          # near clean echoes and wall-to-wall bounces stay crisp
            clusters.setdefault(("near", len(clusters), d), {"near": (d, a, x)})
    L = int(4.5 * SR); ir = np.zeros((L, 2)); notes = []
    for key, g in clusters.items():
        if "near" in g:
            d, a, x = g["near"]; place_tap(ir, np.ones(1), d, a, x); continue
        d = np.array(g["d"]); e = np.array(g["e"]); w = e[:, MID] / e[:, MID].sum()
        decay = float(np.clip(MAT[g["m"]][2], 20, 60))                 # how long one patch of the surface keeps answering
        amp = np.sqrt(e.sum(0)) / TONE                                 # undo test 3's bass cut: far echoes keep their lows
        place_tap(ir, burst(d, e[:, MID], decay, rng), float(d.min()), amp, float((w * np.array(g["x"])).sum()))
        notes.append(f"{WORD[g['m']]} {d.min() * C / 2:.0f}-{d.max() * C / 2:.0f} m, one rolling echo "
                     f"{(d.max() - d.min()) * 1000 + 3 * decay:.0f} ms")
    T_info = None
    if name in REAL:
        irname, cut_ms, T_mid, shift, rel = REAL[name]
        if T_mid is None: T_mid = float(tail["t60"][MID]) if tail else 1.0
        rt, T_info, T_ir = real_tail(irname, cut_ms, T_mid, shift, L)
        e_rt = np.array([energy(b) for b in split8(rt)])
        if tail is not None and rel is None:
            want = tail["e"] * TONE ** 2                                # the rays' estimate of the tail's energy
        else:
            ref = sum(t[1] ** 2 for t in taps) if taps else np.full(8, 1e-4)
            want = ref * 10 ** ((rel or 0) / 10)
        g = np.sqrt(want[MID] / e_rt[MID])                             # one gain: keep the recording's own tone
        gl, gr = pan(.6 * tail["x"]) if tail else (1, 1)
        rt[:, 0] *= g * gl; rt[:, 1] *= g * gr; ir += rt
        notes.append(f"tail: real recording '{irname}', {T_info[MID]:.1f} s (recorded {T_ir:.1f} s)")
    if name in HORIZON:
        level = [h for h in hits if abs(h[1][2]) < .35]
        open_share = sum(1 for h in level if h[0] == "open") / max(len(level), 1)
        if open_share > .05:
            # far land answers from many distances at once: it rolls in over ~0.12 s (a sharp start popped). It starts
            # at 0.25 s: the MK V shot file stops at 0.37 s (-38 dB) because Ready or Not always layers an echo on it
            rt, _, _ = real_tail("koli s1", 80, 2.0, .25 - .08, L, rise=.12)   # ~600 m of air each way
            airg = 10 ** (-AIR * 1200 / 20)
            rt = sum(b * a for b, a in zip(split8(rt), airg))
            rt *= np.sqrt(open_share * 10 ** (HORIZON[name] / 10) / max(energy(rt), 1e-30) * 1e-3)
            ir += rt; notes.append(f"far land rumble from 0.25 s ({open_share * 100:.0f}% of the horizon open)")
    return ir, tail, notes, T_info

# ---------------- level tuning against Ready or Not ----------------
def split3(x):
    L = len(x); FL = np.log2(np.maximum(np.fft.rfftfreq(L, 1 / SR), 20)); X = np.fft.rfft(x, axis=0)
    def lowpart(fc): return np.clip((np.log2(fc) + .5 - FL), 0, 1)       # 1-octave cos^2 crossover
    lo300 = np.cos(np.pi / 2 * (1 - lowpart(300))) ** 2; lo2k = np.cos(np.pi / 2 * (1 - lowpart(2000))) ** 2
    return [np.fft.irfft(X * w[:, None], L, axis=0) for w in (lo300, lo2k - lo300, 1 - lo2k)]

def tune(echo, bang, target, t0, iters=30):
    """Smooth gain curve per band (straight lines between 9 points in time, no steps) so the echo's grid matches
    Ready or Not's within 3 dB. Each window moves the points inside it; a point on a window edge moves by the
    average of both windows. 0-50 ms windows Ready or Not leaves nearly empty only get a ceiling, and windows the
    rays left empty (nothing that far yet) stay empty."""
    parts = split3(echo); t = (np.arange(len(echo)) - t0) / SR
    KN = np.array([0, .025, .05, .175, .3, .65, 1.0, 1.5, 2.0])
    OWN = [[0], [0], [0, 1], [1], [1, 2], [2], [2, 3], [3], [3]]       # which windows each point belongs to
    def gcurve(row): return 10 ** (np.interp(t, KN, row) / 20)
    g0 = measure4.grid(echo, bang, t0)
    loudc = (target > -25) | ((np.arange(4) >= 1)[None, :] & (target > -50))   # after 50 ms the bang no longer hides it
    loudc &= g0 > g0.max(1, keepdims=True) - 40       # the rays left this window empty (nothing that far yet)
    G0 = 10 * np.log10(np.mean(10 ** (target[loudc] / 10)) / np.mean(10 ** (g0[loudc] / 10)))   # overall level first
    P = np.full((3, len(KN)), G0)
    def apply(P): return sum(p * gcurve(P[i])[:, None] for i, p in enumerate(parts))
    ceil = np.maximum(target + 6, -35)
    for _ in range(iters):
        g = measure4.grid(apply(P), bang, t0)
        delta = np.where(loudc, target - g, np.minimum(0, ceil - g))
        for i in range(3):                    # a window the rays left quiet moves with its nearest tuned neighbour,
            for j in range(1, 4):             # so the physics' own fall-off is kept (else the level fell off a cliff)
                if not loudc[i, j] and loudc[i].any():
                    near = min(np.where(loudc[i])[0], key=lambda q: (abs(q - j), -q))
                    delta[i, j] = min(target[i, near] - g[i, near], ceil[i, j] - g[i, j])
        step = np.stack([delta[:, o].mean(1) for o in OWN], 1)
        P = np.clip(P + .7 * step, G0 - 45, G0 + 40)
    y = apply(P); return y, measure4.grid(y, bang, t0), P, loudc

# ---------------- run every place ----------------
rng = np.random.default_rng(4)
if GUN == "mk5":
    fire = rd(f"{GRIZ}/Grizzly-1.wav")
    REF = {"Village yard": "EXT_Small", "Forest": "EXT_Base", "Field + tree line + hill": "EXT_Large",
           "House room": "INT_Small", "Stone cellar": "INT_Base", "Castle hall": "INT_Large"}
    def ron_file(v, take): return f"{GRIZ}/GRIZZLY_Reflection_{v}_0{take}.wav"
else:
    fire = rd(f"{RON}/Python_Fire_01.wav")
    REF = {"Village yard": "Ext_Small", "Forest": "Ext_Base", "Field + tree line + hill": "Ext_Large",
           "House room": "Int_Small", "Stone cellar": "Int_Base", "Castle hall": "Int_Big"}
    def ron_file(v, take): return f"{RON}/Python_Reflection_{v}_0{take}.wav"
colt_punch = cut(fire, .09, .13).mean(1)
ron = {k: rd(ron_file(v, 1)) for k, v in REF.items()}
t0_fire, t0_dry = measure4.onset(fire), measure4.onset(dry)
ron_grid = {k: measure4.grid(x, fire, t0_fire) for k, x in ron.items()}

results = []
for sc in SCENES:
    hits, mirrors, pairs, casts = survey(sc)
    r = dict(sc=sc, hits=hits, mirrors=mirrors, pairs=pairs, casts=casts)
    r["ir"], r["tail"], r["notes"], r["T"] = build_ir(r, rng)
    results.append(r)
    print(f"{sc.name:26s} " + " | ".join(r["notes"]), flush=True)

def wet_of(src, ir): return np.stack([conv(src, ir[:, 0]), conv(src, ir[:, 1])], 1)

# places Ready or Not doesn't have: one overall level, same rule as test 3 (loudness, echo vs bang)
ron_db = {k: db(loud(x) / loud(fire)) for k, x in ron.items()}
out_lvl = float(np.mean([ron_db[k] for k in ("Village yard", "Forest", "Field + tree line + hill")]))
OTHER_DB = {"Open field": out_lvl - 6, "Next to a cliff": out_lvl, "Ravine": out_lvl + 3,
            "Tunnel": ron_db["Castle hall"], "Round cave": ron_db["Castle hall"], "Bigger round cave": ron_db["Castle hall"] + 1}

print("\nGRID CHECK (echo vs bang, dB): rows low/mid/high, windows 0-50ms 50-300ms 0.3-1s 1-2s.  * = more than 3 dB off")
for r in results:
    name = r["sc"].name
    for tag, bang, src, t0 in (("colt", fire, colt_punch, t0_fire), ("deagle", dry, mono, t0_dry)):
        w = wet_of(src, r["ir"])
        if name in REF:
            w, g, P, chk = tune(w, bang, ron_grid[name], t0)
            tg = ron_grid[name]; bad = chk & (np.abs(g - tg) > 3)
            r[f"grid_{tag}"] = g; r[f"bad_{tag}"] = int(bad.sum()); r[f"n_{tag}"] = int(chk.sum())
            if tag == "colt":
                empty = int(((np.arange(4) >= 1)[None, :] & (tg > -50) & ~chk).sum())
                print(f"{name:26s} checked cells {chk.sum()}, off >3 dB: {bad.sum()}, worst {np.abs(np.where(chk, g - tg, 0)).max():4.1f} dB"
                      + (f", left empty by the rays: {empty}" if empty else ""))
                for i, bn in enumerate(("low", "mid", "high")):
                    print(f"   {bn:4s} ours " + " ".join(f"{v:6.1f}{'*' if bad[i, j] else ' '}" for j, v in enumerate(g[i]))
                          + "   RoN " + " ".join(f"{v:6.1f}" for v in tg[i]))
        elif energy(w) > 0:
            w *= 10 ** ((OTHER_DB[name] - db(loud(w) / loud(bang))) / 20)
        r[f"wet_{tag}"] = w

# ---------------- audibility: how much of the echo is not buried under the bang ----------------
def env10(x):
    n = int(.01 * SR); m = len(x) // n; return np.array([loud(x[i * n:(i + 1) * n]) + 1e-15 for i in range(m)])
def heard(bang, echo):
    n = max(len(bang), len(echo)); b, e = env10(pad(bang, n)), env10(pad(echo, n))
    return float(e[e > b * 10 ** (-.6)].sum() / e.sum())
print("\nHEARD (share of echo energy not buried under the bang) and loudness, ours vs Ready or Not, Colt")
for r in results:
    k = r["sc"].name
    if k in REF:
        print(f"  {k:26s} {heard(fire, r['wet_colt']) * 100:4.0f}%  {heard(fire, ron[k]) * 100:4.0f}%   "
              f"loudness ours {db(loud(r['wet_colt']) / loud(fire)):+.1f} dB, RoN {ron_db[k]:+.1f} dB")

# ---------------- write files ----------------
def trim(y, floor_db=-75):
    e = (y ** 2).sum(1); rem = np.cumsum(e[::-1])[::-1]; end = int(np.argmax(rem < rem[0] * 10 ** (floor_db / 10))) or len(y)
    end = max(end, len(dry)); y = y[:end].copy(); f = min(int(.05 * SR), end // 4)
    y[-f:] *= np.linspace(1, 0, f)[:, None]; return y
outs = []
for idx, r in enumerate(results, 1):
    w = r["wet_deagle"]; r["file"] = f"{idx:02d} {r['sc'].name}.wav"
    outs.append((r["file"], trim(pad(dry, len(w)) + w)))
blind = []
GNAME, RNAME = ("mk5", "Ready or Not MK V") if GUN == "mk5" else ("colt", "Ready or Not")
for k, v in REF.items():
    r = next(x for x in results if x["sc"].name == k)
    takes = [(1, fire, r["wet_colt"], ron[k])]
    if GUN == "mk5":                                                   # take 2: another shot, Ready or Not's 2nd echo
        f2 = rd(f"{GRIZ}/Grizzly-2.wav"); e2 = rd(ron_file(v, 2)); t2 = measure4.onset(f2)
        w2, g2, _, _ = tune(wet_of(cut(f2, .09, .13).mean(1), r["ir"]), f2, measure4.grid(e2, f2, t2), t2)
        takes.append((2, f2, w2, e2))
    for take, shot, ours, theirs in takes:
        pair = []
        for tag, e in (("ours", ours), ("ron", theirs)):
            n = max(len(shot), len(e)); f = f"blind test files/{GNAME} {tag} {v}{' take 2' if take == 2 else ''}.wav"
            outs.append((f, trim(pad(shot, n) + pad(e, n)))); pair.append(f)
        blind.append({"place": k + (" (take 2)" if take == 2 else ""), "kind_a": "new", "a": pair[0], "kind_b": "ron",
                      "b": pair[1], "b_label": f"{RNAME} {v}"})
T3 = "/mnt/c/Users/<user>/Desktop/Deagle Progress/10 Gun sound test 3"
a_t3 = rd(f"{T3}/01 Open field.wav")                                  # match test 3's bang level
g_t3 = np.sqrt(energy(a_t3[:int(.1 * SR)]) / energy(dry[:int(.1 * SR)]))
for k in (() if GUN == "mk5" else ("Village yard", "Forest", "Field + tree line + hill", "Next to a cliff", "Stone cellar", "Castle hall")):
    src = next(f for f in os.listdir(T3) if f.endswith(f" {k}.wav"))
    f = f"blind test files/test3 {k}.wav"; outs.append((f, trim(rd(f"{T3}/{src}") / g_t3)))
    blind.append({"place": k, "kind_a": "new", "a": next(r["file"] for r in results if r["sc"].name == k),
                  "kind_b": "old", "b": f, "b_label": "test 3"})
scale = .95 / max(np.abs(y).max() for _, y in outs)
for f, y in outs:
    w = wave.open(f"{OUT}/{f}", "wb"); w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR)
    w.writeframes((np.clip(y * scale, -1, 1) * 32767).astype(np.int16).tobytes()); w.close()

# ---------------- check for cut-outs: sudden drops or jumps in level (50 ms steps) ----------------
print("\nCUT-OUT CHECK (ours only; drops > 15 dB or rises > 8 dB within 50 ms, while above -50 dB, after 0.1 s)")
for f, y in outs:
    if "test3" in f or " ron " in f: continue      # (cut-out check covers ours only)
    n = int(.05 * SR); m = len(y) // n; e = 10 * np.log10((y[:m * n] ** 2).reshape(m, n, 2).sum((1, 2)) + 1e-20); e -= e.max()
    bad = [(round(i * .05, 2), round(e[i] - e[i - 1])) for i in range(3, m - 1) if e[i - 1] > -50 and (e[i] - e[i - 1] < -15 or e[i] - e[i - 1] > 8)]
    print(f"  {f:44s} {'ok' if not bad else bad}")

places = []
for r in results:
    k = r["sc"].name; lines = []
    ms = sorted(r["mirrors"], key=lambda f: f["p"])
    lines.append("Clean echoes: " + (", ".join(f"{WORD[f['m']]} {f['p']:.0f} m" for f in ms[:4]) or "none")
                 + (f" +{len(ms) - 4} more" if len(ms) > 4 else ""))
    far = [n for n in r["notes"] if "rolling" in n]; rest = [n for n in r["notes"] if "rolling" not in n]
    if far: lines.append("Far echoes: " + "; ".join(far))
    if rest: lines.append(" · ".join(rest))
    if k in REF: lines.append(f"Tuned to Ready or Not's {REF[k]}: {r['n_colt'] - r['bad_colt']} of {r['n_colt']} checked cells within 3 dB")
    places.append({"file": r["file"], "name": k, "note": r["sc"].note, "lines": lines})
json.dump({"places": places, "blind": blind}, open(f"{OUT}/manifest.json", "w"), indent=1)
print("wrote", len(outs), "wavs")
