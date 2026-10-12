# Bullet impacts: effects, bullet holes, build

Our own impacts, built from the Ready or Not export (`docs/ron/ron_impact_fx.md`). They replace KCD2's hand-cannon puffs.
Ripped textures are for private use only. The DDS files are gitignored and never go in git.

## 1. Effects (`src/Libs/Particles/deagle_impacts.xml`, library `deagle_impacts`)
Spawn with `Particle.SpawnEffect("deagle_impacts.<family>", hitPos, hitNormal, scale)`. The emitter's y axis is the
surface normal (`EmitAngle` opens a cone around it, 0 = straight out).

**Scale 1 = a 9 mm pistol round.** Lua scales by round energy (Deagle about 2). Sizes and speeds follow Ready or Not's
readable Cascade impacts, converted from UE cm to m. `Size` is half the RoN sprite width, because RoN's pistol
impact BP uses the same "Big" systems as rifles. Every part lives at least 0.08 s after its Random spread, since a new
emitter is pre-aged by one frame (checked by script: no part is shorter).

| effect | parts | look (RoN source) |
|---|---|---|
| `metal` | flash (glow + light r 1.5 m), sparks_fast (~18, 5 m/s, 1 g, bounce), sparks_slow (6), smoke_kick, bits (6) | `P_Bullet_Impact_Metal_Big`. **Also used for hits on plate armour.** |
| `stone` | spark_flash, spatter (velocity-aligned spray), base_dust, dust (grey 0.79, 1.8 s), chips (~16, 7 m/s, bounce) | `P_Bullet_Impact_Concrete_Big`. Stone, concrete, brick, ceramics |
| `plaster` | spatter, dust_jet (white), powder (squib 8x8), chips (~14 white) | `P_BulletImpact_Plaster` |
| `wood` | spatter, smoke_kick (warm 1/0.83/0.75), base_dust, splinters (~8, 2 s, bounce), specks | `P_Bullet_Impact_Wood_Big` |
| `soil` | kick (2 squibs thrown up), cloud (brown), wisp (dark), bits (~20) | `P_VFX_Impact_Dirt`. Soil, dirt, grass, mud, gravel |
| `sand` | kick + cloud in pale tan, fine grains | the dirt kick, lighter and finer |
| `water` | column (~7 drops), droplets (~10 at 1.5 g), puff, mist | `P_BulletImpact_Water` |
| `straw` | fluttering straw bits (air drag 4, gravity 0.35), light dust | (no RoN straw; recoloured shreds) |
| `cloth` | puff_fast, smoke_round, fibres (~10, air-braked) | `P_Bullet_Impact_Fabric_Big` |
| `glass` | flash, quick bright whisp, shards (~10, 3x3 variants, bounce) | `P_VFX_Impact_Glass_Plate_Exit` |

Not in RoN's readable data (Niagara bytecode) and therefore our choice: exact counts on the `NS_*` systems, plus
everything about straw.

## 2. Bullet holes (`src/Materials/deagle/decals/bullet_hole_<surface>.mtl`)
`Particle.CreateMatDecal(pos, normal, size, lifeTime, "Materials/deagle/decals/bullet_hole_<surface>")`.
The format follows KCD2's `Materials/decals/destructibles/decal_wood_02.mtl` (Illum, `%DECAL%NORMAL_MAP`). Normals come
from RoN with **green flipped**: KCD2 is green-up and UE is green-down. I checked this on KCD2's `decal_plaster_01_ddna`,
whose normal field is only curl-free with KCD2's green as is, while RoN's is only curl-free with green flipped.

