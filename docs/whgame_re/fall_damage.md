# How KCD2 computes the player's fall damage (static RE, WHGame 1.5.6)

Why it matters: `flight.lua` lands Henry slowly (sink at up to 6 m/s to 0.25 m above the ground, then gravity comes back) and
the user still takes fall damage (report 2026-10-20). Everything here was read statically: Ghidra daemon on the ProLiant plus a local capstone
scan of a copy of `E:\SteamLibrary\...\Bin\Win64MasterMasterSteamPGO\WHGame.dll` (89,180,672 bytes, mtime 2026-10-09,
md5 170a55fe1ef804b4a9ac6fbf9f6843e5). The copy disassembles the same as the Ghidra project at every address below (string refs at 0x181593f9f,
0x1815a7c46 and others match). Addresses are VAs (image base 0x180000000; RVA = VA - 0x180000000). Nothing was run in game.

## Verdict
- **The damage is computed from HEIGHT, not speed.** Height = `start Z - Z at landing`. The start Z is **the last Z
  at which Henry was on the ground before he left it**, so for a flight that is the take-off point. It stays frozen for the whole
  time in the air. Landing speed has no part in the amount.
- Speed only decides **whether a fall is armed**. The moment Henry's vertical velocity goes below **-3.5 m/s**
  (`pl_movement.ground_velocityToFall`) after more than **0.5 s** in the air (`pl_movement.ground_timeInAirToFall`),
  the game starts a fall action carrying that frozen take-off Z. The landing sink runs at up to 6 m/s, and a dive does too,
  so either one arms it.
- When that fall touches down, `take-off Z - landing Z` is passed to the RPG damage. Health damage starts above **1.5 m**
  (`HarmlessFallHeight`, rpg_param.xml) and a leg injury above **2.2 m** (`InjuringFallHeight`), both after scaling by the
  `FallDamageMultiplier` derived stat (agility). **So the damage comes after any landing more than ~1.5 m below the take-off point,
  however slow.** A landing at or above the take-off height does no damage.
- Hypotheses: **(a) confirmed**, in this form: the start height is the take-off ground Z and nothing resets it while
  airborne. (It is reset only by ground contact or by the FallDisabled context, section 3.) **(b) killed**: the
  `pl_fallDamage_*Speed*` cvars have no reader. 6 m/s only matters because it is faster than 3.5 m/s, which arms the fall.
  **(c) killed as stated**: the 0.25 m drop after gravity comes back reaches about 2.2 m/s, which is under 3.5, so it
  starts no new fall. It is simply the touchdown that resolves the fall armed earlier against the take-off Z.
- **Cheapest fix (Lua only):** while flying and until about 1 s after touchdown, give Henry's soul the game's own buff
  **`fall_damage`** (`buff_id 4bc0b081-a57e-4e6e-8297-6d9db58b39b2`, `buff_params="fdm=0"`, buff.xml line 164). It sets
  FallDamageMultiplier to 0, so the scaled height is 0 and there is no health or leg damage on any of the three landing paths.
  Details and caveats are in section 5.

