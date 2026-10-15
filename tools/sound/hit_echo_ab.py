#!/usr/bin/env python3
"""Does the steel plate get the room's echo like Ready or Not's own hits? The mod now vs the plate sent into it.

Ready or Not's mixer (FmodRender DUMP_MASTER, 2026-10-14): the gunshot (bus 1PWeapons) and the hits (bus Special
Destructables Duck) both send into one room reverb (Reverb_Global), whose sound and send levels the place's snapshot
sets: the gun 0.562 indoors / 0.178 outdoors, the hits 0.447 indoors / ~0.01-0.016 outdoors (RoN gives hits almost no
echo outside). Our plate (a "file:" sound) skips that bus: no echo anywhere.
Now: as the game plays it (RoN's hits at RoN's sends, the plate dry). With echo: the plate in a group with the hits'
send (what the plugin will do: copy the hits bus's send level, so it follows the place and doorways).
Sending hits at the gun's level outdoors was tried (send:<hits bus>=gun - hits): Forest's reverb is so quiet that even
0.9 left a stone hit's tail ~27 dB under it by 0.75 s; most of the gun's outdoor echo is its recorded EXT echo layers.
Each file: the Deagle's shot (RoN's MK V) and a hit HIT_M away the bullet's and the sound's travel time later, 3 times:
plate, stone, plate. Ready or Not's engine renders all of it (final fader 0.5 like the game).

Writes WAVs + index.html to Desktop\\Deagle Progress\\22 Hit echo. usage: hit_echo_ab.py [outdir]"""
import html, os, subprocess, sys
import numpy as np, soundfile as sf
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from impact_level_ab import IMP, RM, SHOT, render   # noqa: E402

DESK = "/mnt/c/Users/<user>/Desktop/Deagle Progress/22 Hit echo"
PLATE = r"E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\Mods\deagle\sounds\steel\plate_0%d.wav"
HITS_BUS = "bus:/Headphones/SFX/Special Destructables Duck"
IMPACT_VOL, NEAR = 2.0, 3.0            # deagle_main.lua cfg: impactVolume, impactNear
PLATE_VOL, PLATE_NEAR = 1.685, 6.0     # sound.lua PLATE
STONE = 8                              # RoN Surface ConcreteStrong (KCD2 rock)
HIT_M, BULLET, C0 = 15.0, 400.0, 343.0
GAP = 3.6                              # s from one shot to the next (the street echo lasts ~3 s)
# place: its globals, the gun's send and the hits' send into the room reverb (measured, see above)
PLACES = [("Small room", ["IsOutside=0", "global:ReverbSMALL=1"], 0.562, 0.447),
          ("Hall", ["IsOutside=0", "global:ReverbHALL=1"], 0.562, 0.447),
          ("Forest", ["IsOutside=1", "global:ReverbFOREST=1"], 0.178, 0.016)]

def falloff(near, d): return near / max(d, near)

