"""Time of the main transient(s) in Ready or Not's handling events, so they can be started at our clips' own impact
moments. Each event is rendered alone PLAYS times by Ready or Not's engine (tools/ron/FmodRender: listener at the
source, Occlusion 0, PropagateDistance 0); FmodRender logs the mix block each play starts on, so times are from the
true event start. A transient = a sharp rise (>= 12 dB within 8 ms) whose peak is within 25 dB of the event's loudest;
its time is the attack (where it first reaches 25 % of its own peak). Descriptions are from the measured sound
(sharpness, brightness, length) and the take names: not listened to.
usage: ron_part_transients.py <events.txt "length_ms|event path" lines> <out.json>"""
import json, os, re, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
import numpy as np, soundfile as sf

EXE = "/mnt/e/Tools/deagle-work/ron/fmodrender/bin/FmodRender.exe"
BANKS = r"C:\Program Files (x86)\Steam\steamapps\common\Ready Or Not\ReadyOrNot\Content\FMOD\Desktop"
RAW, RAWWIN = "/mnt/e/Tools/deagle-work/ron/fmodrender/parts", r"E:\Tools\deagle-work\ron\fmodrender\parts"
PLAYS = 6
BLOCK = 256           # mix block (5.3 ms): start times are quantised to it (1024 gave +-21 ms run to run)
LATENCY = 10 * BLOCK  # FMOD offline output lags a start by exactly 10 mix blocks: measured with a core oscillator
                      # started at a logged block (same at 1024 and 256); removed from every start
os.makedirs(RAW, exist_ok=True)

def render(item):
    length_ms, path = item
    name = re.sub(r"[^A-Za-z0-9]+", "_", path.split("/", 2)[-1])
    gap = length_ms / 1000 + .6
    args = [EXE, "render", BANKS, rf"{RAWWIN}\{name}.wav", path, f"{gap:.3f}", "Occlusion=0", "PropagateDistance=0"]
    for _ in range(PLAYS - 1): args += ["+", f"{gap:.3f}", "Occlusion=0", "PropagateDistance=0"]
    log = subprocess.run(args, capture_output=True, text=True, env=dict(os.environ, WSLENV="DUMP_ALL:FMOD_BLOCK", DUMP_ALL="1", FMOD_BLOCK=str(BLOCK)), timeout=900).stdout
    starts = [int(m) + LATENCY for m in re.findall(r"shot starts at block \d+ = sample (\d+)", log)]
    # sounds per play: the dump after each start lists the channels playing then
    chunks = re.split(r"shot starts at block", log)[1:]
    takes = [sorted(set(re.findall(r"sound '([^']+)'", c))) for c in chunks]
    return path, length_ms, name, starts, takes

def transients(seg, sr):
    a = np.abs(seg).max(1); n = int(.001 * sr); m = len(a) // n
    if m < 10: return []
    e = 20 * np.log10(a[:m * n].reshape(m, n).max(1) + 1e-9); top = e.max(); out = []
    i = 8
    while i < m:
        if e[i] - e[i - 8:i].min() >= 12 and e[i] > top - 25:
            j = i + int(np.argmax(e[i:i + 15]))                          # its peak, within 15 ms
            pk = a[j * n:(j + 1) * n].max(); lo = max((i - 8) * n, 0)
            att = lo + int(np.argmax(a[lo:(j + 1) * n] >= .25 * pk))       # attack: first reach of 25 % of the peak
            out.append((att / sr * 1000, e[j])); i = j + 40                # next transient at least 40 ms later
        else: i += 1
    return out

def describe(seg, sr, tr, takes):
    x = seg.mean(1); S = np.abs(np.fft.rfft(x)) ** 2; f = np.fft.rfftfreq(len(x), 1 / sr)
    cen = (S * f).sum() / max(S.sum(), 1e-30)
    e = np.abs(x); act = (e > e.max() * .03).nonzero()[0]; dur = (act[-1] - act[0]) / sr * 1000 if len(act) else 0
    sharp = "sharp" if tr and len(tr) <= 3 else ("busy" if tr else "soft")
    tone = "bright" if cen > 3500 else ("mid" if cen > 1500 else "dull")
    words = " ".join(takes[0]).lower() if takes and takes[0] else ""
    kind = next((k for k, w in (("cloth", "cloth"), ("pouch", "pouch"), ("click", "dry"), ("slide", "slide"), ("rack", "slide"),
                                 ("mag", "mag"), ("holster", "holster"), ("draw", "draw"), ("ads", "ads")) if w in words), "")
    return f"{sharp} {tone} {kind or 'hit'}".strip(), round(cen), round(dur)

if __name__ == "__main__":
    items = [(int(l.split("|")[0]), l.split("|")[1].strip()) for l in open(sys.argv[1]) if "|" in l]
    rows = []
    with ThreadPoolExecutor(4) as ex:
        for path, length_ms, name, starts, takes in ex.map(render, items):
            x, sr = sf.read(f"{RAW}/{name}.wav"); plays = []
            for k, s in enumerate(starts):
                seg = x[s: s + int((length_ms / 1000 + .3) * sr)]
                tr = transients(seg, sr); top = max((l for _, l in tr), default=0)
                am = np.abs(seg).max(1); first = int(np.argmax(am > am.max() * 10 ** (-40 / 20))) / sr * 1000 if am.max() > 0 else 0
                plays.append({"takes": takes[k] if k < len(takes) else [], "transients_ms": [round(t, 1) for t, _ in tr],
                              "level_db": [round(l - top, 1) for _, l in tr], "first_sound_ms": round(first, 1)})
            seg0 = x[starts[0]: starts[0] + int((length_ms / 1000 + .3) * sr)] if starts else x[:1]
            words, cen, dur = describe(seg0, sr, plays[0]["transients_ms"] if plays else [], [p["takes"] for p in plays])
            distinct = sorted({t for p in plays for t in p["takes"]})
            rows.append({"event": path, "length_ms": length_ms, "plays": plays, "distinct_takes": distinct,
                         "describe": words, "centroid_hz": cen, "sound_ms": dur})
            print(path, len(starts), "plays", flush=True)
    json.dump(rows, open(sys.argv[2], "w"), indent=1)
