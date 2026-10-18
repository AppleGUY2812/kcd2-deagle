# The viewmodel's .mtl for a Ready or Not gun (tools/gb/gb_convert.sh, profile "mtlFrom"): gb_build.py numbers the
# sub-materials by the order the meshes carry them (build.json "materials": id -> RoN material name); the materials
# themselves come from the converted RoN set (Claude 2's tools/ron/mk_grizzly_tex.py: <from>.mtl, one sub-material
# per RoN material, plus its exposure steps <from>_ev9/_ev8/_ev7.mtl for the emissive sight). Each <from>*.mtl
# becomes <out dir>/<slug>_vm*.mtl with its sub-materials in build.json's id order.
#   python3 ron_vm_mtl.py <build.json> <from prefix, e.g. src/Objects/manmade/weapons/ron_grizzly/ron_grizzly> <out dir> <slug>
import glob, json, os, re, sys

build, prefix, out_dir, slug = sys.argv[1:5]
order = [m["source"] for m in sorted(json.load(open(build))["materials"], key=lambda m: m["id"])]
for src in sorted(glob.glob(prefix + "*.mtl")):
    suffix = os.path.basename(src)[len(os.path.basename(prefix)):-4]
    if suffix and not re.fullmatch(r"_ev\d+", suffix):   # _silver, _casing: other models' sets
        continue
    t = open(src).read()
    subs = {m.group(1): m.group(0) for m in re.finditer(r'[ \t]*<Material Name="([^"]+)".*?</Material>', t, re.S)}
    missing = [n for n in order if n not in subs]
    if missing:
        sys.exit(f"{src}: no sub-material for {missing} (has {list(subs)})")
    head = t[:t.index("<SubMaterials>") + len("<SubMaterials>")]
    tail = t[t.index("</SubMaterials>"):]
    out = os.path.join(out_dir, f"{slug}_vm{suffix}.mtl")
    os.makedirs(out_dir, exist_ok=True)
    open(out, "w").write(head + "\n" + "\n".join(subs[n] for n in order) + "\n  " + tail)
    print("MTL", out, "<-", os.path.basename(src), "order", order)
