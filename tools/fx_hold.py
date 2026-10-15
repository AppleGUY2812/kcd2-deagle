#!/usr/bin/env python3
"""How long the plugin must hold each impact / blood effect (fxs' last field, game seconds): the longest any of its
layers stays visible (spawn delay + emitter life + particle life; children started by their parent's particles on
top of the parent), + 0.15 s. Releasing a held emitter deletes it with its particles (native/src/main.cpp HoldPiece),
so a hold shorter than the effect cuts it; a longer one keeps emitters alive for nothing (KCD2's DX12 descriptor heap
ran out at 1024 held, 2026-10-15). Effects not listed are held 2 s.
usage: fx_hold.py            print the FX_HOLD table for src/Scripts/Deagle/effects.lua
       fx_hold.py --check    exit 1 if effects.lua's table differs from the libraries (run by tests/run_all.sh)"""
import math, os, re, sys
import xml.etree.ElementTree as ET

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
LIBS = ['src/Libs/Particles/deagle_impacts.xml', 'src/Libs/Particles/deagle_blood.xml']
LUA = os.path.join(ROOT, 'src/Scripts/Deagle/effects.lua')
MARGIN = 0.15


def num(v):
    try:
        return float((v or '').split(',')[0] or 0)
    except ValueError:
        return 0.0


def life(node, start=0.0):
    p, best = node.find('Params'), 0.0
    on = p is None or p.get('Enabled') != 'false'
    d = emit = plife = 0.0
    if p is not None and on:
        d = start + num(p.get('SpawnDelay'))
        cont = p.get('Continuous') == 'true'
        emit = (num(p.get('EmitterLifeTime')) or 1e9) if cont else 0.0
        plife = num(p.get('ParticleLifeTime'))
        best = d + emit + plife
    ch = node.find('Childs')
    for c in (ch.findall('Particles') if ch is not None else []):
        cp = c.find('Params')
        ind = cp.get('SpawnIndirection') if cp is not None else None
        base = (d + emit + (plife if ind == 'ParentDeath' else 0.0)) if (ind and on) else start
        best = max(best, life(c, base))
    return best


def table():
    rows = []
    for path in LIBS:
        lib = ET.parse(os.path.join(ROOT, path)).getroot()
        for e in lib.findall('Particles'):
            hold = math.ceil((life(e) + MARGIN) * 20 - 1e-9) / 20
            if hold < 10:
                rows.append('\t["%s.%s"] = %.2f,' % (lib.get('Name'), e.get('Name'), hold))
    return '\n'.join(rows)


if __name__ == '__main__':
    t = table()
    if '--check' in sys.argv:
        m = re.search(r'local FX_HOLD = \{\n(.*?)\n\}', open(LUA).read(), re.S)
        if not m or m.group(1) != t:
            print('FX_HOLD in effects.lua is out of date: run tools/fx_hold.py and paste its table')
            sys.exit(1)
        print('FX_HOLD OK')
    else:
        print('local FX_HOLD = {\n' + t + '\n}')
