#!/usr/bin/env python3
"""Listening pack "19 Plate ring split": for each Hickok plate hit, the original clip, the plate alone as
plate_rings.py lifts it, and what's left out after the strike (gun echo, voice, music), so the split can be checked
by ear: no voice or gun in "plate only", no plate ring in "left out". Plate only + left out = the original after the
strike.

usage: plate_split_pack.py <recording.wav> <hits.json> [outdir]   (hits.json: the first 10 of ssr351_hits.json)
Private use only (Hickok45's audio): the WAVs stay on the Desktop."""
import html, json, os, sys
import numpy as np, soundfile as sf
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import plate_rings as pr                                            # noqa: E402

DESK = "/mnt/c/Users/<user>/Desktop/Deagle Progress/19 Plate ring split"
PLATES = [("Plate A (~22 m)", (1, 4, 6)), ("Plate B (~9 m)", (5, 7)), ("Plate C (~100 m)", (2, 3, 8, 9, 10))]

def main():
    src, hits_json = sys.argv[1], sys.argv[2]
    out = sys.argv[3] if len(sys.argv) > 3 else DESK
    os.makedirs(out, exist_ok=True)
    sr = sf.info(src).samplerate
    hits = json.load(open(hits_json))
    rows = []
    for title, ks in PLATES:
        rows.append(f"<tr><th colspan=4>{html.escape(title)}</th></tr>")
        for k in ks:
            h = hits[k - 1]
            x, _ = sf.read(src, start=int((h["shot"] - 1) * sr), stop=int((h["shot"] + h["onset"] + pr.RING_LEN + .5) * sr))
            x = x.mean(1) if x.ndim > 1 else x
            first = pr.tones(x, sr, 1.0, h["onset"])
            fr = sorted(first + pr.more_tones(x, sr, 1.0, h["onset"], first))
            plate, rest, at0 = pr.ring(x, sr, 1.0, h["onset"], fr)
            orig = x[int(.7 * sr): at0 + len(plate)]               # from 0.3 s before the shot
            g = 0.89 / max(np.abs(orig).max(), 1e-9)               # one gain for all three
            sf.write(os.path.join(out, f"original_{k:02d}.wav"), orig * g, sr, subtype="PCM_16")
            sf.write(os.path.join(out, f"plate_only_{k:02d}.wav"), np.clip(plate * g, -1, 1), sr, subtype="PCM_16")
            sf.write(os.path.join(out, f"left_out_{k:02d}.wav"), np.clip(rest * g, -1, 1), sr, subtype="PCM_16")
            cell = lambda f: f"<td><audio controls preload='none' src='{f}'></audio></td>"
            rows.append(f"<tr><td>hit {k:02d}<br><small>{len(fr)} tones ({len(fr) - len(first)} new)</small></td>"
                        + cell(f"original_{k:02d}.wav") + cell(f"plate_only_{k:02d}.wav") + cell(f"left_out_{k:02d}.wav") + "</tr>")
            print(f"hit {k:02d}: {len(first)} + {len(fr) - len(first)} tones")
    page = f"""<!doctype html><html><head><meta charset="utf-8"><title>Plate ring split</title><style>
body{{font:15px system-ui;margin:16px;background:#111;color:#eee;max-width:1100px}} table{{border-collapse:collapse;width:100%}}
td,th{{border-bottom:1px solid #333;padding:6px 8px;text-align:left}} th{{padding-top:18px;color:#9cf}} audio{{height:30px;width:250px}}</style></head><body>
<h1>Plate ring split from Hickok's recording</h1>
<p><b>Original</b> = the clip (starts 0.3 s before the shot). <b>Plate only</b> = the plate as the game would get it: the clank
at the strike, its early shimmer, and its own ringing tones. <b>Left out</b> = everything else after the strike (gun echo,
voice, music). Plate only + left out = the original after the strike. Same volume in all three.</p>
<p>Updated 2026-10-15: you heard plate tones in "left out". They were real: weaker tones next to stronger ones were
missed. A second search on what was left now finds them (it finds nothing on shots with no plate). What can still
be left out: the very end of a tone, cut where it sinks into the gun's echo, so the echo doesn't leak in.</p>
<p>Check: does "plate only" have any voice, laugh, music or gun in it? Does "left out" still have plate ring in it?</p>
<table><tr><th></th><th>original</th><th>plate only</th><th>left out</th></tr>{''.join(rows)}</table></body></html>"""
    open(os.path.join(out, "index.html"), "w").write(page)
    print("->", os.path.join(out, "index.html"))

if __name__ == "__main__":
    main()