def main():
    out = sys.argv[1] if len(sys.argv) > 1 else DESK
    out_win = subprocess.run(["wslpath", "-w", out], capture_output=True, text=True).stdout.strip()
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(out):
        if f.endswith(".wav"): os.remove(os.path.join(out, f))
    delay = HIT_M / BULLET + HIT_M / C0
    plate_vol = PLATE_VOL * IMPACT_VOL * falloff(PLATE_NEAR, HIT_M)
    stone_vol = IMPACT_VOL * falloff(NEAR, HIT_M)
    rows = []
    for place, env, gun_send, hit_send in PLACES:
        # one gunshot render for both versions (RoN picks a random take per render), the hits rendered on their own
        # both ways on the same timeline, then summed: the only difference between the two files is the hits' echo
        env_args = [a for a in env if a.startswith("global:")] + ["start:" + RM]
        shot = [a for a in env if not a.startswith("global:")]
        tmp = os.path.join(out, "_part.wav")
        tmp_win = out_win + "\\_part.wav"
        starts = render(tmp_win, [(GAP, [SHOT] + shot + env_args)] + [(GAP, ["ev:" + SHOT] + shot)] * 2)
        gun, sr = sf.read(tmp)
        gun = gun[starts[0] - sr // 20:]
        mixes = {}
        for echo in (False, True):
            sends = [f"send:files={hit_send}"] if echo else []
            segs, take = [(delay, [IMP, "Surface=1", "vol:0"] + env_args + sends)], 0   # silent: the shot's moment
            for hit in ("plate", "stone", "plate"):
                if hit == "plate":
                    take += 1
                    segs.append((GAP, [f"file:{PLATE % take}", f"vol:{plate_vol:.4f}"]))
                else:
                    segs.append((GAP, ["ev:" + IMP, f"Surface={STONE}", "Occlusion=0", "PropagateDistance=0", f"vol:{stone_vol:.4f}"]))
            hs = render(tmp_win, segs)
            h, _ = sf.read(tmp)
            h = h[hs[0] - sr // 20:]
            n = min(len(gun), len(h))
            mixes[echo] = (gun[:n] + h[:n], h[:n], [k - hs[0] + sr // 20 for k in hs[1:]])
        os.remove(tmp)
        peak = max(np.abs(m[0]).max() for m in mixes.values())
        for echo in (False, True):
            mix, h, at = mixes[echo]
            name = f"{place.lower().replace(' ', '_')}_{'with_echo' if echo else 'now'}.wav"
            sf.write(os.path.join(out, name), mix / max(peak, 1), sr, subtype="PCM_16")
            rows.append((place, "WITH ECHO: the plate echoes like Ready or Not's own hits" if echo else "NOW: as in your game", name,
                         "match" if echo else "now"))
            # the hits' own tail 0.3-1.0 s after each hit over its first 50 ms (a Ready or Not event starts ~0.1 s
            # after its reported start, our file at once: the hit is the loudest point in the 0.25 s after it)
            def db(x): return 20 * np.log10(np.sqrt(np.mean(x ** 2)) + 1e-12)
            pk = [a + int(np.argmax(np.abs(h[a: a + sr // 4]).max(1))) for a in at]
            tail = [db(h[p + int(.3 * sr): p + sr]) - db(h[p: p + int(.05 * sr)]) for p in pk]
            print(f"{place:15s} {'echo' if echo else 'now ':4s} hit tails (plate, stone, plate) " + " ".join(f"{t:+.0f}" for t in tail) + " dB")
    trs, sec = [], None
    for s, label, f, cls in rows:
        if s != sec: trs.append(f"<tr><th colspan=2>{html.escape(s)}</th></tr>"); sec = s
        trs.append(f"<tr class='{cls}'><td>{html.escape(label)}</td><td><audio controls preload='none' src='{f}'></audio></td></tr>")
    page = f"""<!doctype html><html><head><meta charset="utf-8"><title>Hit echo</title><style>
body{{font:15px system-ui;margin:16px;background:#111;color:#eee;max-width:900px}} table{{border-collapse:collapse;width:100%}}
td,th{{border-bottom:1px solid #333;padding:6px 8px;text-align:left}} th{{padding-top:18px;color:#9cf}}
tr.now td{{background:#2b1d1d}} tr.match td{{background:#1d2b1d}} audio{{height:30px}}</style></head><body>
<h1>Does the steel plate echo like the other hits?</h1>
<p>Each file: your Deagle shot and a hit {HIT_M:.0f} m away, 3 times (steel plate, stone, steel plate). The gunshot is
the same recording in both files of a place. Listen to the plates (1st and 3rd hit) after the ring. Red = now: Ready or
Not's own hits (the stone) get the room's echo indoors, the steel plate gets none. Green = the plate gets the same room
echo as Ready or Not's hits. Outdoors Ready or Not gives hits almost no echo, so the forest pair should sound the same.
Ready or Not's engine made every file.</p>
<table>{''.join(trs)}</table></body></html>"""
    open(os.path.join(out, "index.html"), "w").write(page)
    print("->", os.path.join(out, "index.html"))

if __name__ == "__main__":
    main()
