#!/usr/bin/env python3
"""Build Deagle.invData from the game's own item tables and English strings.

Reads (read-only):
  <game>/Data/Tables.pak                 Libs/Tables/item/item.xml and item__*.xml
  <game>/Localization/English_xml.pak    text_ui_items.xml

Writes:
  src/Scripts/Deagle/invnative_data.lua

The Lua table is one flat map: GUID (lower case) -> { n, w, p, c, i }.
"""

import argparse
import os
import random
import re
import sys
import zipfile
import xml.etree.ElementTree as ET
from decimal import Decimal
from pathlib import Path

DEFAULT_GAME = "/mnt/e/SteamLibrary/steamapps/common/KingdomComeDeliverance2"
REPO_ROOT = Path(__file__).resolve().parents[2]

# Short category for the inventory screen. The rows all live in item.xml (plus
# item__*.xml patches), so the row element name is the real table name. These
# classes are folded into the short buckets the brief asks for.
CATEGORY = {
    "MeleeWeapon": "weapon",
    "MissileWeapon": "weapon",
    "Ammo": "weapon",
    "Armor": "armor",
    "Helmet": "armor",
    "Hood": "armor",
    "QuickSlotContainer": "armor",
    "Food": "food",
    "Herb": "herb",
    "AlchemyBase": "alchemy",
    "Ointment": "alchemy",
    "Poison": "alchemy",
    "CraftingMaterial": "crafting",
    "Document": "document",
    "Key": "key",
    "KeyRing": "key",
    "Money": "money",
    "DiceBadge": "dice",
    "Die": "dice",
    "MiscItem": "misc",
    "NPCTool": "misc",
    "PickableItem": "misc",
    "ItemAlias": "misc",
}

ITEM_FILE = re.compile(r"^Libs/Tables/item/item(__[^/]+)?\.xml$")
TEXT_FILE = "text_ui_items.xml"


def lua_string(value):
    """Quote a Python string as a Lua string literal."""
    value = str(value)
    out = []
    for ch in value:
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\r":
            out.append("\\r")
        elif ch == "\t":
            out.append("\\t")
        elif ord(ch) < 32 or ord(ch) == 127:
            out.append("\\%03d" % ord(ch))
        else:
            out.append(ch)
    return '"' + "".join(out) + '"'


def lua_number(text):
    """Format an XML numeric attribute as a Lua number literal."""
    if text is None:
        return "0"
    text = text.strip().replace(",", ".")
    if not text:
        return "0"
    try:
        value = Decimal(text)
    except Exception:
        return "0"
    if value.is_zero():
        return "0"
    # Decimal.normalize() may use an exponent; 'f' forces plain notation.
    return format(value.normalize(), "f")


def read_english(localization_pak):
    """Return {key: English text} from text_ui_items.xml.

    Rows have a key cell followed by one or two English cells; the last
    non-empty cell is the revised text (the earlier one still has typos).
    """
    with zipfile.ZipFile(localization_pak) as pak:
        with pak.open(TEXT_FILE) as handle:
            root = ET.parse(handle).getroot()
    strings = {}
    for row in root.findall("Row"):
        cells = [(cell.text or "") for cell in row.findall("Cell")]
        if not cells:
            continue
        key = cells[0]
        if not key:
            continue
        english = ""
        for cell in reversed(cells[1:]):
            if cell.strip():
                english = cell
                break
        # Collapse layout whitespace; keep the text itself.
        strings[key] = re.sub(r"\s+", " ", english).strip()
    return strings


def item_files(pak):
    names = [n for n in pak.namelist() if ITEM_FILE.match(n)]
    base = [n for n in names if "__" not in n]
    patches = sorted(n for n in names if "__" in n)
    return base + patches


def read_items(tables_pak):
    """Yield (source file, class tag, attributes) for every item row."""
    with zipfile.ZipFile(tables_pak) as pak:
        for name in item_files(pak):
            with pak.open(name) as handle:
                for _event, elem in ET.iterparse(handle, events=("end",)):
                    if elem.tag == "ItemClasses":
                        stem = Path(name).stem
                        for child in elem:
                            yield stem, child.tag, child.attrib
                        elem.clear()


