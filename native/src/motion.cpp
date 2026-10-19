// Procedural viewmodel motion; see motion.h and docs/weapon_movement_design.md (numbers from Ready or Not:
// docs/ron/ron_movement_table.md, ron_movement_curves.json). Today's look (turn sway, bob, shot kick) is the baseline;
// the RoN layer adds to it. Every term but the shot kick scales with mo.scale and is zero while motion is off.
#include "motion.h"
#include <cmath>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#endif

MotionState mo;

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
// velocity that makes a spring at rest peak at `peak` (underdamped: x(t) = v0/wd e^-zwt sin(wd t))
static float ImpulseFor(float peak, float k, float c) {
	float w = sqrtf(k), z = c / (2 * w);
	if (z >= 0.999f) return peak * w * 2.71828f;                    // critically damped: peak = v0 / (w e)
	float wd = w * sqrtf(1 - z * z), tp = atan2f(wd, z * w) / wd;
	return peak * wd / (expf(-z * w * tp) * sinf(wd * tp));
}
// rotation impulse spring: omega 6, zeta 0.6 (peak ~0.19 s, like RoN's crouch / land curves)
static const float ROT_K = 36.0f, ROT_C = 7.2f;

void ResetMotion() {
	MotionParams keep = mo.p; bool en = mo.enabled, aim = mo.aiming; float sc = mo.scale;
	mo = MotionState{};
	mo.p = keep; mo.enabled = en; mo.aiming = aim; mo.scale = sc;
}

void MotionVel(float x, float y, float z) {
	if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return;
	mo.velW[0] = clampf(x, -15, 15); mo.velW[1] = clampf(y, -15, 15); mo.velW[2] = clampf(z, -30, 30); mo.velAge = 0;
}

static void Takeoff() {
	float k = mo.p.jumpScale;
	mo.hang[2].v += ImpulseFor(-0.035f * k, mo.p.hangK, mo.p.hangC);            // RoN jump_start: z -3.5 cm at ~0.17 s
	mo.rotImp[1].v += ImpulseFor(-5.8f * k, ROT_K, ROT_C);                       // roll -5.8 deg at 0.33 s
	mo.rotImp[2].v += ImpulseFor(-2.2f * k, ROT_K, ROT_C);                       // yaw -2.2 deg
}
static void Land() {
	if (mo.airTime < 0.15f) return;
	float k = clampf(-mo.vzAir / 4.5f, 0.5f, 2.0f) * mo.p.landScale;            // 4.5 m/s ~ a 1 m drop
	mo.rotImp[0].v += ImpulseFor(-6.1f * k, ROT_K, ROT_C);                       // RoN jump_end: pitch -6.1 deg at 0.2 s
	mo.hang[2].v += ImpulseFor(-0.027f * k, mo.p.hangK, mo.p.hangC);            // z -2.7 cm
}
void MotionAir(bool inAir) {
	mo.airFromLua = true;
	if (inAir == mo.air) return;
	mo.air = inAir;
	if (inAir) { mo.airTime = 0; Takeoff(); } else Land();
}
void MotionStance(bool crouched) {
	if (crouched == mo.crouched) return;
	mo.crouched = crouched;
	float k = mo.p.crouchScale;
	if (crouched) {   // RoN fp_m4a1_stand_to_crouch: pitch -2.2, roll -2.0, yaw +1.0 deg; x -1.3, y +1.5 cm
		mo.rotImp[0].v += ImpulseFor(-2.2f * k, ROT_K, ROT_C); mo.rotImp[1].v += ImpulseFor(-2.0f * k, ROT_K, ROT_C); mo.rotImp[2].v += ImpulseFor(1.0f * k, ROT_K, ROT_C);
		mo.hang[0].v += ImpulseFor(-0.013f * k, mo.p.hangK, mo.p.hangC); mo.hang[1].v += ImpulseFor(0.015f * k, mo.p.hangK, mo.p.hangC);
	} else {          // crouch_to_stand: pitch -2.6, roll -3.2, yaw +1.6 deg; z -2.3 cm
		mo.rotImp[0].v += ImpulseFor(-2.6f * k, ROT_K, ROT_C); mo.rotImp[1].v += ImpulseFor(-3.2f * k, ROT_K, ROT_C); mo.rotImp[2].v += ImpulseFor(1.6f * k, ROT_K, ROT_C);
		mo.hang[2].v += ImpulseFor(-0.023f * k, mo.p.hangK, mo.p.hangC);
	}
}
void MotionDodge(float side, float strength) {
	if (!std::isfinite(side) || !std::isfinite(strength)) return;
	float k = clampf(strength, 0.5f, 1.5f) * mo.p.dodgeScale; side = clampf(side, -1, 1);
	if (fabsf(side) > 0.1f) {   // sideways: the gun is left behind, rolls and yaws with the hop
		mo.hang[0].v += ImpulseFor(-0.03f * side * k, mo.p.hangK, mo.p.hangC);   // + the strafe inertia ~1-3 cm
		mo.rotImp[1].v += ImpulseFor(8.0f * side * k, ROT_K, ROT_C);
		mo.rotImp[2].v += ImpulseFor(3.0f * side * k, ROT_K, ROT_C);
	} else {                    // backwards: pushed forward, nose up
		mo.hang[1].v += ImpulseFor(0.03f * k, mo.p.hangK, mo.p.hangC);
		mo.rotImp[0].v += ImpulseFor(3.0f * k, ROT_K, ROT_C);
	}
}

