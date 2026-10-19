"""Cut voice-clone references for one KCD2 voice actor from the game's English voice paks.

extract_voice.py <abbr> <slug> <lines.json>
  abbr: the voice's abbreviation in Libs/Tables/skald/voice.xml (tmck = Henry, bcas = Brian Caspe, ...)
  lines.json: [{"id", "kind": reaction|scream|dialog, "text"}] to generate
Writes E:\\Tools\\deagle-work\\voice\\<slug>\\: <slug>_ref_calm.wav/.txt (~20 s of plain lines), <slug>_ref_excited.wav/.txt
(~15 s of lines with "!"), heldout/real_calm.ogg + real_excited.ogg (2 real lines kept out of the references; the pilot
re-speaks their text, for a blind compare), pilot_lines.json. File names are <abbr>_<subtitle id>.ogg; the subtitle
id (minus the prefix) is the row key in english_xml.pak.
"""
import glob
import html
import json
import os
import random
import re
import subprocess
import sys
import zipfile

abbr, slug, lines_path = sys.argv[1:4]
# optional: folders (substrings of the pak path) whose lines make the excited reference instead of "!" lines, e.g.
# "combat/,crime_reaction_barks/assault": real shouting; conversation lines ending in "!" clone into monotone screams
excited_dirs = [d for d in (sys.argv[4] if len(sys.argv) > 4 else "").split(",") if d]
G = "/mnt/e/SteamLibrary/steamapps/common/KingdomComeDeliverance2/Localization/"
W = f"/mnt/e/Tools/deagle-work/voice/{slug}"
os.makedirs(f"{W}/ref", exist_ok=True)
os.makedirs(f"{W}/heldout", exist_ok=True)

text = {}
for p in glob.glob(G + "[Ee]nglish_xml.pak"):
    z = zipfile.ZipFile(p)
    for n in z.namelist():
        if n.endswith(".xml"):
            for row in re.findall(r"<Row>(.*?)</Row>", z.read(n).decode("utf-8", "replace"), re.S):
                c = [html.unescape(x) for x in re.findall(r"<Cell>(.*?)</Cell>", row, re.S)]
                if len(c) >= 2:
                    text[c[0].lower()] = c[-1].strip()
found = []
pre = abbr.lower() + "_"
for p in sorted(glob.glob(G + "english-part*.pak")) + [G + "IPL_english.pak"]:
    for i in zipfile.ZipFile(p).infolist():
        b = i.filename.split("/")[-1].lower()
        if b.startswith(pre) and b.endswith(".ogg") and b[len(pre):-4] in text:
            found.append((p, i.filename, text[b[len(pre):-4]]))
clean = [l for l in found if 5 <= len(l[2].split()) <= 18 and not re.search(r"[<>\[\]{}*]", l[2])]
calm = [l for l in clean if "!" not in l[2] and "?" not in l[2]]
loud = [l for l in clean if "!" in l[2]]
if excited_dirs:
    loud = [l for l in found if any(d in l[1] for d in excited_dirs) and len(l[2].split()) >= 2
            and not re.search(r"[<>\[\]{}*]", l[2])]
print(f"{abbr}: {len(found)} lines with text, {len(calm)} calm, {len(loud)} excited")
rng = random.Random(7)
rng.shuffle(calm)
rng.shuffle(loud)
zs = {}


def pull(entry, dst):
    p, n, _ = entry
    zs.setdefault(p, zipfile.ZipFile(p))
    open(dst, "wb").write(zs[p].read(n))
    return float(subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", dst],
                                capture_output=True, text=True).stdout or 0)


held = {"real_calm": calm.pop(), "real_excited": loud.pop()}
for k, e in held.items():
    pull(e, f"{W}/heldout/{k}.ogg")
sil = f"{W}/ref/silence.wav"
subprocess.run(["ffmpeg", "-y", "-v", "error", "-f", "lavfi", "-i", "anullsrc=r=24000:cl=mono", "-t", "0.35", sil], check=True)
for name, pool, target in (("calm", calm, 20.0), ("excited", loud, 15.0)):
    chosen, tot = [], 0.0
    for i, e in enumerate(pool[:60]):
        dst = f"{W}/ref/{name}_{i:02d}.ogg"
        d = pull(e, dst)
        if 1.2 < d < 9:
            chosen.append((dst, e[2]))
            tot += d
        if tot >= target:
            break
    lst = f"{W}/ref/{name}_list.txt"
    open(lst, "w").write("".join(f"file '{f}'\nfile '{sil}'\n" for f, _ in chosen))
    subprocess.run(["ffmpeg", "-y", "-v", "error", "-f", "concat", "-safe", "0", "-i", lst, "-ac", "1", "-ar", "24000",
                    f"{W}/{slug}_ref_{name}.wav"], check=True)
    open(f"{W}/{slug}_ref_{name}.txt", "w").write(" ".join(t for _, t in chosen))
    print(f"  ref {name}: {len(chosen)} clips {tot:.1f} s")
lines = json.load(open(lines_path))
lines += [{"id": k, "kind": "heldout", "text": e[2], "source": e[1]} for k, e in held.items()]
json.dump({"voice": abbr, "slug": slug, "lines": lines}, open(f"{W}/pilot_lines.json", "w"), indent=1)
for k, e in held.items():
    print(f"  {k}: {e[2]}")
