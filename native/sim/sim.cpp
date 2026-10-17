// The native round simulation: see sim.h. Each function names the ballistics.lua code it mirrors; numbers in double.
#include "sim.h"

#include <cmath>
#include <cstdlib>
#include <new>
#include <unordered_map>
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
	sim_living_fn living = nullptr;
	sim_draw_fn draw = nullptr;
	bool aheadOn = false;
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
	// the frame API: rounds it moves (in launch order), their last move's time, its events
	std::vector<uint8_t> managed;
	std::vector<double> lastT;
	std::vector<uint32_t> active;
	std::vector<sim_event> events;
	// near misses: where each round's frame step began, whom it already passed or hit, each NPC's next allowed time
	std::vector<V> frameFrom;
	std::vector<std::vector<uint64_t>> passed, hitEnts;
	std::unordered_map<uint64_t, double> flyNext;
	double flyAt = -1e300;
	std::vector<sim_living> near;
	// the kill cam's look-ahead: its frame count per round (every other frame), the NPCs already reported
	std::vector<int32_t> kcFrame;
	std::vector<std::vector<uint64_t>> aheadSeen;
	// tracers
	struct Trail {
		bool on = false, hasTurnPos = false, hasTurnDist = false;
		int32_t fx = 0;
		double length = 0, start = 0, dash = 1, reach = 100, turnDist = 0, anchor = 0.5;
		V off{ 0, 0, 0 }, turnPos{ 0, 0, 0 };
	};
	std::vector<Trail> trail;
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

void fillRound(const sim* s, uint32_t id, sim_round* out) {
	out->pos = toC(s->pos[id]);
	out->vel = toC(s->vel[id]);
	out->dist = s->dist[id];
	out->age = s->age[id];
	out->hops = s->hops[id];
	out->tumble = s->tumble[id];
	out->alive = s->alive[id];
	out->skip = s->skip[id];
}

bool has(const std::vector<uint64_t>& v, uint64_t e) {
	for (uint64_t x : v) if (x == e) return true;
	return false;
}

// TracerBullet: a short streak ending where the round is, along the way it went this frame; after a turn it starts at
// the turn point and never reaches back past it; the muzzle offset weighs 1 - d / reach at d m along the path
void drawTracer(sim* s, uint32_t id, V from) {
	sim::Trail& t = s->trail[id];
	double dist = s->dist[id];
	if (!t.on || !s->draw || dist < t.start) return;
	bool turned = t.hasTurnDist;
	if (t.hasTurnPos) { from = t.turnPos; t.hasTurnPos = false; }
	V p = s->pos[id], u = p - from;
	double ml = len(u);
	if (ml <= 1e-4) { u = s->vel[id]; ml = len(u); }
	if (ml <= 0) return;
	u = u * (1.0 / ml);
	double l = std::fmin(t.length, dist - t.start);
	if (turned) l = std::fmin(l, dist - t.turnDist);
	if (l < 0.05) return;
	auto w = [&](double d) { double x = 1 - d / t.reach; return turned || x < 0 ? 0.0 : x; };
	V p1 = p + t.off * w(dist), p0 = p - u * l + t.off * w(dist - l);
	V d = p1 - p0;
	double dl = len(d);
	if (dl <= 0) return;
	d = d * (1.0 / dl);
	double back = l * t.anchor;
	sim_vec at = toC(p1 - d * back), dir = toC(d);
	s->draw(s->ud, id, t.fx, l / t.dash, l, &at, &dir);
}

// distance from point q to the segment a..b, and the closest point
double segDist(V q, V a, V b, V& closest) {
	V ab = b - a;
	double l2 = ab.x * ab.x + ab.y * ab.y + ab.z * ab.z, t = 0;
	if (l2 > 0) t = std::fmin(1.0, std::fmax(0.0, ((q.x - a.x) * ab.x + (q.y - a.y) * ab.y + (q.z - a.z) * ab.z) / l2));
	closest = a + ab * t;
	return len(q - closest);
}

