# First save load: findings (current state only; replace, don't append)

Goal 20 s from Continue to control. Owners: steps 1-4 kcd2-deagle-5a, step 5 + first gameplay frame kcd2-deagle-66.
RE notes: ProLiant ~/re (KCD2.gpr), WHGame.dll 1.5.6; agent reports in the 5a scratchpad (re_step*.md, re_questearly.md).
Plugin: fastload.asi (its own mod since 2026-10-24: fastload.log, fastload_*.txt; native/src/fastload_main.cpp).
"Control" = the end of the first gameplay frames. Times in s after the first level file ("level loading").

## The user's path vs the test boots (M)
- **Best so far, boots 42 / 44 (2026-10-24 13:31 / 13:47, menu path, 1949 / 2902 MB free): 19.2 / 20.0 s** from the
  first level file to control, + 0.3-0.9 s for the Continue click (thread sampler: last menu frame -> first load code),
  so ~19.5-20.9 s from the click. Boot 42: terrain 3.0, level done 10.3, state 4 0.2, apply 4.8 (one 4.75 s main frame,
  kcd2-deagle-66's), gate 3.2. Quest graph replayed at the menu in 4.48 s and kept. Boot 40 before the replay: 22.7 s.
  One boot: +-1 s noise.
- Auto-load boots (bootrun.sh) are not the user's path: the load starts at once, so the menu parses 58 of 457 shaders
  (boot 41: 419 in the load = state 4 5.1 s; 30.1 s total) and NVIDIA NGX/DLSS starts in the first gameplay frame
  (~4 s). Compare test boots with test boots; bootfull.sh N is the user's path.
- The gate's texture tail was two mods (Checkers faces/body + hair: one deflated 4K .dds per texture, every mip request
  re-inflated the whole entry); repacked stored 2026-10-24 (tools/paks/repack_stored.py; originals in
  E:\Tools\deagle-dev\mod_backup): gate 11.0 -> 3.2 s.

## Where the time goes (boot 27, menu path, 33.0 s; boot 42 19.2 s in brackets)
| Step | s | What holds it |
|---|---|---|
| open + terrain | 0-3.7 [3.0] | terrain loaders (RAM-bound read-ahead) |
| objects + entities (Main) | -12.3 [10.3 with the next] | 35,889 entities 2.7 s, human anim sets 2.2 s, navmesh 0.7 s, CryPak lock waits |
| Main waits for the quest job | -13.7 [0] | job 13.7 s, one worker [built at the menu in 4.4 s and kept] |
| finish level | -15.7 | SmartObjectAnimations table 1.43 s, XGenAI global pass 0.55 s (re_stepEF) |
| state 4 | -18.1 [10.5] | PSO precache 2.4 s: 71 shaders parsed (~15 ms each, real CPU) + 15,989 PSOs |
| state 5 apply save | -23.4 [15.3] | Storm 0.86 s, concept BFS 0.35 s, RandomEvent project 0.4 s, XML 0.3 s, ~1.8 s scattered |
| settle gate | -32.1 [18.5] | textures 7.9 s, clothing 6.3 s |
| first gameplay frames | -33.0 [19.2] | 0.3 + 0.6 s |

## RAM is the system limit (M)
User's own loads vs free RAM at level loading: 2122 MB 33.4 s (boot 32), 1597 MB 38.5 s (U1), 1320 MB 48.3 s (U2,
2026-10-24 07:48). U2: state 4 took 11.7 s (2.7 s in test boots) with 969k page faults in that frame (250k in test
boots); 465 MB free at the gate. At launch 4.6 GB was free; pakahead filled to 2.1 GB at the menu, then shed 1.9 GB
and served 299 MB. Other RAM then: KCD2 4.6 GB resident (11.7 GB private), WSL 2.1 GB, Brave 1.0, Steam 0.5,
Jellyfin 0.45, Discord 0.43. The hitch line now splits a long frame's faults into disk reads / standby / fresh
(system-wide) to tell whether the game's own pages are read back.
16 GB PC (Ryzen 5 3600, 6 cores); WSL 2.4-3.7 GB. Read-ahead caches follow ramgov.h (read ahead while >= 1.5 GB free,
free the farthest below 1.2 GB, each item freed at most once). Anything that holds more RAM before the load (PSOs at the
menu, a longer XML lead) costs terrain time: boots 22-23 had ~500 MB less free and terrain 5.2-5.7 s instead of 3.7-3.9.

