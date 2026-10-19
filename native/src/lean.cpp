// The view lean: see lean.h.
#include "lean.h"
#include <cmath>
#include <cstring>

LeanState lean;

namespace {
float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
// UE's FInterpTo: by clamp(dt x speed, 0, 1) of the gap, snapped within 1e-4; speed 0 = at once
float InterpTo(float cur, float target, float dt, float speed) {
	float d = target - cur;
	if (speed <= 0 || d * d < 1e-8f) return target;
	return cur + d * clampf(dt * speed, 0, 1);
}
}

void LeanReset() { LeanParams p = lean.p; lean = LeanState(); lean.p = p; }

bool LeanParam(const char* k, float v) {
	LeanParams& P = lean.p;
	struct { const char* key; float* f; } keys[] = {
		{ "in", &P.rateIn }, { "out", &P.outSpeed }, { "cam", &P.camSpeed }, { "dip", &P.dip }, { "cant", &P.cantSpeed },
		{ "cantmax", &P.cantMax },
	};
	for (auto& e : keys) if (!strcmp(k, e.key)) { *e.f = v; return true; }
	return false;
}

void LeanSet(float targetCm) { lean.target = targetCm; }

void LeanStep(float dt) {
	if (dt < 0) dt = 0;
	const LeanParams& P = lean.p;
	float t = lean.target, q = lean.q;
	if (t == 0) q = InterpTo(q, 0, dt, P.outSpeed);                          // let go
	else if (t * q > 0 && fabsf(t) < fabsf(q)) q = t;                       // a wall: cut at once
	else {                                                                  // toward the lean at its constant rate
		float step = P.rateIn * dt, d = t - q;
		q = fabsf(d) <= step ? t : q + (d > 0 ? step : -step);
	}
	lean.q = q;
	// the view offset: one VInterpTo on (side, down)
	float ts = q, td = P.dip * fabsf(q), ds = ts - lean.side, dd = td - lean.down;
	if (P.camSpeed <= 0 || ds * ds + dd * dd < 1e-4f) { lean.side = ts; lean.down = td; }
	else { float a = clampf(dt * P.camSpeed, 0, 1); lean.side += ds * a; lean.down += dd * a; }
	lean.cant = InterpTo(lean.cant, clampf(q, -P.cantMax, P.cantMax), dt, P.cantSpeed);
}

bool LeanApply(const float m[12], float out[12], bool cant) {
	if (lean.side == 0 && lean.down == 0 && (!cant || lean.cant == 0)) return false;
	float right[3] = { m[0], m[4], m[8] }, fwd[3] = { m[1], m[5], m[9] }, up[3] = { m[2], m[6], m[10] };
	float a = cant ? lean.cant * 3.14159265f / 180.0f : 0, c = cosf(a), s = sinf(a);
	for (int i = 0; i < 3; i++) {
		out[i * 4 + 0] = right[i] * c - up[i] * s;           // a right cant: right tilts down, up toward the right
		out[i * 4 + 1] = fwd[i];
		out[i * 4 + 2] = up[i] * c + right[i] * s;
		// its offset is in the body's space (the camera's parent): along the view's level right, down along world Z
		out[i * 4 + 3] = m[i * 4 + 3] + (right[i] * lean.side - (i == 2 ? lean.down : 0)) * 0.01f;
		if (!std::isfinite(out[i * 4 + 0]) || !std::isfinite(out[i * 4 + 2]) || !std::isfinite(out[i * 4 + 3])) return false;
	}
	return true;
}
