# Blender (SourceIO): find the viewmodel(s) in an extracted GameBanana CS mod, read every sequence (activity, frames,
# fps, sound events), bone and attachment, list the sound files, and write inspect.json + a guessed profile.json.
# usage: blender -b --factory-startup -P gb_inspect.py -- <extracted mod dir> <out dir>   (out dir holds meta.json)
import sys, os, json, addon_utils
SRC, OUT = sys.argv[sys.argv.index("--") + 1:][:2]
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from profile_guess import build_profile, soundscripts, weapon_script
addon_utils.enable("SourceIO", default_set=True)
from SourceIO.library.models.mdl.v49 import MdlV49
from SourceIO.library.utils import FileBuffer

meta = json.load(open(os.path.join(OUT, "meta.json"))) if os.path.exists(os.path.join(OUT, "meta.json")) else {}
mdls, sounds, scripts, wscript = [], [], {}, {}
for root, _, files in os.walk(SRC):
    for f in files:
        p = os.path.join(root, f)
        rel = os.path.relpath(p, SRC).replace("\\", "/")
        if f.lower().endswith(".mdl"):
            mdls.append(p)
        elif f.lower().endswith((".wav", ".mp3", ".ogg")):
            sounds.append(rel)
        elif f.lower().endswith(".txt") and "/scripts/" in "/" + rel.lower():
            text = open(p, encoding="utf-8", errors="replace").read()
            if f.lower().startswith("weapon_"):
                wscript.update(weapon_script(text))
            else:
                scripts.update(soundscripts(text))
views = [m for m in mdls if os.path.basename(m).lower().startswith("v_")] or mdls
print("MDLS", [os.path.relpath(m, SRC) for m in mdls], "viewmodels", len(views), "sounds", len(sounds), "soundscripts", len(scripts), "weapon script", wscript.get("printname"))

def first_attr(o, names, default=None):
    for n in names:
        if hasattr(o, n):
            return getattr(o, n)
    return default

def inspect(path):
    with FileBuffer(path) as f:
        mdl = MdlV49.from_buffer(f)
    descs = first_attr(mdl, ("anim_descs", "animations", "anim_desc"), []) or []
    seqs = []
    for s in mdl.sequences:
        frames, fps = None, None
        idx = list(getattr(s, "anim_desc_indices", []) or [])
        if idx and idx[0] < len(descs):
            d = descs[idx[0]]
            frames = first_attr(d, ("frame_count", "num_frames", "frames"))
            fps = first_attr(d, ("fps",))
            if not isinstance(frames, int):
                frames = None
        evs = [{"cycle": float(getattr(e, "cycle", 0.0)), "event": int(getattr(e, "event", 0)), "type": int(getattr(e, "type", 0)),
                "options": str(getattr(e, "options", "") or "").strip("\x00 "), "name": str(getattr(e, "name", "") or "")}
               for e in (getattr(s, "events", []) or [])]
        seqs.append({"name": s.name, "activity": getattr(s, "activity_name", "") or "", "frames": frames,
                     "fps": float(fps) if fps else None, "looping": bool(getattr(s, "flags", 0) & 1), "events": evs})
    bones = [b.name for b in mdl.bones]
    atts = [{"name": a.name, "bone": bones[a.parent_bone] if 0 <= a.parent_bone < len(bones) else None} for a in getattr(mdl, "attachments", [])]
    mats = [getattr(m, "name", str(m)) for m in getattr(mdl, "materials", [])]
    return {"mdl": os.path.relpath(path, SRC).replace("\\", "/"), "sequences": seqs, "bones": bones, "attachments": atts,
            "materials": mats, "sounds": sounds, "soundscripts": scripts, "weapon_script": wscript}

results = []
for v in views:
    try:
        results.append(inspect(v))
        r = results[-1]
        print("VIEWMODEL", r["mdl"], len(r["sequences"]), "sequences", len(r["bones"]), "bones", len(r["attachments"]), "attachments")
    except Exception as e:
        print("INSPECT FAILED", v, repr(e))
if not results:
    print("NO VIEWMODEL FOUND"); sys.exit(1)
results.sort(key=lambda r: (-len(r["sequences"]), -len(r["bones"]), "css" in r["mdl"].lower()))   # ties: more bones (fingers), then not the CS:S-hands variant
json.dump(results, open(os.path.join(OUT, "inspect.json"), "w"), indent=1)
prof = build_profile(results[0], meta.get("title", ""), meta.get("source"))
prof["slug"] = meta.get("slug", "")
prof["other_viewmodels"] = [r["mdl"] for r in results[1:]]
json.dump(prof, open(os.path.join(OUT, "profile.json"), "w"), indent=1)
print("ROLES", {k: v["sequence"] for k, v in prof["clips"].items()})
print("BONES", {k: v.get("bone") or v.get("attachment") for k, v in prof["bones"].items()})
print("MISSING", prof["missing_required"], "UNASSIGNED", prof["unassigned_sequences"], "MAG", prof["magSize"])
print("INSPECT DONE")
