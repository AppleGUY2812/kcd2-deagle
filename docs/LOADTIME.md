# First save load: findings (current state only; replace, don't append)

Goal 20 s from Continue to control. Owners: steps 1-4 kcd2-deagle-5a, step 5 + first gameplay frame kcd2-deagle-66.
RE notes: ProLiant ~/re (KCD2.gpr), WHGame.dll 1.5.6; agent reports in the 5a scratchpad (re_step*.md, re_questearly.md).
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
- applyfast 3b, PSO precache during the load (e21c93b, default, deagle_psoahead.txt 2): handed to the RLT before
  Main's quest wait; state 4 2.4 -> 0.4-0.5 s (boots 29-30). Main now waits 1.7-2.2 s for the RLT's texture precache.
  Mode 1 (at the menu, on Main) froze the menu ~6.2 s: rejected. Since boot 31 it never fires in time: the quest job
  ends earlier (quests first), the RLT is still on its texture precache then, and later points come after the game's
  own sync. Next: PSO creation on worker threads (re_pso_parallel.md).
- applyfast 5, shaders parsed at the main menu (6a313dd, deagle_menushaders.txt 1): boots 34-36 parsed 457 at the menu,
  20 in the load: state 4 2.7 -> 0.2 s, but no gain on the total: state 4 + gate is 11.0-12.3 s in boots 32-37 and
  state-4 start -> control 17.3-18.7 s in every boot; textures are the long pole and take the time back. U2 (user clicked ~7 s after the menu) parsed only 110.
  re_pso_parallel.md: PSO creation is already on 9 job workers + 4 driver threads; the serial part is the RLT's shader
  parse (~17.6 ms each) and, under low RAM, its texture precache backlog (U2: 2.28 GB unpacked in state 4). Next if
  needed: parse on our own thread from launch, not paced by menu frames (R1), and the PSO job graph started from a
  plugin thread before the quest wait (R2, 2.1-2.7 s per boot of PSO jobs).
- questmenu (a918236, e6a864a; deagle_questmenu.txt 1 since boot 35): after event 0x24 the game's own job body builds
  the graph on a plugin thread (11.5 s, free RAM 2710 -> 2030 MB) and the load keeps it (mgr+0x378 = 0, body skipped).
  Concept messages at the menu during the build: 0x1, 0xc, 0x24; before the load: 0x38, 0x3a (none touch projects).
  Journal after the load identical to a normal load (boots 35 vs 36). Skips below 1200 MB free.
- questreplay (f9cf471, 9b6805e, f237548, 8a9ca19): the quest build replayed from deagle_questreplay_<level>.bin (29 MB;
  key: WHGame.dll, 72 paks / manifests incl. Workshop mods, the job inputs). Phase 2a calls the game's leaf functions in
  the recorded order (same resolver id order, #4838fed6bbbf53ef); phase 1 (21,808 quest XML files) is skipped and
  xmlahead no longer parses them (~250 MB less at the menu). Menu build 14.4 -> 8.43 -> 4.35-4.59 s (2a 2.0, ports 0.37,
  edges 1.35, activation 0.56). A load that builds its own (menu build not kept, a level switch): 6.26 s in the load
  (boot 45, deagle_questmenu.txt 2; it shares the CPU with the level load). A same-level reload during play runs no
  quest job at all (boot 43, ~6 s reload). Checked: questdump equal to the game's own build except the quest's random
  roll (as between two game builds); journal identical in boots 41-45. A failed replay drops the file, the game builds.
- animahead (f71b7da, 7f34635; deagle_animahead.txt 1): female.cdf + male.cdf through the game's LoadAndLockResources on a
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
| Main: SmartObjectAnimations 1.43, XGenAI 0.55, material dedupe 0.5 s | in the load | next (re_stepEF E1, re_menu_main) |
| RLT: shader parse 4-8 s, texture precache, PSO | in the load | RE running: re_menu_render.md |
| save read, mapClouds/Storm, Settle textures at the save's position | in the load | asked kcd2-deagle-66 |
Limit: ~2.8 GB free RAM at the menu (boot 30); everything held early competes.

## Tried, no gain
- r_PipelineStatePrecacheMode 0: state 4 0.3 s but a 0.8 s freeze after control (shaders built on demand).
- Timer resolution: Sleep(1) takes 1.5 ms in the game (it calls timeBeginPeriod(1)); not the shader-parse cost.
- xmlahead workers at normal priority, quest job thread above normal: no change (boot 25).
- xmlahead list re-recorded in the quests-first order (boot 26): XML phase unchanged.

## Open, by expected gain
1. Settle gate 8.7 s (kcd2-deagle-66): textures, clothing.
2. Quest build 4.4 s at the menu, 6.3 s in a load (target 5 s: met at the menu). Left, from the sampler (questbench N
   filesample): edges 1.35 + ports 0.37 s, of which ~1.0 s is the port lookup FUN_1806a0674 walking per-instance
   enumerators the port cache does not cover (needs RE: are repeated (node, name) lookups stable?); 2a 2.0 s
   (attribute conversion FUN_1804fb74c 19 %, slot 2/4 handlers 14 %, pending entries 8 %); activation FUN_18069551c
   0.58 s; teardown frees 0.27 s. No record yet for Kuttenberg (its first menu build records one, ~8.4 s once).
3. Apply save 5.3 s: Storm memo ~0.45 s, concept BFS ~0.4 s (re_stepG).
4. The three build chains end together (quest 14.0, RLT ~14.4, Main 12.3 + E 2.1 s): one alone gains < 1 s; move all
   three to the menu (table above).
5. Objects step 8.6 s (re_stepC): entities 2.7 s, CryPak lock waits ~1.8 s.

## Measuring rules
- One change per boot; same deagle_cvars.txt (the two wh_cc lines are in it).
- Test boots: scratchpad bootrun.sh N (auto-load) or bootmenu.sh N + a Continue click at (1497, 348) client px
  (the user's path). steps.py N... tabulates the steps from the plugin log.
