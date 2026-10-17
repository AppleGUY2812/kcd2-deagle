// The native round simulation: see sim.h. Each function names the ballistics.lua code it mirrors; numbers in double.
#include "sim.h"

#include <cmath>
#include <cstdlib>
#include <new>
#include <vector>

namespace {

struct V { double x, y, z; };
inline V operator+(V a, V b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline V operator-(V a, V b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline V operator*(V a, double k) { return { a.x * k, a.y * k, a.z * k }; }
inline double len(V a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
inline V fromC(const sim_vec& v) { return { v.x, v.y, v.z }; }
inline sim_vec toC(V v) { return { v.x, v.y, v.z }; }

}  // namespace

// Rounds in structure-of-arrays: what one step reads sits together, nothing is allocated per round or per step
struct sim {
	sim_config cfg;
	sim_ray_fn ray = nullptr;
	sim_water_fn water = nullptr;
	void* ud = nullptr;
	std::vector<sim_phys> phys;
	std::vector<uint8_t> pass;        // by surface id: 1 = rays go on through it (no entity)

	uint32_t cap;
	std::vector<V> pos, vel;
	std::vector<double> dist, age;
	std::vector<double> h, rest;      // the current step's length; time left in a step a hit cut (after the answer)
	std::vector<int32_t> subLeft;     // whole steps left in this sim_step
	std::vector<uint8_t> hasRest, alive, tumble, state;
	std::vector<int32_t> hops, physId;
	std::vector<uint64_t> skip;
	std::vector<sim_hit> pending;
	std::vector<uint32_t> freeList;
	sim_stats stats{};
};

namespace {

bool isPass(const sim* s, int32_t surface) {
	return surface >= 0 && (size_t)surface < s->pass.size() && s->pass[surface];
}

// FlightHit: the first solid thing along a step, past foliage / invisible blockers (at most 8, from 1 mm past each).
// 1 hit, 0 nothing, -1 the ray failed
int flightHit(sim* s, V from0, V move, uint64_t skip, sim_ray_hit& h) {
	sim_vec f = toC(from0), m = toC(move);
	s->stats.rays++;
	int rc = s->ray(s->ud, &f, &m, skip, &h);
	int through = 0;
	for (;;) {
		if (rc < 0) return -1;
		if (rc == 0) return 0;
		if (!(h.entity == 0 && isPass(s, h.surface) && through < 8)) return 1;
		through++;
		double ml = len(move);
		V dir = move * (1.0 / ml);
		double done = h.dist + 0.001;
		if (done >= ml) return 0;
		f = toC(from0 + dir * done);
		m = toC(dir * (ml - done));
		s->stats.rays++;
		rc = s->ray(s->ud, &f, &m, skip, &h);
		if (rc > 0) h.dist += done;
	}
}

// waterCross: the water surface a falling step crosses between p and t (where it ends, or the solid thing it hit)
bool waterCross(sim* s, V p, V t, sim_ray_hit& out) {
	if (t.z >= p.z || !s->water) return false;
	sim_vec at = toC(t), n{ 0, 0, 1 };
	double level = 0;
	if (!s->water(s->ud, &at, &level, &n)) return false;
	if (t.z >= level || p.z < level - 0.01) return false;
	double k = std::fmax(0.0, (p.z - level) / (p.z - t.z));
	V d = t - p;
	out.pos = toC(p + d * k);
	out.dist = k * len(d);
	out.surface = s->cfg.waterSurface;
	out.normal = n;
	out.partid = -1;
	out.entity = 0;
	return true;
}

// MoveRound: one step of h seconds; a ray along the move. Drag along the line exactly (dv/dt = -v^2/L: speed x 1/(1+x),
// distance L ln(1+x)/v per unit velocity, x = v h / L), and the speed gravity adds dragged from when it was added
// (mean factor (1 + x/2)/(1+x), the drop's (1 + x/3)/(1+x)): where a round lands doesn't depend on the frame rate
int32_t moveRound(sim* s, uint32_t i, double hstep) {
	s->stats.steps++;
	V p = s->pos[i], v = s->vel[i];
	const sim_phys& ph = s->phys[s->physId[i]];
	const double g = s->cfg.gravity;
	double speed = len(v);
	double L = s->tumble[i] ? ph.tumbleDragLength : ph.dragLength;
	double x = speed * hstep / L;
	double k = 1.0 / (1.0 + x);
	double gv = g * hstep * (1 + 0.5 * x) * k, gp = 0.5 * g * hstep * hstep * (1 + x / 3) * k;
	V v1 = { v.x * k, v.y * k, v.z * k - gv };
	double along = speed > 0 ? L * std::log1p(x) / speed : hstep;
	V m = { v.x * along, v.y * along, v.z * along - gp };
	double mlen = len(m);
	sim_ray_hit hit{};
	int rc = 0;
	if (mlen > 0) {
		rc = flightHit(s, p, m, s->skip[i], hit);
		if (rc < 0) return SIM_DONE;
	}
	V t = rc > 0 ? fromC(hit.pos) : p + m;
	sim_ray_hit w{};
	bool isWater = mlen > 0 && waterCross(s, p, t, w);
	if (isWater) hit = w;
	if (rc > 0 || isWater) {
		// the round as it reaches the hit: slowed as e^(-d/L) over the d it flew, in the time (L/v)(e^(d/L) - 1); its
		// direction there is its velocity's, not the step's chord
		double d = hit.dist;
		double th = speed > 0 ? std::fmin(hstep, L / speed * std::expm1(d / L)) : hstep * d / mlen;
		double xh = speed * th / L, kh = 1.0 / (1.0 + xh);
		V vh = { v.x * kh, v.y * kh, v.z * kh - g * th * (1 + 0.5 * xh) * kh };
		double sh = speed * kh;
		sim_hit& e = s->pending[i];
		e.round = i;
		e.pos = hit.pos;
		e.normal = hit.normal;
		e.dir = toC(vh * (1.0 / len(vh)));
		e.surface = hit.surface;
		e.partid = hit.partid;
		e.entity = hit.entity;
		e.dist = s->dist[i] + d;
		e.energy = 0.5 * ph.bullet * sh * sh;
		e.water = isWater;
		s->vel[i] = vh;
		s->dist[i] = e.dist;
		s->age[i] += th;
		s->pos[i] = fromC(hit.pos);
		s->rest[i] = hstep - th;
		s->hasRest[i] = 1;
		s->stats.hits++;
		return SIM_HIT;
	}
	s->pos[i] = t;
	s->vel[i] = v1;
	s->age[i] += hstep;
	s->dist[i] += mlen;
	return (s->dist[i] > s->cfg.range || s->age[i] > s->cfg.life) ? SIM_DONE : SIM_FLYING;
}

// the rest of a sim_step: first what a hit left of its step, then the whole steps left
int32_t run(sim* s, uint32_t i) {
	if (s->hasRest[i]) {
		s->hasRest[i] = 0;
		int32_t st = moveRound(s, i, s->rest[i]);
		if (st != SIM_FLYING) return s->state[i] = (uint8_t)st;
	}
	while (s->subLeft[i] > 0) {
		s->subLeft[i]--;
		int32_t st = moveRound(s, i, s->h[i]);
		if (st != SIM_FLYING) return s->state[i] = (uint8_t)st;
	}
	return s->state[i] = SIM_FLYING;
}

bool valid(const sim* s, uint32_t id) { return id < s->cap && s->alive[id]; }

}  // namespace

extern "C" {

sim* sim_create(uint32_t capacity, const sim_config* cfg) {
	sim* s = new (std::nothrow) sim();
	if (!s) return nullptr;
	s->cfg = *cfg;
	s->cap = capacity;
	s->pos.resize(capacity); s->vel.resize(capacity);
	s->dist.resize(capacity); s->age.resize(capacity); s->h.resize(capacity); s->rest.resize(capacity);
	s->subLeft.resize(capacity); s->hasRest.resize(capacity); s->alive.resize(capacity); s->tumble.resize(capacity);
	s->state.resize(capacity); s->hops.resize(capacity); s->physId.resize(capacity); s->skip.resize(capacity);
	s->pending.resize(capacity);
	s->freeList.reserve(capacity);
	for (uint32_t i = capacity; i > 0; i--) s->freeList.push_back(i - 1);
	return s;
}

void sim_destroy(sim* s) { delete s; }

void sim_set_world(sim* s, sim_ray_fn ray, sim_water_fn water, void* ud) { s->ray = ray; s->water = water; s->ud = ud; }

void sim_set_pass(sim* s, const int32_t* ids, int32_t n) {
	s->pass.clear();
	for (int32_t k = 0; k < n; k++) {
		if (ids[k] < 0) continue;
		if ((size_t)ids[k] >= s->pass.size()) s->pass.resize(ids[k] + 1, 0);
		s->pass[ids[k]] = 1;
	}
}

int32_t sim_add_phys(sim* s, const sim_phys* p) {
	s->phys.push_back(*p);
	return (int32_t)s->phys.size() - 1;
}

uint32_t sim_fire(sim* s, const sim_vec* pos, const sim_vec* dir, int32_t phys) {
	if (s->freeList.empty() || phys < 0 || (size_t)phys >= s->phys.size()) return UINT32_MAX;
	uint32_t i = s->freeList.back();
	s->freeList.pop_back();
	s->pos[i] = fromC(*pos);
	s->vel[i] = fromC(*dir) * s->phys[phys].velocity;
	s->dist[i] = s->age[i] = s->h[i] = s->rest[i] = 0;
	s->subLeft[i] = s->hops[i] = 0;
	s->hasRest[i] = s->tumble[i] = 0;
	s->state[i] = SIM_FLYING;
	s->alive[i] = 1;
	s->physId[i] = phys;
	s->skip[i] = 0;
	return i;
}

// StepRound: dt cut into n equal steps no longer than cfg.step (n = ceil(dt / step - 1e-9)); nothing moves for dt <= 1e-6
int32_t sim_step(sim* s, uint32_t id, double dt) {
	if (!valid(s, id) || s->state[id] != SIM_FLYING) return valid(s, id) ? (int32_t)s->state[id] : (int32_t)SIM_DONE;
	if (dt <= 1e-6) return SIM_FLYING;
	int32_t n = (int32_t)std::ceil(dt / s->cfg.step - 1e-9);
	s->h[id] = dt / n;
	s->subLeft[id] = n;
	s->hasRest[id] = 0;
	return run(s, id);
}

const sim_hit* sim_pending_hit(const sim* s, uint32_t id) {
	return valid(s, id) && s->state[id] == SIM_HIT ? &s->pending[id] : nullptr;
}

// MoveRound after ApplyHit: landed, or on with the new state; past maxHops or under minSpeedOn it's done; else the
// rest of the cut step and the steps left
int32_t sim_answer(sim* s, uint32_t id, int32_t goesOn, const sim_vec* pos, const sim_vec* vel, uint64_t skip, int32_t tumble) {
	if (!valid(s, id) || s->state[id] != SIM_HIT) return SIM_DONE;
	if (pos) s->pos[id] = fromC(*pos);
	if (vel) s->vel[id] = fromC(*vel);
	s->skip[id] = skip;
	s->tumble[id] = tumble ? 1 : 0;
	if (!goesOn) return s->state[id] = SIM_LANDED;
	s->hops[id]++;
	if (s->hops[id] > s->cfg.maxHops || len(s->vel[id]) < s->cfg.minSpeedOn) return s->state[id] = SIM_DONE;
	return run(s, id);
}

void sim_free(sim* s, uint32_t id) {
	if (!valid(s, id)) return;
	s->alive[id] = 0;
	s->freeList.push_back(id);
}

int32_t sim_get(const sim* s, uint32_t id, sim_round* out) {
	if (!valid(s, id)) return 0;
	out->pos = toC(s->pos[id]);
	out->vel = toC(s->vel[id]);
	out->dist = s->dist[id];
	out->age = s->age[id];
	out->hops = s->hops[id];
	out->tumble = s->tumble[id];
	out->alive = s->alive[id];
	out->skip = s->skip[id];
	return 1;
}

void sim_get_stats(const sim* s, sim_stats* out) { *out = s->stats; }

}  // extern "C"
