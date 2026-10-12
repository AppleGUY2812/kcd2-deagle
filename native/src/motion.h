// Procedural viewmodel motion (docs/weapon_movement_design.md): look sway, walk bob, sprint pose + stride wobble,
// jump / air / land, crouch transition, dodge kick, strafe / move inertia, a soft "hang" spring (RoN's LazySpringBone),
// plus the shot kick. Camera space: x right, y forward, z up. Results in mo.off (m) / mo.rot (deg: pitch about x,
// roll about y, yaw about z), applied by PlaceViewmodel.
#pragma once

struct Spring {
	float x = 0, v = 0;
	// implicit (backward Euler) damped spring: stable at any step; the explicit step it replaced blew up once
	// dt > 2 / sqrt(k) (a 70 ms hitch for the stiff shot kick, k = 784): the gun's transform went NaN and it vanished
	void step(float target, float k, float c, float dt) {
		v = (v - dt * k * (x - target)) / (1.0f + dt * c + dt * dt * k);
		x += v * dt;
	}
};

// per-gun tunables (mparams <key> <value>); defaults = the G19 values of the design doc, section 4
struct MotionParams {
	float dragAimRot = 0.6f, dragAimLoc = 0.4f;          // RoN InertiaDragAimRotation / Location
	float dragStrafeRot = 0.4f, dragStrafeLoc = 0.2f;    // RoN InertiaDragStrafeRotation / Location
	float moveScale[3] = { 10, 10, 5 };                  // RoN MovementSpeedScale (cm at full walk, x 0.3)
	float moveRotP = 60;                                 // RoN MovementSpeedRotationScalePitchYawRoll.pitch (x 0.05)
	float meshInterp = 55;                               // RoN MeshSpaceAimInterp (omega = /5)
	float hangK = 100, hangC = 8, hangAds = 0.3f;        // RoN LazySpringBone SpringStiffness / Damping; share kept in ADS
	float sprintRon = 1;                                 // 1 = RoN's pistol sprint pose + stride wobble, 0 = the old pose
	float sprintPose[6] = { -0.008f, -0.031f, -0.118f, -8.7f, -35.5f, 1.2f };   // off xyz m, rot pitch/roll/yaw deg
	float jumpScale = 1, landScale = 1, crouchScale = 1, dodgeScale = 1, bobScale = 1;
	float testRoll = 0;                                  // deg: constant roll for the sign check (design section 5.1)
};

struct MotionState {
	bool enabled = true, aiming = false;
	float scale = 1.0f;                                  // global multiplier (motion <scale>, deagle_motion)
	MotionParams p;
	// inputs from Lua (vel / air / stance / dodge); vel is WORLD velocity m/s (player:GetVelocity), sent every frame and
	// turned camera-relative here with the current camera; stale -> decays to 0 (the camera's own motion is never used:
	// it carries KCD2's head bob and frame-time noise)
	float velW[3]{}, velS[3]{}; double velAge = 1e9;   // last sent / smoothed (tau 0.06 s), seconds since the last vel
	float yawF = 0, pitchF = 0;                         // turn rates (deg/s), low-passed (uneven frame times)
	bool air = false, airFromLua = false, crouched = false;
	float airTime = 0, vzAir = 0;                        // time in the air, the fall speed at landing
	// internal
	float lastPos[3]{}, lastFwd[3]{}; bool have = false; double t = 0;
	float speed = 0, vz = 0, phase = 0, stride = 0, sprintT = 0;
	bool sprinting = false;
	Spring yaw, pitch, swayX, swayZ, aimK, sprint, airK;
	Spring hang[3], rotImp[3], strafeRoll, strafeYaw, moveP;
	Spring kickBack, kickPitch, kickYaw, kickRoll;       // the shot's push on the gun model (Lua sends it from the cartridge physics)
	float off[3]{}, rot[3]{};                            // results
};
extern MotionState mo;

void UpdateMotion(const float* cam);                    // per processed frame: dt from the performance counter
void MotionStep(const float* cam, float dt);             // portable core (also used by the offline stability test)
void ResetMotion();
void MotionVel(float x, float y, float z);              // world m/s
void MotionAir(bool inAir);
void MotionStance(bool crouched);
void MotionDodge(float side, float strength);
bool MotionParam(const char* key, float value);          // false = unknown key
