"""Gun profile from an inspected Source viewmodel: which sequence plays which role, which bones are the slide /
magazine / rounds, where the muzzle and ejection port are, and the sound cues per clip.
Pure Python (no Blender): used by gb_inspect.py, testable on its own. Every guess is written to the profile so it
can be corrected by hand (guns/<slug>.json)."""
import re

# role -> (activity names, name patterns); 'not' patterns exclude
ROLES = [
    ("idle_empty", ["ACT_VM_IDLE_EMPTY"], [r"idle.*(empty|_e\b|dry)", r"empty.*idle"]),
    ("idle", ["ACT_VM_IDLE"], [r"^idle\d*$", r"^idle(?!.*(empty|lower|sprint|run|crouch|walk|ads|aim))"]),
    ("draw_empty", ["ACT_VM_DRAW_EMPTY"], [r"(draw|deploy|pullout|raise).*(empty|_e\b)"]),
    ("draw_first", [], [r"(draw|deploy|pullout|raise).*first", r"first.*(draw|raise)"]),
    ("draw", ["ACT_VM_DRAW", "ACT_VM_DEPLOY"], [r"^(draw|deploy|pullout|raise)(?!.*(empty|first))"]),
    ("holster_empty", [], [r"(holster|putaway|drop).*(empty|_e\b)"]),
    ("holster", ["ACT_VM_HOLSTER"], [r"^(holster|putaway)(?!.*empty)"]),
    ("shoot_empty", ["ACT_VM_PRIMARYATTACK_EMPTY"], [r"(shoot|fire|shot).*(empty|last)", r"lastshot", r"firelast"]),
    ("dryfire", ["ACT_VM_DRYFIRE"], [r"dry(fire)?"]),
    ("shoot", ["ACT_VM_PRIMARYATTACK"], [r"^(shoot|fire|shot)(single)?\d*$", r"^(shoot|fire|shot)(?!.*(empty|last|ads|aim|burst))"]),
    ("reload_empty", ["ACT_VM_RELOAD_EMPTY"], [r"reload.*(empty|_e\b|dry)", r"empty.*reload"]),
    ("reload", ["ACT_VM_RELOAD"], [r"^reload(?!.*(empty|dry|start|end|insert|loop))"]),
    ("reload_start", [], [r"reload.*start"]), ("reload_insert", [], [r"reload.*(insert|loop)"]), ("reload_end", [], [r"reload.*end"]),
    ("inspect_empty", [], [r"(inspect|lookat|check).*(empty|_e\b)"]),
    ("inspect", ["ACT_VM_FIDGET"], [r"inspect|lookat|check|fidget"]),
    ("sprint", ["ACT_VM_SPRINT_IDLE"], [r"sprint|run"]),
    ("shoot_alt", ["ACT_VM_SECONDARYATTACK"], [r"burst|alt.?fire|secondary"]),
    ("melee", ["ACT_VM_HITCENTER"], [r"melee|bash|stab"]),
]
REQUIRED = ("idle", "draw", "shoot", "reload")

BONES = [   # role, patterns (first matching bone wins; CoD names first, then Source / generic)
    ("camera", [r"^tag_view$", r"camera", r"^v_weapon\.?camera"]),
    ("gun", [r"^j_gun$", r"^tag_weapon$", r"^v_weapon\.?\w*_parent$", r"^v_weapon$"]),
    ("slide", [r"^j_slide$", r"^j_bolt$", r"slide(?!.*(stop|release|lock))", r"bolt(?!.*(release|catch))", r"charg"]),
    ("slide_stop", [r"^j_slidestop$", r"slide.?(stop|release|lock)", r"bolt.?(release|catch)"]),
    ("mag", [r"^j_mag1$", r"^j_mag$", r"mag(?!.*2)(azine)?(?!.*(release|well|catch))", r"clip"]),
    ("mag2", [r"^j_mag2$", r"mag.*2|mag.*new"]),
    ("rounds", [r"^j_ammo_01$", r"^j_ammo", r"bullet", r"round", r"ammo(?!.*(belt|box))"]),
    ("hammer", [r"^j_hammer$", r"hammer"]),
    ("trigger", [r"^j_trigger$", r"trigger"]),
    ("muzzle", [r"^tag_flash$", r"muzzle", r"flash"]),
    ("brass", [r"^tag_brass$", r"brass", r"shell", r"eject"]),
]
ATTACHMENTS = [("muzzle", [r"muzzle", r"flash", r"^1$"]), ("brass", [r"shell", r"eject", r"brass", r"^2$"])]

