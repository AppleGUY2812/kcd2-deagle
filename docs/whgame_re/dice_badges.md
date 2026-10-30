# WHGame 1.5.6 Dice Badges (Static Reverse Engineering)

All addresses are VAs (RVA + 0x180000000). Every claim is marked **V** (verified in code or table), **I** (inferred), or **U** (unknown).

## 1. Badge Items and Subtypes (item.xml)

There are 35 badge items defined in `Libs/Tables/item/item.xml` across 13 subtypes (`Libs/Tables/item/dice_badge_subtype.xml`) and 3 tiers (`dice_badge_type.xml`: 0 = plumb/tin, 1 = silver, 2 = gold). V: item.xml.
Row counts per subtype:
- Headstart (3), Formations (3), Null (3), ExtraValue (1), Antibust (3), DoubleTake (2), Multiplier (3), ExtraDice (4), RerollDice (4), SetDice (3), RerollPips (2), BoostSixes (1), Balatro (3). Total: 35. V: item.xml.
Full list exported to `tools/farkle/badges.csv`.

## 2. Badge Mechanics and Exact Numbers

Universal badge configuration is loaded by `wh::playermodule::C_DiceBadge::ctor` (`0x182e7f30c`) calling `FUN_182e88590` (`0x182e88590`), which fills an `unordered_map` at `badge+0x38`. Values are queried by `FUN_182e898c4` (`0x182e898c4`). V: code.

### 0: Headstart (Passive)
- Lead points added directly to banked score (`player+0x30`) during game initialization in state `CheckPassiveBadge` (`FUN_182e8411c` at `0x182e8411c`). V.
- Formula: `banked += (int)(fraction * (float)targetScore)`. Target score default is 2000 (`wh_pl_DiceTargetScore` at `0x1855edda4`). V: `0x182e8411c`.
- Fractions: Plumb = 0.05 (5%, 100 pts @ 2000), Silver = 0.10 (10%, 200 pts @ 2000), Gold = 0.20 (20%, 400 pts @ 2000). V: float constants at `0x1840b9200` (`0.05f`), `0x1840b9204` (`0.10f`), `0x1840b9208` (`0.20f`). 1 use (consumed at game start). V.

### 1: Formations (Passive)
- Injects a special scoring formation into the player's formation table in `DiceFormationTable::Build` (`0x182e8dc9c`). V.
- Plumb: "The Cut" (pips 3 + 5) = 200 pts. V: `wh_pl_DicePlumbFormationsScore` (default 200) registered in `FUN_18159ec00` (`0x18159ec00`) into `DAT_1855edddc`.
- Silver: "The Gallows" (pips 4 + 5 + 6) = 400 pts. V: `wh_pl_DiceSilverFormationsScore` (default 400) registered in `FUN_18159ec00` into `DAT_1855edde0`.
- Gold: "The Eye" (pips 1 + 3 + 5) = 500 pts. V: `wh_pl_DiceGoldFormationsScore` (default 500) registered in `FUN_18159ec00` into `DAT_1855edde4`.
- Always active throughout the game (passive, 1 use flag). V.

### 2: Null (Passive Defence)
- Cancels the opponent's badge during initialization in `CheckPassiveBadge` (`0x182e8411c`), executing `FUN_182e8e0b8` (`0x182e8e0b8`). V.
- Tier gating (`0x182e8411c`): Gold cancels any opponent tier (plumb, silver, gold); Silver cancels opponent tier < 2 (plumb or silver); Plumb cancels opponent tier == 0 (plumb only). V.
- Cancellation zeroes opponent `badge+0x30` uses, zeroes banked points if opponent had Headstart, or rebuilds formation table without special formations. V: `0x182e8e0b8`.

### 3: ExtraValue (Passive)
- Only Gold exists (`ExtraValuebadgeGold`). Multiplies base 1+1+1 (three 1s) score by 3.0x: base increases from 1000 to 3000 pts (4x1 = 6000, 5x1 = 12000, 6x1 = 24000). V.
- Verified in `DiceFormationTable::Build` (`0x182e8dc9c`): computes `(int)(3.0f * DAT_18409ec78)` where `DAT_18409ec78` = 1000.0f (`0x18409ec78`), factor 3.0f at `0x1840b9278`. V.

