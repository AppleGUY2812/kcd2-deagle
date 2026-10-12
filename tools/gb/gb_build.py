# Blender: gun profile + imported viewmodel (<slug>_vm.blend from gb_import) -> KCD2 viewmodel export, the generic
# version of tools/vm_export.py + tools/vm_extra_anims.py:
#   role clips (@idle, @shoot, @reload...) -> zeroed Source sections restored -> unkeyed bones filled -> @idle_empty ->
#   settle / ease-in wherever a clip doesn't start or end on its rest pose -> @draw_empty, @holster(_empty),
#   @inspect_empty -> magazine-swap timings -> view turned +Y, rounds split into their own skin -> <slug>_vm.chr,
#   <slug>_vm_skin.skin, <slug>_vm_rounds.skin and one DAE per clip at the clip's own frame rate.
# Writes <gun dir>/build/{model,anims}, build.json (clips, timings, materials, bones) and <slug>_vm_built.blend.
# usage: blender -b --factory-startup -P gb_build.py -- <gun dir> <profile.json> <rc.exe>
import bpy, sys, os, json, math, re, addon_utils
from mathutils import Matrix, Vector, Quaternion
GUN, PROF, RC = sys.argv[sys.argv.index("--") + 1:][:3]
prof = json.load(open(PROF))
slug = prof["slug"]
NAME = f"{slug}_vm"
bpy.ops.wm.open_mainfile(filepath=os.path.join(GUN, f"{NAME}.blend"))
scene = bpy.context.scene
arm = max([o for o in scene.objects if o.type == "ARMATURE"], key=lambda a: len(a.data.bones))
PB = arm.pose.bones
A = bpy.data.actions
B = {r: v["bone"] for r, v in prof["bones"].items() if v.get("bone") in arm.data.bones}
if not arm.animation_data:
    arm.animation_data_create()
info = {"slug": slug, "clips": {}, "notes": []}
note = lambda s: (info["notes"].append(s), print("NOTE", s))

# ---------------------------------------------------------------- role clips
USED = ["idle", "idle_empty", "draw", "draw_empty", "holster", "holster_empty", "shoot", "shoot_empty", "shoot_alt", "reload", "reload_empty", "inspect", "inspect_empty"]
for a in list(A):
    if a.name.startswith("@"):
        a.name = "src" + a.name          # keep the originals out of the way
def src(seq):
    return A.get("src@" + seq)
for role, c in prof["clips"].items():
    if role not in USED or not src(c["sequence"]):
        continue
    act = src(c["sequence"]).copy(); act.name = "@" + role; act["fps"] = float(c.get("fps") or 30.0); act.use_fake_user = True
    for i, seq in enumerate(c.get("variants", []) if role == "shoot" else []):
        if src(seq):
            v = src(seq).copy(); v.name = f"@shoot{i + 2}"; v["fps"] = act["fps"]; v.use_fake_user = True
roles = lambda: [a for a in A if a.name.startswith("@")]
print("ROLES", sorted(a.name for a in roles()))


def pose_at(act, f):
    arm.animation_data.action = act; scene.frame_set(int(f)); bpy.context.view_layer.update()
def rng(act):
    return tuple(int(v) for v in act.frame_range)
def snap():
    return {pb.name: (pb.location.copy(), pb.rotation_quaternion.copy()) for pb in PB}
def put(pose, act, f):
    arm.animation_data.action = act
    for pb in PB:
        l, r = pose[pb.name]
        pb.rotation_mode = "QUATERNION"; pb.location, pb.rotation_quaternion = l, r
        pb.keyframe_insert("location", frame=f, group=pb.name); pb.keyframe_insert("rotation_quaternion", frame=f, group=pb.name)
def lerp(p0, p1, k):
    out = {}
    for n, (l0, r0) in p0.items():
        l1, r1 = p1[n]
        if r0.dot(r1) < 0:
            r1 = -r1
        out[n] = (l0.lerp(l1, k), r0.slerp(r1, k))
    return out
