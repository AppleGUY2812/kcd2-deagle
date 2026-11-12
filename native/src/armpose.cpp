// Henry's own arms in the gun's arm pose (the user's choice 2026-11-12: his real arms, so sleeves, cape, cloth
// simulation, dirt and blood are the game's own). The viewmodel keeps playing the fitted clips and is the pose source;
// its own arm skins are hidden (gore.cpp). Engine facts: docs/ARMSWAP.md "Posing Henry's real arms" (WHGame 1.5.6,
// read in the binary; notes on the ProLiant ~/re/work/pose_arms/NOTES.md).
//
// Two steps a frame, from one pose:
//   after the camera (ArmPose_AfterCamera, main thread, from the camera hook once the viewmodel is placed): each of
//     his arm joints is written into his pose (CCharInstance +0x8e0) at the viewmodel joint's place in the world.
//     The skinning job of this frame reads that array, so skin and sleeves are exact against this frame's camera.
//   in his pose job (mode 2; hook on CSkeletonAnim::PoseModifiersPrepare): the same joints, as they stood in his
//     model space last frame, are queued as an engine OperatorQueue on post queue 0. That runs before the cloth
//     collision proxies and the cloth step of the same job, so capsules, cloth pins and hand attachments follow the
//     gun arms. It is one frame old against the camera (the camera is computed after the job).
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include "armpose.h"
#include "armpose_core.h"
#include "gore.h"
#include "log.h"
#include "MinHook.h"

using namespace armpose;

