#!/usr/bin/env python3
"""Writes perks.json for the KCD2 Perk Points window's perk picker (tools/points_ui.ps1): every perk that costs a point
(the same filter as gen_perkdefs.py: a level and visibility 2 in Libs/Tables/rpg/perk*.xml), grouped by the skill key
the window uses (skill.xml skill_name, or stat0-stat3), with its English name and description (text_ui_soul.xml),
level, the perks it excludes (perk2perk_exclusivity.xml) and its sub-perks (metaperk_id, removed with it).
  python3 -I tools/gen_perklist.py <Data/Tables.pak> <Localization/English_xml.pak> <out perks.json>
The output holds game text: it goes next to the window (E:\\Tools\\kcd2-points), not in the repo."""
import html
import json
import re
import sys
import zipfile

tables, english, out = sys.argv[1:4]


def rows(text, tag):
    return [dict(re.findall(r'(\w+)="([^"]*)"', m)) for m in re.findall(rf"<{tag} [^>]*/>", text)]


perks, skills, excl = {}, {}, {}
with zipfile.ZipFile(tables) as z:
    for name in sorted(z.namelist()):
        if re.fullmatch(r"Libs/Tables/rpg/perk[^/]*\.xml", name):
            for a in rows(z.read(name).decode("utf-8", "replace"), "perk"):
                if "perk_id" in a:
                    perks[a["perk_id"].lower()] = a
    for a in rows(z.read("Libs/Tables/rpg/skill.xml").decode("utf-8", "replace"), "skill"):
        skills[a["skill_id"]] = a["skill_name"]
    for a in rows(z.read("Libs/Tables/rpg/perk2perk_exclusivity.xml").decode("utf-8", "replace"), "perk2perk_exclusivity"):
        f, s = a["first_perk_id"].lower(), a["second_perk_id"].lower()
        excl.setdefault(f, []).append(s)
        excl.setdefault(s, []).append(f)
text = {}
with zipfile.ZipFile(english) as z:
    for row in re.findall(r"<Row>(.*?)</Row>", z.read("text_ui_soul.xml").decode("utf-8", "replace"), re.S):
        c = [html.unescape(x) for x in re.findall(r"<Cell>(.*?)</Cell>", row, re.S)]
        if len(c) >= 2:
            text[c[0]] = c[-1].strip()


def clean(s):
    return re.sub(r"\s+", " ", re.sub(r"<[^>]+>", "", s)).strip()


groups = {}
for pid, a in perks.items():
    if not (a.get("level") and a.get("visibility") == "2"):
        continue
    if a.get("skill_selector"):
        key = skills.get(a["skill_selector"])
    elif a.get("stat_selector"):
        key = "stat" + a["stat_selector"]
    else:
        continue
    if not key:
        continue
    groups.setdefault(key, []).append({
        "id": pid,
        "name": clean(text.get(a.get("perk_ui_name", ""), a.get("perk_name", pid))),
        "desc": clean(text.get(a.get("perk_ui_desc", ""), "")),
        "level": int(a["level"]),
        "excl": sorted(excl.get(pid, [])),
        "subs": sorted(k for k, b in perks.items() if b.get("metaperk_id", "").lower() == pid),
    })
for v in groups.values():
    v.sort(key=lambda p: (p["level"], p["name"]))
with open(out, "w", encoding="utf-8") as f:
    json.dump(groups, f, ensure_ascii=False, indent=1)
n = sum(len(v) for v in groups.values())
print(f"{n} perks in {len(groups)} skills/stats, {sum(1 for v in groups.values() for p in v if p['excl'])} with an exclusive partner")