def bake(name, pose, fps):
    act = A.new(name); act.use_fake_user = True; act["fps"] = fps
    put(pose, act, 1); put(pose, act, 2)
    return act

# ---------------------------------------------------------------- Source sections a bone doesn't animate come in zeroed
rest_local = {b.name: (b.parent.matrix_local.inverted() @ b.matrix_local) if b.parent else b.matrix_local for b in arm.data.bones}
cand = [n for n, m in rest_local.items() if m.translation.length > 1e-3]
for act in roles():
    f0, f1 = rng(act)
    bad = {}
    for f in range(f0, f1 + 1):
        pose_at(act, f)
        for n in cand:
            pb = PB[n]
            if (pb.parent.matrix.inverted() @ pb.matrix).translation.length < 1e-5:
                bad.setdefault(n, set()).add(f)
    nframes = f1 - f0 + 1
    # only a gap inside motion is a Source section artifact; zero for the whole clip is a real pose (tag bones often
    # sit exactly on their parent: "restoring" those moved the Glock's slide and barrel a metre away)
    bad = {n: fr for n, fr in bad.items() if len(fr) < nframes}
    for n, frames in bad.items():
        for fc in act.fcurves:
            if fc.data_path.startswith(f'pose.bones["{n}"].'):
                v = 1.0 if fc.data_path.endswith("rotation_quaternion") and fc.array_index == 0 else 0.0
                for kp in fc.keyframe_points:
                    if int(round(kp.co.x)) in frames:
                        kp.co.y = kp.handle_left.y = kp.handle_right.y = v
                fc.update()
    if bad:
        note(f"{act.name}: restored zeroed Source sections for {sorted(bad)}")

# ---------------------------------------------------------------- bones a clip leaves unkeyed get a neutral donor pose
keyed = lambda act: {fc.data_path.split('"')[1] for fc in act.fcurves if fc.data_path.startswith("pose.bones")}
DONORS = [("@idle", "first"), ("@shoot", "first"), ("@draw", "last"), ("@reload_empty", "last"), ("@reload", "last"), ("@inspect", "first")]
every = set().union(*(keyed(a) for a in roles()))
for bn in sorted(every):
    donor = next(((A[d], w) for d, w in DONORS if A.get(d) and bn in keyed(A[d])), None)
    if not donor:
        continue
    pose_at(donor[0], rng(donor[0])[0 if donor[1] == "first" else 1])
    l, r = PB[bn].location.copy(), PB[bn].rotation_quaternion.copy()
    for act in roles():
        if bn in keyed(act):
            continue
        f0, f1 = rng(act)
        for i in range(3):
            fc = act.fcurves.new(f'pose.bones["{bn}"].location', index=i, action_group=bn)
            fc.keyframe_points.insert(f0, l[i]); fc.keyframe_points.insert(f1, l[i])
        for i in range(4):
            fc = act.fcurves.new(f'pose.bones["{bn}"].rotation_quaternion', index=i, action_group=bn)
            fc.keyframe_points.insert(f0, r[i]); fc.keyframe_points.insert(f1, r[i])

