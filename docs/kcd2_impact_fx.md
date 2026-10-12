# KCD2 impact FX inventory (offline read-out)

Sources (all in `Data/IPL_GameData.pak` unless noted): `Libs/MaterialEffects/SurfaceTypes.xml`,
`Libs/MaterialEffects/MaterialEffects.xml` (an Excel-XML lookup table), `Libs/MaterialEffects/FXLibs/*.xml`,
`Libs/Particles/*.xml`, `Materials/decals/**/*.mtl`, `Data/Scripts.pak`, and the Lua-bind strings in `WHGame.dll`.
**Guess** marks anything not read directly.

## TL;DR
- **KCD2 already has a full set of bullet impacts.** `MaterialEffects.xml` has rows `hit_passed_bullet` /
  `hit_stopped_bullet` (plus arrow equivalents). They map every surface to an FX-lib entry with particle, decal and
  audio trigger (`c_mfx_bullet_*`).
- **Easiest path:** look up the effect with `MaterialEffects.GetEffectId("hit_passed_bullet", surfaceIdx)` and run it
  with `MaterialEffects.ExecuteEffect(id, params)`. We get the game's own sound + particle + decal per surface in one
  call (live test needed, section 6).
- **Manual path** (what we do now): `Particle.SpawnEffect("collisions.destructibles.bullet_<surface>", pos, normal)`
  plus `Particle.CreateMatDecal(pos, normal, size, life, "Materials/decals/destructibles/decal_<x>")`.
- **No round bullet-hole decal exists.** The closest are the destructibles decals (wood splinter, plaster chip, soil
  and mud holes, fabric tear) and `Materials/decals/arrow.mtl`. For metal there is no dent decal:
  `decal_metal_01.mtl` actually uses the mud textures.
- **Tracer base:** `WH_Particels.other.arrow_trail`, an additive, camera-facing streak (`Stretch=2`) using
  `arrow_trail_diff.tif`. `WH_Particels.other.flash` has a real dynamic light (radius 6, intensity 30) for muzzle and
  impact flashes.

## 1. Surface types (`SurfaceTypes.xml`)
The XML has no numeric ids; the engine numbers them in load order at runtime. Use
`System.GetSurfaceTypeNameById(id)` (DLL bind) on a ray hit's `surface` field. Pierceability: <=9 blocks rays and
bullets, 10 = pierce but not see, >=11 = see-through.

| group | surfaces (pierceability) |
|---|---|
| flesh / armor | `mat_flesh` 8, `mat_leather` 8, `mat_fabric` 10, `mat_chainmail` 4, `mat_plate` 2, `mat_bells` 2, combos `mat_fabric_plate` 5, `mat_fabric_chainmail` 4, `mat_chainmail_plate` 2, `mat_fabric_chainmail_plate` 2, `mat_leather_chainmail` 4 |
| metal | `mat_metal` 2, `mat_door_metal` 2, `mat_metal_seethrough` 11 |
| stone | `mat_rock` 2, `mat_rock_unwalk` 2, `mat_rock_horse_ignore` 2, `mat_stairs_stone` 7, `mat_plaster` 3, `mat_ceramics` 2 |
| wood | `mat_wood` 5 (type wood), `mat_wood_soft` 9, `mat_wood_unwalk` 5, `mat_door_wood` 5, `mat_shield_wood` 5, `mat_stairs_wood` 7, `mat_railing_wood_seethrough` 11, `mat_wicker` 10 |
| soil / sand | `mat_soil` 7, `mat_soil_unwalk` 7, `mat_mud` 8, `mat_gravel` 8, `mat_road` 10, `mat_field` 10, `mat_forest` 8 |
| grass / plants | `mat_grass` 7, `mat_grassdry` 8, `mat_grass_tall` 10, `mat_bushes` 10, `mat_vegetation` 10, `mat_vegetation_seethrough` 11, `mat_canopy` 10 |
| water | `mat_water` 11, `mat_mudwater` 8, `mat_rockwater` 2, `mat_slowwater` 2 (MaterialEffects also has a `mat_puddle` row) |
| straw / cloth items | `mat_thatch` 9, `mat_sack` 5, `mat_fabric_items` 10, `mat_organic_stuff` 3 |
| glass | `mat_glass` 14 (type glass, can_shatter) |
| helpers | `mat_none`, `mat_default`, `mat_invulnerable` 0, `mat_window` 15, `mat_raycast`, `mat_player_collider`, `mat_*_only`, barriers |

