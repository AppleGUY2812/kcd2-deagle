// The native round simulation (docs/ARCHITECTURE.md step 6, Claude 5): rounds in flight in plain arrays, no engine or
// Windows headers, so the same file runs in the plugin, in a Linux test binary and from LuaJIT (FFI) next to
// ballistics.lua. Semantic reference: ballistics.lua's LaunchRound / StepRound / MoveRound / FlightHit / waterCross; the
// contract: tests/run_round_contract.lua (Claude 1).
//
// The world is behind two callbacks (the plugin: IPhysicalWorld::RayWorldIntersection and the water lookup; the tests:
// scripted planes). A hit stops the round until the caller answers it (Lua's ApplyHit decides: goes on or not, new
// pos / vel / skip / tumble), then the step carries on from the hit for the rest of its time, exactly as MoveRound does.
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { double x, y, z; } sim_vec;

typedef struct {
	double dist;        // m from the ray's start
	sim_vec pos, normal;
	int32_t surface;    // KCD2 surface id
	int32_t partid;     // -1: none
	uint64_t entity;    // 0: static (no entity)
} sim_ray_hit;

// returns 1 hit (out filled), 0 nothing, -1 the ray failed. skip: an entity to ignore (0 none); the player is the
// world's business
typedef int (*sim_ray_fn)(void* ud, const sim_vec* from, const sim_vec* move, uint64_t skip, sim_ray_hit* out);
// returns 1 when `at` is in a water area (level and surface normal filled), 0 when dry
typedef int (*sim_water_fn)(void* ud, const sim_vec* at, double* level, sim_vec* normal);
// the living NPCs within radius of center that may hear a near miss (not the player, not the dead): up to max into
// out, returns how many (npc.lua FlyBy: Physics.SamplePhysEnvironment, ent_living)
typedef struct { uint64_t entity; sim_vec feet; } sim_living;
typedef int (*sim_living_fn)(void* ud, const sim_vec* center, double radius, sim_living* out, int max);
// a round's tracer streak this frame (tracers.lua TracerBullet): the effect fx (the caller's id), count pieces over
// length m, the effect placed at `at` along `dir` (the plugin restarts the round's own emitter there: fxr)
typedef void (*sim_draw_fn)(void* ud, uint32_t round, int32_t fx, double count, double length, const sim_vec* at,
	const sim_vec* dir);

typedef struct {
	double velocity;          // m/s at the muzzle
	double dragLength;        // m: dv/dt = -v|v| / dragLength
	double tumbleDragLength;  // m: the same, tumbling (ballistics.lua TumbleDragLength)
	double bullet;            // kg
} sim_phys;

typedef struct {
	double step;              // s: ROUND_STEP (0.05)
	double life;              // s: ROUND_LIFE (6)
	double range;             // m: cfg.range (1000)
	double gravity;           // m/s^2 (9.81)
	int32_t maxHops;          // cfg.impactCap.maxHops (4)
	int32_t waterSurface;     // id of mat_water (-1: none)
	double minSpeedOn;        // m/s: a round that goes on slower than this is done (30)
	double nearMiss;          // m: cfg.nearMiss, a living NPC's chest this close to a round's frame path hears it (0: off)
	double chest;             // m above the feet (npc.lua CHEST 1.25)
	double flybyGap;          // s: one near miss per NPC this often at most (npc.lua FLYBY_GAP 0.75)
	double aheadK;            // s: the kill cam's look-ahead, reach = speed x aheadK + speed / 60 (killcam.lua KillCamStep:
	                          // c.speed.pre x c.lead x 1.25); 0: off
	double aheadMinDist;      // m: only rounds this far along look ahead (killCam.minDist)
} sim_config;

typedef struct {
	uint32_t round;
	sim_vec pos, normal, dir;
	int32_t surface, partid;
	uint64_t entity;
	double dist;              // path length flown to the hit
	double energy;            // J: 0.5 bullet speed^2 at the step's start
	int32_t water;            // 1: the hit is a water surface crossing
} sim_hit;

enum { SIM_FLYING = 0, SIM_HIT = 1, SIM_DONE = 2, SIM_LANDED = 3 };

typedef struct sim sim;

