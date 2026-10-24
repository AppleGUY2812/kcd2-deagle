#!/usr/bin/env python3
"""The Grizzly body's rigid pieces for tools/ron/mk_grizzly_rigid.py (Silhouette POM, 2026-10-23): reads the viewmodel
export the game's .chr/.skin were compiled from (build/model/ron_grizzly_vm.dae), assigns every body triangle to the
bone its vertices ride (one bone each: checked, none spans two), and writes part_<bone>.npz with the triangle corners
in that bone's own frame (inverse bind matrix x bind shape x position; normals by the inverse transpose) + parts.json.
    python3 -I tools/ron/mk_grizzly_rigid_extract.py [dae] [out dir]"""
import sys, os, json, collections
import xml.etree.ElementTree as ET
import numpy as np

DAE = sys.argv[1] if len(sys.argv) > 1 else '/mnt/e/Tools/deagle-work/guns/ron_grizzly/build/model/ron_grizzly_vm.dae'
OUT = sys.argv[2] if len(sys.argv) > 2 else '/mnt/e/Tools/deagle-work/guns/ron_grizzly/rigid_src'
ns = {'c': 'http://www.collada.org/2005/11/COLLADASchema'}
root = ET.parse(DAE).getroot()
sk = [c for c in root.findall('.//c:controller', ns) if c.find('c:skin', ns) is not None and 'vm_skin_geometry' in c.find('c:skin', ns).get('source')][0]
s = sk.find('c:skin', ns)
srcs = {e.get('id'): e for e in s.findall('c:source', ns)}
joints = [t.split('%')[0] for t in next(e for k, e in srcs.items() if k.endswith('-joints')).find('c:IDREF_array', ns).text.split()]
mats = np.array(next(e for k, e in srcs.items() if k.endswith('-matrices')).find('c:float_array', ns).text.split(), float).reshape(-1, 4, 4)
wts = np.array(next(e for k, e in srcs.items() if k.endswith('-weights')).find('c:float_array', ns).text.split(), float)
bsm = np.array(s.find('c:bind_shape_matrix', ns).text.split(), float).reshape(4, 4)
vw = s.find('c:vertex_weights', ns)
vcount = np.array(vw.find('c:vcount', ns).text.split(), int)
v = np.array(vw.find('c:v', ns).text.split(), int).reshape(-1, 2)
dom = np.zeros(len(vcount), int); k = 0
for i, n in enumerate(vcount):
    pairs = v[k:k + n]; k += n
    dom[i] = pairs[np.argmax(wts[pairs[:, 1]]), 0]
mesh = [g for g in root.findall('.//c:geometry', ns) if g.get('id').endswith('vm_skin_geometry')][0].find('c:mesh', ns)
fa = {e.get('id'): np.array(e.find('c:float_array', ns).text.split(), float) for e in mesh.findall('c:source', ns)}
P = fa[mesh.find('c:vertices', ns).find("c:input[@semantic='POSITION']", ns).get('source')[1:]].reshape(-1, 3)
body = [t for t in mesh.findall('c:triangles', ns) if 'Body' in t.get('material')][0]
inputs = {i.get('semantic') + (i.get('set') or ''): (int(i.get('offset')), i.get('source')[1:]) for i in body.findall('c:input', ns)}
stride = max(o for o, _ in inputs.values()) + 1
idx = np.array(body.find('c:p', ns).text.split(), int).reshape(-1, 3, stride)
uvk = sorted(kk for kk in inputs if kk.startswith('TEXCOORD'))[0]
N = fa[inputs['NORMAL'][1]].reshape(-1, 3)
st = int(mesh.find(f"c:source[@id='{inputs[uvk][1]}']", ns).find('.//c:accessor', ns).get('stride'))
UV = fa[inputs[uvk][1]].reshape(-1, st)[:, :2]
vo, no, to = inputs['VERTEX'][0], inputs['NORMAL'][0], inputs[uvk][0]
tb = dom[idx[:, :, vo]]
span = int((tb.min(1) != tb.max(1)).sum())
if span: sys.exit(f'{span} body triangles span two bones: not rigid')
parts = collections.defaultdict(list)
for t in range(len(idx)): parts[joints[tb[t, 0]]].append(t)
os.makedirs(OUT, exist_ok=True)
out = {}
for b, ts in sorted(parts.items(), key=lambda kv: -len(kv[1])):
    inv = mats[joints.index(b)]
    tri = idx[ts]
    p = P[tri[:, :, vo]].reshape(-1, 3); n = N[tri[:, :, no]].reshape(-1, 3); uv = UV[tri[:, :, to]].reshape(-1, 2)
    # MODEL space (the bind pose, as the .skin holds it): CreateBoneAttachment(.., true) projects the attachment
    # (CE 5.7.1 CAttachmentBONE::ProjectAttachment: relative = inverse(default joint) x absolute default (identity)),
    # so the engine places the piece with its own joint frames: world = joint x inverse(default joint) x model vertex,
    # the skinning itself. The bone-space version (x the DAE's inverse bind, attached unprojected) landed rotated and
    # off the gun in game (the user's screenshot 2026-10-23): the DAE's frames are not the engine's.
    ph = np.c_[p, np.ones(len(p))] @ bsm.T
    nl = n @ np.linalg.inv(bsm[:3, :3]); nl /= np.linalg.norm(nl, axis=1, keepdims=True)
    np.savez(os.path.join(OUT, f'part_{b}.npz'), p=ph[:, :3], n=nl, uv=uv)
    out[b] = len(ts)
    print('%-22s tris %5d  model-space bbox %s .. %s' % (b, len(ts), ph[:, :3].min(0).round(3), ph[:, :3].max(0).round(3)))
json.dump(out, open(os.path.join(OUT, 'parts.json'), 'w'), indent=1)
