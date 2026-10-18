// Ready or Not's own first-person recoil for the guns imported from it (gun.ronStats), 1:1 from its code
// (docs/ron/ron_recoil_code.md): the procedural recoil on weapon_right (4.1-4.2), the mesh-space gun offsets with the
// pending camera kick's push on them (6.2-6.3) and the fire camera shake (5.2). The camera kick itself is Lua's
// (recoil.lua RonKickTick: it turns the aim); this keeps its own copy of the pending kick for 6.3.
// Portable (no Windows): native/test/ronvm_test.cpp runs it offline. Times are game seconds (F9 slow motion too).
//
// Spaces. Ready or Not's arms component space: X = camera left, Y = forward, Z = up (cm); its FRotator (Pitch, Yaw,
// Roll) there: Roll < 0 = muzzle up, Yaw > 0 = muzzle right, Pitch > 0 = top canted right. Ours (motion.h): camera
// space x right, y forward, z up (m); rot = pitch about x (+ = muzzle up), roll about y (+ = top right), yaw about z
// (+ = muzzle left). So right = -X, forward = Y, up = Z; pitch = -Roll, roll = +Pitch, yaw = -Yaw.
#pragma once

struct RonParams {
	bool on = false;
	// procedural recoil (weapon blueprint, C++ defaults in tools/ron/ron_stats.py)
	float damp = 7.5f, buildDamp = 15.0f, fireTime = 0.05f, fireStrength = 0.5f, angleStrength = 0.4f;
	float posBuild[3] = {}, rotBuild[3] = {};             // component X Y Z (cm/s); FRotator P Y R (deg/s)
	float fireAds = 0.7f, angleAds = 0.3f, buildAds = 0.0f; bool hasBuild = false;
	// mesh-space offsets: camera space forward, right, up (cm) and FRotator P Y R (deg); hip / aiming
	float hipT[3] = {}, hipR[3] = {}, adsT[3] = {}, adsR[3] = {};
	float interp = 6.4f;                                  // per second (8 x 0.8)
	float lo[3] = { -10, 2, -10 }, hi[3] = { 10, 10, 10 }; // clamps, ours: right, forward, up (cm)
	float kickMove[3] = { 2, 2, 2 }, kickMax[3] = { 2, 2, 2 };   // forward, right, up: x pending kick deg -> cm
	float move1P = 0.5f, adsBack = 2.5f, adsBackMax = 2.5f;
	// its copy of the camera kick (recoil.lua): rate /s, return share and speed
	float kickRate = 7.0f, retShare = 0.0f, retSpeed = 0.0f;
	// fire camera shake: duration, blend in / out (s); axes 0-6 = rot pitch, yaw, roll (deg), loc forward, right, up
	// (cm), fov (deg): amplitude, frequency (rad/s)
	float shakeDur = 0, shakeIn = 0, shakeOut = 0, shakeA[7] = {}, shakeF[7] = {};
};

struct RonVm {
	RonParams p;
	// procedural recoil (component space, cm / FRotator deg)
	float timer = 0, fireDir[3] = {}, angleDir[3] = {};
	float trans[3] = {}, rot[3] = {}, transB[3] = {}, rotB[3] = {};
	// pending camera kick (deg: pitch up, yaw right), what of it was applied, the burst
	float pend[2] = {}, acc[2] = {}, burstLeft = 0; int shots = 0; bool ret = false;
	// mesh-space offset now (camera forward, right, up cm; FRotator deg)
	float msT[3] = {}, msR[3] = {}; bool msStarted = false;
	// shake
	float shakeLeft = 0, shakeAge = 0, shakePh[7] = {};
	// results, ours: off (m: right, forward, up) and rot (deg: pitch, roll, yaw) about pivot (viewmodel-local m);
	// shakeRot (deg: pitch up, yaw right, roll), shakeLoc (m: right, forward, up), shakeFov (deg, horizontal)
	float off[3] = {}, outRot[3] = {}, pivot[3] = {};
	float shakeRot[3] = {}, shakeLoc[3] = {}, shakeFov = 0;
};
extern RonVm rvm;

void RvmReset();                                        // off, all state cleared
bool RvmParam(const char* key, float v);                // false = unknown key
// a shot: rx, ry, rz = its random numbers (U(-1,1), U(-0.5,0.5), U(-1,1) x RecoilRandomness), the camera kick it added
// (deg), the burst window (2 x FireRate ...), weapon_right in the viewmodel's own space (m), the shake's 7 phases (rad)
void RvmFire(float rx, float ry, float rz, float kickP, float kickY, float window, const float pivot[3], const float phase[7]);
void RvmStep(float dt, bool aiming);                    // once a frame, game seconds
