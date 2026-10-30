# Dice Advisor for Kingdom Come: Deliverance II

Dice Advisor is a read-only helper for Henry's Farkle games. After each throw it puts a soft glow on the dice worth setting aside and shows the recommended move in a panel beside the game's own dice score panel: **THROW AGAIN** or **BANK**, with the risk of throwing on and the reason. It follows what the player actually picks, so the numbers update as dice are selected. No die, score, AI, input or rule is changed.

The advice is a fourth column block in the HUD movie (`Libs/UI/hud.gfx` + `UIElements/HUD.xml`), in the score panel's own style: gold bold headings, cream body text and the same thin divider, no box or fill. It is positioned relative to the game's score container, so it follows it at every resolution. `dice_advisor_ui 0` switches back to the old centre info text (`ShowInfoText`) for the session; the panel is the default.

## What the panel shows

On the result of a throw:

```
Advice
[1] [6] [6] [J]   then BANK
Throwing on: 11 % chance to lose 700 points
Why: keeps your Weighted in hand (+1.6 % win)
```

Row 1 draws the dice to set aside as small die faces (a joker is `J`), then the action in bright gold; when the player's own pick already equals the advice it says `Now BANK` / `Now THROW AGAIN`. Row 2 prices the risk of throwing on. Row 3 is the one-line reason: the die kept in hand, or taking all the scoring dice, or the bank/throw difference. The action and advice update as the player picks dice; a worse pick adds the same `Why` reasoning for the recommended set.

`dice_advisor_detail 1` adds a fourth row with the bank and throw win chances, the opponent's chance to finish on their next turn and, if the advice deliberately keeps fewer dice than another scoring set, the comparison between the two sets.

## Install and use

Close KCD2. Copy `dist/dice` to `Mods/dice` and `dist/dice.asi` to `Bin/Win64MasterMasterSteamPGO/`. On WSL, `bash dice/build.sh --install` does both after checking the game is closed; `bash dice/tools/install_on_close.sh` waits for the game to exit first (it never touches a running game). Launch normally, sit at a dice table and throw.

**Calculating...** appears while an uncached dice matchup is solved on the worker thread. After that the same matchup is answered from `dice_cache/`, and picks inside a throw are answered immediately. The panel is hidden during throws, on the opponent's turn, when the minigame ends and while the advisor is off.

This version requires WHGame 1.5.6 and exactly six dice per player; anything else turns the advisor off (with a log line) instead of guessing. Detailed live-state logging is on by default for the first version so the inferred offsets can be checked on the first run.

## Commands

- `dice_advisor 0|1`: disable/enable advice (saved).
- `dice_advisor_log 0|1`: detailed state logging (saved; default on).
- `dice_advisor_detail 0|1`: the extra take-fewer / opponent / average-points line (saved; default off).
- `dice_advisor_ui 0|1`: the HUD panel (default) or the old centre info text (session only, not saved).

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

Then `bash dice/build.sh` packs the mod. `dice/tools/hud/build.sh` rebuilds the game's `hud.gfx` (advice panel, die faces, glow markers) and `UIElements/HUD.xml` into `src/Libs/UI/`, and `bash dice/build.sh` packs them plus the Lua into `data/dice.pak`. The game's pak is only read, never written.

## How it reads the game

`dice.asi` checks the exact 1.5.6 prologue before hooking `C_Dice::OnDiceStateChanged` at RVA `0x2e907d0`; a mismatch leaves it off. On a state change it reads the current player, banked and turn points, the target, and each die's state, value, six normalised face probabilities, joker marker and view position. All live reads and virtual calls are guarded by Windows structured exception handling; a failed read logs `dice: read failed at ...`, hides the panel and turns the advisor off for that game. The frame tick only drains the console queue and, every 12 frames while advice is up, re-reads the live selection so the player's clicks are answered.

Die state is read as **0 = in hand after a throw, 1 = selected this throw, 2 = set aside from an earlier throw** (inferred; the first-run log confirms it). The current selection is `state 1`; the solver compares it with the advice, which is also why `Score(selected)` and the pick's bust chance are the player's own numbers, not the advisor's recommendation.

The worker thread builds each player's model from the live weights, solves the matchup once (`SolveCached`, in memory and in `dice_cache/`), and caches the solution under a hash of both players' probabilities, joker flags and target. Stale worker results are discarded by generation. `dice.log` records every state, die and emitted advice.

The solver is exact for the table's Farkle rules and optimal future play. It does **not** model unresolved actor ThrowFixing/BustFixing modifiers, badges that change dice, physical animation timing, or extra dice. A bad or unsupported state disables advice rather than guessing.

## Files

- `src/Scripts/DiceAdvisor/dice.lua`: commands, the panel rows (faces, action, why) and the die names.
- `tools/hud/`: `advice.as`, `patch.py`, `patch_xml.py`, `build.sh`: the HUD panel, the glow markers and the movie build.
- `src/Scripts/Mods/dice.lua`: loader.
- `build.sh`, `mod.manifest`, `tools/install_on_close.sh`: packaging and install.
- `tests/test_logic.cpp`: plugin-side tests, run on Linux.
- `../native/src/dice_main.cpp`, `dice_logic.h`: the plugin; the solver lives in `../tools/farkle/`.

## Dice picker (2026-10-28)
On the dice-picking screen (the minigame's Null state before a game) the panel shows the best 6 of your dice:
one row per die kind (`Favourable x3`), then a `Why` row naming the surest die and the points per turn against plain dice. Lua lists the
die items in your inventory (`dice_items.lua`, generated by `dice/tools/gen_dice_items.py` from the game's item.xml)
and sends them to the plugin (`own`); the plugin ranks them with `tools/farkle/bestset.h` (local search by
E[min(turn points, 4000)], about 10 s for 14 kinds, lowest thread priority) and answers `dice_best`. The collection is
ranked once after a load, so the picker usually shows the answer at once. Six ordinary dice are assumed always available.
