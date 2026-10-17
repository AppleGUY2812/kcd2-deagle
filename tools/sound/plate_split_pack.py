#!/usr/bin/env python3
"""Listening pack "26 Plate extraction - with and without": for each Hickok plate hit, like SAM Audio's two files,
the original clip, WITH (the plate alone, as plate_rings.py lifts it, what the game plays) and WITHOUT (everything else:
his gun, its echo, his voice, the music), so the split can be checked by ear: no voice or gun in WITH, no plate ring
in WITHOUT. WITH + WITHOUT = the original, sample for sample. A fourth column: WITH as it was before 2026-10-16 (the
first tone search only, each tone cut where it sank under the echo), which is what the game played until then.

usage: plate_split_pack.py <recording.wav> <hits.json> [outdir]   (hits.json: page16_hits.json, the numbering of pack 16)
Private use only (Hickok45's audio): the WAVs stay on the Desktop."""
import html, json, os, sys
import numpy as np, soundfile as sf
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import plate_rings as pr                                            # noqa: E402

DESK = "/mnt/c/Users/<user>/Desktop/Deagle Progress/26 Plate extraction - with and without"
PLATES = [("Plate A: the one in your game (hits 1, 4, 6)", (1, 4, 6)), ("Plate B (~9 m)", (5, 7)),
          ("Plate C (~100 m)", (2, 3, 8, 9, 10))]
WITH, WITHOUT, BEFORE = "WITH (the plate)", "WITHOUT (everything else)", "WITH before the fix"

def main():
    src, hits_json = sys.argv[1], sys.argv[2]
    out = sys.argv[3] if len(sys.argv) > 3 else DESK
    os.makedirs(out, exist_ok=True)
    sr = sf.info(src).samplerate
    hits = json.load(open(hits_json))
    q = lambda s: s.replace(" ", "%20").replace("(", "%28").replace(")", "%29")
    cell = lambda f: f"<td><audio controls preload='none' src='{q(f)}'></audio></td>"
    rows = []
    for title, ks in PLATES:
        rows.append(f"<tr><th colspan=5>{html.escape(title)}</th></tr>")
        takes = []
        for k in ks:
            h = hits[k - 1]
            x, _ = sf.read(src, start=int((h["shot"] - 1) * sr), stop=int((h["shot"] + h["onset"] + pr.RING_LEN + .5) * sr))
            takes.append((x.mean(1) if x.ndim > 1 else x, h["onset"]))
        for k, (x, onset), (fr, slow) in zip(ks, takes, pr.plate_tones(takes, sr)):
            plate, _, at0 = pr.ring(x, sr, 1.0, onset, fr, slow)
            pr.TAILS, keep = False, pr.RING_LEN
            pr.RING_LEN = 1.6
            old, _, at1 = pr.ring(x, sr, 1.0, onset, pr.tones(x, sr, 1.0, onset))
            pr.TAILS, pr.RING_LEN = True, keep
            a = int(.7 * sr)                                       # from 0.3 s before the shot
            orig = pr.fade_end(x[a: at0 + len(plate)], sr)          # ends with the plate: the take's next sound is next
            w = np.zeros(len(orig)); w[at0 - a:] = plate
            b = np.zeros(len(orig)); b[at1 - a: at1 - a + len(old)] = old[: len(orig) - (at1 - a)]
            g = 0.89 / max(np.abs(orig).max(), 1e-9)               # one gain for all four
            files = [f"hit_{k:02d}_{n}.wav" for n in ("original", WITH, WITHOUT, BEFORE)]
            for f, y in zip(files, (orig, w, orig - w, b)):
                sf.write(os.path.join(out, f), np.clip(y * g, -1, 1), sr, subtype="PCM_16")
            rows.append(f"<tr><td>hit {k:02d}<br><small>{len(fr) + len(slow)} tones</small></td>" + "".join(cell(f) for f in files) + "</tr>")
            print(f"hit {k:02d}: {len(fr)} tones, slow {[round(v) for v in slow]}")
    page = f"""<!doctype html><html><head><meta charset="utf-8"><title>Plate extraction</title><style>
body{{font:15px system-ui;margin:16px;background:#111;color:#eee;max-width:1250px}} table{{border-collapse:collapse;width:100%}}
td,th{{border-bottom:1px solid #333;padding:6px 8px;text-align:left}} th{{padding-top:18px;color:#9cf}} audio{{height:30px;width:220px}}</style></head><body>
<h1>The plate extraction: with and without</h1>
<p>Like SAM Audio's two files, from my own filter on Hickok's recording. <b>WITH</b> = the plate alone (what the game plays).
<b>WITHOUT</b> = everything else: his gun, its echo, his voice, the music. WITH + WITHOUT = the original. Same volume in all.</p>
<p>Fixed 2026-10-16: the plate's ring now fades out all the way (each tone is followed under the gun's echo at its exact
pitch, so the echo stays out), and tones of this plate that one take's search had missed are in. <b>WITH before the fix</b>
= what your game played until now. Also fixed: hits 4 and 6 here were two shots on another plate; now they're the takes
your game plays.</p>
<p>Fixed 2026-10-17: the plate's low hum (653 Hz) is followed until Hickok's next sound (hit 4: 3.3 s; before, it stopped
at 2.0 s and came back in WITHOUT). Each clip ends just before that next sound: all four fade out together there.</p>
<p>The low thump right at the hit in WITHOUT is Hickok's gun, not the plate: every shot in the video has it 0.07-0.1 s
after the bang. Listen to hit 02's original (plate C, 100 m away): the same thump, and that plate rings half a second
later.</p>
<table><tr><th></th><th>original</th><th>{WITH}</th><th>{WITHOUT}</th><th>{BEFORE}</th></tr>{''.join(rows)}</table></body></html>"""
    open(os.path.join(out, "index.html"), "w").write(page)
    print("->", os.path.join(out, "index.html"))

if __name__ == "__main__":
    main()
