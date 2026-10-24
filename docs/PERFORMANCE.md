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

Tried, and **it was not the cvars**: `wh_cc_RequiredPostLoadReadyManagers` 35 -> 10 and `...ReadyTime` 0.5 -> 0.2
measured 7 s for a save load, but flipping back to the game's own 35 / 0.5 and loading again in the same session
measured **8 s** (14:42:38-14:42:46). Both values are back at the game's defaults and neither is in `user.cfg`.

The 22 s that started this hunt was the **first** load after the whole level had just loaded (shader PSOs, terrain,
13k entity loaders), not a property of save loading. A save load into a level that is already up is **7-8 s** with
22 mods on this machine. The honest lesson: measure the same event twice before crediting a change - the 22 s was a
different event masquerading as the same one.

### Boot to play, timed step by step (2026-10-23 18:44, game log lines stamped from outside + `tools/loadprofile.ps1`)

From "Loading last saved game" to `[Deagle] gameplay started` = **62 s** (engine start to the menu before it: ~42 s).

| step | time | game CPU (cores) | disk read | what it looks like |
|---|---|---|---|---|
| level open, paks, brush list | 18:45:15.5 -> 22.3 = **7 s** | 3-4.6 | 127-218 MB/s | parallel reading |
| `Terrain: Waiting for 14153 async loaders` (CTerrain::Load_T: the level's brush models, `wh_e_StreamCgfHeaders` 1, `e_StreamCgfMaxTasksInProgress` 120) | 22.3 -> 42.3 = **20 s** | 4.7 falling to 2.5 | 25-142 MB/s | parallel, neither CPU nor disk full |
| outdoor instances, entities, action map | 42.3 -> 54.2 = **12 s** | **~0.9** | ~90 MB/s flat | ONE thread reading one file after another |
| PSO precache (2.2 s) + up to the save load | 54.2 -> 01.3 = **7 s** | 1.2-3.5 | burst 222, then ~5 MB/s | mostly one thread |
| `Loading saved game` -> gameplay | 01.3 -> 17.1 = **16 s** | 4-6.5 | 150-253 MB/s | parallel; VRAM 3.5 -> 5 GB |

The disk never got near the 870 EVO's ~550 MB/s and the game never used more than ~6.5 of 12 threads, so it is
neither "the SSD is too slow" nor "the CPU is too slow" as such. About 19 s are one thread waiting on reads in a row.

**Memory is short:** with the game up, 1.3 GB of 16 GB is free and Windows' file cache holds 0.7 GB; 32.9 GB is
committed (page file on C:, 3.7 GB in use). The game has 10.6 GB committed but only 4.0 GB in RAM. Other RAM users
at the time: Brave 2.4 GB (23 processes), WSL 2.1 GB, Discord 0.8 GB, Steam's web helper 0.7 GB. Even in normal play
Windows reads 140-560 pages a second back from the page file.

**RAM is not why the load is slow (tested 18:50, Brave and Discord closed, 5.8 GB free at launch): 60.9 s** against
61.6 s before; boot to play 105 s against 104 s. Same steps, same lengths (terrain wait 19.9 s, the one-thread stretch
11 s, the save 14.8 s). The page file on C: was not read during the load (C: ~0 MB/s). The high "pages in" during
the terrain wait (~21,000/s) is exactly the E: read rate (21,000 x 4 KB = 82 MB/s): those are the game's own cached
file reads (Windows counts cache reads as page-ins), not the page file. WHGame.dll does not map files itself (no
MapViewOfFile / CreateFileMapping import).

What is left: the terrain wait and the one-thread stretch read through Windows' file cache at ~80 MB/s, while the
save load (the streaming engine, `sys_UncachedStreamReads`) reads at 150-280 MB/s. Next: `tools/threadsample.ps1`
(samples every busy thread's instruction + stack from outside, no injection) during a boot, to see whether those
threads are busy unpacking or waiting on reads.

### What the load's threads do (2026-10-23 19:07 boot, `tools/threadsample.ps1`, 50 samples a second)

Load 63 s again (19:07:52 -> 19:08:55). The main thread is the bottleneck: in the terrain wait ("Waiting for 14153
async loaders", 18 s) it does not wait (2% of its samples in a wait), it reads and unpacks files itself:

| main thread | terrain wait (18.3 s) | one-thread stretch (6.6 s) |
|---|---|---|
| zlib inflate (`FUN_1803da6b0` inflate_fast, `FUN_1803d9550` inflate) | 38% | 53% |
| crc32 of the unpacked data (`FUN_180612300`, table slice-by-4) | 7% | 10% |
| ReadFile (synchronous, Windows file cache) | 16% | 22% |
| everything else | ~39% | ~15% |

At least 7.5 s of the main thread go to zlib + crc32 in those two steps (a lower bound: the sampler sees a busy thread
on ~60% of ticks). The zlib is 1.2.11 (`inflateInit2_(strm, -15, "1.2.11")` in `FUN_1803d9224`, raw deflate of the pak
entries). The stream struct is zlib's own Windows layout (0x58 bytes, 4-byte uLong: avail_in +0x8, next_out +0x10,
avail_out +0x18, total_out +0x1c; an earlier note here called it modified, wrong). The whole-entry unpack is
`FUN_1807675a8(src, dst, srcSize, dstSize, &outSize, &result, heap)` (called by `FUN_180767540`, CryEngine's
ZipRawUncompress shape); the streaming engine's block jobs go through `FUN_1803d9064` -> `FUN_1803d9224` instead. The workers meanwhile pass work through an I/O completion port
(NtRemoveIoCompletion / NtSetIoCompletion) and sleep. In the save load the unpacking is spread over 5+ workers
(~1.1-1.4 s each), so it is not the critical path there.

Libraries on this PC's paks (Ryzen 5 3600, one thread, MB/s of unpacked data; Python bindings, and libdeflate again
built with MSVC exactly as in the plugin, `native/test/zbench.cpp`):

| | Objects-part0.pak (models) | terrain.pak | level.pak |
|---|---|---|---|
| zlib 1.3.1 (the game has 1.2.11) | 248 | 174 | 642 |
| zlib-ng | 417 | 214 | 923 |
| ISA-L | 426 | 244 | 1007 |
| libdeflate (gcc) | 585 | 302 | 1424 |
| **libdeflate (MSVC, the plugin)** | **589** | **304** | **1629** |
| crc32: zlib / zlib-ng / ISA-L / libdeflate (MSVC) | 2358 / 9737 / 11276 / 9951 | | |

Full race, 2026-10-23 (the user: "the search space is all the decompressor tools in existence"): same entries, same
CPU, one thread, every output checked against the zip's CRC (all 0 bad). MB/s of unpacked data:

| decoder | language | objects | terrain | level |
|---|---|---|---|---|
| **libdeflate 1.26** (MSVC, in the plugin) | C | **589** | **304** | **1629** |
| libdeflate 1.26 (Rust libdeflater / gcc -march=native) | C | 616 / 564 | 316 / 310 | 1597 / 1575 |
| sirocco 0.2 | Rust | 568 | 322 | 1437 |
| ISA-L igzip (python isal) | C + asm | 426 | 244 | 1007 |
| AMD AOCL zlib | C | 419 | 206 | 1113 |
| zlib-ng (python) | C | 417 | 214 | 923 |
| zlib-rs | Rust | 403 | 203 | 926 |
| Wuffs v0.4 | Wuffs -> C | 380 | 199 | 1084 |
| zune-inflate (allocates its output) | Rust | 338 | 214 | 727 |
| miniz_oxide | Rust | 310 | 169 | 820 |
| zlib-dougallj (gcc.amd64) | C | 322 | 193 | 1019 |
| zlib 1.3.1 | C | 248 | 174 | 642 |
| Go klauspost/compress | Go | 183 | 118 | 411 |
| Go compress/flate | Go | 117 | 83 | 379 |
| Zig 0.15.2 std.compress.flate | Zig | 95 | 44 | 236 |

libdeflate wins two of three outright; sirocco ties it on terrain.pak's tiny files (322 vs 316, within run-to-run
noise). Harnesses: scratchpad csrc/harness.c, rbench (Rust), gobench, zigbench; data dumped by zbench.py.

