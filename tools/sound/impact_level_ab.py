#!/usr/bin/env python3
"""How loud a bullet hitting steel is next to the gunshot: the mod now vs matched to real recordings, as a listening
pack with the recording itself on the page.

Real recordings (Hickok45, camera at the shooter, outdoors; MODLOG 2026-10-14): steel dings 0.4-1.3 s after the shot
(roughly 80-250 m) peak 7-15 dB under the bang (5 ms peak level; median -7.5 dB over 25 dings in "Desert Eagle 50 AE",
-9 in the user's Sunday Shoot-a-Round #351 clip) and stand out of the range's echo. Ears and microphones squash the
bang, so this, not the sound pressure ratio at the ear, is what a hit should sound like next to the shot.
Close up no clean recording was found (the ding lands inside the bang); there the hit peaks like the bang, which is
also Ready or Not's own mix at point blank (5 ms peaks: MK V bang -10.2 dBFS, steel hit -11.3).
Matched law: hit volume = NEAR_VOL within NEAR_M, then x (NEAR_M / d) ^ FADE: -8 dB vs the bang at 175 m.
Ready or Not's own events rendered by its engine (tools/ron/FmodRender, final fader 0.5 like the game).

Writes WAVs + index.html to Desktop\\Deagle Progress\\15 Metal hit loudness. usage: impact_level_ab.py [outdir] [clip.mp3]"""
import html, os, re, subprocess, sys
import numpy as np, soundfile as sf

EXE = "/mnt/e/Tools/deagle-work/ron/fmodrender/bin/FmodRender.exe"
BANKS = r"C:\Program Files (x86)\Steam\steamapps\common\Ready Or Not\ReadyOrNot\Content\FMOD\Desktop"
DESK = "/mnt/c/Users/<user>/Desktop/Deagle Progress/15 Metal hit loudness"
CLIP = "/mnt/c/Users/<user>/Downloads/Sunday Shoot-a-Round # 351_8_50.mp3"
# Hickok45 "Desert Eagle 50 AE" (youtu.be/nb7tWcohYSY, audio fetched with yt-dlp): 734.5-744.5 s = 3 shots that all
# ring the same far plate (a 5.5 kHz tone 0.92-0.96 s later, like 25 shots in the video; misses leave no tone)
DE = "/mnt/e/Tools/deagle-work/sound/hickok/nb7tWcohYSY.wav"
RM = "event:/Levels/Reverbs/ReverbMaster"
IMP = "event:/Surfaces/Impacts/Bullet Impacts/Bullet Impact Surfaces"
SHOT = "event:/Weapons/Grizzly/1P Grizzly Fire"
HIT = ["Surface=44", "Occlusion=0", "PropagateDistance=0"]          # Steel, what mat_metal plays
BLOCK = 256
LATENCY = 10 * BLOCK                                                # FMOD offline output lag (ron_part_transients.py)
C0, BULLET = 343.0, 400.0                                           # m/s: sound; a .50 AE's average speed downrange
NOW_VOL, NOW_NEAR = 2.0, 3.0                                        # deagle_main.lua cfg now: impactVolume, impactNear
NEAR_VOL, NEAR_M, FADE = 1.1, 3.0, 0.227                            # matched to the recordings

def now_vol(d): return NOW_VOL * NOW_NEAR / max(d, NOW_NEAR)
def matched_vol(d): return NEAR_VOL * (NEAR_M / max(d, NEAR_M)) ** FADE

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

