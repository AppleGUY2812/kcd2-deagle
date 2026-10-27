# Spec: sort the inventory list by a stat (charisma, armour, ...)

For an implementing agent (opencode / DeepSeek). Written 2026-10-26 by Claude for the user's Inventory Tools mod.
Do Phase 0 first and STOP: write its report, change no code. A reviewer reads the report and says "go" for Phase 1.

## Rules (the user's, non-negotiable)
- Never launch, close or touch the game (Kingdom Come: Deliverance II). Never install. Reading the game's pak files is fine.
- Prefix heavy commands with `nice -n 19`. Use `rg` (not grep -r), `fd` (not find).
- Edit only `inventory/` (and `tests/run_all.sh` if you add a test there). Commit nothing; the reviewer commits.
- Never hand-edit `inventory/src/Libs/UI/*.gfx` or `UIElements/*.xml`: change the sources in `inventory/tools/weightmap/`
  and rebuild with `nice -n 19 bash inventory/tools/weightmap/build.sh`.
- Scalability: no per-item or per-armour code. One generated table from the game's own data, one code path for every stat.
- Keep `bash tests/run_all.sh` passing (it currently passes; `sim_split_test` may fail in a worktree only because its
  source isn't committed: ignore that one, report it).
- Write `inventory/docs/SORT_BY_STAT_REPORT.md` at the end of each phase: what you did, every command you ran to check it
  and its output, what you are unsure about, what you did not do.

## What the user wants
1. A way to sort the inventory list (and the chest / horse / shop lists, which use the same movies) by one stat:
   charisma, the armour values, and the other numbers KCD2 shows for clothing and weapons.
2. It must respect the game's own grouping. KCD2's list has a vanilla button that groups the list under headings
   (armour split into head / torso / legs ... by item category). In the movie that switch is
   `InventoryData.m_HeadingVisibility`. When grouping is on, items stay under their heading and are sorted by the stat
   inside each group. When it is off, the whole list is sorted by the stat.
3. Later (not this spec): "equip best for a stat". Design the stat data so that feature can reuse it.

## How the list sorts today (read these first)
- `inventory/tools/weightmap/total.as`: our total-weight column. It adds two new sort ids (16 desc / 17 asc) without the
  game noticing: wraps `InventoryData.prototype.SetSort`, `SortNext` / `SortPrev` (the sort cycling order `TW_ORDER`),
  `m_SortFunc`, `InvEnum.GetSortFrame` and `_root.fc_getCurrentSort` (reports a vanilla sort id so the saved user config
  stays valid). Its `m_SortFunc` builds the key list exactly like the game: when `m_HeadingVisibility` is on it sorts by
  `Category` (or `IsHorseEquip` / `IsPlayerInventoryId` for the horse / basket filters), then `IsHeading` (heading first),
  then the stores' `IsQuestItem`, then its own key, then `HashName`, `ClassId`. **Copy this pattern; it already
  respects the grouping.** Only the value key changes.
- `inventory/tools/weightmap/patch.py`: how the .as files are spliced into the decompiled movie (`DoAction.as` of
  frame_1). `build.sh` decompiles the game's own movies from `IPL_GameData.pak`, so you can read the game's list code:
  run the decompile step from build.sh into a scratch dir and read `DoAction.as` (InventoryData, InvEnum, fc_setItems,
  the row classes, the sort button wiring).
- The row has six columns: equip (outfit), amount, **stat**, health, weight, price (`mc.stat` in total.as). The game's
  "stat" column already shows one number per item (likely the main defence / attack). Find out what it is.
- The item entries (`it` / `e` in the .as files) carry at least `Id`, `ClassId`, `Name`, `HashName`, `Category`,
  `Weight`, `StackAmount`, `Amount`, `Stolen`, `OutfitPresence`, `IsHeading`, `IconId`. `fc_setItems` fills them from
  the game ("setItems: N fields" in kcd.log via fastmove.lua's OnStat). The full field list is in the decompiled code.

## Phase 0: research (report only, no code)
Answer each with evidence (file + line, or the command and its output):
1. **Field list.** Every field `fc_setItems` puts on an item entry, with what it means. Is any of them a stat value
   (charisma, defence, attack ...)? What does the vanilla "stat" column show, and from which field?
2. **Vanilla sorts.** List `InvEnum.SORT_*` ids and what each sorts by (does a vanilla "stat" sort exist? ids 0-15).
3. **Grouping.** Confirm `m_HeadingVisibility` is the vanilla group button, how it is toggled, and which `Category`
   values the armour groups use (head / torso / arms / legs / ...). Cite the code.
4. **Stat source, option A (build time, preferred if the list has no stat fields).** The game's item tables in
   `IPL_GameData.pak` (look under `Libs/Tables/item/`: armour, clothing, weapons, helmets ...). For each table: the file,
   the row key, and which column matches the list entry's `ClassId` (format! GUID string?). List the stat columns (e.g.
   charisma, stab / slash / blunt defence, noise, visibility, conspicuousness, attack values). Count the rows. Note any
   inheritance (rows that reference a base row) and how values resolve.
5. **Stat source, option B (runtime).** Can Lua get an item's stats from its id or class (KCD2's `ItemManager`,
   `player.inventory`, item tables)? Search the game's own scripts in `Scripts.pak` / `IPL_GameData.pak` for item-stat
   calls. Only report what you find in code; do not guess APIs.