## 1. Where the damage is applied: C_RPGMovement::OnFallDamage
- `wh::rpgmodule::C_RPGMovement::vftable` is at 0x183d18f48. Slot **+0xd0** (0x183d19018) holds **FUN_180725220**, the
  one place every fall-damage path ends:
  - `FUN_1807252e8` wraps the incoming `S_FallDamageData` (entityId +0, **height float +8**, +0xC int, +0x10 flags)
    in a `wh::rpgmodule::C_FallDamageCause` (vftable 0x183a4b2f8).
  - Unless bit 4 of `soul+0xcf0` is set, it reads derived stat **0xCB = DerivStat_EnableFallDamage**
    (`FUN_18046e430(soul,0xcb)`). If that is `<= 0` it returns with no damage. NPCs get efd only from the buffs
    `combat_fall_damage_enable` and `unconscious_fall_damage_enable` (`efd+1`). The player presumably has bit 4 set
    (not verified).
  - Then it calls **FUN_1807243b0(soul, height)**:
    ```
    if (0 < h) {
      fatal = rpg+0x37c (FatalFallHeight)
      h' = stat(0x4E = DerivStat_FallDamageMultiplier) * (1.0/fatal) * h
      if (rpg+0x374 (HarmlessFallHeight)/fatal < h')  -> health damage, lerp(h', harmless/fatal, 1)   (FUN_180c66334, type 9)
      if (rpg+0x378 (InjuringFallHeight)/fatal < h')  -> injury, lerp(h', injuring/fatal, 1), +0x44 = 7 (FUN_180c66194)
      leg_left / leg_right effects (FUN_180724158)
    }
    ```
    rpg_param offsets come from the registration in FUN_180ce3f40: `HarmlessFallHeight` +0x374 (0x180ce6f52),
    `InjuringFallHeight` +0x378 (0x180ce6f8a), `FatalFallHeight` +0x37c (0x180ce6fc2). rpg_param.xml has Harmless
    1.5 and Injuring 2.2. Fatal is not in the xml, so the code default applies (not read).
  - Stat ids: in the DerivStat name table, `DerivStat_FallDamageMultiplier` (pointer at 0x1840acd00) and
    `DerivStat_EnableFallDamage` (0x1840ad0e8) are 125 slots apart, and 0xCB - 0x4E = 125. That is consistent, but the table base was not
    pinned down.
- The callers of slot +0xd0 (found by scanning for `call FUN_180648438` followed by `call [rax+0xd0]`):
  1. **FUN_180724e70 = C_ActorActionLand start** (vtable C_ActorActionLand 0x183a4ad90, slot +0x130 → FUN_1807245fc).
     Height = **FUN_180725594**: `max(land+0x50 - entity->GetWorldPos().z, 0)`, where land+0x50 is the start Z handed to
     the land action. The debug text in FUN_18287d570 is "State: Falling/Landing, Height, Duration"; +0x54 is the time in the air.
     A second call goes to the carried body (when the stance context 10 is set).
  2. FUN_182391c82 (not examined).
  3. FUN_18094ce80 → FUN_181f1e990: **sliding ends**. Height = `wh_player_SlidingDistanceMultForDamage` (default 0.5,
     wh_actor cvars +0xE8) × the slide distance (actor+0xB5C).
- After the call, the land action (FUN_180724e70) asks C_RPGUtils (singleton 0x1855e7460, vt+0x10 = FUN_1807261dc) to
  classify the cause by the health and stamina change it would make. Class 0 means no reaction, 5/6 give events 0x19/0x1a,
  and >=6 is a heavy land. With no damage the class is 0, so there is no stagger either.

## 2. Where the start height comes from (two arming paths, both freeze the take-off Z)
### 2a. Walking or flying off the ground: the fall detector (the path that fires in flight)
`wh::entitymodule::C_ActorStateExpansion::vftable` 0x183b49608, slot +0x10 = FUN_1804a8ac8 → **FUN_1809d36f0**, called every update:
```
inAir = !FallDisabled && (actor+0x5fc & 1)
if (inAir) det+0x10 += dt                                   // time in air
else     { det+0x10 = 0; det+0x14 = entity->GetWorldPos().z }   // last ground Z, refreshed every grounded frame
if (det+0x10 > 0 && actor+0x590 (vz) < -cv.pl_movement.ground_velocityToFall (+0x98, 3.5)
                 && det+0x10 > cv.pl_movement.ground_timeInAirToFall (+0x94, 0.5))
    FUN_180e77788(det, {startZ = det+0x14, timeInAir = det+0x10})   // = start C_ActorActionFall
```
- `cv` = `*(FUN_1809155c8()+0x20)`, the struct registered by FUN_18158fc98. `pl_movement.ground_velocityToFall`
  ("Minimal Z velocity to enable falling", 0x183e0d410) is at cv+0x98, registered at 0x181593c0f with default XMM15 = **3.5**
  (0x18158fe2a). `pl_movement.ground_timeInAirToFall` is at cv+0x94, default **0.5** (0x181593bbd).