## 2. What the game plays per surface (`MaterialEffects.xml`)
The rows are interaction names and the columns are surfaces. Relevant rows: `hit_passed_bullet`, `hit_stopped_bullet`,
`hit_passed_arrow`, `hit_stopped_arrow`, `hit_stone`, `collision_stopped`, and melee rows
`hit_passed_sword_stab_*` etc. For bullets, passed and stopped map identically on world surfaces; they differ only on
armor.

| surfaces | bullet FX-lib entry (`FXLibs/<lib>.xml`) | particle | decal (scale min-max, life 60 s) | audio |
|---|---|---|---|---|
| flesh, fabric, leather, bells | `collisions:hit_passed_bullet_fabric` / `hit_stopped_bullet_fabric` | `collisions.combat.hit_passed_arrow_fabric` / `hit_stopped_arrow_fabric` (dir Reverse) | none | `c_mfx_fabric_bullet_passed` / `_stopped` |
| chainmail (+ combos) | `collisions:hit_{passed,stopped}_bullet_chainmail` | `collisions.combat.sword_plate` (sparks, 0.5-1.0) | none | `c_mfx_chainmail_bullet_passed` / `_stopped` |
| plate (+ combos), door_metal | `collisions:hit_passed_bullet_plate` / `hit_stopped_bullet_plate` | passed: `collisions.combat.sword_body_stab`; stopped: `collisions.combat.hit_stopped_bullet_plate` | none | `c_mfx_plate_bullet_passed` / `_stopped` |
| metal | `destructibles:bullet_metal` | `collisions.destructibles.bullet_metal` (1.1-1.3) | `destructibles/decal_metal_01` 0.1-0.2 | `c_mfx_bullet_metal` |
| rock, stairs_stone, glass, plaster | `destructibles:bullet_stone` | `collisions.destructibles.bullet_stone` | `decal_plaster_01/02/03` 0.2-0.3 | `c_mfx_bullet_stone` |
| wood, wood_unwalk, slope, stairs_wood, door_wood | `destructibles:bullet_wood` | `collisions.destructibles.bullet_wood` | `decal_wood_02` 0.2-0.3, `decal_wood_03` 0.2-0.32 | `c_mfx_bullet_wood` |
| shield_wood | `destructibles:bullet_shield_wood` | `bullet_wood` | wood_02/03 at 0.1-0.15 | `c_mfx_bullet_shield_wood` |
| gravel, soil, forest, wicker, road, field | `destructibles:bullet_soil` | `collisions.destructibles.bullet_soil` (0.9-1.1) | `decal_soil_01` 0.25-0.35 | `c_mfx_bullet_soil` |
| mud | `destructibles:bullet_mud` | `bullet_mud` | `decal_mud_01` 0.2-0.3 | `c_mfx_bullet_mud` |
| mudwater, slowwater | `destructibles:bullet_mudwater` | `bullet_mudwater` | none | `c_mfx_bullet_mud` |
| water, rockwater | `destructibles:bullet_water` | `bullet_mudwater` (no dedicated bullet splash) | none | `c_mfx_bullet_water` |
| grass, grassdry, bushes, grass_tall, veg_seethrough | `destructibles:bullet_grass` | `bullet_grass` | `decal_soil_01` 0.25-0.35 | old-style Sound `sounds/environment:combat/material:arrow_grass` |
| thatch | `destructibles:bullet_straw` | `bullet_straw` (1.0-1.3) | `decal_straw` 0.12-0.20 | `c_mfx_bullet_straw` |
| ceramics | `destructibles:bullet_ceramics` | `bullet_ceramics` | none | `c_mfx_bullet_ceramics` |
| sack | `destructibles:bullet_sack` | `bullet_sack` | `decal_fabric_01` 0.25-0.35 | `c_mfx_bullet_sack` |
| fabric_items | `destructibles:bullet_fabric_items` | `collisions.combat.hit_stopped_bullet_fabric` | none | none |
| vegetation | `collisions:veg_hit` | | | |
| default, invulnerable, player_collider | `destructibles:blank_event` | nothing | | |