bool MotionParam(const char* key, float v) {
	if (!std::isfinite(v)) return false;
	MotionParams& p = mo.p;
	struct { const char* n; float* f; float lo, hi; } t[] = {
		{ "drag_aim_rot", &p.dragAimRot, 0, 3 }, { "drag_aim_loc", &p.dragAimLoc, 0, 3 },
		{ "drag_strafe_rot", &p.dragStrafeRot, 0, 3 }, { "drag_strafe_loc", &p.dragStrafeLoc, 0, 3 },
		{ "move_scale_x", &p.moveScale[0], 0, 40 }, { "move_scale_y", &p.moveScale[1], 0, 40 }, { "move_scale_z", &p.moveScale[2], 0, 40 },
		{ "move_rot_p", &p.moveRotP, -120, 120 }, { "mesh_interp", &p.meshInterp, 5, 120 },
		{ "hang_k", &p.hangK, 10, 400 }, { "hang_c", &p.hangC, 1, 60 }, { "hang_ads", &p.hangAds, 0, 1 },
		{ "sprint_ron", &p.sprintRon, 0, 1 }, { "sprint", &p.sprintScale, 0, 2 },
		{ "sprint_x", &p.sprintPose[0], -0.2f, 0.2f }, { "sprint_y", &p.sprintPose[1], -0.2f, 0.2f }, { "sprint_z", &p.sprintPose[2], -0.2f, 0.2f },
		{ "sprint_pitch", &p.sprintPose[3], -60, 60 }, { "sprint_roll", &p.sprintPose[4], -60, 60 }, { "sprint_yaw", &p.sprintPose[5], -60, 60 },
		{ "jump", &p.jumpScale, 0, 3 }, { "land", &p.landScale, 0, 3 }, { "crouch", &p.crouchScale, 0, 3 }, { "dodge", &p.dodgeScale, 0, 3 },
		{ "bob", &p.bobScale, 0, 3 }, { "test_roll", &p.testRoll, -45, 45 },
	};
	if (!strcmp(key, "reset")) { p = MotionParams{}; return true; }
	for (auto& e : t) if (!strcmp(key, e.n)) { *e.f = clampf(v, e.lo, e.hi); return true; }
	return false;
}

static bool Finite(const float* f, int n) { for (int i = 0; i < n; i++) if (!std::isfinite(f[i])) return false; return true; }

