# How shipped games handle many bullets (q7)

(A) = documented: source code, official docs, dev statement. (B) = forum, fan wiki or vendor claim. Nobody has published a full "bullet budget" write-up. The best primary evidence is engine source code: Dagor (War Thunder's engine), the public CRYENGINE 5 mirror (KCD2's engine family) and ACE3.

## 1. Data layout and pooling
- Dagor's tracers live in one fixed array of 2048 slots with an "exists" bitmask. Visibility culling runs as a CPU job (`tracer_manager_cull`). When the pool is full, new tracers are refused with a single warning, and the top 15% of slots are kept for forced (player) tracers. (A) https://github.com/GaijinEntertainment/DagorEngine/blob/main/prog/gameLibs/publicInclude/render/tracer.h , .../render/tracer/tracer.cpp (`RESERVE_FOR_PLAYER_FRACTION 0.15`)
- War Thunder's actual ballistics code is not in the open release, only stubs (`stub_ballistics.cpp`). (A) same repo, prog/daNetGame-das-aot/stub_ballistics.cpp
- In CRYENGINE GameSDK, each bullet is an entity with a `PE_PARTICLE` physics body. The physics system moves it and reports hits as `EventPhysCollision`. "Reusable" ammo goes back to a pool for its class: the entity is hidden and moved to the origin. (A) https://github.com/MergHQ/CRYENGINE/blob/release/Code/GameSDK/GameDll/Projectile.cpp , WeaponSystem.cpp (`ReturnToPool`), Bullet.cpp
- Unity's docs recommend pre-creating inactive projectiles and reusing them. (A) https://docs.unity.com/en-us/engine/6000.3/manual/analysis/performance-memory/managed-memory/optimizing-code-managed-memory/reusable-code
- Unreal developers report that spawning actors per bullet is expensive, and pool them or move bullets into a subsystem. (B) https://forums.unrealengine.com/t/weapon-system/12693 , https://forums.unrealengine.com/t/darkmate-bullet-projectile-subsystem/2583054

## 2. Ray work
- CRYENGINE has the `rwi_queue` flag ("queues the RWI request, when done it'll generate EventPhysRWIResult") plus `TracePendingRays`, and a priority-based `IntersectionTestQueue`. (A) https://github.com/MergHQ/CRYENGINE/blob/release/Code/CryEngine/CryCommon/CryPhysics/physinterface.h . Uncertain: whether WHGame/KCD2 still has these.
- Unity's `RaycastCommand.ScheduleBatch` runs batched rays "asynchronously and in parallel" in jobs. (A) https://docs.unity3d.com/ScriptReference/RaycastCommand.html
- Unreal's `AsyncLineTraceByChannel`: a trace requested in frame X returns its result in frame X+1. (A) https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UWorld/AsyncLineTraceByChannel
- ACE3 (Arma 3) does the drag maths in a native DLL (`callExtension "ballistics:bullet:simulate"`) and lets the engine handle collision. (A) https://github.com/acemod/ACE3/blob/master/addons/advanced_ballistics/functions/fnc_handleFirePFH.sqf
- A Unity VR developer reports a performance drop past about 100 job-raycast bullets. (B) https://forum.unity.com/threads/raycast-bullets-with-job-system.1430539/

## 3. Hitscan vs projectile, simulation LOD
- ACE3 simulates accurately only the bullets that matter: local shots, remote shots that could reach the player (range ×1.3), visible tracers, and shots seen through a scope. Its step interval defaults to 0.05 s (range 0-0.2). (A) https://github.com/acemod/ACE3/blob/master/addons/advanced_ballistics/functions/fnc_handleFired.sqf , initSettings.inc.sqf
- Arma's `timeToLive` exists "as optimization to avoid having to simulate stray bullets". (A) https://community.bistudio.com/wiki/CfgAmmo_Config_Reference
- Dagor fades and LODs tracer tails by view angle and distance (5 tail LODs, `tailFartherLodDist` 1000 m). (A) tracer.cpp
- Hybrids: Sniper Elite uses a hitscan check and then spawns a projectile. (B, blog) https://gamedeveloper.com/programming/how-do-bullets-work-in-video-games- . Black Ops 4 is said to use hitscan up close and projectiles at range. (B, fan wiki) https://callofduty.fandom.com/wiki/Hitscan

## 4. Tracers
- Real belts usually carry one tracer in five. (A, general) https://en.wikipedia.org/wiki/Tracer_ammunition
- Arma magazines set `tracersEvery` and `lastRoundsTracer`. (B, exact behaviour is forum-derived) https://forums.bohemia.net/forums/topic/96792-calculating-which-rounds-are-tracers/
- Dagor draws tracers with GPU instancing: instanced heads, compute-built tail segments, indirect draws, capped at 2048 tracers with 16 segments each. (A) tracer.h, tracerConsts.hlsli
- CRYENGINE GameSDK `TracerManager` has a fixed pool of 96 tracers and removes the oldest when full. (A) https://github.com/MergHQ/CRYENGINE/blob/release/Code/GameSDK/GameDll/TracerManager.h , TracerManager.cpp

