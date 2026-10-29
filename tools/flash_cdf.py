#!/usr/bin/env python3
"""Puts every mesh muzzle flash card into the viewmodels' .cdf: one skin attachment per card, built on that gun's
skeleton by tools/flash_skin.sh into <gun dir>/flash/<card>.skin, named after the card. The script only hides and shows
them (deagle_main.lua FlashMeshSetup). As bone attachments (a CGF on the muzzle bone, made at load or at run time) every
card drew the first card's model in KCD2 (in game 2026-10-14, MODLOG); the viewmodels' skins (gun, magazines, rounds)
each draw their own.

Checks every card skin first: its skeleton is the gun's .chr's byte for byte (a skin carries the skeleton it was
exported with), every vertex hangs on the muzzle bone alone, and both guns have the same cards.

    python3 tools/flash_cdf.py <repo>                    checks the skins, rewrites the flash attachments of every .cdf
    from flash_cdf import block; block(repo, gun_dir)    the lines, for tools/gb/gb_stage.py
"""
import os, re, struct, sys

FLASH_MTL = "objects/manmade/weapons/deagle/vm"   # the flash materials, shared by every gun
GUNS = {"src/Objects/manmade/weapons/deagle/vm": "deagle_vm",              # gun dir -> its .chr / .cdf
        "src/Objects/manmade/weapons/guns/glock_17": "glock_17_vm",
        "src/Objects/manmade/weapons/guns/ron_grizzly": "ron_grizzly_vm",
        "src/Objects/manmade/weapons/guns/ron_grizzly_henry": "ron_grizzly_henry_vm"}
MUZZLES = {"ron_grizzly_vm": "tag_muzzle", "ron_grizzly_henry_vm": "tag_muzzle"}   # the muzzle bone where it isn't tag_flash (Ready or Not guns)
MUZZLE = "tag_flash"


def chunks(path):
    d = open(path, "rb").read()
    _, num, toff = struct.unpack_from("<III", d, 4)
    out = {}
    for i in range(num):
        t, _, _, size, off = struct.unpack_from("<HHIII", d, toff + i * 16)
        out.setdefault(t, []).append(d[off:off + size])
    return out


def bone_index(chr_path, name):   # CompiledBones (0x2000): 32 bytes, then 584 per bone, its name at 312
    c = chunks(chr_path)[0x2000][0]
    names = [c[32 + i * 584 + 312: 32 + i * 584 + 568].split(b"\0")[0].decode() for i in range((len(c) - 32) // 584)]
    return names.index(name)


def cards(repo, gun_dir):
    d = os.path.join(repo, gun_dir, "flash")
    return sorted(f[:-5] for f in os.listdir(d) if f.endswith(".skin")) if os.path.isdir(d) else []


def check(repo):
    names = None
    for gun_dir, base in GUNS.items():
        chr_path = os.path.join(repo, gun_dir, base + ".chr")
        ref = chunks(chr_path)
        muzzle_bone = MUZZLES.get(base, MUZZLE)
        muzzle = bone_index(chr_path, muzzle_bone)
        mine = cards(repo, gun_dir)
        if not mine:
            raise SystemExit(f"{gun_dir}: no flash card skins (build them: tools/flash_skin.sh)")
        if names is not None and mine != names:
            raise SystemExit(f"{gun_dir}: other flash cards than the first gun's: {sorted(set(mine) ^ set(names))[:6]}")
        names = mine
        for c in mine:
            path = os.path.join(repo, gun_dir, "flash", c + ".skin")
            ch = chunks(path)
            for t in (0x2000, 0x2001):
                if ch.get(t) != ref.get(t):
                    raise SystemExit(f"{path}: skeleton chunk {t:#x} differs from {base}.chr (rebuild: tools/flash_skin.sh)")
            sv = ch[0x2005][0]   # CompiledIntSkinVertices: 32 bytes, then 64 per vertex: 3 Vec3, 4 bone ids (u16), 4 weights
            n = (len(sv) - 32) // 64
            for i in range(n):
                ids = struct.unpack_from("<4H", sv, 32 + i * 64 + 36)
                w = struct.unpack_from("<4f", sv, 32 + i * 64 + 44)
                on = {b: x for b, x in zip(ids, w) if x > 0}
                if set(on) != {muzzle} or abs(sum(on.values()) - 1) > 1e-3:
                    raise SystemExit(f"{path}: vertex {i} hangs on bones {on}, not on {muzzle_bone} ({muzzle}) alone")
    return names


def material(path):   # the material library a skin was exported with (its MtlName chunk, 0x1014)
    return chunks(path)[0x1014][0][:128].split(b"\0")[0].decode()


def block(repo, gun_dir):
    game = gun_dir.split("src/", 1)[1].lower()
    return [f'    <Attachment Type="CA_SKIN" AName="{c}" Binding="{game}/flash/{c}.skin" '
            f'Material="{FLASH_MTL}/{material(os.path.join(repo, gun_dir, "flash", c + ".skin"))}" Flags="0" />'
            for c in cards(repo, gun_dir)]


def write(repo, gun_dir, base):
    path = os.path.join(repo, gun_dir, base + ".cdf")
    lines = [l for l in open(path).read().splitlines() if not re.search(r'AName="deagle_vm_flash', l)]
    end = next(i for i, l in enumerate(lines) if l.strip() == "</AttachmentList>")
    new = lines[:end] + block(repo, gun_dir) + lines[end:]
    open(path, "w").write("\n".join(new) + "\n")
    return len(new) - len(lines)


if __name__ == "__main__":
    repo = sys.argv[1] if len(sys.argv) > 1 else "."
    n = len(check(repo))
    for gun_dir, base in GUNS.items():
        print(f"{gun_dir}/{base}.cdf: {write(repo, gun_dir, base)} flash card skins (all {n} checked)")