### 4: Antibust (Bust Reaction)
- When a bust occurs, `CheckBustBadge` state (`0x183f70fa8`, `FUN_182e91c00`) prompts re-rolling all unbanked dice rather than losing the turn. V.
- Uses per game: Plumb = 1, Silver = 2, Gold = 3. V: constants at `0x1840b9260` (`1.0f`), `0x1840b9264` (`2.0f`), `0x1840b9268` (`3.0f`) loaded via bitmask 0x10b0 in `FUN_182e94c28`.
- Decremented in `FUN_182e83f5c` (`0x182e83f5c`). V.

### 5: DoubleTake (Active Turn Action)
- Doubles the score of the LAST throw only: `turn_points = turn_points_before + (int)(throw_points * 2.0)`. V: `ScoreHeld` (`0x182e93ee4`).
- Can only be used after a scoring throw (`FUN_182e8e170`). V.
- Uses per game: Silver = 2 uses, Gold = 3 uses (Plumb does not exist). V: `0x1840b9264`, `0x1840b9268` via bitmask 0x10b0 in `FUN_182e94c28`.

### 6: Multiplier (Active Turn Action)
- Multiplies the TOTAL turn score when banking/scoring: `turn_points = (int)((turn_points_before + throw_points) * mult)`. V: `ScoreHeld` (`0x182e93ee4`).
- Activated in `PlayerTurn` via `FUN_182e840a4` (`0x182e840a4`), applies at scoring/passing. V.
- Multiplier factor: Plumb = +25% (1.25x, `0x1840b9250`), Silver = +50% (1.50x, `0x1840b9254`), Gold = 2.0x (double, `0x1840b9258`). V.
- Uses per game: 1 use per game for all tiers (not in 0x10b0 mask in `0x182e94c28`). V.

### 7: ExtraDice (Active Turn Action)
- Adds 1 extra die to the hand for the throw. V: `0x182e83bc8`.
- Can only be activated before a throw when all 6 dice are in hand (`dice_count == 6`, `FUN_182e8e170`). V.
- Uses per game: Plumb = 1, Silver = 2 (both Silver Might and Bird King), Gold = 3. V: `0x1840b9260`, `0x1840b9264`, `0x1840b9268` via `0x182e94c28`.

### 8: RerollDice (Active Turn Action)
- After a throw, reroll up to N non-scoring or selected dice. Handled in `BadgeDiceSelecting` state (`0x183f70e08`). V.
- Max dice allowed to reroll (`FUN_182e83bc8`): Plumb = 1 die (`0x1840b9260`), Silver = 2 dice (`0x1840b9264`), Gold = 3 dice (`0x1840b9268`, Fortune & Matrimonium). V.
- Uses per game: 1 use per game across all tiers. V: `0x182e94c28`.

### 9: SetDice (Active Turn Action)
- After a throw, transmute 1 die of choosing to a fixed face value. Handled in `ChangingDieValue` state (`0x183f70c68`). V: `FUN_182e83bc8`.
- Face index set (`FUN_182e8e170`): Plumb = pip 3 (index 2.0f at `0x1840b9110`), Silver = pip 5 (index 4.0f at `0x1840b9114`), Gold = pip 1 (index 0.0f at `0x1840b9118`). V.
- Uses per game: 1 use per game across all tiers. V: `0x182e94c28`.

### 10: RerollPips (Active Turn Action)
- After a throw, reroll dice with specific criteria. Handled in `BadgeDiceSelecting` state (`0x183f70e08`). V: `FUN_182e83bc8`.
- Silver: reroll 1 die of choosing (`1.0f` at `0x1840b9244`). V.
- Gold: reroll 2 dice sharing the same value (`2.0f` at `0x1840b9248`). V.
- Uses per game: 1 use per game (Plumb does not exist). V: `0x182e94c28`.