sim* sim_create(uint32_t capacity, const sim_config* cfg);
void sim_destroy(sim* s);
void sim_set_world(sim* s, sim_ray_fn ray, sim_water_fn water, void* ud);
void sim_set_living(sim* s, sim_living_fn living);
void sim_set_draw(sim* s, sim_draw_fn draw);
// the kill cam may start (none running, no slow motion, no free camera): look-ahead events only while on
void sim_set_ahead(sim* s, int32_t on);
// the round's tracer (tracers.lua TracerNative: cfg.tracer length / start / dash; trail.off and trail.reach, the muzzle
// offset that blends out by reach m along the path; anchor: where the effect sits, the share of the streak back from
// its front end: 1 its start, 0 its end, 0.5 the middle)
void sim_tracer(sim* s, uint32_t id, int32_t fx, double length, double reach, const sim_vec* off, double start, double dash,
	double anchor);
// surfaces a ray passes on through when it hit no entity (foliage, invisible blockers: surfaces.lua fx = "pass")
void sim_set_pass(sim* s, const int32_t* ids, int32_t n);
int32_t sim_add_phys(sim* s, const sim_phys* p);

// a new round at pos along dir (unit) with phys; returns its id, or UINT32_MAX when full. It isn't moved yet: step it
// by the frame (LaunchRound steps it by frameDt at once)
uint32_t sim_fire(sim* s, const sim_vec* pos, const sim_vec* dir, int32_t phys);
// move a round by dt seconds (StepRound: equal steps no longer than cfg.step). Returns SIM_FLYING, SIM_DONE (range,
// life, a failed ray), or SIM_HIT: the hit is in sim_pending_hit and the round waits for sim_answer
int32_t sim_step(sim* s, uint32_t id, double dt);
const sim_hit* sim_pending_hit(const sim* s, uint32_t id);
// the answer to a pending hit (ApplyHit): goesOn 0 = landed; else the round's new state, and the step carries on.
// Returns like sim_step (SIM_LANDED when it didn't go on). A round of the frame API (sim_launch) reports what follows
// as events
// markHit: the hit's entity counts as hit by this round (hits.lua r.hitIds): no near miss to it from this round.
// turned: ApplyHit turned it (Deflect / a corner: r.turnPos): its tracer starts again at pos
int32_t sim_answer(sim* s, uint32_t id, int32_t goesOn, const sim_vec* pos, const sim_vec* vel, uint64_t skip, int32_t tumble,
	int32_t markHit, int32_t turned);
void sim_free(sim* s, uint32_t id);

typedef struct {
	sim_vec pos, vel;
	double dist, age;
	int32_t hops, tumble, alive;
	uint64_t skip;
} sim_round;
int32_t sim_get(const sim* s, uint32_t id, sim_round* out);

// The frame API: the game's path, where Lua touches no round per frame, only events. Rounds launched here are moved
// by sim_advance and freed by the sim when they end; each hit, end and landing is an event. A hit waits for
// sim_answer, whose carrying on can add events (fetch sim_events again after each answer: the list may move).
// Time is the caller's clock (s): a round moves by now - its last move (at most tickMax), as RoundsTick does.
// SIM_EV_FLYBY: a near miss after the round's frame step (npc.lua FlyBy): hit.entity who, hit.pos the closest point of
// the frame's path, hit.dir its direction
// SIM_EV_AHEAD: the kill cam's look-ahead ray from the round hit a living NPC (hit.entity, hit.pos, hit.dist from the
// round): Lua decides (alive, lethal) and may start the cam. Once per round per NPC
enum { SIM_EV_HIT = 1, SIM_EV_DONE = 2, SIM_EV_LANDED = 3, SIM_EV_FLYBY = 4, SIM_EV_AHEAD = 5 };
typedef struct {
	int32_t kind;
	uint32_t round;
	sim_hit hit;              // SIM_EV_HIT
	sim_round state;          // the round after the event (at the hit: pos / vel / dist / age there)
} sim_event;
// LaunchRound: fire and move by firstDt at once (a close shot lands in the frame it's fired)
uint32_t sim_launch(sim* s, const sim_vec* pos, const sim_vec* dir, int32_t phys, double now, double firstDt);
void sim_advance(sim* s, double now, double tickMax);
const sim_event* sim_events(const sim* s, uint32_t* n);
void sim_clear_events(sim* s);
uint32_t sim_alive(const sim* s);

typedef struct { uint64_t rays, steps, hits; } sim_stats;
void sim_get_stats(const sim* s, sim_stats* out);

#ifdef __cplusplus
}
#endif