- `destructibles:bullet_plaster` and `bullet_wicker` entries exist but no matrix cell points at them; we can call them
  by name.
- Arrows use the `arrow_*` versions: smaller particles and smaller decals (stone 0.04-0.06, wood 0.1-0.15).
  `arrow_water` is audio only.
- Particle attributes in these entries: `direction="Normal"` (along the surface normal) or `"Reverse"` (back along
  the shot), `attach="1"`, and `maxscaledist`, which grows the effect with camera distance.
- `Flowgraphs/water_bullet_hit.xml` only puts water droplets on the player's screen (HUD FX), so nothing in the world.

## 3. Particle effects
Format: `library.group.effect`, number of enabled sub-emitters, biggest base `Size` and longest
`ParticleLifeTime` (s), and textures. These are first-pass numbers read from `Params`; curves are scaled at runtime.

### Bullet impacts (`Libs/Particles/collisions.xml`), the full `collisions.destructibles.bullet_*` set
| effect | subs | size | life | look (textures) |
|---|---|---|---|---|
| `bullet_stone` | 6 | 3.0 | 5 | dust cloud, smoke, sparks + spark flash, debris |
| `bullet_plaster` | 4 | 1.5 | 5 | dust cloud + sparks |
| `bullet_wood` | 7 | 1.5 | 6 | wood chips, dust, smoke |
| `bullet_soil` | 4 | 3.0 | 5 | dirt cloud, smoke |
| `bullet_grass` | 5 | 3.0 | 6 | grass bits, dust, smoke |
| `bullet_mud` | 1 | 0.6 | 0.6 | mud splash |
| `bullet_mudwater` | 2 | 0.6 | 0.6 | mud splash (also used for clean water) |
| `bullet_metal` | 2 | 0.3 | 2 | sparks + flash |
| `bullet_straw` | 2 | 0.77 | 6 | straw + dust |
| `bullet_ceramics` | 3 | 1.5 | 6 | ceramic shards + dust |
| `bullet_sack` | 3 | 1.5 | 5 | dust puff |

The arrow equivalents (`collisions.destructibles.arrow_*`: wood, stone, plaster, metal, soil, mud, mudwater, grass,
ceramics, sack, straw, water) are smaller, sizes 0.15-0.38. `arrow_water` (size 0.75, life 3, foam ring) is the
best existing **water ring**.

### Sparks
| effect | size | life | notes |
|---|---|---|---|
| `collisions.combat.sword_sword` | 0.07 | 0.45 | sword clash |
| `collisions.combat.sword_plate` | 0.05 | 0.4 | used for bullets on chainmail |
| `collisions.combat.hit_stopped_arrow_plate` | 0.073 | 0.2 | |
| `collisions.combat.hit_stopped_bullet_plate` | - | - | **does not exist** in any particle library, although mfx `hit_stopped_bullet_plate` points at it; stopped-on-plate bullets play sound only. `hit_stopped_bullet_fabric` (used by `bullet_fabric_items`) is missing too |
| `collisions.combat.hit_stopped_mace_plate` | 0.02 | 0.5 | |
| `collisions.destructibles.arrow_metal` / `sword_metal` | 0.15 | 0.8 | |
| `particles_smithery.particles_smithery.hammering_hit_strong` | 0.023 | 4.0 | long-lived bouncing sparks |
| `particles_smithery.particles_smithery.sparks`, `sharpening` | 0.009-0.08 | 1-1.3 | grinding sparks |

