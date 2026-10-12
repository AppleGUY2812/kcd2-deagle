# KCD2 armor, damage and hit reactions (offline read-out)

Sources: `Data/Tables.pak` (`Libs/Tables/...`), `Data/Scripts.pak`, and the string tables in
`Bin/Win64MasterMasterSteamPGO/WHGame.dll` (game build of 2026-10-09). Paths below are inside those paks.
**Guess** marks anything not read directly from a file.

## TL;DR for the bullet system
- Armor is **per body subpart and per layer**. Item -> `Clothing` name -> `ArmorType` + `ArmorArchetypeId`
  (`Character/CharacterComponent.xml`) -> subparts + layer (`item/armor_archetype2body_subpart.xml`).
- Every armor piece has exactly three defense numbers, `DefenseStab` / `DefenseSlash` / `DefenseSmash`. Missiles have
  `AttackStab` / `AttackSlash` / `AttackSmash` on the **ammo** item. Hand-cannon balls are pure smash (250 / 200), and
  `battle_shot` is stab 50.
- The damage formula itself is native (WHGame.dll). The tables only hold its parameters (section 2). No Lua formula
  exists.
- Lua entry for damage: `soul:DealDamage(stamina, health, ...)`. The DLL binding names its args `stamina,health`. MODLOG
  already verified the 5-arg form in game. `g_gameRules:CreateHit` -> `game:ServerHit` does **not** damage souls (MODLOG).
- Hit reactions are animation rows in `action/hit_reaction.xml` keyed by `hit_type` (HitReactionType, **Bullet = 6**),
  direction and body part. Death rows have `hasRagdoll`.

## 1. Armor

### Body parts (`item/body_part.xml`, human rows, race_id 0)
| id | body_part_name | body_damage_coeff |
|---|---|---|
| 1 | head | **1.5** |
| 2 | torso | 1.0 |
| 3 / 4 | arm_left / arm_right | 0.7 |
| 5 / 6 | leg_left / leg_right | 0.7 |

Each part also has `blood_zone_id` (0 head .. 5 leg_right) and `injury_buff_id` (buffs `injured_head`,
`injured_torso`, `injured_left_arm`... in `rpg/buff.xml`). Animal rows exist too, for example horse_head 1.5,
dog_torso 1.2 and roe_deer_head 2.0.

### Body subparts (`item/body_subpart.xml`): the zones armor is mapped to
- head (1): `head_up`, `head_back`, `head_sides`, `head_face`, `head_neck`, `head_necklace`
- torso (2): `body_front_up`, `body_back_up`, `body_side`, `body_front_down`, `body_back_down`, plus non-armor
  `body_belt`, `body_pouch`, `body_scabbard`
- arms (3/4): `arm_{left|right}_{shoulder|upper|forearm}_{front|back}`, `hand_left`, `hand_right`, `hand_ring`
- legs (5/6): `leg_{left|right}_{upper|lower}_{front|back}`, `foot_{left|right}_boots`, `foot_{left|right}_plates`,
  `foot_spur`

**Front / back matters.** A cuirass may cover only `body_front_*` (archetype `BodyPlateCuirassFrontShort`), so a shot in
the back skips that plate. The bullet has to pick front / back from its direction against the target's facing.
`combat/combat_hit_origin.xml` uses these angle sectors: forward 280..80, backward 100..260, left 80..100,
right 260..280.

### Layers (`item/body_layer.xml`, `body_layer_type.xml`)
| layer id | name | type | layer_order (1 = closest to skin) |
|---|---|---|---|
| 8 | underwear | decoration | 1 |
| 1 | cloth_inner | cloth | 2 |
| 2 | chainmail_inner | chainmail | 3 |
| 3 | plate_inner | plate | 4 |
| 5 | cloth_outer | cloth | 5 |
| 6 | chainmail_outer | chainmail | 6 |
| 7 | plate_outer | plate | 7 |
| 4 / 9 | decoration_inner / decoration_outer | decoration | 8 / 9 |

A bullet passes through the layers from outer to inner, highest `layer_order` first.

### Item -> zones
- `item/item.xml` (+ `item__dlc.xml`), elements `<Armor>` (2848), `<Helmet>` (152), `<Hood>` (150). Attributes:
  `DefenseStab`, `DefenseSlash`, `DefenseSmash`, `MaxStatus`, `StrReq`, `Weight`, `Noise`, `Visibility`,
  `Conspicuousness`, `Charisma`, `Clothing`, `MaxQuality`, `Id` (guid), `Name`. Helmets also have `VisorTypeId`.
