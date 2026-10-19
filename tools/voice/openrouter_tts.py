"""Clone a KCD2 voice through OpenRouter's speech API (ElevenLabs v4, Fish S2.1 Pro, Drama 3, ...): no GPU, pay per character.

openrouter_tts.py <voice> [model ...]
  voice: a folder made by extract_voice.py (E:\\Tools\\deagle-work\\voice\\<voice>\\: pilot_lines.json, refs)
  models: OpenRouter ids; default: the three below
Key: ~/.config/openrouter/key (one line, chmod 600), never on the command line or in chat.
Each line is sent with its reference clip + transcript as `input_references` (OpenRouter routes that only to endpoints
that can clone; a model that can't answers with an error, logged and skipped) and its emotion as `instructions`
(Fish / Gemini read it, others ignore it). Text keeps its [tags]: ElevenLabs v3/v4 read inline audio tags.
Output: <voice>/out/<model slug>/<line id>_0.mp3, plus a cost line per call from the character count.
"""
import base64
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request

PRICE = {"elevenlabs/eleven-v4": 40, "fish-audio/s2.1-pro": 15, "fish-audio/drama-3": 2}  # $ per million characters
# 2026-10-19: only the Fish models clone here; ElevenLabs v3/v4/v4-turbo, MiniMax 2.8 HD and Voxtral answer 404
# "No endpoints supporting voice cloning found for this model"
DEFAULT = ["fish-audio/s2.1-pro", "fish-audio/drama-3"]
voice = sys.argv[1]
models = sys.argv[2:] or DEFAULT
W = f"/mnt/e/Tools/deagle-work/voice/{voice}"
key = open(os.path.expanduser("~/.config/openrouter/key")).read().strip()
lines = json.load(open(f"{W}/pilot_lines.json"))["lines"]
MOOD = {"scream": "Screaming in panic, ragged and breathless, real fear.",
        "reaction": "Shocked and loud, reacting to a sudden gunshot.",
        "dialog": "Natural conversational delivery.", "heldout": None}


def ref(kind):
    k = "calm" if kind == "dialog" else "excited"
    wav = open(f"{W}/{voice}_ref_{k}.wav", "rb").read()
    return [{"type": "input_audio", "input_audio": {"data": "data:audio/wav;base64," + base64.b64encode(wav).decode()}},
            {"type": "text", "text": open(f"{W}/{voice}_ref_{k}.txt").read().strip()}]


total = 0.0
for model in models:
    out = f"{W}/out/{model.split('/')[-1]}"
    os.makedirs(out, exist_ok=True)
    for line in lines:
        kind = line["kind"]
        if kind == "heldout":
            kind = "reaction" if "excited" in line["id"] else "dialog"
        text = line["text"] if model.startswith("elevenlabs/") else re.sub(r"\[[^\]]*\]\s*", "", line["text"])
        body = {"model": model, "input": text, "response_format": "mp3", "input_references": ref(kind)}
        if MOOD.get(kind):
            body["instructions"] = MOOD[kind]
        req = urllib.request.Request("https://openrouter.ai/api/v1/audio/speech", json.dumps(body).encode(),
                                     {"Authorization": f"Bearer {key}", "Content-Type": "application/json",
                                      "X-OpenRouter-Title": "kcd2-voice-pilot"})
        t0 = time.time()
        try:
            audio = urllib.request.urlopen(req, timeout=180).read()
        except urllib.error.HTTPError as e:
            print(f"  {model} {line['id']}: HTTP {e.code} {e.read()[:300]!r}")
            if e.code in (400, 404):  # no cloning endpoint for this model: the rest would fail the same way
                break
            continue
        open(f"{out}/{line['id']}_0.mp3", "wb").write(audio)
        cost = len(text) * PRICE.get(model, 40) / 1e6
        total += cost
        print(f"  {model} {line['id']}: {len(audio) // 1024} KB {time.time() - t0:.1f} s ~${cost:.4f}")
print(f"~${total:.3f} total (from character counts; OpenRouter's activity page has the exact charge)")
