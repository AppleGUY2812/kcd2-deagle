// The native round sim's own cost, no engine: N rounds flown for F frames at 60 fps through a world whose ray finds
// nothing (the integrator, the bookkeeping, one callback a step) and one with a ground plane at z = 0 (hits, answers).
// usage: g++ -std=c++20 -O2 native/sim/sim.cpp native/sim/test/bench.cpp -o bench && ./bench [rounds] [frames]
#include "../sim.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

static int rayNone(void*, const sim_vec*, const sim_vec*, uint64_t, sim_ray_hit*) { return 0; }
static int rayGround(void*, const sim_vec* f, const sim_vec* m, uint64_t, sim_ray_hit* out) {
	if (m->z >= 0 || f->z + m->z > 0) return 0;
	double t = -f->z / m->z;
	out->pos = { f->x + m->x * t, f->y + m->y * t, 0 };
	out->dist = t * std::sqrt(m->x * m->x + m->y * m->y + m->z * m->z);
	out->normal = { 0, 0, 1 };
	out->surface = 1;
	out->partid = -1;
	out->entity = 0;
	return 1;
}

static void run(const char* name, sim_ray_fn ray, uint32_t n, int frames) {
	sim_config cfg{ 0.05, 6, 1000, 9.81, 4, -1, 30 };
	sim* s = sim_create(n, &cfg);
	sim_set_world(s, ray, nullptr, nullptr);
	sim_phys ph{ 470, 600, 240, 0.0195 };   // ~.50 AE
	int32_t p = sim_add_phys(s, &ph);
	std::vector<uint32_t> ids(n);
	srand(1);
	for (uint32_t i = 0; i < n; i++) {
		double a = (rand() % 1000) / 1000.0 * 6.283, e = 0.02 + (rand() % 1000) / 1000.0 * 0.3;
		sim_vec pos{ 0, 0, 1.6 }, dir{ std::cos(a) * std::cos(e), std::sin(a) * std::cos(e), std::sin(e) };
		ids[i] = sim_fire(s, &pos, &dir, p);
	}
	uint64_t alive = 0;
	auto t0 = std::chrono::steady_clock::now();
	for (int f = 0; f < frames; f++) {
		for (uint32_t i = 0; i < n; i++) {
			if (ids[i] == UINT32_MAX) continue;
			int32_t st = sim_step(s, ids[i], 1.0 / 60);
			while (st == SIM_HIT) st = sim_answer(s, ids[i], 0, nullptr, nullptr, 0, 0);   // every hit lands
			if (st != SIM_FLYING) { sim_free(s, ids[i]); ids[i] = UINT32_MAX; } else alive++;
		}
	}
	double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	sim_stats st;
	sim_get_stats(s, &st);
	printf("%-7s %u rounds, %d frames: %.1f ms, %.1f ns per round-step, %.2f ms per frame at 1000 alive; rays %llu hits %llu\n",
		name, n, frames, ms, ms * 1e6 / (double)st.steps, ms / frames * 1000.0 / ((double)alive / frames + 1e-9),
		(unsigned long long)st.rays, (unsigned long long)st.hits);
	sim_destroy(s);
}

int main(int argc, char** argv) {
	uint32_t n = argc > 1 ? (uint32_t)atoi(argv[1]) : 100000;
	int frames = argc > 2 ? atoi(argv[2]) : 600;
	run("sky", rayNone, n, frames);
	run("ground", rayGround, n, frames);
}
