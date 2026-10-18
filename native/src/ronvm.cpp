// Ready or Not's own first-person recoil, 1:1 from its code: see ronvm.h and docs/ron/ron_recoil_code.md (the section
// numbers below are that file's).
#include "ronvm.h"
#include <cmath>
#include <cstring>

RonVm rvm;

namespace {
float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
const float kDeg = 57.29578f;

// UE's FMath::VInterpTo / RInterpTo (component-wise; the rotator delta normalised to -180..180)
void VInterpTo(float* cur, const float* target, float dt, float speed) {
	if (speed <= 0) { memcpy(cur, target, 3 * sizeof(float)); return; }
	float d[3] = { target[0] - cur[0], target[1] - cur[1], target[2] - cur[2] };
	if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] < 1e-4f) { memcpy(cur, target, 3 * sizeof(float)); return; }
	float a = clampf(dt * speed, 0, 1);
	for (int i = 0; i < 3; i++) cur[i] += d[i] * a;
}
float NormDeg(float a) { a = fmodf(a + 180.0f, 360.0f); if (a < 0) a += 360.0f; return a - 180.0f; }
void RInterpTo(float* cur, const float* target, float dt, float speed) {
	if (dt == 0) return;
	if (speed <= 0) { memcpy(cur, target, 3 * sizeof(float)); return; }
	float d[3] = { NormDeg(target[0] - cur[0]), NormDeg(target[1] - cur[1]), NormDeg(target[2] - cur[2]) };
	if (fabsf(d[0]) < 1e-4f && fabsf(d[1]) < 1e-4f && fabsf(d[2]) < 1e-4f) { memcpy(cur, target, 3 * sizeof(float)); return; }
	float a = clampf(dt * speed, 0, 1);
	for (int i = 0; i < 3; i++) cur[i] = NormDeg(cur[i] + d[i] * a);
}

// 6.2 / 6.3: where the gun offset is heading (camera forward, right, up cm; FRotator deg)
void MeshTarget(bool aiming, float* t, float* r) {
	const RonParams& P = rvm.p;
	float pp = rvm.pend[0], py = rvm.pend[1];
	if (!aiming) {
		for (int i = 0; i < 3; i++) { t[i] = P.hipT[i]; r[i] = P.hipR[i]; }
		t[0] -= P.move1P * clampf(P.kickMove[0] * (pp + py), -P.kickMax[0], P.kickMax[0]);
		t[1] += P.move1P * clampf(P.kickMove[1] * py, -P.kickMax[1], P.kickMax[1]);
		t[2] += P.move1P * clampf(P.kickMove[2] * pp, -P.kickMax[2], P.kickMax[2]);
	} else {
		for (int i = 0; i < 3; i++) { t[i] = P.adsT[i]; r[i] = P.adsR[i]; }
		t[0] -= clampf(P.adsBack * (pp + py), 0, P.adsBackMax);
		t[1] += py;
	}
	// its clamps, in the arms' component space (X = left, forward, up): ours right, forward, up
	t[0] = clampf(t[0], P.lo[1], P.hi[1]);
	t[1] = clampf(t[1], P.lo[0], P.hi[0]);
	t[2] = clampf(t[2], P.lo[2], P.hi[2]);
	for (int i = 0; i < 3; i++) r[i] = clampf(r[i], -90, 90);
}
}  // namespace

void RvmReset() {
	rvm = RonVm();
}

