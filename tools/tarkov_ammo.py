#!/usr/bin/env python3
"""Fetch every Escape from Tarkov ammo item from the public tarkov.dev GraphQL API.

Writes docs/cartridges/tarkov_ammo.json (sorted by caliber, then name). Python stdlib only.

Fields: it introspects the `Ammo` type and the `ItemPropertiesAmmo` type (the second one carries
bulletMassGrams, ballisticCoeficient, bulletDiameterMilimeters, durabilityBurnFactor, heatFactor,
misfireChance ... which `Ammo` itself lacks) and requests every non-deprecated scalar field of both.
If introspection fails it uses the field list from the API's own schema
(github.com/the-hideout/tarkov-api, schema-static.mjs, 2026-10). It also stores `armorMaterials`
(destructibility per material) for the armor notes in docs/cartridges.md.

Fallback (--source auto, the default): if tarkov.dev keeps answering "GraphQL server unavailable",
the script reads the same raw game item templates from the SPT project's database
(github.com/sp-tarkov/server-csharp, SPT_Data/database/templates/items.json, Git LFS) and maps them
with tarkov.dev's own field mapping (tarkov-data-manager, modules/get-item-properties.mjs), so the
output has the same shape. The JSON's "source" says which one was used; SPT-only raw fields that
tarkov.dev drops (SpeedRetardation, TracerDistance, PenetrationDamageMod, fragment counts, ...) are
kept under "sptExtra". Rerun with --source api once tarkov.dev is back to get the live numbers.

Usage:  python3 tools/tarkov_ammo.py [--source auto|api|spt] [--game-mode regular|pve] [--out PATH]
        python3 tools/tarkov_ammo.py --tables     (print the docs/cartridges.md tables from the saved JSON)
"""
import argparse
import datetime
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request

API = "https://api.tarkov.dev/graphql"
UA = "kcd2-deagle-tarkov-ammo/1.1 (+private mod research; python urllib)"
SCALARS = {"Int", "Float", "Boolean", "String", "ID"}

# Field list if introspection is unavailable (the API's schema-static.mjs, 2026-10).
FALLBACK_AMMO = [
    "weight", "caliber", "stackMaxSize", "tracer", "tracerColor", "ammoType", "projectileCount",
    "damage", "armorDamage", "fragmentationChance", "ricochetChance", "penetrationChance",
    "penetrationPower", "penetrationPowerDeviation", "accuracyModifier", "recoilModifier",
    "initialSpeed", "lightBleedModifier", "heavyBleedModifier", "staminaBurnPerDamage",
]
FALLBACK_PROPS = FALLBACK_AMMO[1:] + [
    "durabilityBurnFactor", "heatFactor", "ballisticCoeficient", "bulletDiameterMilimeters",
    "bulletMassGrams", "misfireChance", "failureToFeedChance",
]
ITEM_FIELDS = ["id", "name", "shortName", "normalizedName", "weight", "wikiLink"]

# SPT fallback: raw BSG templates. Mapping copied from tarkov-data-manager get-item-properties.mjs.
SPT_REPO = "https://raw.githubusercontent.com/sp-tarkov/server-csharp/main/"
SPT_DB = "Libraries/SPTarkov.Server.Assets/SPT_Data/database/"
SPT_CORE = "Libraries/SPTarkov.Server.Assets/SPT_Data/configs/core.json"
AMMO_PARENT = "5485a8684bdc2da71d8b4567"
SPT_MAP = {
    "ballisticCoeficient": "BallisticCoeficient", "bulletDiameterMilimeters": "BulletDiameterMilimeters",
    "bulletMassGrams": "BulletMassGram", "caliber": "Caliber", "stackMaxSize": "StackMaxSize",
    "tracer": "Tracer", "tracerColor": "TracerColor", "ammoType": "ammoType",
    "projectileCount": "ProjectileCount", "damage": "Damage", "armorDamage": "ArmorDamage",
    "fragmentationChance": "FragmentationChance", "ricochetChance": "RicochetChance",
    "penetrationChance": "PenetrationChanceObstacle", "penetrationPower": "PenetrationPower",
    "penetrationPowerDeviation": "PenetrationPowerDiviation", "initialSpeed": "InitialSpeed",
    "heavyBleedModifier": "HeavyBleedingDelta", "lightBleedModifier": "LightBleedingDelta",
    "durabilityBurnFactor": "DurabilityBurnModificator", "heatFactor": "HeatFactor",
    "staminaBurnPerDamage": "StaminaBurnPerDamage", "misfireChance": "MalfMisfireChance",
    "failureToFeedChance": "MalfFeedChance", "weight": "Weight",
}
SPT_EXTRA = [
    "SpeedRetardation", "TracerDistance", "PenetrationDamageMod", "FragmentsCount", "MinFragmentsCount",
    "MaxFragmentsCount", "FragmentType", "AmmoLifeTimeSec", "ammoDist", "ammoHear", "ammoSfx",
    "buckshotBullets", "casingMass", "ExplosionStrength", "MinExplosionDistance", "MaxExplosionDistance",
    "IsLightAndSoundShot",
]


