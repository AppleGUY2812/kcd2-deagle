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

## Target (proposed; the lead decides and records it here)
1. **Bounded cost.** A frame's cost is set by budgets, never by the number of rounds or hits. Every spawn goes
   through one budgeted manager: effect, decal, sound, light, emitter. Each manager has pools, priority by distance
   and importance, and merges events that look the same. Every cap hit is logged. Studios do this for miniguns;
   real belts carry a tracer every 4-5 rounds.
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

## Questions to answer first (research and reverse engineering)
- Does KCD2 still contain CryEngine's newer particle system (pfx2 / "wavicle": GPU particles, curves)? It's much
  closer to RoN's Niagara than the legacy pfx1 we bake into.
- Bullet holes without a limit: can we spawn CryEngine's projected (deferred) decals at run time in WHGame, or keep a
  pool of decal render nodes that are moved rather than created? Each `Particle.CreateMatDecal` clips the surface's
  mesh and allocates a new render mesh; ~200 a second took the frame to 250 ms, recovering within ~1 s of stopping, so
  creating them costs and drawing them doesn't (the user's A/B run 2026-10-15). It is capped for now
  (`cfg.holeBudget`); the user asked why drawing an image should need a limit at all.
- Lights without entities: from Lua a light needs an entity to hang on (`LoadLight` into a slot), so the impact light
  pool is 6 hidden BasicEntities moved to each hit. A plain BasicEntity got collision by default and they jolted the
  user at 75 rounds/s (fixed in d6fd352). Can native code hand the renderer a light per frame instead (CryEngine's
  per-frame render lights / `AddLightToRenderer`-style API in WHGame), with no entity, no physics and no pool to move?
- Hits by what is drawn, not by the collision tag: KCD2's campfire cauldron is one static brush whose collision is
  all `mat_wood` (5 rays around the pot, 2026-10-15), so an iron pot sounds and sparks like wood. Can native code read
  the render material at the hit triangle (the static brush's render node and its .mtl surface type) and use that
  when it disagrees with the physics surface? That would fix every mis-tagged prop at once.
- Can native code use queued or batched ray casts (CryEngine `rwi_queue`) in WHGame?
- Where are IScriptSystem's function-registration entry points in WHGame 1.5.6 (for the typed binding)?
- The loader/core split with MinHook: the loader owns the hooks, the core gets callbacks. How to hand state over on
  reload, and how to fail safely.

## Migration plan (proposal; one step at a time, measured, ALL SUITES PASS after each)
1. Typed binding for the hottest commands first (`fxr`, `ron3` / `snd`, `kick`), the text bridge kept until each one
   is moved and measured.
2. Loader / core split plus a reload command.
3. Spawn managers with budgets: sounds (Claude 3), effects and lights (Claude 2), decals and tracers (Claude 4).
4. Native round simulation behind a setting. Claude 1's ballistics tests (run_ballistics, run_penetration,
   run_killcam) are the contract; `Deagle:StepRound` / `MoveRound` / `Impact` / `ApplyHit` are the semantic references.
   A/B against Lua, then the round cap goes (now `cfg.impactCap.maxRounds`, default 64; at 75 rounds/s ~43 are alive
   with `range` 250 m, so it bites from ~110 rounds/s).
5. Effects fidelity: pfx2 if it exists.

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
