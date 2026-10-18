"""Add Ready or Not Cascade particle systems that aren't in a surface table (e.g. the weapons' ricochet effect,
P_Bullet_Ricochet, loaded by the weapon constructor) to fx_collect.json, decoded like tools/ron/fxcollect.py does,
so tools/ron/cascade_sim.py can play them.

    python3 -I tools/ron/cascade_add.py /Game/ReadyOrNot/VFX/VFXImpacts/Edit/P_Bullet_Ricochet [...]
"""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from usmap import U  # noqa: E402
from dump import USMAP, gamepath, class_schema  # noqa: E402
from uasset import Pkg, R  # noqa: E402
from tex import extract_many  # noqa: E402
import cascade_sim  # noqa: E402

u = U(USMAP)
data = json.load(open(cascade_sim.FX))
for gp in sys.argv[1:]:
    extract_many([gp])
    base = gamepath(gp)
    p = Pkg(base, u); p.fullrefs = True
    mods = {}
    for i, e in enumerate(p.exports):
        c = p.ref(e['cls'])
        if not (str(c).startswith('ParticleModule') or c in ('ParticleSpriteEmitter', 'ParticleSystem', 'ParticleLODLevel')): continue
        try:
            n, ex = class_schema(p, e['cls'], u); d = p.props(R(p.data(e)), n, ex)
        except Exception as x:
            d = {'__error__': repr(x)}
        mods[p.ref(i + 1)] = {'cls': c, **{a: b for a, b in d.items() if a != 'LODValidity'}}
    data['particle_systems'][gp] = {'type': 'Cascade', 'modules': mods, 'materials': sorted(set(
        p.imports[-o - 1][1] for c, n, o in p.imports if c in ('Material', 'MaterialInstanceConstant') and o < 0))}
    print(gp, len(mods), 'modules; materials', data['particle_systems'][gp]['materials'])
json.dump(data, open(cascade_sim.FX, 'w'), indent=1, default=str)