## 5. High fire rates
- War Thunder plays rapid-fire guns as "looped pre-rendered sounds of the gun bursts". The loop runs while `on_shot`=1. Ground and naval guns can switch to one-shot mode instead. (A, official modding wiki) https://wiki.warthunder.com/Sound_modifications
- CRYENGINE 3 docs: rapid fire is "a loop or a set of loops stitched together", stopped at sync points (`synchedStop`) so the last shot finishes. The tail is a separate one-shot fired on spin_down. Loops are cut into pieces of three to six shots. (A) https://cryengine.com/docs/static/engines/cryengine-3/categories/1114113/pages/13205649
- War Thunder's 1.77 update split ground gun audio into an "attack" layer and a tail per salvo. (A, devblog) https://warthunder.com/en/news/5340-development-new-sounds-in-update-1-77-en
- With per-shot events, Wwise and FMOD users report jitter, double triggers and frame-rate-dependent timing. (B) https://audiokinetic.com/qa/8040/looping-automatic-weapon-system-with-unreal-and-wwise
- Impacts: Unreal's Niagara Data Channels merge many burst effects (impacts) into one shared simulation "if many such systems are spawned rapidly". (A) https://dev.epicgames.com/documentation/unreal-engine/niagara-data-channels-overview
- Not found: primary sources on per-shot vs persistent muzzle flash, or on damage aggregation.

## 6. Budgets and caps
- Dagor sound code keeps a "stealing group" of 4 instances by default (16 at most) per long-range looping event type, replaces the furthest one, and relies on FMOD's stealing=virtualize. A separate component caps the number of instances per sound path. (A) https://github.com/GaijinEntertainment/DagorEngine/blob/main/prog/gameLibs/soundSystem/eventInstanceStealing.cpp , prog/daNetGameLibs/sound/sound_utils/es/limit_max_instances_under_sound_control.das
- Dagor bullet-hole decals are a ring buffer (`nextHoleNo = (nextHoleNo+1) % maxHoles`) with `maxDecals` 10000. (A) prog/gameLibs/billboardDecals/billboardDecals.cpp , billboard_decals.template.blk
- Niagara Effect Types: "Max System Instances" and per-type caps, culled by significance, scaled down by global FX budget use. (A) https://dev.epicgames.com/documentation/unreal-engine/performance-budgeting-using-effect-types-in-niagara-for-unreal-engine
- DICE's HDR audio (GDC 2009) drops sounds that would be masked by louder ones. The details are in the talk, which I didn't read. (A, summary only) https://www.ea.com/frostbite/news/how-hdr-audio-makes-battlefield-bad-company-go-boom
- Not found (primary): Tarkov, Squad, PlanetSide 2, Destiny, DCS or Battlefield projectile internals. What exists is fan analysis only. (B) https://kotaku.com/networking-how-a-shooter-shoots-5869564

## What this means for our mod
1. Move the per-round loop into native code with plain arrays and a fixed pool. Lua should only receive hit events. Test whether WHGame keeps `rwi_queue`/`TracePendingRays` (batched, deferred rays) or `PE_PARTICLE` bullets that the physics thread moves for us.
2. Add simulation LOD the way ACE3 does: full detail for rounds near or visible to the player; far, unseen rounds stepped at about 20 Hz with one long ray; a time-to-live and a range kill for strays.
3. Tracers: 1 in 4-5 at high rates. Keep a fixed tracer pool (CryEngine 96, Dagor 2048) that evicts the oldest and reserves slots for the player's own tracers.
4. Above roughly 15 rounds/s, play a burst loop with sync-point stop plus a one-shot tail (War Thunder, CryEngine 3), not one FMOD event per round. Cap instances and steal the furthest.
5. Impacts: merge per frame per surface, cap new effects per frame, keep holes in a ring buffer, and add up damage per target per frame before applying it.

## Reddit (practitioner and developer comments)
Read through the Arctic Shift archive (the reddit skill). (B-dev) = the game's own developer, (B-practitioner) = says they built it, (B-lore) = anything else. Full-text search of the big game subs timed out. The r/Warthunder title search for "tracers" returned only gameplay complaints. No War Thunder, Tarkov or DCS developer comments on internals were found.

