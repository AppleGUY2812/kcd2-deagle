# Fit Henry's arm skeleton onto any game's first-person arms, frame by frame (Blender). Game-independent: the only
# per-game data is PROFILES (which source joint sits where Henry's joint sits); the fit uses joint positions plus each
# source bone's rest-to-pose rotation, so it doesn't care about bone axes, rolls or rest poses.
# Fit, per side: copy rotations (gives every bone its twist) -> aim the clavicle at the source shoulder -> 2-bone IK
# with the elbow bent toward the source elbow and the palm centre on the source palm centre (Henry's hand is smaller:
# pinning the wrist alone leaves the knuckles short) -> hand rotation -> aim each finger bone at the source's next joint.
from mathutils import Matrix, Vector
import math

def _fingers(fmt, names, ext):
    """fmt(finger, i) -> source joint name; ext: the source has no tip joint, extrapolate the last segment."""
    m = {}
    for H, (src, meta) in names.items():
        start = 0 if meta else 1
        for i in range(1, 5):
            m["Hand%s%d" % (H, i)] = fmt(src, i + start) if (i < 4 or not ext) else ("ext", fmt(src, 3 + start), fmt(src, 2 + start))
    return m

def ron_profile(s):
    m = {"Shoulder": "clavicle_" + s, "Arm": "upperarm_" + s, "ForeArm": "lowerarm_" + s, "Hand": "hand_" + s,
         "HandThumb1": "thumb_1_" + s, "HandThumb2": "thumb_2_" + s, "HandThumb3": "thumb_3_" + s, "HandThumb4": "thumb_4_end_" + s,
         "InHandRing": "ring_1_" + s, "InHandPinky": "pinky_1_" + s}
    for f in ("Index", "Middle", "Ring", "Pinky"):
        r = f.lower()
        for i in range(1, 5):
            m["Hand%s%d" % (f, i)] = ("%s_%d_%s" % (r, i + 1, s)) if i < 4 else "%s_5_end_%s" % (r, s)
    return m

def cod_profile(s):   # Call of Duty viewmodel arms (MW19 / MWII, through the CS:S GameBanana ports)
    m = {"Shoulder": "j_clavicle_" + s, "Arm": "j_shoulder_" + s, "ForeArm": "j_elbow_" + s, "Hand": "j_wrist_" + s,
         "InHandRing": "j_metaring_%s_1" % s, "InHandPinky": "j_metapinky_%s_1" % s}
    for f, r in (("Thumb", "thumb"), ("Index", "index"), ("Middle", "mid"), ("Ring", "ring"), ("Pinky", "pinky")):
        for i in range(1, 4):
            m["Hand%s%d" % (f, i)] = "j_%s_%s_%d" % (r, s, i)
        m["Hand%s4" % f] = ("ext", "j_%s_%s_3" % (r, s), "j_%s_%s_2" % (r, s))
    return m

PROFILES = {   # name: (detect bone, profile fn, side tokens (right, left), arm meshes)
    "ron": ("upperarm_RI", ron_profile, ("RI", "LE"), ["ron_arms"]),
    "cod": ("j_shoulder_ri", cod_profile, ("ri", "le"), ["mp_western_vm_arms.smd", "oakley_base.smd"]),
}
def detect(src):
    for name, p in PROFILES.items():
        if p[0] in src.data.bones: return name
    raise RuntimeError("no arm profile matches " + src.name)

CHAINS = [["Shoulder", "Arm", "ForeArm", "Hand"], ["HandThumb1", "HandThumb2", "HandThumb3", "HandThumb4"],
          ["HandIndex1", "HandIndex2", "HandIndex3", "HandIndex4"], ["HandMiddle1", "HandMiddle2", "HandMiddle3", "HandMiddle4"],
          ["InHandRing", "HandRing1", "HandRing2", "HandRing3", "HandRing4"], ["InHandPinky", "HandPinky1", "HandPinky2", "HandPinky3", "HandPinky4"]]
EXT = 0.8   # a missing fingertip = last joint + 0.8 x the previous segment

