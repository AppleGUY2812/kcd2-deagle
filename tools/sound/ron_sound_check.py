#!/usr/bin/env python3
"""Listening check: every Ready or Not sound the mod plays, rendered by Ready or Not's own FMOD engine
(tools/ron/FmodRender, listener at the source, Occlusion 0, PropagateDistance 0, final fader 0.5 like the game), plus
the alternatives wherever the pick is a guess. Writes WAVs and an index.html to Desktop\\Deagle Progress\\sound check;
the user listens and says which rows are wrong. usage: python3 tools/sound/ron_sound_check.py"""
import html, os, subprocess
from concurrent.futures import ThreadPoolExecutor

EXE = "/mnt/e/Tools/deagle-work/ron/fmodrender/bin/FmodRender.exe"
BANKS = r"C:\Program Files (x86)\Steam\steamapps\common\Ready Or Not\ReadyOrNot\Content\FMOD\Desktop"
OUT = "/mnt/c/Users/<user>/Desktop/Deagle Progress/sound check"
OUTWIN = r"C:\Users\<user>\Desktop\Deagle Progress\sound check"
IMP = "event:/Surfaces/Impacts/Bullet Impacts/Bullet Impact Surfaces"
SURF = {1: "Aluminium", 2: "Asphalt", 3: "Brick", 5: "Cardboard", 6: "Ceramic", 7: "ConcreteSoft", 8: "ConcreteStrong",
        9: "Dirt", 10: "Drywall", 13: "Fabric_Carpet", 14: "Fabric_Stuffing", 15: "Fabric_Thin", 16: "Flesh", 17: "Galvanized",
        18: "Glass_Plate", 19: "Glass_Windshield", 20: "Grass", 21: "Gravel", 26: "Limestone", 27: "Mahogany",
        28: "Marble_Coated", 29: "Marble_Thick", 32: "Paper", 33: "Pine", 34: "Plaster", 36: "Plywood", 39: "Rock",
        41: "Sand", 44: "Steel", 45: "Tin", 46: "Treewood", 48: "Water", 50: "Cloth", 53: "Leather"}

rows = []   # (section, what it's for, label, event, params, plays, secs, globals, starts, pick: "current" / "option" / "")
def row(section, what, label, event, params="", plays=3, secs=1.2, globs=(), starts=(), pick="current"):
    rows.append(dict(section=section, what=what, label=label, event=event, params=params, plays=plays, secs=secs,
                     globs=list(globs), starts=list(starts), pick=pick))

# ---- gunshots, per place (RoN's ReverbMaster + the globals the mod sets for that place)
RM = "event:/Levels/Reverbs/ReverbMaster"
PLACES = [("out_street", "outside: village street, open field", 1, ["ReverbSTREET"]),
          ("out_forest", "outside: forest", 1, ["ReverbFOREST"]),
          ("out_covered", "outside under a roof (porch, stall)", 1, ["ReverbMID", "ReverbMaterialCONCRETE"]),
          ("in_small", "small room, no clear material", 0, ["ReverbSMALL"]),
          ("in_small_stone", "small stone room", 0, ["ReverbSMALL", "ReverbMaterialCONCRETE"]),
          ("in_small_wood", "small wooden room", 0, ["ReverbSMALL", "ReverbMaterialWOOD"]),
          ("in_mid_stone", "normal stone room", 0, ["ReverbMID", "ReverbMaterialCONCRETE"]),
          ("in_mid_wood", "normal wooden room", 0, ["ReverbMID", "ReverbMaterialWOOD"]),
          ("in_mid_metal", "normal room, mostly metal (smithy)", 0, ["ReverbMID", "ReverbMaterialMETAL"]),
          ("in_mid_fabric", "normal room, mostly cloth (tent)", 0, ["ReverbMID", "ReverbMaterialFABRIC"]),
          ("in_hall_stone", "big stone hall (church, castle)", 0, ["ReverbHALL", "ReverbMaterialCONCRETE"]),
          ("in_hall_wood", "big wooden hall (tavern, barn)", 0, ["ReverbHALL", "ReverbMaterialWOOD"]),
          ("in_corridor", "corridor / passage", 0, ["ReverbCORRIDOR"]),
          ("in_cave_stone", "cellar / cave", 0, ["ReverbCAVE", "ReverbMaterialCONCRETE"])]
