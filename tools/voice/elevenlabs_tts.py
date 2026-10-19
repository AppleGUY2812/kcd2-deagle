"""Clone a KCD2 voice on ElevenLabs (Instant Voice Clone) and speak its pilot lines with Eleven v4.

elevenlabs_tts.py <voice> [takes]
  voice: a folder made by extract_voice.py (E:\\Tools\\deagle-work\\voice\\<voice>\\: <voice>_clone.mp3, pilot_lines.json)
  takes: per line, default 2 (v4 varies between runs; keep the best)
Key: ~/.config/elevenlabs/key (one line, chmod 600). Needs a paid plan: the free one can't clone.
The clone is made once from <voice>_clone.mp3 (~90 s, mostly fight barks) and its id kept in elevenlabs_voice_id.txt;
each voice uses one of the plan's voice slots until deleted on the website.
Lines keep their [audio tags]. Stability: 0.0 ("Creative", widest emotion) for screams/reactions, 0.5 for dialog.
Output: <voice>/out/eleven-v4/<line id>_<take>.mp3. Prints credits before and after.
"""
import json
import os
import sys

import requests

API = "https://api.elevenlabs.io/v1"
MODEL = os.environ.get("EL_MODEL", "eleven_v4")
voice = sys.argv[1]
takes = int(sys.argv[2]) if len(sys.argv) > 2 else 2
W = f"/mnt/e/Tools/deagle-work/voice/{voice}"
H = {"xi-api-key": open(os.path.expanduser("~/.config/elevenlabs/key")).read().strip()}


def credits():
    r = requests.get(f"{API}/user/subscription", headers=H, timeout=30)
    r.raise_for_status()
    s = r.json()
    return f"{s['tier']}: {s['character_count']}/{s['character_limit']} used, voice slots {s.get('voice_slots_used', '?')}/{s.get('voice_limit', '?')}"


print("before:", credits())
idf = f"{W}/elevenlabs_voice_id.txt"
if os.path.exists(idf):
    vid = open(idf).read().strip()
else:
    with open(f"{W}/{voice}_clone.mp3", "rb") as f:
        r = requests.post(f"{API}/voices/add", headers=H, timeout=120,
                          data={"name": f"kcd2-{voice}", "description": "KCD2 voice-line pilot (private)"},
                          files=[("files", (f"{voice}_clone.mp3", f, "audio/mpeg"))])
    if r.status_code != 200:
        sys.exit(f"clone failed: HTTP {r.status_code} {r.text[:400]}")
    vid = r.json()["voice_id"]
    open(idf, "w").write(vid)
    print("cloned", voice, "->", vid)

out = f"{W}/out/{MODEL.replace('_', '-')}"
os.makedirs(out, exist_ok=True)
used = 0
for line in json.load(open(f"{W}/pilot_lines.json"))["lines"]:
    kind = line["kind"]
    if kind == "heldout":
        kind = "reaction" if "excited" in line["id"] else "dialog"
    body = {"text": line["text"], "model_id": MODEL,
            "voice_settings": {"stability": 0.5 if kind == "dialog" else 0.0, "similarity_boost": 0.8}}
    for n in range(takes):
        r = requests.post(f"{API}/text-to-speech/{vid}", headers=H, json={**body, "seed": 1000 + n},
                          params={"output_format": "mp3_44100_128"}, timeout=180)
        if r.status_code != 200:
            print(f"  {line['id']}_{n}: HTTP {r.status_code} {r.text[:300]}")
            if r.status_code in (401, 402, 403):  # bad key / out of credits / plan can't: the rest fails too
                sys.exit(1)
            continue
        open(f"{out}/{line['id']}_{n}.mp3", "wb").write(r.content)
        used += len(line["text"])
        print(f"  {line['id']}_{n}: {len(r.content) // 1024} KB")
print(f"~{used} characters sent")
print("after:", credits())
