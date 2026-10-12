#!/usr/bin/env python3
"""Bullet impact / ricochet / whiz sounds from Ready or Not's FMOD banks (private use, never in git).
Decodes the FSB5 Vorbis samples (pip package `fsb5`, needs libvorbis), converts them with ffmpeg to 16-bit PCM mono
48 kHz .wav in assets/build/sounds/impacts/<key>_<n>.wav, peak-normalised to -1 dBFS (never clipped), then measures
each file like tools/shot_loudness.py (loudest 50 ms) and writes src/Scripts/Deagle/guns/impact_sounds.lua with a
per-file gain that brings that blast to REF_DB (gains <= 1).
usage: impact_sounds.py <repo> [<RoN FMOD/Desktop dir>]"""
import math, os, re, subprocess, sys, tempfile, wave
import numpy as np

repo = sys.argv[1]
banks = sys.argv[2] if len(sys.argv) > 2 else "/mnt/c/Program Files (x86)/Steam/steamapps/common/Ready Or Not/ReadyOrNot/Content/FMOD/Desktop"
out_dir = os.path.join(repo, "assets", "build", "sounds", "impacts")
REF_DB = -12.0     # same reference as the gunshots (loudest 50 ms, dBFS)
PEAK_DB = -1.0

# key -> (bank, sample-name regex). Keys = effect families of deagle_impacts.xml (docs/impact_fx_build.md section 3)
# plus extra surfaces and flesh / armor / ricochet / whiz.
SETS = {
    "stone":      ("Interactions.bank", r"^imp_concrete_(sweet_)?\d+$"),
    "brick":      ("Interactions.bank", r"^imp_bricks_\d+$"),
    "ceramic":    ("Interactions.bank", r"^imp_tile_\d+$"),
    "plaster":    ("Interactions.bank", r"^imp_dry_wall_(sweet_)?\d+$"),
    "wood":       ("Interactions.bank", r"^imp_(wood|pine)_\d+$"),
    "metal":      ("Interactions.bank", r"^imp_steel_(b_)?\d+$"),
    "metal_thin": ("Interactions.bank", r"^imp_(galv|alum)_\d+$"),
    "glass":      ("Interactions.bank", r"^imp_glass_(sweet_)?\d+$"),
    "glass_break":("Interactions.bank", r"^Hotel_GlassBreaking_BulletImpact_\d+$"),
    "soil":       ("Interactions.bank", r"^imp_soil_\d+$"),
    "grass":      ("Interactions.bank", r"^imp_trash_heap_foliage_\d+$"),
    "water":      ("Interactions.bank", r"^imp_water_\d+$"),
    "cloth":      ("Interactions.bank", r"^imp_fabric_\d+$"),
    "soft":       ("Interactions.bank", r"^Interaction_BulletSoft_\d+$"),
    "spall":      ("Interactions.bank", r"^Spalling_Impact_\d+$"),
    "flesh":      ("Weapons.bank",      r"^weap_impact_gore_\d+$"),
    "flesh_player": ("Weapons.bank",    r"^PlyrImp_Flesh-\d+$"),
    "armor_body": ("SWAT.bank",         r"^weap_impact_armor_(body_v4|side_v3)_\d+$"),
    "armor_helmet": ("SWAT.bank",       r"^weap_impact_armor_helmet_v2_\d+$"),
    "armor":      ("Weapons.bank",      r"^(weap_shield_bullet_hit_\d+|PlyrImp_SAPI-\d+)$"),
    "bell":       ("Weapons.bank",      r"^imp_bullet_hit_bell_\d+$"),
    "ricochet":   ("Weapons.bank",      r"^ricochet-\d+$"),
    "ricochet_close": ("SWAT.bank",     r"^weap_impact_ricochet_close_\d+$"),
    "ricochet_far": ("SWAT.bank",       r"^weap_impact_ricochet_far_\d+$"),
    "whiz":       ("Weapons.bank",      r"^weap_whizzby_(mid|softmid)_\d+$"),
    "crack":      ("Weapons.bank",      r"^(sonic_crackV2_\d+|impact_1p_hit_crack_A_whizz_\d+)$"),
}
# families with no RoN set of their own reuse a close one (documented in docs/impact_fx_build.md section 7)
ALIAS = {"sand": "soil", "straw": "soft"}

