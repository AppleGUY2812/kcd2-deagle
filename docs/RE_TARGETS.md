# Reverse-engineering targets (2026-10-17)

Source of the list: user asked what to reverse engineer / document to advance the mod. CryEngine 5 source
(`Downloads\CRYENGINE_Source-release.zip`) is a naming/semantics guide ONLY: its `IEntity` vtable offsets do not match
KCD2's (GetPhysics 0x2a8 vs ours 0x278, GetSlotWorldTM 0x350 vs 0x2D8, GetCharacter 0x3c8 vs 0x328).

| # | Target | Needs Ghidra? |
|---|---|---|
| 1 | Why every Lua entity query costs 5-9 ms (GetEntitiesInSphere / ByClass) and a cheaper path. **User wants to discuss with the bullet-system / perf session (Claude 4 asked; Claude 3 owns docs/PERFORMANCE.md, Claude 5 the native round sim).** | yes |
| 2 | `System.AddCCommand` on an existing name keeps the first: command lines can't change without a restart | yes |
| 3 | Kill cam X-ray tint / depth-of-field hook (none found yet) | string search first |
| 4 | Morale `near_gunshot` setter, stat/skill XP (AddStatXP) not found in Lua | yes |
| 5 | A "CryEngine map" doc: each engine function we use, with verified KCD2 offset or "unverified" | partly |

Ghidra 12.1.4 is unpacked at `E:\Tools\ghidra\ghidra_12.1.4_PUBLIC`; `E:\Tools\ghidra\run_analysis.bat` imports
WHGame.dll (log `E:\Tools\ghidra\analysis.log`). NOT started. Estimate 3-8 h (unmeasured) with `GHIDRA_HEADLESS_MAXMEM=10G`,
so run it with KCD2 closed. After it: 1-3 h per question.

## Claude 4's answer (2026-10-17): Ghidra is NOT the bullet fix
- Entity queries are not a bullet cost any more (0 per frame / per round since 243820f). Two 5-9 ms hitches remain, both outside
  bullets: `RemoveStaleEntities` (once per load) and `HideGear` (entering first person, and 1 s later).
- Real bullet cost is per-shot work, about 1 ms/shot (sound, flash cards, 2 flash lights, casing, tracer, animation): 1000 rounds/s
  = Lua 29 ms + engine 220 ms per frame. Fix is design (fast-fire weapon mode, ARCHITECTURE.md rule 1b step 5), not RE.
  Per round in flight ~0.03 ms/frame; native round sim (step 6) fixes it. Decals: 200 holes/s on one spot = 4 fps.
- Profile first (ETW/WPR sampling during `deagle_stress 1000`), then decompile whatever tops it.
- Claude 5 already read the bullet path with capstone: `docs/whgame_re/` (rays, decals, lights, particles_pfx2,
  collision_copy, script_binding; RVAs). Ask Claude 5 before running Ghidra. A Ghidra project would still speed up its next questions.
- Only the binary can answer: which physics event fires when static geometry changes + `IPhysicalWorld::AddEventClient` offset (step 7);
  whether `IConsole::RemoveCommand` exists (= target #2).