# ---------------------------------------------------------------- empty idle (slide locked like the end of the last shot)
pose_at(A["@idle"], rng(A["@idle"])[0]); IDLE = snap()
LOCK, LOCK_MADE = None, False
if A.get("@shoot_empty") and "slide" in B:
    se, sl = A["@shoot_empty"], B["slide"]
    held = [sl] + [b for b in dict.fromkeys((B.get("barrel"), "j_barrel")) if b and b in PB and b != sl]
    def away(act):   # per frame: how far the slide is from its idle place
        f0, f1 = rng(act); out = []
        for f in range(f0, f1 + 1):
            pose_at(act, f); out.append((PB[sl].location - IDLE[sl][0]).length)
        return out
    se_away = away(se)
    # CS:S-era last shots often leave the slide alone (the Glock's glock_firelast doesn't key it) or run it home again;
    # a real pistol locks it back on the empty magazine: play the slide cycle (this clip's, else the single shot's) up
    # to its farthest-back point (slide + the barrel tilting with it) and hold it there to the end
    donor = se if max(se_away) > 1e-4 else A.get("@shoot")
    d_away = se_away if donor is se else (away(donor) if donor else [0])
    if donor and max(d_away) > 1e-4 and se_away[-1] < 0.5 * max(d_away):
        d0 = rng(donor)[0]; dmax = d0 + d_away.index(max(d_away))
        poses = {}
        for f in range(d0, dmax + 1):
            pose_at(donor, f); poses[f] = {b: (PB[b].location.copy(), PB[b].rotation_quaternion.copy()) for b in held}
        f0, f1 = rng(se)
        arm.animation_data.action = se
        for f in range(f0, f1 + 1):
            fd = d0 + (f - f0) / se["fps"] * donor["fps"]
            if fd >= dmax:
                p = poses[dmax]
            else:
                i = int(math.floor(fd)); p = lerp(poses[i], poses[i + 1], fd - i)
            for bn, (l, r) in p.items():
                PB[bn].rotation_mode = "QUATERNION"; PB[bn].location, PB[bn].rotation_quaternion = l, r
                PB[bn].keyframe_insert("location", frame=f, group=bn); PB[bn].keyframe_insert("rotation_quaternion", frame=f, group=bn)
        LOCK_MADE = True
        note(f"@shoot_empty: slide locked back at {(dmax - d0) / donor['fps']:.3f} s, {max(d_away) * 1000:.0f} mm "
             f"(cycle from {donor.name}: the source " + ("runs it home)" if donor is se else "doesn't move it)"))
    pose_at(se, rng(se)[1])
    LOCK = {n: (PB[n].location.copy(), PB[n].rotation_quaternion.copy())
            for n in [B.get("slide"), B.get("slide_stop")] + (held[1:] if LOCK_MADE else []) if n}
if not A.get("@idle_empty"):
    p = dict(IDLE); p.update(LOCK or {})
    bake("@idle_empty", p, A["@idle"]["fps"])
    note("made @idle_empty" + (" (slide locked from the last shot)" if LOCK else " (= idle: no last-shot clip)"))
pose_at(A["@idle_empty"], 1); IDLE_E = snap()

# ---------------------------------------------------------------- every clip starts / ends on its rest pose
BODY = [b for b in (B.get("gun"), "j_wrist_ri", "j_wrist_le", "j_elbow_ri", "j_elbow_le") if b and b in PB]
def body_at(act, f):
    pose_at(act, f)
    return {n: (arm.matrix_world @ PB[n].matrix).copy() for n in BODY}
def gap(m0, m1):
    pos = max((m0[n].translation - m1[n].translation).length for n in BODY) * 1000
    ang = max(math.degrees(m0[n].to_quaternion().rotation_difference(m1[n].to_quaternion()).angle) for n in BODY)
    return pos, ang
pose_at(A["@idle"], 1); REST = {n: (arm.matrix_world @ PB[n].matrix).copy() for n in BODY}
pose_at(A["@idle_empty"], 1); REST_E = {n: (arm.matrix_world @ PB[n].matrix).copy() for n in BODY}
rest_of_end = {"shoot_empty": ("@idle_empty", REST_E, IDLE_E)}
rest_of_start = {"reload_empty": ("@idle_empty", REST_E, IDLE_E), "shoot_empty": ("@idle", REST, IDLE)}
for act in [a for a in roles() if a.name not in ("@idle", "@idle_empty")]:
    role = act.name[1:]
    rname, rmat, rpose = rest_of_end.get(role, ("@idle", REST, IDLE))
    f0, f1 = rng(act)
    pos, ang = gap(body_at(act, f1), rmat)
    if pos > 3 or ang > 1.5:
        n = max(6, min(14, int(pos / 8)))
        pose_at(act, f1); last = snap()
        for i in range(1, n + 1):
            k = i / n; k = k * k * (3 - 2 * k)
            put(lerp(last, rpose, k), act, f1 + i)
        note(f"{act.name}: ended {pos:.0f} mm / {ang:.0f} deg off {rname}; appended a {n}-frame settle")
    if role.startswith(("draw", "holster")):
        continue
    sname, smat, spose = rest_of_start.get(role, ("@idle", REST, IDLE))
    pos, ang = gap(body_at(act, f0), smat)
    if pos > 3 or ang > 1.5:
        n = 8
        clip = []
        for f in range(f0, f0 + n):
            pose_at(act, f); clip.append((f, snap()))
        for f, cp in clip:
            k = (f - f0) / n; k = k * k * (3 - 2 * k)
            put(lerp(spose, cp, k), act, f)
        note(f"{act.name}: started {pos:.0f} mm / {ang:.0f} deg off {sname}; eased in over {n} frames")

