#!/usr/bin/env python3
"""Writes merchant/src/Scripts/MerchantResale/prices.lua from the game's tables (Data/Tables.pak):
  MerchantResaleKinds[guid] = "<categories>|<type>|<subtype>"   what a shop rule matches an item by
  MerchantResaleShops[shop_id] = { {"<filter>", buy, sell}, ... }  shop.xml's ShopItemFilter rows, in order
A shop rule is "<category>.<type>.<subtype>" with * wildcards (shop.xml: "weapon.axe.*", "armor.Cuirass.*",
"misc.common.loot", "player_item.*.*" = anything; exclude="true" rows = the shop doesn't trade it: {"<filter>", false}). The game multiplies a trade by the rule's price_sell_multiplier when
it buys from the player, price_buy_multiplier when it sells (WHGame 1.5.6 FUN_180566410). Self-check: every shop's own
stock (its inventory preset) should match its own rules.
Usage: build_prices.py <game dir> <out.lua>"""
import fnmatch
import re
import sys
import zipfile
import xml.etree.ElementTree as ET


def parse(raw):
    """the game's XML: Czech comments / bytes in files that claim us-ascii"""
    text = re.sub(r"^.*?\?>", "", raw.decode("latin-1"), count=1, flags=re.S)
    return ET.fromstring(re.sub(r"<!--.*?-->", "", text, flags=re.S))


def table(pak, name, key_id, key_name):
    root = ET.fromstring(pak.read(f"Libs/Tables/item/{name}.xml"))
    return {el.get(key_id): el.get(key_name) for el in root.iter() if el.get(key_id) is not None}


