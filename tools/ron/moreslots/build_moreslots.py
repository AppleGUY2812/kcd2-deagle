"""Rebuild the "MoreSlots" Ready or Not mod against the CURRENT game files.

What the original mod (ExsaitX MoreSlots1000) does: it sets SWATArmour.TotalSlots = 1000 on the class defaults of
the four ArmorRevised body-armour blueprints. This script takes the game's own current copies of those four packages
and changes only that one int32 in each .uexp; every other byte is the game's. Then it packs them with repak
(pak v11, uncompressed, same mount point and paths as the old mod) and verifies the result.

usage (WSL, from anywhere):
  python3 -I build_moreslots.py <outdir> [--slots 1000] [--paks DIR] [--repak PATH] [--usmap PATH] [--ooz PATH]
outdir gets: vanilla/ (game copies), stage/ (patched files), pakchunk99-Mods_MoreSlots_Rebuilt_P.pak,
verify/ (files re-extracted from the built pak), build_report.json. Read-only on the game folder.
"""
import sys, os, json, argparse, subprocess, shutil, struct, hashlib

HERE = os.path.dirname(os.path.abspath(__file__))
RON = os.path.dirname(HERE)                          # tools/ron (usmap.py, uasset.py, dump.py, ronx.py, pakls.py)
GAME = "/mnt/c/Program Files (x86)/Steam/steamapps/common/Ready Or Not"
REL = "ReadyOrNot/Content/Blueprints/Items/ArmorRevised"
MOUNT = "../../../" + REL + "/"                      # the old mod's mount point (read from its pak index)
ASSETS = ["Armor_Heavy_V2", "Armor_Light_V2", "Armor_NoArmor", "Armor_StabVest"]
PAKNAME = "pakchunk99-Mods_MoreSlots_Rebuilt_P"
PROP = "TotalSlots"

ap = argparse.ArgumentParser()
ap.add_argument("outdir")
ap.add_argument("--slots", type=int, default=1000)
ap.add_argument("--paks", default=GAME + "/ReadyOrNot/Content/Paks")
ap.add_argument("--repak", default="/mnt/e/Tools/repak-v0.2.3/repak_cli-x86_64-unknown-linux-gnu/repak")
ap.add_argument("--usmap", default="/mnt/e/Tools/deagle-work/ron/exporter/Mappings.usmap")
ap.add_argument("--ooz", default=os.path.expanduser("~/.cache/kcd2-deagle/libooz.so"))
a = ap.parse_args()
out = os.path.abspath(a.outdir); van = out + "/vanilla"; stage = out + "/stage"; ver = out + "/verify"
for d in (van, stage, ver):
    shutil.rmtree(d, ignore_errors=True); os.makedirs(d)

# dump.py reads RON_OUT / RON_PAKS at import (to extract parent blueprints such as BaseBodyArmor on demand)
os.environ.update(RON_OUT=van, RON_PAKS=a.paks, RON_USMAP=a.usmap, RON_OOZ=a.ooz, RON_WORK=out)
sys.path.insert(0, RON)
from usmap import U
from uasset import Pkg, R
import dump