6. **Condition.** Do armour values in game scale with the item's condition (`Health`)? Find the formula in the tables
   or scripts if it exists; if not found, say so.
7. **Stat names.** The game's own localisation keys for each stat name (so the button shows "Charisma" in the player's
   language), from `Localization/*.pak`.
8. **Other mods' items.** Will items added by other mods have rows in option A's table? (They patch the same tables;
   say how a build-time table would miss them.)
9. Recommend A or B (or the list fields from 1) with the reasons, and the size of a generated table.

## Phase 1: implement (only after the reviewer says go)
Design (adjust only where Phase 0 proved something different; say so in the report):
1. **Stat data.** One generated file, `inventory/tools/weightmap/stats.as` (if option A), written by a new
   `inventory/tools/weightmap/gen_stats.py` that reads the game's tables from the pak at build time (build.sh runs it
   before patch.py; patch.py includes stats.as like the other .as files). Shape: `var ST_NAMES = [...]` (stat keys, in
   menu order), `var ST_LOC = {...}` (stat key -> localisation key), `var ST = {}` with `ST["<ClassId>"] = [v0, v1, ...]`
   (numbers, one per stat key, `undefined` where the item has none). Keep it compact (arrays, not objects per row).
   Same table later feeds "equip best", so put no sort logic in it. If option B wins, the same shape comes from Lua at
   open time instead (UIAction.CallFunction with one string), and gen_stats.py is not needed.
2. **Choosing the stat.** One new button in the sort row next to the existing ones (copy how total.as clones the
   weight button and its icon; any existing sort icon is fine for v1). Left click = sort by the current stat
   (desc, then asc on the next click, like the other columns). Right click (or the shoulder button if a controller path
   exists in the code) = next stat in `ST_NAMES`. Its tooltip names the stat ("Sort by Charisma"), translated through
   the stat's localisation key. Remember the chosen stat while the game runs (a `_root` variable is enough; kept
   across restarts only if trivially possible through the plugin's `inv_set`, else skip and say so).
3. **Sorting.** New sort ids 18 (desc) / 19 (asc), added to `TW_ORDER`-style cycling, `GetSortFrame`, and
   `fc_getCurrentSort` (report a vanilla id, as total.as does). In `m_SortFunc`: the same key prefix as total.as (the
   grouping keys when `m_HeadingVisibility` is on; quest items first in stores), then the stat value, then `HashName`,
   `ClassId`. Items with no value for the stat (food, books, a helmet for "attack") go **last inside their group in
   both directions**: use a separate numeric key `StatHas` (1 / 0, DESCENDING) before the value. Headings get no value.
   Prefer wrapping total.as's wrappers (or one shared helper both use) over copying it; no second copy of the key logic.
4. **Show the value.** While sort-by-stat is active, the row's "stat" column shows the chosen stat's value for that
   item (empty when it has none), in the same text style; when another sort is picked the column goes back to the
   game's own value. Do it in the row-fill wrapper total.as already has.
5. **Weight map.** No change (it stays sized by weight).

## Tests (Phase 1)
- `inventory/tools/weightmap/test_sort.js` (node, like test_layout.js): load the sort helper with mock entries
  (2 groups with headings, items with and without the stat, equal values) and check: grouping on = each group
  contiguous with its heading first, sorted by the stat inside; grouping off = one sorted list; missing values last in
  both directions; ties broken by HashName; asc is the reverse of desc for items that have the value.
- If option A: `gen_stats.py --check` prints the row count, 5 sample rows with their names, and fails if a table has
  no rows or a ClassId repeats with different values.
- `nice -n 19 bash inventory/tools/weightmap/build.sh` must print "built ..." for all three movies, and its
  post-build check must still find `function WM_Build`; add a check for your sort function name.
- `luajit inventory/tests/run_inventory.lua`, `node inventory/tools/weightmap/test_layout.js`, `bash tests/run_all.sh`.
- Report the size of each rebuilt .gfx before / after.

## Done means
Phase 1 report lists: what the user will see (button, tooltip, column), how to try it in game in 4 steps, every check
above with its output, and the open risks (other mods' items, condition scaling, controller input).
