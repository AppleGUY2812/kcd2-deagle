#!/usr/bin/env python3
"""CS2 weapon stats from weapons.vdata (KV3 text, SteamDB GameTracking-CS2 mirrors it every patch):
damage, fire rate, full-auto, headshot multiplier, armor penetration, range falloff, spread / inaccuracy and the
recoil parameters, with `_base` inheritance resolved. Values that are [primary, secondary-mode] pairs keep both.
usage: cs2_stats.py <weapons.vdata> [weapon_deagle ...]   (no names: list every weapon class)
       as a module: load(path) -> {class: {field: value}}"""
import re, sys, json

def parse_kv3(text):
    text = re.sub(r"<!--.*?-->", "", text, flags=re.S)
    toks = re.findall(r'"(?:[^"\\]|\\.)*"|[{}\[\]=,]|[^\s{}\[\]=,"]+', text)
    pos = 0
    def value():
        nonlocal pos
        t = toks[pos]; pos += 1
        if t == "{":
            obj = {}
            while toks[pos] != "}":
                k = toks[pos].strip('"'); pos += 1
                assert toks[pos] == "=", (k, toks[pos]); pos += 1
                obj[k] = value()
                if toks[pos] == ",": pos += 1
            pos += 1
            return obj
        if t == "[":
            arr = []
            while toks[pos] != "]":
                arr.append(value())
                if toks[pos] == ",": pos += 1
            pos += 1
            return arr
        if t.startswith('"'):
            return t[1:-1]
        if t in ("true", "false"):
            return t == "true"
        if re.fullmatch(r"[-+]?\d+", t):
            return int(t)
        if re.fullmatch(r"[-+]?(\d+\.\d*|\.\d+|\d+)([eE][-+]?\d+)?", t):
            return float(t)
        if pos < len(toks) and toks[pos].startswith('"') and re.fullmatch(r"\w+:", t):   # resource:"path"
            pos += 1
            return toks[pos - 1][1:-1]
        return t
    return value()

FIELDS = ["m_nDamage", "m_flCycleTime", "m_bIsFullAuto", "m_flHeadshotMultiplier", "m_flArmorRatio", "m_flRange",
          "m_flRangeModifier", "m_flPenetration", "m_iMaxClip1", "m_nNumBullets", "m_flSpread", "m_flInaccuracyStand",
          "m_flInaccuracyCrouch", "m_flInaccuracyMove", "m_flInaccuracyJumpInitial", "m_flRecoveryTimeStand",
          "m_flRecoilMagnitude", "m_flRecoilMagnitudeVariance", "m_flRecoilAngle", "m_flRecoilAngleVariance",
          "m_nRecoilSeed", "m_flMaxSpeed", "m_bHasBurstMode", "m_bIsRevolver", "m_nBurstShotCount"]

def load(path):
    root = parse_kv3(open(path, encoding="utf-8", errors="replace").read())
    entries = {}
    def collect(name, node):
        if isinstance(node, dict):
            if "_class" in node or "_base" in node or name.startswith("weapon_") or name == "statted_item_base":
                entries[name] = node
            for k, v in node.items():
                collect(k, v)
    collect("", root)
    def resolved(name, seen=()):
        node = entries.get(name, {})
        base = node.get("_base")
        out = dict(resolved(base, seen + (name,))) if base and base in entries and base not in seen else dict(entries.get("statted_item_base", {}))
        out.update({k: v for k, v in node.items() if not k.startswith("_")})
        out["_class"] = node.get("_class", out.get("_class"))
        return out
    # keyed by the entry's own name: the R8 Revolver is "weapon_revolver" but reuses the class weapon_deagle
    weapons = {name: resolved(name) for name in entries if name.startswith("weapon_") and not name.endswith("_prefab")}
    return {name: {f: w.get(f) for f in FIELDS if f in w} for name, w in weapons.items()}

if __name__ == "__main__":
    stats = load(sys.argv[1])
    names = sys.argv[2:]
    if not names:
        print(" ".join(sorted(stats)))
    for n in names:
        print(n, json.dumps(stats.get(n), indent=None))