In game (2026-10-23 19:36 boot, verify mode): 63,834 entries / 5.7 GB went through the hook during the load, 0
mismatches; on the same calls zlib 16.6 s vs libdeflate 9.3 s unpacking and 5.3 s vs 0.75 s crc32 (CPU time over all
threads).

**Measured in game, fast mode (2026-10-23 19:49 boot, libdeflate on): 54.3 s** from "Loading last saved game" to
gameplay, against 61.6 / 60.9 s on the two boots without libdeflate and without the sampler: ~7 s (11%) faster, one
run each. Steps: open 6.3 s, terrain wait 15.3 s (was 18.3-20.0), one-thread stretch 8.0 s, pre-save 6.5 s, save 18.2 s
(was 14.8-18.4; this step varies by ~3 s between runs). The hook unpacked the same 63,834 files / 5.7 GB in 9.1 s of
CPU time (zlib on the verify boot: 16.6 s).

`/arch:AVX2` made no difference to the unpack (563 / 303 / 1601) and +12% to crc32, so the plugin builds without it.
GPU unpacking does not fit: entries are tiny (median 2.5 KB) and needed on the CPU, and DirectStorage's GPU path is
GDeflate, a different format (all 82 GB repacked).

Built: `native/src/fastzip.cpp` hooks `FUN_1807675a8` and the crc32 `FUN_180612300` with libdeflate (mode file
`deagle_fastzip.txt` next to the plugin: 0 zlib, 1 libdeflate, 2 verify both). Expected ~4-7 s off the 63 s; not yet
measured in game. Next step after that: unpack ahead on the idle threads (record the main thread's file order).

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

## Unpacking ahead (native/src/pakahead.cpp), 2026-10-23

- Recording boot (fastzip verify, so slow: 68.4 s): 284,662 ZipDir ReadFile calls; 63,832 whole deflate entries. In the
  load window 23,335 first reads of named paks, all matching their pak's directory (offset, sizes, crc32): 3.4 GB
  unpacked. Thread time inside ReadFile in the load: 29.7 s on the 3 loader threads (the terrain step's async
  loaders), 2.3 s on the main thread (verify mode doubles the unpack part).
- First unpack-ahead boot: 52.5 s (54.3 before). Workers had 17,710 entries / 2.1 GB ready from the menu on (read
  15.1 s + unpack 4.5 s of worker time, 0 bad), but only 937 stored entries were served: the serve check read the
  "decompress" flag before it was set, so every deflate entry failed. Fixed the same day. Steps: open 6.2, terrain
  14.9, one-thread 3.9, pre-save 11.6, save 15.9 s.
- Stream unpack hook (FUN_1803d9224) counts: of ~145k blocks, 0 were a first block with a fresh z_stream: ~119k
  later blocks of a file, ~18.6k with no z_stream (one-shot, a local stream), ~6.9k overlapping. The one-shot ones
  are the next candidate for libdeflate.
- Fixed boot (20:32): **47.3 s** (54.3 with libdeflate only, 61-62 with neither). 23,240 of 23,335 entries served
  (3.4 GB, 0 bad), the game came first on 505. Steps: open 5.9, terrain 14.7 (unchanged: not bound by these
  unpacks), one-thread 2.4 (8.0), pre-save 6.9, save 17.5 s. Worker time: read 46 s + unpack 7 s over 6 threads,
  all before or during the load at background priority. The streaming engine's zlib (~25 s of thread time during
  the load, FUN_1803d9224 blocks) is now the biggest unpack cost left.