- `Clothing="GambesonShort01_m04"` -> a `<Clothing Name=...>` node in `Character/CharacterComponent.xml`. Its nearest
  ancestor `<Clothing>` carries `ArmorType="GambesonShort"` and sometimes `ArmorArchetypeId`; children inherit these.
- `item/armor_type.xml`: ArmorType -> `Surface` (`mat_fabric` / `mat_leather` / `mat_chainmail` / `mat_plate`,
  useful for spark / cloth effects) + `SurfacePriorityMultiplier` (likely which surface wins for hit FX, **guess**).
- `item/armor_archetype.xml` (84 rows) + `armor_archetype2body_subpart.xml`: archetype -> (`body_layer_id`,
  `body_subpart_id`, `is_exclusive`).

Selected archetypes:
| archetype | layer | covers |
|---|---|---|
| 1 BodyPlateCuirassFrontShort | plate_outer | body_front_up/down |
| 3 BodyPlateCuirassDoubleShort | plate_outer | body front+back up/down |
| 11 BodyPlateCuirassFullLong | plate_outer | torso all + upper legs (Cuirass, Brigandine) |
| 16 BodyChainShirt_longSleeves | chainmail_outer | torso + full arms (MailShort) |
| 19 BodyChainShirtLong_longSleeves | chainmail_outer | torso + arms + upper legs (MailLong) |
| 24 BodyClothShirt_longSleeves | cloth_outer | torso + arms (Gambesons, Tunics) |
| 32 ArmsPlateCombinedLong | plate_outer | upper back + forearms (ArmPlate, ArmBrigandine) |
| 41 HeadPlateHelmOpen | plate_outer | head_up/back/sides (KettleHat, SkullCap) |
| 42 HeadPlateHelmOpen_collar | plate_outer | + head_neck (Bascinets, incl. BascinetVisor) |
| 43 / 44 HeadPlateHelmFull(_collar) | plate_outer | + head_face |
| 89 HeadChainCap_withCollar | chainmail_outer | head (no face) + neck + shoulders (CoifMail) |
| 45 HandsGloves | plate_outer | hand_left/right (Gauntlets and leather Gloves) |
| 56 LegsPlateProtsLongFull | plate_inner | all leg subparts (LegsBrigandine) |
| 58 LegsPlateProtsLongFull_withFeet | plate_inner + chainmail_inner | legs + feet (LegsPlate) |
| 47 LegsClothTrousersLong | cloth_inner | legs (LegsPadded, Hose) |

`BascinetVisor` uses archetype 42, which has no `head_face`, so a visor does not cover the face zone in this table.
That may be intentional or a data quirk; check live (**guess**).

Strongest item per ArmorType (stab / slash / smash, MaxStatus, weight kg):
| ArmorType | item | stab | slash | smash | MaxStatus | kg |
|---|---|---|---|---|---|---|
| Cuirass | Cuirass03_m01_A5 | 156 | 114 | 40 | 32 | 26 |
| Brigandine | Brigandine10_m10_A5 | 153 | 113 | 39 | 21 | 16 |
| MailLong | MailLong01_m03_C5 | 38 | 79 | 64 | 34 | 26 |
| MailShort | MailShort01_m03_C4 | 34 | 72 | 61 | 40 | 22 |
| GambesonShort | GambesonShort03_m10_A4 | 16 | 53 | 41 | 25 | 14 |
| GambesonLong | GambesonLong04_m07_A5 | 16 | 50 | 40 | 30 | 16 |
| Caftan | Caftan05_m07_D3 | 16 | 55 | 41 | 25 | 15.2 |
| BascinetVisor (Helmet) | BascinetVisor04_m02_A5 | 156 | 114 | 40 | 34 | 15 |
| KettleHat (Helmet) | KettleHat07_m04_A5 | 138 | 104 | 37 | 35 | 7 |
| CoifMail | CoifMail02_mPrague_B3 | 33 | 89 | 69 | 36 | 7.7 |
| ArmPlate | ArmPlate04_m04_A5 | 156 | 114 | 40 | 25 | 6 |
| Gauntlets | Gauntlets05_m01_A5 | 156 | 114 | 40 | 25 | 3.5 |
| LegsPlate | LegsPlate03_m03_A5 | 156 | 114 | 40 | 19 | 14 |
| LegsPadded | LegsPadded01_m07_C3 | 37 | 50 | 40 | 25 | 5.7 |
| TunicShort | TunicShort03_m04_D | 2 | 2 | 3 | 11 | 1 |

