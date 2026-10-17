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
// Returns like sim_step (SIM_LANDED when it didn't go on)
int32_t sim_answer(sim* s, uint32_t id, int32_t goesOn, const sim_vec* pos, const sim_vec* vel, uint64_t skip, int32_t tumble);
void sim_free(sim* s, uint32_t id);

typedef struct {
	sim_vec pos, vel;
	double dist, age;
	int32_t hops, tumble, alive;
	uint64_t skip;
} sim_round;
int32_t sim_get(const sim* s, uint32_t id, sim_round* out);

typedef struct { uint64_t rays, steps, hits; } sim_stats;
void sim_get_stats(const sim* s, sim_stats* out);

#ifdef __cplusplus
}
#endif
