#!/usr/bin/env python3
"""The plate lifted from Hickok's clip (plate_rings.py) played the way the game will: Ready or Not's MK V shot outdoors
with its own echo (its engine, final fader 0.5), the plate the clip's strike delay later, as loud next to the shot's
echo as it is in the clip, above MATCH_HZ (plate over its first 0.3 s vs the gun's echo 0.07-0.11 s after the shot,
before the strike; Ready or Not's echo has far more highs than the camera's, so a full-band match buried the plate).
Next to it, the clip itself. Most of what follows a plate hit in the clip after ~0.4 s is the gun's echo (measured
against 7 shots with no plate), which the game already plays.

usage: plate_in_game.py <recording.wav> <hits.json> <hit number> <outdir>"""
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
    os.makedirs(out, exist_ok=True)
    sr = sf.info(src).samplerate
    h = json.load(open(hits_json))[k - 1]
    x, _ = sf.read(src, start=int((h["shot"] - 1) * sr), stop=int((h["shot"] + h["onset"] + pr.RING_LEN + .5) * sr))
    x = x.mean(1) if x.ndim > 1 else x
    plate, _, at0 = pr.ring(x, sr, 1.0, h["onset"], pr.tones(x, sr, 1.0, h["onset"]))
    pk = int(.95 * sr) + int(np.argmax(np.abs(x[int(.95 * sr): int(1.05 * sr)])))      # the clip's bang
    ph = rms(hi(plate, sr)[: int(.3 * sr)])
    ratio = ph / rms(hi(x, sr)[pk + int(.07 * sr): pk + int(.11 * sr)])
    clip = x[pk - int(.3 * sr): pk + int(2.2 * sr)]
    sf.write(os.path.join(out, f"hickok_hit{k:02d}.wav"), clip / np.abs(clip).max() * .89, sr, subtype="PCM_16")
    delay = (at0 - pk) / sr                                     # the plate after the clip's bang
    name = "game_shot.wav"
    win = subprocess.run(["wslpath", "-w", os.path.join(out, name)], capture_output=True, text=True).stdout.strip()
    starts = render(win, [(GAP, [SHOT, "IsOutside=1", "global:ReverbFOREST=1", "start:" + RM])] + [(GAP, ["ev:" + SHOT, "IsOutside=1"])] * 2)
    g, gsr = sf.read(os.path.join(out, name)); g = g.mean(1) if g.ndim > 1 else g
    assert gsr == sr
    mix = g.copy()
    for s in starts:
        b = s - int(.05 * sr) + int(np.argmax(np.abs(g[s - int(.05 * sr): s + int(.4 * sr)])))   # its bang
        echo = rms(hi(g, sr)[b + int(.07 * sr): b + int(.11 * sr)])
        at = b + int(round(delay * sr))
        n = min(len(plate), len(mix) - at)
        mix[at: at + n] += plate[:n] / ph * ratio * echo
    cut = max(0, starts[0] - int(.3 * sr))
    mix, g = mix[cut:], g[cut:]
    os.remove(os.path.join(out, name))
    peak = max(np.abs(mix).max(), np.abs(g).max())
    sf.write(os.path.join(out, "game_shot_only.wav"), g / max(peak, 1), sr, subtype="PCM_16")
    sf.write(os.path.join(out, f"game_shot_plus_plate{k:02d}.wav"), mix / max(peak, 1), sr, subtype="PCM_16")
    print(f"plate {20 * np.log10(ratio):+.1f} dB vs the gun's echo at 0.07-0.11 s (> {MATCH_HZ:.0f} Hz), strike {delay:.3f} s after the bang, peak {20 * np.log10(peak):+.1f} dBFS")

if __name__ == "__main__":
    main()
