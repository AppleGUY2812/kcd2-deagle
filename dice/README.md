# Dice Advisor for Kingdom Come: Deliverance II

Dice Advisor is a read-only helper for Henry's Farkle games. After each throw it marks the dice worth setting aside with the game's own die selector and writes the recommended move in the game's own centre info-text style: **Throw again** or **Bank**, with the win chance of each. It follows what the player actually picks, so the numbers update as dice are selected. No die, score, AI, input or rule is changed.

Version 1 does not touch `hud.gfx` or `UIElements/HUD.xml`: the panel is the existing HUD info text (`ShowInfoText`), drawn with the game's `DefaultFont` and its ornament background. The dice markers are the same selectors the game itself uses when the player clicks a die.

## What the panel shows

On the result of a throw:

```
Set aside: 1 1 1 = 1000
throw again 62 %       bank 48 %
Finish this turn 31 %       Hot dice 2.3 %
```

When the player has set dice aside:

```
Your pick: 1 1 1 = 1000
Throw 3 dice: bust 27.8 %
throw again 64 %       bank 51 %
Finish this turn 33 %       Hot dice 5.6 %
```

The recommended action is brighter. If the player keeps a worse set than the advice, a `Last choice cost x % win` line appears (the banked/busted choice is priced too, and carried to the next throw's panel). After an unscorable roll the panel says the turn is lost and shows the chance left after the bust.

`dice_advisor_detail 1` adds the extra numbers on a separate line: the opponent's chance to finish on their next turn, the average points of the next throw, and, when the advice deliberately keeps fewer dice than another scoring set, the comparison between the two sets.

## Install and use

Close KCD2. Copy `dist/dice` to `Mods/dice` and `dist/dice.asi` to `Bin/Win64MasterMasterSteamPGO/`. On WSL, `bash dice/build.sh --install` does both after checking the game is closed; `bash dice/tools/install_on_close.sh` waits for the game to exit first (it never touches a running game). Launch normally, sit at a dice table and throw.

**Calculating...** appears while an uncached dice matchup is solved on the worker thread. After that the same matchup is answered from `dice_cache/`, and picks inside a throw are answered immediately. The panel is hidden during throws, on the opponent's turn, when the minigame ends and while the advisor is off.

This version requires WHGame 1.5.6 and exactly six dice per player; anything else turns the advisor off (with a log line) instead of guessing. Detailed live-state logging is on by default for the first version so the inferred offsets can be checked on the first run.

## Commands

- `dice_advisor 0|1`: disable/enable advice (saved).
- `dice_advisor_log 0|1`: detailed state logging (saved; default on).
- `dice_advisor_detail 0|1`: the extra take-fewer / opponent / average-points line (saved; default off).

Settings live in `dice_settings.txt` next to `dice.asi`. The log is `dice.log`, also next to the plugin.

## Build

```
bash tools/farkle/test.sh     # solver, new step 6-7 numbers, offline CLI queries
bash dice/tests/test.sh       # plugin logic: advice -> panel numbers, pick, selectors, cache identity
bash dice/build.sh            # stored-ZIP data/dice.pak + dist/dice.asi
```

Build `dice.asi` with the repository's Windows `native/build.ps1` (MSVC x64); it writes `dice.asi`, `deagle_native.asi`, `fastload.asi` and `inventory.asi` beside the output path. Use the plugin build root `native-build-kimi`:

```
W=$(wslpath -w native)
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "(Get-Process -Id \$PID).PriorityClass='Idle'; & '$W\build.ps1' '$W' '$(wslpath -w native/dist)\deagle_native.asi' 'E:\Tools\deagle-work\native-build-kimi'"
```

Then `bash dice/build.sh` packs the mod. The v1 pak contains only Lua and `mod.manifest`. `dice/tools/hud/build.sh` is research for a future v2 movie panel; it writes into `dice/build/re/hud_v2` and is not part of the v1 build.

## How it reads the game

`dice.asi` checks the exact 1.5.6 prologue before hooking `C_Dice::OnDiceStateChanged` at RVA `0x2e907d0`; a mismatch leaves it off. On a state change it reads the current player, banked and turn points, the target, and each die's state, value, six normalised face probabilities, joker marker and view position. All live reads and virtual calls are guarded by Windows structured exception handling; a failed read logs `dice: read failed at ...`, hides the panel and turns the advisor off for that game. The frame tick only drains the console queue and, every 12 frames while advice is up, re-reads the live selection so the player's clicks are answered.

Die state is read as **0 = in hand after a throw, 1 = selected this throw, 2 = set aside from an earlier throw** (inferred; the first-run log confirms it). The current selection is `state 1`; the solver compares it with the advice, which is also why `Score(selected)` and the pick's bust chance are the player's own numbers, not the advisor's recommendation.

The worker thread builds each player's model from the live weights, solves the matchup once (`SolveCached`, in memory and in `dice_cache/`), and caches the solution under a hash of both players' probabilities, joker flags and target. Stale worker results are discarded by generation. `dice.log` records every state, die and emitted advice.

The solver is exact for the table's Farkle rules and optimal future play. It does **not** model unresolved actor ThrowFixing/BustFixing modifiers, badges that change dice, physical animation timing, or extra dice. A bad or unsupported state disables advice rather than guessing.

## Files

- `src/Scripts/DiceAdvisor/dice.lua`: commands, the panel text and the die selectors.
- `src/Scripts/Mods/dice.lua`: loader.
- `build.sh`, `mod.manifest`, `tools/install_on_close.sh`: packaging and install.
- `tests/test_logic.cpp`: plugin-side tests, run on Linux.
- `../native/src/dice_main.cpp`, `dice_logic.h`: the plugin; the solver lives in `../tools/farkle/`.

## Dice picker (2026-10-28)
On the dice-picking screen (the minigame's Null state before a game) the advice text shows the best 6 of your dice:
"Best dice to bring: 2x Lucky die, 1x Aranka's die, ... / About 1925 points per turn on average". Lua lists the
die items in your inventory (`dice_items.lua`, generated by `dice/tools/gen_dice_items.py` from the game's item.xml)
and sends them to the plugin (`own`); the plugin ranks them with `tools/farkle/bestset.h` (local search by
E[min(turn points, 4000)], about 10 s for 14 kinds, lowest thread priority) and answers `dice_best`. The collection is
ranked once after a load, so the picker usually shows the answer at once. Six ordinary dice are assumed always available.
