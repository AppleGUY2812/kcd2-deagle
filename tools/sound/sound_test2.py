"""Test pack 2: fuller Deagle bangs + one generated echo per KCD2 place (KCD2's own FMOD reverb snapshot values,
research_notes/KCD2 gun sound propagation/kcd2_existing_audio.md section 3.2)."""
import os, sys, wave, numpy as np
import argparse
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
ap.add_argument("out", help="output folder for the WAVs (e.g. Desktop/Deagle Progress/<NN> Gun sound test <n>)")
ap.add_argument("--decho", default=os.path.join(REPO, "assets/build/sounds/decho"),
                help="folder with the Deagle shot decho-1.wav (Call of Duty, gitignored build input)")
ap.add_argument("--ron", default="/mnt/e/Tools/deagle-work/sound/ron_python",
                help="Ready or Not Colt Python WAVs (Python_Fire_*, Python_Reflection_*), made by tools/ron/fsbx.py --wav")
args = ap.parse_args()

SR = 44100
DE, RON, OUT = args.decho, args.ron, args.out
os.makedirs(OUT, exist_ok=True)
rng = np.random.default_rng(11)

def rd(p):
    w = wave.open(p); sr = w.getframerate(); ch = w.getnchannels()
    x = np.frombuffer(w.readframes(w.getnframes()), np.int16).astype(float).reshape(-1, ch) / 32768
    if ch == 1: x = np.repeat(x, 2, 1)
    if sr != SR:
        n = int(round(len(x) * SR / sr)); X = np.fft.rfft(x, axis=0); m = n // 2 + 1
        Y = np.zeros((m, 2), complex); k = min(m, len(X)); Y[:k] = X[:k]
        x = np.fft.irfft(Y, n, axis=0) * n / len(x)
    return x

def lowpass(x, fc, order=2):
    n = len(x); f = np.fft.rfftfreq(n, 1 / SR)
    return np.fft.irfft(np.fft.rfft(x, axis=0) * (1 / np.sqrt(1 + (f / fc) ** (2 * order)))[:, None], n, axis=0)

def conv(a, b):
    n = len(a) + len(b) - 1; N = 1 << (n - 1).bit_length()
    return np.fft.irfft(np.fft.rfft(a, N, axis=0) * np.fft.rfft(b, N, axis=0), N, axis=0)[:n]

def pad(x, n): return np.vstack([x, np.zeros((max(0, n - len(x)), 2))])[:n]
def energy(x): return float((x ** 2).sum())

def cut(x, full, end):   # keep x to `full` s, cosine fade to silence at `end` s
    a, b = int(full * SR), int(end * SR); g = np.ones(b); g[a:b] = 0.5 * (1 + np.cos(np.linspace(0, np.pi, b - a)))
    return x[:b] * g[:, None]

def make_ir(T60, early_ms=20, late_ms=40, hf_cut=None, echo=None, hf_ratio=0.5, hf_ref=5000):
    """FMOD SFX Reverb-like: sparse early taps after early_ms, diffuse late tail after early+late ms that decays
    in T60 (highs above hf_ref decay in T60*hf_ratio, FMOD's default 50%), optional high cut, optional FMOD Echo."""
    n = int(min(4.5, T60 * 1.4 + 0.4) * SR); t = np.arange(n) / SR; ir = np.zeros((n, 2))
    e0 = early_ms / 1000; l0 = e0 + late_ms / 1000; span = max(late_ms, 15) / 1000
    for d in np.sort(rng.uniform(e0, e0 + span, 14)):
        ir[int(d * SR)] += rng.uniform(.25, .6, 2) * rng.choice([-1, 1], 2) * 10 ** (-3 * (d - e0) / T60)
    noise = rng.standard_normal((n, 2)); lo = lowpass(noise, hf_ref, 4); hi = noise - lo
    tl = np.clip(t - l0, 0, None)
    late = lo * np.exp(-tl * 6.91 / T60)[:, None] + hi * np.exp(-tl * 6.91 / (T60 * hf_ratio))[:, None]
    late *= ((t >= l0) * np.minimum(1, tl / 0.02))[:, None]
    ir += late * 0.08
    if hf_cut: ir = lowpass(ir, hf_cut, 2)
    if echo:   # FMOD Echo: repeats every delay_ms, each one feedback% of the last
        dms, fb, wet = echo; D = int(dms / 1000 * SR); y = ir.copy(); k, g = 1, wet
        while g > 1e-3 and k * D < n:
            y[k * D:] += g * ir[:n - k * D]; k += 1; g *= fb
        ir = y
    return ir / np.sqrt(energy(ir))

def t60(wet):   # Schroeder backward integration, -5 to -25 dB fit, x3
    e = (wet ** 2).mean(1)[::-1].cumsum()[::-1]; db = 10 * np.log10(e / e[0] + 1e-30)
    a, b = np.argmax(db < -5), np.argmax(db < -25)
    return 3 * (b - a) / SR if b > a else float("nan")

orig = rd(f"{DE}/decho-1.wav")
short = cut(orig, .30, .45)              # old test 2
longer = cut(orig, .55, .80)             # keeps the Deagle's own boom
colt = rd(f"{RON}/Python_Fire_01.wav")
on = lambda x: int(np.argmax(np.abs(x).max(1) > .1 * np.abs(x).max()))   # onset sample
colt = pad(np.vstack([np.zeros((max(0, on(longer) - on(colt)), 2)), colt[max(0, on(colt) - on(longer)):]]), len(longer))
thump = lowpass(colt, 150, 4); thump *= np.sqrt(energy(lowpass(longer, 150, 4)) / energy(thump))
thumped = longer + thump
b = int(.3 * SR); thumped *= np.sqrt(energy(longer[:b]) / energy(thumped[:b]))   # same loudness as A2, so louder doesn't win by default

