#!/usr/bin/env python3
"""When each clip's work is done, measured from its tracks (tools/vm_tracks_dump.py, tools/gb/gb_tracks_dump.py),
in clip-timeline seconds (frames / the clip's fps), for the script's reload / inspect / draw timing:
  cancel  until then a shot cancels a reload cleanly: the old mag is still in and the spare still off screen
  out     the old mag leaves the gun: before it a cancelled reload keeps the rounds, after it only the chambered one
  seat    the new mag clicks in (its mag-in sound; else when it sits in the gun for good): the ammo goes in
  ready   the gun is back near its firing pose (30 mm / 10 deg; 3 mm / 1 deg when the clip ends with the spare mag
          seated, because the cut out of it switches to idle first), and on an empty reload the slide has closed:
          the reload is over and a shot may cut the rest
  trim    the hands have settled (3 mm / 1 deg to the end): the clip ends there
usage: clip_times.py <tracks.json> [--cues <{key: [[t, file]...]} .json> | --profile guns/<slug>.json]
(a profile's "times" {key: {seat, ready, ...}} override the measured moments)
                     [--swap reload,reload_empty] [--lua]"""
import json, sys, math, argparse, re

BODY = ("j_gun", "j_wrist_ri", "j_wrist_le")
KEY = {"reload": "reload", "reload_empty": "reloadEmpty", "inspect": "inspect", "inspect_empty": "inspectEmpty",
       "draw": "draw", "draw_empty": "drawEmpty"}
REST = {"inspect_empty": "idle_empty", "draw_empty": "idle_empty"}
SLIDE_CUE = ("slideclose", "slide_close", "sliderelease", "slide_release", "slide_charge", "slidecharge", "boltrelease")

def qang(a, b):
    return math.degrees(2 * math.acos(min(1.0, abs(sum(x * y for x, y in zip(a, b))))))

def off(f, ref):
    return (max(math.dist(f[b][:3], ref[b][:3]) for b in BODY if b in f),
            max(qang(f[b][3:7], ref[b][3:7]) for b in BODY if b in f))

def times(t, cues=None, swap=()):
    fps_of = t.get("__fps__", {})
    idle = t["idle1"][0]
    seated = math.dist(idle["j_mag1"][:3], idle["j_gun"][:3]) if "j_mag1" in idle else None
    out = {}
    for clip, key in KEY.items():
        fr = t.get(clip)
        if not fr:
            continue
        fps = float(fps_of.get(clip, 24)) if isinstance(fps_of, dict) else 24.0
        n, ref = len(fr), t[REST.get(clip, "idle1")][0]
        def settled(mm, deg):
            k = n
            while k > 0 and all(x <= y for x, y in zip(off(fr[k - 1], ref), (mm, deg))):
                k -= 1
            return k
        r = {"trim": settled(3, 1) / fps}
        if clip.startswith("reload") and seated is not None:
            def mag_at(f, m):
                return math.dist(f[m][:3], f["j_gun"][:3])
            c = 0
            while c < n and abs(mag_at(fr[c], "j_mag1") - seated) <= 3 and mag_at(fr[c], "j_mag2") > 700:
                c += 1
            k = n
            for m in ("j_mag1", "j_mag2"):
                j = n
                while j > 0 and abs(mag_at(fr[j - 1], m) - seated) <= 3:
                    j -= 1
                if j < n:
                    k = min(k, j)
            o = 0                                # the old mag leaves the gun (it was seated at the start)
            while o < n and abs(mag_at(fr[o], "j_mag1") - seated) <= 3:
                o += 1
            r["cancel"], r["seat"], r["out"] = c / fps, k / fps, o / fps
        # the magazine's click (its mag-in sound) is the moment it's in: the ammo counts from there
        if clip.startswith("reload") and cues:
            clicks = [c[0] for c in cues.get(key, []) if re.search(r"mag_?in(?![a-z])", c[1].lower())]
            if clicks:
                r["seat"] = clicks[0]
        ready = settled(3, 1) if clip in swap else settled(30, 10)
        r["ready"] = ready / fps
        if clip == "reload_empty" and cues:
            slide = [c[0] for c in cues.get(key, []) if any(s in c[1].lower() for s in SLIDE_CUE)]
            if slide:
                r["ready"] = max(r["ready"], max(slide) + 0.05)
        if clip.startswith("inspect"):
            del r["ready"]                       # an inspect never blocks anything
        out[key] = {k: round(v, 3) for k, v in r.items()}
    return out

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("tracks"); ap.add_argument("--cues"); ap.add_argument("--profile")
    ap.add_argument("--swap", default=""); ap.add_argument("--lua", action="store_true")
    a = ap.parse_args()
    cues = json.load(open(a.cues)) if a.cues else None
    if a.profile:                                # a converted gun: cue sounds straight from its profile
        clips = json.load(open(a.profile)).get("clips", {})
        cues = {KEY[r]: [[x["t"], x.get("file") or x.get("option") or ""] for x in c.get("sounds", [])]
                for r, c in clips.items() if r in KEY}
    res = times(json.load(open(a.tracks)), cues, [s for s in a.swap.split(",") if s])
    if a.profile:   # moments the gun's own source gives exactly (a Ready or Not gun's anim notifies) beat the measured ones
        for k, d in json.load(open(a.profile)).get("times", {}).items():
            res.setdefault(k, {}).update(d)
    if a.lua:
        print("times = { " + ", ".join(f"{k} = {{ " + ", ".join(f"{w} = {v}" for w, v in sorted(d.items())) + " }" for k, d in res.items()) + " },")
    else:
        print(json.dumps(res, indent=1))