### Dust, chips, splinters, dirt
- `collisions.destructibles.arrow_stone` / `sword_stone` / `arrow_plaster` (stone debris + dust, ~0.35, 6 s)
- `collisions.destructibles.arrow_wood` / `sword_wood`, `WH_Particels.other.wood_smashed` (0.4, 6 s),
  `particles_woodcutting.cutting.planing_wood_chips` (0.3, 1 s), `particles_woodcutting.cutting.wood_chip` /
  `wood_split` (mesh pieces, 8-15 s)
- `WH_Particels.other.explosion_dust` (3.75, 6 s: dust + stone + wood chips; good for big-calibre hits),
  `WH_Particels.other.trebuchet_hit` (10, 27 s: far too big)

### Water
- `collisions.destructibles.arrow_water` (foam ring 0.75, 3 s)
- `particles_water.streams.surface_impact_tiny` (0.69, 0.8 s) / `surface_impact_small` (1.2, 1.4 s) /
  `surface_impact_medium` (2.0, 1.0 s): river-impact splashes, usable as small, medium and big bullet splash
  (**guess**: built for streams, so they may loop; check)
- `WH_Particels.other.water_splash` (0.38, 0.75 s), `WH_Particels.other.water_drops`

### Blood
- On hit: `collisions.combat.hit_passed_bullet_fabric` / `hit_passed_bullet_plate` (6-7 subs, splat + drops, decals
  up to 20 s), `sword_body_stab` (1.1, front spray), `hit_passed_stab_fabric`, `hit_blood_animevent_a/b/dagger`
- Head: `collisions.combat.blood_head` (leak, 6.5 s)
- Cinematic: `cinematics.blood.blood_stab_a/b`, `blood_slash_a`, `blood_blunt_a`, `blood_splash_small`,
  `blood_leaking`, `blood_leaking_pista`
- In water: `particles_water.streams.water_blood_a/b` (10 s)

### Muzzle (`Libs/Particles/firearms.xml`), for reference
`firearms.hand_cannon.frontParticleEmit` (8 subs, size 2, 3 s: smoke, glow, sparks), `topParticleEmit` (touch-hole
flash), plus `...Npc` (bigger: 13 subs, 12 s smoke) and `topParticleEmitHorseback`.

## 4. Decals (`Materials/decals/`)
| material | textures | use |
|---|---|---|
| `destructibles/decal_wood_01/02/03` | `decal_wood_0N_diff/_ddna` | splintered hole in wood: the best bullet-hole look |
| `destructibles/decal_plaster_01/02/03` | `_diff/_ddna/_displ` (displacement) | chipped crater in stone / plaster |
| `destructibles/decal_soil_01` | soil diff/ddna + mud displ | dirt hole |
| `destructibles/decal_mud_01` | mud diff/ddna/displ | mud hole |
| `destructibles/decal_fabric_01` | fabric diff/ddna | cloth / sack tear |
| `destructibles/decal_straw` | straw diff/ddn | |
| `destructibles/decal_metal_01` | **mud textures** (`decal_mud_01_*`) | metal "dent" is really a mud mark; we need our own |
| `destructibles/decal_blood_slash` | wood_01 textures | misnamed; not blood |
| `arrow.mtl` | `decal_arrow_diff` | arrow mark (small hole / slit, **guess** at the look) |
| `decal_blood_a..e` (+ `_old`), `decal_puddle_g_blood`, `decal_bloody_palm_print(_b)` | `blood_a..e_diff` | blood splats / pools |
| `decal_red_leaking`, `decal_red_leaking_bright` | | blood running down walls (**guess** from name) |
| `decal_plaster_cracks_a..d` (+ orange / dark), `decal_plaster_damage_a/b`, `decal_plaster_stone_damage`, `decal_trosky_rocks_cracked_rock(1)` | `_diff/_ddna` | cracks around a big impact |
| `decal_burned_wood`, `decal_burned_ash`, `decal_burned_coalmakers`, `decal_smoke_stain(_big)`, `decal_gunpowder_spilled`, `decal_black_powder_cin` | | scorch / powder burn (muzzle-contact or explosive rounds) |

Nothing is a clean round bullet hole. The wood and plaster destructibles decals are the closest, and with their
displacement maps they look deep.

