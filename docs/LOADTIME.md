# First save load: findings (current state only; replace, don't append)

Goal 20 s from Continue to control. Owners: steps 1-4 kcd2-deagle-5a, step 5 + first gameplay frame kcd2-deagle-66.
RE notes: ProLiant ~/re (KCD2.gpr), WHGame.dll 1.5.6; agent reports in the 5a scratchpad (re_step*.md, re_questearly.md).
"Control" = the end of the first gameplay frames. Times in s after the first level file ("level loading").

## The user's path vs the test boots (M)
- Best so far, boot 35 (2026-10-24 08:24, menu path, 2910 MB free): **30.6 s** with the quest graph built at the menu
  and kept (questmenu); boots 34 / 36 without it: 33.8 / 33.7 s. Steps in boot 35: terrain 3.5, level done 12.5 (16.1
  without), state 4 1.4, apply 5.0, gate 10.6 s. The gate grew from 8.2-9.0 s (boots 32-33) to 10.6-11.7 s in boots
  34-36 (streamahead 6fb0c4c era; kcd2-deagle-66 told). Boot 37 (c40fb0a, gate readers stopped, repeats served
  from kept copies: 415 hits): gate still 11.0 s, TextureStreaming last at 10.5 s; 32.1 s total, quests kept again.
  r_TexturesStreamPoolSize 5120 is only the fallback: the pool is dynamic (r_TexturesStreamDynamicPool 1, kcd2-deagle-66).
- **The gate's texture tail is two mods** (re_texture_early.md, boot 37 trace): Mods/checkersfacesandbody (1.68 GB) and
  checkershair ship each texture as one deflated 4K .dds (201 entries, 2.3 GB unpacked); vanilla splits per mip. Every
  mip request re-reads and re-inflates the whole entry (299/299 passes, 5-9 per texture): gate seconds 5-11 spent 90 % of
  the IO thread's read time on them; TextureStreaming ready 0.06 s after the last mod read; mod MB read tracks the ready
  time across boots. The RLT's RT_Precache also inflates all 2.1 GB of them (U2's +2.28 GB). Fix candidate: repack them
  stored (lossless, +0.6 GB disk), user's call; expected gate -5 s (I, one traced boot). Starting view textures before
  state 4 gains <= 0.5 s (vanilla view textures ~155 MB) and is blocked by the frame-bound scheduler: dropped.
- Menu + Continue (boot 27, 2026-10-24 02:31): **34.4 s from the click** (1.4 s click -> level loading, then 33.0 s).
  With 3b: 34.6 s (boot 29, gate 9.4 s) and 33.4 s (boot 30, + readiness temp 128). The gate varies +-1 s per boot.
- Auto-load test boots (wh_sys_AutoLoadLastSave; boots 24-26): 38.3-39.4 s. They carry two costs the user's path does
  not: NVIDIA NGX/DLSS start-up (~4 s first gameplay frame; on the menu path it happens before the load) and less
  menu time for the read-ahead (gate 10-11 s vs 8.7 s). Compare test boots with test boots.

## Where the time goes (boot 27, menu path, 33.0 s)
| Step | s | What holds it |
|---|---|---|
| open + terrain | 0-3.7 | terrain loaders (RAM-bound read-ahead) |
| objects + entities (Main) | -12.3 | 35,889 entities 2.7 s, human anim sets 2.2 s, navmesh 0.7 s, CryPak lock waits |
| Main waits for the quest job | -13.7 | job 13.7 s: XML 2.25 s + nodes 6.0 s + ports 1.0 s + edges 3.3 s, one worker |
| finish level | -15.7 | SmartObjectAnimations table 1.43 s, XGenAI global pass 0.55 s (re_stepEF) |
| state 4 | -18.1 | PSO precache 2.4 s: 71 shaders parsed (~15 ms each, real CPU) + 15,989 PSOs |
| state 5 apply save | -23.4 | Storm 0.86 s, concept BFS 0.35 s, RandomEvent project 0.4 s, XML 0.3 s, ~1.8 s scattered |
| settle gate | -32.1 | textures 7.9 s, clothing 6.3 s |
| first gameplay frames | -33.0 | 0.3 + 0.6 s |

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
- skin cap (kcd2-deagle-66): wh_cc_MaxSkinLoadingInProgress 120 / JobQueueSize 80 (load): clothing 10.2 -> 7.2 s
  (did not repeat in every boot).

## Start early (user rule 2026-10-24: every step at launch / menu, before Continue)
| Work | Starts now | Status |
|---|---|---|
| unpack (pakahead) | launch +1 s | done; 2 GB cap: 17,650 of 23,338 files ready before the menu |
| ground meshes (streamahead) | launch +1 s | done; 512 MB held cap |
| XML parse (xmlahead) | menu idle (~launch +22 s) | capped 3,000 files ahead of the game: 4 workers idle most of the menu; level files wait for level loading |
| quest graph 13.7 s | save list read (~launch +20 s) | done (questmenu): -2.4 s (31.35 s in boots 35/37 vs 33.75 s in 34/36); on |
| Main: anim sets 2.2, SmartObjectAnimations 1.43, XGenAI 0.55 s | in the load | RE running: re_menu_main.md |
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
2. Quest job: off the load path when the menu build is kept, 11.5-14 s otherwise (loads during play, quick Continue).
   **Target (user): 5 s or less.** Path (re_quest_builder.md 5 + re_quest_dump_binary.md + re_quest_replay_parallel.md):
   graph-dump comparator first (exactness check), binary DOM (phase 1 2.3 -> ~0.4 s), replay builder + more cores for
   2a (6.2 s) and pre-resolved edges for 2b (4.5 s). Step 1 built: questfast (d352a75) rttr sorted insert, expect
   -0.4-0.8 s on the first build per process; not measured yet.
3. Apply save 5.3 s: Storm memo ~0.45 s, concept BFS ~0.4 s (re_stepG).
4. The three build chains end together (quest 14.0, RLT ~14.4, Main 12.3 + E 2.1 s): one alone gains < 1 s; move all
   three to the menu (table above).
5. Objects step 8.6 s (re_stepC): entities 2.7 s, CryPak lock waits ~1.8 s.

## Measuring rules
- One change per boot; same deagle_cvars.txt (the two wh_cc lines are in it).
- Test boots: scratchpad bootrun.sh N (auto-load) or bootmenu.sh N + a Continue click at (1497, 348) client px
  (the user's path). steps.py N... tabulates the steps from the plugin log.