namespace {

// ---- WHGame 1.5.6
constexpr uintptr_t kRvaVtCharInst = 0x40602f8;      // CCharInstance
constexpr uintptr_t kRvaPosePrep = 0x83ec18;         // CSkeletonAnim::PoseModifiersPrepare(this, const QuatTS&)
constexpr uintptr_t kRvaQueueCreate = 0x5a9ed4;      // bool (unused, std::shared_ptr<IAnimationOperatorQueue>* out)
constexpr uintptr_t kRvaQueuePush = 0x83f0b0;        // CSkeletonAnim vt+0x128: (this, queue 0-3, shared_ptr* (released), name)
constexpr uintptr_t kRvaVtOpQueue = 0x3a3ed98;       // COperatorQueue
const unsigned char kSigPosePrep[] = { 0x48, 0x89, 0x5c, 0x24, 0x18, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8d, 0x6c, 0x24 };
const unsigned char kSigQueueCreate[] = { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x40, 0x48, 0xc7, 0x02, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8b, 0xfa };
const unsigned char kSigQueuePush[] = { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x30, 0x48, 0x8b, 0x81, 0xe8, 0x04 };
// CCharInstance: +0x110 CSkeletonAnim (+0x4e0 CSkeletonPose*, +0x4e8 CCharInstance*), +0x698 CSkeletonPose (+0x318
// the job's pose while one is prepared, +0x360 flag 2 and +0x11c flag 0x20: the engine's own gate for its late
// "BlenderAim" push on queue 0, which this file's push copies: Prepare by hand, then push), +0x8e0 CPoseData, +0xa70 CDefaultSkeleton* (+8 joints, 0x30 each: +0
// name, +0xc crc32 of the lower-case name, +0x16 parent). CPoseData: +0x08 offset of the absolute array, +0x20
// buffer (relative array at its 4-aligned start), +0x60 joint count.
constexpr int kOffSkelAnim = 0x110, kOffPoseData = 0x8e0, kOffSkel = 0xa70, kJointSize = 0x30;
const uint32_t kPlayer = 0x7777;
// Where the arms start, per source skeleton (KCD2's male body): these joints and everything below them.
const char* const kRoots[] = { "LeftShoulder", "RightShoulder" };
constexpr int kMaxCopy = 192;

using GetEntityFn = uintptr_t(__fastcall*)(uintptr_t es, uint32_t id);   // IEntitySystem +0x70
using GetCharacterFn = uintptr_t(__fastcall*)(uintptr_t ent, int slot);  // IEntity +0x328
using GetWorldTMFn = const float* (__fastcall*)(uintptr_t ent);          // IEntity +0x100
using PosePrepFn = void(__fastcall*)(uintptr_t skelAnim, uintptr_t loc);
using QueueCreateFn = bool(__fastcall*)(uintptr_t unused, uintptr_t* out2);
using QueuePushFn = unsigned char(__fastcall*)(uintptr_t skelAnim, int queue, uintptr_t* sp2, const char* name);
using VoidFn = void(__fastcall*)(uintptr_t self);
using BoolFn = unsigned char(__fastcall*)(uintptr_t self);
using PushPosFn = void(__fastcall*)(uintptr_t self, uint32_t joint, uint16_t op, const float* v3);
using PushRotFn = void(__fastcall*)(uintptr_t self, uint32_t joint, uint16_t op, const float* q4);

struct Side { uintptr_t inst = 0, skel = 0; uint32_t serial = 0; int n = 0; };   // serial: skel+0x178, one per skeleton ever built
struct Key {
	uintptr_t bi = 0, bs = 0, vi = 0, vs = 0; uint32_t bser = 0, vser = 0; int bn = 0, vn = 0;
	bool operator==(const Key& o) const { return bi == o.bi && bs == o.bs && vi == o.vi && vs == o.vs && bser == o.bser && vser == o.vser && bn == o.bn && vn == o.vn; }
};
struct Arrays { QT* rel = nullptr; QT* abs = nullptr; int n = 0; };

uintptr_t g_wh = 0;
int g_sig = -1;                                   // -1 unchecked, 0 another build, 1 WHGame 1.5.6
std::atomic<int> g_mode{ 0 };                     // 0 off, 1 skin after the camera, 2 + in the pose job
Plan g_plan;
Side g_body, g_vm;
bool g_planOk = false;
Key g_key, g_noKey;                               // the pair the plan is for; the pair last refused
uint64_t g_noFrame = 0;                           // a refused pair is tried again after 30 drawn frames
int g_refused = 0;                                // why the last plan was refused, said once per change
PosePrepFn g_origPosePrep = nullptr;
bool g_hooked = false;

// the pose for the job: his copied joints in his model space, as of the last camera step
std::mutex g_mu;
int g_stageN = 0;
uint32_t g_stageJoint[kMaxCopy];
QT g_stagePose[kMaxCopy];
uintptr_t g_stageSkel = 0;
uint32_t g_stageSerial = 0;
uint64_t g_stageFrame = 0;
std::atomic<uintptr_t> g_stageSkelAnim{ 0 };
std::atomic<uint64_t> g_frame{ 0 };
uintptr_t g_queue[2] = { 0, 0 };                  // std::shared_ptr<IAnimationOperatorQueue>: object, control block

struct Stats { uint64_t frames = 0, pushes = 0, pushFail = 0, noJob = 0, stale = 0, busy = 0; double us = 0, usMax = 0; } g_st;

bool Rd(uintptr_t addr, uintptr_t& out) {
	__try { out = *(uintptr_t*)addr; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { out = 0; return false; }
}
uintptr_t Vf(uintptr_t obj, int off) { uintptr_t vt; return Rd(obj, vt) && Rd(vt + off, vt) ? vt : 0; }

bool SigOk() {
	if (g_sig < 0) {
		g_wh = (uintptr_t)GetModuleHandleA("WHGame.dll");
		__try {
			g_sig = g_wh && !memcmp((void*)(g_wh + kRvaPosePrep), kSigPosePrep, sizeof(kSigPosePrep)) &&
			        !memcmp((void*)(g_wh + kRvaQueueCreate), kSigQueueCreate, sizeof(kSigQueueCreate)) &&
			        !memcmp((void*)(g_wh + kRvaQueuePush), kSigQueuePush, sizeof(kSigQueuePush)) ? 1 : 0;
		} __except (EXCEPTION_EXECUTE_HANDLER) { g_sig = 0; }
	}
	return g_sig == 1;
}

// entity -> its character instance (slot 0) and world matrix. The instance must be a CCharInstance.
bool Instance(uintptr_t env, uint32_t id, uintptr_t& inst, float* tm12) {
	__try {
		uintptr_t es = 0; if (!Rd(env + 0xA0, es) || !es) return false;
		auto getEnt = (GetEntityFn)Vf(es, 0x70); uintptr_t ent = getEnt ? getEnt(es, id) : 0; if (!ent) return false;
		auto getChr = (GetCharacterFn)Vf(ent, 0x328); inst = getChr ? getChr(ent, 0) : 0; if (!inst) return false;
		if (*(uintptr_t*)inst != g_wh + kRvaVtCharInst) return false;
		auto getTM = (GetWorldTMFn)Vf(ent, 0x100); const float* m = getTM ? getTM(ent) : nullptr; if (!m) return false;
		memcpy(tm12, m, 12 * sizeof(float));
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool PoseArrays(uintptr_t inst, Arrays& a) {
	__try {
		const uintptr_t p = inst + kOffPoseData, buf = *(uintptr_t*)(p + 0x20);
		const uintptr_t rel = (buf + 3) & ~(uintptr_t)3, off = *(uintptr_t*)(p + 0x08);
		const int n = *(int*)(p + 0x60);
		if (!buf || n <= 0 || n > kMaxJoints || off < (uintptr_t)n * sizeof(QT) || off > (1u << 20)) return false;
		a.rel = (QT*)rel; a.abs = (QT*)(rel + off); a.n = n;
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// The skeleton's joint names and parents, each name checked against the crc32 the game keeps beside it (so a wrong
// layout is refused, not used). Returns the joint count, 0 when anything is off.
int Joints(uintptr_t skel, const char** names, int16_t* parent, int cap) {
	__try {
		// the count sits before the array (what CDefaultSkeleton vt+0x08 returns; vt+0xa8 is the skeleton's serial)
		const uintptr_t arr = *(uintptr_t*)(skel + 8);
		const int n = arr ? *(int*)(arr - 4) : 0;
		if (n <= 0 || n > cap) return 0;
		for (int i = 0; i < n; i++) {
			const uintptr_t e = arr + (uintptr_t)i * kJointSize;
			const char* nm = *(const char**)e;
			if (!nm || strnlen(nm, 128) >= 128 || Crc32Lower(nm) != *(uint32_t*)(e + 0xc)) return 0;
			names[i] = nm;
			parent[i] = *(int16_t*)(e + 0x16);
		}
		return n;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

uint32_t Serial(uintptr_t skel) {
	__try { return *(uint32_t*)(skel + 0x178); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// absolute = parent's absolute * relative for the picked joints (pick[i] >= 0): the pose arrays are what this file
// takes them for. Only the arm joints: cloth joints elsewhere on Henry need not agree.
float Coherence(const Arrays& a, const int16_t* parent, const int16_t* pick, int n) {
	__try {
		float worst = 0;
		for (int i = 0; i < n; i++) {
			if (parent[i] < 0 || pick[i] < 0) continue;
			const QT m = Mul(a.abs[parent[i]], a.rel[i]);
			const float d = fabsf(m.tx - a.abs[i].tx) + fabsf(m.ty - a.abs[i].ty) + fabsf(m.tz - a.abs[i].tz);
			if (!(d <= worst)) worst = d;      // a NaN counts
		}
		return worst;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return 1e9f; }
}

void PlanOk(bool ok) {   // gore.cpp hides the viewmodel's arm skins only while the pose is really written
	if (ok != g_planOk) { g_planOk = ok; ClothKick(); }
}

bool Refuse(int why, const char* text) {
	PlanOk(false);
	if (g_refused != why) { g_refused = why; Log("armpose: not applied (the gun's own arms show): %s", text); }
	return false;
}

// model space = the entity's world matrix is assumed for both characters; a slot offset would shift the arms. Said once
// per plan: how far slot 0's world matrix (IEntity vt+0x2D8) is from the entity's.
float SlotGap(uintptr_t env, uint32_t id) {
	__try {
		uintptr_t es = *(uintptr_t*)(env + 0xA0);
		uintptr_t ent = ((GetEntityFn)Vf(es, 0x70))(es, id);
		auto slotTM = (const float* (__fastcall*)(uintptr_t, int))Vf(ent, 0x2D8);
		auto getTM = (GetWorldTMFn)Vf(ent, 0x100);
		if (!ent || !slotTM || !getTM) return -1;
		const float* a = slotTM(ent, 0); const float* b = getTM(ent);
		if (!a || !b) return -1;
		float d = 0;
		for (int i = 0; i < 12; i++) d += fabsf(a[i] - b[i]);
		return d;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// (Re)builds the plan when either character or skeleton changed. Henry's skeleton is replaced on every change of
// clothes (the game extends it with the clothes' joints), so his joint numbers move.
bool Plan_(uintptr_t env, uint32_t vmId, uintptr_t bodyInst, uintptr_t vmInst) {
	uintptr_t bs = 0, vs = 0;
	if (!Rd(bodyInst + kOffSkel, bs) || !Rd(vmInst + kOffSkel, vs) || !bs || !vs) return Refuse(1, "a character without a skeleton");
	Arrays ba, va;
	if (!PoseArrays(bodyInst, ba) || !PoseArrays(vmInst, va)) return Refuse(2, "pose data not readable");
	const Key key{ bodyInst, bs, vmInst, vs, Serial(bs), Serial(vs), ba.n, va.n };
	if (g_planOk && key == g_key) return true;
	const uint64_t frame = g_frame.load();
	if (!g_planOk && key == g_noKey && frame - g_noFrame < 30) return false;   // refused a moment ago: not every call
	auto no = [&](int why, const char* text) { g_noKey = key; g_noFrame = frame; return Refuse(why, text); };
	static const char* bn[kMaxJoints]; static const char* vn[kMaxJoints];
	static int16_t bp[kMaxJoints], vp[kMaxJoints], used[kMaxJoints];
	const int nb = Joints(bs, bn, bp, kMaxJoints), nv = Joints(vs, vn, vp, kMaxJoints);
	if (!nb || !nv) return no(3, "joint table not as expected (names do not match their ids)");
	if (nb != ba.n || nv != va.n) return no(4, "pose and skeleton disagree on the joint count (a skeleton swap in progress)");
	if (!Build(g_plan, bn, bp, nb, vn, nv, kRoots, 2)) return no(6, "the viewmodel shares no arm joint with the body (not a Henry-arms gun)");
	if (g_plan.copied > kMaxCopy) return no(7, "more arm joints than this file holds");
	for (int i = 0; i < nv; i++) used[i] = -1;
	for (int i = 0; i < nb; i++) if (g_plan.src[i] >= 0) used[g_plan.src[i]] = 0;
	const float cb = Coherence(ba, bp, g_plan.src, nb), cv = Coherence(va, vp, used, nv);
	if (!(cb < 2e-3f) || !(cv < 2e-3f)) { char t[160]; snprintf(t, sizeof(t), "pose arrays not as expected (arm joints: absolute vs parent * relative off by %.4f m body, %.4f m viewmodel)", cb, cv); return no(5, t); }
	g_body = { bodyInst, bs, key.bser, nb }; g_vm = { vmInst, vs, key.vser, nv };
	g_key = key; g_refused = 0;
	PlanOk(true);
	Log("armpose: plan: %d of Henry's %d joints copy the viewmodel's (%d joints), %d below them follow their parent; slot 0 vs entity matrix differs by %.4f (Henry) %.4f (viewmodel), thread %lu",
		g_plan.copied, nb, nv, g_plan.follow, SlotGap(env, kPlayer), SlotGap(env, vmId), GetCurrentThreadId());
	return true;
}

// The write itself. out / joint: the copied joints in his model space, for the job. Returns how many, -1 on a fault.
int Write(uintptr_t bodyInst, uintptr_t vmInst, const QT& vmToBody, uint32_t* joint, QT* out) {
	__try {
		Arrays ba, va;
		if (!PoseArrays(bodyInst, ba) || !PoseArrays(vmInst, va) || ba.n != g_plan.n) return -1;
		for (int i = 0; i < g_plan.n; i++) if (g_plan.src[i] >= 0 && (g_plan.src[i] >= va.n || !Finite(va.abs[g_plan.src[i]]))) return -1;
		Apply(g_plan, ba.abs, ba.rel, va.abs, vmToBody);
		int k = 0;
		for (int i = 0; i < g_plan.n && k < kMaxCopy; i++) if (g_plan.src[i] >= 0) { joint[k] = (uint32_t)i; out[k++] = ba.abs[i]; }
		return k;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// ---- in the pose job
bool QueueMake() {
	__try {
		uintptr_t sp[2] = { 0, 0 };
		if (!((QueueCreateFn)(g_wh + kRvaQueueCreate))(0, sp) || !sp[0] || !sp[1]) return false;
		if (*(uintptr_t*)sp[0] != g_wh + kRvaVtOpQueue) return false;   // kept (one leaked object) rather than released blind
		g_queue[0] = sp[0]; g_queue[1] = sp[1];
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// 1 pushed, 0 the engine refused it (queue full), -1 fault, -2 no job this frame, -3 the engine still holds the queue
int QueueFill(uintptr_t skelAnim, const uint32_t* joint, const QT* pose, int n) {
	__try {
		const uintptr_t sp = *(uintptr_t*)(skelAnim + 0x4e0);
		// as the engine gates its own late push ("BlenderAim"): a full update, not ragdoll-driven; and a job pose exists
		if (!sp || !*(uintptr_t*)(sp + 0x318) || !(*(unsigned char*)(sp + 0x360) & 2) || (*(unsigned char*)(sp + 0x11c) & 0x20)) return -2;
		// last frame's queue entry must be gone (the engine drops it when it synchronises): we alone hold the object
		if (*(volatile long*)(g_queue[1] + 8) != 1) return -3;
		const uintptr_t q = g_queue[0], vt = *(uintptr_t*)q;
		((VoidFn)*(uintptr_t*)(vt + 0x70))(q);                           // Clear
		for (int k = 0; k < n; k++) {                                    // parents first: joints are in index order
			((PushPosFn)*(uintptr_t*)(vt + 0x40))(q, joint[k], 0, &pose[k].tx);   // op 0: override, model space
			((PushRotFn)*(uintptr_t*)(vt + 0x48))(q, joint[k], 0, &pose[k].x);
		}
		((VoidFn)*(uintptr_t*)(vt + 0x68))(q);                           // ComputeAbsolute: joints below the last one
		((BoolFn)*(uintptr_t*)(vt + 0x20))(q);                           // Prepare: the engine's pass has already run
		_InterlockedIncrement((volatile long*)(g_queue[1] + 8));         // the push releases the pointer it is given
		uintptr_t copy[2] = { g_queue[0], g_queue[1] };
		return ((QueuePushFn)(g_wh + kRvaQueuePush))(skelAnim, 0, copy, "DeagleArms") ? 1 : 0;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

void InJob(uintptr_t skelAnim) {
	static uint32_t joint[kMaxCopy];
	static QT pose[kMaxCopy];
	int n = 0;
	uintptr_t skel = 0, inst = 0, now = 0;
	uint32_t serial = 0;
	{ static bool said = false; if (!said) { said = true; Log("armpose: Henry's pose prepare runs on thread %lu", GetCurrentThreadId()); } }
	{
		std::lock_guard<std::mutex> lk(g_mu);
		if (g_frame.load() != g_stageFrame) { g_st.stale++; return; }   // no pose from the last drawn frame: the gun is away
		n = g_stageN; skel = g_stageSkel; serial = g_stageSerial;
		memcpy(joint, g_stageJoint, n * sizeof(uint32_t)); memcpy(pose, g_stagePose, n * sizeof(QT));
	}
	if (n <= 0 || !Rd(skelAnim + 0x4e8, inst) || !inst || !Rd(inst + kOffSkel, now) || now != skel || Serial(now) != serial) { g_st.stale++; return; }
	if (!g_queue[0] && !QueueMake()) { g_mode = 1; Log("armpose: the engine gave no operator queue: back to mode 1 (skin only)"); return; }
	const int r = QueueFill(skelAnim, joint, pose, n);
	if (r == 1) g_st.pushes++; else if (r == 0) g_st.pushFail++; else if (r == -2) g_st.noJob++;
	else if (r == -3) { if (!g_st.busy++) Log("armpose: the engine still held last frame's queue: this frame not queued (counted as busy)"); }
	else { g_mode = 1; Log("armpose: fault while queueing the pose: back to mode 1 (skin only)"); }
}

void __fastcall HkPosePrep(uintptr_t skelAnim, uintptr_t loc) {
	g_origPosePrep(skelAnim, loc);
	if (g_mode.load(std::memory_order_relaxed) >= 2 && skelAnim == g_stageSkelAnim.load(std::memory_order_relaxed)) InJob(skelAnim);
}

}  // namespace

void ArmPose_Install() {
	if (!SigOk()) { Log("armpose: not WHGame 1.5.6 (signature mismatch): off"); return; }
	void* t = (void*)(g_wh + kRvaPosePrep);
	g_hooked = MH_CreateHook(t, (void*)&HkPosePrep, (void**)&g_origPosePrep) == MH_OK && MH_EnableHook(t) == MH_OK;
	Log("armpose: %s", g_hooked ? "ready (off until \"armpose 1|2\")" : "pose hook FAILED: mode 2 not available");
}

bool ArmPose_On() { return g_mode.load(std::memory_order_relaxed) > 0; }
bool ArmPose_Active() { return g_mode.load(std::memory_order_relaxed) > 0 && g_planOk; }

void ArmPose_AfterCamera(uintptr_t env, uint32_t vmId, bool newFrame) {
	const uint64_t frame = newFrame ? ++g_frame : g_frame.load();
	const int mode = g_mode.load(std::memory_order_relaxed);
	if (!mode || !env || !vmId || !SigOk()) return;
	LARGE_INTEGER t0, t1, fq; QueryPerformanceCounter(&t0);
	uintptr_t body = 0, vm = 0;
	float btm[12], vtm[12];
	if (!Instance(env, kPlayer, body, btm) || !Instance(env, vmId, vm, vtm)) { Refuse(8, "no character on Henry or on the viewmodel"); return; }
	QT bw, vw;
	if (!FromRows(btm, bw) || !FromRows(vtm, vw)) { Refuse(9, "an entity is scaled or mirrored"); return; }
	if (!Plan_(env, vmId, body, vm)) return;
	static uint32_t joint[kMaxCopy];
	static QT pose[kMaxCopy];
	const int n = Write(body, vm, Between(bw, vw), joint, pose);
	if (n < 0) { Refuse(10, "fault or bad value while writing the pose"); return; }
	if (mode >= 2) {
		std::lock_guard<std::mutex> lk(g_mu);
		g_stageN = n; g_stageSkel = g_body.skel; g_stageSerial = g_body.serial; g_stageFrame = frame;
		memcpy(g_stageJoint, joint, n * sizeof(uint32_t)); memcpy(g_stagePose, pose, n * sizeof(QT));
		g_stageSkelAnim = body + kOffSkelAnim;
	}
	QueryPerformanceCounter(&t1); QueryPerformanceFrequency(&fq);
	const double us = (t1.QuadPart - t0.QuadPart) * 1e6 / (double)fq.QuadPart;
	g_st.us += us; if (us > g_st.usMax) g_st.usMax = us;   // every call, the same frame's repeats too
	if (!newFrame) return;
	if (++g_st.frames % 3000 == 0) {
		Log("armpose %d: %.1f us a frame, %.0f max call, over 3000 frames (main thread only: mode 2's work in the pose job is not in it); job: %llu queued, %llu refused, %llu frames without a job, %llu stale, %llu busy",
			mode, g_st.us / 3000, g_st.usMax, g_st.pushes, g_st.pushFail, g_st.noJob, g_st.stale, g_st.busy);
		g_st.us = g_st.usMax = 0;
	}
}

bool ArmPose_Command(const char* cmd, const char* rest) {
	if (strcmp(cmd, "armpose")) return false;
	if (!SigOk()) { Log("armpose: not WHGame 1.5.6 (signature mismatch), refusing"); return true; }
	if (!*rest || !_strnicmp(rest, "status", 6)) {
		Log("armpose %d: plan %s (%d copied, %d follow; Henry %d joints, viewmodel %d); %llu frames written; job: %llu queued, %llu refused, %llu without a job, %llu stale, %llu busy; hook %s",
			g_mode.load(), g_planOk ? "ok" : "none", g_plan.copied, g_plan.follow, g_body.n, g_vm.n, g_st.frames, g_st.pushes, g_st.pushFail, g_st.noJob, g_st.stale, g_st.busy, g_hooked ? "in" : "missing");
		return true;
	}
	int m = atoi(rest);
	if (m < 0) m = 0;
	if (m > 2) m = 2;
	if (m == 2 && !g_hooked) { m = 1; Log("armpose: the pose hook is missing: mode 1 (skin only)"); }
	if (!m) { g_stageSkelAnim = 0; PlanOk(false); }   // 1 <-> 2 keeps the plan: no frame with both pairs of arms
	g_refused = 0; g_noKey = Key{};
	g_mode = m;
	ClothKick();   // gore.cpp: the viewmodel's own arm skins go (or come back) on the next frame
	Log("armpose %d (%s)", m, m == 0 ? "off: Henry's arms are his own animation's again" : m == 1 ? "his arm joints follow the viewmodel's, written after the camera" :
		"as 1, and queued in his pose job so cloth and capsules follow");
	return true;
}
