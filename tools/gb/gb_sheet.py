#!/usr/bin/env python3
"""Contact sheet of a gun's clip previews: one row per role, 4 frames each, labelled.
usage: gb_sheet.py <preview dir> <profile.json> <out.png>"""
import sys, os, json
from PIL import Image, ImageDraw
PDIR, PROF, OUT = sys.argv[1:4]
prof = json.load(open(PROF))
rows = []
for name in sorted(os.listdir(PDIR)):
    role = name.rsplit("_", 1)[0]
    if role not in [r[0] for r in rows]:
        rows.append((role, []))
    dict(rows)[role].append(name)
w, h = 480, 270
S = Image.new("RGB", (w * 4 + 220, h * len(rows) + 40), (24, 24, 28))
d = ImageDraw.Draw(S)
d.text((10, 10), f"{prof.get('title', '')}  |  mag {prof.get('magSize')}  |  missing: {', '.join(prof.get('missing_required', [])) or 'none'}", fill=(240, 240, 240))
for y, (role, files) in enumerate(rows):
    c = prof["clips"].get(role, {})
    label = f"{role}\n{c.get('sequence', '')}\n{c.get('frames')} fr @ {c.get('fps')} fps\n{len(c.get('sounds', []))} sounds"
    d.multiline_text((10, 40 + y * h + 10), label, fill=(230, 230, 230), spacing=6)
    for x, f in enumerate(sorted(files)[:4]):
        S.paste(Image.open(os.path.join(PDIR, f)).convert("RGB").resize((w, h)), (220 + x * w, 40 + y * h))
S.save(OUT)
print("SHEET", OUT, len(rows), "rows")