The pattern: plate is very high vs stab (~150) and low vs smash (~40). Mail is mid stab (~35), high slash (~75) and
mid-high smash (~62). Padding is low stab (~16) and mid slash / smash (~40-50). This is why KCD2's balls are smash.

Condition: in-game item condition is the item's runtime **health** (0..1); `MaxStatus` is a separate per-item number
whose exact meaning isn't confirmed (**guess**: dirt / blood capacity). Related params (`rpg/rpg_param.xml`):
`ItemHealthDamagedThreshold=0.4`, `ItemHealthAlmostBrokenThreshold=0.2`, `ArmorStatusToDefenseCoef=0.6` (worn armor
loses up to 60 % of its defense, **guess** on the exact curve), `DamageToArmorStatus=0.3`,
`DamageToArmorStatusHigherLayers=0.375`, `DamageToArmorStatusLowerLayers=0.0075`, `MissileWeaponDamageStatus=0.3`.

## 2. Damage math (params only; formula is native)
Neither Lua nor the tables contain the formula. WHGame.dll holds the code and reads these `rpg/rpg_param.xml` keys
(names confirmed in the DLL strings next to `MinDmgR`, `BlockingDmgRCoef`, `MaxStaminaDamage`, `GoodWeaponDefense`):

| param | value | probable role |
|---|---|---|
| `LethalDmgR` | 300 | damage-ratio threshold for a lethal hit (**guess**) |
| `MaxStaminaDmgR` / `MinStaminaDmgR` | 80 / -50 | ratio window where damage goes to stamina vs health (**guess**) |
| `MinStaminaDamage` | 15 | floor for stamina damage |
| `RangedWpnPwrToEnergy` | 1.2 | missile weapon `Power` -> projectile energy |
| `RangedWpnSpeedToAttack` | 0.021 | projectile speed -> attack bonus |
| `ArmorStatusToDefenseCoef` | 0.6 | armor condition -> defense |
| `WeaponStatusToAttackCoef` | 0.6 | weapon condition -> attack |
| `AverageArmorDefenseWeight` | 0.25 | mix of zone defense vs whole-outfit average (**guess**) |
| `CombatLevelToRelativeAttackIncrease` / `...HealthLossDecrease` | 0.5 / 0.5 | NPC combat level scaling |
| `GoodArmorDefense` / `GoodWeaponAttack` | 100 / 90 | reference values (UI / AI "badass" rating) |
| `CombatHitUnconsciousDepth` | 480 | knock-out depth from hits |
| `ClothesBloodSoakOnMaxBleeding` | 240 | seconds to fully bloody clothes at bleeding 1 |
| `MaxStabBuffApplyChance` / `MaxSlashBuffApplyChance` / `MaxSmashBuffApplyChance` | 999 | caps for per-type buff (bleeding / injury) chance |
| `TransferedBloodToVictimMin/Max` | 0.3 / 0.5 | blood decal on victim |

- Head multiplier: `body_part.body_damage_coeff` **1.5** for the head (0.7 for limbs). MODLOG notes that
  `DealDamage(..., bodyPart)` already applies it.
- Damage type: `rpg/rpg_damage_type.xml`: 1 stab, 2 slash, 4 smash, 5 stab&smash, 6 slash&smash.
  `combat/combat_attack_type.xml` maps melee attacks (stab -> 5, slash -> 6, smash -> 4). `cannot_kill="true"` on
  throw / kick / punch / hook / direct / backoff.
- Missile side: the ammo's `AttackStab/Slash/Smash` + `PowerMod`, and the launcher's `Power` (bows ~100-200, guns
  620-655). The per-hit record in the DLL is `wh::combatmodule::CombatHitSubInfo` with `AttackCoef` and
  `BodySubpartId`, so armor is resolved per subpart.
- Shape of the formula (**guess**, from KCD1 knowledge plus these param names): attack per damage type minus the
  matching defense of the layers on the hit subpart (scaled by condition), a ratio checked against `LethalDmgR`, and the
  stamina / health split via the DmgR window. Confirm by firing a hand cannon at a known-armor NPC and reading health.
