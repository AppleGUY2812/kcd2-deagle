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