// What StepRound does after a round's moves, in its order: the tracer (TracerBullet), the kill cam's look-ahead
// (KillCamStep), near misses (FlyBy). One living query covers both of the last two.
void frameEnd(sim* s, uint32_t id, bool done) {
	V f = s->frameFrom[id], p = s->pos[id], sv = p - f, v = s->vel[id];
	double now = s->lastT[id];
	drawTracer(s, id, f);
	// KillCamStep's gates, in its order: not done, no hops, not tumbling, far enough, fast enough, every other frame,
	// not climbing; then reach = speed x aheadK + a frame of flight
	bool ahead = false;
	V aEnd = p;
	double speed = len(v);
	if (!done && s->aheadOn && s->cfg.aheadK > 0 && s->hops[id] == 0 && !s->tumble[id] && s->dist[id] >= s->cfg.aheadMinDist
		&& speed >= 50) {
		s->kcFrame[id]++;
		if (s->kcFrame[id] % 2 == 1 && v.z <= 0.2 * speed) {
			ahead = true;
			aEnd = p + v * ((speed * s->cfg.aheadK + speed / 60) / speed);
		}
	}
	double nearM = s->cfg.nearMiss, len2 = sv.x * sv.x + sv.y * sv.y + sv.z * sv.z;
	bool fly = nearM > 0 && len2 >= 1e-6;
	if (!s->living || !(fly || ahead)) return;
	// one query round everything this frame touches: the frame's path and the look-ahead
	V lo = { std::fmin(f.x, std::fmin(p.x, aEnd.x)), std::fmin(f.y, std::fmin(p.y, aEnd.y)), std::fmin(f.z, std::fmin(p.z, aEnd.z)) };
	V hi = { std::fmax(f.x, std::fmax(p.x, aEnd.x)), std::fmax(f.y, std::fmax(p.y, aEnd.y)), std::fmax(f.z, std::fmax(p.z, aEnd.z)) };
	const double pad = std::fmax(nearM, 2.5);   // the look-ahead's body column: 2 m tall, 1 m round
	if (s->near.size() < 256) s->near.resize(256);
	sim_vec c = toC((lo + hi) * 0.5);
	int n = s->living(s->ud, &c, len(hi - lo) / 2 + pad, s->near.data(), (int)s->near.size());

	if (ahead) {   // a ray only when a living body stands near the look-ahead (KillCamStep casts one every other frame)
		bool any = false;
		for (int k = 0; k < n && !any; k++) {
			const sim_living& L = s->near[k];
			if (has(s->aheadSeen[id], L.entity)) continue;
			V cl, feet = { L.feet.x, L.feet.y, L.feet.z };
			for (double z = 0; z <= 2.0 && !any; z += 0.5) any = segDist(feet + V{ 0, 0, z }, p, aEnd, cl) <= 1.0;
		}
		if (any) {
			sim_vec from = toC(p), move = toC(aEnd - p);
			sim_ray_hit h{};
			s->stats.rays++;
			if (s->ray(s->ud, &from, &move, s->skip[id], &h) > 0 && h.entity) {
				bool isLiving = false;
				for (int k = 0; k < n && !isLiving; k++) isLiving = s->near[k].entity == h.entity;
				if (isLiving && !has(s->aheadSeen[id], h.entity)) {
					s->aheadSeen[id].push_back(h.entity);
					sim_event e{};
					e.kind = SIM_EV_AHEAD;
					e.round = id;
					e.hit.round = id;
					e.hit.pos = h.pos;
					e.hit.normal = h.normal;
					e.hit.dir = toC(v * (1.0 / speed));
					e.hit.surface = h.surface;
					e.hit.partid = h.partid;
					e.hit.entity = h.entity;
					e.hit.dist = h.dist;
					fillRound(s, id, &e.state);
					s->events.push_back(e);
				}
			}
		}
	}

	if (!fly) return;
	// FlyBy: living NPCs whose chest is within nearMiss of the frame's straight path; once per NPC per round, never one
	// this round hit, each NPC once per flybyGap
	double flen = std::sqrt(len2);
	if (s->flyAt > now) s->flyNext.clear();   // the clock went back (a load)
	s->flyAt = now;
	for (int k = 0; k < n; k++) {
		const sim_living& L = s->near[k];
		if (has(s->passed[id], L.entity) || has(s->hitEnts[id], L.entity)) continue;
		auto it = s->flyNext.find(L.entity);
		if (it != s->flyNext.end() && now < it->second) continue;
		V cl, chest = { L.feet.x, L.feet.y, L.feet.z + s->cfg.chest };
		if (segDist(chest, f, p, cl) > nearM) continue;
		s->passed[id].push_back(L.entity);
		s->flyNext[L.entity] = now + s->cfg.flybyGap;
		sim_event e{};
		e.kind = SIM_EV_FLYBY;
		e.round = id;
		e.hit.entity = L.entity;
		e.hit.pos = toC(cl);
		e.hit.dir = toC(sv * (1.0 / flen));
		fillRound(s, id, &e.state);
		s->events.push_back(e);
	}
}

// a frame-API round's step result as events: a hit waits for its answer; otherwise its frame step is over (near
// misses), and an ended round is freed (it leaves `active` at the next compaction)
void emit(sim* s, uint32_t id, int32_t st) {
	if (st != SIM_HIT) frameEnd(s, id, st != SIM_FLYING);
	if (st == SIM_FLYING) return;
	sim_event e{};
	e.round = id;
	fillRound(s, id, &e.state);
	if (st == SIM_HIT) { e.kind = SIM_EV_HIT; e.hit = s->pending[id]; }
	else {
		e.kind = st == SIM_LANDED ? SIM_EV_LANDED : SIM_EV_DONE;
		s->alive[id] = 0;
		s->managed[id] = 0;
		s->freeList.push_back(id);
	}
	s->events.push_back(e);
}

