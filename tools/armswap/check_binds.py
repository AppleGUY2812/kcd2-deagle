#!/usr/bin/env python3
"""Compare a built gun's exported joint binds with the game's own male_body_arms/hands binds.

CryEngine's .chr default pose comes from the DAE's skin INV_BIND_MATRIX (the Blender exporter's
transform_bone_matrix). A .skin attached in the .cdf is bound to that default pose; if it differs from the game's
bind for the same joint, the skin draws as stretched loops around the camera.

usage: python3 tools/armswap/check_binds.py [slug] [--quiet]
  DAE:  /mnt/e/Tools/deagle-work/guns/<slug>/build/model/<slug>_vm.dae
  game: /mnt/e/Tools/deagle-work/henry/Objects/characters/humans/male/body/*.glb
Missing DAE or glbs = SKIP, exit 0 (tests/run_all.sh may run this off the build machine).
"""
import json, os, struct, sys
import xml.etree.ElementTree as ET
import numpy as np

NS = '{http://www.collada.org/2005/11/COLLADASchema}'
GAME_BODY = os.environ.get('HENRYVM_BODY', '/mnt/e/Tools/deagle-work/henry/Objects/characters/humans/male/body')
GUNS = os.environ.get('HENRYVM_GUNS', '/mnt/e/Tools/deagle-work/guns')
CONV = np.array([[1, 0, 0, 0], [0, 0, -1, 0], [0, 1, 0, 0], [0, 0, 0, 1]], float)
POS_TOL, ROT_TOL = 0.1, 0.01


def read_glb(path):
    data = open(path, 'rb').read()
    magic, version, length = struct.unpack_from('<III', data, 0)
    assert magic == 0x46546C67, 'not a glb'
    off, gltf, binary = 12, None, None
    while off < length:
        clen, ctype = struct.unpack_from('<II', data, off)
        chunk = data[off + 8: off + 8 + clen]
        if ctype == 0x4E4F534A:
            gltf = json.loads(chunk)
        elif ctype == 0x004E4942:
            binary = chunk
        off += 8 + clen
    return gltf, binary


def game_binds():
    out = {}
    for name in ('male_body_arms.glb', 'male_body_hands.glb'):
        path = os.path.join(GAME_BODY, name)
        gltf, binary = read_glb(path)
        skin = gltf['skins'][0]
        acc = gltf['accessors'][skin['inverseBindMatrices']]
        view = gltf['bufferViews'][acc['bufferView']]
        base = view.get('byteOffset', 0) + acc.get('byteOffset', 0)
        for k, node in enumerate(skin['joints']):
            vals = struct.unpack_from('<16f', binary, base + k * 64)
            ibm = np.array(vals).reshape(4, 4).T
            bind = np.linalg.inv(ibm)
            out[gltf['nodes'][node]['name']] = CONV @ bind @ CONV.T
    return out


def dae_binds(dae):
    root = ET.parse(dae).getroot()
    for ctrl in root.findall(NS + 'library_controllers/' + NS + 'controller'):
        if not ctrl.get('id').endswith('_vm_skin'):
            continue
        skin = ctrl.find(NS + 'skin')
        srcs = {}
        for src in skin.findall(NS + 'source'):
            arr = list(src)[0]
            vals = (arr.text or '').split() if arr.tag == NS + 'IDREF_array' else [float(x) for x in (arr.text or '').split()]
            srcs[src.get('id')] = vals
        ji = skin.find(NS + 'joints')
        ns = next(i.get('source') for i in ji.findall(NS + 'input') if i.get('semantic') == 'JOINT')[1:]
        ms = next(i.get('source') for i in ji.findall(NS + 'input') if i.get('semantic') == 'INV_BIND_MATRIX')[1:]
        names = [n.split('%')[0] for n in srcs[ns]]
        mats = np.array(srcs[ms]).reshape(-1, 4, 4)
        return {n: np.linalg.inv(m) for n, m in zip(names, mats)}
    raise RuntimeError('no _vm_skin controller in ' + dae)


def main():
    slug = next((a for a in sys.argv[1:] if not a.startswith('-')), 'ron_grizzly_henry')
    quiet = '--quiet' in sys.argv
    dae = os.path.join(GUNS, slug, 'build', 'model', slug + '_vm.dae')
    if not os.path.exists(dae) or not os.path.exists(os.path.join(GAME_BODY, 'male_body_arms.glb')):
        print('SKIP check_binds: no %s or game body glbs' % dae)
        return 0
    G, D = game_binds(), dae_binds(dae)
    rows, missing = [], []
    for n, g in G.items():
        if n not in D:
            missing.append(n)
            continue
        err = D[n] @ np.linalg.inv(g)
        pos = float(np.linalg.norm(err[:3, 3])) * 1000.0
        r = err[:3, :3]
        det = float(np.linalg.det(r))
        # exported float32 binds carry a ~1e-6 scale; compare the rotation part without that scale (otherwise arccos
        # near 1 turns the scale into a fake 0.07 deg)
        if det > 0:
            r = r / (det ** (1.0 / 3.0))
        rot = float(np.degrees(np.arccos(np.clip((np.trace(r) - 1) / 2, -1, 1))))
        rows.append((pos, rot, n))
    rows.sort(reverse=True)
    if not quiet:
        for pos, rot, n in sorted(rows, key=lambda r: r[2].lower()):
            print('%-24s pos %9.3f mm   rot %8.3f deg' % (n, pos, rot))
    max_pos, pos_name = max(((r[0], r[2]) for r in rows), default=(0, ''))
    max_rot, rot_name = max(((r[1], r[2]) for r in rows), default=(0, ''))
    print('check_binds %s: %d joints, max pos %.3f mm (%s), max rot %.3f deg (%s), missing %d' % (
        slug, len(rows), max_pos, pos_name, max_rot, rot_name, len(missing)))

    if missing:
        print('missing from DAE:', ', '.join(sorted(missing)[:8]))
    return 0 if not missing and max_pos <= POS_TOL and max_rot <= ROT_TOL else 1


if __name__ == '__main__':
    sys.exit(main())
