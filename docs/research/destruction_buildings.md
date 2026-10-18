KCD2 building destruction: state of the art and what works in KCD2 (7 sonnet agents: the 5 planned + 2 for "mesh-world wizardry")

Tags: [V] = checked against source we read; [U] = unverified (memory, a search snippet, or an estimate).

Source coverage: most GDC talks are paywalled, so the AAA internals below come from slide decks and secondary write-ups. I checked the CryPhysics header claims myself against a CE5 mirror.

(a) METHODS RANKED BY QUALITY PER EFFORT FOR KCD2

1. Pre-fracture offline, swap chunks in at runtime, and drive collapse with a support graph. This is the current plan, and it is the right one.
   - Remedy's Control used a batch Houdini pre-fracture over hundreds of objects [U, talk not readable].
   - Red Faction Guerrilla used pre-broken meshes plus a stress model [V wiki].
   - The Bad Company recipe is the same idea: swap inside the blast sphere and hide the swap with smoke [U, Reddit commenter].
   - This is the only approach that scales to about 1,700 whole-house meshes without hand authoring.
2. CryEngine's own jointed-breakable physics as the graph solver [V docs + header].
   - Give one physical entity several parts and join them with `pe_params_structural_joint`.
   - When a joint is overloaded it is removed, connectivity is recomputed, and the disconnected parts become new rigid bodies (fires `EventPhysJointBroken`). That is the support graph, done natively.
   - Limits: 64 pieces per object [V CE3 doc], and parts with mass 0 act as ground.
   - Reddit reports joints going unstable past 20-30 connections, so it may wobble [U]. Our own BFS graph is the fallback (rank 3).
3. Our own support graph in C++. This is the robust form of the same thing.
   - Offline: chunks, plus bonds weighted by shared face area, plus an anchored flag on chunks touching the ground.
   - On a blast: apply damage with falloff, delete broken bonds, run a BFS from the anchors, and turn each unsupported island into ONE PE_RIGID body.
   - Cost is about 200 lines and well under 1 ms for 150 chunks.
   - The NVIDIA Blast stress solver header has no PhysX dependency [V] if real load paths are wanted later. Its value is towers that topple; players notice when they don't (Reddit 1wfdo0j).
4. Native shielding from `SimulateExplosion`. Its occlusion cubemap (`nOccRes`) and `IsAffectedByExplosion` give R6 Siege-style "the blast doesn't go through walls" for free [V docs]. Siege also used raycasts capped by blockers [snippet].
5. Mask or decal damage on the intact house for small hits.
   - Frostbite 2 used a signed distance field built from sphere primitives, stored as a low-res volume texture (about 2 m per texel). Costs were 0.2-0.8 ms on PS3 [V SIGGRAPH10 deck].
   - The cheap version is decals or an overlay whose opacity follows damage (Reddit).
   - A real masking shader needs a material hook, which is risky.