- **FallDisabled** is `C_ModelContextProperty<E_FallDisabledContext,bool>` at `actor+0x990 → +0x238` (constructed at 0x1803f0480,
  vftable 0x183a5d878, Get = vt+0x10 = FUN_181a81d30 `return byte[+0x1f]`). It ORs source bytes +0x18..+0x1e, which are set
  through **FUN_1809d3894(prop, sourceIndex, bool)**. The sources in use: 0 (0x18040a967 clears, 0x180f49e34 sets), 1 (ground
  state 0x1809d1ca0, from physics), 2 (0x1809d1b38 clears), 3 (the "noninteractive" state FUN_1817478c0). **Sources 4-6 are unused.**
- FUN_180e77788 does nothing while bit **0x12** of the flag context `actor+0x990 → +0x370` (vt+8 = test) is set. The game's
  jump start sets that bit for the player (FUN_180cf7f98 → FUN_1809daff4(...,0x12,1)), so a game jump suppresses this path.
- C_ActorActionFall (vftable 0x183ac2030; ctor FUN_180e778a0; params copied to fall+0x50/+0x54 by FUN_180e7780c) hands
  the same {startZ, time} to the land action when it ends (FUN_180724590 → FUN_180724aac → C_ActorActionLand,
  S_LandActionParams at land+0x50/+0x54).
- `actor+0x5fc & 1` reads as "in the air / flying": FUN_180723420 lands when it clears, and the UnstuckSpeed help text says
  "when flying". This is inferred, not proven.

### 2b. The game's own jump (Space on the ground)
- The ground state (FUN_1809d1b4c, event 0x65) jumps when a jump is requested (move request +0x5e), context 0x1d is clear and
  the time on the ground (actor+0x418) is over 0.2 (FUN_1809d20d8). **Space is the flight's "up" key**, so taking off with Space
  from the ground very likely also starts KCD2's jump (not verified in game).
- C_ActorActionJump (vftable 0x183b51b90) start is FUN_180cf7f98 → **FUN_180cf8ee4**, which sets `jump+0x68+0x1c = entity Z at the start`
  (the start Z). The update (vt slot 0x183b51c78 = FUN_180722714 → FUN_180722870 → FUN_180722940) waits
  `wh_actor_LandCheckDelay` (+0x74, 0.3 s), then lands when **FUN_180723420** is true:
  - `actor+0x5fc & 1` is clear (back on the ground), or
  - "unstuck": still in the air, with speed² ≤ `wh_actor_UnstuckSpeed`² (+0x68, default 0.1, so about 0.1 m/s) for longer than
    `wh_actor_UnstuckJumpTimeout` (+0x6c, default 1.0 s). A flight hover with no keys held meets this.
- Landing is FUN_180723484 → FUN_1807236b8 → C_ActorActionLand with {startZ = jump+0x84, time = actor+0x410}. This is
  skipped if flag-context bit **0x13** is set.
- So the jump path also measures from the take-off Z. An "unstuck" landing mid-hover does no damage (start - hover < 0,
  clamped to 0), but it ends the jump and clears bit 0x12. A later descent faster than 3.5 m/s then arms path 2a, still
  against the frozen take-off ground Z.

## 3. What resets the start height
- Path 2a: every frame Henry counts as grounded (`actor+0x5fc & 1` clear) **or FallDisabled is set**, det+0x14 = current Z.
  Nothing else writes it. Height gained in flight never changes it, and neither does hovering or any speed.
- Path 2b: only a new jump action (FUN_180cf8ee4). The only other reader is FUN_180723484.
- Swimming or stance changes: no write to these fields was found. The jump-area functions were scanned for +0x1c/+0x84 writes.

## 4. The cvars that look relevant but aren't
- `pl_fallDamage_Normal_SpeedSafe` (cv+0x6a4, default 8.0), `pl_fallDamage_Normal_SpeedFatal` (cv+0x6a8, default 13.7),
  `pl_fallDamage_SpeedBias` (cv+0x6b4), `pl_debugFallDamage` (cv+0x6b8, 0x18159403f), and `pl_fallHeight` (cv+0x60c,
  default 0.8, "The height above the ground at which the player starts to fall", 0x1815930bb): **no reader found**. A
  full-binary scan for the access pattern `call FUN_1809155c8; mov rX,[rax+0x20]; ... [rX+off]` finds the readers of
  +0x94/+0x98 (FUN_1809d36f0 and others) but none for these offsets, and the remaining float accesses to 0x6a4/0x6a8/0x6b4/0x60c
  are other structs (stack frames, rpg params). They are leftover GameSDK cvars. **Setting them changes nothing.**