Simulation
- Back in UE3 he kept bullets as plain structs in one array and traced each frame from the old to the new position. Only tracer rounds took a pooled, non-networked tracer actor, which got a shadowless light only while the frame rate was healthy. (B-practitioner, licensed-UE dev) https://www.reddit.com/r/unrealengine/comments/16du7r4/
- Same thread: a subsystem with a POD struct array and one trace per round handles "thousands in flight, no problem; rendering is another matter" (B-practitioner). A dev whose studio uses Mass in production says async traces "return one frame late" but cost less, and that you should pool if you use actors (B-practitioner).
- A central bullet manager with a struct array, a trace from the previous to the current position, and visuals through Niagara Data Channels. Another commenter: don't tick every bullet every frame; stagger them or use a bigger per-bullet dt. (B-practitioner / B-lore) https://www.reddit.com/r/unrealengine/comments/1myzvu5/
- For high fire rates, someone found it "very effective" to treat each gun as a probability field (a cone of bullet density) and roll hits for whatever passes through it, with no per-round simulation. (B-practitioner) same thread
- Unity: 500k projectiles using Burst jobs, batched raycasts and `RenderMeshInstanced`. The main thread only handles the roughly 4,000 hits per frame, which a queue passes back. 200k spawned/s ran at 27 fps and 20k/s at 272 fps (RTX 5070). Several shots per frame each carry their leftover time into the next step. (B-practitioner) https://www.reddit.com/r/Unity3D/comments/1vn9aye/
- In the same thread, a counter to adding up damage per frame: "one object soaks up all the projectiles you hit it with in the same frame". (B-lore)
- Gone Viral (Skullbot Games): the UE4 `ProjectileMovementComponent` tick dominates the game thread. About 170 pooled bullets spawned in one frame push it past 16 ms, and 700+ bring it to about 32 ms (25 fps). (B-dev: wrote the studio's dev blog) https://www.reddit.com/r/unrealengine/comments/84varh/
- To exceed the frame rate, fire n rounds in a frame, each offset by velocity × its fractional frame time. Firing on fixed physics steps keeps frame drops from changing the gunplay. (B-lore) https://www.reddit.com/r/gamedev/comments/17i81we/
- Their game runs the whole ballistic path at fire time, with a fixed timestep over the bullet's life, and plays tracers and hits at once. It suits short ranges. (B-practitioner) https://www.reddit.com/r/unrealengine/comments/lf6kzs/
- Total War-style thread: projectile data updated about 4 times a second on a staggered schedule, with far projectiles drawn merged. Another commenter: 20-30 Hz updates go unnoticed. (B-practitioner / B-lore) https://www.reddit.com/r/gamedev/comments/1hleies/
- ACE3 developer dedmen: the air-distortion effect "only happens if AB is simulating the bullet", so Advanced Ballistics simulates only some rounds. (B-dev: he posts ACE release notes) https://www.reddit.com/r/arma/comments/7dlqqw/_/dpzkq3w/
- Pooling: in Godot, the gain only came once pooled nodes were disabled rather than removed from the tree, because removal was the cost (GDQuest, B-practitioner) https://www.reddit.com/r/gamedev/comments/qdgsyd/ . The 500-upvote counter-view says it doesn't matter below roughly 1,000 spawns a second, but an 80-pellet shotgun did need a pool (B-lore) https://www.reddit.com/r/gamedev/comments/1ic5da7/ . Another suggestion: a pool of about 100 that destroys the oldest when full (B-lore) https://www.reddit.com/r/unrealengine/comments/16ysjjk/
- A tower-defense developer uses one particle system per effect type plus a manager that emits at given positions (fewer draw calls), a spatial hash grid, and ray-circle tests. (B-dev, This Isn't Just Tower Defense) https://www.reddit.com/r/Unity3D/comments/1o80ylq/

Tracers
- Make every 3rd to 6th round a tracer, or 3-4 untraced impacts between tracers. GAU-8-type air-to-ground belts carry no tracers. (B-lore) https://www.reddit.com/r/gamedev/comments/1rczekb/ , https://www.reddit.com/r/Unity3D/comments/8zsz80/
- Tracers are cosmetic: keep them approximate, simulate them on the GPU, and use distance-field collision instead of line traces. (B-lore) https://www.reddit.com/r/unrealengine/comments/1hiqopo/
- Draw them as velocity-aligned billboards (edge-on looks flat, so add end caps), or as two crossed quads built in a geometry shader from point + velocity. (B-practitioner, own engines) https://www.reddit.com/r/gamedev/comments/21l1mn/

Sound
- One sound per bullet: rounds fell on frames 0, 2, 4, 7, 8, 11 "and you can imagine how awful that sounded". The fix was intro + loop + outro roughly matching the fire rate, "and nobody noticed". (B-practitioner) https://www.reddit.com/r/GameAudio/comments/4zcfbq/ . A dissent in the same thread from a gun-pack vendor: single shots plus tails that fade when the next shot starts (B-practitioner).
- Per-shot triggering is quantised to the game frame and FMOD's own update. It works at M2 rates but stutters above that. (B-lore) https://www.reddit.com/r/GameAudio/comments/yvpnxv/
- A minigun at 50 rounds/s as one-shots "will easily chew up CPU". Use a loop with a fast release plus a tail. (B-lore) https://www.reddit.com/r/GameAudio/comments/5865wt/
- Trigger + chopped loop + tail on release. On a AAA FPS, NPC guns were simple while the player's gun had up to 8 layers, and the loop files carried markers to play back the real fire rate. (B-practitioner) https://www.reddit.com/r/GameAudio/comments/iyen5f/
- Cap and prioritise simultaneous gunshot voices. On one shipped title, playing every event "sounded horrendous". (B-practitioner) https://www.reddit.com/r/GameAudio/comments/88q9ed/ . Let stronger weapons duck weaker ones through HDR or priority (B-lore) https://www.reddit.com/r/GameAudio/comments/p0xiau/

Render side
- Decals cost per covered pixel × stack depth, not per count. 10,000 non-overlapping decals showed no big drop, but overlapping layers behave like stacked transparency (B-lore) https://www.reddit.com/r/unrealengine/comments/1m3dixj/ . In that thread's own game, 80 translucent 2048² blood decals cost 20 fps and 200 cost 40 fps; the fixes offered were masked instead of translucent, a count limit and culling (B-practitioner).
- Decals get expensive "in large numbers (objects in memory) or when they cover large amounts of screen space (overdraw)". Overlapping decals are like overlapping transparents. One 2048² decal spawned per tick went from 60 to 30 fps within a minute. Games fade bullet holes for this reason. (B-lore) https://www.reddit.com/r/unrealengine/comments/1dv1cnp/
- Our case (≈200 deferred decals/s on one spot → 4 fps, building over ~1.5 s and clearing ~1 s after stopping) fits this: about 300 projection boxes stack on the same pixels, so cost tracks live decals × covered area and clears as they expire or fade. Merging hits on one spot into a single decal is the advice above "use a decal once the count passes X". (inference + B-lore) https://www.reddit.com/r/unrealengine/comments/16ysjjk/
- Many lights: Forward+/clustered stops helping "if many lights are touching the whole screen — that's just too many lights per pixel". Some games cap 4 lights per tile. Per-light cost is bandwidth × covered pixels (96 B/light/pixel in that thread). (B-lore) https://www.reddit.com/r/GraphicsProgramming/comments/1m5e195/
- Clustered (froxel) light lists, or a 2D tile system the commenter built that packs thousands of lights into a texture. (B-practitioner) https://www.reddit.com/r/opengl/comments/ycareh/ . Tiled deferred with 10,000 lights in compute (B-practitioner) https://www.reddit.com/r/gamedev/comments/7falq6/
- Quad overdraw: tiny or thin triangles waste 2×2 pixel quads, which hits thin tracer streaks and spark sprites (B-lore) https://www.reddit.com/r/GraphicsProgramming/comments/1ftze60/ . When every particle has the same lifetime, a FIFO ring removes the oldest for free (B-lore) https://www.reddit.com/r/GraphicsProgramming/comments/7xlwh9/

### Held up against the primary sources
- Agree: a struct array, a trace per step and an outside pool (philbax, the subsystem builders) = recommendation 1 and CryEngine/Dagor pools. "Pool ~100, destroy oldest" = CryEngine TracerManager 96. Async traces one frame late = Unreal docs. Loops above high rates (skompan, minigun, FMOD quantisation) = War Thunder / CryEngine 3 docs and the Wwise Q&A. Voice caps = Dagor stealing groups. 1 tracer in 3-6 = the 1:5 belt. One system per effect type = Niagara Data Channels. Selective AB simulation (dedmen) = ACE3 source.
- Add: several rounds per frame with fractional-time offsets (needed for 75 rds/s at 60 fps). A light on a tracer only while fps is healthy. Staggered or lower-rate updates (fits ACE's 0.05 s). Whole-path at fire time for short shots. Decal cost = overlap × coverage, which explains the one-spot 4 fps. Lights-per-pixel and per-tile caps for muzzle and impact lights.
- Contradict or caveat: per-frame damage summing can "soak" a frame's hits. Apply hits in order and let rounds past a dead target continue, rather than one lump sum (refines point 5). A vendor prefers per-shot + tail fades over loops: fine at low rates, but no source shows it at 50-75 rds/s. "Pooling doesn't matter" holds only below the spawn rates we target. The Unity-forum "drop past ~100 job raycasts" is not borne out: batched raycasts reached 200k/s, so that limit is the setup (VR Quest 2), not raycasts.