for gun, ev in (("Deagle (MK V)", "event:/Weapons/Grizzly/1P Grizzly Fire"), ("Glock (G19)", "event:/Weapons/G19 New/1P G19 Fire")):
    for cls, what, out, gl in PLACES:
        if gun.startswith("Glock") and cls not in ("out_street", "in_small_stone", "in_hall_wood", "in_cave_stone"):
            continue
        row(f"Gunshots: {gun}", f"{what} ({cls})", " + ".join(gl), ev, f"IsOutside={out}", plays=2, secs=4.0,
            globs=[f"{g}=1" for g in gl], starts=[RM])

# ---- bullet impacts: KCD2 surface -> RoN Surface (current pick first, then options)
IMPACTS = [
    ("stone: rock, castle walls, cobbles (mat_rock, mat_default)", [8, 7, 39, 3, 29, 28, 26]),
    ("stone stairs (mat_stairs_stone)", [8, 26, 3]),
    ("plaster walls (mat_plaster)", [34, 10]),
    ("pots, tiles (mat_ceramics)", [6]),
    ("planks, doors, wooden stairs (mat_wood, mat_door_wood)", [33, 27, 36, 46]),
    ("tree trunks, soft wood (mat_wood_soft)", [46, 33]),
    ("wooden shields (mat_shield_wood)", [36, 33]),
    ("wicker baskets (mat_wicker)", [5, 32, 46]),
    ("metal (mat_metal, doors, bells' frames)", [44, 17, 45, 1]),
    ("earth: soil, road, field, forest floor, mud (mat_soil ...)", [9, 2, 41]),
    ("gravel (mat_gravel)", [21]),
    ("grass, dry grass, thatch roofs (mat_grass, mat_thatch)", [20]),
    ("water, puddles (mat_water ...)", [48]),
    ("glass (mat_glass)", [18, 19]),
    ("cloth, sacks (mat_fabric, mat_sack)", [15, 50, 13]),
    ("hay, organic stuff (mat_organic_stuff)", [14, 20]),
    ("leather (mat_leather)", [53]),
    ("NPC body hit (mat_flesh)", [16]),
]
for what, opts in IMPACTS:
    for i, sf in enumerate(opts):
        pick = "current" if i == 0 else "option"
        if what.startswith("stone: rock") and sf == 39: pick = "option (what you heard: RoN's 'Rock')"
        row("Bullet impacts", what, f"Surface {sf} {SURF[sf]}", IMP, f"Surface={sf} Occlusion=0 PropagateDistance=0", pick=pick)
row("Bullet impacts", "NPC body hit (mat_flesh)", "3P Gore event", "event:/Surfaces/Impacts/Bullet Impacts/3P Bullet Impacts/3P Gore",
    "Occlusion=0 PropagateDistance=0", pick="option")
row("Bullet impacts", "church bells (mat_bells)", "Surface 44 Steel", IMP, "Surface=44 Occlusion=0 PropagateDistance=0")
row("Bullet impacts", "church bells (mat_bells)", "Tower Bell event", "event:/Surfaces/Impacts/Bullet Impacts/Bullet Impact Tower Bell",
    "Occlusion=0 PropagateDistance=0", plays=1, secs=8.5, pick="option")
