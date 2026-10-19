# Steam triage (2026-10-18) — entropy = largest section; queue: ~/re/queue_steam.sh (running, nohup)
- EldenRing: Game/eldenring.exe, 87.0 MB, .text 6.58 — queued (Ghidra ELDENRING, last; a tiny 2nd .text is 7.29 = protector stub; start_protected_game.exe/EAC ignored)
- Cyberpunk2077: bin/x64/Cyberpunk2077.exe, 59.9 MB, .text 6.55 — queued (CYBERPUNK2077)
- Hitman2: Retail/HITMAN2.exe, 36.7 MB, .text 6.48 — queued (HITMAN2; dx12Retail/HITMAN2.exe is a near-identical variant, not imported)
- BeamNG: Bin64/BeamNG.drive.x64.exe, 46.2 MB, .text 6.50 — queued (BEAMNG)
- ScrapMechanic: Release/ScrapMechanic.exe, 28.9 MB, .text 6.49 — queued (SCRAPMECHANIC)
- Arma3: arma3_x64.exe, 39.2 MB, .text 6.46 — queued (ARMA3; .bind section 7.96 is Steam DRM wrapper, small, .text readable)
- Titanfall2: Titanfall2.exe 1.76 MB is a launcher stub, .text 8.00 — SKIPPED; instead queued the real code bin/x64_retail/engine.dll (6.51), client.dll (6.48), server.dll (6.43) into TITANFALL2
- Payday2: PAYDAY2.exe, 16.3 MB, .text 6.60 — queued first (PAYDAY2)
- R6Siege: RainbowSix.exe, 113.8 MB, .UBX1 7.99 — SKIPPED (packed/encrypted; RainbowSixHelper.exe is the same size)
- TABS: TotallyAccurateBattleSimulator.exe (Unity, 0.65 MB) — Mono (Managed/Assembly-CSharp.dll 3.6 MB, no GameAssembly.dll) -> ilspycmd into ~/re/decomp/TABS (running first)
Order: TABS, Payday2, ScrapMechanic, Titanfall2 (3 dlls), Hitman2, Arma3, BeamNG, Cyberpunk2077, EldenRing. Logs: ~/re/logs/<name>.log, status.log.
