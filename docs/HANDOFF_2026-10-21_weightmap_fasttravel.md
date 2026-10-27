# Handoff 2026-10-21: weight map, G key, fast travel / load times

Read CLAUDE.md first (the user's rules: never launch/close the game, install only while KingdomCome.exe is not
running, show visual checks, keep Notion + the wiki artifact current, commit only your own paths).

## State
- Installed in game (pak + plugin, 2026-10-21 20:43): weight map icon buttons (gold, with tooltips), "All stolen" in
  the list view (gold row frames, press again to clear), Shift/Ctrl click via the plugin (`deagle_mods`), G fast
  transfer fix (console args arrive quoted: use `Deagle.Arg`), optimized map rebuild (timings logged as
  `[Deagle] fastmove: <screen> map N: X ms, B boxes, R redrawn`).
- NOT tested in game yet: all of the above. First ask: open inventory, press G on one item (should go to the horse);
  then read `fastmove: map` lines in kcd.log for real rebuild timings.
- Built, NOT installed: plugin frame-gap logger (`native/src/main.cpp`, "timing: frame N after X ms, camera moved").
  The build printed BUILT, but the check that the string is in `native/dist/deagle_native.asi` and the run of
  `tests/run_all.sh` were not done. Do both, then install on the game's close.
- Nothing is committed. Files: `tools/ui/weightmap/*` (common.as, total.as, fast.as, inv.as, tr.as, md.as,
  patch.py, patch_xml.py, build.sh), `src/Libs/UI/**` (build output), `src/Scripts/Deagle/fastmove.lua`,
  `native/src/main.cpp` (fastkey, mods, timing blocks only; the sound section belongs to the sound session),
  `loc/english/text__deagle.xml`, `docs/DEBUG.md`, `tools/points_ui.ps1`, `tools/gen_perklist.py`.

## Fast travel black screen (investigation, half done)
From WHGame.dll (decompile on the ProLiant: `/tmp/wh.c`, from `~/re/index/KCD2/WHGame.dll.c.zst`):
- Fast travel runs the skip-time system: `wh_pl_SkipTimeStep` 500 ms, `wh_pl_SkipTimeMaxFPS` 120, world time
  ratio up to 360x. The game is in `wh::C_PrecacheMode` (reason `PMS_SkipTime`) for the whole trip; the black end
  is reason `PMS_Fader`.
- After arrival the character system (`wh_cc_*`) wants `wh_cc_RequiredPostLoadReadyManagers` = 35 NPC characters
  ready for `wh_cc_RequiredPostLoadReadyTime` = 0.5 s, and builds them with `wh_cc_MaxSkinLoaderJobs` = 1 (one at a
  time). The "mostly NPC builds" hypothesis below is now **disproven** by the measurement.
- Do NOT turn on `wh_pl_FastTravelDebug`: the map stopped responding right after it was set (game kept rendering).
- `wh_ui_FaderProfiler` is on by default (saves fades over 2 s to Saved Games/kingdomcome2/FaderProfiler) but wrote
  nothing: the black screen is likely not a fader, or the profiler is stripped.

### Measured 2026-10-23 (long and short trip in the same session, 22 mods)
The user's question was whether the game loads the destination while the fast-travel figure walks across the map.
Instrumentation: the plugin logs every frame gap >= 100 ms and every camera jump >= 30 m
(`timing: frame ...` in `deagle_native.log`), plus `tools/loadprofile.ps1` sampling CPU/disk/GPU/VRAM once a second.

| | long trip (8670 m) | short trip (~330 m) |
|---|---|---|
| map open -> pawn animation | 14:55:44.9 -> 14:56:06.7 (21.8 s) | 14:56:19.5 -> 14:56:26.3 (6.8 s incl. load) |
| during the animation | disk **0-7 MB/s**, CPU ~3.2 of 12 threads, GPU 50-70 %, VRAM flat at 6.85 GB | - |
| black screen after arrival | 14:56:07.1 -> 14:56:19.6 (**12.5 s**) | 14:56:26.4 -> 14:56:29.3 (**~2.7 s**) |
| during the black | disk **145-158 MB/s**, CPU 7-8 threads, VRAM 6.85 -> 7.2 GB | disk ~90 MB/s, CPU 3.5-5.7 threads |

What this says:
- **Nothing is preloaded during the pawn animation.** The trip is a map-scene cutscene with the world paused; the
  SSD is idle for 21 s and then does all the work in 12.5 s. The user's read of it was correct.
- The black screen is **not a stalled frame or a stuck fader**: the frame counter advanced 6173 -> 7115 during those
  12.5 s of black (~75 fps), so the game is rendering behind the black while it streams and builds.
- Its length scales with how much has to be streamed (12.5 s long vs ~2.7 s short), i.e. it is the destination
  stream-in: roughly 1.9 GB at 150 MB/s on the SATA 870 EVO plus decompression (8 of 12 threads busy), not NPC
  clothing builds (`wh_cc_RequiredPostLoadReadyManagers` was back at the game's 35 / 0.5 for this test).
- So the practical levers are (a) get the engine to start that 12.5 s of work during the 21 s it spends idle,
  (b) make it cheaper (the game is on a SATA SSD at 150 MB/s; the two NVMe drives are the ones to move it to).

Next: sample the cvar values during a trip (rcon every 2 s) to find which "opti cvars" skip time flips
(`wh_pl_SkipTimeEnableOptiCVars` is 1 by default, `wh_pl_SkipTimeMaxUpdatesPerFrame` 45, `wh_pl_SkipTimeStep` 500),
then test the one that touches streaming. Report numbers, not guesses.

## Save loading slower (user's hunch: inventory)
- One measured load: "Loading saved game" 20:47:59 -> `[Deagle] gameplay started` 20:48:09 (10 s to scripts).
  No older baseline with timestamps exists. 22 mods are active (list in kcd.log); save files are about 1.9 MB.
- Next: with timestamps on, time a load with and without the Deagle mod (and with the big mods out), same save.