row("Bullet impacts", "ricochet off stone / metal", "Ricochets", "event:/Surfaces/Impacts/Bullet Impacts/Ricochets", "Occlusion=0 PropagateDistance=0", secs=1.5)
A = "event:/Surfaces/Impacts/Armor Impacts/"
row("Bullet impacts", "worn armour: plate / mail hit, round goes through", "3P Armor Body", A + "3P/3P Armor Body", "Occlusion=0 PropagateDistance=2")
row("Bullet impacts", "worn armour: helmet hit, round goes through", "3P Armor Head", A + "3P/3P Armor Head", "Occlusion=0 PropagateDistance=2")
row("Bullet impacts", "worn armour: plate stops the round", "3P Armor Body Block", A + "Blocks/3P Armor Body Block", "Occlusion=0 PropagateDistance=2")
row("Bullet impacts", "worn armour: helmet stops the round", "3P Armor Head Block", A + "Blocks/3P Armor Head Block", "Occlusion=0 PropagateDistance=12")
row("Bullet impacts", "worn armour (any)", "Surface 44 Steel instead", IMP, "Surface=44 Occlusion=0 PropagateDistance=0", pick="option")

# ---- casings (9mmShell, Surface = where it lands)
for sf in (8, 9, 33, 44, 20, 21, 48):
    row("Shell casings landing", f"casing on {SURF[sf].lower()}", f"9mmShell Surface {sf}", "event:/Weapons/Shared/9mmShell", f"Surface={sf}")

# ---- handling
G, N, S = "event:/Weapons/Grizzly/Grizzly ", "event:/Weapons/G19 New/G19 ", "event:/Weapons/Shared/"
H = [("Deagle reload", "ReloadStart (at the mag release)", G + "ReloadStart"), ("Deagle reload", "MagOut", G + "MagOut"),
     ("Deagle reload", "MagIn", G + "MagIn"), ("Deagle empty reload", "Reload Empty Start", G + "Reload Empty Start"),
     ("Deagle empty reload", "Reload Empty MagOut (also inspect)", G + "Reload Empty MagOut"),
     ("Deagle empty reload", "Reload Empty MagIn", G + "Reload Empty MagIn"),
     ("Deagle empty reload", "Reload Empty Slide Release (also inspect)", G + "Reload Empty Slide Release"),
     ("Glock reloads / inspect", "Cloth-ReloadStart", N + "Cloth-ReloadStart"), ("Glock reloads / inspect", "WetMagOutQuick", N + "WetMagOutQuick"),
     ("Glock reloads / inspect", "WetMagInQuick", N + "WetMagInQuick"), ("Glock reloads / inspect", "Cloth-ReloadEnd", N + "Cloth-ReloadEnd"),
     ("Glock reloads / inspect", "Dry Mag Out", N + "Dry Mag Out"), ("Glock reloads / inspect", "Cloth-PouchGrab", N + "Cloth-PouchGrab"),
     ("Glock reloads / inspect", "Slide", N + "Slide"), ("Glock reloads / inspect", "Mag Check Out", N + "Mag Check Out"),
     ("Glock reloads / inspect", "Mag Check In", N + "Mag Check In"), ("Glock reloads / inspect", "Cloth-CheckDone", N + "Cloth-CheckDone"),
     ("Both guns", "draw (Draw Pistol V2)", S + "Draw/Draw Pistol V2"), ("Both guns", "holster", S + "Pistols Shared/Pistol Holster"),
     ("Both guns", "aim in (ADS Pistol Start)", S + "ADS/ADS Pistol Start"), ("Both guns", "aim out (ADS Pistol Stop)", S + "ADS/ADS Pistol Stop"),
     ("Both guns", "mag hits the floor (empty reloads)", S + "Mag Drops/Pistol Mag Drop"),
     ("Deagle", "empty click (FN57 Dry Fire, from RoN's MK V data)", "event:/Weapons/FN57/FN57 Dry Fire"),
     ("Glock", "empty click (G19 Dryfire Old)", "event:/Weapons/G19/G19 Dryfire Old")]
for what, label, ev in H:
    row("Handling", what, label, ev, "", plays=2, secs=1.8)

os.makedirs(OUT, exist_ok=True)
env = dict(os.environ, WSLENV="FMOD_EXTRA_BANKS:FMOD_MASTER_GAIN", FMOD_MASTER_GAIN="0.5",
           FMOD_EXTRA_BANKS="Interactions.bank;SWAT.bank;Global Amb.bank;UI.bank")

