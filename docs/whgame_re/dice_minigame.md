WHGame 1.5.6 dice minigame (static RE, 2026-10-27)

## Dice advisor research on WHGame.dll 1.5.6

Static analysis only, nothing was run. Addresses are VAs with RVA = VA − 0x180000000, and decompile evidence is in `~/re/index/KCD2/WHGame.dll.c.zst` on the ProLiant (queried with `qk`). Everything is marked **V** (verified in decompile) or **I** (inferred). I appended about 55 lines to `~/re/index/KCD2/names.tsv` on the ProLiant. Nothing was added to the repo.

### 1. Classes and how to reach the live instance

All classes are in namespace `wh::playermodule` unless noted.
- **`C_Dice`**: the controller for the player's dice minigame, action type 7, size 0x5b8.
  - Constructor `0x182e7ec5c`. The vtables are at `0x183f72040`, `0x183f72060` and `0x183f72098`, so `0x183f72040` is the primary one.
  - It is created by `FUN_1809f0234` (switch case 7), which is reached through `FUN_182024240` (`PlayerActionRegistry::FindOrCreate`). V
- **`C_DiceGameModel`** (called G below): constructor `0x182e7f41c`, vtables `0x18473f178` and `0x18473f368`. The second one is the big one.
- **`C_DiceGamePlayerModel`**: constructor `0x182e7f608`, vtable `0x18473f198`.
- **`C_DieModel`**: constructor `0x182e7f76c`, vtable `0x18473f160`.
- **States** (`I_DiceGameModelState` subclasses), with vtable VAs:

| State | Vtable |
|---|---|
| PlayerTurn | `0x183f711e0` |
| Rolling | `0x183f71a00` |
| Scoring | `0x183f71ba0` |
| Passing | `0x183f716c0` |
| Bust | `0x183f71860` |
| Initial | `0x183f71380` |
| PlayerChange | `0x183f70928` |
| Ending | `0x183f70788` |
| End | `0x183f705e8` |
| ExtraRolling | `0x183f702a8` |
| ReRolling | `0x183f70ac8` |
| ChangingDieValue | `0x183f70c68` |
| BadgeDiceSelecting | `0x183f70e08` |
| CheckBustBadge | `0x183f70fa8` |
| CheckPassiveBadge | `0x183f70448` |
| Null | `0x183f71520` |
| Holding | `0x18473f1c8` |

- A state's vtable slot 1 returns its name string (e.g. "Rolling"). Predicates on a state are `vt+0xf0` Initial, `+0xf8` PlayerTurn, `+0x100` Holding, `+0x108` Scoring, `+0x110` Rolling, `+0x118` Bust and `+0x120` Passing. V
- **UI classes**: `wh::guimodule::C_UIDice` (constructor `0x182bb7da4`) and the interface `I_UIDice`, which sits at C_UIDice+0x58.

**Getting the instance:**
- The game's own way: `FUN_182e8afb4` at `0x182e8afb4` returns the local player's `C_Dice*`. V
  - It calls `FUN_182024240(registry, entityId, 7, create, flag)`.
  - `registry` = `(*(gGame+0x128))->vt+0x38()` and `entityId` = `(*(gGame+8))->vt+0x200()->+0x30`, where `gGame` = `FUN_1809155c8()` at `0x1809155c8`.
- Safest for a MinHook plugin: hook one of the C_Dice functions and capture `this` from RCX. The best choice is `FUN_182e907d0` (see section 3).
- In `C_Dice`:
  - `+0x4a0` is G (a shared_ptr, with the control block at +0x4a8). V
  - `+0x598` is the `I_UIDice*`. V
  - `+0x348` is playerId, `+0x34c` is tableId, and `+0x350` is the opponent actor (set in `C_Dice::StartGame` at `0x182e97b64`). V
  - `+0x4f0` is the cursor die index (−1 means none). V
  - `+0x4b0 + playerIdx*0x10` is a shared_ptr to that player's dice "side" object. `+0xf0..+0xf8` of that object is a vector of 16-byte die views. V

### 2. Live state layout

**G (C_DiceGameModel).** V
- `+0x20` is player A, the local player (shared_ptr, control block at +0x28).
- `+0x30` is player B, the opponent.
- `+0x40` is the state-change signal. `FUN_182e8d2f0` subscribes `FUN_182e907d0` to it.
- `+0x100` is the target score. The constructor takes the `wh_pl_DiceTargetScore` default of 2000, and `C_Dice::Start` overwrites it.
- `+0x108` is a turn controller whose `vt+0x20()` returns the current player model.
- Per-player formation tables sit at `(G+0x18) + idx*0x18`; each entry is 0x20 bytes with the score at +0x18.