## Built and measured
- fastzip (libdeflate) + pakahead: 61 -> 47 s (2026-10-23 boots, older clock).
- xmlahead bounded lead (8d38d00): quest XML phase 5.0 -> 1.0 s (2.25 s since quests go first, see below).
- streamahead, 4 readers + RAM rule (a168e6d, d587a0e): terrain wait 3.4 -> 0.9-2.3 s, depends on free RAM.
- applyfast fog cull (d587a0e): map clouds vs fog of war 0.86 s -> 60 ms, 0 mismatches in 71k checks.
- applyfast facial DBA sort (2195bf8): lists arrive nearly sorted (10 breaks in 40k), 80-95 ms; gain not measurable.
- applyfast quests first (02c973b, on): quest job starts 3.1-3.3 s earlier; Main's wait 3.3 -> 1.4-1.9 s; state 4
  ~1 s earlier. Quests not yet checked in game.
- applyfast 3b, PSO precache during the load (e21c93b, default, fastload_psoahead.txt 2): handed to the RLT before
  Main's quest wait; state 4 2.4 -> 0.4-0.5 s (boots 29-30). Main now waits 1.7-2.2 s for the RLT's texture precache.
  Mode 1 (at the menu, on Main) froze the menu ~6.2 s: rejected. Since boot 31 it never fires in time: the quest job
  ends earlier (quests first), the RLT is still on its texture precache then, and later points come after the game's
  own sync. Next: PSO creation on worker threads (re_pso_parallel.md).
- applyfast 5, shaders parsed at the main menu (6a313dd, fastload_menushaders.txt 1): boots 34-36 parsed 457 at the menu,
  20 in the load: state 4 2.7 -> 0.2 s, but no gain on the total: state 4 + gate is 11.0-12.3 s in boots 32-37 and
  state-4 start -> control 17.3-18.7 s in every boot; textures are the long pole and take the time back. U2 (user clicked ~7 s after the menu) parsed only 110.
  re_pso_parallel.md: PSO creation is already on 9 job workers + 4 driver threads; the serial part is the RLT's shader
  parse (~17.6 ms each) and, under low RAM, its texture precache backlog (U2: 2.28 GB unpacked in state 4). Next if
  needed: parse on our own thread from launch, not paced by menu frames (R1), and the PSO job graph started from a
  plugin thread before the quest wait (R2, 2.1-2.7 s per boot of PSO jobs).
- questmenu (a918236, e6a864a; fastload_questmenu.txt 1 since boot 35): after event 0x24 the game's own job body builds
  the graph on a plugin thread (11.5 s, free RAM 2710 -> 2030 MB) and the load keeps it (mgr+0x378 = 0, body skipped).
  Concept messages at the menu during the build: 0x1, 0xc, 0x24; before the load: 0x38, 0x3a (none touch projects).
  Journal after the load identical to a normal load (boots 35 vs 36). Skips below 1200 MB free.
