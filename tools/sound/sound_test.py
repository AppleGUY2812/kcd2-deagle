"""Listening pack: Deagle bang with its baked echo cut off, plus swapped-in echoes (Colt / generated)."""
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
rng = np.random.default_rng(7)

def rd(p):
    w = wave.open(p); sr = w.getframerate(); ch = w.getnchannels()
    x = np.frombuffer(w.readframes(w.getnframes()), np.int16).astype(float).reshape(-1, ch) / 32768
    if ch == 1: x = np.repeat(x, 2, 1)
    if sr != SR:   # FFT resample per channel
        n = int(round(len(x) * SR / sr)); X = np.fft.rfft(x, axis=0); m = n // 2 + 1
        Y = np.zeros((m, 2), complex); k = min(m, len(X)); Y[:k] = X[:k]
        x = np.fft.irfft(Y, n, axis=0) * n / len(x)
    return x

def wr(name, x):
    pk = np.abs(x).max()
    if pk > 0.98: x = x * 0.98 / pk
    w = wave.open(os.path.join(OUT, name), "wb"); w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR)
    w.writeframes((np.clip(x, -1, 1) * 32767).astype(np.int16).tobytes()); w.close()

def lowpass(x, fc, order=2):
    n = len(x); f = np.fft.rfftfreq(n, 1 / SR); X = np.fft.rfft(x, axis=0)
    return np.fft.irfft(X * (1 / np.sqrt(1 + (f / fc) ** (2 * order)))[:, None], n, axis=0)

def conv(a, b):
    n = len(a) + len(b) - 1; N = 1 << (n - 1).bit_length()
    return np.fft.irfft(np.fft.rfft(a, N, axis=0) * np.fft.rfft(b, N, axis=0), N, axis=0)[:n]

def pad(x, n): return np.vstack([x, np.zeros((max(0, n - len(x)), 2))])[:n]

def env_db(x, t0, t1):   # mean energy in a window, dB relative to the 10 ms peak
    win = int(.01 * SR); e = np.convolve((x ** 2).mean(1), np.ones(win) / win, "same")
    return 10 * np.log10(e[int(t0 * SR):int(t1 * SR)].mean() / e.max())

def add_echo(dry, echo, target_db, at=0.0):
    """mix the echo in so the 0.5-2.5 s energy matches target_db (the original Deagle file's own echo level)"""
    L = max(len(dry), int(at * SR) + len(echo)); out = pad(dry, L); e = np.zeros((L, 2))
    e[int(at * SR):int(at * SR) + len(echo)] = echo
    lo, hi = 0.0, 64.0
    for _ in range(40):   # bisect the echo gain
        g = (lo + hi) / 2
        lo, hi = (g, hi) if env_db(out + g * e, .5, 2.5) < target_db else (lo, g)
    return out + g * e

orig = rd(f"{DE}/decho-1.wav")
target = env_db(orig, .5, 2.5)
print(f"original echo level {target:.1f} dB")

# Deagle bang with the baked echo cut off: full to 0.30 s, cosine fade to silence by 0.45 s
fade = np.ones(len(orig)); a, b = int(.30 * SR), int(.45 * SR)
fade[a:b] = 0.5 * (1 + np.cos(np.linspace(0, np.pi, b - a))); fade[b:] = 0
cut = (orig * fade[:, None])[:b]

colt_ext = rd(f"{RON}/Python_Reflection_Ext_Base_01.wav")
# Colt echo darkened toward KCD2's forest reverb: highs cut at 1.1 kHz, extra decay so it dies in ~1.7 s
t = np.arange(len(colt_ext)) / SR
colt_forest = lowpass(colt_ext, 1100) * np.exp(-t * 6.91 / 1.7 * 0.6)[:, None]

# Generated forest echo with KCD2's rvb_outdoor_forest_01 numbers: early reflections at 160 ms,
# late reverb 88 ms later, 1.7 s decay, highs cut at 1.1 kHz. Decorrelated noise per ear = wide, diffuse.
n = int(2.6 * SR); ir = np.zeros((n, 2)); tt = np.arange(n) / SR
for d in rng.uniform(.160, .248, 14):   # sparse early taps
    ir[int(d * SR)] += rng.uniform(.15, .4, 2) * rng.choice([-1, 1], 2)
late0 = int(.248 * SR); late = rng.standard_normal((n - late0, 2))
late *= np.exp(-(tt[late0:] - .248) * 6.91 / 1.7)[:, None] * np.minimum(1, (tt[late0:] - .248) / .03)[:, None]
ir[late0:] += late * 0.12
ir = lowpass(ir, 1100)
made_forest = conv(cut, ir)

colt_bang = rd(f"{RON}/Python_Fire_01.wav")
colt_bang *= np.abs(cut).max() / np.abs(colt_bang).max()   # same peak as the Deagle

# Open field with bounce-back echoes (what step 4 adds): a tree line 60 m away and a hill 240 m away.
# Each echo = the bang, muffled and quieter, delayed by 2 x distance / 343 m/s.
field = add_echo(cut, colt_ext, target - 3)
for dist, gain, fc in [(60, .30, 2500), (240, .16, 1200)]:
    d = int(2 * dist / 343 * SR); slap = lowpass(cut, fc) * gain
    field = pad(field, max(len(field), d + len(slap))); field[d:d + len(slap)] += slap

wr("1 Deagle now (echo baked in).wav", orig)
wr("2 Deagle cut - no echo.wav", cut)
wr("3 Deagle cut + Colt outdoor echo.wav", add_echo(cut, colt_ext, target))
wr("4 Deagle cut + Colt echo darkened for forest.wav", add_echo(cut, colt_forest, target))
wr("5 Deagle cut + made forest echo (KCD2's own settings).wav", add_echo(cut, made_forest, target))   # made_forest is wet only (the IR has no direct sound)
wr("6 Colt bang + Colt outdoor echo (for comparison).wav", add_echo(colt_bang, colt_ext, target))
wr("7 Deagle cut + field echo + tree line 60m + hill 240m.wav", field)
for f in sorted(os.listdir(OUT)):
    x = rd(os.path.join(OUT, f)); print(f"{f:62s} {len(x)/SR:4.2f}s  peak {np.abs(x).max():.2f}  echo {env_db(x,.5,2.5):6.1f} dB")
