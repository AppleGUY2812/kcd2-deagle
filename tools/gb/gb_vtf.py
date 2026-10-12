# Blender (SourceIO): for each material of a converted gun (build.json), find its .vmt (this gun's mod first, then
# every other imported gun), read the maps and phong settings, decode the VTFs to raw RGBA .npy (top-down rows).
# Writes <gun dir>/tex/npy/*.npy and <gun dir>/tex/tex.json for tools/gb/gb_tex.py.
# usage: blender -b --factory-startup -P gb_vtf.py -- <gun dir> <guns root>
import sys, os, json, re, addon_utils
import numpy as np
GUN, ROOT = sys.argv[sys.argv.index("--") + 1:][:2]
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from profile_guess import parse_keyvalues
addon_utils.enable("SourceIO", default_set=True)
from SourceIO.library.utils.pylib.vtf import load_vtf_texture

build = json.load(open(os.path.join(GUN, "build.json")))
srcs = [os.path.join(GUN, "src")] + [os.path.join(ROOT, g, "src") for g in sorted(os.listdir(ROOT))
                                      if os.path.join(ROOT, g) != os.path.normpath(GUN) and os.path.isdir(os.path.join(ROOT, g, "src"))]
vmts, mat_dirs = {}, []
for s in srcs:
    for root, dirs, files in os.walk(s):
        if os.path.basename(root).lower() == "materials":
            mat_dirs.append(root)
        for f in files:
            if f.lower().endswith(".vmt"):
                vmts.setdefault(f[:-4].lower(), os.path.join(root, f))
def find_vtf(rel):
    rel = rel.replace("\\", "/").lower().strip("/")
    if not rel.endswith(".vtf"):
        rel += ".vtf"
    for d in mat_dirs:
        p = os.path.join(d, *rel.split("/"))
        if os.path.exists(p):
            return p
        # case-insensitive fallback
        cur = d
        for part in rel.split("/"):
            hit = next((e for e in os.listdir(cur) if e.lower() == part), None) if os.path.isdir(cur) else None
            if not hit:
                cur = None; break
            cur = os.path.join(cur, hit)
        if cur and os.path.exists(cur):
            return cur
    return None
os.makedirs(os.path.join(GUN, "tex", "npy"), exist_ok=True)
out = {}
for m in build["materials"]:
    name = m["source"]
    vmt = vmts.get(name.lower())
    entry = {"id": m["id"], "sub": m["sub"], "source": name, "vmt": vmt, "maps": {}, "params": {}}
    if vmt:
        kv = parse_keyvalues(open(vmt, encoding="utf-8", errors="replace").read())
        flat = {}
        def walk(items):
            for k, v in items:
                if isinstance(v, list): walk(v)
                elif k.startswith("$") and k.lower() not in flat: flat[k.lower()] = v
        walk(kv)
        entry["params"] = {k: v for k, v in flat.items() if k in ("$phongexponent", "$phongboost", "$normalmapalphaphongmask", "$basemapalphaphongmask",
                                                                   "$color2", "$blendtintbybasealpha", "$phongalbedotint", "$translucent", "$alphatest")}
        for key, tag in (("$basetexture", "base"), ("$bumpmap", "bump"), ("$normalmap", "bump"), ("$phongexponenttexture", "exp"), ("$envmapmask", "env")):
            if key in flat and tag not in entry["maps"]:
                p = find_vtf(flat[key])
                if not p:
                    print("MISSING VTF", name, key, flat[key]); continue
                try:
                    px, w, h, is_float = load_vtf_texture(open(p, "rb").read())     # top-down rows (as tools/vm_vtf_dump.py)
                    a = np.frombuffer(px, dtype=np.float32 if is_float else np.uint8).reshape(h, w, 4)
                    if is_float:
                        a = (np.clip(a, 0, 1) * 255).astype(np.uint8)
                    fn = f"{m['sub']}_{tag}.npy"
                    np.save(os.path.join(GUN, "tex", "npy", fn), a)
                    entry["maps"][tag] = fn
                except Exception as e:
                    print("DECODE FAILED", name, key, repr(e))
    out[name] = entry
    print("MATERIAL", name, "vmt" if vmt else "NO VMT", sorted(entry["maps"]))
json.dump(out, open(os.path.join(GUN, "tex", "tex.json"), "w"), indent=1)
print("VTF DONE")