def decode_banks():
    import fsb5
    found = {}
    for bank in sorted({b for b, _ in SETS.values()}):
        data = open(os.path.join(banks, bank), "rb").read()
        i = data.find(b"FSB5")
        while i >= 0:
            f = fsb5.FSB5(data[i:])
            for s in f.samples:
                for key, (b, rx) in SETS.items():
                    if b == bank and re.search(rx, s.name) and s.name not in {n for n, _ in found.get(key, [])}:
                        found.setdefault(key, []).append((s.name, f.rebuild_sample(s)))
            i = data.find(b"FSB5", i + 4)
    return found

def read(path):
    w = wave.open(path); n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
    x = np.frombuffer(w.readframes(n), np.int16).astype(float).reshape(-1, ch).mean(axis=1) / 32768; w.close()
    return x, sr

def blast_db(x, sr):
    win = int(0.05 * sr); e = np.convolve(x * x, np.ones(win) / win, mode="valid")
    return 10 * math.log10(e.max() + 1e-12)

os.makedirs(out_dir, exist_ok=True)
lua, total = {}, 0
with tempfile.TemporaryDirectory() as tmp:
    for key, items in sorted(decode_banks().items()):
        files = []
        for n, (name, blob) in enumerate(sorted(items), 1):
            src = os.path.join(tmp, "s.ogg"); open(src, "wb").write(blob)
            raw = os.path.join(tmp, "raw.wav")
            subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", src, "-ac", "1", "-ar", "48000", "-c:a", "pcm_s16le", raw], check=True)
            x, sr = read(raw)
            peak = np.abs(x).max() + 1e-12
            x = x * (10 ** (PEAK_DB / 20) / peak)           # peak-normalise, never clips
            rel = f"impacts/{key}_{n}.wav"
            w = wave.open(os.path.join(out_dir, f"{key}_{n}.wav"), "wb"); w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
            w.writeframes(np.clip(np.round(x * 32767), -32768, 32767).astype(np.int16).tobytes()); w.close()
            db = blast_db(x, sr)
            files.append((rel, round(min(1.0, 10 ** ((REF_DB - db) / 20)), 4), round(db, 1), round(len(x) / sr, 2), name))
            total += 1
        lua[key] = files

path = os.path.join(repo, "src", "Scripts", "Deagle", "guns", "impact_sounds.lua")
with open(path, "w") as fh:
    fh.write("-- generated by tools/impact_sounds.py from Ready or Not's FMOD banks (Interactions / Weapons / SWAT .bank; private use,\n"
             "-- files in assets/build/sounds/impacts, never in git). Pick one file at random per hit.\n"
             f"-- Files are peak-normalised to {PEAK_DB:.0f} dBFS; impactGain brings each one's loudest 50 ms to {REF_DB:.0f} dBFS like shot_gain.lua.\n"
             "-- Hearing distances: RoN's FMOD event attenuation (min / max distance) lives in the compiled event data and was\n"
             "-- not read. impactRange below is a GUESS (m) from how loud each kind of hit is; tune in game.\n")
    fh.write("Deagle.impactSounds = {\n")
    for key, files in lua.items():
        fh.write(f"\t{key} = {{ " + ", ".join(f'"{f[0]}"' for f in files) + " },\n")
    for a, b in ALIAS.items():
        fh.write(f"\t{a} = nil,  -- set below: no RoN set, uses {b}\n")
    fh.write("}\n")
    for a, b in ALIAS.items():
        fh.write(f"Deagle.impactSounds.{a} = Deagle.impactSounds.{b}\n")
    fh.write("\n-- per-file gain to the common level; source sample, blast dBFS and length in the comment\nDeagle.impactGain = {\n")
    for key, files in lua.items():
        for rel, g, db, dur, name in files:
            fh.write(f'\t["{rel}"] = {g},  -- {name}, {db} dB, {dur} s\n')
    fh.write("}\n")
    fh.write("\n-- min = full volume inside, max = silent beyond (m). GUESS (see above).\nDeagle.impactRange = {\n"
             "\tdefault = { min = 2, max = 40 }, metal = { min = 3, max = 60 }, metal_thin = { min = 3, max = 60 }, armor = { min = 3, max = 60 },\n"
             "\tbell = { min = 4, max = 120 }, flesh = { min = 1, max = 25 }, flesh_player = { min = 0, max = 5 }, ricochet = { min = 3, max = 80 },\n"
             "\tricochet_close = { min = 2, max = 30 }, ricochet_far = { min = 10, max = 150 }, whiz = { min = 0, max = 6 }, crack = { min = 0, max = 15 },\n"
             "\tspall = { min = 2, max = 30 }, glass_break = { min = 3, max = 50 },\n}\n")
print(f"IMPACT SOUNDS {total} files, {len(lua)} sets -> {path}")