def render(i):
    r = rows[i]
    r["file"] = f"{i + 1:03d}.wav"
    p = r["params"].split()
    args = [EXE, "render", BANKS, OUTWIN + "\\" + r["file"], r["event"], str(r["secs"])] + p
    args += [f"global:{g}" for g in r["globs"]] + [f"start:{s}" for s in r["starts"]]
    for _ in range(r["plays"] - 1): args += ["+", str(r["secs"])] + p
    for _ in range(4):   # WSL's Windows interop sometimes drops a launch under load (UtilAcceptVsock): try again
        res = subprocess.run(args, capture_output=True, text=True, env=env, timeout=600)
        r["ok"] = res.returncode == 0 and os.path.exists(os.path.join(OUT, r["file"]))
        if r["ok"]: break
    if not r["ok"]: r["err"] = (res.stdout + res.stderr)[-300:]

with ThreadPoolExecutor(3) as ex: list(ex.map(render, range(len(rows))))

# FmodRender records its ~4.8 s silent warm-up (room snapshots fading in, audio loading): cut each file to its sound,
# from 50 ms before the first sample over the noise floor to 100 ms after the last
import wave
import numpy as np
for r in rows:
    if not r["ok"]: continue
    path = os.path.join(OUT, r["file"])
    with wave.open(path) as w:
        prm, a = w.getparams(), np.frombuffer(w.readframes(w.getnframes()), np.int16).reshape(-1, w.getnchannels())
    loud = np.nonzero(np.abs(a).max(1) > 30)[0]
    if len(loud) == 0: r["silent"] = True; continue
    sr = prm.framerate
    a = a[max(0, loud[0] - sr // 20): min(len(a), loud[-1] + sr // 10)]
    with wave.open(path, "wb") as w:
        w.setparams(prm); w.writeframes(a.tobytes())

out, sec = [], None
for i, r in enumerate(rows):
    if r["section"] != sec:
        if sec: out.append("</table>")
        sec = r["section"]
        out.append(f"<h2>{html.escape(sec)}</h2><table><tr><th>#</th><th>used for</th><th>sound</th><th>pick</th><th></th></tr>")
    cls = "cur" if r["pick"] == "current" else "opt"
    play = "<b>silent in Ready or Not's engine</b>" if r.get("silent") else f'<audio controls preload="none" src="{r["file"]}"></audio>' if r["ok"] else f'<b>render failed</b> {html.escape(r.get("err", ""))}'
    out.append(f'<tr class="{cls}"><td>{i + 1}</td><td>{html.escape(r["what"])}</td><td>{html.escape(r["label"])}</td>'
               f'<td>{html.escape(r["pick"])}</td><td>{play}</td></tr>')
out.append("</table>")
page = f"""<!doctype html><html><head><meta charset="utf-8"><title>Sound check</title><style>
body{{font:15px system-ui;margin:16px;background:#111;color:#eee}} table{{border-collapse:collapse;width:100%}}
td,th{{border-bottom:1px solid #333;padding:4px 8px;text-align:left}} tr.cur td{{background:#1d2b1d}} tr.opt td{{color:#bbb}}
audio{{height:30px}} h2{{margin-top:28px}}</style></head><body>
<h1>Ready or Not sound check</h1>
<p>Every sound the mod plays, made by Ready or Not's own engine. Green rows = what the mod uses now; grey rows = other
choices. Impacts play 3 times (RoN picks a random take each time). Tell Claude the numbers that sound wrong, and which
option you want instead.</p>
{chr(10).join(out)}</body></html>"""
open(os.path.join(OUT, "index.html"), "w").write(page)
bad = [r for r in rows if not r["ok"]]
print(f"{len(rows)} rows, {len(bad)} failed -> {OUT}/index.html")
for r in bad: print("FAILED", r["label"], r.get("err", "")[:200])
