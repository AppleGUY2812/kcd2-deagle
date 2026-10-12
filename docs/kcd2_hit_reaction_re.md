# KCD2 hit reactions: static RE of WHGame.dll

Binary: `Bin/Win64MasterMasterSteamPGO/WHGame.dll`, SHA-256 prefix `BDF8F9E4A11257A7` (copy in scratchpad, identical to
the game's). Image base 0x180000000; **all addresses below are RVAs**. Done with capstone + pefile + RTTI; no game
run. **Guess** marks inference I could not close statically.

## TL;DR: why `DealDamage(0, 5, player.id, false, part)` showed nothing
1. **Lua `DealDamage` always tags the hit as `HitReactionType.Fall` (5).** The damage record's hit-type byte defaults
   to 5, and the 4th arg only changes it to 1 (Melee) while also clearing every reaction flag. The reaction selector
   `SelectReactionType` (0xB24964) special-cases type 5 and forces the fall reaction type. That is the type used by fall
   damage (0x285CF2C builds the same event with type 5), and there are almost no `hit_reaction.xml` rows for it.
   Result: no flinch.
2. **The 5th arg is a body-part *name* string**, not a number. The bind reads it with `GetParam<const char*>`
   (0x41DC20, the same helper as our LogAlways hook) and looks it up by name; anything else gives no body part. That
   is why head / leg made no difference: no zone multiplier was applied. Use `"head"`, `"torso"`, `"arm_left"`,
   `"arm_right"`, `"leg_left"`, `"leg_right"` (`body_part_name` in `item/body_part.xml`).
3. Reactions are played by `HitReactionComponent::ProcessHit` (0xB24600) from a **hit-reaction event struct**
   (ctor 0xB2454C). Our bullet should build that event with `hitType = 6` (Bullet) or `1` (Melee), its strength, and
   the victim / attacker ids, then hand it to the component. Details in sections 2 and 5.

## 1. DealDamage, end to end
**Lua bind registration** (0x1675E3D..0x1675E57): `RegisterFunction("DealDamage", "stamina,health", method=0xEE018C)`
through the generic two-float thunk 0xEE0138.
- Thunk: `GetParam` of two floats; arg1 goes to xmm2 and arg2 to xmm3 of the method. So **arg1 = stamina, arg2 =
  health** (matches the bind's arg string and the live test where (0, 5) took 5 hp).

**Method 0xEE018C** `(this, IFunctionHandler* pH, float stamina, float health)`:
- `soul = GetThis(pH)` (0x41E964).
- Arg 3 (if count > 2): attacker, read by 0x712164 (entity/soul handle). Default is a global (no attacker).
- Arg 4 (if count > 3): **bool**, read by 0x4D2098, into `bSuppress`.
- Arg 5 (if count > 4): **string**, read by 0x41DC20, then a name lookup (0x827CB4) into the body-part record.
  If the id is -1, no body part.
- Calls `soul->m_combatSoul.vfn[0xD8/8 = 27](stamina, health, attackerSoul, bSuppress, bodyPartRec*)`.
  `m_combatSoul` is an embedded **`C_CombatSoul`** at **C_Soul + 0xF0** (vtable RVA 0x3F45670, set in its ctor
  0x70F6CC called from the C_Soul ctor 0x3F1C7C).

**`C_CombatSoul::DealDamage` (vfn 27, 0xEE031C)** builds a 0x60-byte damage record `S_Damage` on the stack and calls
vfn 28:

| off | type | meaning |
|---|---|---|
| 0x00 | float | stamina damage |
| 0x04 | float | health damage |
| 0x08 | ptr | attacker (soul/entity) |
| 0x10 | u8 | bSuppress (Lua arg 4) |
| 0x18 | 0x40 bytes | body-part / hit-zone sub-struct (init 0xEE0404: id -1, two names, 16-byte vector, ...) |
| 0x58 | u8 | **hit type, default 5** (= HitReactionType.Fall) |
| 0x5C | u32 | default 5, copied into the event's +0x54 slot (**guess**: attack / damage kind) |

**`C_CombatSoul::ApplyDamage` (vfn 28, 0x7256D4)** `(this, const S_Damage*)`:
- Returns early if the victim's health at `[this+0x78]+0x708` is already <= 0.
- Builds the combat damage info, then calls `this->vfn[0x168/8]` (the actual armor/health math, not followed).
- `hitType = bSuppress ? 1 : S_Damage.0x58`.
- Builds a hit-reaction event (ctor 0xB2454C), fills attacker / victim ids, stamina and health, `isFatal`
  (0x726648) and `hitStrength` (0x7261DC). If bSuppress, it clears the event's flag mask (+0x78 = 0). It then posts
  the event: `GetGame()(0x9155C8) -> [+0x1A0] -> vfn[0x528/8 = 165](&event)`.

**So the 4th arg is "suppress" in effect:** true sets hitType Melee (1) with flags 0, i.e. nothing; false sets hitType
Fall (5) with flags 0x3F, i.e. a fall reaction that doesn't exist for that hit.

### hitStrength from damage (0x7261DC; exact)
Health and stamina deltas come from 0x7262EC (negative = loss). Thresholds read from constants: 10.0 at 0x409E6E0,
50.0 at 0x409A414.

| case | HitReactionStrength |
|---|---|
| no change | 0 Zero |
| any gain | 1 Healing |
| stamina only, loss < 10 | 2 Tickle |
| stamina only, loss < 50 | 3 Unpleasant |
| stamina only, loss >= 50 | 4 Exhausting |
| health loss, fatal (0x726648) | 7 Fatal |
| health loss < 50 | 5 MinorInjury |
| health loss >= 50 | 6 MajorInjury |

There is no minimum: any health loss is at least MinorInjury.

## 2. Picking and playing the reaction
**Event struct** `S_HitReactionEvent` (ctor 0xB2454C; ~0x9C bytes). Fields are confirmed where a writer or reader was
seen, otherwise marked:

| off | type | meaning (source) |
|---|---|---|
| 0x00 | u32 | attacker EntityId (DealDamage; ProcessHit `[rbx]`) |
| 0x08 | ptr | default global (**guess**: name / tag) |
| 0x10 | u32 | 0x15 (**guess**: event kind) |
| 0x14 | i32 | -1 |
| 0x18 | u32 | **hitType**: HitReactionType 1 Melee, 2 MeleeStealth, 3 Collision, 5 Fall, 6 Bullet, 8 Decoy, 9 Stone, 10 Shock |
| 0x24 | u8 | 2 |
| 0x28 | u32 | victim EntityId (DealDamage, collision path) |
| 0x2C / 0x30 | float | stamina / health damage (DealDamage) |
| 0x34 | u32 / word | body part (collision path writes a word; ProcessHit reads +0x34, **guess**: bodyPartId) |
| 0x38 / 0x3C | | collision-specific (**guess**) |
| 0x40 | u8 | isFatal / death (forces strength 7 in ProcessHit) |
| 0x41 | u8 | second death-ish flag (**guess**: unconscious) |
| 0x44 | u32 | **hitStrength** 0..7 |
| 0x50..0x5B | Vec3 | hit position (collision path) |
| 0x60 / 0x6C | float | 1.0 defaults |
| 0x68..0x73 | Vec3 | **guess**: hit direction / velocity (collision path writes here) |
| 0x76 | u8 | special-case flag in SelectReactionType |
| 0x78 | u32 | **flags**, default 0x3F. Bit 0x04: extra call 0xB24A0C. Bits 0x03 both set: fallback call when no action plays. Bit 0x20: AI / witness stimulus (strength, type, distance). 0 = silent |

**`HitReactionComponent::ProcessHit` (0xB24600)** `(component*, S_HitReactionEvent*)`. Its `this` is the actor's
`[actor+0x240]`; see `ActorOnEvent` 0x706190, event id 0x20, payload `*(evt+0x18)`.
1. `SelectReactionType` (0xB24964): type 8 → decoy, **type 5 → fall**, +0x76, +0x40/+0x41 → death, type 3 / 10 use
   strength, else `StrengthToReactionType(strength)` (0xB250F0). That maps 0 → none, 1..4 → light (weak), 5/6 → the
   same injury type, 7 → death. The reaction types match `hit_reaction_type.xml`
   (Weak / Minor / Major / Death); the exact id per global is **guess**.
2. 0xB25168 gate, 0xB25188 / 0xB251E4 query the `hit_reaction` table (`S_HitReactionQueryData`, filled by 0xB2547C,
   0xB25B68, 0xB259C0 from actor state: direction, stance, mounted...), then `CreateHitReactionAction` (0xE77DF4)
   starts actor action 28 `hitreaction` (`C_ActorAnimatedAction<S_HitreactionActionParams>`, base ctor 0xE78120).
3. Without a row it falls back (flags & 3 == 3) and still posts the AI stimulus (flags & 0x20).

Missiles (arrows / balls) enter the same component through the actor event path. The arrow hit handler itself wasn't
followed (**guess**: it builds the event with type 6 and the real strength).

## 3. Physical hit path
Not closed. Facts:
- cvar block from getter 0x4AADE0 (TLS); `wh_HitReaction_CollisionsEnabled` at +0x398 (checked in 0xB23BF0),
  `wh_HitReaction_PhysicalHitCoef` at +0x39C (registered in 0x15A7864), +0x3A0 next (read in 0xB23B18).
- 0xB23BF0 is the **physics-collision hit handler**: it turns body collisions into events with hitType 3 via 0xB24178
  and posts them like DealDamage does. That is how bumps make NPCs stagger.
- `C_ProceduralClipPlayRagdollHitReaction` is registered at 0x1AD51EE; its Execute was not traced.
- Usable now from Lua: `entity:AddImpulse(...)` (bind exists) for our own shove, plus `actor:RagDollize` (actor bind
  near `StandUp`, `Revive`, `PostPhysicalize`).

## 4. Other Lua binds near the soul / actor tables
Soul: `AddInjury(injury, bodyPartName)`, `HealBleeding(heal, bodyPartId)`, `DealDamage(stamina, health)`,
`SetState(state, value)`, `HasPerk`, `AddBuff`.
Actor: `RagDollize`, `StandUp`, `Revive`, `RequestKnockOut`, `RequestStealthKill`, `RequestMercyKill`, `SetHealth`,
`AddBlood`, `CanStealthKnockout`.
Game rules: `ServerHit`, `SendDamageIndicator`, `KillPlayer`.
**No `PlayHitReaction` / `Stagger` / `Knockdown` bind exists** (string search). `HitReaction` only appears as a UI
overlay name.

## 5. Callable functions + unique patterns (verified unique in .text)
| name | RVA | conv / args | pattern |
|---|---|---|---|
| Lua DealDamage method | 0xEE018C | (this, IFunctionHandler*, float stamina @xmm2, float health @xmm3) | `48 8B C4 48 89 58 08 55 56 57 41 54 41 55 41 56 41 57 48 8D 68 A1 48 81 EC 00 01 00 00 0F 29 70 B8 0F 29 78 A8` |
| C_CombatSoul::DealDamage (vfn 27) | 0xEE031C | (C_CombatSoul* = soul+0xF0, float stamina @xmm1, float health @xmm2, void* attacker @r9, bool suppress @[rsp+0x28], const BodyPart* @[rsp+0x30]) | `48 8B C4 53 56 57 48 81 EC B0 00 00 00 0F 29 70 D8 0F 29 78 C8 48 8B 05 ?? ?? ?? ??` |
| C_CombatSoul::ApplyDamage (vfn 28) | 0x7256D4 | (C_CombatSoul*, const S_Damage*) | `48 89 5C 24 18 55 56 57 48 8D 6C 24 E0 48 81 EC 20 01 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 45 10 48 8B DA` |
| ComputeHitStrength | 0x7261DC | (?, ?, ?) returns 0..7 | `4C 8B DC 49 89 5B 08 49 89 6B 10 49 89 73 18 57 48 83 EC 60 0F 29 74 24 50` |
| S_HitReactionEvent ctor | 0xB2454C | (S_HitReactionEvent* out) | `48 83 EC 18 45 33 C0 C6 41 24 02 83 CA FF 4C 89 04 24` |
| HitReactionComponent::ProcessHit | 0xB24600 | (component* = [actor+0x240], const S_HitReactionEvent*) | `48 89 5C 24 18 55 56 57 41 56 41 57 48 8D 6C 24 C9 48 81 EC B0 00 00 00 F6 42 78 04` |
| SelectReactionType | 0xB24964 | (component*, const S_HitReactionEvent*) | `48 89 5C 24 08 55 48 8B EC 48 83 EC 20 83 7A 18 08` |
| StrengthToReactionType | 0xB250F0 | (?, int strength @edx) | `48 83 EC 28 83 FA 05 74 2B 83 FA 06 74 26 83 FA 07` |
| GetGame | 0x9155C8 | () returns game; post event: `[ret+0x1A0]->vfn[0x528/8](&evt)` | `40 53 48 83 EC 20 8B 0D ?? ?? ?? ?? 65 48 8B 04 25 58 00 00 00 BA 18 00 00 00 48 8B 04 C8 8B 04 02 39 05 ?? ?? ?? ?? 7F 0D 48 8B 05 ?? ?? ?? ??` |
| Actor OnEvent | 0x706190 | (actor*, event*) where event id `[e+8]`, 0x20 = hit reaction, payload `**(e+0x18)` | `48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B FA 48 8B D9 E8 ?? ?? ?? ?? 8B 47 08 83 F8 02` |
| Fall hit reaction (example caller) | 0x285CF2C | (actor*, pos*): builds event type 5 and calls ProcessHit on `[actor+0x240]` | `40 53 48 81 EC F0 00 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 84 24 E0 00 00 00 48 8B D9 4C 8B D2` |

All are MSVC x64 (`__fastcall`, rcx/rdx/r8/r9, floats in xmm by position).

## 6. Suggested plan (native plugin)
Copy 0x285CF2C's pattern:
1. `S_HitReactionEvent e; ctor(&e)` (0xB2454C) into a 0xA0-byte zeroed buffer.
2. Set `e+0x00 = shooterId`, `e+0x28 = victimId`, `e+0x18 = 6` (Bullet; try 1 Melee if 6 finds no non-death rows:
   the xml only has 8 Bullet rows, all death), `e+0x44 = strength` (5 minor / 6 major, or our own scaling),
   `e+0x30 = health damage`, `e+0x50 = hit pos`, `e+0x78 = 0x3F`.
3. `ProcessHit(*(void**)(actor + 0x240), &e)` (0xB24600). We need the native actor pointer: **guess**, it's
   the object 0x706190 runs on, i.e. the game actor of `GetEntity(id)`; finding the entity → actor getter is next.
4. Keep damage via Lua `DealDamage(stamina, health, attackerId, true, "torso")` (suppress its fall event).

Cheap Lua test first: `DealDamage(0, 5, player.id, false, "torso")` should now apply the torso zone. Also try
`DealDamage(0, 60, player.id, false, "torso")` to see whether a MajorInjury-strength Fall-type hit plays anything
(expected: nothing).