- Bleeding: Lua binds `soul:HealBleeding(heal, bodyPartId)`, `soul:AddInjury(injury, bodyPartName)`; buffs
  `bleeding`, `injured_<part>`; DLL has `IsBleeding`, `IsBodyPartBleeding`, `GetBleeding`, `CauseAlwaysHeavyBleeding`.

## 3. Hand cannon
| item (`item/item.xml`) | Class | Power | other |
|---|---|---|---|
| `handgonneNormal01` | 13 | 620 | Weight 6, Model hand_cannon_a.cdf, LimbResistance 50, Defense 10 |
| `handgunFancy01` | 13 | 635 | Weight 11, hand_cannon_b.cdf |
| `hookgunNormal01` | 13 | 620 | Weight 6, hook_gun_a.cdf |
| `hookgunFancy01` | 13 | 640 | Weight 12, hook_gun_b.cdf |
| `hookgunPushek` (item__dlc.xml) | 13 | 655 | Weight 9 |

| ammo | Type (ammo_class) | AttackStab | AttackSmash | PowerMod | RpgWeight |
|---|---|---|---|---|---|
| `shot_ball` | 3 Ball | 0 | **250** | 1 | 50 |
| `shot_scatter` | 4 Shotgun | 0 | **200** | 1 | 50 |
| `battle_shot` | 3 Ball | **50** | 0 | 1 | 30 |

For comparison, arrows and bolts are stab 115-180 (for example `bolt_piercing` 180). Shot is smash, so it is scored
against the low (~40) smash defense of plate.

Projectile: `Scripts/Entities/Items/XML/Ammo/Arrow.xml`, `<ammo name="Arrow" class="Arrow">`. Physics particle: mass 0.3,
speed 60, radius 0.05, air_resistance 0, water_resistance 0.5, gravity 0,0,-9, pierceability 9, single_contact 1,
traceable 1. Params: `hitRecoil=Level2`, `hitRecoilArmorMode=Level1`, `lifetime=15`, `bulletType=2`, `ammo_category=bow`,
trail `WH_Particels.other.arrow_trail`. The DLL also knows the ammo keys `pierceabilityLevels` and `bounceableBullet`.
The only other ammo file is `Decoy.xml`.

How damage travels (**guess**, consistent with the tables): the fired particle is a generic `Arrow`; the DLL's
`C_MissileWeaponItem` / `C_MissileWeaponRuntimeData` keep the shooter + ammo item, and on impact the hit is scored with
the ammo's Attack* values and the launcher's Power. Damage numbers never live in the projectile XML.

## 4. Hit reactions, stagger, ragdoll
- `action/hit_reaction_type.xml`: 1 Ragdoll, 2 AnimatedWeak (`weak_hit`), 3 AnimatedMinor (`minor_hit`),
  4 AnimatedMajor (`major_hit`), 5 AnimatedDeath (`death`, is_death_reaction).
- `action/hit_reaction.xml` (660 rows). Columns: `hit_type`, `hitreaction_type_id`, `hit_direction`, `body_part_id`,
  `damage_type`, `hasRagdoll`, `isfullbody`, `mn_fragment_id` (`HitDeath`, `HitDeathTorso`), `mn_tags` (e.g.
  `so_forward+death`), `animation_duration`, `actor_class_hash`, `mounted`, `stance_tag`, `speed_tag`.
  `hit_type` = `HitReactionType` from `ai/ai_enums.xml`: 1 Melee, 2 MeleeStealth, 3 Collision, 5 Fall, **6 Bullet**,
  8 Decoy, 9 Stone, 10 Shock. Bullet rows: 8, all AnimatedDeath with `hasRagdoll=true`.
- Ragdoll on death: chosen per row by `hasRagdoll` (154 of the generic death rows true). The DLL has
  `C_ProceduralClipPlayRagdollHitReaction` ("PlayRagdollHitReaction" procedural clip), `C_RagdollManager`, `RagDollize`,
  `QueueRagdollCreation`, `EnableBlendToRagdoll` / `DisableBlendRagdoll`.