# ---------------------------------------------------------------- empty reload with a lock we made: the source clip
# has the slide forward from the start, so keep it locked (never forward of the lock) until the hand charges it (the
# clip's farthest-back slide point), or failing that until its slide release / charge sound, then follow the clip home
if LOCK_MADE and A.get("@reload_empty"):
    re_, sl = A["@reload_empty"], B["slide"]
    f0, f1 = rng(re_)
    base = IDLE[sl][0]
    axis = (LOCK[sl][0] - base).normalized()
    travel = (LOCK[sl][0] - base).length
    frames = []
    for f in range(f0, f1 + 1):
        pose_at(re_, f); frames.append((f, PB[sl].location.copy()))
    # the charge is found on the untouched source clip: the ease-in above starts this one on the locked idle
    orig = src(prof["clips"].get("reload_empty", {}).get("sequence", "")) or re_
    proj = []
    for f, _ in frames:
        pose_at(orig, f); proj.append((PB[sl].location - base).dot(axis))
    i = proj.index(max(proj))
    how = "charge"
    if proj[i] < 0.5 * travel:
        cue = next((s["t"] for s in prof["clips"].get("reload_empty", {}).get("sounds", [])
                    if re.search(r"slide|charge|release", (s.get("option") or "") + (s.get("file") or ""), re.I)
                    and not re.search(r"mag", (s.get("option") or "") + (s.get("file") or ""), re.I)), None)
        i = int(round(cue * re_["fps"])) if cue is not None else int(len(frames) * 0.6)
        i = max(0, min(len(frames) - 1, i)); how = "sound cue" if cue is not None else "60% (no cue)"
    arm.animation_data.action = re_
    for f, L in frames[:i + 1]:
        s_ = max((L - base).dot(axis), travel)
        PB[sl].location = base + axis * s_ + ((L - base) - axis * (L - base).dot(axis))
        PB[sl].rotation_quaternion = LOCK[sl][1]
        PB[sl].keyframe_insert("location", frame=f, group=sl); PB[sl].keyframe_insert("rotation_quaternion", frame=f, group=sl)
        for n, (l, r) in LOCK.items():
            if n != sl:
                PB[n].location, PB[n].rotation_quaternion = l, r
                PB[n].keyframe_insert("location", frame=f, group=n); PB[n].keyframe_insert("rotation_quaternion", frame=f, group=n)
    note(f"@reload_empty: slide held locked until frame {frames[i][0]} ({how})")

# ---------------------------------------------------------------- empty draw / holsters / empty inspect
def locked_copy(srcname, dst):
    s = A.get(srcname)
    if not (s and LOCK):
        return None
    d = s.copy(); d.name = dst; d.use_fake_user = True; d["fps"] = s["fps"]
    for fc in [fc for fc in d.fcurves if fc.data_path.split('"')[1:2] and fc.data_path.split('"')[1] in LOCK]:
        d.fcurves.remove(fc)
    f0, f1 = rng(s)
    arm.animation_data.action = d
    for n, (l, r) in LOCK.items():
        for f in (f0, f1):
            PB[n].location, PB[n].rotation_quaternion = l, r
            PB[n].keyframe_insert("location", frame=f, group=n); PB[n].keyframe_insert("rotation_quaternion", frame=f, group=n)
    return d
