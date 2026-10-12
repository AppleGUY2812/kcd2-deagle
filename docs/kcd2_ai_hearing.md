# KCD2 AI hearing: making NPCs react to our gunshots

Sources: `Data/Scripts.pak` (Lua + AI behaviour-tree XML), `Data/Tables.pak` (`Libs/Tables/rpg/rpg_sound.xml`,
`Libs/Tables/ai/*`), and Lua-bind strings in `WHGame.dll` (1.5.6). **Guess** marks inference.

## TL;DR
KCD2 already has a gunshot sound for the AI. One Lua call per shot makes NPCs hear it:
```lua
XGenAIModule.ProduceSound(enum_sound.gun_fired, pos, multiplier)        -- at a world position (shot origin)
XGenAIModule.ProduceSoundWUID(enum_sound.gun_fired, player.this.id, multiplier)  -- at an entity (the shooter)
```
- `enum_sound.gun_fired` = 14 and `enum_sound.bullet_hit` = 16 (ids from `rpg_sound.xml`).
- **Guess:** `enum_sound` holds these names. KCD2's scripts use `enum_sound.door`. If `gun_fired` is nil, pass 14.
- No native plugin work is needed.

## 1. How KCD2 tells the AI about a sound
**Lua binds** (`XGenAIModule`; arg names from the DLL bind strings):

| bind | args | used by KCD2 |
|---|---|---|
| `XGenAIModule.ProduceSound` | `int, pos, mult` (sound kind, world pos, intensity multiplier) | AI player brain (`<ProduceSound Sound= Position= Multiplier=>` node) |
| `XGenAIModule.ProduceSoundWUID` | `int, wuid, mult` | `Scripts/Systems/Crime.lua:135` `Crime.ProduceAiSoundOnDudePosition(soundKind, multiplier)` → `ProduceSoundWUID(soundKind, player.this.id, multiplier)`; doors (`AnimDoor.lua:1004`, `enum_sound.door, crimeDoorSoundMod`); stashes |
| `XGenAIModule.SpawnPerceptibleVolume` | `pos, radius, height, visibility, conspicuousness, label, timer, worldTime` | a **visible** stimulus volume (**guess**: a muzzle flash / smoke cloud NPCs can see) |
| `XGenAIModule.SpawnPerceptibleVolumeOnWUID` | `wuid, radius, height, visibility, conspicuousness, label, timer, worldTime` | same, attached to an entity |
| `XGenAIModule.SendMessageToEntity` / `SendMessageToEntityData` | `wuid, type, value` | direct brain messages (`Scripts/Debug/Quick.lua` `q.sm`) |

- KCD2's AI uses its own XGenAI hearing system (`C_HearingSystem`), not CryAI. The CryEngine `AI.*` binds
  (`AI.CreateStimulusEvent(ownerId, targetID, stimulusName, pData)`, `AI.CreateStimulusEventInRange`,
  `AI.SetSoundPerceptionDescriptor`) exist, but KCD2's scripts never use them for hearing. Skip them.
- **Sound kinds** (`rpg/rpg_sound.xml`; one-shot intensity unless noted):

| id | name | intensity | decrement / s | notes |
|---|---|---|---|---|
| 14 | **gun_fired** | **2000** | 0.1 | the loudest human sound in the game |
| 16 | **bullet_hit** | **500** | 0.1 | impact noise |
| 13 | bow_fired | 160 | 0.18 | |
| 15 | arrow_hit | 260 | 0.1 | |
| 1 | combat | 225 | 0.15 | melee fighting |
| 6 | combatScript | 65.4 | 0.08 | what the player brain emits on arrow hits (`handleArrowHitData.xml`) |
| 5 | whistle | 760 | 0.1 | |
| 22 | dog_bark | 1500 | 0.4 | |
| 19 | alarm | 123 | 0.5 | |
| 7 | door | 32 | 0.6 | |

All kinds have `notification_threshold` 1 and `notification_cooldown` 1–12 s.