- questreplay (f9cf471, 9b6805e, f237548, 8a9ca19): the quest build replayed from fastload_questreplay_<level>.bin (29 MB;
  key: WHGame.dll, 72 paks / manifests incl. Workshop mods, the job inputs). Phase 2a calls the game's leaf functions in
  the recorded order (same resolver id order, #4838fed6bbbf53ef); phase 1 (21,808 quest XML files) is skipped and
  xmlahead no longer parses them (~250 MB less at the menu). Menu build 14.4 -> 8.43 -> 4.35-4.59 s (2a 2.0, ports 0.37,
  edges 1.35, activation 0.56). A load that builds its own (menu build not kept, a level switch): 6.26 s in the load
  (boot 45, fastload_questmenu.txt 2; it shares the CPU with the level load). A same-level reload during play runs no
  quest job at all (boot 43, ~6 s reload). Checked: questdump equal to the game's own build except the quest's random
  roll (as between two game builds); journal identical in boots 41-45. A failed replay drops the file, the game builds.
- animahead (f71b7da, 7f34635; fastload_animahead.txt 1): female.cdf + male.cdf through the game's LoadAndLockResources on a
  plugin thread at the menu, after the quest build (2.2 + 1.1 s there); the load's 30+ human skeleton loads hit the
  cache (0 ms). "Loading level" (FUN_180b8f748) waits for a running preload. **Never during the load**: boot 39 ran it on
  Main in the terrain wait and crashed (the global animation header array CM+0x280 has no lock; the terrain loaders on
  job workers add to it; dump: FUN_18083f874).
- skin cap (kcd2-deagle-66): wh_cc_MaxSkinLoadingInProgress 120 / JobQueueSize 80 (load): clothing 10.2 -> 7.2 s
  (did not repeat in every boot).

## Start early (user rule 2026-10-24: every step at launch / menu, before Continue)
| Work | Starts now | Status |
|---|---|---|
| unpack (pakahead) | launch +1 s | done; 2 GB cap: 17,650 of 23,338 files ready before the menu |
| ground meshes (streamahead) | launch +1 s | done; 512 MB held cap |
| XML parse (xmlahead) | menu idle (~launch +22 s) | capped 3,000 files ahead of the game: 4 workers idle most of the menu; level files wait for level loading |
| quest graph (4.4 s, replayed) | save list read (~launch +20 s) | done (questmenu + questreplay); kept by the first load |
| Main: human anim sets 1.9-2.2 s | menu, after the quest build | done (animahead): boot 40 |
| Main: navmesh ~0.7 s, terrain loaders / level open | in the load | kcd2-deagle-66 (plan below) |
| RLT: shader parse 4-8 s, texture precache, PSO | in the load | RE running: re_menu_render.md |
| save read, mapClouds/Storm, Settle textures at the save's position | in the load | asked kcd2-deagle-66 |
Limit: ~2.8 GB free RAM at the menu (boot 30); everything held early competes.

## Tried, no gain
- r_PipelineStatePrecacheMode 0: state 4 0.3 s but a 0.8 s freeze after control (shaders built on demand).
- Timer resolution: Sleep(1) takes 1.5 ms in the game (it calls timeBeginPeriod(1)); not the shader-parse cost.
- xmlahead workers at normal priority, quest job thread above normal: no change (boot 25).
- xmlahead list re-recorded in the quests-first order (boot 26): XML phase unchanged.

## Plan: 10 s from Continue (target since 2026-10-24 15:30; was 20 s)
Boot 57 (all current work, menu path): 18.3 s = LoadLevel 8.3 + state 4 0.8 + apply 4.7 + gate 3.4 + rest 1.1.
Shaving inside the steps reaches ~14-15 s at best (5a's LoadLevel items ~2.5 s, 66's apply ~1-1.5 s). 10 s needs whole steps
before Continue.
How Continue loads (S, 2026-10-24): KCD2's level changer FUN_180fc3868 (level manager states 1/3 -> 4) always runs the console
command `map <level>`: CCryAction::MapCmd FUN_180b8f748 ("Loading level") -> game context FUN_180b6e348 / FUN_180b8ee2c ->
BlockingConnect FUN_1817ad804 -> context establisher FUN_18066b548 -> load-level task FUN_18183ead4 -> CLevelSystem::LoadLevel.
Then states 5 (FUN_180fc3c98), 7 (event 0x1010), 8, 9. CryEngine's same-level quick load (CGameSerialize::LoadGame
FUN_18370f1f0 + FUN_18370fef4: reset 3D engine, physics, entities, no level load) exists but KCD2's changer never uses it;
the "QuickLoad" script binding is an empty stub (FUN_1811cda9c).
Measured 2026-10-24 (boots 59-61):
- KCD2 has its own load onto the loaded level: SaveGameManager FUN_1825bdc7c(mgr, user, save) compares the save's level
  with the loaded one; same level -> FUN_1825bda54 (the save read into the world, PostLoadGame, gate 0x3f93 "+25bdbab"),
  no map, no LoadLevel. A normal Continue: FUN_1825bd9ac (UI and level-manager state 5) -> FUN_1825bdc7c -> level manager
  (map, LoadLevel) -> state 5 -> FUN_1825bdc7c again, now the same level. Continue's save: user 0, save 100.
- Boot 61 (samelevel.cpp, `samelevel load` 20 s after control): the same save loaded again onto the loaded level: 6.1 s to
  control (apply 5.1 + gate 0.9), vs 17.7 s for that boot's Continue (map + LoadLevel 9.6, apply 4.4, gate 3.4).
  Screenshots before / after: same place and view.