if not A.get("@draw_empty") and locked_copy("@draw", "@draw_empty"):
    note("made @draw_empty (slide locked)")
for s, d in (("@draw", "@holster"), ("@draw_empty", "@holster_empty")):
    if A.get(d) or not A.get(s):
        continue
    h = A[s].copy(); h.name = d; h.use_fake_user = True; h["fps"] = A[s]["fps"]
    f0, f1 = A[s].frame_range
    for fc in h.fcurves:
        for kp in fc.keyframe_points:
            kp.co.x = f0 + f1 - kp.co.x
            kp.handle_left.x, kp.handle_right.x = f0 + f1 - kp.handle_right.x, f0 + f1 - kp.handle_left.x
        fc.update()
    note(f"made {d} (draw reversed)")
if A.get("@inspect") and not A.get("@inspect_empty") and LOCK and B.get("slide"):
    # the slide stays locked back; the hand's slide check still moves it: a locked slide gives a few mm more under a
    # pull and comes back onto the lock (a pull past that follows the hand); it never goes forward of the lock
    ie = A["@inspect"].copy(); ie.name = "@inspect_empty"; ie.use_fake_user = True; ie["fps"] = A["@inspect"]["fps"]
    sl = B["slide"]
    base = IDLE[sl][0]                     # travel measured from the idle slide, like the lock itself
    L_lock = LOCK[sl][0] - base
    axis = L_lock.normalized() if L_lock.length > 1e-6 else Vector((1, 0, 0))
    f0, f1 = rng(A["@inspect"])
    pulls = []
    for f in range(f0, f1 + 1):
        pose_at(A["@inspect"], f); pulls.append((f, PB[sl].location - base))
    pmax = max(L.dot(axis) for _, L in pulls)
    EXTRA = 0.006
    keys = []
    for f, L in pulls:
        p = L.dot(axis)
        s_ = max(p, L_lock.length + (EXTRA * min(1.0, max(0.0, p) / pmax) if pmax > 1e-4 else 0.0))
        keys.append((f, base + axis * s_ + (L - axis * p)))
    for fc in [fc for fc in ie.fcurves if fc.data_path.split('"')[1:2] and fc.data_path.split('"')[1] in LOCK]:
        ie.fcurves.remove(fc)
    arm.animation_data.action = ie
    for f, L in keys:
        PB[sl].location = L; PB[sl].rotation_quaternion = LOCK[sl][1]
        PB[sl].keyframe_insert("location", frame=f, group=sl); PB[sl].keyframe_insert("rotation_quaternion", frame=f, group=sl)
    for n, (l, r) in LOCK.items():
        if n != sl:
            for f in (f0, f1):
                PB[n].location, PB[n].rotation_quaternion = l, r
                PB[n].keyframe_insert("location", frame=f, group=n); PB[n].keyframe_insert("rotation_quaternion", frame=f, group=n)
    note("made @inspect_empty (slide locked)")

# ---------------------------------------------------------------- magazine-swap timings (which mag is seated when)
def mag_track(act):
    out = []
    g = B.get("gun")
    for f in range(rng(act)[0], rng(act)[1] + 1):
        pose_at(act, f)
        gp = arm.matrix_world @ PB[g].head if g else Vector()
        d = lambda n: ((arm.matrix_world @ PB[n].head) - gp).length * 1000 if n else 1e9
        out.append((f, d(B.get("mag")), d(B.get("mag2"))))
    return out
seated = None
if A.get("@idle") and B.get("mag"):
    seated = mag_track(A["@idle"])[0][1]