class ApiDown(Exception):
    pass


def http(url, data=None, headers=None, timeout=120):
    h = {"User-Agent": UA}
    h.update(headers or {})
    req = urllib.request.Request(url, data=data, headers=h, method="POST" if data else "GET")
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read()


def post(query, variables=None, tries=6):
    """POST a GraphQL query; retry on network errors and on the API's 'server unavailable' reply."""
    body = json.dumps({"query": query, "variables": variables or {}}).encode()
    delay, last = 5, None
    for attempt in range(1, tries + 1):
        try:
            try:
                raw = http(API, body, {"Content-Type": "application/json", "Accept": "application/json"}, timeout=90)
            except urllib.error.HTTPError as e:  # the API sends its errors (e.g. 422 "unavailable") as JSON
                raw = e.read() or json.dumps({"errors": [f"HTTP {e.code}"]}).encode()
            reply = json.loads(raw.decode("utf-8"))
        except (urllib.error.URLError, TimeoutError, json.JSONDecodeError) as e:
            last = repr(e)
        else:
            if reply.get("data") is not None:
                if reply.get("errors"):  # partial data: keep it, report the errors
                    print("warning: GraphQL errors:", json.dumps(reply["errors"])[:500], file=sys.stderr)
                return reply["data"]
            last = json.dumps(reply.get("errors"))[:300]
        if attempt < tries:
            print(f"attempt {attempt}/{tries} failed: {last}; retrying in {delay}s", file=sys.stderr)
            time.sleep(delay)
            delay = min(delay * 2, 120)
    raise ApiDown(f"tarkov.dev API failed after {tries} tries: {last}")


def base_type(t):
    while t and t.get("kind") in ("NON_NULL", "LIST"):
        t = t.get("ofType")
    return t or {}


def scalar_fields(type_name):
    """Non-deprecated scalar fields of a GraphQL type, via introspection."""
    q = """query($n: String!) { __type(name: $n) { fields { name isDeprecated
             type { kind name ofType { kind name ofType { kind name ofType { kind name } } } } } } }"""
    t = post(q, {"n": type_name}, tries=2)["__type"]
    if not t:
        raise RuntimeError(f"type {type_name} not found")
    return [f["name"] for f in t["fields"]
            if base_type(f["type"]).get("kind") == "SCALAR"
            and base_type(f["type"]).get("name") in SCALARS and not f.get("isDeprecated")]


def from_api(game_mode, tries):
    try:
        ammo_fields = scalar_fields("Ammo")
        prop_fields = scalar_fields("ItemPropertiesAmmo")
        field_source = "introspection"
    except (RuntimeError, KeyError, TypeError, ApiDown) as e:
        print(f"introspection failed ({e}); using the built-in field list", file=sys.stderr)
        ammo_fields, prop_fields, field_source = FALLBACK_AMMO, FALLBACK_PROPS, "built-in list (schema-static.mjs)"
    print(f"Ammo fields ({field_source}): {', '.join(ammo_fields)}", file=sys.stderr)
    print(f"ItemPropertiesAmmo fields: {', '.join(prop_fields)}", file=sys.stderr)
    query = (
        "query($gm: GameMode) { ammo(gameMode: $gm) { " + " ".join(ammo_fields)
        + " item { " + " ".join(ITEM_FIELDS)
        + " properties { __typename ... on ItemPropertiesAmmo { " + " ".join(prop_fields) + " } } } }"
        + " armorMaterials { id name destructibility explosionDestructibility"
        + " minRepairDegradation maxRepairDegradation } }"
    )
    data = post(query, {"gm": game_mode}, tries=tries)
    return {
        "source": API, "gameMode": game_mode, "fieldSource": field_source,
        "ammoFields": ammo_fields, "itemPropertiesAmmoFields": prop_fields,
        "ammo": data.get("ammo") or [], "armorMaterials": data.get("armorMaterials") or [],
    }


