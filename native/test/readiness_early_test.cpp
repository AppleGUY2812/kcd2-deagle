// Outside-the-game test of readiness_early.h (the "let me in early" decision). Run by tests/run_all.sh.
#include "../src/readiness_early.h"
#include <cstdio>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main() {
	using namespace early;
	CHECK(Detail("TextureStreaming") && Detail("SvotiUpload") && Detail("HLodStreaming"));
	CHECK(!Detail("TerrainStreaming") && !Detail("AnimationStreaming") && !Detail("EntityStreaming"));
	CHECK(!Detail("task(vt 3a1234)") && !Detail(nullptr) && !Detail(""));
	CHECK(Eligible(0x3f93) && Eligible(0x3f91) && !Eligible(0x81) && !Eligible(0x3b91));

	const Cfg c;
	const double dt = 1.0 / 60;
	{   // the short fast-travel trip: terrain + animations busy until 6.2 s, textures until 6.8 s
		State s;
		double let = -1;
		for (int i = 0; i < 420; i++) {
			const double t = i * dt;
			const bool must = t >= 6.2, detail = t < 6.8;
			if (Step(s, c, t, must, detail) && let < 0) let = t;
		}
		CHECK(let > 6.49 && let < 6.5 + 2 * dt);   // 6.2 + hold 0.3
		CHECK(s.wouldOpen == let);
	}
	{   // must-have tasks ready from the start: not before minAge
		State s;
		double let = -1;
		for (int i = 0; i < 180; i++) if (Step(s, c, i * dt, true, true) && let < 0) let = i * dt;
		CHECK(let >= 1.0 && let < 1.0 + 2 * dt);
	}
	{   // terrain in waves: ready for 0.2 s, busy again, then ready for good at 4 s
		State s;
		double let = -1;
		for (int i = 0; i < 360; i++) {
			const double t = i * dt;
			const bool must = (t >= 2.0 && t < 2.2) || t >= 4.0;
			if (Step(s, c, t, must, true) && let < 0) let = t;
		}
		CHECK(let > 4.29 && let < 4.3 + 2 * dt);
	}
	{   // detail done first: never let in early (the game opens it itself)
		State s;
		bool any = false;
		for (int i = 0; i < 300; i++) any |= Step(s, c, i * dt, i * dt >= 3.0, i * dt < 2.0);
		CHECK(!any && s.wouldOpen < 0);
	}
	{   // must-have never ready: never
		State s;
		bool any = false;
		for (int i = 0; i < 300; i++) any |= Step(s, c, i * dt, false, true);
		CHECK(!any);
	}
	if (fails) return 1;
	printf("readiness_early_test: all passed\n");
	return 0;
}