- **Hearing range.** The exact attenuation lives in native code and wasn't traced (rpg param
  `MaxHearingSoundAttenuationCoef` 0.02 is involved). **Guess:** intensity × mult falls off with the square of
  distance against threshold 1, so range ≈ √(intensity × mult): gun_fired ≈ 45 m, bullet_hit ≈ 22 m, door ≈ 6 m,
  dog_bark ≈ 39 m. Under that guess:
  - 9 mm at ~80 m: `mult ≈ (80 / 45)² ≈ 3.2`
  - Deagle at ~120 m: `mult ≈ 7`
  - **Calibrate live:** stand an NPC at known distances and raise `mult` until he reacts.
- The game's own hand cannons almost certainly raise `gun_fired` natively when they fire (**guess**: it's an
  `rpg_sound` id that no script sends). We just send the same kind.

## 2. Gunshot → crime?
- **Hearing alone is not a crime.** A heard sound makes NPCs `lookAround` (stimulus kind `crime_stimulusKind.sound`),
  turn, approach, search or bark. With enough repeats they get "fed up" and ignore it (`crime_fedUpWithSound*`).
- The **crime** comes from what they then see: an assault on someone (our hit events with an attacker and flag 0x20,
  or `soul:DealDamage` with an attacker), a corpse (corpse-reaction trees), or trespass. These go through the crime
  trees (`AI/crime/*`, `C_GetCrimeLevelByStimulusKind`, `crime_attackKind` / `assaultVictimKind`) and reputation.
- To make the shot itself alarming without a hit: also emit `enum_sound.alarm` or a `dramatic_bark` (600) from a
  witness. **Guess**: that's how KCD2 spreads alarm (`AI/crime/spreadDramaticBark.xml`).

## 3. What NPCs do on a gunshot (behaviour trees in Scripts.pak)
- `hearingInfo` mailbox → `AI/npc/basic/switch/handleHearing.xml` → `interrupt_lookAround.xml` (the big one). It reads
  `$lookAroundData.soundType` and `.stimulusKind`.
- **gun_fired is special-cased:** `soundType == sound.gun_fired & distance < crime_gunFiredSound_highNervousnessDistance
  (5 m)` gives the high-nervousness reaction (startle / flee-like). Farther away: normal lookAround (turn to the
  sound, walk to a randomised point near it, search 2 iterations).
- Script params (`ai/ScriptParams.xml`):

| param | value |
|---|---|
| `crime_lookAround_anchor_maxDistance` | 40 m (they won't walk farther than this to investigate) |
| `crime_lookAround_soundPosRandomizer_exteriorDistanceMultiplier` | 0.3 (sound position blurred by 30 % of distance) |
| `crime_combatSoundImmediateReactionMaxDistance` | 5 |
| `crime_combatSound_highNervousnessDistance` | 5 |
| `crime_fedUpWithSoundPosCount` | 5 (after 5 sounds they stop caring) |
| `crime_fedUpWithSoundReactionCooldown` | 20 s |

- Animals: `AI/animal/basic/switch/animal_handleHearing.xml` (startle, flee).
- Message types: `hearingInfo`, `crime_produceSound`, `soundTrigger`, `lookAround_updateSoundPosition`,
  `perceptionInfo*` (`ai/mailbox.xml`).

## 4. Native
Not needed. For reference: `ProduceSound` bind registration at RVA 0x19C3C5C (thunk 0x2AB8E58), `ProduceSoundWUID`
at 0x19C3D04. Use Lua.

## Suggested use in Deagle
```lua
-- per shot (main thread, after spawning the round):
local kind = (enum_sound and enum_sound.gun_fired) or 14
XGenAIModule.ProduceSound(kind, muzzlePos, gun.noiseMult or 3.2)       -- 9 mm ~80 m (calibrate)
-- per impact on the world / an NPC (optional, quieter):
XGenAIModule.ProduceSound((enum_sound and enum_sound.bullet_hit) or 16, hitPos, 1)
```
Suppressed guns could use a lower `mult` or `combatScript`. First live check: fire 30 m from a guard and watch
for lookAround. Then step the distance up to find the real range, and fit `mult` per calibre.
