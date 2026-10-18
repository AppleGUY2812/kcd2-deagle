# Surface damage research (Lead B, 5 agents, 2026-10-18): dirt dents / craters, mud & snow tracks, bullet holes

Engine claims come from the public CryEngine 5 mirror (ValtoGameEngines/CryEngine@release) as a proxy for KCD2's fork
("roughly Hunt 1896 level"); all are UNVERIFIED in WHGame.dll until tested. Per-topic notes: agent1-5 files here.

## Findings that change the plan
1. Stock craters are probably dead: OnExplosion -> CTerrain::MakeCrater -> CTerrainModifications::MakeCrater ->
   CTerrain::SetZ, and SetZ is a stub in CE5 (terrain_hmap.cpp: "NOT CURRENTLY SUPPORTED DUE TO NEW INTEGER DATA").
   KCD2 still has e_TerrainDeformations ("Allows in-game terrain surface deformations"), TerrainMods (save list),
   DeformTerrain / DeformTerrainUsingMat (Lua), GetTerrainElevation: testable from Lua in 30 min on a throwaway save
   (craters would persist). If it works: depth 1 - d/r m (cone, max ~1 m), saved, physics follows; it also deletes every
   decal in the radius and small vegetation.
2. POM on our current bullet holes does nothing: Particle.CreateMatDecal goes through the deferred decal pass, whose
   shader samples diffuse / normal / smoothness / opacity only (KCD2's stage binds the same slots, docs/whgame_re/decals.md).
   KCD2's own POM decals are Illum on mesh/quad geometry. Silhouette POM is stripped when %DECAL is set.

## Ranked (quality per effort for KCD2)
- Dirt: 1) mesh crater stamps (IL-2 Korea): Blender crater .cgf variants, Illum + KCD2 dirt textures + POM, sunk
  0.3-0.6 m, OnExplosion(pos, r, false) to clear grass, then a scorch decal + debris; 2) stock deformation if the test
  works; 3) deferred scorch decal for small rounds; 4) plugin heightmap writes (weeks, high risk).
- Mud / snow tracks: AAA all use a height render target + tessellation (Batman AO, GoW, RDR2, Spintires); KCD2's terrain
  has no tessellation path. Realistic: pooled footprint / hoof decals (own CDecalRenderNode pool) + particles.
- Bullet holes: 1) POM mesh decals (tiny quad .cgf, Illum %DECAL%NORMAL_MAP%PARALLAX_OCCLUSION_MAPPING, _displ height,
  PomDisplacement <= 0.05, pooled, 1 mm off the surface; flat surfaces); 2) particles + per-material deferred variants;
  3) exit holes on thin planks (second ray); 4) light leak behind thin sunlit planks; 5) real see-through only with
  breakable geometry (pe_explosion holeSize on breakable parts) or per-asset alpha-cut swaps.

## Staged plan (estimates)
S0 0.5 d: crater test + Ghidra check; S1 1-2 d: POM mesh-decal holes; S2 2-4 d: crater stamps; S3 2-3 d: footprints;
S4 optional 1-3 weeks: heightmap writes or render-target tessellated patches.
Building destruction: ../destruction_buildings.md (Lead A).