timing = {"swapTail": {}}
for role in ("reload", "reload_empty"):
    # a reload that ends with the spare mag seated: leaving it with a blend would slide the spare away from the gun,
    # so the script first switches to idle instantly (identical image) for this long before the end
    act = A.get("@" + role)
    if seated is None or not act or not B.get("mag2"):
        continue
    tr = mag_track(act); fps = act["fps"]; f1 = tr[-1][0]
    print(f"MAGEND @{role} seated {seated:.1f} mag1 {tr[-1][1]:.1f} mag2 {tr[-1][2]:.1f}")
    if abs(tr[-1][2] - seated) <= 3:
        k = len(tr) - 1
        while k > 0 and abs(tr[k - 1][2] - seated) <= 3:
            k -= 1
        timing["swapTail"][role] = round((f1 - tr[k][0]) / fps + 0.05, 3)
        note(f"@{role} ends with the spare mag seated for the last {timing['swapTail'][role]} s (instant switch to idle first)")
if seated is not None and A.get("@reload_empty"):
    tr = mag_track(A["@reload_empty"]); fps = A["@reload_empty"]["fps"]
    for (fa, a1, _), (fb, b1, _) in zip(tr, tr[1:]):
        if a1 - b1 > 300:                                               # the main mag teleports back as the new one
            timing["newMagAt"] = round((fa - tr[0][0] + 0.5) / fps, 3)
            note(f"@reload_empty: the dropped mag comes back as the new one at {timing['newMagAt']} s")
            break
# a burst clip (MWII's fireburst): its slide cycles give the gap between the rounds, so they land on the animation
if A.get("@shoot_alt") and B.get("slide"):
    act, sl = A["@shoot_alt"], B["slide"]
    tr = []
    for f in range(rng(act)[0], rng(act)[1] + 1):
        pose_at(act, f); tr.append((PB[sl].location - IDLE[sl][0]).length)
    top = max(tr)
    starts = [i for i in range(1, len(tr)) if tr[i] >= 0.3 * top > tr[i - 1]]
    if len(starts) >= 2:
        timing["burstGap"] = round((starts[-1] - starts[0]) / (len(starts) - 1) / act["fps"], 3)
        timing["burstRounds"] = len(starts)
        note(f"@shoot_alt: burst of {len(starts)} rounds, {timing['burstGap']} s apart (slide cycles)")
info["timing"] = timing

# ---------------------------------------------------------------- rc keeps keys on a 30 fps grid: faster clips are resampled
for act in roles():
    fps = float(act.get("fps", 30.0))
    if fps <= 30.0 + 1e-6:
        continue
    f0, f1 = rng(act)
    m = max(1, int(round((f1 - f0) * 30.0 / fps)))
    # blend neighbouring whole frames ourselves: Blender's own sub-frame evaluation of these imported curves swings
    # through garbage between frames (the Glock's left arm flew off in the shot); the game interpolates keys properly
    whole = {}
    def frame_pose(f):
        if f not in whole:
            pose_at(act, min(f, f1)); whole[f] = snap()
        return whole[f]
    samples = []
    for j in range(m + 1):
        t = min(f0 + j * fps / 30.0, float(f1))
        fl = int(math.floor(t))
        samples.append(lerp(frame_pose(fl), frame_pose(min(fl + 1, f1)), t - fl))
    new = A.new(act.name + "_30"); new.use_fake_user = True; new["fps"] = 30.0
    for j, p in enumerate(samples):
        put(p, new, 1 + j)
    old = act.name; A.remove(act); new.name = old
    note(f"{old}: {f1 - f0 + 1} frames at {fps:g} fps resampled to {m + 1} at 30 fps (same {(f1 - f0) / fps:.3f} s)")
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(GUN, f"{NAME}_roles.blend"), copy=True)

# ---------------------------------------------------------------- export
addon_utils.enable("io_KCD2_Blender_Toolkit", default_set=True)
from io_KCD2_Blender_Toolkit.bcry_exporter.configuration import Configuration
Configuration.rc_path = RC; Configuration.texture_rc_path = RC
for a in [a for a in A if a.name.startswith("src@")]:
    A.remove(a)