def trimmed(out, name, starts):
    x, sr = sf.read(out + "/" + name)          # cut the engine's silent warm-up
    sf.write(out + "/" + name, x[max(0, starts[0] - sr // 20):], sr, subtype="PCM_16")

def main():
    out = sys.argv[1] if len(sys.argv) > 1 else DESK
    clip = sys.argv[2] if len(sys.argv) > 2 else CLIP
    out_win = subprocess.run(["wslpath", "-w", out], capture_output=True, text=True).stdout.strip()
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(out):
        if f.endswith(".wav"): os.remove(os.path.join(out, f))
    rows = []   # (section, label, file)
    if os.path.exists(DE):
        subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-ss", "734.5", "-t", "10", "-i", DE, out + "/hickok45_deagle_3_hits.wav"], check=True)
        rows.append(("Real recordings", "Hickok45, Desert Eagle .50 AE: 3 shots, every one hits a steel plate far out. The DING comes about 1 s after each shot", "hickok45_deagle_3_hits.wav"))
    if os.path.exists(clip):   # its first shot rings a plate about 1 s later (the second one is less clear)
        subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-ss", "1.15", "-t", "2.4", "-i", clip, out + "/hickok45_your_clip.wav"], check=True)
        rows.append(("Real recordings", "Hickok45, your clip: one shot, the DING about 1 s later", "hickok45_your_clip.wav"))
    name = "metal_hit_alone.wav"
    trimmed(out, name, render(out_win + "\\" + name, [(1.5, [IMP] + HIT + [f"vol:{NOW_VOL}"])] + [(1.5, ["ev:" + IMP] + HIT + [f"vol:{NOW_VOL}"])] * 2))
    rows.append(("Each sound alone", "the metal hit alone (what to listen for)", name))
    name = "gunshot_alone.wav"
    trimmed(out, name, render(out_win + "\\" + name, [(3.6, [SHOT, "IsOutside=1", "global:ReverbSTREET=1", "start:" + RM])] + [(3.6, ["ev:" + SHOT, "IsOutside=1"])] * 2))
    rows.append(("Each sound alone", "the gunshot alone (bang + Ready or Not's echo)", name))
    for d in (175, 25, 4):
        for mode, vol in (("now", now_vol(d)), ("matched", matched_vol(d))):
            name = f"deagle_{d}m_{mode}.wav"
            delay = d / BULLET + d / C0            # the round flies there, the sound comes back
            segs = []
            for _ in range(3):
                segs += [(delay, ["ev:" + SHOT, "IsOutside=1"]), (3.6, ["ev:" + IMP] + HIT + [f"vol:{vol:.5f}"])]
            segs[0] = (delay, [SHOT, "IsOutside=1", "global:ReverbSTREET=1", "start:" + RM])
            trimmed(out, name, render(out_win + "\\" + name, segs))
            label = ("the mod NOW" if mode == "now" else "MATCHED to the recordings") + f": steel {d} m away"
            rows.append((f"Steel {d} m away" + (" (like the clip)" if d == 175 else ""), label, name))
            print(f"{name:26s} hit volume {vol:.3f} ({20 * np.log10(vol):+.1f} dB)")
    trs, sec = [], None
    for s, label, f in rows:
        if s != sec: trs.append(f"<tr><th colspan=2>{html.escape(s)}</th></tr>"); sec = s
        cls = "now" if "NOW" in label else "match" if "MATCHED" in label else ""
        trs.append(f"<tr class='{cls}'><td>{html.escape(label)}</td><td><audio controls preload='none' src='{f}'></audio></td></tr>")
    page = f"""<!doctype html><html><head><meta charset="utf-8"><title>Metal hit loudness</title><style>
body{{font:15px system-ui;margin:16px;background:#111;color:#eee;max-width:900px}} table{{border-collapse:collapse;width:100%}}
td,th{{border-bottom:1px solid #333;padding:6px 8px;text-align:left}} th{{padding-top:18px;color:#9cf}}
tr.now td{{background:#2b1d1d}} tr.match td{{background:#1d2b1d}} audio{{height:30px}}</style></head><body>
<h1>Metal hit: how loud next to the gunshot</h1>
<p>Set your volume once on the first file and don't touch it. Red = the mod now. Green = matched to real recordings
(Hickok45: steel far away dings about 8 dB under the bang). Every file: 3 shots, each followed by the metal hit after
the bullet's flight and the sound's way back. Ready or Not's engine made every game file.</p>
<table>{''.join(trs)}</table></body></html>"""
    open(out + "/index.html", "w").write(page)
    print("->", out + "/index.html")

if __name__ == "__main__":
    main()
