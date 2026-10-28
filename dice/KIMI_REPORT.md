# Dice Advisor v1: build and test report

Date: 2026-10-27. Branch `kimi/advisor`, worktree `/home/johnpork/kcd2-deagle-kimi`. Nothing was committed, and nothing
was installed into either game folder or started/stopped.

## What v1 is

`dice.asi` hooks `C_Dice::OnDiceStateChanged` in WHGame 1.5.6 (prologue checked first), reads the live Farkle state
(two players, six dice each: state, value, six face probabilities, joker, view x/y, banked and turn points, target),
solves the matchup on a worker thread with the shared `tools/farkle` solver, and sends the result to the Lua mod over the
game's `System.LogAlways` channel. The mod marks the advised dice with the game's own selector (`AddDiceSelector`, ids
100+) and writes the panel with the existing HUD info text (`ShowInfoText` / `HideInfoText`), so there is no `hud.gfx`,
no `UIElements/HUD.xml` and no drawn art in the v1 pak.

Panel, after a throw: `Set aside: 1 1 1 = 1000`, `throw again x %` / `bank y %`, `Finish this turn x %` / `Hot dice y %`.
Panel, once the player sets dice aside: `Your pick: ... = score`, `Throw N dice: bust x %`, the win chance of throwing
vs. banking, finish/hot, and `Last choice cost x % win` when the committed pick was worse than the advice (a bank or a
bust costs too, and the cost is carried to the next throw's panel). `dice_advisor_detail 1` adds the opponent's finish
chance, the next throw's average points, and the take-fewer comparison.

## Files changed

New:

- `native/src/dice_main.cpp`: plugin init, state hook, guarded live read, poll on the frame tick, worker thread,
   `SolveCached` cache, mistake cost, settings/log, console commands.
- `native/src/dice_logic.h`: snapshot model, die/selection conversion, `AdviseSnapshot` / `SelectSnapshot` / `Present`
   (advice, the player's own pick, finish, hot, take-fewer, opponent, average points, mistake).
- `tools/farkle/advisor.h`: cache identity (`DieKeyName` = probabilities + joker), `Selection` + `EvaluateSelection`,
   `HotDice`, `ExpectedThrowPoints`, `TurnTables`/`TurnTablesFor`, `FinishFromTurnStart`/`FinishFromSelection`,
   `MistakeCost`.
- `tools/farkle/farkle_win.cpp`: offline queries `--faces`, `--keep`, `--pos`, `--finish`, `--opp-finish`, `--hot N`,
   `--expected N`, and the default matchup query.
- `tools/farkle/test_advisor.cpp`, `tools/farkle/test.sh`: solver tests and CLI checks for the new numbers.
- `dice/` (whole mod): `mod.manifest`, `build.sh`, `src/Scripts/Mods/dice.lua`,
   `src/Scripts/DiceAdvisor/dice.lua`, `tests/test_logic.cpp`, `tests/test.sh`, `README.md`, `KIMI_REPORT.md`,
   `tools/install_on_close.sh`, `tools/hud/build.sh` (v2 research, not in the v1 pak).

Modified:

- `native/CMakeLists.txt`: the `dice` shared library (`.asi`), including `../tools/farkle`.
- `native/build.ps1`: creates the output folder, builds and requires `dice.asi`, copies it beside `deagle_native.asi`,
  and copies `../tools/farkle` next to the Windows work folder (the dice sources include `advisor.h` from there).

## Build

Build root `E:\Tools\deagle-work\native-build-kimi`, command:

```
W=$(wslpath -w native)
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "(Get-Process -Id \$PID).PriorityClass='Idle'; \
  & '$W\build.ps1' '$W' '$(wslpath -w native/dist)\deagle_native.asi' 'E:\Tools\deagle-work\native-build-kimi'"
```

Final line:

```
BUILT 540160 + fastload.asi 1610752 + inventory.asi 275456 + dice.asi 442880
```

`native/dist/` holds `deagle_native.asi`, `dice.asi`, `fastload.asi`, `inventory.asi`. `bash dice/build.sh` then wrote
`dice/dist/dice.asi` (442880 bytes), `dice/dist/dice/data/dice.pak` (9347 bytes, the two Lua files), and
`dice/dist/dice/mod.manifest` loose beside `data/`.

Errors met and fixed on the way (three build runs, all in the dice code or its build wiring):

1. `MinHook.h` not found and `tools/farkle` not found: this worktree's `native/third_party/minhook` was an empty
   submodule directory, and `robocopy` only copies `native/`. The MinHook checkout was copied from
   `../kcd2-deagle/native/third_party/minhook`, and `build.ps1` now copies `tools/farkle` to the work folder's parent
   so the `../tools/farkle` include path resolves there.
2. `std::min` inside `farkle_core.h` broke under `windows.h`'s `min`/`max` macros (`C2589`): `dice_main.cpp` defines
   `NOMINMAX` before `windows.h`, and `dice_logic.h` undefines both macros before including `advisor.h`.
3. `StateName()` returned `std::string` inside `__try` (`C2712`, object unwinding): the guarded read now fills a caller's
   `char[96]` in `ReadStateName()` and the string is built outside the `__try`.

No warnings from the `dice` target. `deagle_native`, `fastload` and `inventory` built unchanged.

## Tests (Linux, run once after the build)

`bash tools/farkle/test.sh`:

```
[ok] Scorer::Best: 24 hand-derived cases
[ok] obvious bank
[ok] obvious keep
[ok] bust advice
[ok] hot dice chance (1 die 1/3, 2 dice 4/36, 3 dice 12/216)
[ok] average points of the next throw (one die 25)
[ok] finish this turn / opponent next turn (50 -> 96.91 %)
[ok] committed-choice cost
[ok] player's own pick: score, dice left and bust chance
[ok] live dice cache identity (probabilities + joker)
[ok] cache reload gives identical W without solving
[ok] fair-game symmetry
All advisor tests passed.
[ok] farkle_win queries (advice, pick, finish, hot, expected)
```

`bash dice/tests/test.sh`:

```
[ok] fresh throw -> advice numbers and selector IDs
[ok] player's own pick: score, dice left, bust, win and finish numbers
[ok] bank recommendation finishes the turn
[ok] bust data and no selectors
[ok] live joker mapping
[ok] take-fewer fields are consistent with the advice
[ok] live die names carry probabilities (and joker)
[ok] live matchup cache identity
All dice plugin logic tests passed.
```

One test fix was needed: `farkle_win` printed `J` for face 0 on every die, so the ordinary 1 1 1 was shown as
`J J J`. `FacesText` now takes the dice models and prints `J` only for a joker die's face 0.

## Verified here vs. needs a first in-game run

Verified outside the game:

- Farkle scoring, the solver's win chances, the target-50 finish (96.91 %), hot dice (1/3, 4/36, 12/216), the one-die
  expected 25, take-fewer, and the mistake cost.
- The plugin-side mapping from a snapshot to every number the panel prints, including the player's own pick, selector
  ids 100+, bust, bank, joker faces, and the cache key (probabilities + joker, not the live dice object).
- `dice.asi` compiles and links against the same headers the tests use; the pak packs the two Lua files.

Needs the first in-game run (cannot be checked without the game):

- The state hook prologue and the frame-tick vtable patch on the user's exact WHGame 1.5.6 build.
- The inferred live offsets: die state 0/1/2, value 6 = joker, the player/side layout, and the die view coordinates
  used for the selectors.
- `ShowInfoText`'s actual look and placement, the six-line height, and that `HideInfoText` does not disturb game texts
  at the moment the panel is refreshed (it flushes all info texts; accepted for v1).
- `AddDiceSelector` / `RemoveDiceSelector` on the ids 100+ next to the game's own selection, and that the game's input
  ignores those markers.
- State names containing `Holding` / `Scoring` / `Passing`, and that the panel hides at the right moments (throw,
  opponent, bank transition, minigame end).

## What the user does

1. Close KCD2, copy `dice/dist/dice` to `Mods/dice` and `dice/dist/dice.asi` to `Bin/Win64MasterMasterSteamPGO/`
   (`bash dice/build.sh --install` does both; `dice/tools/install_on_close.sh` waits for the game to exit first).
2. Launch, sit at a dice table, throw. The panel appears after the throw settles; "Calculating..." shows while a new
   dice matchup is solved (one solve per matchup, then `dice_cache/`).
3. Click dice: the panel follows the player's own selection and switches to the `Your pick` form.
4. Commands: `dice_advisor 0|1`, `dice_advisor_detail 0|1`, `dice_advisor_log 0|1` (saved in `dice_settings.txt`).
   `dice.log` next to `dice.asi` has every state, die and emitted advice; it is the file to send back if a number or a
   marker looks wrong on the first run.
