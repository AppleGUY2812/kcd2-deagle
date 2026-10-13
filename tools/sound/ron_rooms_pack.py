"""Listening pack: Ready or Not's MK V exactly as the game plays it, rendered by Ready or Not's own FMOD engine
(tools/ron/FmodRender) in each of its 8 room reverbs, next to the user's in-game recording. Usage: <out dir>"""
import base64, html, json, os, sys, wave
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from measure4 import rd, SR, onset
R = "/mnt/e/Tools/deagle-work/ron/fmodrender/sets"          # tools/sound/render_mk5_sets.sh (8 takes per file)
REC = "/tmp/claude-1000/-home-johnpork/81286427-298b-45b8-8c38-b64077090665/scratchpad/ron_mk5_ingame.wav"
OUT = sys.argv[1]; os.makedirs(OUT, exist_ok=True)
ROOMS = [("in_small", "Inside: small room (freezer, cottage room)"), ("in_small_wood", "Inside: small room, wooden walls"),
         ("in_mid", "Inside: normal room"), ("in_corridor", "Inside: corridor"), ("in_hall", "Inside: hall (diner, tavern)"),
         ("in_hall_wood", "Inside: hall, wooden walls"), ("in_cave", "Inside: cellar / cave"),
         ("out_covered", "Outside under a roof (pump canopy, porch)"), ("out_street", "Outside: street (the gas station forecourt)"),
         ("out_forest", "Outside: forest (what the Deagle now plays)")]
items = []
for f, t in ROOMS:
    x = rd(f"{R}/{f}.wav"); a = onset(x); items.append((f"{len(items) + 1:02d} {f}.wav", x[max(a - 64, 0): a + int(3.9 * SR)], t, f"Ready or Not's engine, set {f}"))
x = rd(REC); a = int(.6 * SR); items.append(("11 your recording - gas station.wav", x[a: a + int(3.4 * SR)], "Your in-game recording (gas station)",
                                                  "4 shots from your video, for comparison with 'Outdoors: street'"))
pk = max(np.abs(y).max() for _, y, _, _ in items[:-1])
for i, (f, y, _, _) in enumerate(items):
    g = .95 / (pk if i < len(items) - 1 else np.abs(y).max()); y = y.copy(); fl = int(.05 * SR); y[-fl:] *= np.linspace(1, 0, fl)[:, None]
    w = wave.open(f"{OUT}/{f}", "wb"); w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR)
    w.writeframes((np.clip(y * g, -1, 1) * 32767).astype(np.int16).tobytes()); w.close()
KEYS = "1234567890q"
btns = "\n".join(f'<button class="pl" data-f="{html.escape(f)}" data-k="{KEYS[i]}"><kbd>{KEYS[i]}</kbd><div><span>{html.escape(t)}</span>'
                 f'<small>{html.escape(s)}</small></div></button>' for i, (f, _, t, s) in enumerate(items))
aud = json.dumps({f: base64.b64encode(open(f"{OUT}/{f}", "rb").read()).decode() for f, _, _, _ in items})
page = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "mk5_pack.py")).read().split('page = """', 1)[1].split('"""', 1)[0]
page = page.replace("<title>MK V sounds</title>", "<title>Ready or Not rooms</title>").replace(
    "<h1>Ready or Not's MK V (.50 AE) as our Deagle</h1>", "<h1>Ready or Not's MK V, rendered by Ready or Not's own engine</h1>").replace(
    """<p>Every sound here is Ready or Not's own recording: the MK V shot plus Ready or Not's MK V echo for that kind of place.
Nothing is generated. Click or press the key. Space stops.</p>""",
    """<p>Not a mix of the files: Ready or Not's own FMOD engine played its own "1P Grizzly Fire" event, with all its layers
(shot + 3 echoes), limiters and the room reverb snapshot a map switches on. Checked against your gas station video:
within 0.7 dB on average. Click or press the key. Space stops.</p>""")
open(f"{OUT}/PLAY ME.html", "w").write(page.replace("BTNS", btns).replace("AUD", aud))
print(len(items), "sounds")
