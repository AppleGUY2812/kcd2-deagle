// The view lean: see lean.h.
#include "lean.h"
#include <cmath>
#include <cstring>

LeanState lean;

namespace {
float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
}

void LeanReset() { lean.target = 0; lean.now = 0; }

bool LeanParam(const char* k, float v) {
	LeanParams& P = lean.p;
	struct { const char* key; float* f; } keys[] = {
		{ "side", &P.side }, { "down", &P.down }, { "roll", &P.roll }, { "in", &P.speedIn }, { "out", &P.speedOut },
	};
	for (auto& e : keys) if (!strcmp(k, e.key)) { *e.f = v; return true; }
	return false;
}

void LeanSet(float target) { lean.target = clampf(target, -1, 1); }

// UE's FInterpTo: toward the target by clamp(dt x speed, 0, 1) of the gap; snapped when within 1e-4; speed 0 = at once
void LeanStep(float dt) {
	if (dt < 0) dt = 0;
	float d = lean.target - lean.now;
	if (fabsf(d) < 1e-4f) { lean.now = lean.target; return; }
	bool in = fabsf(lean.target) > fabsf(lean.now) && lean.target * lean.now >= 0;
	float speed = in ? lean.p.speedIn : lean.p.speedOut;
	lean.now = speed <= 0 ? lean.target : lean.now + d * clampf(dt * speed, 0, 1);
}

bool LeanApply(const float m[12], float out[12]) {
	float L = lean.now;
	if (L == 0) return false;
	const LeanParams& P = lean.p;
	float right[3] = { m[0], m[4], m[8] }, fwd[3] = { m[1], m[5], m[9] }, up[3] = { m[2], m[6], m[10] };
	// roll about forward: the view's top goes toward the lean side (right lean = clockwise as seen from behind)
	float a = L * P.roll * 3.14159265f / 180.0f, c = cosf(a), s = sinf(a);
	float r2[3], u2[3];
	for (int i = 0; i < 3; i++) { r2[i] = right[i] * c - up[i] * s; u2[i] = up[i] * c + right[i] * s; }
	float side = L * P.side, down = fabsf(L) * P.down;
	for (int i = 0; i < 3; i++) {
		out[i * 4 + 0] = r2[i]; out[i * 4 + 1] = fwd[i]; out[i * 4 + 2] = u2[i];
		out[i * 4 + 3] = m[i * 4 + 3] + right[i] * side - up[i] * down;
		if (!std::isfinite(out[i * 4 + 0]) || !std::isfinite(out[i * 4 + 2]) || !std::isfinite(out[i * 4 + 3])) return false;
	}
	return true;
}
