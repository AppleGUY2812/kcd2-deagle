#!/usr/bin/env python3
"""Builds the tiny mod zzz_no_perk_exclusive (user, 2026-10-19: "i need to break the exclusive rule"): KCD2's
perk2perk_exclusivity table (8 pairs: Roadrunner / Wildrider, Rodent / Weasel Boy, Secret of Matter / Equilibrium I
and II, ...) with every row taken out, so both perks of a pair can be bought. The game ships the table as XML and as a
compiled .tbl (28-byte header: u32 3, u64 schema hash, u32 1, u32 row count, 8 zero bytes; then 32 bytes a row, two
GUIDs); which one it reads isn't known, so both come empty. Same path as the game's, so the mod's pak overrides it
(that a whole table is replaced this way, not merged like a `__name` patch, is untested).
  python3 -I tools/mk_no_perk_exclusive.py <Data/Tables.pak> <out mod folder>
Undo: delete the mod folder. Needs a game restart (tables load at start)."""
import os
import struct
import sys
import zipfile

tables, out = sys.argv[1], sys.argv[2]
X = "Libs/Tables/rpg/perk2perk_exclusivity.xml"
T = "Libs/Tables/rpg/perk2perk_exclusivity.tbl"
with zipfile.ZipFile(tables) as z:
    xml = z.read(X).decode("ascii")
    tbl = z.read(T)
head, body = xml.split("<perk2perk_exclusivitys", 1)
tag, rest = body.split(">", 1)
xml_empty = head + "<perk2perk_exclusivitys" + tag + ">\n\t</perk2perk_exclusivitys>" + rest.split("</perk2perk_exclusivitys>", 1)[1]
ver, schema, one, rows = struct.unpack_from("<IQII", tbl, 0)
assert ver == 3 and one == 1 and len(tbl) == 28 + 32 * rows, (ver, one, rows, len(tbl))
tbl_empty = tbl[:16] + struct.pack("<I", 0) + tbl[20:28]
os.makedirs(os.path.join(out, "Data"), exist_ok=True)
with open(os.path.join(out, "mod.manifest"), "w", newline="\n") as f:
    f.write('<?xml version="1.0" encoding="utf-8"?>\n<kcd_mod>\n  <info>\n    <name>No Perk Exclusivity</name>\n'
            '    <modid>zzz_no_perk_exclusive</modid>\n    <description>Both perks of an exclusive pair can be bought.</description>\n'
            '    <author>local</author>\n    <version>1.0</version>\n    <created_on>10-19-2026</created_on>\n  </info>\n</kcd_mod>\n')
with zipfile.ZipFile(os.path.join(out, "Data", "zzz_no_perk_exclusive.pak"), "w", zipfile.ZIP_STORED) as z:
    z.writestr(X, xml_empty)
    z.writestr(T, tbl_empty)
print(f"{rows} exclusive pairs removed -> {out}")
