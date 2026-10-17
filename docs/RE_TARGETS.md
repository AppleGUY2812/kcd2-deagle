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