**Player model, one for A and one for B.** V
- `+0x18` is the turn (unbanked) points.
- `+0x30` is the banked total.
- `+0x48` is the AI object pointer: null means human, non-null means NPC. NPC difficulty is set via the AI's `vt+0x60`.
- `+0x58` is the player index, 0 or 1.
- `+0xd8` and `+0xe0` are begin and end of the dice vector. Each element is 16 bytes (a shared_ptr of `C_DieModel`), so the count is (end−begin)>>4. It is 6, or up to 9 with ExtraDice (I).
- `+0x180` is a byte: 1 means the last roll has no scoring formation, i.e. bust.
- `+0x184` is the score of the last scoring action.
- `+0x188` is the shared_ptr to the badge (the control block is at +0x190).
- `+0x198` is a byte flag for "already scored", which skips the score step in Passing.
- `+0x199` and `+0x1a0..+0x1a8` hold predetermined throw values, a vector<int>. Their producer is unknown.
- `+0x1b8` is the actor, `+0x1c0` is the ThrowFixing probability and `+0x1c4` is the BustFixing probability.

**C_DieModel.** V
- `+0x28` is the die id, 0..5 for each player (set in the player constructor).
- `+0x40` is a byte state: 0 = in hand, 1 = selected/held, 2 = scored and set aside.
- `+0x58` is the current value (see the face-value point below).
- `+0x88..+0x9c` are 6 floats, the face probabilities (weights divided by their sum, normalised in `FUN_182e960f8`).
- `+0xa0..+0xb4` are 6 ints, the SideValues, taken from the item table.
- A die with a joker on slot 0 has `die+0xa0 == 6`. The check `if (+0xa0 == 6)` appears in the collection and fixing code.
- **Face value:** `die+0x58` is in 0..5 for faces 1-6, and **6 is the joker**. `FUN_182e93c70` returns `SideValues[pickedSlot]`. The element passed to the matcher is `(value = +0x58, flag = (+0xa0 == 6))`.
- There is no item GUID or class id on the die model. It copies only weights and SideValues, so it cannot be tied to a CSV `internal_id` directly.
  - For the solver this does not matter: the weights and joker are right there.
  - If you need an id, match the 6 normalised floats against `dice_game.csv`. Twins (Ci/Lu/Fer, Painter's R/G/B, Lucky and its quest copy) are indistinguishable but have identical weights.
- The side-view object holds screen positions. Each die view has `+0x18` as the Vec2 screen position, `+0x30` as the die entity id, `+0x40` as a CryString name, `+0x48` as the `C_DieModel*` and `+0x80` as the highlight flag.
- The opponent's dice are player B's vector (`*(G+0x30) + 0xd8`), with the same layout. V

**When the outcome is decided:** at roll time, not when the dice land. `FUN_182e99004` (`PlayerModel::RollDice(reroll)`) writes `die+0x58` for every non-scored die through `FUN_182e93c28`. V
- It then evaluates the free dice and sets the bust flag at `+0x180`.
- I assume the physical dice are steered to match, but that is **I**.

**Scores for the HUD.** `FUN_182e969c0` at `0x182e969c0` refreshes the HUD by calling `I_UIDice::ShowDiceScore` with the target and, per player, current, total and selected points. V
- Hooking `ShowDiceScore` (`0x182bbb5a4`) therefore gives every number the HUD shows as plain arguments.
- The "selected" points come from `FUN_182e947f4(G+0x18, 1, &score)`, which scores the dice with state 1.

### 3. Hook points and the player's state

- **State transition: `FUN_182e907d0` (`C_Dice::OnDiceStateChanged(this, ?, oldState, newState)`)**. V that it is the subscriber on `G+0x40`.
  - To detect "throw settled, player may choose", check that `newState` has the PlayerTurn predicate true (`vt+0xf8`, vtable `0x183f711e0`) and that `newState->vt+0x20()` returns a player model with `+0x48 == 0` (human). That is **I** for timing.
  - The Rolling state's next-state function `FUN_182e98cb8` chooses the Bust state if `player+0x180` is set, otherwise the next playable state. That is the only exit from Rolling that I found.
  - Alternative: a post-hook on `FUN_182e99004` (RVA 0x2e99004) sees the final values at roll time, before the dice visibly settle.
- **Select (hold) a die:**
  - `FUN_182e8fa84` is the `dice_hold` input handler. It calls `FUN_182e97f64(G, dieId = view->+0x48->+0x28, toggleTo = (die+0x40 != 1))`.
  - That requests a `C_HoldingDiceGameState{dieId, hold}`. Its `OnEnter` is `FUN_182e91eb0`, which calls `FUN_182e8becc`.
  - `FUN_182e8becc` flips `die+0x40` between 0 and 1 only. A die in state 2 can't change. V
- **Throw again (score and continue):**
  - `FUN_182e91b3c` is the `dice_score` handler. It gates on `sm->vt+0x90` and then dispatches `sm->vt+0x98`, with `sm = *(G+0x108)`.
  - Scoring `OnEnter` (`FUN_182e92110`) calls `ScoreHeld(1.0)`.
  - Then Rolling `OnEnter` (`FUN_182e92084`) increments the throw count and calls `RollDice`. V
- **Bank/pass:**
  - `FUN_182e919e0` is the `dice_pass` handler. It runs `ScoreHeld(mult)` in Passing `OnEnter` (`FUN_182e91f28`).
  - Then PlayerChange `OnEnter` (`FUN_182e91f80`) does `total(+0x30) += turn(+0x18)` and hands the turn over.
  - PlayerChange `OnExit` (`FUN_182e902c8`) resets turn points to 0.
  - A bust zeroes turn points in `FUN_182e901b4`. V
  - The badge-use handler is `FUN_182e8f86c`.
- **Other useful functions:**
  - `ScoreHeld` is `FUN_182e93ee4`, the matcher is `FUN_182e94138`, and the table builder is `FUN_182e8dc9c`.
  - The matcher returns true only if every selected die is consumed and the score is above 0.
- **Not found:** where the current state object is stored. The handler receives it as an argument, so keep your own copy of the last `newState` you see.

### 4. HUD calls (`AddDiceSelector`, `ShowDiceProperties`)

- **I_UIDice vtable** (the sub-vtable inside C_UIDice, at `0x183c342f8`). V

| Slot | Function | VA |
|---|---|---|
| 0 | ShowDiceScore | `0x182bbb5a4` |
| 1 | HideDiceScore | `0x182bb9b6c` |
| 2 | AddDiceSelector | `0x182bb8a5c` |
| 3 | RemoveDiceSelector | `0x182bbaaec` |
| 4 | ShowDiceCursor | `0x182bbb4b0` |
| 5 | HideDiceCursor | `0x182bb9ac0` |
| 6 | ShowDiceProperties | `0x182bbb53c` |
| 7 | HideDiceProperties | `0x182bb9b34` |

- **Caller of `AddDiceSelector` / `RemoveDiceSelector`:** `FUN_182e96ce4` (`C_DieView::SetSelectorVisible(bool)`) calls `ui->vt+0x10(ui, id, &Vec2)` and `ui->vt+0x18(ui, id)`. V
  - `ui` is `FUN_182e8afb4()->+0x598`.
  - `Id = dieModel+0x28`, the die id 0..5 (or up to 8 with ExtraDice). It is **not unique across players**, since both players' dice start at 0.
  - The third argument points to a Vec2 (x at +0, y at +4).
- **Where X/Y come from:** `FUN_182e8a790(unused, out Vec2, worldXYZ)` projects the die's world position (from its entity) through the camera and converts to centred UI space. V
  - The conversion is `ui = (pixel − windowSize/2) * 1080/windowHeight`, so (0,0) is the screen centre and the height is 1080.
  - The cursor handler `FUN_182e8fbfc` also writes `view+0x18` (the same projection, in plain pixels).
  - The selector is only re-sent when the position changes by more than 0.5.
- **ShowDiceProperties:** the cursor path (`FUN_182e951bc` into `FUN_182e96998`) passes `name = view+0x40` and `value = die+0x58`.
- **Calling `AddDiceSelector` ourselves:** the mechanism looks sound, but I cannot call it safe.
  - A call through `ui->vt+0x10` reuses the game's own path. I did **not** verify the Flash side, or what it does with an Id the game never sent.
  - Use Ids at 100 or above, e.g. `100 + dieIndex`, to avoid the game's 0..8.
  - Call `RemoveDiceSelector` for them when the minigame ends.
- **Open point:** the Flash function takes an `IsPlayer` argument, but the C++ thunk (`FUN_182bb8a5c`) never receives one from the caller. It passes the address `I_UIDice+8` (that is, C_UIDice+0x60) as the sixth marshalled argument.
  - I could not tell whether that is a bool field the game sets elsewhere, or something else.
  - Check it live (see below).

### 5. Hot dice and minimum bank

- **Hot dice: yes, verified.**
  - `FUN_182e842b8` (`Model::AllFirstSixScored`, model `vt+0x38`) is true when the first 6 dice all have `die+0x40 == 2`.
  - Rolling `OnEnter` (`FUN_182e92084`) runs it, and if true it applies a lambda to every die that sets state 0 (`FUN_18202bc00`, which calls `FUN_182e954fc(die, 0)`).
  - Then it rolls, so the player gets fresh dice. With ExtraDice the check covers only the first 6 dice, but the reset applies to all of them.
- **Minimum bank:** none found, but I did not search exhaustively. V for the paths below.
  - `ScoreHeld` fails only when the held dice are not fully consumed by formations or the score is 0 or less (the `0 < score` return in `FUN_182e94138`).
  - Neither Passing, PlayerChange nor Scoring compares the turn score against a threshold.

### Derived stats DiceThrowFixing / DiceBustFixing / DiceAIDifficulty

- **Who reads them:** only `FUN_182e94908` (`PlayerModel::BindActor`). It sets `+0x1b8 = actor` and then reads `actor+0x668` (the stat container) with `vt+0x2c8(index)`. V
  - Index 13 (0xd, the table at `0x1840aca90`) goes to `+0x1c0`, which is ThrowFixing.
  - Index 14 (0xe) goes to `+0x1c4`, which is BustFixing.
  - Index 17 (0x11) is AIDifficulty, passed to the AI object's `vt+0x60` (NPCs only).
- **Whose stat:** each participant uses its own actor's stats. Player A is the local player and player B is the opponent. V
- **What changes in a throw** (`FUN_182e99004`, after the roll, with the game's `rand`): V
  - ThrowFixing, probability `+0x1c0`:
    - If at least two free dice share a value (and that value is not the joker) and not all dice share it, one other free die is changed to that most common value.
    - The bust flag is recomputed afterwards.
  - BustFixing, probability `+0x1c4`, applied when the flag is still set after the above:
    - The first free die is forced to value 0 (a one). If that die has a joker on slot 0, it is forced to value 4 (a five) instead, because such a die has no plain one.
    - The bust flag is recomputed. This is a "saved bust".
- **Defaults:** I did not find the numeric formula, so the baseline is **I**. Table data shows only one source of non-zero values: buff `golden_egg` (`Libs/Tables/rpg/buff.xml`).
  - Its params are `charisma=20,rcw=3,pt1+0.5,pt5+0.5,dtf+0.75,dbf+0.25`, with duration 600. `dtf` is DiceThrowFixing and `dbf` is DiceBustFixing.
  - No table or script references that buff by name or GUID, so it is probably granted by an item or by code I did not find.
- **Related find:** the perk "Lucky Day" is `bnc+0.4`, the chance that a badge is not consumed. It is a separate stat (index 0x12).
- **Face-weight multiplier (I):** each face weight is also multiplied by a per-actor modifier before the pick (`FUN_182e8bb2c`, via a game-singleton `vt+0x198` that I did not resolve). The golden-egg `pt1` and `pt5` stats are the likely sources.

### Left unknown

- Where the current state object is stored.
- The opponent AI's decision code (`C_SimpleDiceAI`) beyond its difficulty field.
- The sixth marshalled argument in `AddDiceSelector` (the `IsPlayer` question above).
- The `+0x1a0` predetermined-throw vector and its producer.
- The numeric defaults of the three derived stats.
- Whether the physical dice always land on the rolled `+0x58` value.
- Whether `die+0x58` changes under the Balatro and set-die badges. The `ChangingDieValue` state is where it would, but I did not read it.

### Proposed read-only live checks

Run these with cdb attached while the user sits at a table and it is the user's turn.
1. Break at `base+0x2e91b3c` (the `dice_score` handler), or at `base+0x2e907d0`, and note `rcx` as `C_Dice* C`.
2. `dq C+4a0 L1` gives G. `dq G+20 L1` gives A, and `dq G+30 L1` gives B.
3. `dd A+18 L1` should match the HUD turn points. `dd A+30 L1` should be the total, `db A+180 L1` the bust flag, and `dd G+100 L1` the target.
4. `dq A+d8 L2` gives begin and end of the dice vector. Then for each 16-byte entry's first qword D:
   - `dd D+28 L1` (id);
   - `db D+40 L1` (state);
   - `dd D+58 L1` (value);
   - `dd D+88 L6` (probabilities, as float bits);
   - `dd D+a0 L6` (SideValues).
5. Take the UI object `U = dq C+598 L1`. Compare `dd U+60 L1` before and after a selector appears, to see whether that is the `IsPlayer` flag.
6. Compare `A+0x1c0` and `A+0x1c4` (float) before and after eating the golden egg, if you can get one.