- Strength scale the AI reads (`HitReactionStrength`, ai_enums.xml): 0 Zero, 1 Healing, 2 Tickle, 3 Unpleasant,
  4 Exhausting, 5 MinorInjury, 6 MajorInjury, 7 Fatal. AI scripts test `$hitReaction.hitType`,
  `$hitReaction.hitStrength` and `$hitReaction.attacker` (crime, fleeing, battle logic). Our hits must arrive with
  these set to trigger crime / flee reactions.
- Physical shove: cvars `wh_HitReaction_Enabled`, `wh_HitReaction_PhysicalHitCoef` ("Coefficient for physical hit
  impulses"), `wh_HitReaction_CollisionsEnabled`, `wh_HitReaction_EnvironmentCollisionScale`, `wh_HitReaction_Debug`.
- `hitRecoil` / `hitRecoilArmorMode` take `level1..level3` (DLL strings); Arrow uses Level2, or Level1 when armor stops it
  (**guess** on "ArmorMode" meaning).
- `knockdownChance` is in the DLL next to CryAI archetype properties (`sightrange`, `attackrange`, `damageRadius`), not
  in any table or script. It is likely a legacy AI property, not a per-hit chance (**guess**). `hitStrength` sits next
  to combat-condition names (`bodyPartId`, `stealthKill`, `blockedAttack`): it is the HitReactionStrength above.

## 5. Lua side
| call | where | notes |
|---|---|---|
| `soul:DealDamage(stamina, health, attackerId, suppressHitreaction, bodyPart)` | DLL bind (args named `stamina,health`) | MODLOG: verified in game, headshot kills, head x1.5 applied. `Scripts/Debug/Quick.lua:33` names them `(hitToHealth, hitToStamina, __null, true)`, which contradicts the DLL; trust the DLL + MODLOG |
| `soul:AddInjury(injury, bodyPartName)` | DLL bind | wound / limp per part |
| `soul:HealBleeding(heal, bodyPartId)` | DLL bind | |
| `g_gameRules:CreateHit(targetId, shooterId, weaponId, dmg, radius, material, partId, type, pos, dir, normal)` | `Scripts/GameRules/SinglePlayer.lua:184` | wraps `self.game:ServerHit(...)` with `GetHitMaterialId` / `GetHitTypeId("normal")`; per MODLOG it doesn't damage souls |
| `SinglePlayer.Client:OnHit(hit)` -> `trg.Client.OnHit(trg, hit)` | SinglePlayer.lua:291 | hit table fields used in scripts: `pos`, `dir`, `normal`, `damage`, `type`, `shooterId`, `attackerId`, `target`, `ipart`, `partId`, `impact_force_mul`, `impact_force_mul_final`, `explosion`, `projectileClass`, `target_material` |
| `actor:RequestKnockOut(id)`, `RequestStealthKill(id)`, `RequestMercyKill(id)` | Lua call sites | animation takedowns, not hit reactions |
| `entity:AddImpulse(...)` | DLL bind (MODLOG) | our own body shove |

Open questions for live tests:
1. Does `DealDamage` with `suppressHitreaction=false` play the Bullet (`hit_type` 6) reaction rows, or melee ones?
2. Does it set `hitStrength` / attacker for the AI, so crime and flee logic fire?
3. Is there a Lua path to the armor-aware missile scoring (the code path used when an Arrow particle hits), or do we
   compute armor ourselves from the tables above and pass the result to `DealDamage`?

## Runtime armor lookup (what an NPC wears, from Lua)
There is no "equipped items" call (`actor:GetInitialClothingPreset()` doesn't exist either; probed live). **The
inventory is the outfit** (verified live on an armored guard):

| call | returns |
|---|---|
| `npc.inventory:GetInventoryTable()` | `{ itemId, ... }` |
| `ItemManager.GetItem(itemId)` | `{ id, class (item guid), amount, entity, health (condition 0..1) }` |
| `ItemManager.GetItemName(classGuid)` | name (logs) |

**Generated table** `src/Scripts/Deagle/guns/armor.lua` (from `tools/gen_armor.py <copy of Tables.pak>`, ~0.6 MB):
- `Deagle.armor[itemGuid]` = `{ name, type, layer = plate|mail|padding|leather|cloth, order, stab, slash, smash,
  maxStatus, surface, zones }`.
- 2459 items: 488 plate, 55 mail, 229 padding, 254 leather, 1433 cloth. Skipped: no defense, horse gear, spurs,
  necklaces.
- Guids are lowercase. No item in the data is front-only or back-only, so there is no side split.

Use per hit:
1. Get the hit `zone` from `Deagle.hitBones` (see "Hit zones from bones").
2. `for _, id in pairs(npc.inventory:GetInventoryTable()) do local it = ItemManager.GetItem(id); local a = it and
   Deagle.armor[tostring(it.class):lower()]` and keep the pieces whose `a.zones` contains `zone`, with `it.health`.
   (Weapons and food simply aren't in the table.)
3. Sort by `order` descending. **KCD2's layer order** (`item/body_layer.xml` `layer_order`, outside first):
   decoration_outer 9 > decoration_inner 8 > plate_outer 7 > chainmail_outer 6 > cloth_outer 5 > plate_inner 4 >
   chainmail_inner 3 > cloth_inner 2 > underwear 1 > body 0.
4. Spend the round's energy through each piece. Effective defense = defense × (1 − 0.6 × (1 − health)), from
   KCD2's `ArmorStatusToDefenseCoef` 0.6 (**guess** on the curve). Stop: spark + dent (plate / mail `surface`).
   Pass: next layer, then flesh.

## Hit zones from bones (`Deagle.hitBones`)
A ray on a living NPC hits only its single physics capsule (`mat_flesh`, no part id; probed live), so the zone comes
from our own capsules on the skeleton.

`src/Scripts/Deagle/guns/hitbones.lua` is generated by `tools/gen_hitbones.py <copy of male.chr>`.
- Source: `Objects/characters/humans/male/skeleton/male.chr` (IPL_Characters-part3.pak):
  - CompiledBones (243 bones, names as used at runtime: `Head`, `Neck`, `Spine` .. `Spine4`, `Hips`,
    `LeftShoulder` / `LeftArm` / `LeftForeArm` / `LeftHand` / `LeftHandSlot`, `LeftUpLeg` / `LeftLeg` / `LeftFoot` /
    `LeftToeBase`, Right*)
  - **CompiledPhysicalProxies**: the ragdoll hulls on 19 bones.
- Each capsule goes from bone `a` to bone `b`, between fractions `t0`..`t1` of that segment. Radius is fitted to the
  bone's proxy hull in bind pose: `r` = median hull-vertex distance (snug), `rMax` = largest (encloses the hull).
- **Measured** from proxies: head, chest, belly, pelvis (runs LeftUpLeg ↔ RightUpLeg), shoulders, upper arms,
  forearms, hands, thighs, shins, feet.
- **Guess:** neck (no proxy; r 0.06). Belly collapses to a sphere (center at t 0.58 of Spine → Spine3, r 0.126).
- The cloth proxies in `male.cdf` (`CA_PROX` attachments, e.g. `Ribcage_PCloth` r 0.14, `hips_PCloth` r 0.11, upper
  legs 0.1, upper arms 0.068) agree roughly. They weren't used.
- Zones: `zone` is fine-grained (head / neck / torso / arm_* / hand_* / leg_* / foot_*). `armor` is the 6 armor
  zones, mapped the way KCD2's tables do (`body_subpart` → `body_part`): `head_neck` → head, `hand_left/right` →
  arm_left/right, `foot_left/right_*` → leg_left/right. Damage multiplier (`body_part.body_damage_coeff`): head 1.5,
  torso 1.0, limbs 0.7.

Use per round:
1. Take the round's segment from the last to the current position, in world space.
2. Only for an NPC whose physics capsule was hit (or which is within ~1 m of the segment): for each capsule,
   `A = npc:GetBonePos(a)`, `B = npc:GetBonePos(b)`, `P0 = A + t0 (B − A)`, `P1 = A + t1 (B − A)`, and run a
   segment-vs-capsule test (closest points between the ray and P0P1 ≤ radius → entry t).
3. Take the **earliest entry** along the ray. On a tie (overlaps at joints), prefer the smaller radius. That puts the
   arm in front of the chest, and the hand in front of the forearm.
4. Use `r` for limbs and the head (snug). For the torso consider `rMax`: the torso is wider than deep, and a round
   cylinder with the median radius misses its sides.
5. Feed `armor` to the armor lookup ("Runtime armor lookup") and `zone` to hit reactions and gore.
Cost: 19 capsules × 2 GetBonePos per hit NPC. Only do it for NPCs near the ray.
