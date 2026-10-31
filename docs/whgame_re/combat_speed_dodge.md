# Attack speed vs NPC dodge (WHGame 1.5.6, 2026-10-30)

- `FUN_180c5683c` "Combat_NpcDefenseChoiceWeights" = C_RPGCombat vtable (0x183ae4410) +0x1b0:
  `(this, float out[5], ctx*)`. ctx[0], ctx[1] = the two combatants (vtable calls +0x400/+0x330/+0x2c8/+0x120 on [0],
  +0x310/+0x120 on [1]), ctx+0x18 float, +0x1c/+0x1d flags, ctx[4] a wuid. out[4] = dodge:
  lerp(CombatDodgeProb0 +0xc44, CombatDodgeProb1 +0xc48 by skill gap) x CombatDodgeProbCoef +0xc4c x
  (rpg+0x40 - ctx[0] stat 0x18). No attack-speed input.
- `FUN_180d4de0c` "Combat_AttackSpeedMultiplier" = C_RPGCombat vtable +0x120: `(this, attacker, defender)` ->
  `(f - 0.5) x mod + 1 + 0.5 x mod` with mod = MaxAttackSpeedPlayerMod (+0x58) when `attacker->vtbl[0x410](0) != 0`
  (the player), else +0x5c (NPC) / +0x54 (NPC vs player). f from FUN_180d4dee8 (skill x agility).
- Prologues (qk data): weights `48 8b c4 48 89 58 08 48 89 70 10 55 57`, speed
  `48 8b c4 48 89 58 08 48 89 70 10 48 89 78 18`.
- `reach.asi` (native/src/reach_main.cpp): records the speed of the player's attacks (speed, defender pointer, time)
  and multiplies out[4] by speed ^ -dodge_speed_exp when the weights come within 1.5 s and ctx[0] or ctx[1] is that
  defender, or within 0.6 s without a match. Unverified in game: that ctx holds the same defender pointer (reach.log
  says "same defender" or "by time only" for the first 40 changes).
