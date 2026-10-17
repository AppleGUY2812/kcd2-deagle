# Architecture: vision and brief for the architecture lead

Draft by Claude 4 (2026-10-15). The architecture lead (Claude 5, a dedicated session the user asked for) owns this file
from here. Keep it current: what's decided, what's measured, what's next.

## The user's goals (their words, 2026-10-15)
- "I would prefer our mod handle an unlimited amount of bullets." No cap may remove a round in flight.
- Fire rates up to a CIWS (~75 rounds a second), stress-testable (`deagle_stress 75`, `deagle_stress 75 aim ab`).
- "Impacts allow me to light up the entire environment" (Claude 2's impact light pool, a44c89c).
- "I am obsessed with optimization, it is the mark of a true anti slop coder." No fallbacks, no patch solutions.
- Ready or Not's muzzle flash, sound and effects, measured against RoN, not guessed.
- "This bridge seems very brittle and not professional": the Lua <-> plugin text bridge.

## The bar (the user, 2026-10-15)
"A 60 player lobby with full auto guns in Phantom Forces shooting auto guns, that is the bare minimum." At ~800
rounds/min each (typical full auto; Phantom Forces' own numbers not checked), all 60 firing = ~800 rounds/s; target
**1000 rounds/s, 60 fps, p99 frame <= 16.7 ms**, every round a real round (~700 in flight), 2000-4000 hits/s. This needs:
hit budgets by priority (full effects near and on screen, merged / cheaper far, none unseen) instead of flat caps;
the fast-fire mode for every shooter (looped distant sound, small flash, tracer every Nth); NPC gunners themselves
(AI, animation) are KCD2's cost and a project of their own. Benchmark: a `lobby` stress mode, 60 invisible gun spots
around the player, full auto ~13 rounds/s each, across the map: same rounds, hits, sounds and tracers, no NPCs.

## Milestone 2: 100,000 rounds/s (the user: "we can hit 100k a second, we just need more tricks")
At 60 fps: ~1700 fired a frame, ~57,000 in flight. Moving them (SIMD) ~0.3 ms; KCD2's ray (~5-15 us) can't follow
(57,000 rays a frame = ~285 ms). Plan: our own copy of the static collision within ~300 m (terrain heightfield + static
meshes read from KCD2's physics once, kept in sync for doors / breakables) traced with Intel Embree (Apache 2.0,
millions of incoherent rays/s per core) on worker threads; NPCs as bone capsules tested analytically; rounds of one gun
traced in packets; tracers through `EmitParticle` on one emitter per type, every Nth round; hits merged per spot; one
battle soundscape; hits applied in order per target, reported to Lua once a frame. Open: can native code read KCD2's
collision geometry (IPhysicalEntity parts -> IGeometry -> mesh / primitive data), how many cores KCD2 leaves free, and
the pfx1 cost per particle. Milestone 1 (1000 rounds/s) is on the way and reused.
Collision copy check (static RE, docs/whgame_re/collision_copy.md): **amber-green**. KCD2's RayWorldIntersection is
CryEngine's rwi.cpp nearly byte for byte; static entities via GetEntitiesInBox (own buffer, main thread, triggers
on-demand physics like a real ray), parts / trimesh / heightfield readable, surface mapping known; terrain holes and
two-sided triangles to replicate; no creation event (re-check areas every 1-2 s, entity-deleted event id 14);
entity-owned statics (doors) count as dynamic; NPC rays hit per-bone parts, never the living capsule. Next: a probe
comparing our triangle / heightfield test with KCD2's ray on the same hit.

## Bullet design: work only when something happens (steps 6-7 and milestone 2)
1. A round is a formula: origin, direction, fire time, cartridge (drag + gravity are fixed per cartridge), so its
   position at any time is computed on demand (closed form or a per-cartridge table). Nothing per round per frame.
2. Events in time order (a timing wheel / heap): at the shot the static path cast gives "static hit at t" or "expires
   at t"; each frame pops only the events due. Cost follows hits per frame, not rounds alive.
3. Movers ask, rounds don't: at the shot each path is written into a grid of cells with its time window per cell; each
   frame each NPC / horse / rigid body looks up rounds passing its cells now and tests its bone shapes. A hit cancels
   the round's static event. The same lookup gives near misses (FlyBy).
4. Most rounds are decided at the shot: no mover near the whole path = a scheduled hit, never checked again.
5. Static casts on worker threads, ahead of need (a round can't reach a wall before its first frame of flight);
   rounds landing within the first frame (~7 m) are cast at once on the main thread.
6. A burst from one gun traced as a thin cone, split into single rounds only where the cone touches an edge.
7. Tracers every Nth round, drawn from the formula, only paths in the view frustum.
8. Invalidation only on change: a door opens / something breaks -> re-cast the rounds whose cells overlap it.
Frame cost = events due + movers x local rounds + visible tracers. Shot cost = one static cast per round (KCD2's ray up
to ~1000 rounds/s, our tracer beyond). Each idea ships behind a setting and is measured on / off.

## The GPU (decided 2026-10-15)
Not for bullet hits first. GPU results come back 1-3 frames late (a round flies ~7 m a frame, so close shots would land
late), KCD2's GPU load is unmeasured (PresentMon), and hooking its DX12 is where today's descriptor crash came from.
The CPU tracer (Embree, 1-2 cores for 100k rounds/s) answers in the same frame. The GPU comes in where a measurement
says the CPU ran out: far path segments traced ahead (a round's path is known at the shot), and drawing tracers
instanced at 100k rounds/s (needs a renderer hook: the riskiest part, last).

## Team and assignments (2026-10-15)
Models read from the session logs (`~/.claude/projects/*/*.jsonl`, the last assistant message's model and token usage);
effort isn't recorded reliably there (Claude 1's log shows a switch to xhigh).

| Claude | Session | Model | Context now | Owns / first job |
|---|---|---|---|---|
| 1 manager, ballistics | deagle-game-todo-notion | Sonnet 5.5 | ~136k | Notion; contract tests for the native sim: golden Lua traces, hit order per target, seeded randomness, sub-frame spawn offsets |
| 2 effects | niagara-metal-impact-emulator | Opus 5.5 | ~767k | impact budgets by priority (near / on screen full, far merged, unseen none) for 2000-4000 hits/s |
| 3 sound | kcd2-deagle-9b | Opus 5.5 | ~735k | fast-fire loop + tail (candidate real recordings for the user to judge), sound priority at lobby scale |
| 4 flash, viewmodel, native | kcd2-deagle-4a | Opus 5.5 | ~628k | `lobby` stress mode (baseline now), fast-fire visuals (continuous flash, tracer every Nth, casing stream) |
| 5 architecture | kcd2-deagle-54 | Opus 5.5 | ~400k | probes (installed at next close), typed binding, loader / core split, native sim, collision copy |

Contexts of 600-770k tokens: each compacts at its next clean point (`/compact keep: my files and ownership, open
tasks, docs/ARCHITECTURE.md`); big jobs run in fresh helper agents with a written brief. Effort: xhigh for native,
reverse engineering and the reload core (Claudes 4, 5); high for Claudes 2, 3; Claude 1 as is (Sonnet for managing;
the native sim's C++ is Claude 5's, Claude 1 owns its tests).

## Where we are (measured in game 2026-10-15; details in docs/PERFORMANCE.md and MODLOG.md)
- **Lua** (KCD2's official mod scripting, patch-stable, hot-reloadable with `tools/live.sh reload`): 14 modules in
  `src/Scripts/Deagle/` plus `ballistics.lua`, `killcam.lua`, `guns/`. Owners are listed in CLAUDE.md "Who edits what".
- **Plugin** `native/` (`deagle_native.asi`, Ultimate ASI Loader, MinHook): engine hooks found by reverse engineering
  WHGame 1.5.6 (vtable offsets). A KCD2 patch can break any of them. A plugin change needs a game restart.
- **Bridge**: Lua -> plugin = `System.LogAlways("[DN] <cmd>")` lines the plugin parses (~6-10 us each, formatting
  plus parsing). Plugin -> Lua = console commands queued and run at the next camera step. A capability handshake
  (`deagle_native_caps`) failed silently on 2026-10-15 (a `%1` console command takes one word), so it was off all
  session. A dev reload does not replace a console command's script.
- **Costs**:
  - A round in flight: ~0.025-0.04 ms of Lua a frame (64 rounds = 1.6-2.4 ms; the mod's budget is 1.0 avg / 2.0 p99).
  - An entity-system query (GetEntitiesInSphere / ByClass): 5-9 ms at any size.
  - The physics grid (`Physics.SamplePhysEnvironment`): ~0-0.12 ms near.
  - One 40 m ray: 0.015 ms.
  - A new particle emitter per round per frame ran the DX12 descriptor heap out (crash). Now one emitter per round,
    restarted (`fxr`).
  - At 75 rounds/s into the ground: 2-4 impacts a round (~290 a second). Frame time falls off a cliff (8 fps, engine
    120-250 ms) past ~180 impacts a second. Hit sounds are suspect #1 (A/B: it held 41-61 fps with hit sounds off).
- **Caps now**:
  - `cfg.impactCap.maxRounds` 64: the oldest round in flight is dropped. This contradicts the user's goal.
  - `cfg.impactCap`: at most 2 impact effects per round and 6 per frame (a round at the ground made 2.7-5.9 impacts
    through ricochet and penetration chains); damage, penetration and ricochets themselves aren't capped.
  - Tracer emitters 128, held impact pieces 256.
  - Impact lights 6, impact effects 30 a second.

## Target (agreed direction; decisions below)
1. **Bounded cost.** A frame's cost is set by budgets, never by the number of rounds or hits. Every spawn goes
   through one budgeted manager: effect, decal, sound, light, emitter. Each manager has pools, priority by distance
   and importance, and merges events that look the same. Every cap hit is logged. Studios do this for miniguns;
   real belts carry a tracer every 4-5 rounds.
1b. **High fire rates are weapons of their own, not many single shots.** At 1000 rounds/s (the user's test,
   2026-10-15) the impacts stayed capped, but the shot itself (gunshot sound, flash cards, 2 flash lights, casing, a
   new tracer emitter, the gun animation) ran 1000 times a second: Lua 29 ms + engine 220 ms a frame, 4 fps. A
   minigun / CIWS needs a looping firing sound, one continuous muzzle flame and light, a tracer every Nth round (real
   belts: 1 in 4-5), casings as a stream effect, while each round stays a real round in the native simulation.
2. **Hot paths native, decisions in Lua.**
   - Native: the round simulation (all rounds in arrays; rays batched, queued to the physics thread if KCD2's
     physics allows), tracers, effect and sound dispatch.
   - Lua gets events (hit, near miss) and decides damage, AI reactions and settings.
   - "Unlimited bullets" = a round costs microseconds. Far or idle rounds are stepped less often, never removed.
     The real ceiling is the ray budget per frame: measure it.
3. **A typed binding instead of text.** The plugin registers real Lua functions through KCD2's script system (numbers
   in, results out, errors loud, a version check), and the `[DN]` text bridge retires.
4. **Hot-reloadable native code.** A small loader `.asi` (the hooks, rarely changed) plus a core DLL it can unload and
   reload on a command, with state handed over. Native changes then don't cost the user a restart, which was the
   objection to moving work into C++.
5. **Measured.** Every step is measured before and after:
   - `deagle_stress`, including its A/B mode, plus synthetic per-subsystem benchmarks (N effects / sounds / decals a
     second at a fixed spot), so one subsystem is timed without ballistics noise;
   - the soak line (docs/PERFORMANCE.md);
   - perf checks in `tests/run_all.sh`.

## Resources
- CryEngine's online documentation (WebSearch / WebFetch), including its C++ API reference pages for the engine
  interfaces (particles, decals, lights, physics, script system).
- The official CRYENGINE GitHub repo is NOT open: it needs a GitHub account linked to a CryEngine account, and the
  user's GitHub can't be linked right now (2026-10-15). Don't wait for it. Use the docs, the public headers of older
  CryEngine SDKs and mod kits (KCD2's engine descends from that line), and above all the game binary.
- KCD2 runs Warhorse's own fork of an older CryEngine (WHGame 1.5.6): docs and headers are a map, not proof.
  Confirm every offset and behaviour in the game binary (the rea reverse-engineering tools; MODLOG has the method)
  and in game.

## Answers to the first questions (Claude 5, 2026-10-15; static reverse engineering of WHGame 1.5.6, none run in game yet)
Full notes with addresses, offsets and struct layouts: `docs/whgame_re/` (one file per question). "Static" = read
from the binary with capstone; every claim still needs its in-game probe (migration step 1).

| Question | Answer | Confidence | Notes |
|---|---|---|---|
| pfx2 / GPU particles? | **Absent.** No pfx2 class, no .pfx asset in 109 paks; `e_ParticlesConvertPfx1` and `r_GpuParticles` are registered but never read; GpuParticles shaders were never compiled. pfx1 only. Useful instead: `IParticleEmitter::EmitParticle` (vt +0x2C8) fires extra particles from one existing emitter, and `IParticleManager::CreateEmitter` (+0x68) builds one without XML. | ~95 % | particles_pfx2.md |
| Bullet holes without a limit? | Lua decals are **already deferred** (`e_DecalsDeferredDynamic` 1: no mesh clipping). Two real limits: the decal manager's ring `CDecal[512]` advancing 2 per decal (~256 live, oldest overwritten whatever `holeLife` says), and the renderer draws at most `r_deferredDecalsMaxLimit` 1280 deferred decals a frame (the rest dropped silently). The in-game A/B (200 holes/s = 4 fps, builds ~1.5 s, clears ~1 s) disagrees with "creation is cheap": the best static fit is the deferred-decal stage re-binding every live decal each frame while new ones shift the order (0x774d24). Our own pool of `CDecalRenderNode`s (CreateRenderNode(7), moved with SetMatrix) bypasses the 256 ring; 1280 a frame stays the draw ceiling. | medium-low on the cost cause | decals.md; probe: `r_deferredDecalsMaxLimit 0` while firing |
| Lights without entities? | **Yes**: `C3DEngine::CreateLightSource` (+0x530) gives a light render node, no IEntity, no physics; moved with SetMatrix (+0x80), registered with RegisterEntity (+0x130). There is no per-frame "add light" call outside the render pass. Hard ceiling: the tiled light list keeps **255** per frame (probes and ambient first, ours last, the rest dropped silently); `e_DynamicLightsMaxCount` is never read. So: a pool of ~16-24, hits merged. | high (static) | lights.md |
| Hits by the drawn material? | **Yes**: the Lua ray result already carries `hit.renderNode` (the CBrush). Native: node +0xB0 GetEntityStatObj, +0x130 GetMaterial, then `CStatObj::RayIntersection` (+0x288) returns the drawn triangle's sub-material and surface id. CPU triangle loop, no acceleration structure: only for suspicious surfaces. Unknown: whether the pot's render sub-material has a surface type (else map by sub-material name, in a table, logged). | high (static) | render_material.md |
| Queued / batched rays? | **Queued yes, batched no.** `rwi_queue` = 0x800 works: the physics thread ("Physics", on by default) traces the whole queue before each step, results arrive on the main thread 1-2 frames later via the params' OnEvent. `IPhysicalWorld` = gEnv+0x30, RayWorldIntersection vt +0x118 with a 0x70-byte SRWIParams. A synchronous ray from our own thread is legal but takes the same locks as the game's rays. | high (static) | rays.md |
| Script registration (typed binding)? | `IScriptTable::AddFunction` (+0xB0) with a plain C callback `fn(pH, data, size)`; table from `IScriptSystem::CreateTable` (+0x68), published with `SetGlobalAny` (+0xF8); params via IFunctionHandler +0x38, results +0x40..+0x58. No new byte patterns. **KCD2's Lua is PUC 5.1 with 32-bit float numbers**: entity ids must travel as handles, positions past ~4 km lose millimetres. Errors: return `nil, msg` and let a Lua wrapper raise it (luaL_error longjmps over C++ frames). | high (static) | script_binding.md |
| Loader / core split? | Loader `.asi` keeps MinHook, patterns, hooks and the Lua C entry points; each hook calls the core through one atomic table; the core DLL is copied before loading so the build can overwrite it; reload at the start of a camera step with no call in flight; state handed over as plain numbers in loader-owned memory (no std containers across, static CRT per DLL); version check, loud failure, hooks then call only the originals. RoN's FMOD stays loaded (no 531 ms bank reload). | design | loader_core.md |
| How do other games do it? | Native arrays + fixed pools (Dagor tracers 2048 with 15 % kept for the player; CryEngine GameSDK 96 tracers, oldest dropped); queued / batched rays (CryEngine `rwi_queue`, Unity `RaycastCommand`, Unreal async traces one frame late); simulation LOD (ACE3: full detail only for rounds that can matter to the player, 0.05 s steps); a tracer every 4-5 rounds; rapid fire = **a looped burst sound with a tail**, not one sound per shot (War Thunder sound docs, CryEngine 3 audio docs); bullet holes in a ring (Dagor 10,000). No primary source on Tarkov / Squad / Battlefield internals. | documented, sources in the file | other_games_projectiles.md |

## Decisions (Claude 5)
- Effects stay pfx1. Fidelity comes from baked RoN looks (as now) plus fewer, reused emitters: one emitter per effect
  type and `EmitParticle` per hit, not an emitter per hit.
- "Unlimited bullets" = no cap ever removes a round in flight. Cost is bounded by budgets on what a round *causes*
  (rays per frame, effects, sounds, lights, holes), never by deleting rounds. Far / unseen rounds step less often.
- Engine ceilings we accept as hard: 255 tiled lights, 1280 deferred decals drawn a frame. Our managers stay under
  them by merging and priority, and log when they would have gone over.
- High fire rates (1b) are a weapon mode: loop + tail sound, one continuous flash and light, tracer every Nth round,
  casing stream. Each round is still simulated.
- Typed binding first (it makes every later step cheaper and measurable), then the loader / core split (so native
  steps stop costing the user restarts), then the native round simulation.

## Migration plan (one step at a time, measured, ALL SUITES PASS after each)
1. **Probe build** (one plugin install, one restart): each probe a console command, read-only or self-cleaning.
   Probes: hook thread ids; `DN.add` via AddFunction; a queued ray; an entity-free light; the drawn material on
   the cauldron; 1000 native rays timed. Measure: each probe's log line, plus µs per call / per ray (QPC).
   Status: built (bd8a983, native/src/probe.cpp mine by agreement with Claude 4; snippets tools/probe/basic.lua,
   light.lua, mat.lua run over rcon). Installed (plugin 73be688c). **Measured 2026-10-17 (Claude 1 ran them):** a
   native ray 2.84 us vs a Lua ray 6.01 us (the Lua layer is ~half); `DN.add` 0.118 us vs a `[DN]` text line 0.587 us
   (5x); a queued ray delivered 12.29 ms later; entity-free light: create 18.9 us, move ~9 us; `deagle_stress 75` at
   60 fps: Lua 1.02 ms avg, 1.83 p99. The drawn-material probe failed (no renderNode on a rock, no hit on a wall at
   30 m; surface name passed wrongly): mat.lua fixed to dump the whole hit table, rerun pending. Lua commands and the camera step run
   on the same thread (26744): the core can reload at the start of a camera step with no Lua call in flight. Light
   visibility still to confirm.
2. **Typed binding** for `fxr`, `snd` / `ron3`, `kick`; `[DN]` kept until each moves. Measure: µs per call
   vs `[DN]`'s 6-10; `deagle_stress` Lua ms; run_perf gate "0 `[DN]` lines for moved commands".
   Plus a precise clock (Claude 4, MODLOG 1af7b4a: Lua numbers are float32, so `GetCurrTime` at 51602 s moves in
   3.9 ms steps and `GetCurrAsyncTime` in 0.24 ms after an hour). Never hand Lua an absolute time: `DN.dt()` is the
   frame step computed in double natively; `DN.now()` returns the 64-bit QPC tick as a lightuserdata (exact) and
   `DN.since(t)` the seconds since it (a small float32, ~µs exact) for timing a span once; round ages stay summed dts (360 float32 adds over 6 s drift < 0.1 ms; ~0.12 µs a call per round avoided). Measure: 1000 frames of `DN.dt()` vs QPC in the
   plugin log, max error < 1 µs; a 1 ms step added to a 10 s age is kept.
3. **Loader / core split** + `deagle_native_reload`. Measure: reload ms; held emitters / tracer slots equal before
   and after; a 60 min soak (handles, private MB flat); 20 reloads in a row without a crash.
4. **Budget managers** (owners build theirs): sounds (Claude 3), effects + entity-free lights (Claude 2), holes +
   tracers (Claude 4). Measure each alone with a synthetic benchmark (N a second at a fixed spot, no ballistics):
   frame p99 and engine ms at N = 30 / 100 / 300; plus `r_deferredDecalsMaxLimit 0` to split decal draw cost.
   Already there: sound.lua HitBudget (d8be300), effects.lua FxBudget (b0bad31). One shared sampler: a `deagle_stress`
   spawner mode in debug.lua: `Deagle:Stress('spawn <name> <per s>')` (Claude 4). Sound's Lua manager is built
   (Claude 3, 8d856c0 / 1336ea1: `SoundBudget` per category hit / ricochet / casing, close-hit reserve, flesh never
   dropped; casing waits on one line in shells.lua). Spawners registered: `hole` (Claude 4), `fx_<effect>` and
   `light` (Claude 2, 3ec59cc), `hitsound`, `hitsound_budget`, `plate`, `gunshot3p` (Claude 3). First run (next game
   session, the game in front): hole 50 / 100 / 200 / 400, then hole 200 with `r_deferredDecalsMaxLimit 0`; fx_soil,
   fx_stone, fx_metal 30 / 100 / 300; light 300; hitsound 30 / 100 / 300. Still to do: the plugin's instance ring (step 3) and the effect /
   light calls (step 2).
5. **High fire-rate mode** (1b). Measure: `deagle_stress 1000` and `75` frame p99 and Lua ms (now 4 fps at 1000).
   Ready or Not has no loop / tail fire events (Claude 3: all 1015 weapon events are one per shot, the M249 too), so
   the loop + tail comes from a real CIWS / minigun recording the user approves by ear (private use).
6. **Native round simulation** behind a setting; Claude 1's suites are the contract; A/B against Lua; then
   `maxRounds` goes. Measure: µs per round, rays per frame, `deagle_stress 75 / 150 / 1000`, 1000 rounds alive.
   Contract from Claude 1 (2026-10-15): Lua keeps `Impact` / `ApplyHit` / `ArmorHit` / `Deflect`; native hands them
   the same hit table (pos, normal, entity, surface, dist, energy) and takes back goesOn + new r.pos / r.vel / r.skip /
   r.tumble. killcam.lua gets r.pos / vel / dist / hops / tumble / skip once a frame per round plus `KillCamHit` and
   done callbacks (its 16 m look-ahead ray stays Lua). Honour `impactCap.maxHops` (and `maxRounds` until step 6
   removes it) or hand those rounds to Lua. Gravity / drag in double inside the integrator (float32 positions are
   fine: run_ballistics tolerates ~1 cm at 100 m). Randomness from a seed Lua sets, so the tests stay deterministic.
   Static brushes give entity nil / partid nil in Lua; `hit.renderNode` is the brush (Claude 4, live).
7. **Static / dynamic split (the user's idea, 2026-10-15), ray LOD + `rwi_queue`.** The static world (terrain, brushes)
   never moves: at the shot (and after each bounce or exit) cast its whole path once as ~50 m chords (chord sag at
   440 m/s ~2 cm) against static + terrain only, and store "first static hit at time t". Per frame, only a ray limited to
   moving types (living, rigid, sleeping rigid), and none when no moving body is near the path (physics grid). Misses:
   static geometry that changes later (doors, breakables). Then far / unseen rounds on `rwi_queue`. Measure: main-thread ray ms, physics step ms (QPC around
   TracePendingRays), hit position error vs synchronous rays.
8. **Drawn-material hits** for suspicious surfaces. Measure: µs per test, first-test stall, the cauldron A/B.

## Throughput model (estimate, 2026-10-15; replaced by measurements as steps land)
Frame ms = (T0 + c x life x R) / (1 - s x R / 1000). R rounds/s; T0 6.5 ms (the sky scene without the gun); s = per-shot
cost (1 ms now: 1000 rounds/s = 250 shots a frame at 4 fps, 249 ms); c = per bullet in flight per frame (0.03 ms now);
life 0.57 s (range 250 m). Check: 75 rounds/s gives 119 fps, measured 88-124. Sky fire only: impacts are under their
own budgets (75 rounds/s at a wall: worst frame 24 ms, measured).

| Rounds/s at | 60 fps | 90 fps | 120 fps |
|---|---|---|---|
| now (cap 64 drops rounds past ~112/s) | 301 | 163 | 72 |
| plan, c 0.01 ms, s 0.02 ms (fast-fire mode, native, static/dynamic split) | ~1700 | ~780 | ~310 |
| plan, c 0.005 ms | ~3200 | ~1500 | ~610 |

s and c after the plan are guesses; step 1 measures the native ray, steps 5-7 measure s and c.

## How each step is built and proven
- Durable state lives in files (this doc, the step's spec, tests), not in a session's chat. Each step starts from a
  fresh session or subagent with a short brief: the step text, the files it touches, the contract tests, the
  measurement. Long contexts get compacted and lose detail; the brief is what survives.
- Owners keep their files. Opus for native, reverse engineering and the hot-reload core (crash-prone, hard to
  test); a cheaper model for mechanical work (Lua wrappers, test cases, docs). High effort for native and RE.
- Every idea ships behind a setting (on / off in the same session) and gets: mock tests + run_perf gates; an
  independent review; the in-game A/B/A/B (deagle_stress sky 75 / 300 / 1000, wall 75, the subsystem spawners)
  with frame p50 / p99, Lua ms, engine ms and native counters (rays a frame, bullets alive); for bullets, the hit
  points against the Lua version. The before / after row goes into docs/PERFORMANCE.md section 6.
- The user: one in-game session per batch, the Claudes drive it over rcon, the user keeps the game in front and
  judges only what needs eyes or ears.

## How to work here
- Read CLAUDE.md, README.md, TODO.md, MODLOG.md, docs/PERFORMANCE.md, docs/code_navigation.md first.
- Owners still edit their own files (CLAUDE.md "Who edits what"). The lead writes the new native core and the
  binding, and agrees each step with the owners. Message them by name (ListAgents): Claude 1
  `deagle-game-todo-notion` (ballistics, coordinator), Claude 2 `niagara-metal-impact-emulator` (effects), Claude 3
  `kcd2-deagle-9b` (sound, performance audit), Claude 4 `kcd2-deagle-4a` (flash, viewmodel, tracers, native).
- The user's rules:
  - Never launch or close the game. Install only while `tasklist.exe | grep -ci kingdom` is 0.
  - Ping Claude 2 before any `tools/live.sh reload`.
  - Show visual checks in `C:\Users\<user>\Desktop\Deagle Progress`.
  - No fallbacks or patch solutions.
  - Ripped assets stay private.
  - Keep Notion ("Needs you" first, prefixes 🧪 / ❓ / 🔧 / ✅) and the wiki artifact current.
  - Commit only your own hunks. ALL SUITES PASS.
  - Write to the user in short, plain steps that lead with the action.