def build_rows(tables_pak, localization_pak):
    english = read_english(localization_pak)
    rows = {}
    sources = {}
    duplicates = 0
    for source, tag, attrs in read_items(tables_pak):
        guid = (attrs.get("Id") or "").lower()
        if not guid:
            continue
        sources[source] = sources.get(source, 0) + 1
        if guid in rows:
            duplicates += 1
            continue
        ui_key = attrs.get("UIName") or ""
        english_name = english.get(ui_key, "") if ui_key else ""
        name = english_name or attrs.get("Name") or ""
        icon = attrs.get("IconId") or attrs.get("Name") or ""
        rows[guid] = {
            "n": name,
            "w": lua_number(attrs.get("Weight")),
            "p": attrs.get("Price") or "0",
            "c": CATEGORY.get(tag, tag.lower()),
            "i": icon,
            "en": bool(english_name),
        }
    return rows, english, sources, duplicates


def write_lua(rows, out_path):
    lines = [
        "-- generated by tools/invnative/build_data.py from the game's "
        "Tables.pak and English_xml.pak; don't edit by hand",
        "Deagle = Deagle or {}",
        "Deagle.invData = {",
    ]
    for guid in sorted(rows):
        row = rows[guid]
        price = row["p"]
        try:
            price = str(int(price))
        except Exception:
            price = "0"
        lines.append(
            '[%s] = { n = %s, w = %s, p = %s, c = %s, i = %s },'
            % (
                lua_string(guid),
                lua_string(row["n"]),
                row["w"],
                price,
                lua_string(row["c"]),
                lua_string(row["i"]),
            )
        )
    lines.append("}")
    out_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", default=os.environ.get("KCD2_DIR", DEFAULT_GAME))
    parser.add_argument("--out", default=str(REPO_ROOT / "src/Scripts/Deagle/invnative_data.lua"))
    parser.add_argument("--check", action="store_true", help="print the checks only (no write)")
    args = parser.parse_args()

    tables_pak = Path(args.game) / "Data/Tables.pak"
    localization_pak = Path(args.game) / "Localization/English_xml.pak"
    for path in (tables_pak, localization_pak):
        if not path.is_file():
            sys.exit("missing input: %s" % path)

    rows, english, sources, duplicates = build_rows(tables_pak, localization_pak)
    out_path = Path(args.out)
    if not args.check:
        out_path.parent.mkdir(parents=True, exist_ok=True)
        write_lua(rows, out_path)

    named = [g for g, r in rows.items() if r["en"]]
    unnamed = [g for g, r in rows.items() if not r["en"]]
    belt = "7da54a04-67c4-4767-8b60-ee9211cc465e"

    print("item files: %d; source rows by file:" % len(sources))
    for name in sorted(sources):
        print("  %-28s %5d" % (name, sources[name]))
    print("text strings: %d (%s)" % (len(english), TEXT_FILE))
    print("rows written: %d (duplicate GUIDs skipped: %d)" % (len(rows), duplicates))
    print("rows with English name: %d" % len(named))
    print("rows without English name: %d" % len(unnamed))
    for guid in sorted(unnamed)[:10]:
        r = rows[guid]
        print("  %s  name=%r  UIName key absent/empty" % (guid, r["n"]))
    if belt in rows:
        r = rows[belt]
        print(
            'belt row: ["%s"] = { n = "%s", w = %s, p = %s, c = "%s", i = "%s" }'
            % (belt, r["n"], r["w"], r["p"], r["c"], r["i"])
        )
    else:
        print("belt row MISSING: %s" % belt)

    print("15 random rows:")
    for guid in random.sample(sorted(rows), min(15, len(rows))):
        r = rows[guid]
        print(
            '  ["%s"] = { n = %s, w = %s, p = %s, c = "%s", i = "%s" }'
            % (guid, lua_string(r["n"]), r["w"], r["p"], r["c"], r["i"])
        )

    if not args.check and out_path.is_file():
        print("output: %s  %.1f KB" % (out_path, out_path.stat().st_size / 1024.0))


if __name__ == "__main__":
    main()
