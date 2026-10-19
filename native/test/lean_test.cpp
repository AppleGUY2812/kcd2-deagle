// Offline check of native/src/lean.cpp. Built and run by tests/run_all.sh:
//   g++ -O2 -std=c++17 native/test/lean_test.cpp native/src/lean.cpp -o lean_test && ./lean_test
#include "../src/lean.h"
#include <cmath>
#include <cstdio>

static int fails = 0;
static void check(const char* what, bool ok, double got) {
	printf("%s %s (%.4f)\n", ok ? "PASS" : "FAIL", what, got);
	if (!ok) fails++;
}

int main() {
	// a camera at (10, 20, 1.6) looking along +Y: right +X, forward +Y, up +Z
	const float cam[12] = { 1, 0, 0, 10,  0, 1, 0, 20,  0, 0, 1, 1.6f };
	float out[12];
	LeanReset();
	LeanParam("side", 0.3f); LeanParam("down", 0.05f); LeanParam("roll", 10); LeanParam("in", 8); LeanParam("out", 10);
	check("upright: nothing applied", !LeanApply(cam, out), 0);
	// full right lean
	LeanSet(1); LeanStep(10.0f);
	check("full lean reached (dt x speed >= 1)", fabsf(lean.now - 1) < 1e-6f, lean.now);
	bool ok = LeanApply(cam, out);
	check("right lean: 0.3 m right", ok && fabsf(out[3] - 10.3f) < 1e-5f && fabsf(out[7] - 20) < 1e-5f, out[3]);
	check("            0.05 m down", fabsf(out[11] - 1.55f) < 1e-5f, out[11]);
	// roll: up tilts toward +X (right) by 10 deg, right tilts down
	check("            view top toward the right by 10 deg", fabsf(out[2] - sinf(10 * 3.14159265f / 180)) < 1e-5f && out[10] < 1, out[2]);
	check("            right axis tilts down", out[8] < 0, out[8]);
	check("            forward untouched", out[1] == 0 && out[5] == 1 && out[9] == 0, out[5]);
	float dot = out[0] * out[2] + out[4] * out[6] + out[8] * out[10];
	float lr = sqrtf(out[0] * out[0] + out[4] * out[4] + out[8] * out[8]);
	check("            still orthonormal", fabsf(dot) < 1e-6f && fabsf(lr - 1) < 1e-6f, dot);
	// left lean mirrors
	LeanSet(-1); LeanStep(10.0f); LeanApply(cam, out);
	check("left lean: 0.3 m left, 0.05 down, top to the left", fabsf(out[3] - 9.7f) < 1e-5f && fabsf(out[11] - 1.55f) < 1e-5f && out[2] < 0, out[3]);
	// interpolation: in at 8 / s (FInterpTo), out at 10 / s
	LeanReset(); LeanSet(1);
	for (int i = 0; i < 6; i++) LeanStep(1.0f / 60);
	float want = 1 - powf(1 - 8.0f / 60, 6);
	check("in at 8 / s: 1 - (1 - 8/60)^6 after 0.1 s", fabsf(lean.now - want) < 1e-5f, lean.now);
	lean.now = 1; LeanSet(0);
	for (int i = 0; i < 6; i++) LeanStep(1.0f / 60);
	want = powf(1 - 10.0f / 60, 6);
	check("out at 10 / s", fabsf(lean.now - want) < 1e-5f, lean.now);
	// a wall cut the target to 0.4 mid-lean: it settles there
	LeanSet(0.4f); for (int i = 0; i < 600; i++) LeanStep(1.0f / 60);
	check("settles on a cut target 0.4", fabsf(lean.now - 0.4f) < 1e-6f, lean.now);
	printf("LEAN %s\n", fails ? "FAIL" : "PASS");
	return fails ? 1 : 0;
}
