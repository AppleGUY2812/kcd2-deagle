#!/usr/bin/env python3
"""How loud a bullet hitting steel is next to the gunshot, in real life vs in the mod, as a listening pack.

Real life (no published measurement of bullet-on-steel loudness exists, so the hit side is physics):
- shot: A-weighted sound exposure at the shooter's ear, Meinke et al. 2014 (Int J Audiol 53 S16, Table 4, NIOSH):
  .44 Magnum 87.7 dB LAeq8 = 132.3 dB SEL_A, .357 Magnum 88.5 = 133.1 (SEL = LAeq8 + 10 log10(28800 s)).
  .50 AE (more powder than either) 134; the 9 mm 6.5 dB under it (the mod's own blast law, calibers.lua).
- hit: the bullet splashes on the plate and hands it its momentum J = m v in about bullet length / v ("Ballistic
  Impacts with Bullet Splash", PMC10254373, 2023, .308 on 8 mm hard steel: force integral = m v, pulse 36-49 us). Energy into the plate's bending waves =
  J^2 / pulse x the plate's point mobility 1 / (8 sqrt(B m'')); a free plate radiates most of it as sound (radiation
  vs damping loss factors), spread over a sphere, A-weighted, air loss per ISO 9613-1 (20 C, 50 %).
In the mod: Ready or Not's own events rendered by its engine (tools/ron/FmodRender, final fader 0.5 like the game);
the hit's volume = what the plugin plays (cfg impactVolume x Falloff), or the real-life level vs the shot.

Writes WAVs + index.html to Desktop\\Deagle Progress\\15 Metal hit loudness. usage: impact_level_ab.py [outdir]"""
import html, os, re, subprocess, sys
import numpy as np, soundfile as sf
from scipy.signal import bilinear, lfilter

EXE = "/mnt/e/Tools/deagle-work/ron/fmodrender/bin/FmodRender.exe"
BANKS = r"C:\Program Files (x86)\Steam\steamapps\common\Ready Or Not\ReadyOrNot\Content\FMOD\Desktop"
DESK = "/mnt/c/Users/<user>/Desktop/Deagle Progress/15 Metal hit loudness"
RM = "event:/Levels/Reverbs/ReverbMaster"
IMP = "event:/Surfaces/Impacts/Bullet Impacts/Bullet Impact Surfaces"
HIT = ["Surface=44", "Occlusion=0", "PropagateDistance=0"]          # Steel, what mat_metal plays
BLOCK = 256
LATENCY = 10 * BLOCK                                                # FMOD offline output lag (ron_part_transients.py)
IMPACT_VOLUME, IMPACT_NEAR = 2.0, 3.0                               # deagle_main.lua cfg now

# bullet kg, m/s (calibers.lua, the Glock's 4.5" barrel), bullet length m, shot SEL_A at the ear, in-game shot volume
GUNS = {"deagle": ("Deagle", "event:/Weapons/Grizzly/1P Grizzly Fire", 0.01944, 442, 0.0225, 134.0, 1.0),
        "glock": ("Glock", "event:/Weapons/G19 New/1P G19 Fire", 0.00804, 360, 0.0156, 127.5, 0.83)}
PLATES = {"hanging": ("a hanging steel plate (rings)", 0.0095, 3e-4), "bolted": ("bolted-down steel (duller)", 0.0095, 3e-3)}

# ---- the physics
RHO0, C0 = 1.2, 343.0
E0 = 4e-10 / (RHO0 * C0)          # J/m^2 = 0 dB sound exposure
AIR = {125: .0004, 250: .001, 500: .0019, 1000: .0037, 2000: .0097, 4000: .0328, 8000: .117, 16000: .38}

def a_db(f):
    f2 = f * f
    ra = 12194 ** 2 * f2 * f2 / ((f2 + 20.6 ** 2) * np.sqrt((f2 + 107.7 ** 2) * (f2 + 737.9 ** 2)) * (f2 + 12194 ** 2))
    return 20 * np.log10(ra) + 2.0

