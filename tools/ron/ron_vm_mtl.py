# The viewmodel's .mtl for a Ready or Not gun (tools/gb/gb_convert.sh, profile "mtlFrom"): gb_build.py numbers the
# sub-materials by the order the meshes carry them (build.json "materials": id -> RoN material name); the materials
# themselves come from the converted RoN set (Claude 2's tools/ron/mk_grizzly_tex.py: <from>.mtl, one sub-material
# per RoN material, plus its exposure steps <from>_ev9/_ev8/_ev7.mtl for the emissive sight). Each <from>*.mtl
# becomes <out dir>/<slug>_vm*.mtl with its sub-materials in build.json's id order. A skin set (<from>_silver*.mtl: RoN's
# other finish, a texture swap on the same mesh) becomes <slug>_vm_silver*.mtl: a slot whose RoN material it lacks takes
# the skin's variant of it (MI_PIS_GRIZZLY_Body -> MI_PIS_GRIZZLY_Body_Silver); the Lua deagle_skin swaps it in.
#   python3 ron_vm_mtl.py <build.json> <from prefix, e.g. src/Objects/manmade/weapons/ron_grizzly/ron_grizzly> <out dir> <slug>
import glob, json, os, re, sys

build, prefix, out_dir, slug = sys.argv[1:5]
order = [m["source"] for m in sorted(json.load(open(build))["materials"], key=lambda m: m["id"])]
for src in sorted(glob.glob(prefix + "*.mtl")):
    suffix = os.path.basename(src)[len(os.path.basename(prefix)):-4]
    skin = re.fullmatch(r"_([a-z]+)(_ev\d+)?", suffix)
    if suffix and not re.fullmatch(r"_ev\d+", suffix) and not (skin and skin.group(1) not in ("casing",)):
        continue   # _casing: another model's set
    t = open(src).read()
    subs = {m.group(1): m.group(0) for m in re.finditer(r'[ \t]*<Material Name="([^"]+)".*?</Material>', t, re.S)}
    if skin:
        for n in order:
            if n not in subs and n + "_" + skin.group(1).capitalize() in subs: subs[n] = subs[n + "_" + skin.group(1).capitalize()]
    missing = [n for n in order if n not in subs]
    if missing:
        sys.exit(f"{src}: no sub-material for {missing} (has {list(subs)})")
    head = t[:t.index("<SubMaterials>") + len("<SubMaterials>")]
    tail = t[t.index("</SubMaterials>"):]
    out = os.path.join(out_dir, f"{slug}_vm{suffix}.mtl")
    os.makedirs(out_dir, exist_ok=True)
    open(out, "w").write(head + "\n" + "\n".join(subs[n] for n in order) + "\n  " + tail)
    print("MTL", out, "<-", os.path.basename(src), "order", order)