MAG_SIZES = [   # name pattern -> rounds (edit in the profile if the gun differs)
    (r"deagle|desert ?eagle", 7), (r"glock ?18", 33), (r"glock|p320|m9\b|m9a1|p226", 17), (r"usp", 12),
    (r"1911|m45", 7), (r"makarov|pm\b", 8), (r"p90", 50), (r"mp7", 40), (r"uzi", 32), (r"vector", 25), (r"mp5|mp9|mpx|spc|vityaz|pp-?19", 30),
    (r"ak|akm|m4|mk18|hk416|scar|mcx|mk16|g3|mk17|sa-?58|l85|tavor|x95|mdr|sg ?552|honey ?badger|as ?val|vhs|sks", 30),
    (r"m249", 100), (r"svd|m110|sass", 10), (r"barrett|m82|tac-?338", 10),
    (r"supernova|ksg|m590|benelli|m4 super", 8),
]


def _first(patterns, names, exclude=()):
    for p in patterns:
        rx = re.compile(p, re.I)
        for n in names:
            if n not in exclude and rx.search(n):
                return n
    return None


def _strip_prefix(names):
    """'glock_draw', 'glock_idle' -> 'draw', 'idle': drop a weapon-name prefix most sequences share"""
    pre = {}
    for n in names:
        if "_" in n:
            pre[n.split("_", 1)[0]] = pre.get(n.split("_", 1)[0], 0) + 1
    common = [p for p, c in pre.items() if c >= max(2, len(names) // 2)]
    return {n: (n.split("_", 1)[1] if common and n.split("_", 1)[0] in common and "_" in n else n) for n in names}


def guess_roles(sequences):
    """sequences: list of dicts {name, activity, frames, fps}. Returns {role: sequence name}."""
    used, roles = set(), {}
    short = _strip_prefix([s["name"] for s in sequences])
    back = {v: k for k, v in short.items()}
    for role, acts, pats in ROLES:
        by_act = [s["name"] for s in sequences if s.get("activity") in acts and s["name"] not in used]
        hit = _first(pats, list(short.values()), {short[u] for u in used})
        pick = by_act[0] if by_act else (back[hit] if hit else None)
        if pick:
            roles[role] = pick
            used.add(pick)
    return roles


def guess_bones(bones, attachments):
    out = {}
    for role, pats in BONES:
        b = _first(pats, bones)
        if b:
            out[role] = {"bone": b}
    for role, pats in ATTACHMENTS:
        if role in out:
            continue
        a = _first(pats, [x["name"] for x in attachments])
        if a:
            att = next(x for x in attachments if x["name"] == a)
            out[role] = {"attachment": a, "bone": att.get("bone")}
    return out


def guess_mag_size(title):
    for pat, n in MAG_SIZES:
        if re.search(pat, title or "", re.I):
            return n
    return None


def parse_keyvalues(text):
    """Valve KeyValues -> list of (key, value) pairs; a value is a string or a nested list. Comments // are skipped."""
    toks, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c.isspace():
            i += 1
        elif text.startswith("//", i):
            j = text.find("\n", i); i = n if j < 0 else j
        elif c in "{}":
            toks.append(c); i += 1
        elif c == '"':
            j = text.find('"', i + 1); j = n if j < 0 else j
            toks.append(("s", text[i + 1:j])); i = j + 1
        else:
            j = i
            while j < n and not text[j].isspace() and text[j] not in '{}"':
                j += 1
            toks.append(("s", text[i:j])); i = j
    pos = 0
    def block():
        nonlocal pos
        out = []
        while pos < len(toks):
            t = toks[pos]
            if t == "}":
                pos += 1; return out
            if t == "{":
                pos += 1; out.append(("", block())); continue
            key = t[1]; pos += 1
            if pos < len(toks) and toks[pos] == "{":
                pos += 1; out.append((key, block()))
            elif pos < len(toks) and toks[pos] != "}":
                out.append((key, toks[pos][1] if isinstance(toks[pos], tuple) else "")); pos += 1
        return out
    return block()


def soundscripts(text):
    """soundscript text -> {sound name (lowercase): [wave paths]}"""
    out = {}
    for name, body in parse_keyvalues(text):
        if not isinstance(body, list):
            continue
        waves = []
        for k, v in body:
            if k.lower() == "wave" and isinstance(v, str):
                waves.append(v)
            elif k.lower() == "rndwave" and isinstance(v, list):
                waves += [w for kk, w in v if kk.lower() == "wave" and isinstance(w, str)]
        if waves:
            out[name.lower()] = [re.sub(r"^[^A-Za-z0-9_]+", "", w).replace("\\", "/") for w in waves]
    return out


def weapon_script(text):
    """weapon_*.txt -> flat {key (lowercase): value} of the top-level weapon data"""
    out = {}
    def walk(kv):
        for k, v in kv:
            if isinstance(v, list):
                walk(v)
            elif k and k.lower() not in out:
                out[k.lower()] = v
    walk(parse_keyvalues(text))
    return out


def _tokens(s):
    return [t for t in re.split(r"[^a-z0-9]+", s.lower()) if t and t not in ("wav", "mp3", "ogg", "weapon", "weapons", "sound", "sounds")]


def _find_wave(wave, files):
    w = wave.lower().replace("\\", "/")
    return next((f for f in files if f.lower().endswith("sound/" + w) or f.lower().endswith(w)), None)


def match_sound(option, files, scripts=None):
    """event option (soundscript name or path) -> matching sound file(s) (relative path, or a list for random
    variants) or None. Soundscripts first (exact), then the path, then a strict token match."""
    opt = option.strip().replace("\\", "/").lower()
    if not opt:
        return None
    if scripts and opt in scripts:
        found = [f for f in (_find_wave(w, files) for w in scripts[opt]) if f]
        return (found[0] if len(found) == 1 else found) if found else None
    for f in files:
        if f.lower().endswith(opt) or opt.endswith(f.lower()):
            return f
    want = set(_tokens(opt.rsplit(".", 1)[0] if "/" in opt else opt))
    best, score = None, 0.0
    for f in files:
        have = set(_tokens(f.rsplit("/", 1)[-1].rsplit(".", 1)[0]))
        if not have:
            continue
        s = len(want & have) / max(1, len(have))
        if s > score:
            best, score = f, s
    return best if score >= 0.75 else None


SOUND_EVENTS = (5004, 5006, 5005, 3004)   # CL_EVENT_SOUND and friends


def sound_cues(seq, files, scripts=None):
    """[(seconds, file)] for a sequence's sound events."""
    frames, fps = max(1, seq.get("frames") or 1), seq.get("fps") or 30.0
    cues = []
    for e in seq.get("events", []):
        if e.get("event") not in SOUND_EVENTS and not e.get("name", "").upper().startswith(("AE_CL_PLAYSOUND", "CL_EVENT_SOUND")):
            continue
        f = match_sound(e.get("options", ""), files, scripts)
        cues.append({"t": round(e.get("cycle", 0.0) * (frames - 1) / fps, 3), "file": f, "option": e.get("options", "")})
    return cues


def shot_sounds(scripts, files):
    """the gunshot: CS weapons play soundscript '<weapon>.Single' from code, not from an animation event"""
    for name in sorted(scripts, key=lambda n: (not n.endswith(".single"), n)):
        if re.search(r"\.single$|\.(shot|fire)\b", name):
            found = [f for f in (_find_wave(w, files) for w in scripts[name]) if f]
            if found:
                return found
    return []


def _int(v):
    try: return int(float(v))
    except (TypeError, ValueError): return None


def _float(v):
    try: return float(v)
    except (TypeError, ValueError): return None


def build_profile(inspect, title="", source=None):
    seqs = inspect["sequences"]
    ws = inspect.get("weapon_script") or {}
    roles = guess_roles(seqs)
    by_name = {s["name"]: s for s in seqs}
    clips = {}
    for role, name in roles.items():
        s = by_name[name]
        clips[role] = {"sequence": name, "frames": s.get("frames"), "fps": s.get("fps"),
                       "sounds": sound_cues(s, inspect.get("sounds", []), inspect.get("soundscripts"))}
    # several plain shot sequences (shoot1, shoot2...) alternate
    if "shoot" in roles:
        rx = re.compile(r"^(\w+_)?(shoot|fire|shot)(single)?\d*$", re.I)
        alts = [s["name"] for s in seqs if rx.match(s["name"]) and s["name"] != roles["shoot"] and s["name"] not in roles.values()]
        if alts:
            clips["shoot"]["variants"] = alts
            roles.update({f"shoot_variant{i + 1}": a for i, a in enumerate(alts)})
    missing = [r for r in REQUIRED if r not in roles]
    return {
        "title": title,
        "source": source or {},
        "mdl": inspect.get("mdl"),
        "magSize": _int(ws.get("clip_size")) or guess_mag_size(title or inspect.get("mdl", "")),
        "fireInterval": _float(ws.get("cycletime")),
        "weaponScript": {k: ws[k] for k in ("printname", "clip_size", "cycletime", "damage", "range", "penetration", "weapontype", "bullets") if k in ws},
        "clips": clips,
        "bones": guess_bones(inspect.get("bones", []), inspect.get("attachments", [])),
        "shotSounds": shot_sounds(inspect.get("soundscripts") or {}, inspect.get("sounds", [])),
        "unassigned_sequences": [s["name"] for s in seqs if s["name"] not in roles.values()],
        "missing_required": missing,
        "notes": "guessed by tools/gb/profile_guess.py; correct any wrong role / bone / magSize here",
    }