| decal | RoN textures (diffuse+opacity / normal) | smoothness | use on |
|---|---|---|---|
| `bullet_hole_metal` | Metal_Hard_BO / Metal_Hard_N | 0.55 | metal, door_metal, plate armour items |
| `bullet_hole_stone` | Concrete_Hard_BO / _N | 0.2 | rock, stairs_stone |
| `bullet_hole_brick` | Brick_BO / _N | 0.15 | brick walls (no KCD2 surface; pick by material if wanted) |
| `bullet_hole_plaster` | Plaster_BO / _N | 0.15 | plaster |
| `bullet_hole_wood` | WoodHard_BO / _N | 0.2 | wood, door_wood, stairs_wood, shield_wood |
| `bullet_hole_wood_soft` | WoodSoft_BO / _N | 0.15 | wood_soft, wicker |
| `bullet_hole_soil` | Dirt_BO / _N | 0.1 | soil, gravel, road, field, forest, mud, grass |
| `bullet_hole_cloth` | Fabric_BO / Cardboard_N | 0.1 | sack, fabric_items |
| `bullet_hole_glass` | Glass_BO (cracks) / Metal_Hard_N | 0.8 | glass, window |
| `bullet_hole_ceramic` | Tile_BO / _N | 0.6 | ceramics |

**Size:** RoN's pistol range is 3-4 cm (`ImpactEffects_Pistol` DecalMinSize 3 / DecalMaxSize 4, rifle 4-6, exit 6-12),
so use `size = 0.03 + 0.01*random` at scale 1, times the same energy scale as the effect. The hole fills about the
middle half of each texture. KCD2's own bullet decals use 0.2-0.3, so check in game whether CreateMatDecal's size is
full width or radius (**unverified**). Lifetime: RoN's comes from C++ and isn't readable; KCD2 uses 60 s.

## 3. KCD2 surface -> effect + decal
Surfaces from `docs/kcd2_impact_fx.md` section 1 (`System.GetSurfaceTypeNameById(hit.surface)`).

| KCD2 surfaces | effect | decal |
|---|---|---|
| `mat_metal`, `mat_door_metal`, `mat_metal_seethrough`, `mat_plate`, `mat_bells`, `mat_chainmail` and all plate / chainmail combos | `metal` | `bullet_hole_metal` on world metal; none on worn armour |
| `mat_rock`, `mat_rock_unwalk`, `mat_rock_horse_ignore`, `mat_stairs_stone`, `mat_rockwater` (if not water) | `stone` | `bullet_hole_stone` |
| `mat_ceramics` | `stone` | `bullet_hole_ceramic` |
| `mat_plaster` | `plaster` | `bullet_hole_plaster` |
| `mat_wood`, `mat_wood_unwalk`, `mat_door_wood`, `mat_shield_wood`, `mat_stairs_wood`, `mat_railing_wood_seethrough` | `wood` | `bullet_hole_wood` |
| `mat_wood_soft`, `mat_wicker` | `wood` | `bullet_hole_wood_soft` |
| `mat_soil`, `mat_soil_unwalk`, `mat_mud`, `mat_gravel`, `mat_road`, `mat_field`, `mat_forest`, `mat_grass`, `mat_grassdry`, `mat_grass_tall`, `mat_bushes`, `mat_vegetation*`, `mat_canopy` | `soil` | `bullet_hole_soil` (none on grass_tall / bushes / vegetation / canopy) |
| sand (no KCD2 surface; use for `mat_road` / `mat_field` if they read too dark) | `sand` | `bullet_hole_soil` |
| `mat_water`, `mat_mudwater`, `mat_slowwater`, `mat_puddle` | `water` | none |
| `mat_thatch`, `mat_organic_stuff` | `straw` | none (or KCD2 `decal_straw`) |
| `mat_sack`, `mat_fabric_items`, `mat_fabric`, `mat_leather` (non-body) | `cloth` | `bullet_hole_cloth` |
| `mat_glass`, `mat_window` | `glass` | `bullet_hole_glass` |
| `mat_flesh` and body hits | keep KCD2 blood (`collisions.combat.hit_passed_bullet_fabric`) | none |
| `mat_default`, `mat_invulnerable`, helpers | `stone` (or nothing) | none |

