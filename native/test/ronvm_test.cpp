// Offline check of native/src/ronvm.cpp against docs/ron/ron_recoil_code.md (Ready or Not's recoil, decompiled), with
// the Grizzly's numbers. Built and run by tests/run_all.sh:
//   g++ -O2 -std=c++17 native/test/ronvm_test.cpp native/src/ronvm.cpp -o ronvm_test && ./ronvm_test
#include "../src/ronvm.h"
#include <cmath>
#include <cstdio>

static int fails = 0;
static void check(const char* what, bool ok, double got) {
	printf("%s %s (%.4f)\n", ok ? "PASS" : "FAIL", what, got);
	if (!ok) fails++;
}

static void grizzly() {
	RvmReset();
	const struct { const char* k; float v; } p[] = {
		{ "on", 1 }, { "damp", 2.0f }, { "bdamp", 2.5f }, { "ftime", 0.128f }, { "fstr", 8.5f }, { "astr", 0.075f },
		{ "pbz", 13 }, { "rbr", -15 }, { "fads", 0.7f }, { "aads", 0.3f }, { "bads", 1.0f }, { "hasb", 1 },
		{ "hipf", 5 }, { "hipr", -0.3f }, { "hipu", -0.3f }, { "adsf", 15 }, { "adsr", -6.55f }, { "adsu", 3.1f }, { "adsl", -0.5f },
		{ "kret", 0 }, { "kretspd", 0 },
		{ "sdur", 0.2f }, { "sout", 0.5f }, { "sa0", 14 }, { "sf0", 18 }, { "sa2", 8.8f }, { "sf2", 16 }, { "sa3", 8 }, { "sf3", 32 },
		{ "sa4", 0.25f }, { "sf4", 25 }, { "sa5", 4 }, { "sf5", 13 }, { "sa6", 1.5f }, { "sf6", 10 },
	};
	for (auto& e : p) RvmParam(e.k, e.v);
}

int main() {
	const float pivot[3] = { 0.2f, 0.3f, -0.1f }, ph0[7] = {};
	const float dt = 1.0f / 120.0f;
	// one hip shot, the random parts at their mean (0): 4.4's peaks, about 1.0 cm back, 1.5 cm up, 1.7 deg muzzle up at 133 ms
	grizzly();
	RvmStep(dt, false);                       // the offset settles on its hip target first
	float hip[3] = { rvm.off[0], rvm.off[1], rvm.off[2] };
	RvmFire(0, 0, 0, 0, 0, 0.59f, pivot, ph0);
	float backPeak = 0, upPeak = 0, rotPeak = 0, tPeak = 0, zAt = 0;
	for (int i = 1; i <= 120; i++) {
		RvmStep(dt, false);
		float back = -(rvm.off[1] - hip[1]) * 100, up = (rvm.off[2] - hip[2]) * 100;
		if (back > backPeak) { backPeak = back; tPeak = i * dt; }
		upPeak = fmaxf(upPeak, up); rotPeak = fmaxf(rotPeak, rvm.outRot[0]);
		if (i == 12) zAt = rvm.shakeLoc[2] * 100;   // 0.1 s
	}
	check("proc recoil peak back ~1.00 cm (4.4)", fabsf(backPeak - 1.00f) < 0.03f, backPeak);
	check("its peak at ~133 ms", fabsf(tPeak - 0.133f) < 0.01f, tPeak);
	check("buildup peak up ~1.49 cm", fabsf(upPeak - 1.49f) < 0.03f, upPeak);
	check("buildup peak muzzle up ~1.72 deg (Roll -15/s)", fabsf(rotPeak - 1.72f) < 0.03f, rotPeak);
	// the shake: up = 4 w sin(13 t), w = (0.2 - t) / 0.5, phase 0 (the one axis without a random phase)
	float t = 0.1f, want = 4.0f * (0.2f - t) / 0.5f * sinf(13 * t);
	check("shake up at 0.1 s = 4 w sin(13 t) cm", fabsf(zAt - want) < 0.02f, zAt);
	for (int i = 0; i < 40; i++) RvmStep(dt, false);
	check("shake over after 0.2 s", rvm.shakeLoc[2] == 0 && rvm.shakeRot[0] == 0, rvm.shakeLoc[2]);
	// the pending camera kick pushes the hip offset: + 0.5 x clamp(2 Pp, 2) cm up at once (6.3)
	grizzly();
	RvmStep(dt, false);
	float u0 = rvm.off[2];
	RvmFire(0, 0, 0, 8.69f, -0.5f, 0.59f, pivot, ph0);
	RvmStep(0.0f, false);
	float push = 0;
	for (int i = 0; i < 30; i++) { RvmStep(dt, false); push = fmaxf(push, (rvm.off[2] - u0) * 100 - 0); }
	check("pending kick lifts the hip gun (with the buildup) above 1.5 cm", push > 1.5f, push);
	for (int i = 0; i < 240; i++) RvmStep(dt, false);
	check("the pending kick is gone 2 s on (dropped at 0.59 s)", fabsf(rvm.pend[0]) < 1e-6f && rvm.shots == 0, rvm.pend[0]);
	// aiming: the target is (15 clamped to 10, -6.55, 3.1) cm, 0.5 deg muzzle up, reached at 6.4 per second
	grizzly();
	RvmStep(dt, false);
	for (int i = 0; i < 600; i++) RvmStep(dt, true);
	check("ADS forward 15 clamped to 10 cm", fabsf(rvm.off[1] * 100 - 10) < 0.01f, rvm.off[1] * 100);
	check("ADS right -6.55 cm, up 3.1 cm", fabsf(rvm.off[0] * 100 + 6.55f) < 0.01f && fabsf(rvm.off[2] * 100 - 3.1f) < 0.01f, rvm.off[0] * 100);
	check("ADS 0.5 deg muzzle up", fabsf(rvm.outRot[0] - 0.5f) < 0.01f, rvm.outRot[0]);
	grizzly();
	RvmStep(dt, false);
	float f0 = rvm.off[1];
	for (int i = 0; i < 12; i++) RvmStep(dt, true);   // 0.1 s into the aim: 1 - (1 - 6.4/120)^12 = 48 % of the way
	float frac = (rvm.off[1] - f0) / (0.10f - f0);
	check("aim-in at 6.4/s: 48 % of the way after 0.1 s", fabsf(frac - (1 - powf(1 - 6.4f / 120, 12))) < 0.01f, frac);
	// off: nothing moves
	RvmReset(); RvmStep(dt, false);
	check("off: no offset", rvm.off[0] == 0 && rvm.off[1] == 0 && rvm.off[2] == 0, rvm.off[1]);
	printf("RONVM %s\n", fails ? "FAIL" : "PASS");
	return fails ? 1 : 0;
}
