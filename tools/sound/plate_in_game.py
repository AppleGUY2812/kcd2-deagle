#!/usr/bin/env python3
"""The plate lifted from Hickok's clip (plate_rings.py) played the way the game will: Ready or Not's MK V shot outdoors
with its own echo (its engine, final fader 0.5), the plate the clip's strike delay later, as loud next to the shot's
echo as it is in the clip, above MATCH_HZ (the gun's echo 0.07-0.11 s after the shot, before the strike; Ready or
Not's echo has far more highs than the camera's, so a full-band match buried the plate).
Next to it, the clip itself. Most of what follows a plate hit in the clip after ~0.4 s is the gun's echo (measured
against 7 shots with no plate), which the game already plays.

usage: plate_in_game.py <recording.wav> <hits.json> <hit number> <outdir> [tag:full:gain[:tones] ...]
Every variant on the same rendered shots (Ready or Not picks a random take per render). full: plate_rings.CLANK_FULL
in s; gain: dB over the clip's balance; tones: all (default: plate_rings.all_tones, with the second pass) or first
(only plate_rings.tones, the plate as it was before 2026-10-15)."""
import json, os, subprocess, sys
import numpy as np, soundfile as sf
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import plate_rings as pr                                            # noqa: E402
from impact_level_ab import RM, SHOT, render                        # noqa: E402

GAP = 3.6
MATCH_HZ = 2000.0   # the balance is matched above this: where the plate is (below it, only the gun's echo)

def rms(x): return float(np.sqrt(np.mean(x * x)) + 1e-12)

def hi(x, sr): return pr.sosfiltfilt(pr.butter(4, MATCH_HZ, "high", fs=sr, output="sos"), x)

def main():
    src, hits_json, k, out = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
    variants = [(v.split(":") + ["all"])[:4] for v in sys.argv[5:]] or [["", "0", "0", "all"]]
    os.makedirs(out, exist_ok=True)
    sr = sf.info(src).samplerate
    h = json.load(open(hits_json))[k - 1]
    x, _ = sf.read(src, start=int((h["shot"] - 1) * sr), stop=int((h["shot"] + h["onset"] + pr.RING_LEN + .5) * sr))
    x = x.mean(1) if x.ndim > 1 else x
    found = {"all": pr.all_tones(x, sr, 1.0, h["onset"]), "first": pr.tones(x, sr, 1.0, h["onset"])}
    pk = int(.95 * sr) + int(np.argmax(np.abs(x[int(.95 * sr): int(1.05 * sr)])))      # the clip's bang
    echo_clip = rms(hi(x, sr)[pk + int(.07 * sr): pk + int(.11 * sr)])
    clip = x[pk - int(.3 * sr): pk + int(2.2 * sr)]
    sf.write(os.path.join(out, f"hickok_hit{k:02d}.wav"), clip / np.abs(clip).max() * .89, sr, subtype="PCM_16")
    name = "game_shot.wav"
    win = subprocess.run(["wslpath", "-w", os.path.join(out, name)], capture_output=True, text=True).stdout.strip()
    starts = render(win, [(GAP, [SHOT, "IsOutside=1", "global:ReverbFOREST=1", "start:" + RM])] + [(GAP, ["ev:" + SHOT, "IsOutside=1"])] * 2)
    g, gsr = sf.read(os.path.join(out, name)); g = g.mean(1) if g.ndim > 1 else g
    os.remove(os.path.join(out, name))
    assert gsr == sr
    bangs = [s - int(.05 * sr) + int(np.argmax(np.abs(g[s - int(.05 * sr): s + int(.4 * sr)]))) for s in starts]
    cut = max(0, starts[0] - int(.3 * sr))
    mixes = {}
    for tag, full, gain_db, which in variants:
        pr.CLANK_FULL = float(full)
        plate, _, at0 = pr.ring(x, sr, 1.0, h["onset"], found[which])
        delay = (at0 - pk) / sr                                 # the plate after the clip's bang
        mix = g.copy()
        for b in bangs:
            # as loud next to the game shot's echo as next to the clip's (over MATCH_HZ, before the strike)
            scale = rms(hi(g, sr)[b + int(.07 * sr): b + int(.11 * sr)]) / echo_clip * 10 ** (float(gain_db) / 20)
            at = b + int(round(delay * sr)); n = min(len(plate), len(mix) - at)
            mix[at: at + n] += plate[:n] * scale
        mixes[tag] = mix[cut:]
        print(f"{tag or 'plain'}: {len(found[which])} tones ({which}), full-band clank {float(full) * 1000:.0f} ms, {float(gain_db):+.0f} dB, strike {delay:.3f} s after the bang")
    g = g[cut:]
    peak = max([np.abs(g).max()] + [np.abs(m).max() for m in mixes.values()])
    sf.write(os.path.join(out, "game_shot_only.wav"), g / max(peak, 1), sr, subtype="PCM_16")
    for tag, m in mixes.items():
        sf.write(os.path.join(out, f"game_shot_plus_plate{k:02d}{'_' + tag if tag else ''}.wav"), m / max(peak, 1), sr, subtype="PCM_16")
    print(f"peak {20 * np.log10(peak):+.1f} dBFS")

if __name__ == "__main__":
    main()