## 4. Rebuild
1. Textures (TIF with alpha, 35 files) live in `E:\Tools\deagle-work\fx_tif_impacts\`. They are made from the RoN PNG
   export (`E:\Tools\deagle-work\ron_fx\`) by the session's converter: particles are copied (sparks/glow premultiplied
   onto black, glass/straw recoloured, drop alpha from luminance); decals keep `_BO` as colour+opacity, and normals get
   green flipped plus smoothness in alpha. The source map is in `fx_tif_impacts\_particles.json` / `_holes.json`.
2. Compile and stage: `powershell.exe -ExecutionPolicy Bypass -File tools\fx_impact_textures.ps1` (rc.exe presets
   `AlbedoWithGenericAlpha` / `Decal_AlbedoWithOpacity` / `NormalsWithSmoothness`, streamed). This writes 33
   textures, 178 DDS files incl. mips, into `src/Textures/particles/deagle/impacts/`.
3. `build.sh` packs `Libs/Particles/deagle_impacts.xml`, `Materials/deagle/decals/*.mtl` and the staged DDS like the
   muzzle FX.

Texture list: `imp_dust` (thinSmoke 8x4), `imp_dustjet` (dustImpact 16x2), `imp_squib` (8x8), `imp_wisp` (8x8),
`imp_spray` (debrisSpray 4x1), `imp_sparks` (longSpark 4x2, additive), `imp_glow` (additive), `imp_stones` (2x2),
`imp_splinters` (2x2), `imp_glass` (3x3), `imp_fabric` (2x2), `imp_straw` (2x2), `imp_drop`; `hole_<surface>_diff` /
`_ddna` for the 10 decals.

## 5. Wallbangs, exits and ricochets
### Data: `src/Scripts/Deagle/guns/penetration.lua`
`Deagle.penetration[<family or mat_* surface>] = { family, ron, density, armour, penetrable, maxDepth = { mm9, ae50 }, ricochetChance, ricochetMaxAngle, spall }`,
plus `Deagle.penetrationAmmo["9x19" / "50ae"]` and `Deagle.ricochetMaxAngle = 30`. The file only fills tables. It has
to be loaded next to `calibers.lua` (not done here: `deagle_main.lua` is off limits for me).

What the values mean (RoN's combining code is native and was **not read**; this is the inferred model):
- **Gets in?** Only if `penetrable` and the round's level is at least `armour`. 9x19 FMJ is level 2, .50 AE FMJ level
  3, JHP 0 (RoN AmmoDataTable). Surfaces from RoN PenetrationData: steel 4, concrete strong / sheet metal 3,
  concrete soft 2, everything else 0.
- **How far?** `maxDepth = budget / density` (m). The budget is RoN's per-weapon `PenetrationDistance`: 9 mm guns
  ~10 cm (MP5, M92). For the .50 AE I **guessed** 30 cm, since RoN has no .50 AE gun (.45 = 8, .357 Python = 55). Examples:

| surface (RoN) | density | armour | 9 mm | .50 AE | ricochet (9 mm, < 30 deg) | spall |
|---|---|---|---|---|---|---|
| drywall | 0.6 | 0 | 17 cm | 50 cm | no | 0 |
| plaster | 0.8 | 0 | 12.5 cm | 37.5 cm | 0.1 | 0 |
| wood (plywood / pine) | 1 (default, guess) | 0 | 10 cm | 30 cm | plywood no / pine 0.1 | 0 |
| brick | 1.95 | 0 | 5.1 cm | 15 cm | 0.1 | 0 |
| stone (concrete strong) | 5.5 | 3 | stopped | 5.5 cm | 0.1 | 0.3 |
| sheet metal / plate armour (galvanized) | 2 | 3 | stopped | 15 cm | 0.125 | 0.7 |
| solid metal (steel) | 8 | 4 | stopped | stopped | 0.1 | 0.2 |
| glass | 0.6 | 0 | 17 cm | 50 cm | no | 0 |
| flesh | 3 | 0 | 3.3 cm | 10 cm | no | 0 |
| soil / sand / grass ground | - | - | not penetrable | not penetrable | no | 0 |

- **What's left after a wallbang:** RoN's speed / damage loss per surface is C++. **Guess:** keep
  `1 - depth / maxDepth` of the energy.
- **Ricochet:** only if the surface can ricochet and the shot hits within `ricochetMaxAngle` (30 deg, Projectile_Base
  RequiredAngleToDeflect) of the surface. Chance = ammo RicochetChance (9 mm FMJ 0.1; RoN gives the .50 AE 0) x the
  surface multiplier (sheet metal 1.25).
- **Spall:** a stopped round on an armoured surface throws fragments with probability `spall` (RoN SpallingChance),
  with ammo SpallingDamage 10-15 within 0.6 m.

### Exit effects (`deagle_impacts.xml`): emitter +Y = the round's travel direction out of the far side
| effect | use for | RoN source |
|---|---|---|
| `stone_exit` | stone, brick, ceramic, rock | `P_VFX_Impact_Brick_Exit`, `Concrete_Soft_Exit`: fast dust jet (7 m/s, 0.28 s), wide puff, hot fragments, chips |
| `plaster_exit` | plaster, drywall | `P_VFX_Impact_Plaster_Exit` + `Drywall_Exit`: white whisps, slower cloud, sparks, bits |
| `wood_exit` | wood, wood_soft, doors, shields | `P_VFX_Impact_Pine_Exit`: brown smokeball, squib, spatters + splinters |
| `metal_exit` | sheet metal, plate armour | `P_VFX_Impact_Steel_Exit`: sparks, tiny-spark burst, red-hot bits, smoke, shards |
| `glass_exit` | glass, windows | `P_VFX_Impact_Glass_Plate_Exit`: glass bits, bright whisp, sparks |
| `cloth_exit` | cloth, sack, straw, flesh-free fabric | `P_VFX_Impact_FabricThin_Exit`: smoke, fibres, tiny flash |

**Exit holes:** RoN's ExitImpactEffects use the same `Decal_Impact_Inst_*` textures as our holes, just bigger:
**6-12 cm** (DecalMinSize 6 / Max 12) versus 3-4 cm on entry. So there are no separate `_exit` materials: reuse
`bullet_hole_<surface>` on the far side, at `size = 0.06 + 0.06 * random` at scale 1.

### Ricochet effects: emitter +Y = the round's NEW direction
| effect | use for | RoN source |
|---|---|---|
| `ricochet_metal` | metal, plate armour on people, chainmail | Projectile_Base `RichochetParticle` = `Electrical_impact_Large`: 55 sparks in a 0-45 deg cone at 5-25 m/s, a 10-spark burst with light, two 0.2 s flashes, smoke along the new path. RoN's colour is electric blue; ours is hot-metal orange |
| `ricochet_stone` | stone, brick, plaster, wood_soft | RoN replays the concrete impact at ParticleScale 0.33: small spark flash + streaks, a little dust, chips |

No new textures were needed: the hot fragments use `imp_glow`, and everything else reuses the impact set. All parts
live >= 0.08 s (checked by script) and scale 1 = 9 mm.

## 6. Blood (`src/Libs/Particles/deagle_blood.xml`, library `deagle_blood`)
Source: RoN `Data/Blood_Standard` (BloodData), which every character uses:
- **Particles:** HitEntryParticles = `Blood_Smoke_Impact_Red`; flesh exit = `P_VFX_Impact_Flesh_Exit`; big burst = `PS_Blood_Burst_Directional`.
- **Decals:** Splatters = `Blood_01..07_Inst`; HeadshotSplatters = `MI_Blood_Headshot_01..03` (38-60); artery drops = `Blood_Drop_01..05` (15-20); animated wall splatters 45-52 within 3 m; pool = `BP_RoNBloodPool` (DecalSize 8 x 64 x 64).
- **Units:** sizes in UE cm. UE DecalSize is a half extent, so a pool is ~1.28 m across.

`Particle.SpawnEffect("deagle_blood.<effect>", pos, dir, scale)`. Scale 1 = 9 mm, and Lua scales by energy as for the impacts.
All parts live >= 0.08 s (script-checked).

| effect | emitter +Y | parts (RoN numbers) |
|---|---|---|
| `entry` | surface normal at the wound (back toward the shooter, against the round) | splat 0.08-0.16 s; spray streaks 12 (3 m/s); flecks 22 (5 m/s, ~18 deg cone, 1 g, stick on contact); red mist 2 (0.5-1.3 s, drag 7.5); dust kick |
| `exit` | the round's travel direction | core splat; 3 slash strands (2.2 m/s); spray 18 (4 m/s); 30 drops (5 m/s, 20 deg); 8 blobs; mist |
| `head` | travel direction (burst out of the back of the head) | 2 core splats; 10 ropes + 3 big ropes; 16 big blobs (3.5 m/s); far spatter 15; slow spatter 12; 40 drops (30 deg); big mist |
| `armor_entry` | surface normal | cloth tuft fast 12 (5 m/s, air drag) + slow 6 (1.6 s); fabric puff; small blood splat, 8 flecks, light mist |

Blood colour: RoN's linear dark red, (0.09-0.2, 0, 0) on sprites and (0.025-0.09, 0, 0) on decals. Here it is lifted to
~(0.34, 0.012, 0.01) on the particles for KCD2 lighting, and sRGB (92, 6, 4) on the decals (pool (62, 3, 2)).

### Blood decals (`src/Materials/deagle/decals/`)
Same format as the bullet holes. The normals are built from the mask (or RoN's own, with green flipped), with wet
smoothness 0.8-0.95. Natural size is the full width in m at scale 1. Multiply by the energy scale and add ±15% at
random, then rotate randomly.

| material | look (RoN source) | natural size (m) | use |
|---|---|---|---|
| `blood_spatter_1` | directional wall spray (`Decal_Blood_Splat_1`, RoN exit/shotgun BloodExitDecals) | 0.45 | wall behind an exit, aligned with the shot |
| `blood_spatter_2` | wide wall spray (`Decal_Blood_Splat_2`) | 0.5 | wall behind an exit |
| `blood_spatter_3` | round spray (`Blood_04` mask) | 0.4 | wall / floor near the target |
| `blood_spatter_4` | sideways spray (`Blood_07` sideways mask) | 0.45 | wall at a grazing angle |
| `blood_spatter_5` | headshot burst (Megascans albedo, `MI_Blood_Headshot_01`) | 1.0 | wall behind a head hit (RoN 38-60 half-size) |
| `blood_spatter_6` | drips / drops (`Blood_Drop_04`) | 0.18 | floor under a wound, artery drops |
| `blood_spatter_7` | drop trail (`Blood_Drop_05`) | 0.18 | floor, walking wounded |
| `blood_pool` | pool (RoN `T_BloodPool_Packed` R channel at full expansion) | 1.28 | under a body. RoN grows it (BloodExpansion) after `BloodPoolSpawnDelay`; we can re-place it at growing sizes |

RoN only places wall splatters within `AnimatedSplatterMaxDistance` = 3 m behind the target. Lifetime is C++.

### Rebuild
Blood TIFs (`bld_*_diff`, `blooddecal_*_diff/_ddna`, 22 files) sit in `E:\Tools\deagle-work\fx_tif_impacts\`, made by
the session's blood converter from `E:\Tools\deagle-work\ron_fx\blood\` (86 PNG). The source map is in `_blood.json`.
`tools/fx_impact_textures.ps1` compiles them along with the impact set (presets: particles AlbedoWithGenericAlpha,
decals Decal_AlbedoWithOpacity / NormalsWithSmoothness). Staged total: 318 DDS files. Contact sheet:
`Desktop\Deagle Progress\blood_textures_sheet.png`.