bool RvmParam(const char* k, float v) {
	RonParams& P = rvm.p;
	struct { const char* key; float* f; } keys[] = {
		{ "damp", &P.damp }, { "bdamp", &P.buildDamp }, { "ftime", &P.fireTime }, { "fstr", &P.fireStrength }, { "astr", &P.angleStrength },
		{ "pbx", &P.posBuild[0] }, { "pby", &P.posBuild[1] }, { "pbz", &P.posBuild[2] },
		{ "rbp", &P.rotBuild[0] }, { "rby", &P.rotBuild[1] }, { "rbr", &P.rotBuild[2] },
		{ "fads", &P.fireAds }, { "aads", &P.angleAds }, { "bads", &P.buildAds },
		{ "hipf", &P.hipT[0] }, { "hipr", &P.hipT[1] }, { "hipu", &P.hipT[2] }, { "hipp", &P.hipR[0] }, { "hipy", &P.hipR[1] }, { "hipl", &P.hipR[2] },
		{ "adsf", &P.adsT[0] }, { "adsr", &P.adsT[1] }, { "adsu", &P.adsT[2] }, { "adsp", &P.adsR[0] }, { "adsy", &P.adsR[1] }, { "adsl", &P.adsR[2] },
		{ "interp", &P.interp }, { "lor", &P.lo[0] }, { "lof", &P.lo[1] }, { "lou", &P.lo[2] }, { "hir", &P.hi[0] }, { "hif", &P.hi[1] }, { "hiu", &P.hi[2] },
		{ "kmf", &P.kickMove[0] }, { "kmr", &P.kickMove[1] }, { "kmu", &P.kickMove[2] },
		{ "kxf", &P.kickMax[0] }, { "kxr", &P.kickMax[1] }, { "kxu", &P.kickMax[2] },
		{ "move1p", &P.move1P }, { "adsback", &P.adsBack }, { "adsbackmax", &P.adsBackMax },
		{ "krate", &P.kickRate }, { "kret", &P.retShare }, { "kretspd", &P.retSpeed },
		{ "sdur", &P.shakeDur }, { "sin", &P.shakeIn }, { "sout", &P.shakeOut },
		{ "sa0", &P.shakeA[0] }, { "sa1", &P.shakeA[1] }, { "sa2", &P.shakeA[2] }, { "sa3", &P.shakeA[3] }, { "sa4", &P.shakeA[4] }, { "sa5", &P.shakeA[5] }, { "sa6", &P.shakeA[6] },
		{ "sf0", &P.shakeF[0] }, { "sf1", &P.shakeF[1] }, { "sf2", &P.shakeF[2] }, { "sf3", &P.shakeF[3] }, { "sf4", &P.shakeF[4] }, { "sf5", &P.shakeF[5] }, { "sf6", &P.shakeF[6] },
	};
	if (!strcmp(k, "on")) { P.on = v != 0; rvm.msStarted = false; return true; }
	if (!strcmp(k, "hasb")) { P.hasBuild = v != 0; return true; }
	for (auto& e : keys) if (!strcmp(k, e.key)) { *e.f = v; return true; }
	return false;
}

void RvmFire(float rx, float ry, float rz, float kickP, float kickY, float window, const float pivot[3], const float phase[7]) {
	if (!rvm.p.on) return;
	// 4.1: the push lasts RecoilFireTime along FireDir = norm(rx, -1, rz); AngleDir = the rotator of
	// norm(0.7 rx, ry, -0.2 rz): Pitch = atan2(Z, hypot(X, Y)), Yaw = atan2(Y, X), Roll 0
	rvm.timer = rvm.p.fireTime;
	float f[3] = { rx, -1.0f, rz }, fl = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
	for (int i = 0; i < 3; i++) rvm.fireDir[i] = f[i] / fl;
	float a[3] = { 0.7f * rx, ry, -0.2f * rz }, al = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
	if (al > 1e-8f) { for (float& x : a) x /= al; } else { a[0] = 1; a[1] = a[2] = 0; }
	rvm.angleDir[0] = atan2f(a[2], sqrtf(a[0] * a[0] + a[1] * a[1])) * kDeg;
	rvm.angleDir[1] = atan2f(a[1], a[0]) * kDeg;
	rvm.angleDir[2] = 0;
	// 2.4 / 3.2: the camera kick goes into the pending kick; a burst's first shot clears what was applied
	if (rvm.shots == 0) rvm.acc[0] = rvm.acc[1] = 0;
	rvm.ret = false;
	rvm.pend[0] += kickP; rvm.pend[1] += kickY;
	rvm.shots++; rvm.burstLeft = window;
	memcpy(rvm.pivot, pivot, sizeof(rvm.pivot));
	// 5.1 / 5.2: the shake restarts from scratch with these phases
	if (rvm.p.shakeDur > 0) { rvm.shakeLeft = rvm.p.shakeDur; rvm.shakeAge = 0; memcpy(rvm.shakePh, phase, sizeof(rvm.shakePh)); }
}