def hit_vs_shot_db(gun, d, plate):
    _, _, m, v, length, shot_sel, _ = GUNS[gun]
    _, h, eta_d = PLATES[plate]
    f = np.linspace(20, 20000, 20000)
    B, mpp = 200e9 * h ** 3 / (12 * (1 - 0.3 ** 2)), 7850 * h
    into = 2 * (m * v) ** 2 * np.sinc(f * length / v) ** 2 / (8 * np.sqrt(B * mpp)) * (f[1] - f[0])
    fc = C0 ** 2 / (2 * np.pi) * np.sqrt(mpp / B)
    eta_rad = 2 * RHO0 * C0 * np.where(f >= fc, 1.0, 0.1 * np.sqrt(f / fc)) / (2 * np.pi * f * mpp)
    sound = into * eta_rad / (eta_rad + eta_d)
    air = np.interp(np.log(f), np.log(list(AIR)), list(AIR.values())) * d
    w = (sound * 10 ** ((a_db(f) - air) / 10)).sum()
    return 10 * np.log10(w / (4 * np.pi * d * d) / E0) - shot_sel

# ---- Ready or Not's engine
def a_filter(sr):
    f1, f2, f3, f4 = 20.598997, 107.65265, 737.86223, 12194.217
    den = np.polymul([1, 4 * np.pi * f4, (2 * np.pi * f4) ** 2], [1, 4 * np.pi * f1, (2 * np.pi * f1) ** 2])
    den = np.polymul(np.polymul(den, [1, 2 * np.pi * f3]), [1, 2 * np.pi * f2])
    return bilinear([(2 * np.pi * f4) ** 2 * 10 ** (1.9997 / 20), 0, 0, 0, 0], den, sr)

def render(out_win, segments):
    """segments: [(secs until the next one starts, [args])]; the first one's args start with its event."""
    args = [EXE, "render", BANKS, out_win, segments[0][1][0], f"{segments[0][0]:.4f}"] + segments[0][1][1:]
    for secs, a in segments[1:]: args += ["+", f"{secs:.4f}"] + a
    env = dict(os.environ, WSLENV="FMOD_EXTRA_BANKS:FMOD_MASTER_GAIN:FMOD_BLOCK", FMOD_MASTER_GAIN="0.5", FMOD_BLOCK=str(BLOCK),
               FMOD_EXTRA_BANKS="Interactions.bank;SWAT.bank;Global Amb.bank;UI.bank")
    for _ in range(4):   # WSL's Windows interop sometimes drops a launch under load
        r = subprocess.run(args, capture_output=True, text=True, env=env, timeout=900)
        if r.returncode == 0: break
    if r.returncode: raise SystemExit("render failed: " + (r.stdout + r.stderr)[-400:])
    return [int(s) + LATENCY for s in re.findall(r"shot starts at block \d+ = sample (\d+)", r.stdout)]

def shot_args(gun, vol=None):
    g = GUNS[gun]
    return ["ev:" + g[1], "IsOutside=1", f"vol:{vol if vol is not None else g[6]}"]

def sel_a(x, sr, b, a, start, secs):
    seg = x[start: start + int(secs * sr)]
    y = lfilter(b, a, seg.mean(1) if seg.ndim > 1 else seg)
    return 10 * np.log10((y ** 2).sum() / sr + 1e-20)

def calibrate(work_win, work):
    """bang = the shot's first 150 ms (its echo layers start later), hit = the whole steel hit; both at volume 1."""
    n = 8
    segs = [(4.0, shot_args(g, 1.0)) for g in GUNS for _ in range(n)] + [(2.5, ["ev:" + IMP] + HIT) for _ in range(n)]
    segs[0] = (4.0, ["event:/Weapons/Grizzly/1P Grizzly Fire"] + segs[0][1][1:] + ["global:ReverbSTREET=1", "start:" + RM])
    starts = render(work_win + r"\calib.wav", segs)
    x, sr = sf.read(work + "/calib.wav"); b, a = a_filter(sr)
    lv = [sel_a(x, sr, b, a, s, .15 if i < 2 * n else 2.5) for i, s in enumerate(starts)]
    cal = {g: float(np.mean(lv[i * n:(i + 1) * n])) for i, g in enumerate(GUNS)}
    cal["hit"] = float(np.mean(lv[2 * n:]))
    return cal