## 5. Trails and tracers
- `WH_Particels.other.arrow_trail` (Arrow.xml `<trail>`). One sub-emitter: Count 80, Continuous, EmitterLifeTime 12,
  ParticleLifeTime 0.8, Facing CameraX, BlendType **Additive**, OctagonalShape, Size 0.13, Stretch 2,
  InheritVelocity 0.03, Color 0.74 grey, Alpha 0.55, texture `textures/particles/arrow_trail_diff.tif`. This is a soft
  grey streak; for a tracer, copy it into our own library with orange colour, higher alpha, emissive lighting and
  shorter life (0.1-0.2).
- `WH_Particels.other.flash`: 1 particle, life 0.4, size 3, Additive, emissive 4.04, with a
  **LightSource** (AffectsThisAreaOnly, Radius 6, Intensity 30). This is a template for a glowing tracer head or an
  impact flash.
- Fire arrows: `WH_Particels.fires.burning_arrows` (flame + smoke, 2.2, 6 s), `WH_Particels.other.arrow_fire_vranik`,
  `WH_Particels.other.theresa_arrow_fire`.
- Torch trails (`WH_Particels.fires.torch_trailer`, `torch_trailer_less_sparks`): flame streams with sparks, size up to
  10. Too big, but they contain a moving-emitter spark sub-emitter we could borrow.
- DLL cvar `wh_ArrowTrailEffectStartDistance` delays when the arrow trail appears (value unknown).

## 6. Lua
| call | signature (DLL arg names) | seen in scripts |
|---|---|---|
| `Particle.SpawnEffect` | `(effectName, pos, dir, [scale], [entityId], [partId])` | `ShootableStashBase.lua:168` `Particle.SpawnEffect(self.Properties.ParticleEffect, hit.pos, hit.dir)`; `SpawnParticleEffect.lua:9`; `ChoppingBlock.lua:92` |
| `Particle.SpawnEffectLine` | `(effectName, startPos, endPos, dir, scale, slices)` | not used; spawns copies along a line, a cheap tracer option |
| `Particle.CreateDecal` | `(pos, normal, size, lifeTime, textureName, [angle], [hitDirection], [entityId], [partid])` | not used |
| `Particle.CreateMatDecal` | `(pos, normal, size, lifeTime, materialName, [angle], [hitDirection], [entityId], [partid])` | not used; takes `Materials/decals/...` paths |
| `Particle.IsEffectAvailable` / `CreateEffect(name, params)` / `DeleteEffect` | | |
| `MaterialEffects.GetEffectId` | `(customName, surfaceIndex2)`, e.g. `("hit_passed_bullet", surfaceIdx)` | |
| `MaterialEffects.GetEffectIdByLibName` | `(LibName, FGFXName)`, e.g. `("destructibles", "bullet_wood")` | |
| `MaterialEffects.ExecuteEffect` | `(effectId, paramsTable)`; table keys **guess** from CryEngine: `pos`, `normal`, `dir`, `scale`, `angle` | |
| `MaterialEffects.PrintEffectIdByMatIndex` | `(MatName1, MatName2)` | debug |
| `System.GetSurfaceTypeNameById` | `(surfaceId)` | |
| `entity:LoadParticleEffect(slot, effect, params)` / `PreLoadParticleEffect(effect)` / `FreeSlot(slot)` | | Lamp.lua, Lightning.lua: attached, looping emitters (tracer that follows an entity) |

The arg names come from the DLL's bind strings, which sit next to their function names. The CreateDecal versus
CreateMatDecal pairing is inferred from the `textureName` / `materialName` difference.

Live checks to do:
1. `MaterialEffects.ExecuteEffect(MaterialEffects.GetEffectId("hit_passed_bullet", idx), {pos=..., normal=...})` on
   wood / stone / water: do we get sound + particle + decal in one call?
2. Fire at plate: confirm the stopped-bullet hit plays no particle (the effect it names is missing), then pick our own spark effect.
3. `Particle.CreateMatDecal(pos, n, 0.25, 60, "Materials/decals/destructibles/decal_wood_02")` on a wall.
