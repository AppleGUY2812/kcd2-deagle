# First save load: findings (current state only; replace, don't append)

Goals: 10 s from Continue to control (since 2026-10-24), launch -> main menu 5 s. Owners: steps 1-4 kcd2-deagle-5a, step 5
+ first gameplay frame kcd2-deagle-66.
RE notes: ProLiant ~/re (KCD2.gpr), WHGame.dll 1.5.6; agent reports in the 5a scratchpad (re_step*.md, re_questearly.md).
Plugin: fastload.asi (its own mod since 2026-10-24: fastload.log, fastload_*.txt; native/src/fastload_main.cpp).
"Control" = the end of the first gameplay frames. Times in s after the first level file ("level loading").

## The user's path vs the test boots (M)
- **Best so far, boots 42 / 44 (2026-10-24 13:31 / 13:47, menu path, 1949 / 2902 MB free): 19.2 / 20.0 s** from the
  first level file to control, + 0.3-0.9 s for the Continue click (thread sampler: last menu frame -> first load code),
  so ~19.5-20.9 s from the click. Boot 42: terrain 3.0, level done 10.3, state 4 0.2, apply 4.8 (one 4.75 s main frame,
  kcd2-deagle-66's), gate 3.2. Quest graph replayed at the menu in 4.48 s and kept. Boot 40 before the replay: 22.7 s.
  One boot: +-1 s noise.
- **Auto-load boots now (boots 126 / 127, 2026-10-25): launch -> in control 33.2 / 33.0 s** = menu ready 12.3 / 11.7 s
  after the process start + 0.5 s + 18.5 / 19.5 s load (level step 12.2 / 12.9 s). Boots 124 / 125 just before
  (pakahead's old list): 36.3 / 36.5 s; boot 123 (devahead): 34.3 s; boot 118: 35.7 s; boot 102: 45.3 s.
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
User's own loads vs free RAM at level loading: 2122 MB 33.4 s, 1597 MB 38.5 s, 1320 MB 48.3 s (U2: state 4 11.7 s with
969k page faults). 16 GB PC (Ryzen 5 3600, RTX 3070 Ti 8 GB). Other RAM (2026-10-24 21:40, game closed): WSL 2.1 GB, Brave
1.9 GB, Windows ~2.5 GB (services 0.8, compression 0.5, kernel pools 1.0), Steam 0.5 GB; Discord + Jellyfin (1.7 GB) closed
by kcd2-deagle-66 before boot 90, so boots 90+ have ~1.7 GB more free than 84-89: compare 90+ with each other.
Census (memsite.cpp, fastload_memsite.txt 1; boot 91, MB in RAM): load start working set 3750 (WHGame 1375, plugin 985:
streamahead copies 508, pakahead 372 + 32), free 2964; gameplay start 5822 (WHGame 3299, NVIDIA driver 749, plugin 518 =
streamahead copies for the rest of the load, freed at gameplay), free 1604. The game itself grows ~2 GB during the load.
The unseen private memory (3 GB committed) is write-combined GPU memory, only ~0.5 GB of it in RAM; DXGI: video memory
2.3 of 7.2 GB budget at gameplay (no overflow), system RAM for GPU data 0.57 GB.
Windows reuses offered pages (OfferVirtualMemory, any priority) before any file cache, whenever the game needs a fresh
page: boots 89-91 took back 0-1 of ~495 pakahead entries, boot 90 0 of 10,660 streamahead copies. Offering = freeing.
- pakahead (kept): big entries more than 256 MB ahead of the load are offered; Windows drops many (boot 127: 462, 771 MB)
  and the toucher takes them back 256 MB ahead of the load, a worker unpacking a dropped one again: 0 found missing at
  the game's read since the list is the load's own order (boots 121-125, old list: 221-429, unpacked by the game).
  Plugin RAM at load start 1591 -> 985 MB (85 -> 91).
- streamahead offering (boot 90): 27.1 s; mapped paks + file-cache prefetch, no copies (boot 89): 28.4 s (hard faults on
  the game's IO thread for dropped pages; the game reads these requests unbuffered, so the file cache can't serve it).
  Both rejected; the copies stay (512 MB cap, freed below 1.2 GB free by ramgov).
Next knobs, one boot each: pakahead window 256 -> 128 MB, streamahead cap 512 -> 256 MB (~0.25 GB each).

## Built and measured
- fastzip (libdeflate) + pakahead: 61 -> 47 s (2026-10-23 boots, older clock).
- xmlahead bounded lead (8d38d00): quest XML phase 5.0 -> 1.0 s (2.25 s since quests go first, see below).
- xmlahead keeps the save load's quests/ files (boot 74): the apply's place graph (the place the save stands in,
  ttkc_villageCenter) reads 2,398 quests/ files through the Skald loader FUN_180979068 (no cache), and SkipQuests had
  dropped every quests/ file once the menu build replays. Now only those listed before the first levels/ file are
  dropped, and the list was re-recorded after questreplay (13,601 files, was 33,012 from before it): the place job's
  node walk 557 -> 28 ms, apply 4.7 -> 4.3 s. The list goes stale when the load changes: re-record it
  (tools/xmlahead_list.py on fastload_xmlahead_trace.csv).
- streamahead, 4 readers + RAM rule (a168e6d, d587a0e): terrain wait 3.4 -> 0.9-2.3 s, depends on free RAM.
- pakahead serves the in-memory paks (Engine.pak, ShaderCache.pak, Shaders.pak; boot 94): their entries are matched to
  the files by entry, unpacked from disk at start and kept for the game's repeat reads, freed 3 s after the last read:
  1,832 reads (754 MB, 325 MB held) served at start-up instead of unpacked on one game thread (boot 93: 1.7 s); main
  menu 20.0 -> 19.2 s after process start. Lists keep their lines as "mem|<path>" (tools/pakahead_list.py).
- pakahead writes its own list when the first load ends (2026-10-25, boots 125 -> 126 / 127): the load's whole reads in
  time order + how often each was read. The hand-made list (boot 94) still had 2.1 GB of the Checkers face mod from
  before its stored repack (389 bad, never read), and its order let one thread's menu-time reads move the load position
  near the end (Windows then dropped 691 MB the game unpacked itself). Entries read more than once are kept until their
  last read (134 MB at most): Rivers of Blood's 2048x2048 uncompressed blood textures (16.8 MB, deflated) are read 4
  times each by the render loading thread. Its inflating 1,397 -> 49-51 ms, Main's render sync wait 1.73 -> 0.40-0.63 s,
  served 1.1 -> 3.1 GB, game came first 569 -> 27-44; control 36.5 -> 33.2 / 33.0 s; menu 13.6 -> 12.3 / 11.7 s from
  process start (2 GB less unpacked at start-up; one boot each, +-1 s). The list follows a new save one start late.
- Start-up installs (kcd2-deagle-66's timing, boot 94): 94 MinHook enables froze every game thread 5.9 s in all during
  the plugin's 6.5 s of installs (Applyprof 0.9 s, Streamio 0.5, Applyfast 0.46, Xmlahead 0.44, Questfast 0.44).
- applyfast fog cull (d587a0e): map clouds vs fog of war 0.86 s -> 60 ms, 0 mismatches in 71k checks.
- applyfast facial DBA sort (2195bf8): lists arrive nearly sorted (10 breaks in 40k), 80-95 ms; gain not measurable.
- applyfast quests first (02c973b, on): quest job starts 3.1-3.3 s earlier; Main's wait 3.3 -> 1.4-1.9 s; state 4
  ~1 s earlier. Quests not yet checked in game.
- applyfast 3c, PSO precache on our thread (4fa48a7, default, fastload_psoahead.txt 3): the game's NonBlocking precache
  (FUN_181dfbac0) runs on a plugin thread from the quest-wait point, ~4 s before LoadLevel's end; a gate on
  FUN_180babff8 makes the game's own call wait for it (boot 118: it found it done and added 137 PSOs in 0.15 s).
  Boot 118: 15,852 PSOs in 2.06 s, 0 parse-lock waits; Main's sync wait at LoadLevel's end 3.0 -> 1.5 s, level step
  14.2 -> 12.9 s, control 37.5 -> 35.7 s. The rest of that wait was the RLT inflating the blood mod's textures 4x
  each: served by pakahead since boot 126 (wait 0.4-0.6 s).
  Mode 2 (3b, hand-over to the RLT) never fired in time since boot 31; mode 1 (menu, on Main) froze the menu ~6.2 s.
- applyfast 5, shaders parsed at the main menu (6a313dd, fastload_menushaders.txt 1): boots 34-36 parsed 457 at the menu,
  20 in the load: state 4 2.7 -> 0.2 s, but no gain on the total: state 4 + gate is 11.0-12.3 s in boots 32-37 and
  state-4 start -> control 17.3-18.7 s in every boot; textures are the long pole and take the time back. U2 (user clicked ~7 s after the menu) parsed only 110.
  re_pso_parallel.md: PSO creation is already on 9 job workers + 4 driver threads; the serial part is the RLT's shader
  parse (~17.6 ms each) and, under low RAM, its texture precache backlog (U2: 2.28 GB unpacked in state 4). R2 (the PSO
  job graph from a plugin thread) is 3c above; R1 (parse from launch on our thread) not built.
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
  plugin thread; the load's 30+ human skeleton loads hit the cache (0 ms). Mode 1 (2026-10-25) starts it at start-up,
  right after CharacterManager::OnFacialsPakChanged (~10.6 s after the plugin), 4.0-5.0 s, done by the load start; boot
  104 -> 105 (one change): the load no longer waits 3.2 s for it, load 23.6 -> 21.1 s, but the menu +1.3 s (female.cdf
  4.0 s there vs 2.2 s at the menu: it shares the CPU with start-up), launch -> control 42.1 -> 41.1 s. Mode 3 = the old
  start at the save list read, mode 2 = after the quest build. "Loading level" (FUN_180b8f748) waits for a running preload. **Never during the load**: boot 39 ran it on
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
| Main: human anim sets 1.9-2.2 s | start-up, after the facial paks | done (animahead mode 1): boot 105 |
| Main: navmesh ~0.7 s, terrain loaders / level open | in the load | kcd2-deagle-66 (plan below) |
| RLT: shader parse 4-8 s, texture precache, PSO | in the load | RE running: re_menu_render.md |
| save read, mapClouds/Storm, Settle textures at the save's position | in the load | asked kcd2-deagle-66 |
Limit: ~2.8 GB free RAM at the menu (boot 30); everything held early competes.

## Tried, no gain
- r_PipelineStatePrecacheMode 0: state 4 0.3 s but a 0.8 s freeze after control (shaders built on demand).
- Timer resolution: Sleep(1) takes 1.5 ms in the game (it calls timeBeginPeriod(1)); not the shader-parse cost.
- xmlahead workers at normal priority, quest job thread above normal: no change (boot 25).
- xmlahead list re-recorded in the quests-first order (boot 26): XML phase unchanged.
- Menu shaders queued in proportion to the frame gap (1-32 a render sync, boot 93): all 457 parsed before the load (20 left
  in it), but the main menu came ~5 s later (= the 5.1 s of parsing: the render thread is on start-up's critical path) and
  the post-LoadLevel part of state 4 stayed 3.5 s, so it is not the render loading thread's shader backlog. Reverted; the
  parse itself (436 shaders, ~11 ms each on one thread) is the cost wherever it runs. Again with up to 24 a sync (boot
  109): 281 parsed before the load, 196 in it; start-up +1.2-1.6 s (Main's syncs wait for the render thread), load no
  faster. Dropped.

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
Route to 10 s, "ready at the menu" (readymenu.cpp, built 2026-10-24, off by default; ON in the user's game since 2026-10-24
19:00, fastload_readymenu.txt "1 5"; the game starts the load by itself): once the menu work is done (save scan, quest build, animations, terrain meshes asked) and no keyboard or
mouse input for 5 s, the plugin calls the game's "Loading last saved game..." FUN_182ba7c10(C_UISaveLoad) (what Continue and
wh_sys_AutoLoadLastSave call; the pointer from a hook on its start-up method FUN_180da1d54, which runs ~20 s after the
plugin), skipped if a load was started any other way or the menu's busy byte C_UISaveLoad+0x72 is set; at gameplay start
the pause menu opens ((*(C_UISaveLoad+0x120)+0x58)->vt[1](3), the "open_pause_menu" call; C_UIMenu = C_UISaveLoad-0xb0).
Boot 76: menu work done 30.7 s after the plugin, load 31.7 -> 50.5 s, pause menu at gameplay start, Resume (Esc) = control
with no frame over 250 ms. Plugin start -> control 54.3 s vs 74.8-76.0 s in the Continue boots 74-75 (their click comes
~15 s after the menu work). The load itself took 18.4 s (boot 75: 16.3 s): it starts 1 s after the menu work, with 1.8 GB
RAM free (boot 75: 2.8 GB). Another save on the same level from the pause menu = the same-level path (~6 s). Open: the
5 s idle rule is system-wide (GetLastInputInfo), so a user idle inside a menu page (Settings) for 5 s gets the load too.
Boot 77 (installed build, 1.8 GB free): menu work done 18.5 s after the menu's start-up, not 10.5: the terrain preload
waited 1,267 frames on the 1.5 GB RAM rule (14.4 s, not 6.3), and readymenu waited for it; journal identical to boot 75's.
Since then readymenu goes once the preload has been RAM-blocked for 1 s (the load reads the rest). Boot 78: it did (menu work
done 3.9 s after start-up; 6.5 s more waiting on the user's input), but only 1.4 GB was free at the load's start (Discord,
browser, WSL's 6 GB VM; 16 GB PC): load 24.0 s (terrain 5.7, level 13.1), in game 57.5 s after the plugin (boot 77: 58.2).
Free RAM now sets the load time more than any step.
Boot 79: typing in another window held readymenu 24.8 s (input counted system-wide). Since then input counts only while
the game is the foreground window, and the load starts once the save list is read (it waits for questmenu's build and
animahead by itself, and cuts the terrain preload short). Boot 80: load started 23.9 s after the plugin (menu up at 20.0;
the 4 s between is the menu's first frame, DLSS start-up), in game at 51.7 s; the load took 27.7 s with 1.3 GB free:
animahead skipped its menu preload (RAM rule), the terrain preload asked nothing, streamahead freed all 885 MB, pakahead
held 876 MB. Boots 76-80, free RAM at load start vs load: 1.8 GB 18.8 s, 1.8 GB 18.1 s, 1.4 GB 24.0 s, 1.5 GB 21.4 s,
1.3 GB 27.7 s. The PC (16 GB) had WSL 5.1 GB, Discord 1.8 GB, Brave ~2 GB, Steam's web helper 1.2 GB committed.
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
7. LoadLevel's end -> state 5 (3.0-3.85 s, boots 88-94; 1.5 s since 3c, boot 118) = Main in SyncMainWithRender's timed wait (FUN_1809a6308, rip
   WHGame+1c1edc0): CCryAction::MapCmd FUN_180b8f748 sends system event 0x100d, the renderer's handler FUN_1807a26ac
   FlushAndWaits (FUN_1807a2138) for the render loading thread, which runs PostLoadLevel's PSO precache then: 56 shader
   parses + 15,989 graphics PSOs in 2.39 s (kcd.log "Precached ... NonBlocking"; already JobManager jobs,
   "PrecachePSOsJob_ShadersActivationPhase", r_PipelineStatePrecacheMode 2 = jobbed; the NVIDIA DXCache is warm). The
   early hand-over (applyfast 3b) failed in every boot 88-94: the loading thread is busy with its texture precache at
   the quest wait. 3c runs the precache on our thread instead; the texture batch after it (boot 118, 1.5 s: the RLT
   inflating Rivers of Blood's 16.8 MB textures, each read 4 times) is served by pakahead since boot 126: 0.4-0.6 s left.
8. The menu's first frame (menu ready -> load start, 4.2-4.8 s) is NVIDIA NGX start-up on the render thread. Boot 96
   (render-thread samples): t 17.10-21.38 s after the plugin all in the DLSS plugin's FUN_181e0b7c0 -> FUN_181dffa40
   (NVSDK_NGX_D3D12_Init + GetCapabilityParameters: signature checks, driver-profile re-reads), Main in
   SyncMainWithRender the whole time. The render thread idles t 10-16.8 s before it. ngxearly.cpp runs that init on a
   plugin thread once the D3D device exists (~4.5 s after the plugin, 4.3-4.8 s long) and hands the result to the game's
   call (0 ms). Boots 97 -> 98 (one change): menu ready -> load start 5.2 -> 0.5 s, launch -> in control 46.8 -> 42.9 s;
   boots 99-101 41.8-42.2 s.
   Main's other render waits before the menu: ~1.85 s in FlushAndWait (FUN_1807a2138), spread over small pieces.
9. response_depot_wave_file (dbprof 1.08-1.13 s on Main before the menu: ~0.43 s XML parse, the rest row building) is
   the one XML file in Localization\english-part3.pak (63,435 .ogg), which mounts last, ~20 ms before the game reads it:
   xmlahead's parse-ahead can't reach it through CryPak. dbasync (kcd2-deagle-66, 2eb78d9) loads it on a worker: boot
   108 Main never waited, menu -1.6 s.
10. Main's text step (text_ui_*.xml into the string table, FUN_18071aec0 / FUN_180a5a084, right after the language's
   XML paks mount) is 1.4-1.55 s, ~60 % of it waiting on the Windows heap lock (RtlEnterCriticalSection under
   RtlAllocateHeap / RtlFreeHeap). Lifting xmlahead's cap at the mount (boot 114) removed Main's 0.4 s wait for the files
   but made this step 0.27 s longer. Since then (xmlahead text cap) the workers parse only localization/ files until the
   game reads its last one: boot 119 -> 120 step 1.55 -> 1.37 s (heap-lock samples 46 -> 37), menu 15.65 -> 14.96 s from
   process start (one pair). The animahead thread waits on the same lock (34 of 43 samples): the holder is a thread the
   sampler did not name (xmlahead workers named since boot 121).

## Hang at the loading screen's start (boot 107, once in ~70 auto-loads)
20 ms after readymenu started the load, the render thread waited forever in the menu video's Bink frame (FUN_180892e30:
BinkDoFrameAsyncMulti / BinkDoFrameAsyncWait(-1)), the 4 BinkAsy threads asleep, Main in SyncMainWithRender. Not seen
again (boot 109 = the same build). applyfast logs the render thread's video mode (m_pRT+0x140: 0 off, 1 asked, 2 running,
4 stopping) at each change, so a repeat shows how far the loading screen got. Samples: 5a scratchpad boot107_samples.csv.

## Measuring rules
- One change per boot; same fastload_cvars.txt (the two wh_cc lines are in it).
- Test boots: scratchpad bootrun.sh N (auto-load) or bootmenu.sh N + a Continue click at (1497, 348) client px
  (the user's path). steps.py N... tabulates the steps from the plugin log.
- A crash: fastload_crash.txt next to the plugin (crashlog.cpp, since 7a9eb8c) has the faulting address as module+offset,
  the registers and the stack's code addresses; the last block before the crash is the crash. A clean boot (69) writes 2
  handled faults (readiness.cpp's guarded read, one in a module without exports), so not every block is a crash.