def main():
    out = sys.argv[1] if len(sys.argv) > 1 else DESK
    out_win = subprocess.run(["wslpath", "-w", out], capture_output=True, text=True).stdout.strip() if out.startswith("/mnt/") else out
    os.makedirs(out, exist_ok=True)
    cal = calibrate(out_win, out)
    os.remove(out + "/calib.wav")
    print("calibration, SEL_A dBFS at volume 1:", {k: round(v, 1) for k, v in cal.items()})
    cases = [("deagle", 4), ("deagle", 10), ("deagle", 25), ("glock", 10)]
    rows = []
    # first, each sound on its own, so you know what to listen for
    solo = [("Deagle", "-", "the metal hit alone (what to listen for), as loud as the mod plays it up close", "metal_hit_alone.wav",
             [(1.5, [IMP] + HIT + [f"vol:{IMPACT_VOLUME}"])] + [(1.5, ["ev:" + IMP] + HIT + [f"vol:{IMPACT_VOLUME}"])] * 2),
            ("Deagle", "-", "the gunshot alone (bang + Ready or Not's echo)", "gunshot_alone.wav",
             [(3.6, [GUNS["deagle"][1], "IsOutside=1", "global:ReverbSTREET=1", "start:" + RM])] + [(3.6, shot_args("deagle"))] * 2)]
    for gun, d, label, name, segs in solo:
        starts = render(out_win + "\\" + name, segs)
        x, sr = sf.read(out + "/" + name)
        sf.write(out + "/" + name, x[max(0, starts[0] - sr // 20):], sr, subtype="PCM_16")
        rows.append((gun, d, label, None, None, name))
    for gun, d in cases:
        for mode in ("now", "hanging", "bolted"):
            if mode == "bolted" and d != 10: continue
            if mode == "now":
                vol = IMPACT_VOLUME * IMPACT_NEAR / max(d, IMPACT_NEAR)
                game_shot = 20 * np.log10(GUNS[gun][6])
                rel = cal["hit"] + 20 * np.log10(vol) - (cal[gun] + game_shot)
                label = "in the mod now"
            else:
                rel = hit_vs_shot_db(gun, d, mode)
                vol = 10 ** ((rel + cal[gun] + 20 * np.log10(GUNS[gun][6]) - cal["hit"]) / 20)
                label = "real life: " + PLATES[mode][0]
            name = f"{GUNS[gun][0].lower()}_{d}m_{mode}.wav"
            segs = []
            for _ in range(3):   # shot, the hit after the sound's travel time, then the shot's tail
                segs += [(d / C0, shot_args(gun)), (3.6, ["ev:" + IMP] + HIT + [f"vol:{vol:.5f}"])]
            segs[0] = (d / C0, [GUNS[gun][1]] + segs[0][1][1:] + ["global:ReverbSTREET=1", "start:" + RM])
            starts = render(out_win + "\\" + name, segs)
            x, sr = sf.read(out + "/" + name)          # cut the engine's silent warm-up
            x = x[max(0, starts[0] - sr // 20):]
            sf.write(out + "/" + name, x, sr, subtype="PCM_16")
            rows.append((GUNS[gun][0], d, label, rel, vol, name))
            print(f"{name:28s} hit vs shot {rel:+6.1f} dB  (event volume {vol:.4f})")
    trs = "".join(f"<tr class='{'solo' if r[3] is None else 'now' if 'now' in r[2] else 'real'}'><td>{r[0]}</td>"
                  f"<td>{'' if r[3] is None else str(r[1]) + ' m'}</td><td>{html.escape(r[2])}</td>"
                  f"<td>{'' if r[3] is None else f'{r[3]:+.0f} dB'}</td><td><audio controls preload='none' src='{r[5]}'></audio></td></tr>" for r in rows)
    page = f"""<!doctype html><html><head><meta charset="utf-8"><title>Metal hit loudness</title><style>
body{{font:15px system-ui;margin:16px;background:#111;color:#eee;max-width:900px}} table{{border-collapse:collapse;width:100%}}
td,th{{border-bottom:1px solid #333;padding:6px 8px;text-align:left}} tr.now td{{background:#2b1d1d}} tr.solo td{{background:#1d222b}} audio{{height:30px}}</style>
</head><body><h1>Metal hit: how loud next to the gunshot</h1>
<p>Blue rows: each sound alone. Play the metal hit first so you know what it sounds like. Then the rest: 3 shots
outdoors, each followed by that metal hit at that distance. Red rows = the mod now. The others = the hit at its
real-life loudness next to the same gunshot. If you can't hear the hit in a file, that's the result: the gunshot's
echo covers it (picture: what you hear.png). Ready or Not's engine made every file. Set your volume once on the first
file and don't touch it.</p>
<p>Real life: gunshot at the shooter's ear measured by NIOSH (Meinke et al. 2014); the hit is physics (the round's
momentum into the steel plate, the plate's ring, spread with distance), about +-6 dB. Nobody has published a
measurement of it.</p>
<table><tr><th>gun</th><th>steel at</th><th>level</th><th>hit vs shot</th><th></th></tr>{trs}</table></body></html>"""
    open(out + "/index.html", "w").write(page)
    print("->", out + "/index.html")

if __name__ == "__main__":
    main()
