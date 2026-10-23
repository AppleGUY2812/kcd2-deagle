# Performance: budgets, method, tools, findings

Started 2026-10-15 after two bad bugs in one day (a 5-9 ms engine query per frame, and a new particle emitter per
round per frame that crashed KCD2 out of DX12 descriptor memory). The user asked for an audit "using professional game
optimization auditing techniques". Keep this file current: every per-frame or per-round change is measured against the
budgets below, and every finding keeps its status here.

## 1. Budgets

The frame is 16.67 ms at 60 fps and 6.94 ms at 144 fps. One frame over budget is a visible hitch, so budgets are
**frame time**, never fps, and they hold for the **worst** frames (p99), not only the average.

| What | Budget (main thread, worst scene) | Why |
|---|---|---|
| The whole mod (Lua + plugin) | ≤ 1.0 ms average, ≤ 2.0 ms p99 a frame | Unreal gives all skeletal animation 1.0 ms of game thread by default [1]; script GC should never take more than 1-2 ms of a 16.7 ms frame [2] |
| Entity-system queries (`GetEntitiesInSphere`, `GetEntitiesByClass`, ...) | **0 per frame**, 0 per round | each costs 5-9 ms in KCD2 at any size (section 3) |
| New particle emitters | ≤ 1 per round (reused, moved), ≤ 256 held | a new emitter per round per frame exhausted the DX12 descriptor heap (section 3) |
| Lua garbage | ≤ 1 KB a frame idle, ≤ 15 KB a frame shooting | at 60 fps that is 60 KB/s idle, 0.9 MB/s shooting; LuaJIT's incremental GC pays for every KB inside our functions [2][3] |
| Rays (`Physics.RayWorldIntersection`, 0.015-0.02 ms) | ≤ 2 per round per frame, ≤ 25 per frame for everything else | measured cost × the 64-round cap |
| Plugin commands (`[DN]` lines, ~6-10 µs each) | ≤ 3 per idle frame; per round only the tracer's one | string.format + the plugin's parse |
| Live sounds (Ready or Not's FMOD) | under -60 dB: never started | its Studio update runs on the game's main thread (SYNCHRONOUS_UPDATE) |

Sub-budgets for the 1.0 ms: rounds in flight 0.4 ms, near miss 0.1, effects / holes 0.15, sound 0.15 (plugin update
included), viewmodel / HUD / motion 0.2. These are our choice (anchored on [1][2]); revise them with measurements.

## 2. Method (what studios do, fitted to a Lua + plugin mod)

1. **Budgets first** (section 1), then measure against them. Report p50 / p95 / p99 / max; never only averages. Use
   p99-99.9 and capture enough frames (with 2000 frames the 99.95th percentile is a single frame); a stutter shows as
   a "hockey stick" in the percentile curve [4].
2. **Instrumented zones.** Wrap each entry point and keep count, total and max per 0.5 s window, plus the
   `collectgarbage("count")` delta (a drop means a GC step ran inside). `tools/perf/all_on.lua` wraps every function
   reachable from `_G`. **Timer caveat:** `System.GetCurrAsyncTime` is float seconds since the timer started; its
   resolution is ~0.24 ms after 34 min of game and ~1 ms after 2.3 h [5]. Average many calls; time hot plugin code with
   QueryPerformanceCounter (`ron: update ... us` in the plugin log).
3. **CPU- or GPU-bound?** PresentMon logs MsCPUBusy / MsCPUWait / MsGPUBusy per frame with no change to the game
   (`PresentMon --process_name KingdomCome.exe --output_file x.csv --timed 30 --terminate_after_timed`) [6].
   GPU work (lights, particles' fill) doesn't show in Lua timings at all.
4. **Fixed scenarios, A/B/A/B.** Same save, same camera, ≥ 60 s each, mod effect off and on alternating (OpenMW's Lua
   work used A/B/A/B [7]): idle with the gun out; a town crowd (Kuttenberg); infinite-ammo spray at the sky
   (`deagle_stress`); spray at a crowd; impact / decal spam at the test wall.
5. **Attribute with the engine's own counters** that KCD2 kept (section 4): `p_profile`, `p_profile_entities`,
   `e_ParticlesDebug`, `r_DisplayInfo`, `memstats`.
6. **Hard caps, logged.** Pools of fixed size; when full skip the spawn (particles) or steal the quietest / furthest
   (sounds) [8][9]; log every time a cap is hit (rate-limited).
7. **Soak.** 60-120 min with a log line every 30 s: Lua KB, held emitters / decals / voices, the process handle count
   (GetProcessHandleCount), private bytes, VRAM CurrentUsage against Budget (QueryVideoMemoryInfo) [10]. After the
   warm-up every slope must be flat. This is the check for the descriptor / handle class of crash; the DX12 shader
   heap holds 1,000,000 descriptors [11].
8. **Gates in the tests.** The mock can't time anything, but it counts what costs: `tests/run_perf.lua` (in
   `tests/run_all.sh`) fails on entity queries per frame, new tracer emitters per round per frame, room-check rays
   while standing still, echo weights resent once settled, more than 3 plugin commands an idle frame, more than one AI
   sound per hit, and Lua garbage over its gate (idle and shooting, printed per function). Riot catches ~1 ms
   regressions only after filtering noise and timing sub-frame zones [12]; the mock's counts have no noise.
9. **Record every build** in section 6: scenario, frame p50 / p99, mod ms average / max, garbage KB a frame, peaks.

## 3. Known costs in KCD2 (measured)

| Call | Cost | Measured |
|---|---|---|
| `System.GetEntitiesInSphere` | 5.4 ms at 3 m, 8.8 ms at 300 m: **any** radius | 2026-10-15, town, wrapped (MODLOG) |
| `System.GetEntitiesByClass` ("NPC", "Player") | 6.3-6.9 ms | 2026-10-15 (Claude 4) |
| `Physics.SamplePhysEnvironment(pos, r, ent_living)` | ~0 ms at 3 m, 0.12 at 30 m, 1.9 at 300 m | the physics grid (Claude 4); worn items have no physics |
| `Physics.RayWorldIntersection` 40 m | 0.012-0.015 ms (0.02 in town) | `tools/perf/engine_costs.lua` |
| `CryAction.GetWaterInfo` | 0.009 ms | MODLOG |
| The room check (21 rays) | 0.3-0.6 ms every 0.25 s, now only after the eye moves 0.5 m | `engine_costs.lua` |
| A Lua table per NPC per step (44 NPCs) | 2 ms a frame | FlyBy before 8092483 |
| A new particle emitter per round per frame, held 2 s | 140 → 70 fps, then `NCryDX12::CDeviceDescriptorAllocator::Allocate(3) failed: Out of device descriptor memory` | fdb7ed9 |
| A `%1` console command given several words | refused ("Too many arguments"): use `%line` | e4b9570 |
| Ready or Not's bank load (7 banks) | 531 ms, once, on the main thread | plugin log |
| The game's camera step | 2-3 calls per drawn frame | plugin log (0e756ab) |
| 150-290 Ready or Not sound starts a second | 8-12 fps (engine 76-120 ms), vs 41-61 fps without them | `deagle_stress 75 aim ab` (per-start µs: the plugin log, after the install) |

## 4. Tools

- `deagle_stress` (in game, docs/DEBUG.md): 21 s of sky fire, a `STRESS` line every 0.5 s with Lua vs engine ms.
- `tools/perf/all_on.lua` then `tools/perf/all_dump.lua` (via `tools/live.sh <file>`): every function's calls and ms
  per frame, idle vs shooting. The wrapper itself makes a table per call: read the ranking, not absolute ms.
- `tools/perf/engine_costs.lua`: single engine calls timed in the running game (read-only).
- `tools/perf/cvars.lua`: which profiling cvars KCD2 kept.
- `tools/loadtime.py`: the real boot / level / save-load / fast-travel / Apse-open times, read off `kcd.log` (and the
  plugin log's `invtime` lines). `python3 tools/loadtime.py` = the running session, `--all` = every kept log. It needs
  `log_IncludeTime 1` (the plugin sends it at start; `tools/live.sh cmd "log_IncludeTime 1"` in a running game), and
  the Apse rows need `deagle_invtime` on. `tests/test_loadtime.py` keeps its parsing honest.
- `tests/run_perf.lua`: the gates (section 2, step 8) and a garbage split per function.
- The plugin log (`deagle_native.log` next to the exe): `ron: update <avg> us avg, <max> max ...` every 3000 updates.
- PresentMon for frame percentiles and CPU / GPU split [6].
- CryEngine cvars KCD2 has (2026-10-15): `r_DisplayInfo` (1 = fps, tris, draw calls, memory; 3 = fps and ms only),
  `r_Stats`, `profile` / `profile_peak` / `profile_filter` / `profile_allthreads`, `e_ParticlesDebug` (with
  `r_DisplayInfo 1`), `p_profile` / `p_profile_entities` (physics step ms, per-entity top 16), `ai_DebugDraw`,
  `memstats`, `sys_enable_budgetmonitoring`, `e_DebugDraw`, `sys_streaming_debug`. Gone: Statoscope
  (`e_StatoscopeEnabled`), `sys_budget`, `r_ProfileGPU`, `r_GPUTimers`, `e_ParticlesPoolSize`, `s_DrawAudioDebug`.
  In retail CryEngine builds the frame profiler is compiled out (USE_FRAME_PROFILER is non-release only), so
  `profile` may exist and show nothing: check before relying on it [13][14].

## Soak line (one every 30 s of real time, during a 60-120 min session)

One kcd.log line, `[Deagle] PERF` (plugin side: Claude 4's `perfstat`; Lua side appended by Lua), every field a number:

| Field | What | Source | Pass after the 10 min warm-up |
|---|---|---|---|
| `t` | minutes since the session started | Lua | |
| `fps50` `fps99` | frame time p50 / p99 over the last 30 s, ms | Lua (frame deltas, `System.GetFrameTime` or the VmTick delta) | p99 ≤ budget + 2 ms |
| `modms` `modmax` | the mod's own time a frame, average / max over 30 s | `deagle_stress` zones | ≤ 1.0 / ≤ 2.0 (p99) |
| `luakb` | `collectgarbage("count")` | Lua | sawtooth with a flat peak |
| `rounds` | rounds in flight | Lua | ≤ 64 |
| `held` | impact pieces the plugin holds | plugin | ≤ 256, flat |
| `traces` | tracer slots in use | plugin | ≤ 128, 0 when nobody shoots |
| `flashes` | flashes held | plugin | ≤ 9 |
| `voices` `real` `pending` | Ready or Not voices playing / real / sounds on their way | plugin, `RonStats` (ron.cpp) | flat; 0-ish when quiet |
| `holes` | `CreateMatDecal` calls in the last 30 s | Lua | |
| `handles` | process handle count | plugin, `GetProcessHandleCount` | flat (± 1 %) |
| `privmb` | private bytes, MB | plugin, `GetProcessMemoryInfo` PrivateUsage | flat (± 1 %) |
| `vrammb` `vrambudget` | VRAM CurrentUsage / Budget, MB | plugin, DXGI `QueryVideoMemoryInfo` (LOCAL) | usage < budget, flat |

A soak passes when, after the warm-up, no count keeps growing (fit a line through the 30 s samples: the slope times
the session length stays under 1 % of the value, or under 1 unit for small counts).

## 5. Findings (audit 2026-10-15)

Owners: 🟥 Claude 1 (ballistics, penetration, kill cam hooks), 🟦 Claude 2 (effects), 🟩 Claude 3 (sound),
🟪 Claude 4 (the rest of the Lua, the plugin). Status: ✅ fixed (commit), 🔧 queued, ❓ to measure first.
"Install" = needs the plugin installed while the game is closed.

| # | Finding | Cost | Where | Owner | Status |
|---|---|---|---|---|---|
| 1 | Near-miss check: `GetEntitiesInSphere` per round per frame | 6-9 ms a call | near miss | 🟪 | ✅ 60a1d1e, 268165f, 8092483, 243820f (physics grid per step) |
| 2 | Tracers: a new emitter per round per frame, held 2 s (up to 1024) | 140 → 70 fps, DX12 crash | tracer / plugin | 🟪 | ✅ fdb7ed9 (one emitter per round, `fxr`) |
| 3 | `HideGear`: `GetEntitiesInSphere` every 1 s with the gun out, idle too | a 5-9 ms stutter every second | VmFollow | 🟪 | ✅ 243820f (at first person + once 1 s later) |
| 4 | `NearMissList`: a 300 m sphere query every 2 s while rounds fly, on the trigger frame | 6-10 ms hitch every 2 s | near miss | 🟪 | ✅ 243820f |
| 5 | "auto" tracer effect picked from raw frame time: crossing 20 / 33 ms replaced every round's emitter | emitter churn near 50 fps | tracer | 🟪 | ✅ 412a9c5 |
| 6 | Plugin tracer slots (64): over 64 rounds the evicted round took a slot back each frame | emitter churn at > 11 rounds/s | plugin | 🟪 | ✅ 0e756ab (128, no eviction), install |
| 7 | Camera step runs 2-3x per drawn frame: all per-frame plugin work doubled (motion dt spike, flash frames at half length) | 2x plugin cost, sway jitter | plugin | 🟪 | ✅ 0e756ab; the user after the install: "the gun feels the same which is good" |
| 8 | Held impact effects: ring of 1024 (the crash count), a flat 2 s each | up to several hundred alive in slow motion | plugin / effects | 🟪🟦 | ✅ 0e756ab (ring 256, `fxs ... [hold s]`), a34cb45 (each effect's own longest layer + 0.15 s, 0.40-2.15 s; tools/fx_hold.py, kept current by run_all) |
| 9 | A save load kept held effects and tracer slots that the load may have freed | crash risk | plugin | 🟪 | ✅ 0e756ab, install |
| 10 | Log lines (fprintf + fflush, 10-50 µs) in hot paths: hitreact every hit, freecam nudge every killcam frame, casespin, "ron: didn't play" | 0.01-0.5 ms a frame | plugin | 🟪🟩 | ✅ 0e756ab, 61b73f7 |
| 11 | Plugin command parsing (`sscanf_s` of 10 fields) | 6-10 µs a command | plugin | 🟪 | ✅ `fxr` with strtol/strtof (0e756ab); 🔧 batch the tracer lines |
| 12 | Ready or Not's Studio update ran every camera step (2-3x a frame) on the main thread; inaudible far sounds still got voices | est. 0.2-2 ms a frame shooting | ron.cpp | 🟩 | ✅ 84d806e (once a frame, < -60 dB dropped, timed in the log), install |
| 13 | Sound travel queue erased from the middle | O(n·k) | ron.cpp | 🟩 | ✅ 84d806e |
| 14 | Room check: 21+ rays in one frame every 0.25 s; EnvFade rebuilt a 12-key table every frame | 0.3-0.6 ms every 0.25 s | sound | 🟩 | ✅ a5f5f5b (rays only after moving 0.5 m; nothing sent once settled) |
| 15 | One AI sound per layer of a hit (armour + flesh + ricochet) | each re-runs the AI's hearing | sound | 🟩 | ✅ 61b73f7 |
| 16 | Flight step 0.02 s: 2-3 rays per round per frame exactly when the frame rate drops | ~2x rays at < 50 fps | ballistics | 🟥 | ✅ 66e69e0 (0.05 s steps; water check only on falling steps) |
| 17 | Tick dt uncapped; an error stopped the chain and stale rounds caught up 6 s at once | +17 ms after a hitch; up to 300 rays per round | ballistics | 🟥 | ✅ 66e69e0 (dt ≤ 0.1 s, a pcall per round) |
| 18 | No cap on rounds in flight (life 6 s: 90 at 15 rounds/s) | everything per round scales | ballistics | 🟥 | ✅ 66e69e0 (64) |
| 19 | `ArmorOn` scanned the whole inventory every NPC hit | est. 0.1-0.4 ms a hit | ballistics | 🟥 | ✅ 66e69e0 (cached 2 s) |
| 20 | 8+ see-through hulls: the 9th spawned a missing "pass" effect plus a back-ray | wasted rays | ballistics | 🟥 | ✅ 66e69e0 |
| 21 | Kill cam: an extra 17 m ray per straight round past 40 m every frame | ~0.9 ms a frame at 45 rounds | kill cam | 🟥 | ✅ Claude 1, 2026-10-15 (every other frame, not while climbing) |
| 22 | Rounds: ~20 tables per round per frame (MoveRound, FlightHit) | was 75.8 KB a frame (30 rounds) | ballistics | 🟥 | ✅ b710fc4 (in place) |
| 23 | `TracerBullet`: ~1.5 KB garbage per round per frame (vector tables, 10-float format) | 44 KB a frame at 30 rounds | tracer | 🟪 | 🔧 plain numbers, `%.2f` (after the file split) |
| 24 | FlyBy: a near-miss AI message per NPC per round (15/s per NPC); exact check = 2 engine calls per NPC per round per frame within 32 m | est. 1.5-2.5 ms a frame down a busy street | near miss | 🟪 | 🔧 per-NPC gap 0.5-1 s, per-frame cache |
| 25 | Idle garbage 3.97 KB a frame: MotionTick (closure + history table, `vel` sent every frame), VmPlace twice a frame (vm.OnUpdate and VmTick), FlashFollow ~10 engine calls a frame while a flash burns, CaseTick, DrawHud tables | budget 1 KB | viewmodel / motion | 🟪 | 🔧 |
| 26 | `deagle_stress` double-counts EnvTick / MotionTick / DrawHud inside VmTick and misses VmPlace via OnUpdate | wrong numbers | stress | 🟪 | 🔧 |
| 27 | Path drawing: shifts 600 entries per line, 600 pcall DrawLines a frame; PathMark builds 9 tables a hit even when off | 1-3 ms a frame while on | debug | 🟪 | 🔧 ring index |
| 28 | Holes: life 300 s, one per ricochet skip; `CreateMatDecal` never timed; the pool fills in a minute at 15 rounds/s | ❓ | holes | 🟪 | ❓ time it at the test wall; then 60-120 s, no hole past ~80 m |
| 29 | Far impacts (300-1000 m) get the full effect + hole + sound | wasted pool slots | effects / holes | 🟦🟪 | ✅ effects: none past 250 m x the debug zoom (a34cb45, gated in run_perf); ✅ holes: none past 100 m x the debug zoom (+0.5 m slack), 2026-10-22, tests/run_hits.lua; the two draws still happen so the physics / golden trace is unchanged (sounds: ✅ #12) |
| 30 | Muzzle flash lights: 2 per shot, 40 m radius (GPU) | est. 0.2-0.5 ms GPU per light | flash | 🟪 | ❓ compare the look at 20 m (the user approved 40) |
| 31 | Casing bounce sounds: up to 3 per casing | 45-90 sound starts/s | shells | 🟪🟩 | ✅ 38ad0fb (clinks through `soundBudget.casing`: 12 a second, burst 6, merged within 0.3 m / 0.1 s) |
| 32 | A plugin without `fxr` falls back to the crash path (an emitter per round per frame) | crash | tracer | 🟪 | 🔧 no tracers + a "restart the game" note |
| 33 | Ready or Not's banks load on the first command (531 ms, main thread); `ronload` sample data loads inside the next update | one-time hitches | ron.cpp | 🟩 | ❓ load on a worker thread at plugin start |
| 34 | RoN mixer blocks 256 x 4 (~188 wake-ups/s, the full bus graph each) | a core share, off the main thread | ron.cpp | 🟩 | ❓ measure; 512 x 4 if it matters (it was set so the bang lands with the flash) |
| 36 | Hit sounds under a 75 rounds/s CIWS: 150-290 Ready or Not starts a second (each living 1-3 s, its engine on the main thread) | 8-12 fps, engine 76-120 ms; 41-61 fps with hit sounds off (`deagle_stress 75 aim ab`) | sound | 🟩 | ✅ d8be300, then one budget per category (`soundBudget`: hit 30/s, ricochet 10/s, casing 12/s; merge within `near` m; far sounds leave a reserve for close ones; your gunshot, handling and hits on people never dropped); the plugin times each start (`ron: update ... sounds started, us avg / max each`), install |
| 37 | Casings at 250 rounds/s: ~4 a frame thrown from one port spot, inside each other; the rigid-body solver | 60 -> 5 fps within 2 s, engine 135-217 ms (`deagle_stress 250 sky shot casebody,portsmoke`: the bodies, not the port smoke) | shells | 🟪 | ✅ ca3e288 (`eject.ghost`: in flight casings ignore each other through their own collision class bit, on the ground they collide again), verified live: FIRE all p50 17.3 p99 20.8 ms (Claude 1, 2026-10-17) |
| 38 | Our Lua per frame at 250 rounds/s, sky, casings fixed: avg 5.0 ms, p99 14.4 (budget 1.0 / 2.0). Rounds alone avg 2.0 (~64 in flight); tracers + flash + sound + hearing + anim ~1.8; casings ~1.2 (~0.3 ms a casing) | ~0.7 ms of Lua per shot, 0.03 per round in flight a frame | weapon, shells, tracers, flash, ballistics | 🟪🟥 | 🔧 per-function profile next; the high fire-rate mode (ARCHITECTURE 1b) and the native round sim (step 6) carry most of it |
| 35 | Not perf, found on the way: `deagle_testnpc` dummies still bleed; ArmorHit computes the same wound twice | | | 🟪🟥 | 🔧 |

In-game ray costs (step 7, 2026-10-22; `rounds stats` after 87 min and 396,244 steps): static 5.76 µs (9,682 calls),
dynamic 0.57 µs (396,244), living 0.70 µs (396,216), tracer 0.83 µs (206,736). At 250 rounds/s that is ~30 µs a frame
of rays and ~0.2 ms of tracer calls; the 4.9 ms Lua is viewmodel / motion, not rays. A physics-grid "any moving body near
the path" query would cost about the living query (0.70 µs), more than the 0.57 µs dynamic ray it would skip, so it isn't
worth adding; `rwi_queue` would save under 0.1 ms a frame at these costs.

## 6. Results per build

| Date | Build | Scenario | Frame | Mod | Garbage | Peaks |
|---|---|---|---|---|---|---|
| 2026-10-15 | fdb7ed9 + fxr working | `deagle_stress` (sky fire, 45 rounds + tracers) | engine 6.6-8 ms flat (was rising to 11.4) | Lua ~0.06 ms per round in the air | | 45 rounds |
| 2026-10-15 | plugin 0e756ab + 84d806e installed | `deagle_stress` (10 rounds/s, tracers) | 128-140 fps, engine 5.9-6.8 ms flat | Lua 0.6-1.0 ms; 1.6 ms at 46 rounds (over the 1.0 average) | | 49 emitters for 49 rounds |
| 2026-10-15 | d46d1ff (mock) | `tests/run_perf.lua` | | idle 0 entity queries, 0 rays; shooting 0 queries | idle 3.97 KB, shooting 74.9 KB a frame (TracerBullet 44) | 30 rounds |
| 2026-10-22 | step 7 split installed | `rounds stats` (87 min, 396,244 steps) | native rays ~30 µs a frame at 250 rounds/s | Lua 4.91 avg / 9.83 p99 (stress) | | static 5.76 µs, dynamic 0.57 µs, living 0.70 µs, tracer 0.83 µs |

## 7. Load and screen times, 2026-10-23 (first numbers with `log_IncludeTime` on)

Read with `tools/loadtime.py`. One session, so far (Ryzen 5 3600 / RTX 3070 Ti / 16 GB @ 3200 / NVMe, 22 mods):

| Span | Time | What it is |
|---|---|---|
| boot | **112 s** | launch (14:10:30) to Henry controllable (14:12:22), through the autosave |
| level `trosecko` | ~28 s | `Loading level trosecko` 14:11:18 to the level's action map 14:11:46 |
| save load (level already up) | **22 s** | `Loading saved game` 14:12:00 to `[Deagle] gameplay started` 14:12:22 |
| Apse inventory open | **319 ms** | key to fade-in, one open, with `invtime 2` + `wh_ui_ApseCameraChangeTime 0.15` |

The 22 s is the number to beat on "saves load slower": it is a save into a loaded level with every mod in, and it is
the first one measured with timestamps. Boot and level are one sample each. Fast travel has no sample yet.

### What the load is doing (sampled 2026-10-23, `tools/loadprofile.ps1`, 1 s rows)

The log is silent from `Loading saved game` to `[Deagle] gameplay started`, so the load was sampled from outside while
it ran (boot 14:29:27 -> controllable 14:31:02, 95 s):

| | during the load | after |
|---|---|---|
| game CPU | up to **5 cores** (peaks 5.0/s), then ~2.5 | 2.6-3.0 |
| disk read | **130-200 MB/s** sustained, spikes to 204 | 0.3-2 |
| GPU | 3-40 % | ~37 |
| VRAM | 2.3 -> **6.2 GB** | 6.2 GB flat |
| log | `Terrain: Waiting for 14153 async loaders...` then `DONE` | |

So it is mixed, not one bottleneck: a serialized stretch (entity deserialize, world build), a 14k-chunk terrain
stream, and 130-200 MB/s of pak reads. It is not VRAM-starved at the end (6.2 / 8.2 GB) and not GPU-bound.

**Hardware placement is the biggest lever and it is bad right now:** `E:` - where the game, its 93 GB of paks, the
workshop mods and the saves all live - is a **Samsung 870 EVO SATA SSD** (disk 0), while both NVMe drives are the OS
(`C:`, WDC SN550, 51 GB free) and data (`D:`, Crucial P3, 39 GB free). Neither has room for the 93 GB game; `F:` (2 TB
HDD) has 1.5 TB free to offload to. Moving KCD2 to an NVMe is worth more than anything in `user.cfg`.

Tried 2026-10-23, live: `wh_cc_RequiredPostLoadReadyManagers` 35 -> 10 and `wh_cc_RequiredPostLoadReadyTime` 0.5 -> 0.2
(the game waits for that many NPC characters to be ready before it hands control over). Measurement pending; revert to
35 / 0.5 if arrivals show empty or T-posed NPCs.

## Sources

[1] Unreal Animation Budget Allocator: https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-budget-allocator-in-unreal-engine
[2] lua-users, GC in real-time games: http://lua-users.org/wiki/GarbageCollectionInRealTimeGames
[3] Lua 5.1 manual 2.10 (incremental GC, pause / stepmul): https://www.lua.org/manual/5.1/manual.html#2.10 ; Lua Gems (presize, reuse tables): https://www.lua.org/gems/sample.pdf ; WoW, tables without garbage: https://warcraft.wiki.gg/wiki/HOWTO:_Use_Tables_Without_Generating_Extra_Garbage
[4] NVIDIA, analysing stutter with percentiles: https://developer.nvidia.com/content/analysing-stutter-%E2%80%93-mining-more-percentiles-0
[5] CryEngine 5 ScriptBind_System / Timer (GetCurrAsyncTime is float seconds): https://github.com/ValtoGameEngines/CryEngine
[6] PresentMon console: https://github.com/GameTechDev/PresentMon/blob/main/README-ConsoleApplication.md
[7] OpenMW Lua GC on a worker (1500 tables/frame, A/B/A/B): https://gitlab.com/OpenMW/openmw/-/merge_requests/5487
[8] Game Programming Patterns, Object Pool: https://gameprogrammingpatterns.com/object-pool.html
[9] FMOD 2.03 voice limits and stealing: https://fmod.com/docs/2.03/studio/advanced-topics.html ; MSFS voice budgets: https://docs.flightsimulator.com/msfs2024/html/4_Sound/Sound_Optimisations.htm
[10] GetProcessHandleCount: https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesshandlecount ; QueryVideoMemoryInfo: https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/nf-dxgi1_4-idxgiadapter3-queryvideomemoryinfo
[11] D3D12 hardware tiers (1,000,000 descriptors): https://learn.microsoft.com/en-us/windows/win32/direct3d12/hardware-support ; descriptor heaps: https://learn.microsoft.com/en-us/windows/win32/direct3d12/descriptor-heaps-overview
[12] Riot, the case of the missing milliseconds: https://www.riotgames.com/en/news/profiling-case-missing-milliseconds ; performance monitoring: https://www.riotgames.com/en/news/down-rabbit-hole-performance-monitoring
[13] CryEngine 5 profiling quick guide: https://www.cryengine.com/docs/static/engines/cryengine-5/categories/23756816/pages/35260143 ; physics profiling: https://www.cryengine.com/docs/static/engines/cryengine-5/categories/23756816/pages/26876703
[14] Lumberyard ProjectDefines.h (USE_FRAME_PROFILER non-release): https://github.com/aws/lumberyard/blob/master/dev/Code/CryEngine/CryCommon/ProjectDefines.h
Also: Unity profiling best practices (35 % headroom, CPU vs GPU bound): https://unity.com/how-to/best-practices-for-profiling-game-performance
