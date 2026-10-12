#!/usr/bin/env python3
"""Generate src/Scripts/Deagle/guns/armor.lua from KCD2's Tables.pak: per wearable item its layer, defense and the body
zones it covers. At runtime an NPC's inventory is its outfit: npc.inventory:GetInventoryTable() -> ItemManager.GetItem(id)
-> .class (item guid, look up here) and .health (condition 0..1). Notes: docs/kcd2_armor_hits.md ("Runtime armor lookup").

usage: gen_armor.py <copy of Data/Tables.pak> [out.lua]
       (copy the pak out of the game folder first; this never touches the game)"""
import collections, os, sys, zipfile
import xml.etree.ElementTree as ET

T = "Libs/Tables/"

# KCD2 body_subpart -> (our zone, side); side None = not front/back specific (counts for both)
def zone_of(sub):
    if sub.startswith("head_"):
        return None if sub == "head_necklace" else ("head", None)
    if sub.startswith("body_"):
        if sub in ("body_belt", "body_pouch", "body_scabbard"): return None
        return ("torso", "front" if "_front" in sub else "back" if "_back" in sub else None)
    for side in ("left", "right"):
        if sub.startswith("arm_" + side) or sub == "hand_" + side:
            return ("arm_" + side, "front" if sub.endswith("_front") else "back" if sub.endswith("_back") else None)
        if sub.startswith("leg_" + side) or sub.startswith("foot_" + side):
            return ("leg_" + side, "front" if sub.endswith("_front") else "back" if sub.endswith("_back") else None)
    return None   # rings, spurs, horse / animal parts

PADDING = {"GambesonShort", "GambesonLong", "Caftan", "Pourpoint", "LegsPadded", "CollarPadded", "HorsePadded"}

def layer_of(armor_type, surface, layer_types):
    """our layer name from the archetype's body layers (plate / chainmail / cloth / decoration) and the armor type"""
    if "plate" in layer_types and surface in ("mat_plate", "mat_leather") and armor_type not in ("Gloves", "BootsAnkle", "BootsKnee", "Shoes"):
        return "plate"            # cuirass, brigandine, plate arms / legs, gauntlets, helmets
    if "chainmail" in layer_types and surface == "mat_chainmail":
        return "mail"
    if surface == "mat_chainmail":
        return "mail"             # coifs / mail collars
    if armor_type in PADDING:
        return "padding"
    if surface == "mat_leather":
        return "leather"
    return "cloth"

def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    pak = zipfile.ZipFile(sys.argv[1])
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(repo, "src", "Scripts", "Deagle", "guns", "armor.lua")
    xml = lambda name: ET.fromstring(pak.read(T + name))

    # Clothing name -> (ArmorType, ArmorArchetypeId), inherited down the CharacterComponent tree
    clothing = {}
    def walk(e, at, ar):
        if e.tag == "Clothing":
            at = e.get("ArmorType", at); ar = e.get("ArmorArchetypeId", ar)
            clothing[e.get("Name")] = (at, ar)
        for c in e: walk(c, at, ar)
    walk(xml("Character/CharacterComponent.xml"), None, None)

    surface = {a.get("Name"): a.get("Surface") for a in xml("item/armor_type.xml").iter("armor_type")}
    subpart = {s.get("body_subpart_id"): s.get("body_subpart_name") for s in xml("item/body_subpart.xml").iter("body_subpart")}
    layer_type_name = {l.get("body_layer_type_id"): l.get("body_layer_type_name") for l in xml("item/body_layer_type.xml").iter("body_layer_type")}
    layers = {l.get("body_layer_id"): (l.get("body_layer_name"), layer_type_name[l.get("body_layer_type_id")], int(l.get("layer_order")))
              for l in xml("item/body_layer.xml").iter("body_layer")}
    arch = collections.defaultdict(list)
    for r in xml("item/armor_archetype2body_subpart.xml").iter("armor_archetype2body_subpart"):
        arch[r.get("armor_archetype_id")].append((layers[r.get("body_layer_id")], subpart.get(r.get("body_subpart_id"), "")))

    items = {}
    skipped = collections.Counter()
    for name in sorted(n for n in pak.namelist() if n.startswith(T + "item/item") and n.endswith(".xml")):
        for e in ET.fromstring(pak.read(name)).iter():
            if e.tag not in ("Armor", "Helmet", "Hood"):
                continue
            stab, slash, smash = (float(e.get(k, 0)) for k in ("DefenseStab", "DefenseSlash", "DefenseSmash"))
            if stab + slash + smash <= 0:
                skipped["no defense"] += 1; continue
            at, ar = clothing.get(e.get("Clothing"), (None, None))
            if ar is None or ar not in arch:
                skipped["no archetype"] += 1; continue
            zones, ltypes, order = [], set(), 0
            for (lname, ltype, lorder), sub in arch[ar]:
                z = zone_of(sub)
                if not z: continue
                if z[0] not in zones: zones.append(z[0])
                ltypes.add(ltype); order = max(order, lorder)
            if not zones:
                skipped["no human zone"] += 1; continue
            items[e.get("Id").lower()] = dict(
                name=e.get("Name"), type=at, layer=layer_of(at, surface.get(at), ltypes), order=order,
                stab=stab, slash=slash, smash=smash, maxStatus=int(float(e.get("MaxStatus", 0))),
                surface=(surface.get(at) or "").replace("mat_", ""), zones=zones)

    num = lambda v: str(int(v)) if float(v).is_integer() else repr(round(v, 3))
    lst = lambda xs: "{ " + ", ".join('"%s"' % x for x in xs) + " }"
    lines = ["-- GENERATED by tools/gen_armor.py from Tables.pak; do not edit. See docs/kcd2_armor_hits.md (Runtime armor lookup).",
             "-- armor[itemGuid] = { name, type (ArmorType), layer = plate|mail|padding|leather|cloth, order (KCD2 body_layer",
             "--   layer_order: higher = further out, hit first), stab, slash, smash, maxStatus, surface, zones }",
             "-- lookup: npc.inventory:GetInventoryTable() -> ItemManager.GetItem(id).class:lower() -> Deagle.armor[...]",
             "Deagle = Deagle or {}",
             "Deagle.armor = {"]
    for g in sorted(items):
        a = items[g]
        lines.append('\t["%s"] = { name = "%s", type = "%s", layer = "%s", order = %d, stab = %s, slash = %s, smash = %s, maxStatus = %d, '
                     'surface = "%s", zones = %s },' % (
                         g, a["name"], a["type"], a["layer"], a["order"], num(a["stab"]), num(a["slash"]), num(a["smash"]),
                         a["maxStatus"], a["surface"], lst(a["zones"])))
    lines.append("}")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    by_layer = collections.Counter(a["layer"] for a in items.values())
    print(f"armor.lua: {len(items)} items ({dict(by_layer)}); skipped {dict(skipped)} -> {out}")

if __name__ == "__main__":
    main()
