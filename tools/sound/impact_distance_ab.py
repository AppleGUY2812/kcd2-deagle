#!/usr/bin/env python3
"""Bullet hits and ricochets far from the listener: the mod now vs with air absorption, as a listening pack.

Now: the plugin's Falloff law, inverse distance from impactNear (3 m) out, with Ready or Not's own fade off
(PropagateDistance 0). So a hit 200 m away plays Ready or Not's point-blank sound, only quieter: as bright as one
next to you. ("I can hear the ricochet from my freecam way farther than I'm supposed to", the user, 2026-10-14.)
With air: the same level law, plus the air's absorption over the distance (ISO 9613-1, AIR_T deg C, AIR_RH % humidity),
which takes the highs first: 100 m costs ~0.4 dB at 1 kHz, ~3 dB at 4 kHz, ~12 dB at 8 kHz.
The filter is the one the plugin will use (air_fir: linear phase, frequency-sampled from ISO 9613-1).
Ready or Not's events rendered by its engine (tools/ron/FmodRender, final fader 0.5 like the game) at the game's
instance volume for each distance; the air filter applied to that render.

Writes WAVs + index.html to Desktop\\Deagle Progress\\18 Hits far away. usage: impact_distance_ab.py [outdir]"""
import html, os, sys
import numpy as np, soundfile as sf
from scipy.signal import fftconvolve
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from impact_level_ab import IMP, RM, SHOT, render   # noqa: E402

DESK = "/mnt/c/Users/<user>/Desktop/Deagle Progress/18 Hits far away"
RIC = "event:/Surfaces/Impacts/Bullet Impacts/Ricochets"
STEEL = [IMP, "Surface=44", "Occlusion=0", "PropagateDistance=0"]
RICO = [RIC, "Occlusion=0", "PropagateDistance=0"]
IMPACT_VOL, NEAR = 2.0, 3.0            # deagle_main.lua cfg: impactVolume, impactNear
RICO_VOL = 2.0                         # cfg.ricochet.volume (x a fresh round's HitLoudness 1)
AIR_T, AIR_RH = 15.0, 70.0             # a mild summer day
TAPS = 255
DISTS = (10, 25, 50, 100, 200)
GAP = 1.6                              # s between the 3 hits in a file

def air_db_per_m(f, t=AIR_T, rh=AIR_RH, pa=101.325):
    """ISO 9613-1 pure-tone air absorption, dB per metre."""
    pr, T, T0, T01 = 101.325, t + 273.15, 293.15, 273.16
    h = rh * 10 ** (-6.8346 * (T01 / T) ** 1.261 + 4.6151) * pr / pa
    frO = pa / pr * (24 + 4.04e4 * h * (0.02 + h) / (0.391 + h))
    frN = pa / pr * (T / T0) ** -0.5 * (9 + 280 * h * np.exp(-4.170 * ((T / T0) ** (-1 / 3) - 1)))
    f2 = np.asarray(f, float) ** 2
    return 8.686 * f2 * (1.84e-11 * pr / pa * (T / T0) ** 0.5 + (T / T0) ** -2.5 * (
        0.01275 * np.exp(-2239.1 / T) / (frO + f2 / frO) + 0.1068 * np.exp(-3352.0 / T) / (frN + f2 / frN)))

