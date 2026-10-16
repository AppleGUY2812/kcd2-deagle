#!/usr/bin/env python3
"""The steel hit the game plays: plate A of Sunday Shoot-a-Round #351 (hits 1, 4, 6: three takes of one plate), lifted
by plate_rings.py, as WAVs for the plugin (assets/build/sounds/steel, installed to Mods\\deagle\\sounds\\steel), and the
volume that puts them next to Ready or Not's MK V shot as in the clip (the user's pick, pack 20 "B").

Level: each take at hit 1's level over 2 kHz in its first 0.3 s (the camera's gain differs from shot to shot). In the
clip, plate A is DIST m from the camera and over 2 kHz as loud as the gun's echo 0.07-0.11 s after the bang times
RATIO; the game shot's echo there is measured on Ready or Not's own render (its engine, final fader 0.5 like the game,
3 shots). The plugin plays a file at vol x the master (0.5 x the mod's volume), with Falloff=impactNear (inverse
distance from 3 m), and HitSound sends impactVolume x the surface's vol: so
    vol = v_at_DIST x DIST / impactNear / impactVolume,   v_at_DIST = plate / file x scale / 0.5.
Private use only: the WAVs never go into the repo.

usage: plate_sounds.py <recording.wav> <hits.json> [outdir]   (hits.json: page16_hits.json, the numbering of pack 16)"""
import json, os, subprocess, sys, tempfile
import numpy as np, soundfile as sf
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import plate_rings as pr                                            # noqa: E402
from impact_level_ab import RM, SHOT, render                        # noqa: E402

HITS = (1, 4, 6)
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../assets/build/sounds/steel")
MATCH_HZ = 2000.0
IMPACT_VOL, NEAR = 2.0, 3.0            # deagle_main.lua cfg: impactVolume, impactNear
PAN = 0.7071                           # a centred file reaches each ear at this (FMOD's constant-power pan; measured
                                       # through the plugin, native/test/ron_coexist): pack 20 played it at 1 in both
BULLET = 400.0                         # m/s downrange (a pistol round); with 343 m/s back: the plate's distance

def hi(x, sr): return pr.sosfiltfilt(pr.butter(4, MATCH_HZ, "high", fs=sr, output="sos"), x)
def rms(x): return float(np.sqrt(np.mean(x * x)) + 1e-12)

def main():
    src, hits_json = sys.argv[1], sys.argv[2]
    out = sys.argv[3] if len(sys.argv) > 3 else OUT
    os.makedirs(out, exist_ok=True)
    sr = sf.info(src).samplerate
    hits = json.load(open(hits_json))
    plates, ref, takes = [], None, []
    for k in HITS:
        h = hits[k - 1]
        x, _ = sf.read(src, start=int((h["shot"] - 1) * sr), stop=int((h["shot"] + h["onset"] + pr.RING_LEN + .5) * sr))
        takes.append((x.mean(1) if x.ndim > 1 else x, h["onset"]))
    for (x, onset), fr in zip(takes, pr.plate_tones(takes, sr)):
        plate, _, at0 = pr.ring(x, sr, 1.0, onset, fr)
        level = rms(hi(plate, sr)[: int(.3 * sr)])
        if ref is None:   # hit 1: the clip's balance and the plate's distance
            pk = int(.95 * sr) + int(np.argmax(np.abs(x[int(.95 * sr): int(1.05 * sr)])))
            ratio = level / rms(hi(x, sr)[pk + int(.07 * sr): pk + int(.11 * sr)])
            delay = (at0 - pk) / sr
            dist = delay / (1 / BULLET + 1 / 343.0)
            ref = level
        plates.append(plate * ref / level)
    f = 0.89 / max(np.abs(p).max() for p in plates)
    for i, p in enumerate(plates, 1):
        sf.write(os.path.join(out, f"plate_{i:02d}.wav"), p * f, sr, subtype="PCM_16")
    with tempfile.TemporaryDirectory(dir="/mnt/e/Tools/deagle-work") as tmp:
        name = os.path.join(tmp, "shot.wav")
        win = subprocess.run(["wslpath", "-w", name], capture_output=True, text=True).stdout.strip()
        starts = render(win, [(3.6, [SHOT, "IsOutside=1", "global:ReverbFOREST=1", "start:" + RM])] + [(3.6, ["ev:" + SHOT, "IsOutside=1"])] * 2)
        g, _ = sf.read(name); g = g.mean(1) if g.ndim > 1 else g
    G = hi(g, sr)
    echo = np.mean([rms(G[b + int(.07 * sr): b + int(.11 * sr)]) for b in
                    (s - int(.05 * sr) + int(np.argmax(np.abs(g[s - int(.05 * sr): s + int(.4 * sr)]))) for s in starts)])
    v_at = ratio * echo / ref / f / 0.5 / PAN   # file gain that gives the clip's balance at dist
    vol = v_at * dist / NEAR / IMPACT_VOL
    print(f"{len(plates)} takes -> {out}; plate A {dist:.1f} m away, {20 * np.log10(ratio):+.1f} dB over the gun's echo (> {MATCH_HZ:.0f} Hz)")
    print(f"SURFACES steel vol = {vol:.3f}  (file gain at {dist:.0f} m {v_at:.3f}, at {NEAR:.0f} m and closer {v_at * dist / NEAR:.3f})")

if __name__ == "__main__":
    main()
