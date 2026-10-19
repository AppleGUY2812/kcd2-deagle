"""Build the blind listening folder for a voice-clone pilot.

listen_folder.py <work dir> <desktop folder>
Per line: one folder with every take from every model under shuffled letters (A.wav, B.wav, ...). The two held-out
lines also get the real game recording shuffled in: can you spot the real one? The key is in ANSWERS.txt.
"""
import json
import random
import shutil
import subprocess
import sys
from pathlib import Path

work, desk = Path(sys.argv[1]), Path(sys.argv[2])
lines = json.loads((work / "pilot_lines.json").read_text())["lines"]
out = work / "out"
models = sorted(p.name for p in out.iterdir() if p.is_dir())
if desk.exists():
    shutil.rmtree(desk)
desk.mkdir(parents=True)
rng = random.Random(1105)
key = []
for i, line in enumerate(lines, 1):
    clips = [(f"{m} take {p.stem.rsplit('_', 1)[1]}", p) for m in models
             for p in sorted((out / m).glob(f"{line['id']}_*.wav"))]
    if line["kind"] == "heldout":
        real = work / "heldout" / f"{line['id']}.ogg"
        clips.append(("REAL game recording", real))
    if not clips:
        key.append(f"{i:02d} {line['id']}: no audio (failed on every model, see out/*/{line['id']}.log)")
        continue
    rng.shuffle(clips)
    words = "".join(c for c in line["text"][:40] if c.isalnum() or c in " ,'-").strip()
    d = desk / f"{i:02d} {words}"
    d.mkdir()
    (d / "line.txt").write_text(line["text"] + "\n")
    key.append(f"{i:02d} {line['text']}")
    for letter, (what, src) in zip("ABCDEFGH", clips):
        # everything as 44.1 kHz WAV at one loudness, so nothing gives the answer away by format or volume
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", str(src), "-ac", "1", "-ar", "44100",
                        "-af", "loudnorm=I=-18:TP=-2", str(d / f"{letter}.wav")], check=True)
        key.append(f"   {letter} = {what}")
(desk / "ANSWERS.txt").write_text("\n".join(key) + "\n")
(desk / "README.txt").write_text(
    "Henry (Tom McKay) cloned by AI. Each folder is one line; the letters are different models and takes, shuffled.\n"
    "Lines 'real_...': one of the letters is the real game recording. Guess first, then open ANSWERS.txt.\n"
    f"Models: {', '.join(models)}\n")
print(f"{len(lines)} lines -> {desk}")