- `wh_actor_FallSpeedHeavyLand` (wh_actor cvars, struct DAT_1855dbec0 via FUN_1804aade0, +0x38) only picks the
  heavy-land animation.

## 5. Fixes, cheapest first
1. **Lua: the game's `fall_damage` buff (recommended).**
   - Write: `player.soul:AddBuff("4bc0b081-a57e-4e6e-8297-6d9db58b39b2")` when the flight starts (FlightDyn, after
     `dyn(p, FLYING)`). It must be in place at touchdown, and the fall can be armed any time during the flight.
   - Remove it with `player.soul:RemoveAllBuffsByGuid("4bc0b081-a57e-4e6e-8297-6d9db58b39b2")` (the binding name exists in
     WHGame strings). Do this about **1 s after** `FlightStop` gives gravity back. By then the Land action has started on the
     first grounded frame and applied (zero) damage. Better still, wait until `groundGap(p)` ≤ ~0.05 and then 1 s more.
   - Why it works: `fdm=0` makes stat 0x4E zero, so h' = 0 in FUN_1807243b0. No health damage, no leg injury, and the
     RPGUtils class is 0, so no stagger. It covers all three paths (2a, 2b, slide end).
   - Caveats: the buff is `is_persistent="true"`, so a save made mid-flight keeps it. Remove it unconditionally at script
     load and whenever flight isn't active (`hits.lua` already uses `soul:AddBuff`). A quest that adds the same buff
     would lose it to our RemoveAllBuffsByGuid (low risk; there is no `HasBuff` binding name to check first). Nothing in the
     binary confirms that `AddBuff` takes a GUID string here, but that is how hits.lua's bleeding buff is used.
2. **Lua, partial: stay slower than 3.5 m/s going down.** Use `landSpeed` ≤ 3 and cap the flight's own downward velocity
   above -3.5. That keeps path 2a from arming, which also avoids a "falling" action and animation in mid-air. It does **not**
   cover path 2b (a Space take-off from the ground): that landing measures take-off Z minus landing Z on touchdown whatever
   the speed. Use it on top of fix 1, not instead of it.
3. **Native, the engine's own switch:** set FallDisabled source 6 while flying with
   `FUN_1809d3894(actor+0x990 → +0x238, 6, 1)` (RVA 0x9D3894) and clear it with `(…, 6, 0)` about 1 s after touchdown.
   Use the function, not a raw byte write, because it runs the listeners. Result: det+0x14 follows the current Z and no Fall
   action is created. Add `FUN_1808324d4(actor+0x990 → +0x370, 0x13, 1)` (the flag-context set used by FUN_180cf91ac,
   signature inferred) to also block the jump path's landing. **Open:** how to get from Henry's entity to this actor object
   (the object with +0x38 IEntity, +0x5fc flags, +0x990 model context and +0x590 vz). That is not mapped.
4. **Native, single choke point:** replace the vtable slot 0x183d19018 (C_RPGMovement +0xd0) with a thunk. If
   `S_FallDamageData.entityId == Henry` while the flight guard is on, it does `*out = 0; return out;`. FUN_180724e70
   handles a null cause (it marks the landing and skips the reaction). The slide path's FUN_1819bed94 with a null cause was
   not checked. This needs VirtualProtect on .rdata and has the most crash risk of the four.

## Open questions
- Whether `actor+0x5fc & 1` is set while Henry hovers with gravity 0 (assumed: he has no ground contact).
- Whether Space while flying (in the air) can start another game jump. The jump needs ground time > 0.2 s
  (FUN_1809d20d8), so probably not.
- FatalFallHeight's default, and the player's `soul+0xcf0` bit 4.
- FUN_182391c82 (the third caller of OnFallDamage) was not read.
- An in-game check of fix 1: take off from a roof, land 5 m lower slowly, with and without the buff. Watch `wh_rpg` health.
