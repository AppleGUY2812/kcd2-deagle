# Destruction RE: Teardown 0.9.5 and Hatred (static Ghidra 12 + asset reads, 2026-10-18)

Tags: [V] read in the decompile, asset or Lua; [I] inferred. Ghidra projects: ~/re/proj/TEARDOWN and HATRED on the
ProLiant. Scripts and outputs: ~/re/work/destruction/.

## Teardown (voxels, connectivity only)
Way in: the Lua API table at 0x14043a000, with {fn, name, 0} entries.

- `Explosion` FUN_140162aa0 → FUN_1400d7640 [V]: s is clamped to 0.5-4.
  - Radius `R = max(2.1s, 0.4)`.
  - MakeHole(R, 0.85R, 0.6R); 5 fires spawned within 0.1-0.6R.
  - Impulse on bodies within 6R, ray-occluded: `s*10000/(d+1)*(1-d/6R)`, applied at the closest point.
- `MakeHole(pos, rSoft, rMed, rHard)` FUN_140168310 → FUN_140127560 [V]:
  - Carve FUN_140124fd0: the voxel distance gets sin/cos noise, so edges are jagged. A voxel is removed if
    `materialTier[mat]` ≤ the tier its distance falls in (rHard/rMed/rSoft).
  - The removed inner voxels are split and spawned as small dynamic debris bodies.
  - Split FUN_140126960: a 6-neighbour flood fill. Each island after the first moves into a new volume.
  - Regroup FUN_1401265e0: **the biggest group keeps the original body (static stays static). Every other group becomes
    a new dynamic body**, woken, with a random 0.5 kick (probably spin, [I]).
  - FUN_1401298b0: **joints and lights move to whichever piece still holds their anchor point, otherwise they detach.**
  - Break sound by voxels removed: <151 small, <501 medium, else large.
- There is no stress model [V]: one voxel bridge holds a whole house.
- Fire FUN_1401335c0 [V]:
  - Heat from neighbours within 3 m: `(1-(d/3)^2)^2`.
  - Per-frame caps: ≤2 scorches, ≤1 burn hole, ≤4 new fires.
- Lua [V]:
  - explosion.lua: 25/50/100 crumb particles (life 0.5-3.5 s, sticky); flash 0.1-0.4 s; the large blast adds a ring of
    126 dust puffs moving sideways at 8-12 m/s.
  - debriscleanup.lua deletes broken pieces that are >20 m away, <1 m/s and <200 voxels.

## Hatred (UE4 + APEX 1.3)
- Content [V, 398 DestructibleMesh assets from the pak]:
  - Houses are **modular**: walls, floors and doors are separate destructibles.
  - 3-51 chunks per asset (median about 16; walls average 18-30).
  - All Voronoi, mostly authored in PhysXLab. The APEX blob stores `CachedOverlaps` (the chunk adjacency).
- Parameters [V]:
  - DamageThreshold: walls 400-700 (mostly 600); props 20-300.
  - SupportDepth 1 (370 assets). bWorldSupport 372.
  - **bFormExtendedStructures 187**: neighbouring modules support each other.
  - Debris depth 1, lifetime 30-50 s.
  - Impact damage on only 12 assets.
- Fork-only flags [V]: CanAffectNavigation, DoNotBlockExplosions, PawnCanJumpOver, DisableFireDamage.
- Code [V]:
  - ApplyDamage FUN_1410fe470 / ApplyRadiusDamage FUN_1410fe650 (from natives FUN_14130dfd0), both gated by destruction
    detail mode.
  - Weapon hit FUN_14012ebf0: ApplyRadiusDamage(DamageVsDestructibles, radius).
  - HP gate FUN_140165870: at HP ≤ 0, **ApplyRadiusDamage(1000, origin, 300 cm, full) shatters the whole piece.**
  - **Game tick FUN_14018fd70: one queued destructible damage event per 1/6 s.**
  - OnCollide FUN_1401ac900: impulse ≥ threshold → **break after Min+r·(Max-Min)+0.2+0.1r s**.
- Explosives [V]:
  - Health 100-1500; damage 1000-1500.
  - Outer radius 120-200 cm.
  - **Chain delay random 0.1-0.3 s.**

## Transfers to KCD2
1. **Support graph = Teardown's split + regroup.** BFS from the chunks touching terrain. The anchored component stays
   static; each unanchored island becomes ONE rigid body.
2. **Material tiers.** One blast checks three radii: plaster/thatch at R, timber 0.85R, stone 0.6R. Add noise to each
   chunk's distance so holes aren't spheres.
3. **Sinew comes almost free.** After a break, keep each joint whose anchor chunk survives and drop the rest.
4. **Timing.** Teardown collapses instantly. Hatred staggers: a 0.2-0.3 s break delay, 0.1-0.3 s chain delays, 6 events
   a second. Queue the support failures like that to get the 0.2-1 s sag from destruction_feel.md.
5. **Add load.** Connectivity alone lets one beam hold a house, so add a mass-above threshold per bond.
6. **Debris budget.**
   - About 16 chunks per wall module (Hatred); 30-64 per house fits.
   - Crumbs as particles.
   - Clean up pieces that are far, slow and small.
7. **Feel tricks.**
   - A random spin on freed bodies.
   - An off-centre impulse with `1/(d+1)` falloff.
   - A sideways dust ring.
   - Break sound picked by the volume removed.
   - Shatter at 0 HP for props.
8. **Modules.** Group chunks into wall modules with world support and neighbour support, and a threshold per module.

## Doesn't transfer
- Runtime voxel carving (Teardown).
- APEX/PhysX (KCD2 uses CryPhysics).
- Hatred's isometric camera hides the fact that buildings never collapse whole.

Not found:
- What a negative FractureImpulseScale (78 assets) does.
- The per-weapon DamageVsDestructibles values (C++ defaults).
- The material-tier and noise constants (addresses noted, bytes not read).