def lfs_get(path):
    """Download a Git-LFS file of the SPT repo: pointer from GitHub raw, object from their LFS server."""
    ptr = http(SPT_REPO + path).decode()
    oid = re.search(r"oid sha256:([0-9a-f]{64})", ptr)
    size = re.search(r"size (\d+)", ptr)
    if not (oid and size):  # not an LFS pointer: it is the file itself
        return ptr.encode()
    lfs = re.search(r"url\s*=\s*(\S+)", http(SPT_REPO + ".lfsconfig").decode()).group(1).rstrip("/")
    req = {"operation": "download", "transfers": ["basic"],
           "objects": [{"oid": oid.group(1), "size": int(size.group(1))}]}
    hdr = {"Accept": "application/vnd.git-lfs+json", "Content-Type": "application/vnd.git-lfs+json"}
    obj = json.loads(http(lfs + "/objects/batch", json.dumps(req).encode(), hdr))["objects"][0]
    if obj.get("error"):
        raise RuntimeError(f"LFS error: {obj['error']}")
    act = obj["actions"]["download"]
    return http(act["href"], headers=act.get("header") or {}, timeout=600)


def from_spt():
    print("reading SPT item templates (github.com/sp-tarkov/server-csharp) ...", file=sys.stderr)
    items = json.loads(lfs_get(SPT_DB + "templates/items.json"))
    en = json.loads(http(SPT_REPO + SPT_DB + "locales/global/en.json"))
    glob = json.loads(http(SPT_REPO + SPT_DB + "globals.json"))
    try:
        version = json.loads(http(SPT_REPO + SPT_CORE)).get("compatibleTarkovVersion")
    except (urllib.error.URLError, json.JSONDecodeError):
        version = None
    ammo = []
    for tid, it in items.items():
        p = it.get("_props") or {}
        if it.get("_parent") != AMMO_PARENT or not p.get("Caliber"):
            continue
        props = {k: p.get(raw) for k, raw in SPT_MAP.items()}
        props["accuracyModifier"] = (p.get("ammoAccr") or 0) / 100
        props["recoilModifier"] = (p.get("ammoRec") or 0) / 100
        if p.get("IsLightAndSoundShot"):
            props["ammoType"] = "flashbang"
        top = {k: props[k] for k in FALLBACK_AMMO}
        top["item"] = {"id": tid, "name": en.get(f"{tid} Name", it.get("_name")),
                       "shortName": en.get(f"{tid} ShortName"), "weight": p.get("Weight"),
                       "properties": dict(props, __typename="ItemPropertiesAmmo")}
        top["sptExtra"] = {k: p.get(k) for k in SPT_EXTRA if k in p}
        ammo.append(top)
    mats = []
    for name, m in ((glob.get("config") or {}).get("ArmorMaterials") or {}).items():
        mats.append({"id": name, "name": name, "destructibility": m.get("Destructibility"),
                     "explosionDestructibility": m.get("ExplosionDestructibility"),
                     "minRepairDegradation": m.get("MinRepairDegradation"),
                     "maxRepairDegradation": m.get("MaxRepairDegradation")})
    return {
        "source": "SPT database (github.com/sp-tarkov/server-csharp, SPT_Data/database/templates/items.json)"
                  " mapped with tarkov.dev's field mapping; tarkov.dev API was unavailable",
        "gameVersion": version, "gameMode": "regular (raw templates)", "fieldSource": "tarkov-data-manager mapping",
        "ammoFields": FALLBACK_AMMO, "itemPropertiesAmmoFields": FALLBACK_PROPS, "sptExtraFields": SPT_EXTRA,
        "ammo": ammo, "armorMaterials": mats,
    }


# --tables: the per-caliber tables in docs/cartridges.md, for the calibers the mod uses.
MOD_CALIBERS = [
    ("Caliber127x33", ".50 AE (12.7x33)", "Desert Eagle"), ("Caliber9x19PARA", "9x19 Parabellum", "Glock 17"),
    ("Caliber9x33R", ".357 Magnum (9x33R)", ""), ("Caliber1143x23ACP", ".45 ACP (11.43x23)", ""),
    ("Caliber556x45NATO", "5.56x45 NATO", ""), ("Caliber762x39", "7.62x39", ""),
    ("Caliber762x51", "7.62x51 NATO", ""), ("Caliber12g", "12 gauge (12/70)", ""),
]
TABLE_SKIP = {"Airsoft 6mm BB": "joke item tagged with this caliber"}
TABLE_SUSPECT = {
    ".45 ACP Match FMJ": "mass/diameter/BC copied from 9x19 (real 230 gr = 14.9 g, 11.5 mm)",
    ".45 ACP RIP": "mass/diameter/BC copied from 9x19",
    "12/70 7mm buckshot": "0.09 g per 7 mm pellet is impossible (lead ball ~2.0 g)",
}


def pen_chance(pen, armor_class, durability_pct=100.0):
    """Tarkov per-hit penetration chance (community reconstruction: TarkovGunsmith Ballistics.cs)."""
    a = (121 - 5000 / (45 + durability_pct * 2)) * armor_class * 0.1
    if durability_pct <= 0:
        return 1.0
    if pen >= a:
        return 1 + pen / (0.9 * a - pen) / 100
    if pen > a - 15:
        return 0.4 * (a - pen - 15) ** 2 / 100
    return 0.0