### 11: BoostSixes (Passive)
- Only Gold exists (`ziskejOdznacek_tycheGoldBadge`). Whenever 3 or more sixes are scored, their base points are doubled (2.0x multiplier at `0x1840b9258`). V.
- Verified in `DiceFormationMatcher::Evaluate` (`0x182e94138`) -> `FUN_182027c20` (`0x182027c20`): counts scoring sixes (`n >= 3`), subtracts base six points (`n * 600 - 1200`), and adds `base_six_points * 2.0f`. 3 sixes = 1200 pts (instead of 600). Passive, permanent. V.

### 12: Balatro (Active Turn Action)
- On a die with a joker or devil's head symbol, transmute that die to the joker symbol. V: `text_ui_items.xml`, `FUN_182e8e170`.
- Uses per game: Plumb = 1, Silver = 2, Gold = 3. V: `0x1840b9260`, `0x1840b9264`, `0x1840b9268` via bitmask 0x10b0 in `0x182e94c28`.

## 3. Turn Usage, Inputs, States, and Use Decrement

- **Input mapping** (from `Libs/Config/defaultProfile.xml` and `keybindSuperactions.xml`):
  - `dice_badge_use`: Keyboard **`R`**, Xbox **`LT`** (`xi_triggerl_btn`), PS **`L2`** (`pad_l2`). Dispatches to `C_Dice::OnBadgeInput` (`0x182e8f86c`). V.
  - `dice_exit_badge_effect`: Keyboard **`X`**, Xbox **`RT`** (`xi_triggerr_btn`), PS **`R2`** (`pad_r2`). Dispatches to `0x182e90388`. V.
- **States**:
  - `PlayerTurn` (`0x183f711e0`): Main state where active turn badges (DoubleTake, Multiplier, SetDice, Balatro, RerollDice, RerollPips, ExtraDice) are triggered. V.
  - `CheckBustBadge` (`0x183f70fa8`): Intermediary state after a bust roll; prompts for Antibust. V.
  - `BadgeDiceSelecting` (`0x183f70e08`): Interactive selection of dice to reroll (RerollDice, RerollPips). V.
  - `ChangingDieValue` (`0x183f70c68`): Interactive selection of die to transmute (SetDice, Balatro). V.
  - `CheckPassiveBadge` (`0x183f70448`): State at game start resolving Headstart, Null, Formations. V.
- **Timing (before vs after throw)**:
  - Before throw: `ExtraDice` requires hand count == 6 (`0x182e8e170`). V.
  - After throw: `SetDice`, `Balatro`, `RerollDice`, `RerollPips` modify rolled dice before banking. V.
  - After scoring: `DoubleTake` and `Multiplier` require held scoring dice (`cVar4 != 0`, `0x182e8e170`). V.
  - After bust: `Antibust` in `CheckBustBadge`. V.
- **Decrement Function and Offset**:
  - Decrement is performed by **`FUN_182e86a28` (`0x182e86a28`)**. V.
  - Field offset: **`*(int32_t*)(badge + 0x30) -= 1`**. V.
  - Call sites: `FUN_182e840a4` (`0x182e840a4`, PlayerTurn), `FUN_182e83f5c` (`0x182e83f5c`, CheckBustBadge), `FUN_182e83ef8` (`0x182e83ef8`, BadgeDiceSelecting). V.
- **Perk "Lucky Day"**:
  - Table: `Libs/Tables/rpg/buff__perk.xml` row `buff_lucky_day`, `buff_params="bnc+0.4"`, `implementation="Cpp:AlcoholMood"`. Drinking skill perk, level 8 (`perk__kcd2.xml`). V.
  - Stat index 0x12 (`bnc` = Badge Not Consumed). Scales with alcohol level up to +40% chance. V.
  - `FUN_182e86a28` calls RPG manager `plVar1->vt+0x1a0(uVar3)`. If the roll succeeds, it returns 0 and skips decrementing `badge+0x30`. V.