void RvmStep(float dt, bool aiming) {
	RonParams& P = rvm.p;
	if (!P.on) { memset(rvm.off, 0, sizeof(rvm.off)); memset(rvm.outRot, 0, sizeof(rvm.outRot)); rvm.shakeFov = 0;
		memset(rvm.shakeRot, 0, sizeof(rvm.shakeRot)); memset(rvm.shakeLoc, 0, sizeof(rvm.shakeLoc)); return; }
	if (dt < 0) dt = 0;
	// 4.2: decay, then while the timer runs the push
	float a = fminf(P.damp * dt, 1.0f), b = fminf(P.buildDamp * dt, 1.0f);
	for (int i = 0; i < 3; i++) { rvm.trans[i] -= a * rvm.trans[i]; rvm.rot[i] -= a * rvm.rot[i]; rvm.transB[i] -= b * rvm.transB[i]; rvm.rotB[i] -= b * rvm.rotB[i]; }
	float mF = aiming ? P.fireAds : 1, mA = aiming ? P.angleAds : 1, mB = aiming ? P.buildAds : 1;
	if (rvm.timer > 0) {
		rvm.timer = fmaxf(rvm.timer - dt, 0);
		for (int i = 0; i < 3; i++) {
			rvm.trans[i] += P.fireStrength * rvm.fireDir[i] * mF * dt;
			rvm.rot[i] += P.angleStrength * rvm.angleDir[i] * mA * dt;
			if (P.hasBuild) { rvm.transB[i] += mB * P.posBuild[i] * dt; rvm.rotB[i] += mB * P.rotBuild[i] * dt; }
		}
	}
	// 3.1 / 3.2: the pending camera kick (as recoil.lua applies it): the burst ends after its window
	if (rvm.shots > 0) {
		rvm.burstLeft -= dt;
		if (rvm.burstLeft <= 0) {
			rvm.pend[0] = -P.retShare * rvm.acc[0]; rvm.pend[1] = -P.retShare * rvm.acc[1];
			rvm.acc[0] = rvm.acc[1] = 0; rvm.shots = 0; rvm.ret = true;
		}
	}
	float k = (rvm.ret ? P.retSpeed : P.kickRate) * dt;
	for (int i = 0; i < 2; i++) {
		float step = rvm.pend[i] * k;
		rvm.pend[i] -= step;
		if (!rvm.ret) rvm.acc[i] += step;
	}
	rvm.acc[1] = fmaxf(rvm.acc[1], 0);
	// 6.2: the gun offset heads for its target at interp per second
	float t[3], r[3];
	MeshTarget(aiming, t, r);
	if (!rvm.msStarted) { memcpy(rvm.msT, t, sizeof(t)); memcpy(rvm.msR, r, sizeof(r)); rvm.msStarted = true; }
	else { VInterpTo(rvm.msT, t, dt, P.interp); RInterpTo(rvm.msR, r, dt, P.interp); }
	// ours: camera right, forward, up (m); pitch, roll, yaw (deg) from the FRotator sum
	rvm.off[0] = (rvm.msT[1] - rvm.trans[0] - rvm.transB[0]) * 0.01f;
	rvm.off[1] = (rvm.msT[0] + rvm.trans[1] + rvm.transB[1]) * 0.01f;
	rvm.off[2] = (rvm.msT[2] + rvm.trans[2] + rvm.transB[2]) * 0.01f;
	float sP = rvm.rot[0] + rvm.rotB[0] + rvm.msR[0], sY = rvm.rot[1] + rvm.rotB[1] + rvm.msR[1], sR = rvm.rot[2] + rvm.rotB[2] + rvm.msR[2];
	rvm.outRot[0] = -sR; rvm.outRot[1] = sP; rvm.outRot[2] = -sY;
	// 5.2: the shake (blend-out weight = what is left / BlendOut; blend-in age / BlendIn), phases advance, then sine
	float v[7] = {};
	if (rvm.shakeLeft > 0) {
		rvm.shakeLeft = fmaxf(rvm.shakeLeft - dt, 0);
		rvm.shakeAge += dt;
		if (rvm.shakeLeft > 0) {
			float w = 1;
			if (P.shakeOut > 0 && rvm.shakeLeft < P.shakeOut) w *= rvm.shakeLeft / P.shakeOut;
			if (P.shakeIn > 0 && rvm.shakeAge < P.shakeIn) w *= rvm.shakeAge / P.shakeIn;
			for (int i = 0; i < 7; i++) { rvm.shakePh[i] += dt * P.shakeF[i]; v[i] = P.shakeA[i] * sinf(rvm.shakePh[i]) * w; }
		}
	}
	rvm.shakeRot[0] = v[0]; rvm.shakeRot[1] = v[1]; rvm.shakeRot[2] = v[2];
	rvm.shakeLoc[0] = v[4] * 0.01f; rvm.shakeLoc[1] = v[3] * 0.01f; rvm.shakeLoc[2] = v[5] * 0.01f;
	rvm.shakeFov = v[6];
	for (float& x : rvm.off) if (!std::isfinite(x)) { RonParams keep = rvm.p; RvmReset(); rvm.p = keep; return; }
}