meshes = [o for o in scene.objects if o.type == "MESH"]
for o in [o for o in scene.objects if o.type not in ("ARMATURE", "MESH")]:
    bpy.data.objects.remove(o)
R = Matrix.Rotation(math.radians(90), 4, "Z")                # Source view looks +X -> CryEngine +Y
for o in [arm] + [m for m in meshes if m.parent is None]:
    o.matrix_world = R @ o.matrix_world
bpy.ops.object.select_all(action="DESELECT")
for o in [arm] + meshes:
    o.select_set(True)
bpy.context.view_layer.objects.active = arm
bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
renamed = {}
for b in arm.data.bones:
    if "." in b.name or " " in b.name:
        new = re.sub(r"[^A-Za-z0-9_]", "_", b.name); renamed[b.name] = new; b.name = new
info["bones"] = {r: renamed.get(n, n) for r, n in B.items()}
arm.name = NAME
mats, slot = [], {}
for o in meshes:
    for s in o.material_slots:
        if s.material and s.material.name not in slot:
            slot[s.material.name] = len(mats) + 1; mats.append(s.material.name)
info["materials"] = []
for i, m in enumerate(mats, 1):
    sub = re.sub(r"[^A-Za-z0-9]", "", m)[:24] or f"M{i}"
    bpy.data.materials[m].name = f"{NAME}__{i:02d}__{sub}__physNone"
    info["materials"].append({"id": i, "source": m, "sub": sub})
print("MATERIALS", info["materials"])
bpy.ops.object.select_all(action="DESELECT")
for o in meshes:
    o.select_set(True)
bpy.context.view_layer.objects.active = meshes[0]
bpy.ops.object.join()
skin = bpy.context.view_layer.objects.active
skin.name = f"{NAME}_skin"
if skin.parent is not arm:
    mw = skin.matrix_world.copy(); skin.parent = arm; skin.matrix_world = mw
for md in skin.modifiers:
    if md.type == "ARMATURE":
        md.object = arm
rounds = None
mag, mag2 = info["bones"].get("mag"), info["bones"].get("mag2")
def under(b, top):
    b = arm.data.bones.get(b).parent if arm.data.bones.get(b) else None
    while b:
        if b.name == top:
            return True
        b = b.parent
    return False
rbones = {b.name for b in arm.data.bones if mag and under(b.name, mag) and not (mag2 and under(b.name, mag2)) and not re.search(r"follow|spring", b.name, re.I)}
rb = ", ".join(sorted(rbones))
if rbones:
    gid = {skin.vertex_groups[n].index for n in rbones if n in skin.vertex_groups}
    inmag = {v.index for v in skin.data.vertices if v.groups and max(v.groups, key=lambda g: g.weight).group in gid}
    for v in skin.data.vertices: v.select = False
    for e in skin.data.edges: e.select = False
    nsel = 0
    for p in skin.data.polygons:
        p.select = all(i in inmag for i in p.vertices)
        if p.select:
            nsel += 1
            for i in p.vertices: skin.data.vertices[i].select = True
    if nsel:
        bpy.ops.object.select_all(action="DESELECT"); skin.select_set(True); bpy.context.view_layer.objects.active = skin
        bpy.ops.object.mode_set(mode="EDIT"); bpy.ops.mesh.select_mode(type="FACE"); bpy.ops.mesh.separate(type="SELECTED")
        bpy.ops.object.mode_set(mode="OBJECT")
        rounds = next(o for o in bpy.context.selected_objects if o is not skin)
        rounds.name = f"{NAME}_rounds"
        note(f"magazine rounds ({rb}) split into their own skin: {len(rounds.data.vertices)} verts")