def main():
    game, out = sys.argv[1], sys.argv[2]
    pak = zipfile.ZipFile(f"{game}/Data/Tables.pak")
    weapon = {}
    for el in ET.fromstring(pak.read("Libs/Tables/item/weapon_class.xml")).iter():
        if el.tag in ("MeleeWeaponClass", "MissileWeaponClass") and el.get("id") is not None:
            weapon[el.get("id")] = el.get("name")
    armor_types = sorted(table(pak, "armor_type", "Id", "Name").values(), key=len, reverse=True)
    T = {
        "ammo": table(pak, "ammo_class", "ammo_class_id", "ammo_class_name"),
        "food": table(pak, "food_type", "food_type_id", "food_type_name"),
        "food_sub": table(pak, "food_subtype", "food_subtype_id", "food_subtype_name"),
        "craft": table(pak, "crafting_material_type", "crafting_material_type_id", "crafting_material_type_name"),
        "craft_sub": table(pak, "crafting_material_subtype", "crafting_material_subtype_id",
                           "crafting_material_subtype_name"),
        "misc": table(pak, "misc_type", "misc_type_id", "misc_type_name"),
        "misc_sub": table(pak, "misc_subtype", "misc_subtype_id", "misc_subtype_name"),
        "oint": table(pak, "ointment_item_type", "ointment_item_type_id", "ointment_item_type_name"),
        "oint_sub": table(pak, "ointment_item_subtype", "ointment_item_subtype_id", "ointment_item_subtype_name"),
        "doc": table(pak, "document_class", "document_class_id", "document_class_name"),
    }

    def clothing_type(el):
        # the clothing asset name carries the type (TunicShort05_m09 -> TunicShort, NobleCaparison01 -> Caparison);
        # prefix first, then anywhere, longest type name first
        c = (el.get("Clothing") or el.get("Name") or "").lower()
        for t in armor_types:
            if c.startswith(t.lower()):
                return t
        for t in armor_types:
            if t.lower() in c:
                return t
        return ""

    def kind(el):
        """(categories, type, subtype) of an item element, or None for things no shop trades."""
        t, a = el.tag, el.get
        if t == "MeleeWeapon": return ["melee_weapon", "weapon"], weapon.get(a("Class"), ""), ""
        if t == "MissileWeapon": return ["missile_weapon", "weapon"], weapon.get(a("Class"), ""), ""
        if t == "Ammo": return ["ammo"], T["ammo"].get(a("Type"), ""), ""
        if t == "Armor": return ["armor"], clothing_type(el), ""
        if t == "Helmet": return ["helmet"], clothing_type(el), ""
        if t == "Hood": return ["hood"], clothing_type(el), ""
        if t == "Food": return ["food"], T["food"].get(a("Type"), ""), T["food_sub"].get(a("SubType"), "")
        if t == "Herb": return ["herb", "crafting_material"], "herb", T["craft_sub"].get(a("SubType"), "")
        if t == "CraftingMaterial":
            return ["crafting_material"], T["craft"].get(a("Type"), ""), T["craft_sub"].get(a("SubType"), "")
        if t == "MiscItem": return ["misc"], T["misc"].get(a("Type"), ""), T["misc_sub"].get(a("SubType"), "")
        if t == "Ointment": return ["ointment_item"], T["oint"].get(a("Type"), ""), T["oint_sub"].get(a("SubType"), "")
        if t == "Document": return ["document"], T["doc"].get(a("Type"), ""), ""
        simple = {"Poison": "poison", "Die": "die", "DiceBadge": "dice_badge", "QuickSlotContainer": "quick_slot_container",
                  "NPCTool": "npc_tool", "AlchemyBase": "alchemy_base"}
        if t in simple: return [simple[t]], "", ""
        return None

    kinds, names = {}, {}
    for n in sorted(x for x in pak.namelist() if re.match(r"Libs/Tables/item/item(__[^/]*)?\.xml$", x)):
        for el in ET.fromstring(pak.read(n)).iter():
            guid, name = el.get("Id"), el.get("Name")
            if not guid or not name:
                continue
            k = kind(el)
            if k:
                kinds[guid.lower()] = k
                names[name.lower()] = guid.lower()

    shops, presets = {}, {}
    shop_xml = pak.read("Libs/Tables/shop/shop.xml").decode("latin-1")          # Czech comments in a us-ascii file
    shop_xml = re.sub(r"<!--.*?-->", "", re.sub(r"^.*?\?>", "", shop_xml, count=1, flags=re.S), flags=re.S)
    for el in ET.fromstring(shop_xml).iter("ShopData"):
        rows = [(f.get("filter"), None, None) if f.get("exclude") == "true" else
                (f.get("filter"), float(f.get("price_buy_multiplier")), float(f.get("price_sell_multiplier")))
                for f in el.iter("ShopItemFilter")]
        shops[int(el.get("shop_id"))] = rows
        presets[int(el.get("shop_id"))] = (el.get("shop_name"), el.get("inventory_preset"))

    def rule(rows, k):
        """the last shop rule that matches (the game's later, more specific rows override 'player_item.*.*')"""
        cats, typ, sub = k
        hit = None
        for f, b, s in rows:
            parts = (f.split(".") + ["*", "*"])[:3]
            if (parts[0] == "player_item" or parts[0] in cats) and fnmatch.fnmatchcase(typ, parts[1]) \
                    and fnmatch.fnmatchcase(sub, parts[2]):
                hit = (f, b, s)
        return hit

    # self-check: each shop's stock against its own rules
    stock = {}
    for n in pak.namelist():
        if re.match(r"Libs/Tables/item/InventoryPreset[^/]*\.xml$", n):
            for p in parse(pak.read(n)).iter("InventoryPreset"):
                stock[p.get("Name")] = [i.get("Name") for i in p.iter("PresetItem")]
    total = hit = 0
    misses = {}
    for sid, (sname, preset) in presets.items():
        for item in stock.get(preset, []):
            guid = names.get((item or "").lower())
            if not guid:
                continue
            total += 1
            r = rule(shops[sid], kinds[guid])
            if r and r[1] is not None:
                hit += 1
            else:
                key = (sname, "|".join([",".join(kinds[guid][0]), kinds[guid][1], kinds[guid][2]]))
                misses[key] = misses.get(key, 0) + 1
    print(f"{len(kinds)} item kinds, {len(shops)} shops; shop stock matched by its own rules: {hit}/{total}")
    for (s, k), c in sorted(misses.items(), key=lambda x: -x[1])[:25]:
        print(f"  miss {c:3d}  {s:40s} {k}")

    lines = ["-- generated by merchant/tools/build_prices.py from the game's item and shop tables; do not edit",
             "MerchantResaleKinds = {"]
    for g in sorted(kinds):
        c, t, s = kinds[g]
        lines.append(f'["{g}"]="{",".join(c)}|{t}|{s}",')
    lines += ["}", "MerchantResaleShops = {"]
    for sid in sorted(shops):
        rows = ",".join(f'{{"{f}",false}}' if b is None else f'{{"{f}",{b:g},{s:g}}}' for f, b, s in shops[sid])
        lines.append(f"[{sid}]={{{rows}}},")
    lines.append("}")
    open(out, "w", newline="\n").write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
