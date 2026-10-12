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