void compact(sim* s) {
	size_t w = 0;
	for (uint32_t id : s->active) if (s->alive[id] && s->managed[id]) s->active[w++] = id;
	s->active.resize(w);
}

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
	s->managed.resize(capacity); s->lastT.resize(capacity);
	s->frameFrom.resize(capacity); s->passed.resize(capacity); s->hitEnts.resize(capacity);
	s->kcFrame.resize(capacity); s->aheadSeen.resize(capacity); s->trail.resize(capacity);
	s->active.reserve(capacity);
	s->events.reserve(1024);
	s->freeList.reserve(capacity);
	for (uint32_t i = capacity; i > 0; i--) s->freeList.push_back(i - 1);
	return s;
}

void sim_destroy(sim* s) { delete s; }

void sim_set_world(sim* s, sim_ray_fn ray, sim_water_fn water, void* ud) { s->ray = ray; s->water = water; s->ud = ud; }
void sim_set_living(sim* s, sim_living_fn living) { s->living = living; }
void sim_set_draw(sim* s, sim_draw_fn draw) { s->draw = draw; }
void sim_set_ahead(sim* s, int32_t on) { s->aheadOn = on != 0; }

void sim_tracer(sim* s, uint32_t id, int32_t fx, double length, double reach, const sim_vec* off, double start, double dash,
	double anchor) {
	if (!valid(s, id)) return;
	sim::Trail& t = s->trail[id];
	t.on = true;
	t.fx = fx;
	t.length = length;
	t.start = start;
	t.dash = dash > 0 ? dash : 1;
	t.anchor = anchor;
	t.off = off ? fromC(*off) : V{ 0, 0, 0 };
	t.reach = reach > 0 ? reach : 1;
}

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
	s->managed[i] = 0;
	s->passed[i].clear();
	s->hitEnts[i].clear();
	s->kcFrame[i] = 0;
	s->aheadSeen[i].clear();
	s->trail[i] = sim::Trail{};
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
int32_t sim_answer(sim* s, uint32_t id, int32_t goesOn, const sim_vec* pos, const sim_vec* vel, uint64_t skip, int32_t tumble,
	int32_t markHit, int32_t turned) {
	if (!valid(s, id) || s->state[id] != SIM_HIT) return SIM_DONE;
	if (markHit && s->pending[id].entity && !has(s->hitEnts[id], s->pending[id].entity)) s->hitEnts[id].push_back(s->pending[id].entity);
	if (pos) s->pos[id] = fromC(*pos);
	if (vel) s->vel[id] = fromC(*vel);
	s->skip[id] = skip;
	s->tumble[id] = tumble ? 1 : 0;
	if (turned && goesOn) {   // Deflect / a corner: the tracer starts again at the turn
		s->trail[id].hasTurnPos = s->trail[id].hasTurnDist = true;
		s->trail[id].turnPos = s->pos[id];
		s->trail[id].turnDist = s->dist[id];
	}
	int32_t st;
	if (!goesOn) st = s->state[id] = SIM_LANDED;
	else {
		s->hops[id]++;
		if (s->hops[id] > s->cfg.maxHops || len(s->vel[id]) < s->cfg.minSpeedOn) st = s->state[id] = SIM_DONE;
		else st = run(s, id);
	}
	if (s->managed[id]) emit(s, id, st);
	return st;
}

void sim_free(sim* s, uint32_t id) {
	if (!valid(s, id)) return;
	s->alive[id] = 0;
	s->managed[id] = 0;
	s->freeList.push_back(id);
}

int32_t sim_get(const sim* s, uint32_t id, sim_round* out) {
	if (!valid(s, id)) return 0;
	fillRound(s, id, out);
	return 1;
}

uint32_t sim_launch(sim* s, const sim_vec* pos, const sim_vec* dir, int32_t phys, double now, double firstDt) {
	uint32_t id = sim_fire(s, pos, dir, phys);
	if (id == UINT32_MAX) return id;
	s->managed[id] = 1;
	s->lastT[id] = now;
	s->active.push_back(id);
	s->frameFrom[id] = s->pos[id];
	emit(s, id, sim_step(s, id, firstDt));
	return id;
}

// RoundsTick: every flying round moves by the time since its last move (a round launched this frame: 0)
void sim_advance(sim* s, double now, double tickMax) {
	compact(s);
	for (size_t k = 0, n = s->active.size(); k < n; k++) {
		uint32_t id = s->active[k];
		if (!s->alive[id] || s->state[id] != SIM_FLYING) continue;   // ended, or a hit still unanswered
		double dt = std::fmin(std::fmax(now - s->lastT[id], 0.0), tickMax);
		s->lastT[id] = now;
		s->frameFrom[id] = s->pos[id];
		emit(s, id, sim_step(s, id, dt));
	}
}

const sim_event* sim_events(const sim* s, uint32_t* n) {
	*n = (uint32_t)s->events.size();
	return s->events.data();
}

void sim_clear_events(sim* s) { s->events.clear(); }

uint32_t sim_alive(const sim* s) {
	uint32_t n = 0;
	for (uint32_t id : s->active) n += s->alive[id] && s->managed[id];
	return n;
}

void sim_get_stats(const sim* s, sim_stats* out) { *out = s->stats; }

}  // extern "C"
