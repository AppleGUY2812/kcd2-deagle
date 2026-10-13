"""Render EVERY take combination of Ready or Not's MK V first-person shot for each place class, with Ready or Not's own
FMOD engine (tools/ron/FmodRender). One shot = 1 of Grizzly-1..4 + INT/EXT Small, Base, Large, each _01 or _02:
4 x 2 x 2 x 2 = 32 combos per class, all reachable (400-shot count: Grizzly-1..4 played 107 / 86 / 99 / 108 times).
Each class is one long render of SHOTS shots (the engine's random order covers all 32 by shot 163); the engine's log
names the takes of every shot; the first copy of each combo is cut out (onset found per shot: shots sit 188 mix
blocks = 4.0107 s apart, not 4 s) and written as assets/build/sounds/mk5/<class>/shot-1..32.wav + takes.txt.
Also re-cuts assets/build/sounds/decho/decho-1..8.wav from out_forest. Run: ~/.venvs/deagle-audio/bin/python <this>"""
import os, re, subprocess, sys, wave
from concurrent.futures import ThreadPoolExecutor
import numpy as np, soundfile as sf
from math import gcd
from scipy.signal import resample_poly
HERE = os.path.dirname(os.path.abspath(__file__)); REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from measure4 import SR

EXE = "/mnt/e/Tools/deagle-work/ron/fmodrender/bin/FmodRender.exe"
BANKS = r"C:\Program Files (x86)\Steam\steamapps\common\Ready Or Not\ReadyOrNot\Content\FMOD\Desktop"
RAW, RAWWIN = "/mnt/e/Tools/deagle-work/ron/fmodrender/combos_all", r"E:\Tools\deagle-work\ron\fmodrender\combos_all"
OUT = os.path.join(REPO, "assets/build/sounds/mk5")
SHOTS, SPACING, KEEP = 170, 4.0, 3.95
CLASSES = {   # class: (IsOutside, snapshots) - same as render_mk5_sets.sh
    "in_small": (0, ["Reverb_IN_Small01"]), "in_small_wood": (0, ["Reverb_IN_Small01", "Reverb Materials/Reverb_WOOD"]),
    "in_mid": (0, ["Reverb_IN_Mid01"]), "in_mid_wood": (0, ["Reverb_IN_Mid01", "Reverb Materials/Reverb_WOOD"]),
    "in_hall": (0, ["Reverb_IN_Hall01"]), "in_hall_wood": (0, ["Reverb_IN_Hall01", "Reverb Materials/Reverb_WOOD"]),
    "in_corridor": (0, ["Reverb_IN_Corridor01"]), "in_corridor_wood": (0, ["Reverb_IN_Corridor01", "Reverb Materials/Reverb_WOOD"]),
    "in_cave": (0, ["Reverb_OUT_Cave01"]), "in_cave_stone": (0, ["Reverb_OUT_Cave01", "Reverb Materials/Reverb_CONCRETE"]),
    "out_forest": (1, ["Reverb_OUT_Forest01"]), "out_street": (1, ["Reverb_OUT_Street01"]),
    "out_covered": (1, ["Reverb_IN_Mid01", "Reverb Materials/Reverb_CONCRETE"]),
}
os.makedirs(RAW, exist_ok=True)

def render(name):
    io, snaps = CLASSES[name]
    args = [EXE, "render", BANKS, rf"{RAWWIN}\{name}.wav", "event:/Weapons/Grizzly/1P Grizzly Fire", str(SPACING), f"IsOutside={io}"]
    args += [f"snapshot:/{s}" for s in snaps]
    for _ in range(SHOTS - 1): args += ["+", str(SPACING), f"IsOutside={io}"]
    env = dict(os.environ, WSLENV="DUMP_ALL:FMOD_MASTER_GAIN", DUMP_ALL="1", FMOD_MASTER_GAIN="0.5")
    log = subprocess.run(args, capture_output=True, text=True, env=env, timeout=3600).stdout
    sounds = re.findall(r"sound '([^']+)'", log)
    shots = [sounds[i:i + 4] for i in range(0, len(sounds), 4)]
    combos = []
    for s in shots:
        bang = next(x for x in s if x.startswith("Grizzly-"))
        e = {re.search(r"_(Small|Base|Large)_", x).group(1): x[-2:] for x in s if "Reflection" in x}
        combos.append((bang, e["Small"], e["Base"], e["Large"]))
    return name, combos

def cut(name, combos):
    # work on the raw 48 kHz render (resampling all 11 minutes at once ran out of memory); resample each cut alone
    x, sr = sf.read(f"{RAW}/{name}.wav", dtype="float32"); env = np.abs(x).max(1); thr = .1 * env.max()
    starts, pos = [], int(np.argmax(env > thr))
    for k in range(len(combos)):                     # each shot's real onset, searched near where it should be
        lo = max(pos - int(.05 * sr), 0); hi = pos + int(.05 * sr)
        a = lo + int(np.argmax(env[lo:hi] > thr)); starts.append(a); pos = a + int(4.0107 * sr)
    first = {}
    for k, c in enumerate(combos): first.setdefault(c, k)
    keys = sorted(first)
    d = f"{OUT}/{name}"; os.makedirs(d, exist_ok=True)
    for f in os.listdir(d):
        if f.startswith("shot-") or f == "takes.txt": os.remove(f"{d}/{f}")
    lines = []
    for n, c in enumerate(keys, 1):
        write(f"{d}/shot-{n}.wav", take(x, sr, starts[first[c]])); lines.append(f"shot-{n} {c[0]} Small_{c[1]} Base_{c[2]} Large_{c[3]}")
    open(f"{d}/takes.txt", "w").write("\n".join(lines) + "\n")
    return len(keys), max(first.values()) + 1, x, sr, starts

def take(x, sr, a):
    """one shot from the raw render: from 32 samples before its onset, KEEP s, at 44.1 kHz, 50 ms fade at the end"""
    seg = x[max(a - int(32 * sr / SR), 0): a + int(KEEP * sr)].astype(float)
    y = resample_poly(seg, SR // gcd(SR, sr), sr // gcd(SR, sr), axis=0)[:int(KEEP * SR)]
    fl = int(.05 * SR); y[-fl:] *= np.linspace(1, 0, fl)[:, None]; return y

def write(path, y):
    w = wave.open(path, "wb"); w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR)
    w.writeframes((np.clip(y, -1, 1) * 32767).astype(np.int16).tobytes()); w.close()

if __name__ == "__main__":
    names = sys.argv[1:] or list(CLASSES)
    with ThreadPoolExecutor(4) as ex:
        for name, combos in ex.map(render, names):
            n, last, x, sr, starts = cut(name, combos)
            print(f"{name:18s} {len(combos)} shots rendered, {n} combos (all by shot {last})", flush=True)
            if name == "out_forest":                    # the decho fallback files: first 8 shots, cut at their real onsets
                for k in range(8):
                    write(os.path.join(REPO, f"assets/build/sounds/decho/decho-{k + 1}.wav"), take(x, sr, starts[k]))
