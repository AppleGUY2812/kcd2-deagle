"""Load real impulse responses (OpenAIR, York), decode B-format to stereo, measure them.
The recordings are not in git: download with tools/sound/get_openair.sh (default folder below, or $OPENAIR)."""
import os
from math import gcd
import numpy as np, soundfile as sf
from scipy.signal import resample_poly, butter, sosfiltfilt
SR = 44100
ROOT = os.environ.get("OPENAIR", "/mnt/e/Tools/deagle-work/sound/openair")

def load(rel, deg=60):
    x, sr = sf.read(f"{ROOT}/{rel}", always_2d=True)
    if sr != SR: g = gcd(SR, sr); x = resample_poly(x, SR // g, sr // g, axis=0)
    if x.shape[1] == 4:                                   # B-format WXYZ (FuMa): two virtual cardioids at +-deg
        W, X, Y = x[:, 0] * np.sqrt(2), x[:, 1], x[:, 2]; c, s = np.cos(np.radians(deg)), np.sin(np.radians(deg))
        x = np.stack([.5 * (W + c * X + s * Y), .5 * (W + c * X - s * Y)], 1)
    elif x.shape[1] == 1: x = np.repeat(x, 2, 1)
    elif x.shape[1] > 2: x = x[:, :2]
    return x

def direct(x): a = np.abs(x).max(1); return int(np.argmax(a > .25 * a.max()))

OCT = [125, 250, 500, 1000, 2000, 4000, 8000]
def octave(x, fc):
    lo, hi = fc / np.sqrt(2), min(fc * np.sqrt(2), SR / 2 * .95)
    return sosfiltfilt(butter(4, [lo, hi], "band", fs=SR, output="sos"), x, axis=0)