class Fit:
    def __init__(self, bpy, src, henry, G):
        self.bpy, self.src, self.h, self.G = bpy, src, henry, G
        self.profile = detect(src)
        _, fn, sides, self.arm_meshes = PROFILES[self.profile]
        self.sides = [("Right", fn(sides[0])), ("Left", fn(sides[1]))]

    def source_scale(self):
        """source arm size / Henry's (shoulder -> elbow -> wrist -> middle knuckle, rest pose, both sides). Shooters
        often shrink first-person arms (CoD's hand: 6.3 cm wrist to knuckle; Henry 8.3, RoN 9.3). Scaling the whole
        viewmodel (gun + arms) by 1 / this about the camera leaves the picture unchanged and makes the source arms
        Henry's size, so his real-size arms and clothing fit."""
        src = hen = 0.0
        for H, m in self.sides:
            for a, b in (("Arm", "ForeArm"), ("ForeArm", "Hand"), ("Hand", "HandMiddle1")):
                src += (self.sp(m[b], True) - self.sp(m[a], True)).length
                hen += (self.hp(H + b, True) - self.hp(H + a, True)).length
        return src / hen

    # --- positions / rotations ---
    def upd(self): self.bpy.context.view_layer.update()
    def sp(self, j, rest=False):
        if isinstance(j, tuple):
            a, b = self.sp(j[1], rest), self.sp(j[2], rest); return a + (a - b) * EXT
        if rest: return self.src.matrix_world @ self.src.data.bones[j].head_local
        return (self.src.matrix_world @ self.src.pose.bones[j].matrix).translation
    def hp(self, b, rest=False):
        return (self.h.matrix_world @ (self.h.data.bones[b].matrix_local if rest else self.h.pose.bones[b].matrix)).translation
    def hq(self, b, rest=False):
        return (self.h.matrix_world @ (self.h.data.bones[b].matrix_local if rest else self.h.pose.bones[b].matrix)).to_quaternion()
    def set_rot(self, b, q):
        self.h.pose.bones[b].matrix = self.h.matrix_world.inverted() @ Matrix.LocRotScale(self.hp(b), q, None); self.upd()
    def swing(self, b, frm, to):
        if frm.length < 1e-6 or to.length < 1e-6: return
        self.set_rot(b, frm.normalized().rotation_difference(to.normalized()) @ self.hq(b))
    def copy_rot(self, H, m, b, child):
        sb = m[b]
        if isinstance(sb, tuple): return
        dh = (self.hp(H + child, True) - self.hp(H + b, True)).normalized()
        dr = (self.sp(m[child], True) - self.sp(sb, True)).normalized()
        S = self.src.matrix_world
        q = (S @ self.src.pose.bones[sb].matrix).to_quaternion() @ (S @ self.src.data.bones[sb].matrix_local).to_quaternion().inverted() \
            @ dh.rotation_difference(dr) @ self.hq(H + b, True)
        self.set_rot(H + b, q)
    def aim(self, H, m, b, child):
        J = self.hp(H + b); self.swing(H + b, self.hp(H + child) - J, self.sp(m[child]) - J)

    def ik(self, H, m, T, pole):
        S, E, W = self.hp(H + "Arm"), self.hp(H + "ForeArm"), self.hp(H + "Hand")
        a, b = (E - S).length, (W - E).length
        d = min((T - S).length, a + b - 1e-4)
        u = (T - S).normalized(); v = (pole - S) - u * (pole - S).dot(u)
        v = v.normalized() if v.length > 1e-6 else Vector((0, 0, -1))
        x = (a * a - b * b + d * d) / (2 * d)
        self.swing(H + "Arm", E - S, u * x + v * math.sqrt(max(a * a - x * x, 0)))
        E, W = self.hp(H + "ForeArm"), self.hp(H + "Hand")
        self.swing(H + "ForeArm", W - E, T - E)

    def frame(self, mode="full"):
        """Pose Henry on the source's current frame. mode 'copy' = the old fit (rotations + wrist IK only)."""
        h = self.h
        for pb in h.pose.bones: pb.matrix_basis = Matrix.Identity(4)
        h.matrix_world = self.G; self.upd()
        mid_h = (self.hp("RightArm", True) + self.hp("LeftArm", True)) / 2
        # shoulder targets: in 'full', walk back from the source wrist along the source forearm and upper arm with Henry's
        # lengths, so forearm + upper arm keep the source's directions when the arms differ in length (CoD's are ~20%
        # shorter); the shoulders (off screen) land wherever that puts them, the visible elbow and forearm stay right
        shoulder = {}
        for H, m in self.sides:
            if mode == "full":
                W, E, S = self.sp(m["Hand"]), self.sp(m["ForeArm"]), self.sp(m["Arm"])
                E2 = W + (E - W).normalized() * (self.hp(H + "Hand", True) - self.hp(H + "ForeArm", True)).length
                shoulder[H] = E2 + (S - E).normalized() * (self.hp(H + "ForeArm", True) - self.hp(H + "Arm", True)).length
            else:
                shoulder[H] = self.sp(m["Arm"])
        mid_s = sum(shoulder.values(), Vector()) / 2
        h.matrix_world = Matrix.Translation(mid_s - mid_h) @ self.G; self.upd()
        for H, m in self.sides:
            for ch in CHAINS:
                for b, c in zip(ch, ch[1:]): self.copy_rot(H, m, b, c)
            if mode == "full":
                J = self.hp(H + "Shoulder"); self.swing(H + "Shoulder", self.hp(H + "Arm") - J, shoulder[H] - J)
            pole = self.sp(m["ForeArm"]) if mode == "full" else self.hp(H + "ForeArm")
            T = self.sp(m["Hand"])
            for it in range(2 if mode == "full" else 1):
                self.ik(H, m, T, pole)
                self.copy_rot(H, m, "Hand", "HandMiddle1")
                if mode == "full":   # palm centre on the source's palm centre
                    pal_s = (self.sp(m["Hand"]) + self.sp(m["HandMiddle1"])) / 2
                    pal_h = (self.hp(H + "Hand") + self.hp(H + "HandMiddle1")) / 2
                    T = T + (pal_s - pal_h)
            for ch in CHAINS[1:]:
                for b, c in zip(ch, ch[1:]):
                    self.copy_rot(H, m, b, c)
                    if mode == "full": self.aim(H, m, b, c)

    def directions(self):
        """degrees between Henry's and the source's upper arm / forearm, both sides (what shows on screen)."""
        out = {}
        for H, m in self.sides:
            for a, b in (("Arm", "ForeArm"), ("ForeArm", "Hand")):
                out[(H, a + "Dir")] = math.degrees((self.hp(H + b) - self.hp(H + a)).angle(self.sp(m[b]) - self.sp(m[a])))
        return out

    JOINTS = ["Arm", "ForeArm", "Hand", "HandThumb4", "HandIndex1", "HandIndex4", "HandMiddle1", "HandMiddle4",
              "HandRing1", "HandRing4", "HandPinky1", "HandPinky4"]
    def offsets(self):
        """cm between each Henry joint and its source joint, both sides."""
        return {(H, j): (self.hp(H + j) - self.sp(m[j])).length * 100 for H, m in self.sides for j in self.JOINTS}