def num(x, nd=2):
    if x is None:
        return "?"
    s = f"{x:.{nd}f}"
    return (s.rstrip("0").rstrip(".") if "." in s else s) or "0"


def tables(data):
    out = []
    for cal, title, gun in MOD_CALIBERS:
        rows = [a for a in data["ammo"] if a["caliber"] == cal and a["item"]["name"] not in TABLE_SKIP]
        out.append(f"### {title}" + (f" ({gun})" if gun else "") + f" - `{cal}`, {len(rows)} types\n")
        out.append("| Ammo | Dmg | Pen | Armor dmg % | Speed m/s | Mass g | BC | Frag % | Ricochet % "
                   "| Obstacle pen % | Tracer | E J | p kg m/s | Beats class |")
        out.append("|---|--:|--:|--:|--:|--:|--:|--:|--:|--:|---|--:|--:|:-:|")
        for a in sorted(rows, key=lambda r: -r["penetrationPower"]):
            p, n = a["item"].get("properties") or {}, a["projectileCount"] or 1
            m, v = p.get("bulletMassGrams"), a.get("initialSpeed")
            e, mom = (0.5 * m / 1000 * v * v, m / 1000 * v) if m and v else (float("nan"), float("nan"))
            beat = max([c for c in range(1, 7) if pen_chance(a["penetrationPower"], c) >= 0.5], default=0)
            many = n > 1
            out.append(" | ".join([
                "| " + a["item"]["name"] + (" (!)" if a["item"]["name"] in TABLE_SUSPECT else ""),
                f'{a["damage"]} x{n}' if many else str(a["damage"]), str(a["penetrationPower"]), str(a["armorDamage"]),
                num(v, 0), num(m) + (" /pellet" if many else ""), num(p.get("ballisticCoeficient"), 3),
                num(a["fragmentationChance"] * 100, 1), num(a["ricochetChance"] * 100, 1),
                num(a["penetrationChance"] * 100, 1),
                (a.get("tracerColor") or "yes").replace("tracer", "").lower() if a["tracer"] else "-",
                f"{e:.0f}" + (f" x{n} = {e * n:.0f}" if many else ""),
                num(mom) + (f" x{n} = {num(mom * n)}" if many else ""), str(beat or "-")]) + " |")
        notes = [f"`{k}`: {v}" for k, v in TABLE_SUSPECT.items() if any(r["item"]["name"] == k for r in rows)]
        notes += [f"left out `{k}` ({v})" for k, v in TABLE_SKIP.items()
                  if any(r["item"]["name"] == k and r["caliber"] == cal for r in data["ammo"])]
        if notes:
            out.append("\n(!) " + "; ".join(notes))
        out.append("")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--tables", action="store_true",
                    help="don't fetch: print the Markdown tables for docs/cartridges.md from the saved JSON")
    ap.add_argument("--source", default="auto", choices=["auto", "api", "spt"])
    ap.add_argument("--game-mode", default="regular", choices=["regular", "pve"])
    ap.add_argument("--tries", type=int, default=6, help="API attempts (backoff 5 s doubling to 120 s)")
    here = os.path.dirname(os.path.abspath(__file__))
    ap.add_argument("--out", default=os.path.join(here, "..", "docs", "cartridges", "tarkov_ammo.json"))
    args = ap.parse_args()

    if args.tables:
        with open(os.path.abspath(args.out), encoding="utf-8") as f:
            print(tables(json.load(f)))
        return
    if args.source == "spt":
        out = from_spt()
    else:
        try:
            out = from_api(args.game_mode, args.tries)
        except ApiDown as e:
            if args.source == "api":
                raise SystemExit(str(e))
            print(f"{e}\nfalling back to the SPT database", file=sys.stderr)
            out = from_spt()
    ammo = out["ammo"]
    if not ammo:
        raise SystemExit("no ammo returned")
    ammo.sort(key=lambda a: ((a.get("caliber") or "~"), (a.get("item") or {}).get("name") or ""))
    out["armorMaterials"].sort(key=lambda m: m.get("name") or "")
    out = {"fetchedUtc": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
           **{k: v for k, v in out.items() if k not in ("ammo", "armorMaterials")},
           "count": len(ammo), "ammo": ammo, "armorMaterials": out["armorMaterials"]}
    path = os.path.abspath(args.out)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(out, f, indent=1, ensure_ascii=False)
        f.write("\n")
    cals = sorted({a.get("caliber") or "?" for a in ammo})
    print(f"wrote {len(ammo)} ammo items in {len(cals)} calibers from {out['source'][:40]}... -> {path}")


if __name__ == "__main__":
    main()