## What the 47 s load does (boot sampler with stacks, 2026-10-23 20:51, tools/threadsample.ps1)

~60 samples/s = one thread 100 % busy. Load start = "Loading last saved game".
- open (0-6.4 s): main thread busy (FUN_1810368a8 30 % self, under CTerrain::Load_T FUN_18052c81c).
- terrain (6.4-22.7 s): two threads at 100 % carry it: 10032 = CryPak reads (CCachedFileData::GetData, ZipDir
  ReadFile) + the shader preprocessor (FUN_1805c06f8 "#ifdef / #elifcvar", FUN_1805bf950 "Couldn't find entry
  function"), 23 % of it in OUR memcpy (pakahead serve, stalled on the held buffers: paged out); 18740 =
  BucketAllocator (FUN_1804f7610) + FUN_1806e8aa0 / FUN_181e25ac0 (unnamed). The main thread is busy part of the
  time and waits in FUN_1804635d4 (a mutex-guarded queue push). Thread 22260 spins on an IO completion port
  (CCTPEndpoint, the local client-server network loop) at 100 % the whole time.
- onethread + presave (22.7-32 s): driver threads compile pipelines (dxcompiler, nvwgf2umx); then the main thread
  at 100 % (FUN_180b59d88 ...).
- save (32-47 s): the main thread is ~18 % busy, and 67 % of those samples are in FUN_18085f3c4: the cvar
  sys_SleepIfInactiveWH (default 1) "when the game window is inactive, sleep the main thread for 25ms every frame"
  (the user had tabbed out). The texture job threads (inflate via FUN_1803d9224 -> FUN_1803d93c0 in-place path)
  sit ~25 % busy; r_TexturesStreamingMaxRequestedMB is 4 MB, ...Jobs 32.
- Settings that cap threads: r_ShadersAsyncMaxThreads 1, r_PSOCompilationWorkerThreads 2. Set at registration by
  native/src/cvars.cpp from now on (see docs/DEBUG.md "cvars").
- RAM: 16 GB PC; at 21:12 WSL held 3.8 GB, Brave + Discord ~1.7 GB, and Windows had 3.8 GB free at game start
  (the game grows to ~6.5 GB during a load): unpacking ahead turned itself off.
- 21:36 boot with all five settings: **57.9 s** (unpacking ahead off: only 4466 MB free). Steps: open 6.4, terrain
  16.2, one-thread 5.0, pre-save 10.0, save 20.4 s. Thread names (sampler): the terrain/one-thread work runs on
  RenderLoadingThread (CryPak reads, our libdeflate unpack: 30-57 % of it with unpacking ahead off, the shader
  preprocessor); the 4 ShaderCompile threads stayed idle. Save step: Main 65 % busy (no throttle now), 7 JobWorkers
  ~43 % busy, half of it waiting, ~15 % zlib. "Network" (CCTPEndpoint) spins at ~90 % of a core the whole load.
  Kept: sys_SleepIfInactiveWH 0 while loading. Dropped: the texture-streaming caps (save step +5 s), shader and PSO
  thread counts (no gain seen). Unpacking ahead now runs from 4 GB free (cap free - 3.5 GB).

## 22:00 boot: XML ahead, and what holds the terrain step (2026-10-23)

- XML parse-ahead with the first list (3,258 files from the pak recording): 48.4 s, no gain (47.2 s before). The game makes
  35,894 XML loads per boot (9.1 s of thread time, 7.9 s of it outside `levels/`); the list held 9 % of them. Next build:
  the full recorded list (33,011 files), parsed while the main menu sits idle.
- kcd.log is buffered: its stamps are when lines reach the file. Exact boundaries come from the plugin's `timing: frame`
  lines and, for the terrain step, `loadwatch:` (native/src/loadwatch.cpp).
- Terrain step (17.3 s): the main thread only spins over the 14,153 loaders (`while (!empty) FUN_18354ff84`). Two threads
  run flat out until second 14: RenderLoadingThread (11.9 s busy; 35 % of it in the plugin's memcpy handing over
  unpacked-ahead files: 3.4 GB at 0.8 GB/s, stalled on loads from our buffers, unpinned with 6.8 GB free) and
  JobWorker_06 (12.4 s: allocator ~40 %, quest "concept" nodes, XML). Next build: a toucher thread pages the buffers
  back in ahead of the game, with copy speed and page residency in the `pakahead:` status line.
- Step 5 ("apply save" + the frames after it) belongs to the step-5 session (kcd2-deagle-66) from 22:10.