6. Runtime boolean holes. See the "Mesh-world wizardry" section below.
7. Skip: voxels (Teardown), cloud destruction (Crackdown 3), server-side destruction (The Finals), and FEM (Parker/O'Brien DMM).

(b) RECOMMENDED ARCHITECTURE

- Intact state: the house stays a static brush and costs nothing until it is first hit.
- On the first hit:
  1. The plugin hides the brush: `IRenderNode::SetRndFlags(ERF_HIDDEN)`, then remove or disable its physics (dephysicalize, or `pe_params_part` flags) [U for the KCD2 fork].
  2. Stream in `house_X_frac.cgf`: 30-64 chunks, convex proxies under 256 vertices, one shared "broken plaster/stone/wood" interior material.
  3. Spawn it as one multi-part entity. Anchored chunks are static, the rest are kinematic or sleeping.
- Graph: try the native joints first (`pe_params_structural_joint`, using `maxForcePush/Pull/Shift`, `maxTorqueBend/Twist`, `bBreakable`, `partid[2]` where -1 means ground, and `partidEpicenter`) [V fields]. If unstable, use our own BFS graph.
- Blast:
  - Call `IPhysicalWorld::SimulateExplosion(pe_explosion*)`. Fields: `epicenter`, `rmin/rmax/r`, `impulsivePressureAtR`, `nOccRes` [V].
  - The default `iTypes` is `ent_rigid|ent_sleeping_rigid|ent_living|ent_independent`. It does NOT include `ent_static` [V]. Only parts with `geom_colltype_explosion` receive the explosion [V doc].
  - Our damage falloff, `(1-d/R)^2`, drives bond breaking. Use `pe_action_impulse` at an off-centre point so pieces spin.
- Debris rules (all [U] proposals):
  - Pieces under about 0.5 m become particles. Rigid bodies are capped at 64 per event and 150 globally.
  - No debris-on-debris collisions. Try `g_breakageNoDebrisCollisions`.
  - Pieces sleep after about 0.5 s. Small pieces fade or sink after 5-10 s. Big settled pieces are frozen as static.
  - Beyond about 60 m, swap straight to the ruin with no simulation.
- Look and feel (timings [U]):
  - 0-3 frames: flash and a point light.
  - 0-0.4 s: fireball, a distortion ring and a ground dust ring.
  - 0.3-1.5 s after the blast: a base-footprint dust cloud as the building falls. This also hides the swap.
  - Up to 8 s: smoke column, scorch decals.
  - Trauma-based camera shake (shake = trauma squared, decaying about 1 per second).
  - Sound delayed by distance at 343 m/s, with near/mid/far layers, a collapse rumble, and tinnitus within 10 m.
- Persistence: per house, store a stable ID, the fracture-set version, a bitset of removed chunks, quantized transforms of settled big chunks (about 600 bytes), and a state of INTACT, DAMAGED or RUBBLE.
  - A RUBBLE house is replaced by a ruin mesh per archetype.
  - 100 destroyed houses come to about 60 KB.
- Memory: about 2-3 MB per fractured house, streamed on demand and freed once it becomes rubble. That fits easily in a few hundred MB [U, arithmetic].

(c) STAGED PLAN (person-days are [U] estimates)

- S0, probes (1-3 d).
  - In Ghidra, search for `RegisterExplosionShape`, `breakable_id`, `hole_size`, `EventPhysJointBroken`, `DeformPhysicalEntity`, `p_debug_joints`, `BreakableManager`, `SMFXBreakageParams`.
  - In the console, try `p_draw_helpers 1` and `p_debug_joints 1`.
  - In C++, build a 2-part entity, set a structural joint, apply an impulse, and catch `EventPhysJointBroken`.
- S1, FIRST PROTOTYPE (5-8 d).
  - Take one house by hand: Blender Cell Fracture into about 40 chunks, export a multi-node .cgf (`$physics_proxy` per chunk, `mass=`, `entity`).
  - In the plugin: hide the brush, spawn the chunks, run the BFS graph, call `SimulateExplosion`, and add dust VFX.
  - Done when a grenade at a corner drops that corner and the roof falls once its supports are gone.
- S2 (8-15 d): debris budget and LOD, persistence, ruin swap, comparison against native joints.
- S3, batch pipeline (10-20 d, plus 30-140 CPU-hours, i.e. a few hours on a server).
  - Headless steps: `blender -b -P`, pinned to 4.0.2 or later.
  - Voxel-remesh a watertight proxy (OpenVDB or Manifold).
  - Run a Voronoi with denser seeds near doors and windows, and grain-aligned cells for beams.
  - Transfer the materials, run CoACD hulls, then rc.exe.
  - Fallback: plane cuts along the walls.
- S4, safety (5-10 d).
  - Protected-house list: quest, shop and NPC homes.
  - NPCs inside, and attached doors, beds, chests and lights.
  - Local MNM navmesh rebuild (`AI:RegenerateMNM` exists [snippet]).
- S5, optional (10+ d): runtime boolean holes.
- Total: about 40-60 person-days.

(d) RISKS AND UNKNOWNS

- Hiding and dephysicalizing brushes in the KCD2 fork is untested.
- The rc.exe naming for physics proxies is [U]. The proxy pages returned only navigation.
- Most source houses are open shells, so fracture needs a remeshed proxy. That can blur details and break UVs.
- Native jointed breaking may be stripped from WHGame or unstable.
- Baked GI and probes stay lit inside a destroyed house. Lights left hanging must be killed.
- AI may get stuck after navmesh changes, and quest scripts may reference the house.
- Thread safety: mesh and physics rebuilds must run on the main thread.
- RAM: run one heavy job at a time while the game runs.
- Not found anywhere: KCD-specific destruction modding, how the Crysis shack was set up, the Destroyable Objects doc body, and shipped numbers for active-body caps.

MESH-WORLD WIZARDRY

- "Breaking Good" (Sellán et al., SIGGRAPH Asia 2022).
  - Precomputes "fracture modes" per object on a coarse tetrahedral cage. At runtime an impact is just projected onto those modes, so fracture follows the impact.
  - Python/libigl/TetGen/MOSEK. The modes take under a minute per cage.
  - Fits the offline batch: several impact-dependent fracture variants per house instead of one fixed set.
- Runtime holes.
  - Cherchi et al. 2022 booleans: about 20 ms at 25k triangles, against 390 ms for libigl [V paper].
  - Our own Manifold test: 4.4-4.9 ms per cut on a 4.7k-triangle room, 8.9-10 ms on 21.6k triangles. This was WSL2 on a synthetic watertight box, so it is best-case.
  - Manifold rejects non-manifold input. mcut accepts open surfaces but has no timings.
  - CoACD rebuilds convex hulls asynchronously. V-HACD is deprecated.
  - Design:
    1. Pre-split each house into wall patches of 1-3k triangles.
    2. Subtract one of 5-10 jagged cutters carrying the interior material, on a worker thread.
    3. Re-upload `IIndexedMesh` and rebuild the physics geometry on the main thread.
    4. Turn the removed volume into a debris chunk, then run the island check.
  - Risks: vertex streams and LODs must survive the cut, and repeated cuts degrade the mesh.
- CryEngine already has this built in [V header + docs].
  - `AddExplosionShape` or Lua `Physics.RegisterExplosionShape`, plus `pe_explosion.holeSize/iholeType`, plus `pe_params_part.idmatBreakable >= 0`, or `DeformPhysicalEntity(pent, pt, dir, r)`.
  - Needs closed, low-poly meshes. Use it on wall chunks, not whole houses.
- Cheap tricks from Reddit (indie, not shipped):
  - Hide swaps with dust.
  - Render rubble as GPU particles.
  - Bake about 6 fracture variants and pick one at random.
  - Run the stress or connectivity solve on a graph, not with joints.
  - Decals whose opacity follows damage.
  - A solo C++/Jolt runtime Voronoi engine reports about 6.7 ms under heavy tests.
- Two Minute Papers: only episode #4 was verified (thin-sheet glass tearing), and it isn't relevant. Müller 2013 VACD is the root of Blast: an impact-placed fracture pattern, convex pieces and island detection. Its ideas are already in the plan.

(e) SOURCES (R = fetched and read, S = search snippet only)

AAA:
- R https://www.slideshare.net/DICEStudio/siggraph10-arrdestruction-maskinginfrostbite2
- R https://www.ea.com/ea-studios/ea-sports/news/how-battlefield-6-redefined-destruction
- R https://www.redfactionwiki.com/wiki/GeoMod_2.0
- R https://www.gamedeveloper.com/design/designing-for-destruction-part-1-the-cost-of-destruction
- R https://gamingbolt.com/cloudgine-interview-crackdown-3-and-clouds-of-destruction
- R https://www.pcgameshardware.de/Teardown-Spiel-73677/Specials/PC-Game-Test-Benchmark-Voxel-Dynamic-Destruction-1394545/2/
- R https://gdcvault.com/play/1023003/The-Art-of-Destruction-in
- R https://news.ubisoft.com/en-us/article/4GHX2yepSaKkflLjLAlpwO/the-art-of-destruction-in-rainbow-six-siege-an-interview-with-julien-lheureux
- R https://80.lv/articles/gdc-talk-destructible-environments-in-control
- S https://gdcvault.com/play/1034280/Engineering-Mayhem-Technical-Deep-Dive
- S https://www.ubisoft.com/en-us/game/rainbow-six/siege/news-updates/1QkezaGoRkDWqcQ6duGvtk
- S https://windowscentral.com/inside-crackdown-3s-azure-cloud-powered-destruction

CryEngine:
- R https://raw.githubusercontent.com/ValtoGameEngines/CryEngine/master/Code/CryEngine/CryCommon/CryPhysics/physinterface.h
- R https://cryengine.com/docs/static/engines/cryengine-5/categories/23756813/pages/24282347 (Physical Breakage Systems)
- R https://cryengine.com/docs/static/engines/cryengine-5/categories/23756813/pages/24282320 (Physical Explosions)
- R https://www.cryengine.com/docs/static/engines/cryengine-5/categories/23756816/pages/23308020 (Breakable Objects)
- R https://www.cryengine.com/docs/static/engines/cryengine-5/categories/23756816/pages/23308030 (Boolean Destructibles)
- R https://www.cryengine.com/docs/static/engines/cryengine-5/categories/23756816/pages/56655880 (Physics Constraint Components)
- R https://www.cryengine.com/docs/static/engines/cryengine-3/categories/1114113/pages/1310916 (Jointed Breakable Objects)
- R https://www.cryengine.com/docs/static/engines/cryengine-3/categories/1114113/pages/1310904
- R https://cryengine.com/docs/static/engines/cryengine-3/categories/1638401/pages/1933376 (Physics Lua)
- S https://www.cryengine.com/docs/static/engines/cryengine-3/categories/1114113/pages/18384528 (MNM regeneration)

Engines, papers, libraries:
- R https://github.com/NVIDIAGameWorks/Blast
- R https://raw.githubusercontent.com/NVIDIAGameWorks/Blast/master/include/extensions/stress/NvBlastExtStressSolver.h
- R https://github.com/NVIDIA-Omniverse/PhysX/tree/main/blast
- R https://docs.omniverse.nvidia.com/kit/docs/blast-sdk/latest/docs/api/extensions/ext_authoring.html
- R https://www.dgp.toronto.edu/projects/breaking-good/
- R https://github.com/sgsellan/fracture-modes
- R https://ar5iv.arxiv.org/html/2205.14151
- R https://github.com/elalish/manifold
- R https://github.com/cutdigital/mcut
- R https://github.com/SarahWeiii/CoACD
- R https://github.com/kmammou/v-hacd
- R https://history.siggraph.org/?p=108389
- R https://pages.graphics.cs.wisc.edu/777-S11/2011/01/24/real-time-deformation-and-fracture-in-a-game-environment-4
- R https://github.com/LeonidasWhite/BCRYExporter
- R https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Chaos/FRemoveOnBreakData/__ctor
- R https://forums.unrealengine.com/t/released-chaos-debris-optimizer-1-1-0-ue5-chaos-debris-cleanup-plugin/2746154
- S https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/GeometryCollectionEngine/UGeometryCollectionComponent
- S https://sidefx.com/docs/houdini/unreal/chaosintegration.html
- S https://blenderartists.org/t/cell-fracture-in-python-script/1479494
- S https://projects.blender.org/blender/blender-addons/issues/105051
- S https://www.youtube.com/watch?v=A7Gut679I-o (Two Minute Papers #4)

Feel:
- R https://www.gamedeveloper.com/programming/video-sprucing-up-cameras-with-math
- S https://bevy.org/examples/camera/2d-screen-shake/
- S https://www.ea.com/frostbite/news/how-hdr-audio-makes-battlefield-bad-company-go-boom

Reddit (R, via the reddit skill; prefix https://www.reddit.com/r/):
- gamedev/comments/uop534
- gamedev/comments/vi8tvp
- gamedev/comments/1977d2j
- howdidtheycodeit/comments/18cpzvt
- unrealengine/comments/11yfj62
- unrealengine/comments/1pgm9en
- unrealengine/comments/1pd639u
- IndieDev/comments/1vt7fmm
- IndieDev/comments/1wfdo0j
- SoloDevelopment/comments/1vdvivp
- gamedevscreens/comments/1wk8pof
- UnrealEngine5/comments/17s1db0
