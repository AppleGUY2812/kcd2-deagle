# Destruction research: everything so far (2026-10-18)

One page over all the destruction research for the KCD2 mod: explosives (grenades, airstrikes, tanks) that break houses,
dents in the dirt, real-looking bullet holes. Nothing is built yet. Details and sources are in the linked reports.

| report | what | by |
|---|---|---|
| [destruction_buildings.md](destruction_buildings.md) | state of the art + a KCD2 architecture and staged plan | Lead A, 7 agents |
| [surface_damage/README.md](surface_damage/README.md) | craters, mud / snow tracks, bullet holes (+ 5 agent notes) | Lead B, 5 agents |
| [destruction_feel.md](destruction_feel.md) | the user's requirements: nothing stiff, parts that hang on like sinew | the user + Claude 1 |
| [sinew_people_playground.md](sinew_people_playground.md) | People Playground decompiled: crack / sag / dangle / tear in code | agent |
| [destruction_re_teardown_hatred.md](destruction_re_teardown_hatred.md) | Teardown and Hatred decompiled: holes, collapse, staggered breaking | agent |
| [destruction_videos.md](destruction_videos.md) | 20 confirmed talks and videos | agent |

## 1. What KCD2 has to work with (checked in its code)
- KCD2 runs a CryEngine fork (~CryEngine 5 / Hunt: Showdown level). Its physics still contains CryEngine's breakage
  systems: breakable joints (`g_joint_breaking`, `p_tick_breakable`), breakable objects with tension, breakable trees,
  `e_DeformableObjects`, explosion shapes. Whether they still work is untested.
- Houses are mostly whole meshes: ~1,740 house models, with roofs (~100), windows (~115) and doors (~70) separate.
- `e_TerrainDeformations` ("Allows in-game terrain surface deformations") still exists, but in CryEngine 5 the height
  write behind it is an empty stub: probably dead, a 30-minute test decides.
- Its decals can't use parallax (POM) on our current path: the deferred-decal shader has no height slot.

## 2. Buildings: the plan (backed by Red Faction, Control, Battlefield, Hatred, Teardown)
1. **Pre-fracture offline:** each house model shattered into 30-64 chunks (Voronoi, denser near doors and windows,
   wood split along the grain), batch on the ProLiant (~1,700 houses overnight). "Breaking Good" (SIGGRAPH Asia 2022)
   can add several hit-dependent fracture variants per house.
2. **On a blast:** hide the intact house (render + collision), swap in its chunks; only chunks in the blast get an
   impulse. Teardown's tricks: three blast radii by material hardness (soft goes at R, medium 0.85 R, hard 0.6 R) and a
   little noise on each chunk's distance, so the break edge is jagged.
3. **What falls (support graph):** chunks touching the ground are anchors; after the blast, a flood fill from the
   anchors keeps what's still connected and turns every cut-off island into one falling body with a random spin
   (Teardown's rule: the biggest connected piece stays static). Add a load check per bond so a thin bridge can't hold a
   whole roof (Teardown has none). KCD2's own breakable joints may do this natively: test first.
4. **Timing:** staggered, never all at once: Hatred applies one break event every 1/6 s and chains with random
   0.1-0.3 s delays; the roof sags 0.2-1 s, cracks, then drops.
5. **Debris budget:** ~16 chunks per wall section; pieces under ~0.5 m as particles; 64 rigid bodies per event and 150
   overall; no debris-on-debris collisions; sleep after ~0.5 s; delete far, slow, small pieces (Teardown: >20 m away,
   <1 m/s, small); beyond ~60 m swap straight to the ruin.
6. **Feel:** flash + light (3 frames), fireball + ground dust ring (to 0.4 s), a dust cloud at the base that also hides
   the swap, a smoke column (to 8 s), scorch decals; camera shake by "trauma"; sound delayed by distance, a collapse
   rumble, the break sound chosen by the volume removed.
7. **Saving:** per house a stable ID, removed-chunk bits and settled big chunks (~600 bytes); fully ruined houses swap
   to one ruin mesh. 100 destroyed houses ~60 KB.
8. **Safety:** a protected list for quest, shop and home buildings; NPCs and furniture inside; navmesh rebuild.

## 3. Nothing stiff, and parts that hang on (the user's requirements)
- Stiff fixes: mixed chunk sizes and jagged edges, progressive failure, bendable joints, chunks that break again on a
  hard landing, crumbs and dust trailing pieces, mass / friction per material (stone heavy, thatch light).
- Sinew, from People Playground's code: stress builds when a joint is overbent and fades (x0.982 a tick); past the
  limit the joint goes floppy (its limit opens halfway) instead of breaking; on half of all tears a spring links the two
  torn edges (rest ~5 cm, 4 Hz, damping 0.5, 0.8 s invincible), drawn as 5-13 strands that thin and snap one by one as
  the gap grows, one holding to the end. Hanging ropes are a drawn curve; only hero ropes are real physics.
- From Teardown: a joint (or lamp) stays with whichever piece still holds its anchor: dangling for free.
- In CryEngine: joint pull / bend / twist strengths, rope entities, stretchy fibre strips skinned to both chunks.

## 4. Real holes in walls (later stage)
- CryEngine's own boolean hole-carving (`pe_explosion.holeSize`, explosion shapes, `DeformPhysicalEntity`) needs closed
  low-poly meshes: usable on wall chunks, not whole houses.
- Runtime mesh cutting with Google's Manifold: 5-10 ms per cut on a 5-20k-triangle wall section (measured), then
  rebuild the collision (CoACD). Possible, but late.

## 5. Ground and surfaces (Lead B)
- Craters: test the built-in terrain deformation first (30 min, throwaway save: craters would persist). Fallback that
  ships elsewhere (IL-2 Korea): crater meshes sunk 0.3-0.6 m into the ground, KCD2 dirt material with POM, grass
  cleared, a scorch decal and debris. 2-4 days.
- Mud / snow tracks: AAA uses a GPU height map + tessellation that KCD2's terrain lacks; realistic here: footprint and
  hoof decals from a pool + dust puffs. 2-3 days.
- Bullet holes with depth: tiny hole meshes with POM (height map, displacement <= 0.05), pooled; exit holes on thin
  planks; a light leak behind thin sunlit boards. Real see-through only on breakable geometry. 1-2 days.

## 6. Staged plan
| stage | what | time |
|---|---|---|
| S0 | probe: do KCD2's breakable joints, explosion shapes and hole-carving still work (code search + a 2-piece test in game); the 30-min crater test | 1-3 days |
| S1 | prototype: one house, one grenade: corner drops, roof falls, nothing stiff, something hangs on; recorded in game, the user judges | 5-8 days |
| S2 | debris budget, distance, saving, ruin swap | 8-15 days |
| S3 | batch pipeline: fracture all houses on the ProLiant | 10-20 days |
| S4 | safety: protected buildings, NPCs, navmesh | 5-10 days |
| S5 | optional: runtime holes | 10+ days |
Total ~40-60 days. Ground and holes run beside it: craters 2-4 d, holes 1-2 d, tracks 2-3 d.

## 7. Open risks
Hiding static houses and their collision in KCD2's fork is untested; the breakage systems may be stripped; most house
models are open shells (fracture needs a closed proxy); baked lighting stays lit inside a ruin; quest scripts and AI;
CryEngine joints may go unstable past 20-30 links.
