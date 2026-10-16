#!/usr/bin/env python3
"""A steel plate's ring lifted out of a range recording (Hickok45), to replace Ready or Not's steel hit.

A struck plate rings at a set of steady pitches (its modes): they start together at the strike and fade over ~0.3-1.5 s.
Each one is found as a narrow peak that holds its pitch right after the strike, and kept with a narrow zero-phase
band-pass of the original until the last time it stands GATE dB over the bands beside it. The gunshot, its echo, his
voice and the music spread over every pitch, so almost none of them gets through; steady background tones (there
before the shot) are excluded. The plate's own echo is at its pitches and partly comes along.
Past that cut a tone still rings, under the echo: it's followed in a band NB Hz either side of its exact pitch (5-10x
less echo) until it last stands GATE dB over narrow bands beside it. A plate's takes share its pitches, so each take
also gets the ones its own search missed but the other takes found (plate_tones). Plate A, 2026-10-16: 0.6-5.2 % of
its ring had been left out (the user heard it in "WITHOUT"), now 0.01-0.2 % (33-50 dB under the plate).
Hits on the same plate find the same pitches (Sunday Shoot-a-Round #351: 3 plates over 10 hits).
Why not SAM Audio: its laugh + music removal on hit_01 flattened 16 of the plate's 18 pitches from 10-26 dB over their
neighbours to 1-6 dB (it regenerates the audio). A fit of decaying tones failed too: the pitches beat and the echo
and voice overlap them (no clean decay to fit).
Writes ring_NN.wav (the plate: its ring + the strike's clank and early shimmer) and left_out_NN.wav (the rest after the strike, same gain) for checking by ear.
Private use only: the WAVs go to E:\\Tools\\deagle-work\\sound\\hickok\\rings, never into the repo.

usage: plate_rings.py <recording.wav> <hits.json> [outdir] [only index]
hits.json: [{"shot": s, "onset": s after the shot}, ...] (made by the plate finder, multi-tone rings after shots)"""
import json, os, sys
import numpy as np, soundfile as sf
from scipy.signal import butter, sosfiltfilt, welch
from scipy.ndimage import median_filter

OUT = "/mnt/e/Tools/deagle-work/sound/hickok/rings"
LO, HI = 700, 16000        # Hz searched for the plate's tones
MAX_TONES = 48
RING_LEN = 1.6             # s kept after the onset
SIDE, GATE = 80.0, 6.0     # Hz to the bands a tone is compared with; dB it must stand over them
TAILS, NB = True, 4.0       # follow each tone past its cut in a band NB Hz either side of its pitch (ring())
# the strike's clank and the plate's early shimmer between its pitches, from the original: above SHIMMER_HZ (below
# it nothing rises at the strike, it's the gun's echo), CLANK_HOLD s from the strike, faded out over CLANK_FADE s.
# Measured on hit 01 against 7 shots with no plate: 2-16 kHz stands 4-8 dB over the gun's echo for ~0.3 s, then level
CLANK_HOLD, CLANK_FADE, SHIMMER_HZ = .1, .15, 1500.0
CLANK_FULL = 0.012        # s of the clank kept full-band (the user picked this, pack 20 "B"); its body
# below SHIMMER_HZ: 300-1500 Hz rises 3-5 dB at the strike