def run(*cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode: sys.exit(f"FAILED {cmd}\n{r.stdout}\n{r.stderr}")
    return r.stdout

# 1. the game's current copies (vanilla pakchunk<N>-Windows.pak only; mod paks are skipped by ronx.py)
rx = "^" + REL + "/(" + "|".join(ASSETS) + r")\.(uasset|uexp)$"
print(run(sys.executable, "-I", RON + "/ronx.py", a.paks, van, a.ooz, rx).strip())
u = U(a.usmap)

def decode(base):
    """all exports of a package -> {export: props}. The class defaults must decode byte-exact (export size minus the
    4-byte trailer); components carry native data after their properties, so for them only errors count."""
    res = dump.dump(base, u)
    for k, v in res.items():
        if "error" in v: raise SystemExit(f"{base}: export {k} failed to decode: {v['error']}")
        c, s = map(int, v["consumed"].split("/"))
        if k.startswith("Default__") and c != s - 4:
            raise SystemExit(f"{base}: class defaults decoded {c}/{s} bytes (mappings out of date?)")
    return res

def cdo(res):
    k = [k for k in res if k.startswith("Default__")]; assert len(k) == 1, k; return k[0]

def flat_diff(x, y, path=""):
    if isinstance(x, dict) and isinstance(y, dict):
        d = []
        for k in sorted(set(x) | set(y), key=str):
            if k not in x or k not in y: d.append((path + "/" + str(k), x.get(k, "<absent>"), y.get(k, "<absent>")))
            else: d += flat_diff(x[k], y[k], path + "/" + str(k))
        return d
    return [] if x == y else [(path, x, y)]

report = dict(slots=a.slots, prop=PROP, assets={})
for name in ASSETS:
    base = f"{van}/{REL}/{name}"
    before = decode(base); ck = cdo(before)
    old = before[ck]["props"].get(PROP)
    if not isinstance(old, int) or old == 0:
        sys.exit(f"{name}: {PROP} is {old!r} in the game file (not serialized as a nonzero int); "
                 "a same-size patch is impossible, the property would have to be added to the unversioned header")
    p = Pkg(base, u); e = [e for e in p.exports if e["name"] == ck][0]
    lo = e["off"] - p.hdrsize; hi = lo + e["size"]
    uexp = open(base + ".uexp", "rb").read()
    # candidates: every int32 == old value inside the CDO export; keep the one whose patch changes only PROP
    hits = []
    for off in range(lo, hi - 3):
        if struct.unpack_from("<i", uexp, off)[0] != old: continue
        tmp = f"{out}/_probe/{REL}/{name}"; os.makedirs(os.path.dirname(tmp), exist_ok=True)
        shutil.copyfile(base + ".uasset", tmp + ".uasset")
        b = bytearray(uexp); struct.pack_into("<i", b, off, a.slots); open(tmp + ".uexp", "wb").write(b)
        try: after = decode(tmp)
        except SystemExit: continue
        if flat_diff(before, after) == [(f"/{ck}/props/{PROP}", old, a.slots)]: hits.append(off)
    shutil.rmtree(out + "/_probe", ignore_errors=True)
    if len(hits) != 1: sys.exit(f"{name}: expected exactly one {PROP} offset, found {hits}")
    b = bytearray(uexp); struct.pack_into("<i", b, hits[0], a.slots)
    dst = f"{stage}/{name}"
    shutil.copyfile(base + ".uasset", dst + ".uasset"); open(dst + ".uexp", "wb").write(b)
    report["assets"][name] = dict(old=old, new=a.slots, uexp_offset=hits[0],
                                  game_uasset_sha1=hashlib.sha1(open(base + ".uasset", "rb").read()).hexdigest(),
                                  game_uexp_sha1=hashlib.sha1(uexp).hexdigest())
    print(f"{name}: {PROP} {old} -> {a.slots} (uexp offset {hits[0]})")

# 2. pack: mount point = the old mod's, files flat under it (= ReadyOrNot/Content/Blueprints/Items/ArmorRevised/*)
packdir = f"{out}/{PAKNAME}"; shutil.rmtree(packdir, ignore_errors=True); shutil.copytree(stage, packdir)
pak = f"{out}/{PAKNAME}.pak"
if os.path.exists(pak): os.remove(pak)
run(a.repak, "pack", "--version", "V11", "--mount-point", MOUNT, "--quiet", packdir, pak)
shutil.rmtree(packdir)

# 3. verify: header, listing, byte-exact round trip, decoded diff vs the game's files
f = open(pak, "rb"); size = os.path.getsize(pak); f.seek(size - 300); t = f.read()
i = t.rfind(struct.pack("<I", 0x5A6F12E1)); pver, io, isz = struct.unpack_from("<Iqq", t, i + 4)
f.seek(io); ib = f.read(isz); n, = struct.unpack_from("<i", ib, 0); mount = ib[4:4 + n - 1].decode()
assert pver == 11, pver; assert mount == MOUNT, mount; assert t[i - 1] == 0, "index encrypted flag set"
os.makedirs(f"{ver}/pak", exist_ok=True); os.link(pak, f"{ver}/pak/{PAKNAME}.pak")
run(sys.executable, "-I", RON + "/pakls.py", f"{ver}/pak", f"{ver}/list.txt", f"{PAKNAME}.pak")
listing = open(f"{ver}/list.txt").read().split()
want = sorted(MOUNT + n + x for n in ASSETS for x in (".uasset", ".uexp"))
assert sorted(x.replace(MOUNT + "/", MOUNT) for x in listing) == want, listing
env = dict(os.environ, RON_ALL_PAKS="1")
r = subprocess.run([sys.executable, "-I", RON + "/ronx.py", f"{ver}/pak", f"{ver}/out", a.ooz, "."],
                   capture_output=True, text=True, env=env); assert "extracted 8" in r.stdout, r.stdout + r.stderr
os.remove(f"{ver}/pak/{PAKNAME}.pak"); os.rmdir(f"{ver}/pak")
for name in ASSETS:
    for x in (".uasset", ".uexp"):
        assert open(f"{ver}/out/{REL}/{name}{x}", "rb").read() == open(f"{stage}/{name}{x}", "rb").read(), name + x
    gb = open(f"{van}/{REL}/{name}.uexp", "rb").read(); pb = open(f"{stage}/{name}.uexp", "rb").read()
    nbytes = sum(1 for p, q in zip(gb, pb) if p != q)
    d = flat_diff(decode(f"{van}/{REL}/{name}"), decode(f"{ver}/out/{REL}/{name}"))
    report["assets"][name].update(bytes_changed=nbytes, decoded_diff=[list(x) for x in d])
    assert len(gb) == len(pb) and nbytes <= 4 and len(d) == 1 and d[0][0].endswith("/props/" + PROP), (name, d)
    print(f"verify {name}: round trip byte-exact, {nbytes} uexp bytes differ, decoded diff = {d}")
report.update(pak=pak, pak_size=size, pak_sha1=hashlib.sha1(open(pak, "rb").read()).hexdigest(),
              pak_version=pver, mount=mount, listing=listing)
json.dump(report, open(out + "/build_report.json", "w"), indent=1)
print(f"OK {pak} ({size} bytes)")
