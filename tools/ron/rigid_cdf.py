#!/usr/bin/env python3
"""Puts a gun's rigid body pieces (tools/ron/mk_grizzly_rigid.py: one CGF per gun bone, in that bone's frame) into its
viewmodel .cdf as CA_BONE attachments, one per bone, RelRotation/RelPosition identity: world = joint x the piece, the
skinning itself. The script only hides and shows them (skin.lua SkinRigid).
Why the .cdf (2026-10-23): bone attachments made at run time on the viewmodel never sat on their bone in KCD2 (they
stayed at the model origin or the rest pose whatever SetAttachmentPos/Angles said, live, 4 tries) and drew stale models
(MODLOG 2026-10-14); the .cdf's own bone attachments sat on the muzzle (the flash cards). One model per bone here: the
flash cards' "every CGF on one bone draws the first" doesn't come up.
    python3 tools/ron/rigid_cdf.py [repo]          rewrites the rigid attachments of every gun with pieces
    from rigid_cdf import block; block(repo, slug)  the lines, for tools/gb/gb_stage.py"""
import os, re, sys

GUNS = {"ron_grizzly": {"cdf": "ron_grizzly_vm.cdf", "skin": "mirror", "prefix": "ron_grizzly_rigid_",
                        "mtl": "ron_grizzly_rigid_pom",
                        "bones": ["J_Gun", "tag_slide", "tag_sight", "tag_barrel", "tag_grip_safety", "tag_hammer",
                                  "tag_slidelock_safety", "tag_slidelock", "tag_trigger", "tag_mag_release"]}}


# Off since 2026-10-27: the pieces are world entities now (skin.lua SkinRigid, 7ccef84), and these .cdf copies DID draw:
# the script hid them once at skin apply, before a freshly pulled-out viewmodel had its attachments, so they stayed
# visible over the skinned gun (the user's "extra grip" on the mirror skin, 10-26 and 10-27; listatt: all 10 hidden 0).
# write() now only strips them. True puts them back.
IN_CDF = False


def block(repo, slug):
    g = GUNS.get(slug)
    if not g or not IN_CDF:
        return []
    game = f"objects/manmade/weapons/guns/{slug}/rigid"
    src = os.path.join(repo, "src/Objects/manmade/weapons/guns", slug, "rigid")
    out = []
    for b in g["bones"]:
        cgf = g["prefix"] + b.lower() + ".cgf"
        if not os.path.exists(os.path.join(src, cgf)):
            raise SystemExit(f"{slug}: {cgf} missing (build it: tools/ron/mk_grizzly_rigid.py)")
        out.append(f'    <Attachment Type="CA_BONE" AName="vm_rigid_{g["skin"]}_{b}" Binding="{game}/{cgf}" Material="{game}/{g["mtl"]}" '
                   f'RelRotation="1,0,0,0" RelPosition="0,0,0" BoneName="{b}" Flags="0" />')
    return out


def write(repo, slug):
    path = os.path.join(repo, "src/Objects/manmade/weapons/guns", slug, GUNS[slug]["cdf"])
    lines = [l for l in open(path).read().splitlines() if 'AName="vm_rigid_' not in l]
    end = next(i for i, l in enumerate(lines) if l.strip() == "</AttachmentList>")
    new = lines[:end] + block(repo, slug) + lines[end:]
    open(path, "w").write("\n".join(new) + "\n")
    return len(new) - len(lines)


if __name__ == "__main__":
    repo = sys.argv[1] if len(sys.argv) > 1 else "."
    for slug in GUNS:
        print(slug, write(repo, slug), "rigid pieces (lines added; negative = removed)")
