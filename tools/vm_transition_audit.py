# Transition audit: every clip combination the script can produce (ends, starts, interruptions, queued draw/holster).
# Instant switches must match within 2 mm / 1 deg; blends must move slower than 60% of the clips' own peak motion.
# usage: python3 tools/vm_transition_audit.py <tracks.json>   (tracks from tools/vm_tracks_dump.py)
# Transition audit v2: the rules the script implements. Instant switches (clip end -> rest) must match within 2 mm /
# 1 deg (magazines compared as an unordered pair, since the two mags are identical); blends must stay under the
# speed limits (gun / hands / elbows, slide and magazines relative to the gun).
import json, math, sys
T = json.load(open(sys.argv[1]))
FPS = 24.0
R = dict(blend=0.08, ads=0.14, lead=0.2, refire=0.12, cut=0.2, inspect_cut=0.3)
LIM = dict(pos=1500, ang=400, part=1500)          # mm/s, deg/s, mm/s
INST = dict(pos=2.0, ang=1.0, part=2.0)           # instant switch tolerances
BODY = ["j_gun", "j_wrist_ri", "j_wrist_le", "j_elbow_ri", "j_elbow_le"]

def qang(a, b):
    return math.degrees(2 * math.acos(min(1.0, abs(sum(x * y for x, y in zip(a[3:], b[3:]))))))
def rel(fr, part):
    g, p = fr["j_gun"], fr[part]
    w, x, y, z = g[3:]
    v = [p[i] - g[i] for i in range(3)]
    qx, qy, qz = -x, -y, -z
    tx = 2 * (qy * v[2] - qz * v[1]); ty = 2 * (qz * v[0] - qx * v[2]); tz = 2 * (qx * v[1] - qy * v[0])
    return [v[0] + w * tx + (qy * tz - qz * ty), v[1] + w * ty + (qz * tx - qx * tz), v[2] + w * tz + (qx * ty - qy * tx)]
def gap(fa, fb):
    pos = max(math.dist(fa[b][:3], fb[b][:3]) for b in BODY)
    ang = max(qang(fa[b], fb[b]) for b in BODY)
    slide = math.dist(rel(fa, "j_slide"), rel(fb, "j_slide"))
    a1, a2, b1, b2 = rel(fa, "j_mag1"), rel(fa, "j_mag2"), rel(fb, "j_mag1"), rel(fb, "j_mag2")
    near = lambda u, v: min(math.hypot(*u), math.hypot(*v)) < 350     # a parked (off-screen) mag doesn't count
    d = lambda u, v: math.dist(u, v) if near(u, v) else 0.0
    mags_same = max(d(a1, b1), d(a2, b2))
    mags_swap = max(d(a1, b2), d(a2, b1))
    return pos, ang, slide, mags_same, mags_swap

NAT = {}
for name, frames in T.items():
    sp = [gap(frames[i], frames[i + 1]) for i in range(len(frames) - 1)] or [(0, 0, 0, 0, 0)]
    NAT[name] = (max(x[0] for x in sp) * FPS, max(x[1] for x in sp) * FPS, max(max(x[2], x[3]) for x in sp) * FPS)
CAP = dict(pos=2000, ang=700, part=2000)
def limits(a, b):
    na, nb = NAT.get(a, (0, 0, 0)), NAT.get(b, (0, 0, 0))
    k = 0.6
    return (min(CAP["pos"], max(LIM["pos"], k * max(na[0], nb[0]))), min(CAP["ang"], max(LIM["ang"], k * max(na[1], nb[1]))),
            min(CAP["part"], max(LIM["part"], k * max(na[2], nb[2]))))
rows = []
first = lambda n: T[n][0]
last = lambda n: T[n][-1]
L = lambda n: (len(T[n]) - 1) / FPS
at = lambda n, s: T[n][max(0, min(len(T[n]) - 1, int(round(s * FPS))))]

def instant(label, fa, fb):
    pos, ang, slide, ms, mw = gap(fa, fb)
    part = max(slide, min(ms, mw))
    bad = pos > INST["pos"] or ang > INST["ang"] or part > INST["part"]
    rows.append((bad, "instant " + label, f"gap {pos:5.1f}mm {ang:4.1f}deg slide/mags {part:5.1f}mm", max(pos / INST["pos"], ang / INST["ang"], part / INST["part"])))