- Boot 60 (the game's own wh_sys_AutoLoadLastSave): launch -> control 54.4 s vs ~48 s when Continue is clicked as the menu
  work ends: the load starts before the menu work, so every menu preload is lost (load 29.3 s, gate 8.4). Dropped as is.
- wh_sys_ForceMainMenu ("main menu over a +map level"): no reader found in the release build; the main menu over a loaded
  level is not a game feature to rely on. The pause menu is.
Route to 10 s ("ready at the menu", needs the user's OK: the game starts the load by itself): once the menu work is done and
the user hasn't started anything, the plugin starts the newest save's normal load and ends it in the pause menu; Resume =
control at once; another save on the same level = the same-level path (~6 s); Exit to main menu = the level unloaded, as
now. Launch -> ready stays ~48 s, so shaving the steps still counts.
The game's pieces for it (static RE, 2026-10-24, not built):
- "Loading last saved game..." FUN_182ba7c10(C_UISaveLoad): the newest save of user saveMgr+0x188 (FUN_180f685a4 ->
  FUN_1819c0b1c), then FUN_182ba7ca8 queues FUN_1825bd9ac. The Continue lambda FUN_181f90aa0 (busy flag obj+0x122) and the
  start-up check FUN_180da1d54 (wh_sys_AutoLoadLastSave = C_UISaveLoad+0x8c) both call it: no save id needed.
- FUN_180da1d54(C_UISaveLoad) runs once at start-up: a hook there gives the pointer. Without the auto-load it shows the
  main menu with (*(ui+0x120)+0x58)->vt[1](1).
- The pause menu: action "open_pause_menu" (FUN_180612a18, bound to C_UIMenu by its init FUN_18061377c) -> FUN_181f90330:
  (C_UIMenu+0x58)->vt[1](3); ui+0x120 is the same C_UIMenu (same +0x58 controller).
Open: the trigger (menu work done, RAM >= 1.5 GB, the user not in a submenu: game not foreground or idle >= 10 s), and
whether vt[1](3) at gameplay start pauses before the first frame of play.
Split: kcd2-deagle-5a the level step / route A-B; kcd2-deagle-66 apply (4.7: souls 1.2 incl. Storm 0.78, XGenAI 0.8,
profiles + concepts 0.6, database XML 0.5, CryEngine data 0.35), gate 3.4, state 4.

## The map build (Main in LoadLevel; levelprof) - 2026-10-24
Boot 50 (paklock on, profiler on, 20.3 s): LoadLevel 9.5 s on Main = level open + paks 2.8 (octree object reads 0.47,
pak opens / folder scans 0.6), terrain wait 1.1, LoadEntities 2.9 (Lua 1.3, CryPhysics step-lock waits 0.47), navmesh 0.6,
SmartObjectAnimations 1.2, XGenAI 0.5, material dedupe 0.25.
1. levelprof (7f08db1): timing hooks + 30 s full-stack sample of Main (fastload_levelprof.txt 1).
2. paklock (358dfa2): FReadRaw outside CryPak's open-files lock. Main's FOpen/FClose waits 1.1 s -> 0.05 s, LoadLevel
   10.1 -> 9.4-9.6 s.
3. Terrain meshes at the menu (66, 9989499 + d252766): boot 57 LoadLevel 8.3 s, terrain step 3.0 -> 2.1. Boots 52-53
   crashed: the async CGF load's callback is argument 6, the hook passed 5 (cdb on the BugSplat dump).
4. Stored pak entries read in pieces (paklock windows, off): the octree object file, 153,000 reads, ~0.47 s Main. Whole-entry
   read (boot 51) held 605 MB of sound banks: dropped. 256 KB windows: built, not yet measured (boot 52's crash was item 3).
5. SmartObjectAnimations in the level-start job (dbahead, off): boot 58 LoadLevel -0.98 s but WRONG: at level start the
   game-profile source is empty (0 vs 879), all 29,007 ProfileIds unknown. Table readers run only after the load.
6. LoadEntities' 0.47 s spin: DestroyPhysicalEntity FUN_180846e10 write-locks m_lockStep (world+0x5fdc) through
   FUN_18050b638; waits come in 15-50 ms runs. The physics thread was not in TimeStep then (outside sampler); holder unknown.

## Measuring rules
- One change per boot; same fastload_cvars.txt (the two wh_cc lines are in it).
- Test boots: scratchpad bootrun.sh N (auto-load) or bootmenu.sh N + a Continue click at (1497, 348) client px
  (the user's path). steps.py N... tabulates the steps from the plugin log.
- A crash: fastload_crash.txt next to the plugin (crashlog.cpp, since 7a9eb8c) has the faulting address as module+offset,
  the registers and the stack's code addresses; the last block before the crash is the crash. A clean boot (69) writes 2
  handled faults (readiness.cpp's guarded read, one in a module without exports), so not every block is a crash.