info["has_rounds"] = rounds is not None
pm = bpy.data.meshes.new(f"{NAME}_proxy"); pm.from_pydata([(0, 0, 0), (0.001, 0, 0), (0, 0, 0.001)], [], [(0, 1, 2)])
pm.materials.append(bpy.data.materials[f"{NAME}__01__{info['materials'][0]['sub']}__physNone"])
proxy = bpy.data.objects.new(f"{NAME}_proxy", pm); scene.collection.objects.link(proxy); proxy.parent = arm
root = next(b.name for b in arm.data.bones if not b.parent)
vg = proxy.vertex_groups.new(name=root); vg.add([0, 1, 2], 1.0, "REPLACE")
md = proxy.modifiers.new("Armature", "ARMATURE"); md.object = arm
bpy.ops.object.select_all(action="DESELECT"); arm.select_set(True); proxy.select_set(True); bpy.context.view_layer.objects.active = proxy
bpy.ops.bcry.add_cry_export_node("EXEC_DEFAULT", node_type="chr", node_name=NAME)
for o, n in ((skin, f"{NAME}_skin"), (rounds, f"{NAME}_rounds")):
    if o:
        bpy.ops.object.select_all(action="DESELECT"); o.select_set(True); bpy.context.view_layer.objects.active = o
        bpy.ops.bcry.add_cry_export_node("EXEC_DEFAULT", node_type="skin", node_name=n)
out_model, out_anims = os.path.join(GUN, "build", "model"), os.path.join(GUN, "build", "anims")
os.makedirs(out_model, exist_ok=True); os.makedirs(out_anims, exist_ok=True)
arm.animation_data.action = A["@idle"]; scene.frame_set(rng(A["@idle"])[0])
bpy.ops.bcry.export_to_game("EXEC_DEFAULT", filepath=os.path.join(out_model, f"{NAME}.dae"), apply_modifiers=False,
                            generate_materials=False, convert_textures=False, save_dae=True, disable_rc=False)
print("MODEL EXPORTED", sorted(os.listdir(out_model)))
for act in sorted(roles(), key=lambda a: a.name):
    clip = f"{NAME}_{act.name[1:]}"
    for c in [c for c in bpy.data.collections if c.name.endswith(".i_caf")]:
        bpy.data.collections.remove(c)
    fps = float(act.get("fps", 30.0))
    scene.render.fps = int(round(fps)); scene.render.fps_base = round(fps) / fps
    arm.animation_data.action = act
    f0, f1 = rng(act)
    scene.frame_start, scene.frame_end = f0, f1
    bpy.ops.object.select_all(action="DESELECT"); arm.select_set(True); bpy.context.view_layer.objects.active = arm
    bpy.ops.bcry.add_cry_animation_node("EXEC_DEFAULT", node_type="i_caf", node_name=clip, mode="Manual", range_type="Values", node_start=f0, node_end=f1)
    bpy.ops.bcry.export_animations("EXEC_DEFAULT", filepath=os.path.join(out_anims, f"{clip}.dae"), disable_rc=True, save_dae=True)
    info["clips"][act.name[1:]] = {"name": clip, "frames": f1 - f0 + 1, "fps": fps}
    print("ANIM", clip, f0, f1, fps)
    if act.name in ("@shoot", "@shoot_empty"):    # additive copy: recoil stacked on the idle / ADS pose (burst, auto)
        for c in [c for c in bpy.data.collections if c.name.endswith(".i_caf")]:
            bpy.data.collections.remove(c)
        bpy.ops.object.select_all(action="DESELECT"); arm.select_set(True); bpy.context.view_layer.objects.active = arm
        bpy.ops.bcry.add_cry_animation_node("EXEC_DEFAULT", node_type="i_caf", node_name=clip + "_add", mode="Manual", range_type="Values", node_start=f0, node_end=f1)
        bpy.ops.bcry.export_animations("EXEC_DEFAULT", filepath=os.path.join(out_anims, f"{clip}_add.dae"), disable_rc=True, save_dae=True)
        info["clips"][act.name[1:] + "_add"] = {"name": clip + "_add", "frames": f1 - f0 + 1, "fps": fps}
        print("ANIM", clip + "_add", f0, f1, fps)
json.dump(info, open(os.path.join(GUN, "build.json"), "w"), indent=1)
print("GB BUILD DONE")