def air_fir(d, sr, taps=TAPS):
    """Linear-phase FIR with the air's loss over d metres: frequency-sampled, Hann-windowed (the plugin's design)."""
    n = taps + 1
    H = 10 ** (-air_db_per_m(np.fft.rfftfreq(n, 1 / sr)) * d / 20)
    h = np.roll(np.fft.irfft(H, n), n // 2)[1:]          # centred, odd length (symmetric)
    return h * np.hanning(taps + 2)[1:-1]

def with_air(x, sr, d):
    h = air_fir(d, sr)
    y = np.stack([fftconvolve(x[:, c], h)[TAPS // 2: TAPS // 2 + len(x)] for c in range(x.shape[1])], 1) if x.ndim > 1 \
        else fftconvolve(x, h)[TAPS // 2: TAPS // 2 + len(x)]
    return y

def render_hits(out, out_win, name, ev, vol):
    gap = 3.6 if ev[0] == SHOT else GAP
    segs = [(gap, ev + [f"vol:{vol:.5f}"])] + [(gap, ["ev:" + ev[0]] + [a for a in ev[1:] if ":" not in a] + [f"vol:{vol:.5f}"])] * 2
    starts = render(out_win + "\\" + name, segs)
    x, sr = sf.read(out + "/" + name)
    x = x[max(0, starts[0] - sr // 20):]
    sf.write(out + "/" + name, x, sr, subtype="PCM_16")
    return x, sr

def main():
    out = sys.argv[1] if len(sys.argv) > 1 else DESK
    import subprocess
    out_win = subprocess.run(["wslpath", "-w", out], capture_output=True, text=True).stdout.strip()
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(out):
        if f.endswith(".wav"): os.remove(os.path.join(out, f))
    rows = []
    name = "gunshot_alone.wav"   # the Deagle's shot as you hear it (Ready or Not's MK V, outdoors), for the level
    render_hits(out, out_win, name, [SHOT, "IsOutside=1", "global:ReverbSTREET=1", "start:" + RM], 1.0)
    rows.append(("Your gunshot (for the level)", "the Deagle's shot as you hear it", name, ""))
    for what, ev, base in (("Ricochet", RICO, IMPACT_VOL * RICO_VOL), ("Steel hit", STEEL, IMPACT_VOL)):
        key = what.split()[0].lower()
        name = f"{key}_03m.wav"
        render_hits(out, out_win, name, ev, base)
        rows.append((what, "3 m: full level (set your volume here once)", name, ""))
        for d in DISTS:
            vol = base * NEAR / d
            now = f"{key}_{d:03d}m_now.wav"
            x, sr = render_hits(out, out_win, now, ev, vol)
            air = f"{key}_{d:03d}m_air.wav"
            sf.write(out + "/" + air, with_air(x, sr, d), sr, subtype="PCM_16")
            loss = ", ".join(f"{k} kHz -{air_db_per_m(k * 1000) * d:.1f}" for k in (1, 4, 8))
            rows.append((what, f"{d} m, the mod NOW ({20 * np.log10(vol):+.1f} dB, as bright as up close)", now, "now"))
            rows.append((what, f"{d} m, WITH AIR (same level, air takes {loss} dB)", air, "match"))
            print(f"{what} {d:3d} m vol {vol:.4f} air {loss}")
    trs, sec = [], None
    for s, label, f, cls in rows:
        if s != sec: trs.append(f"<tr><th colspan=2>{html.escape(s)}</th></tr>"); sec = s
        trs.append(f"<tr class='{cls}'><td>{html.escape(label)}</td><td><audio controls preload='none' src='{f}'></audio></td></tr>")
    page = f"""<!doctype html><html><head><meta charset="utf-8"><title>Hits far away</title><style>
body{{font:15px system-ui;margin:16px;background:#111;color:#eee;max-width:900px}} table{{border-collapse:collapse;width:100%}}
td,th{{border-bottom:1px solid #333;padding:6px 8px;text-align:left}} th{{padding-top:18px;color:#9cf}}
tr.now td{{background:#2b1d1d}} tr.match td{{background:#1d2b1d}} audio{{height:30px}}</style></head><body>
<h1>Hits and ricochets far away</h1>
<p>Set your volume once on the gunshot and don't touch it. A fresh ricochet 3 m away plays 4 dB louder than your
own shot, 25 m away 5 dB under it, 100 m away 17 dB under it (the 3 m files touch Ready or Not's limiter). Every file: the same sound 3 times.
Red = the mod now: quieter with distance, but as bright as up close. Green = the same level plus what the air takes
on the way (the highs go first, ISO 9613-1, {AIR_T:.0f} &deg;C, {AIR_RH:.0f} % humidity). Ready or Not's engine made
every file; the ricochet is a fresh round's (Claude 1 already makes spent rounds quieter).</p>
<table>{''.join(trs)}</table></body></html>"""
    open(out + "/index.html", "w").write(page)
    print("->", out + "/index.html")

if __name__ == "__main__":
    main()
