// Offline check of native/src/lean.cpp against Ready or Not's quick lean (docs/ron/ron_grizzly_interactions.md).
// Built and run by tests/run_all.sh:
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
	const float dt = 1.0f / 60;
	// a camera at (10, 20, 1.6) looking along +Y, 30 deg down: right +X, forward (0, cos, -sin), up (0, sin, cos)
	const float c30 = cosf(0.5236f), s30 = sinf(0.5236f);
	const float cam[12] = { 1, 0, 0, 10,  0, c30, s30, 20,  0, -s30, c30, 1.6f };
	float out[12];
	LeanReset();
	check("upright: nothing applied", !LeanApply(cam, out, true), 0);
	// E: the quick lean runs to 15 cm at 400 cm/s (2.25 frames at 60 fps)
	LeanSet(15);
	LeanStep(dt);
	check("quick lean 400 cm/s: 6.67 cm after a frame", fabsf(lean.q - 400 * dt) < 1e-4f, lean.q);
	LeanStep(dt); LeanStep(dt);
	check("  15 cm by the third frame", lean.q == 15, lean.q);
	for (int i = 0; i < 600; i++) LeanStep(dt);
	check("view settles 15 cm right, 7.5 cm down", fabsf(lean.side - 15) < 1e-3f && fabsf(lean.down - 7.5f) < 1e-3f, lean.side);
	check("arms cant 15 deg", fabsf(lean.cant - 15) < 1e-3f, lean.cant);
	bool ok = LeanApply(cam, out, false);
	check("the view: 0.15 m along its level right, 0.075 m straight down (even looking down)",
		ok && fabsf(out[3] - 10.15f) < 1e-5f && fabsf(out[7] - 20) < 1e-5f && fabsf(out[11] - 1.525f) < 1e-5f, out[11]);
	check("  no roll", out[0] == 1 && out[2] == 0 && out[10] == c30, out[2]);
	LeanApply(cam, out, true);
	check("the gun's camera: rolled 15 deg, top to the right", fabsf(out[2] - sinf(15 * 3.14159265f / 180)) < 1e-5f && out[8] < 0, out[2]);
	float dot = out[0] * out[2] + out[4] * out[6] + out[8] * out[10], dot2 = out[0] * out[1] + out[4] * out[5] + out[8] * out[9];
	check("  still orthonormal", fabsf(dot) < 1e-6f && fabsf(dot2) < 1e-6f, dot);
	// the camera follows at VInterpTo 8: from 0, one frame of 1/60 s covers 8/60 of the gap to the target
	LeanReset(); LeanSet(15); LeanStep(dt);
	check("view VInterpTo 8 / s behind the quick lean", fabsf(lean.side - 400 * dt * 8 * dt) < 1e-4f, lean.side);
	// a wall: the target drops to 5 cm: taken at once
	for (int i = 0; i < 600; i++) LeanStep(dt);
	LeanSet(5); LeanStep(dt);
	check("a wall cuts the quick lean to 5 cm at once", lean.q == 5, lean.q);
	// let go: back at FInterpTo 30
	LeanSet(0); LeanStep(dt);
	check("let go: FInterpTo 30 / s", fabsf(lean.q - 5 * (1 - 30 * dt)) < 1e-4f, lean.q);
	// Q from a right lean: through 0 to -15 at 400 cm/s
	LeanReset(); LeanSet(15); for (int i = 0; i < 600; i++) LeanStep(dt);
	LeanSet(-15); LeanStep(dt);
	check("E to Q: 400 cm/s through the middle", fabsf(lean.q - (15 - 400 * dt)) < 1e-4f, lean.q);
	for (int i = 0; i < 600; i++) LeanStep(dt);
	check("  settles 15 cm left, cant -15", fabsf(lean.side + 15) < 1e-3f && fabsf(lean.cant + 15) < 1e-3f && fabsf(lean.down - 7.5f) < 1e-3f, lean.side);
	printf("LEAN %s\n", fails ? "FAIL" : "PASS");
	return fails ? 1 : 0;
}