void MotionStep(const float* cam, float dt) {
	float pos[3] = { cam[3], cam[7], cam[11] }, fwd[3] = { cam[1], cam[5], cam[9] }, right[3] = { cam[0], cam[4], cam[8] };
	if (!mo.have || dt <= 0 || dt > 0.25f) { memcpy(mo.lastPos, pos, sizeof(pos)); memcpy(mo.lastFwd, fwd, sizeof(fwd)); mo.have = true; return; }
	mo.t += dt; mo.velAge += dt;
	float dx = pos[0] - mo.lastPos[0], dy = pos[1] - mo.lastPos[1], dz = pos[2] - mo.lastPos[2];
	float hs = sqrtf(dx * dx + dy * dy) / dt, vz = dz / dt;
	bool cut = hs > 15;                                                          // teleports / cutscene cuts
	// velocity: Lua's world velocity only; stale (no vel for 0.25 s) -> fades to 0 over ~0.2 s
	if (mo.velAge > 0.25) for (int c = 0; c < 3; c++) mo.velW[c] *= expf(-dt / 0.1f);
	// soft deadband: GetVelocity noise while standing (< 0.1 m/s) can't move the gun; full response from 0.2 m/s
	float vw[3] = { mo.velW[0], mo.velW[1], mo.velW[2] };
	{ float m = sqrtf(vw[0] * vw[0] + vw[1] * vw[1] + vw[2] * vw[2]), g = clampf((m - 0.1f) / 0.1f, 0, 1); for (int c = 0; c < 3; c++) vw[c] *= g; }
	if (cut) for (int c = 0; c < 3; c++) mo.velW[c] = mo.velS[c] = 0;
	// camera horizontal axes for the camera-relative velocity (current camera, every sub-step)
	float rl = sqrtf(right[0] * right[0] + right[1] * right[1]), fl = sqrtf(fwd[0] * fwd[0] + fwd[1] * fwd[1]);
	float rH[2] = { rl > 1e-4f ? right[0] / rl : 1, rl > 1e-4f ? right[1] / rl : 0 }, fH[2] = { fl > 1e-4f ? fwd[0] / fl : 0, fl > 1e-4f ? fwd[1] / fl : 1 };
	// turn rates (deg/s) from the forward vector change, low-passed (tau 30 ms): mouse deltas don't follow frame time,
	// so delta / dt jitters with uneven frames (the old "micro jitter")
	float yawNow = atan2f(fwd[1], fwd[0]), yawLast = atan2f(mo.lastFwd[1], mo.lastFwd[0]);
	float dyaw = yawNow - yawLast; if (dyaw > 3.14159f) dyaw -= 6.28318f; if (dyaw < -3.14159f) dyaw += 6.28318f;
	float dpitch = asinf(clampf(fwd[2], -1, 1)) - asinf(clampf(mo.lastFwd[2], -1, 1));
	float yawRate = cut ? 0 : dyaw / dt * 57.3f, pitchRate = cut ? 0 : dpitch / dt * 57.3f;
	float fa = 1 - expf(-dt / 0.03f);
	mo.yawF += (yawRate - mo.yawF) * fa; mo.pitchF += (pitchRate - mo.pitchF) * fa;
	yawRate = mo.yawF; pitchRate = mo.pitchF;
	memcpy(mo.lastPos, pos, sizeof(pos)); memcpy(mo.lastFwd, fwd, sizeof(fwd));
	// air from the camera only when Lua never said (falling fast for a while)
	if (!mo.airFromLua) {
		bool falling = vz < -2.5f;
		if (falling != mo.air) { if (falling) { mo.air = true; mo.airTime = 0; } else { Land(); mo.air = false; } }
	}
	if (mo.air) { mo.airTime += dt; mo.vzAir = mo.airFromLua ? mo.velS[2] : vz; }
	// sprint with hysteresis (in above 4.6 m/s, out below 3.8), never while aiming or crouched
	float speedNow = sqrtf(mo.velS[0] * mo.velS[0] + mo.velS[1] * mo.velS[1]);
	if (!mo.sprinting && speedNow > 4.6f && !mo.aiming && !mo.crouched) mo.sprinting = true;
	else if (mo.sprinting && (speedNow < 3.8f || mo.aiming || mo.crouched)) mo.sprinting = false;
	float vr = 0, vf = 0, vu = 0;

	const MotionParams& P = mo.p;
	const float wMesh = P.meshInterp / 5.0f, kMesh = wMesh * wMesh, cMesh = 2 * 0.8f * wMesh;
	// sub-step so the look and the springs match at 20-240 fps (implicit springs are stable anyway)
	int n = (int)ceilf(dt / (1.0f / 240.0f)); if (n < 1) n = 1; if (n > 16) n = 16;
	float h = dt / n;
	const float va = 1 - expf(-h / 0.06f);
	for (int i = 0; i < n; i++) {
		for (int c = 0; c < 3; c++) mo.velS[c] += (vw[c] - mo.velS[c]) * va;
		vr = mo.velS[0] * rH[0] + mo.velS[1] * rH[1]; vf = mo.velS[0] * fH[0] + mo.velS[1] * fH[1]; vu = mo.velS[2];
		mo.speed += (sqrtf(vr * vr + vf * vf) - mo.speed) * (1 - expf(-h / 0.15f));
		mo.aimK.step(mo.aiming ? 1.0f : 0.0f, 200, 28, h);
		// look sway (baseline: the gun lags behind turns), gain from RoN's drag (0.6 -> today's 0.012 / 0.010)
		float g = P.dragAimRot / 0.6f;
		mo.yaw.step(clampf(-yawRate * 0.012f * g, -2.5f, 2.5f), 120, 18, h);
		mo.pitch.step(clampf(-pitchRate * 0.010f * g, -2.0f, 2.0f), 120, 18, h);
		mo.swayX.step(clampf(-yawRate * 0.00004f * P.dragAimLoc, -0.012f, 0.012f), kMesh, cMesh, h);   // new: the gun shifts against the turn
		mo.swayZ.step(clampf(-pitchRate * 0.00004f * P.dragAimLoc, -0.01f, 0.01f), kMesh, cMesh, h);
		// strafe / move inertia (new), through the hang spring (offsets) and a mesh-interp spring (strafe roll / yaw)
		float tx = -vr * 0.004f * P.dragStrafeLoc - vr / 3.2f * P.moveScale[0] / 100 * 0.3f;
		float ty = -vf * 0.004f * P.dragStrafeLoc - vf / 3.2f * P.moveScale[1] / 100 * 0.3f;
		float tz = -vu / 3.2f * P.moveScale[2] / 100 * 0.3f;
		tx = clampf(tx, -0.03f, 0.03f); ty = clampf(ty, -0.02f, 0.02f); tz = clampf(tz, -0.02f, 0.02f);
		mo.hang[0].step(tx, P.hangK, P.hangC, h); mo.hang[1].step(ty, P.hangK, P.hangC, h); mo.hang[2].step(tz, P.hangK, P.hangC, h);
		mo.strafeRoll.step(clampf(-vr * 1.5f * P.dragStrafeRot, -6, 6), kMesh, cMesh, h);
		mo.strafeYaw.step(clampf(-vr * 0.5f * P.dragStrafeRot, -3, 3), kMesh, cMesh, h);
		mo.moveP.step(clampf(-vf / 3.2f * P.moveRotP * 0.05f, -4, 4), kMesh, cMesh, h);   // pitch with forward speed, through the mesh-interp spring
		for (int c = 0; c < 3; c++) mo.rotImp[c].step(0, ROT_K, ROT_C, h);
		// sprint blend: in omega 7 zeta 0.7 (~0.3 s), out omega 6 zeta 0.75 (~0.6 s), like RoN's start / stop clips
		if (mo.sprinting) mo.sprint.step(1, 49, 9.8f, h); else mo.sprint.step(0, 36, 9.0f, h);
		// air pose (omega 5, zeta 0.8)
		mo.airK.step(mo.air && mo.airTime > 0.1f ? 1.0f : 0.0f, 25, 8, h);
		mo.kickBack.step(0, 784, 50, h); mo.kickPitch.step(0, 784, 50, h); mo.kickYaw.step(0, 784, 50, h); mo.kickRoll.step(0, 784, 50, h);
	}
	// bob: phase advances with distance walked (one step ~0.75 m); the sprint stride (0.64 s) with distance too
	mo.phase += mo.speed * dt / 0.75f * 3.14159f;
	mo.stride += dt / 0.64f * 6.28318f * clampf(mo.speed / 5.0f, 0, 1.2f);
	if (mo.phase > 1e4f) mo.phase -= 6283.18f;
	if (mo.stride > 1e4f) mo.stride -= 6283.18f;
	float a = fminf(1.0f, mo.speed / 5.0f) * P.bobScale;
	float hip = 1.0f - clampf(mo.aimK.x, 0, 1) * 0.85f;                          // most motion fades while aiming
	float hangShare = 1.0f - clampf(mo.aimK.x, 0, 1) * (1 - P.hangAds);
	float bobX = sinf(mo.phase) * 0.006f * a, bobZ = -fabsf(cosf(mo.phase)) * 0.005f * a;
	float bobP = sinf(2 * mo.phase) * 0.6f * a, bobY = sinf(mo.phase) * 0.9f * a;   // new: RoN jog-loop rotations
	// sprint pose: RoN's pistol loop mean + stride wobble, or the old pose
	float s = clampf(mo.sprint.x, -0.2f, 1.2f) * P.sprintScale, r = P.sprintRon;
	const float oldPose[6] = { 0.015f, -0.01f, -0.03f, -14.0f, 12.0f, 6.0f };
	float sp[6];
	for (int c = 0; c < 6; c++) sp[c] = s * (r * P.sprintPose[c] + (1 - r) * oldPose[c]);
	float wob = s * r * clampf(mo.speed / 5.0f, 0, 1);
	float st = mo.stride;
	sp[0] += wob * 0.042f * sinf(st); sp[2] += wob * 0.012f * sinf(2 * st);
	sp[3] += wob * 9.0f * sinf(2 * st + 0.3f); sp[4] += wob * 5.0f * sinf(2 * st); sp[5] += wob * 6.0f * sinf(st + 0.5f);
	// air pose (RoN jump_loop): z +1.9 cm, roll +4.9 + 2.5 sin(1.3 t), yaw +1.6, pitch -0.6
	float ak = clampf(mo.airK.x, 0, 1.2f) * P.jumpScale;
	float airOff = ak * 0.019f, airRot[3] = { -0.6f * ak, ak * (4.9f + 2.5f * sinf(1.3f * mo.airTime)), 1.6f * ak };
	float k = mo.enabled ? mo.scale : 0.0f;
	mo.off[0] = k * (bobX * hip + sp[0] + (mo.swayX.x * hip + mo.hang[0].x * hangShare));
	mo.off[1] = k * (sp[1] + mo.hang[1].x * hangShare) - mo.kickBack.x;
	mo.off[2] = k * (bobZ * hip + sp[2] + airOff + mo.swayZ.x * hip + mo.hang[2].x * hangShare);
	mo.rot[0] = k * (mo.pitch.x * hip + sp[3] + airRot[0] + mo.rotImp[0].x + bobP * hip + mo.moveP.x * hip) + mo.kickPitch.x;
	mo.rot[1] = k * (mo.yaw.x * 0.5f * hip + sp[4] + airRot[1] + mo.rotImp[1].x + sinf(mo.phase) * 0.6f * a * hip + mo.strafeRoll.x * hip + P.testRoll) + mo.kickRoll.x;
	mo.rot[2] = k * (mo.yaw.x * hip + sp[5] + airRot[2] + mo.rotImp[2].x + bobY * hip + mo.strafeYaw.x * hip) + mo.kickYaw.x;
	if (!Finite(mo.off, 3) || !Finite(mo.rot, 3) || !std::isfinite(mo.speed)) ResetMotion();
}

#ifdef _WIN32
void UpdateMotion(const float* cam) {
	static LARGE_INTEGER lastT{}; static double freq = 0;
	LARGE_INTEGER now; QueryPerformanceCounter(&now);
	if (!freq) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); freq = (double)f.QuadPart; }
	float dt = lastT.QuadPart ? (float)((now.QuadPart - lastT.QuadPart) / freq) : 0.016f;
	lastT = now;
	MotionStep(cam, dt);
}
#endif
