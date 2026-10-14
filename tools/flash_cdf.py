#!/usr/bin/env python3
"""Puts every mesh muzzle flash card (src/Objects/manmade/weapons/deagle/vm/deagle_vm_flash*.cgf: ours from
tools/flash_mesh.py, Ready or Not's from tools/flash_mesh_ron.py) into the viewmodels' .cdf as bone attachments on the
muzzle bone, one per card, named after its CGF. The script only hides and shows them (deagle_main.lua FlashMeshSetup).
Attachments made while the game runs (CreateBoneAttachment + SetAttachmentCGF) drew stale models in KCD2: after a look
switch every card showed the same leftover flash, even an apple put on one (in game 2026-10-14, MODLOG). The ones a
.cdf makes at load draw what they hold, like the magazines and rounds.

    python3 tools/flash_cdf.py <repo>              rewrites the flash attachments of every viewmodel .cdf
    from flash_cdf import block; block(repo, bone)  the lines, for tools/gb/gb_stage.py
"""
import os, re, struct, sys

VM = "objects/manmade/weapons/deagle/vm"           # as the game names it
SRC = "src/Objects/manmade/weapons/deagle/vm"      # in the repo
CDFS = {"src/Objects/manmade/weapons/deagle/vm/deagle_vm.cdf": "tag_flash",
        "src/Objects/manmade/weapons/guns/glock_17/glock_17_vm.cdf": "tag_flash"}
NAME = re.compile(r"^deagle_vm_flash(_.+)?\.cgf$")


def material(path):   # the material a CGF was exported with (its MtlName chunk, 0x1014)
    d = open(path, "rb").read()
    _, num, toff = struct.unpack_from("<III", d, 4)
    for i in range(num):
        t, _, _, size, off = struct.unpack_from("<HHIII", d, toff + i * 16)
        if t == 0x1014:
            return d[off:off + 128].split(b"\0")[0].decode()
    raise SystemExit(f"{path}: no material chunk")


def cards(repo):
    src = os.path.join(repo, SRC)
    return sorted(f[:-4] for f in os.listdir(src) if NAME.match(f))


def block(repo, bone):
    src = os.path.join(repo, SRC)
    out = []
    for c in cards(repo):
        out.append(f'    <Attachment Type="CA_BONE" AName="{c}" Binding="{VM}/{c}.cgf" Material="{VM}/{material(os.path.join(src, c + ".cgf"))}" '
                   f'RelRotation="1,0,0,0" RelPosition="0,0,0" BoneName="{bone}" Flags="0" />')
    return out


def write(repo, rel, bone):
    path = os.path.join(repo, rel)
    lines = [l for l in open(path).read().splitlines() if not re.search(r'AName="deagle_vm_flash', l)]
    end = next(i for i, l in enumerate(lines) if l.strip() == "</AttachmentList>")
    new = lines[:end] + block(repo, bone) + lines[end:]
    open(path, "w").write("\n".join(new) + "\n")
    return len(new) - len(lines)


if __name__ == "__main__":
    repo = sys.argv[1] if len(sys.argv) > 1 else "."
    if not cards(repo):
        raise SystemExit("no flash cards in " + SRC + " (build them: tools/flash_mesh.py, tools/flash_mesh_ron.py)")
    for rel, bone in CDFS.items():
        print(rel, write(repo, rel, bone), "flash cards on", bone)