def tones(x, sr, shot, onset):
    """Frequencies (Hz) of the plate's tones: narrow peaks that stand >= 10 dB over their neighbours at the same pitch
    in two windows (strike +0.02..0.2 s and +0.15..0.45 s: a plate's tone holds its pitch, a flicker of the gun's echo
    doesn't) and >= 12 dB over the same band before the shot."""
    n = 4096
    def psd(a, b):
        seg = x[int((shot + onset + a) * sr): int((shot + onset + b) * sr)]
        f, P = welch(seg, sr, nperseg=n, noverlap=n * 3 // 4)
        return f, 10 * np.log10(P + 1e-20)
    pre = x[int((shot - .7) * sr): int((shot - .03) * sr)]
    f, B = welch(pre, sr, nperseg=n, noverlap=n * 3 // 4); Bd = 10 * np.log10(B + 1e-20)
    _, R1 = psd(.02, .2); _, R2 = psd(.15, .45)
    p1, p2 = R1 - median_filter(R1, size=31), R2 - median_filter(R2, size=31)
    near = lambda p: np.maximum.reduce([np.roll(p, k) for k in (-1, 0, 1)])   # same pitch within one bin (12 Hz)
    ok = (f > LO) & (f < HI) & (p1 > 10) & (near(p2) > 10) & (R1 - Bd > 12)
    peaks = [i for i in np.nonzero(ok)[0] if R1[i] == R1[max(0, i - 2): i + 3].max()]
    peaks.sort(key=lambda i: -R1[i])
    out = []
    for i in peaks:
        if all(abs(f[i] - g) > 25 for g in out): out.append(float(f[i]))
        if len(out) == MAX_TONES: break
    return sorted(out)

def more_tones(x, sr, shot, onset, freqs):
    """Plate tones the first pass missed: next to a stronger tone a weaker one doesn't stand 10 dB over a 360 Hz median,
    so the rest left after taking the found ones is searched again (the user heard plate tones in left_out, pack 19).
    A tone here: >= 10 dB over a 160 Hz median at +0.03-0.3 s, >= 6 dB at the same pitch at +0.3-0.7 s, >= 10 dB over
    the band before the shot. On 12 shots with no plate this finds nothing (2026-10-15); on hits 1-10 it found 1-15
    each, repeating on hits of the same plate (plate C: 2391, 3035, 3680, 8379, 8730, 10148, 11285 Hz)."""
    if not freqs: return []
    _, rest, _ = ring(x, sr, shot, onset, freqs)
    n = 4096
    def psd(seg):
        f, P = welch(seg, sr, nperseg=min(n, len(seg)), noverlap=min(n, len(seg)) * 3 // 4)
        return f, 10 * np.log10(P + 1e-20)
    f, A = psd(rest[int(.03 * sr): int(.3 * sr)]); _, B = psd(rest[int(.3 * sr): int(.7 * sr)])
    _, P = psd(x[int((shot - .7) * sr): int((shot - .03) * sr)])
    w = max(3, int(160 / (f[1] - f[0])) | 1)
    a, b = A - median_filter(A, size=w, mode="nearest"), B - median_filter(B, size=w, mode="nearest")
    near = lambda p: np.maximum.reduce([np.roll(p, k) for k in (-1, 0, 1)])
    ok = (f > LO) & (f < HI) & (a >= 10) & (near(b) >= 6) & (A - near(P) >= 10)
    out = []
    for i in np.nonzero(ok)[0]:
        if a[i] == a[max(0, i - 2): i + 3].max() and all(abs(f[i] - g) > 25 for g in list(freqs) + out): out.append(float(f[i]))
    return sorted(out)

def all_tones(x, sr, shot, onset):
    """tones() plus the ones it missed (more_tones)."""
    fr = tones(x, sr, shot, onset)
    return sorted(fr + more_tones(x, sr, shot, onset, fr))

def env(y, sr):
    """Level of a narrow band over time, smoothed over 20 ms."""
    w = int(.02 * sr)
    return np.sqrt(np.convolve(y * y, np.ones(w) / w, "same")) + 1e-12

def band(seg, sr, fq):
    bw = max(18.0, fq * .004)
    return sosfiltfilt(butter(3, [fq - bw, fq + bw], "bandpass", fs=sr, output="sos"), seg)

def over(e, side, sr):
    """dB a band stands over its side bands, averaged over 150 ms (a beat's dip doesn't end a tone)."""
    w = int(.15 * sr)
    return 20 * np.log10(np.sqrt(np.convolve(e ** 2, np.ones(w) / w, "same")) / np.sqrt(np.convolve(side ** 2, np.ones(w) / w, "same")))

def narrow(seg, sr, fq):
    return sosfiltfilt(butter(2, [fq - NB, fq + NB], "bandpass", fs=sr, output="sos"), seg)

def pitch(seg, sr, fq, a, b):
    """A tone's exact pitch (the finder's is to 12 Hz): the peak of a long zero-padded spectrum of its band from a to
    b, interpolated."""
    y = band(seg, sr, fq)[a:b] * np.hanning(b - a)
    n = 1 << 20
    S, f = np.abs(np.fft.rfft(y, n)), np.fft.rfftfreq(n, 1 / sr)
    m = np.nonzero((f > fq - 30) & (f < fq + 30))[0]
    i = m[np.argmax(S[m])]
    l, c, r = np.log(S[i - 1: i + 2] + 1e-20)
    return f[i] + (l - r) / (2 * (l - 2 * c + r)) * (f[1] - f[0])

def rings_at(x, sr, shot, onset, freqs):
    """Of a plate's known pitches (from all its takes), the ones that ring in this take: >= 10 dB over a 360 Hz median
    at +0.02-0.3 s and over the same band before the shot. A take's own search misses some of them (plate A: hit 1 rang
    at 5 of hits 4 and 6's pitches it hadn't found, hit 4 at 2, hit 6 at 2); on 7 shots with no plate 0-1 of plate A's
    63 pitches pass (2026-10-16)."""
    n = 4096
    def psd(a, b):
        f, P = welch(x[int(a * sr): int(b * sr)], sr, nperseg=n, noverlap=n * 3 // 4)
        return f, 10 * np.log10(P + 1e-20)
    f, A = psd(shot + onset + .02, shot + onset + .3)
    _, P = psd(shot - .7, shot - .03)
    loc = A - median_filter(A, size=31)
    out = []
    for fq in freqs:
        i = int(round(fq / (f[1] - f[0])))
        j = i - 1 + int(np.argmax(A[i - 1: i + 2]))
        if loc[j] >= 10 and A[j] - P[j] >= 10: out.append(fq)
    return out

def plate_tones(takes, sr):
    """Tones of each take of one plate [(x, onset), ...] (shot at 1.0 s): its own (all_tones) plus the plate's pitches
    found on its other takes that ring in it too (rings_at)."""
    own = [all_tones(x, sr, 1.0, o) for x, o in takes]
    known = []
    for fr in own:
        for fq in fr:
            if all(abs(fq - g) > 25 for g in known): known.append(fq)
    out = []
    for (x, o), fr in zip(takes, own):
        add = [fq for fq in rings_at(x, sr, 1.0, o, known) if all(abs(fq - g) > 25 for g in fr)]
        out.append(sorted(fr + add))
    return out

def clear(fq, freqs, sign):
    """The nearest pitch SIDE Hz or more to one side of fq with no plate tone within 40 Hz of it."""
    d = SIDE
    while any(abs(fq + sign * d - g) < 40 for g in freqs): d += 20
    return fq + sign * d

def ring(x, sr, shot, onset, freqs):
    """The plate's tones from the strike on. Each one is kept only while it stands GATE dB over what sits next to it
    (two bands SIDE Hz either side: the gun's echo, voice, music spread over every pitch), averaged over 150 ms,
    until the last time it does, so none of that leaks in after the ring has faded into it. Plus the strike's clank.
    Returns the plate and the rest (gun / echo / voice / music, to check by ear), both from 2 ms before the strike,
    and where that is in x."""
    a = int((shot + onset - .01) * sr)
    lead = int(.2 * sr)
    seg = x[a - lead: a + int(RING_LEN * sr)]
    y = np.zeros(len(seg))
    for fq in freqs:
        b = band(seg, sr, fq)
        side = np.maximum(env(band(seg, sr, clear(fq, freqs, -1)), sr), env(band(seg, sr, clear(fq, freqs, 1)), sr))
        on = np.nonzero(over(env(b, sr), side, sr)[lead:] > GATE)[0]
        if not len(on): continue
        end = lead + on[-1]                                    # the last time it stood out: kept until then
        g = np.zeros(len(seg)); g[:end] = 1
        fo = int(.08 * sr); g[end: end + fo] = np.linspace(1, 0, len(g[end: end + fo]))
        y += b * g
        if not TAILS: continue
        # past the cut the tone rings on under the echo (the user heard it in "left out"): followed in a band NB Hz
        # either side of its exact pitch, which holds 5-10x less of the echo, until the last time it stands GATE dB
        # over narrow bands beside it; faded in as the wide band fades out (one tone, so the two add up)
        f1 = pitch(seg, sr, fq, lead + int(.02 * sr), max(end, lead + int(.25 * sr)))
        nb = narrow(seg, sr, f1)
        nside = np.maximum(env(narrow(seg, sr, clear(f1, freqs, -1)), sr), env(narrow(seg, sr, clear(f1, freqs, 1)), sr))
        on = np.nonzero(over(env(nb, sr), nside, sr)[end:] > GATE)[0]
        if not len(on) or on[-1] <= fo: continue
        e2 = end + on[-1]
        t = np.zeros(len(seg)); t[end: end + fo] = np.linspace(0, 1, fo); t[end + fo: e2] = 1
        t[e2: e2 + fo] = np.linspace(1, 0, len(t[e2: e2 + fo]))
        y += nb * t
    # the strike itself: the sharpest rise of the level over 2 kHz from 10 ms before the finder's onset to 30 ms after. The clank and
    # shimmer there are spread over every pitch like the gun's echo, but louder for a while: the original's non-tonal
    # part over SHIMMER_HZ is taken for CLANK_HOLD s from the strike, faded out over CLANK_FADE s
    hp = sosfiltfilt(butter(4, 2000, "high", fs=sr, output="sos"), seg)
    m = int(.001 * sr)
    lvl = 10 * np.log10(np.convolve(hp * hp, np.ones(m) / m, "same") + 1e-20)
    s0 = lead + int(.01 * sr)
    back = int(.005 * sr)
    cand = range(s0 - int(.01 * sr), s0 + int(.03 * sr))   # not earlier: the camera's gain coming back after the bang
    st = max(cand, key=lambda i: lvl[i] - lvl[i - back: i].min()) - m // 2
    w = np.zeros(len(seg))
    r1, h1, f1 = int(.001 * sr), int(CLANK_HOLD * sr), int(CLANK_FADE * sr)
    w[st - r1: st] = .5 - .5 * np.cos(np.linspace(0, np.pi, r1)); w[st: st + h1] = 1
    w[st + h1: st + h1 + f1] = .5 + .5 * np.cos(np.linspace(0, np.pi, f1))
    y[: st - r1] = 0; y[st - r1: st] *= np.linspace(0, 1, r1)  # the ring starts at the strike
    clank = sosfiltfilt(butter(4, SHIMMER_HZ, "high", fs=sr, output="sos"), seg - y) * w
    if CLANK_FULL > 0:                                       # the first CLANK_FULL s full-band, faded over 3 ms
        wf = np.zeros(len(seg)); n1, f3 = int(CLANK_FULL * sr), int(.003 * sr)
        wf[st - r1: st] = w[st - r1: st]; wf[st: st + n1] = 1; wf[st + n1: st + n1 + f3] = np.linspace(1, 0, f3)
        clank = clank * (1 - wf) + (seg - y) * wf
    out = st - 2 * r1
    plate = (y + clank)[out:]
    tail = int(.3 * sr); plate[-tail:] *= np.linspace(1, 0, tail) ** 2
    return plate, seg[out:] - plate, a - lead + out

def main():
    src, hits_json = sys.argv[1], sys.argv[2]
    out = sys.argv[3] if len(sys.argv) > 3 else OUT
    only = int(sys.argv[4]) if len(sys.argv) > 4 else None
    os.makedirs(out, exist_ok=True)
    sr = sf.info(src).samplerate
    hits = json.load(open(hits_json))
    meta = []
    for i, h in enumerate(hits):
        if only is not None and i != only: continue
        x, _ = sf.read(src, start=int((h["shot"] - 1) * sr), stop=int((h["shot"] + h["onset"] + RING_LEN + .5) * sr))
        x = x.mean(1) if x.ndim > 1 else x
        h = dict(h, shot=1.0)                                   # times in this slice
        fr = all_tones(x, sr, h["shot"], h["onset"])
        if len(fr) < 3: print(f"hit {i + 1}: only {len(fr)} tones, skipped"); continue
        y, rest, _ = ring(x, sr, h["shot"], h["onset"], fr)
        k = 0.89 / np.abs(y).max()
        name = f"ring_{i + 1:02d}.wav"
        sf.write(os.path.join(out, name), y * k, sr, subtype="PCM_16")
        sf.write(os.path.join(out, f"left_out_{i + 1:02d}.wav"), np.clip(rest * k, -1, 1), sr, subtype="PCM_16")
        meta.append(dict(file=name, shot=hits[i]["shot"], onset=h["onset"], tones=[round(v) for v in fr]))
        print(f"{name}: {len(fr)} tones {[round(v) for v in fr]}")
    json.dump(meta, open(os.path.join(out, "rings.json"), "w"), indent=1)

if __name__ == "__main__":
    main()