- **HUD Display**:
  - `C_Dice::RefreshScoreHud` (`0x182e969c0`) calls `I_UIDice::ShowDiceScore` (`0x182bbb5a4`), passing `currentUses` = `*(int*)(badge + 0x30)` and `maxUses` = `FUN_182e8ba5c(badge)` for both players. V.

## 4. Reading Live Badge State for Plugins

To read live badge status without hooking or modifying code:
1. Obtain `C_Dice*` controller (via `PlayerActionRegistry::FindOrCreate` action 7, `0x182024240`, or hook `0x182e907d0` `RCX`). V.
2. Game model: `G = *(uintptr_t*)(C_Dice + 0x4a0)`. V: `0x182e7ec5c`, `0x182e8d2f0`.
3. Players:
   - Player A (human): `playerA = *(uintptr_t*)(G + 0x20)`. V: `0x182e7f41c`, `0x182e969c0`.
   - Player B (opponent): `playerB = *(uintptr_t*)(G + 0x30)`. V: `0x182e7f41c`, `0x182e969c0`.
4. Badge shared_ptr:
   - Pointer: `badge = *(uintptr_t*)(player + 0x188)`. Control block is at `player + 0x190`. Null if no badge. V: `0x182e7f608`, `FUN_182e8a108` (`0x182e8a108`), `0x182e94bc4`.
5. Offsets inside `wh::playermodule::C_DiceBadge` (`0x182e7f30c`):
   - `badge + 0x18`: `uint64_t` item GUID/ID (matches `id` column in `tools/farkle/badges.csv`). V: `0x182e8991c`, `0x182e89974`, `0x182e969c0`.
   - `badge + 0x28`: `C_DiceGamePlayerModel*` owning player model. V: `0x182e86a28`, `0x182e84ef0`.
   - `badge + 0x30`: `int32_t` remaining uses. V: `0x182e86a28`, `0x182e94c28`, `0x182e8e168`, `0x182e969c0`.
   - Subtype & Tier: Item table lookup via `FUN_1804695b4(*(badge + 0x18))`: `vt+0x50` = tier (0..2), `vt+0x58` = subtype (0..12). Alternatively match GUID against `badges.csv`. V: `0x182e8991c`, `0x182e89974`.
   - Max uses: If subtype in `{4, 5, 7, 12}`, `max_uses = tier + 1` (1, 2, 3); otherwise `1`. V: `FUN_182e8ba5c` (`0x182e8ba5c`).

## 5. NPC Opponents and AI Usage (C_SimpleDiceAI)

- **Do NPCs have badges?**: Yes, if badge items exist in their NPC inventory (`actor + 8`). In `FUN_182e84ef0` (`0x182e84ef0`), the game scans opponent inventory for category 7 items (`DiceBadge`), filters matching `tier <= requested_tier`, and randomly selects one via `FUN_182e9482c` (`0x182e9482c`). V.
- Cvar `wh_pl_DiceAIAllBadges` (default 0, registered at `0x18159ec00` line 254): if set to 1, all badges are added to NPC inventory for selection. V. If NPC has no badge in inventory (and cvar is 0), they play without one. V.
- **AI Decision Logic (`C_SimpleDiceAI`)**:
  - Antibust: Equipping Antibust immediately adds `+0.2f` to AI courage/risk stat (`AI->vt+0x78`) via `AI->vt+0x70` (clamped `[0.0, 1.0]`). In `CheckBustBadge` (`FUN_182e91c00`), `AI->vt+0x20` is called to decide whether to re-roll. When the last use is expended, `FUN_182e83f5c` subtracts `0.2f`. V.
  - Active turn badges: Dispatched in `FUN_182e949a8` (`0x182e949a8`):
    - `AI->vt+0x28`: DoubleTake (5) and Multiplier (6). V.
    - `AI->vt+0x30`: SetDice (9), passing target pip. V.
    - `AI->vt+0x38`: ExtraDice (7). V.
    - `AI->vt+0x40`: RerollDice (8) and RerollPips (10). V.