# Outdoor echo level: the original CoD file's own echo-to-bang energy ratio. Indoors +6 dB (close walls = loud echo).
R = energy(orig[int(.45 * SR):]) / energy(orig[:int(.45 * SR)])
places = [   # name, IR, level vs outdoor (dB). Numbers are KCD2's snapshot values where the notes have them.
    ("Forest (KCD2 forest_01 - 1.7s, 160+88ms, highs cut 1.1kHz)", make_ir(1.7, 160, 88, 1100, (5.2, .085, .3)), 0),
    ("Open field (KCD2 outdoor bus - 1.5s + 360ms echo 43pct)", make_ir(1.5, echo=(360, .43, .5)), 0),
    ("Village yard (KCD2 yard_01 - 0.9s, 20+25ms, 4.8kHz, 72ms echo)", make_ir(0.9, 20, 25, 4800, (72, .11, .4)), -3),
    ("House indoors (KCD2 house_01 - 0.55s, highs cut 700Hz)", make_ir(0.55, 5, 10, 700), 6),
    ("Cellar (KCD2 cellar - 1.2s, highs cut 1.7kHz)", make_ir(1.2, 8, 12, 1700), 6),
    ("Cave (KCD2 cave_IR1 - 1.9s, 24ms echo 22pct)", make_ir(1.9, 15, 20, None, (24, .22, .4)), 6),
]
files = [("A1 bang - short cut (old test 2)", short), ("A2 bang - longer cut", longer),
         ("A3 bang - longer cut + Colt low thump", thumped)]
dry = longer
for i, (name, ir, db) in enumerate(places, 1):
    wet = conv(dry, ir); wet *= np.sqrt(R * 10 ** (db / 10) * energy(dry) / energy(wet))
    files.append((f"B{i} {name}", pad(dry, len(wet)) + wet, wet))
# field + bounce-backs: tree line 60 m, hill 240 m (delay = 2 x distance / 343 m/s, quieter + duller)
field = files[4][1].copy()
for dist, gain, fc in [(60, .30, 2500), (240, .16, 1200)]:
    d = int(2 * dist / 343 * SR); s = lowpass(dry, fc) * gain
    field = pad(field, max(len(field), d + len(s))); field[d:d + len(s)] += s
files.append(("B7 Open field + tree line 60m + hill 240m", field))

scale = 0.98 / max(np.abs(f[1]).max() for f in files)   # one gain for the whole set: same bang loudness everywhere
for f in files:
    x = f[1] * scale
    w = wave.open(os.path.join(OUT, f[0] + ".wav"), "wb"); w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR)
    w.writeframes((np.clip(x, -1, 1) * 32767).astype(np.int16).tobytes()); w.close()
    extra = f"  echo decay {t60(f[2]):.2f}s" if len(f) > 2 else ""
    print(f"{f[0]:68s} {len(x)/SR:4.2f}s  peak {np.abs(x).max():.2f}{extra}")
print(f"set gain {20*np.log10(scale):+.1f} dB, outdoor echo/bang energy {10*np.log10(R):.1f} dB")

# ---- C1-C6: pro reference, Ready or Not's Colt bang + its own echo files, the Colt bang as loud as our A2 bang.
# 9f made these with a separate inline step right after (2026-10-12 17:57 UTC) into the same folder; folded in here so
# one run rebuilds everything make_player.py (PLAY ME.html) expects. Private use: these are Ready or Not sounds.
def rd16(p):
    w = wave.open(p); return np.frombuffer(w.readframes(w.getnframes()), np.int16).reshape(-1, 2) / 32768.
a2 = rd16(os.path.join(OUT, "A2 bang - longer cut.wav")); b = int(.3 * SR)
fire = rd(f"{RON}/Python_Fire_01.wav")
g = np.sqrt(energy(a2[:b]) / energy(fire[:b]))
sets = [("Ext_Small", "outdoor small space"), ("Ext_Base", "outdoor medium space"), ("Ext_Large", "outdoor big open space"),
        ("Int_Small", "indoor small room"), ("Int_Base", "indoor medium room"), ("Int_Big", "indoor big hall")]
outs = []
for i, (tag, desc) in enumerate(sets, 1):
    ref = rd(f"{RON}/Python_Reflection_{tag}_01.wav"); n = max(len(fire), len(ref))
    outs.append((f"C{i} RoN Colt - {desc} (Ready or Not's own {tag} echo, pro reference)", (pad(fire, n) + pad(ref, n)) * g))
pk = max(np.abs(x).max() for _, x in outs)
if pk > 0.98: outs = [(n_, x * 0.98 / pk) for n_, x in outs]; print("scaled down", round(20 * np.log10(0.98 / pk), 1), "dB")
for name, x in outs:
    w = wave.open(os.path.join(OUT, name + ".wav"), "wb"); w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR)
    w.writeframes((np.clip(x, -1, 1) * 32767).astype(np.int16).tobytes()); w.close()
    print(f"{name[:40]:40s} {len(x)/SR:.2f}s peak {np.abs(x).max():.2f}")
