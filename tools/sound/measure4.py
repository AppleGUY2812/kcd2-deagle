"""Shared measuring for test 4: echo energy per band x time window, relative to the bang's first 50 ms."""
import numpy as np, wave
SR = 44100
BANDS3 = [("low", 0, 300), ("mid", 300, 2000), ("high", 2000, 22050)]
WINS = [("0-50ms", 0, .05), ("50-300ms", .05, .3), ("0.3-1s", .3, 1), ("1-2s", 1, 2)]

def rd(p):
    w = wave.open(p); sr = w.getframerate(); ch = w.getnchannels(); sw = w.getsampwidth()
    raw = w.readframes(w.getnframes())
    if sw == 2: x = np.frombuffer(raw, np.int16).astype(float) / 32768
    elif sw == 3:
        b = np.frombuffer(raw, np.uint8).reshape(-1, 3)
        x = (b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8) | (b[:, 2].astype(np.int32) << 16))
        x = np.where(x >= 1 << 23, x - (1 << 24), x).astype(float) / (1 << 23)
    else: x = np.frombuffer(raw, np.int32).astype(float) / 2 ** 31
    x = x.reshape(-1, ch)
    if ch == 1: x = np.repeat(x, 2, 1)
    x = x[:, :2]
    if sr != SR:
        n = int(round(len(x) * SR / sr)); X = np.fft.rfft(x, axis=0); mm = n // 2 + 1
        Y = np.zeros((mm, 2), complex); k = min(mm, len(X)); Y[:k] = X[:k]; x = np.fft.irfft(Y, n, axis=0) * n / len(x)
    return x

def onset(x, frac=.1): a = np.abs(x).max(1); return int(np.argmax(a > frac * a.max()))

def band(x, lo, hi):
    X = np.fft.rfft(x, axis=0); f = np.fft.rfftfreq(len(x), 1 / SR); X[(f < lo) | (f >= hi)] = 0
    return np.fft.irfft(X, len(x), axis=0)

def grid(echo, bang, t0=None):
    """dB of echo energy per (band, window) vs the bang's energy in the same band, first 50 ms. t0 = bang onset sample."""
    if t0 is None: t0 = onset(bang)
    out = np.zeros((3, 4))
    for i, (_, lo, hi) in enumerate(BANDS3):
        eb = band(echo, lo, hi) ** 2; bb = band(bang, lo, hi) ** 2
        ref = bb[t0:t0 + int(.05 * SR)].sum()
        for j, (_, a, z) in enumerate(WINS):
            s = eb[t0 + int(a * SR): t0 + int(z * SR)].sum()
            out[i, j] = 10 * np.log10(s / ref + 1e-12)
    return out