def blended(label, fa, fb, blend, names=("", "")):
    pos, ang, slide, ms, mw = gap(fa, fb)   # a blend interpolates each bone: mags move by their own (same-bone) distance
    part = max(slide, ms)
    sp = (pos / blend, ang / blend, part / blend)
    lp, la, lq = limits(*names)
    sc = max(sp[0] / lp, sp[1] / la, sp[2] / lq)
    rows.append((sc > 1, f"blend {blend:.2f}s " + label, f"gap {pos:5.1f}mm {ang:4.1f}deg part {part:5.1f}mm -> {sp[0]:5.0f}mm/s {sp[1]:4.0f}deg/s part {sp[2]:5.0f}mm/s", sc))
def blended_worst(label, a, b, blend, t0=0.0, t1=None, via_idle=False):
    t1 = L(a) if t1 is None else t1
    worst = None
    t = t0
    while t <= t1 + 1e-9:
        fa = at(a, t)
        pos, ang, slide, ms, mw = gap(fa, first(b))
        lp, la, lq = limits(a, b)
        sc = max(pos / blend / lp, ang / blend / la, max(slide, ms) / blend / lq)
        if worst is None or sc > worst[0]: worst = (sc, t)
        t += 1 / FPS
    blended(f"{label} (worst at {worst[1]:.2f}s)", at(a, worst[1]), first(b), blend, (a, b))

# clip end -> rest: instant
for a, r in (("shoot1", "idle1"), ("shoot2", "idle1"), ("shoot_empty", "idle_empty"), ("reload", "idle1"), ("reload_empty", "idle1"),
             ("inspect", "idle1"), ("inspect_empty", "idle_empty"), ("draw", "idle1"), ("draw_empty", "idle_empty")):
    instant(f"end {a} -> {r}", last(a), first(r))
# aim held: hand-over adsLead before the end (reload goes through an instant idle first: the spare mag is seated)
instant("handover at reload end -> idle1 (mag swap)", last("reload"), first("idle1"))
blended("handover idle1 -> ads", first("idle1"), first("ads"), R["ads"])
for a, r in (("reload_empty", "ads"), ("draw", "ads"), ("draw_empty", "ads_empty")):
    blended(f"handover {a} -> {r}", at(a, L(a) - R["lead"]), first(r), R["lead"])
# rest -> clip
for r, b, bl in (("idle1", "shoot1", R["blend"]), ("idle1", "shoot_empty", R["blend"]), ("idle1", "reload", R["blend"]), ("idle1", "inspect", R["blend"]),
                 ("idle1", "holster", R["blend"]), ("idle_empty", "reload_empty", R["blend"]), ("idle_empty", "inspect_empty", R["blend"]),
                 ("idle_empty", "holster_empty", R["blend"]), ("ads", "reload", R["cut"]), ("ads_empty", "reload_empty", R["cut"]),
                 ("ads", "holster", R["cut"]), ("ads_empty", "holster_empty", R["cut"]), ("idle1", "ads", R["ads"]), ("idle_empty", "ads_empty", R["ads"]),
                 ("ads", "idle1", R["ads"]), ("ads_empty", "idle_empty", R["ads"])):
    blended(f"start {r} -> {b}", first(r), first(b), bl)
# interruptions
blended_worst("refire shoot1 -> shoot2", "shoot1", "shoot2", R["refire"], t0=0.22)
blended_worst("refire shoot1 -> shoot_empty", "shoot1", "shoot_empty", R["refire"], t0=0.22)
for a, e in (("shoot1", ""), ("shoot_empty", "_empty")):
    blended_worst(f"cut {a} -> holster{e}", a, "holster" + e, R["cut"])
    blended_worst(f"cut {a} -> reload{e}", a, "reload" + e, R["cut"])
    blended_worst(f"cut {a} -> ads{e}", a, "ads" + e, R["cut"])
for b in ("shoot1", "shoot_empty", "reload", "holster", "ads"):
    blended_worst(f"cut inspect -> {b}", "inspect", b, R["inspect_cut"])
for b in ("reload_empty", "holster_empty", "ads_empty"):
    blended_worst(f"cut inspect_empty -> {b}", "inspect_empty", b, R["inspect_cut"])
# queued (no blend): holster mid-reload waits for the end; draw <-> holster wait for each other; draw blocks fire/reload/inspect
instant("queued reload end -> holster start", last("reload"), first("holster"))
instant("queued reload_empty end -> holster start", last("reload_empty"), first("holster"))
instant("queued draw end -> holster start", last("draw"), first("holster"))
instant("queued draw_empty end -> holster_empty start", last("draw_empty"), first("holster_empty"))

bad = [r for r in rows if r[0]]
print(f"TRANSITIONS {len(rows)}, JUMPS {len(bad)}  (blend limits {LIM}, instant tolerance {INST})")
for r in sorted(rows, key=lambda r: -r[3]):
    print(f"  {'JUMP' if r[0] else 'ok  '} {r[1]:58s} {r[2]}")
