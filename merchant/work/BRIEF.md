# Brief: can a mod make merchants resell what you sell them? (feasibility research, read-only)

You are in the git worktree `~/kcd2-deagle-builds/wt-merchant` (branch `merchant-research`). Edit only the two outputs below.

## The idea (the user's, decided)
In Kingdom Come: Deliverance II the player sells items to merchants; a merchant's purse (coins) drains and the items just sit in
their stock. We want a NEW mod ("Merchant Resale") where merchants, over game time, resell what the player sold them to other
customers: the item leaves the merchant's stock and coins come back into their purse. That keeps merchants solvent, so selling to
them stays worthwhile instead of just robbing their chests.
Design leaning (decided, follow it unless the research proves it impossible): settle LAZILY when the player opens that merchant's
shop (elapsed game time since the player's last sale or visit -> some of the player-sold items are removed, coins added), so no
per-frame ticks and no need for the merchant to be loaded while the player is away.

## Deliverable
`docs/merchant_resale_research.md` (<= 200 lines) and `work/REPORT.md` (<= 40 lines: one table of the 7 questions: answer, V/I/U, source).
Mark every statement V (verified: file+line, table row or function address) / I (inferred) / U (unknown). Never write a name,
signature or offset you did not read.

## Read first
1. `inventory/README.md` and `inventory/src/Scripts/Inventory/fastmove.lua` (how our existing Lua mod talks to a native plugin, and how it handles shop screens: shops, baskets, G fast transfer).
2. `dice/src/Scripts/DiceAdvisor/dice.lua` top 60 lines (the Lua <-> plugin channel: Lua logs "[TAG] cmd", the plugin hooks the log and answers with console commands).
3. `native/src/inventory_main.cpp` (a small plugin that hooks WHGame.dll functions with MinHook).

## Data you can use (read-only)
- Game Lua (290 files): `unzip -p "/mnt/d/steam_bruh/steamapps/common/KingdomComeDeliverance2/Data/Scripts.pak" '*.lua' | rg -n -i "shops\.|shop:|soul:|inventory:" ` (use `unzip -l` to list files, `unzip -p ... <file>` to read one).
  Known: a C++-exposed `Shops` table (`Shops.OpenInventoryForItem`, `Shops.IsLinkedWithShop`, `Shops.OnPropertyChange`), an entity `Shop` (iShopId, sShopName), `UIShopLinkNode`.
- Game tables: `unzip -p ".../Data/Tables.pak" Libs/Tables/shop/shop.xml` (ShopData: shop_id, shop_name, restock_period in days, inventory_preset, ShopItemFilter buy/sell multipliers) and
  `Libs/Tables/item/InventoryPreset__shops.xml` (presets include `inventory_shop_money_600`, `_2500`, `_350`: the purse looks like a money item inside the shop's inventory).
- Quests/XML that touch shops: `unzip -l .../Scripts.pak | rg -i shop` (e.g. `Quests/Final/.../restock_pri_nakupu/...`, `AI/crime/createtheftsfromshop.xml`).
- Decompiled game code on the ProLiant: `ssh -i /home/johnpork/.ssh/proliant_key -o ConnectTimeout=10 <user>@<server> '~/re/scripts/qk <cmd> <addr>'`
  with cmds `dec <addr>`, `callers`, `callees`, `strin <addr>` (strings used by a function), `near <addr>`, `data <addr>`, `sym`. One qk command per ssh call, nothing else there.
  Names found so far: `~/re/index/KCD2/names.tsv` on that host (`grep -i shop`). Addresses are 0x180000000 + RVA. Start by finding the C++ registration of the Lua `Shops` table (search its function-name strings with `qk` / names.tsv).

## The 7 questions (each answer: a few lines + sources)
1. SHOP API: every function of the Lua `Shops` namespace (name, args, return), the `Shop` entity, and how a merchant NPC maps to its shop (shop id / entity / soul).
2. INVENTORY + MONEY: how to list, add, remove items in a shop's inventory from Lua (`inventory:GetInventoryTable`, `AddItem`, `DeleteItem`, `ItemManager.*` ... which exist) and how the purse is stored (money item class? a stat?) and read / changed.
3. SALE EVENT: where does a player -> merchant sale complete (UI basket confirm)? Function address + what it receives (item, count, price, shop). Is there a Lua-visible callback, or is a native hook needed? Also: are items the player sold marked (e.g. `IsFromShop`, a flag, a separate list) or mixed with the stock?
4. SHOP OPEN: the best hook point to run code just BEFORE the shop's inventory is shown (Lua event, UI call such as the one our inventory mod already sees, or a native function).
5. RESTOCK: how `restock_period` works (what is reset: items, money, both; checked when; by game time?), and whether restocking would wipe our changes or conflict.
6. TIME: the game-time API from Lua (day count / world time; names) and whether it keeps counting while the shop is not loaded.
7. SAVES: how a mod can keep per-save state (the list of player-sold items per shop + timestamps): game variables / persistent script tables / a sidecar file written by the plugin keyed by save name. What do the existing mods (inventory, dice) do? What survives a save/load in Lua?

## Rules
- Read-only: do not launch or touch the game, do not install or download anything, do not edit anything except the two outputs.
- If something can't be found, say where you looked and why it failed. Never claim a check passed without the evidence.
- End the document with a section "Recommended design": which hook for the sale, which for the shop open, how to remove items / add coins, where to store state, and the 3 biggest risks (crash risks first: e.g. changing an inventory while its UI is open).
- Commit only your own files on this branch (`git add <paths>`; `git commit`), no push, no merge, trailer `Co-Authored-By: Gemini (agy) <noreply@google.com>`.

# ROUND 2: build phase 1 (read-only hooks) + the resale logic with tests

(Round 1 was done by agy/Gemini; its research is `docs/merchant_resale_research.md`. Round 2 is yours, GPT-6.1 Sol.)
Ghidra access (read-only, the decompile of WHGame.dll 1.5.6, functions mostly unnamed FUN_<addr>, RTTI class names named):
`ssh -i ~/.ssh/proliant_key -o ConnectTimeout=10 <user>@<server> '~/re/scripts/qk <cmd> <arg>'` with cmd one of
dec (decompile a function), dis, refs, data, str, strref, sym, rtti, callees, fn. Example: `qk dec 0x1808d5150`,
`qk rtti 0x183a81ad8`, `qk str OnSaveAI`. Never write on the ProLiant except /tmp; ~/re/index/KCD2/names.tsv is a
list of already-named functions (read it: `grep -i shop ~/re/index/KCD2/names.tsv`).
Game Lua and tables: `unzip -p /mnt/d/steam_bruh/steamapps/common/KingdomComeDeliverance2/Data/Scripts.pak <file>`
and `.../Data/Tables.pak` (read-only).

Your research was good: Claude checked both vtable slots in Ghidra (correct). Read `docs/merchant_resale_design.md`
first: it is decided, follow it. Same worktree, same branch. Install or download nothing; never touch the game folder
or a running game. Build of the .asi is Claude's job: write the code, you cannot compile Windows plugins here.

## Step A: native field research (read-only, Ghidra on the ProLiant via the same ssh + `~/re/scripts/qk`)
From `C_Item*` (basket element) find, with addresses: item class GUID, item display/class name, base price (the
`price` field of the item class), and the category / item type the shop filters use (`armor.*`, `weapon.*`, `food.*`).
From the merchant pointer at transaction+0x10: entity id, name, world position. From `C_UIShop::Open`'s args: which
merchant/shop it opens and how to read the purse (Shops.GetShopMoney's native body shows the path).
Also: is `Player:OnSaveAI` called by KCD2's save system (search the decompile for the string "OnSaveAI" and its caller)?
Check: write each answer with V/I/U + address into `docs/merchant_resale_native.md` (<= 120 lines).

## Step B: `native/src/merchant_logic.h` (pure C++17, no windows.h, no game types)
The design's rules 1-4 as plain functions/structs: Ledger rows, Market (300 m clustering), Demand per market x
category with the 4-day half-life, ResaleEta(seeded spread from item id), Settle(now) -> {rows resold, coin earned},
the bonus/restock rule 4 with the 10x cap, Serialize/Deserialize to one string (for the save).
Test: `native/test/merchant_logic_test.cpp`, built and run locally with
`nice -n 19 g++ -std=c++17 -O2 -Wall -I native/src native/test/merchant_logic_test.cpp -o /tmp/mlt && /tmp/mlt`.
Cases with printed numbers: one plate in fresh Kuttenberg resells in 2.1-3.9 days; 30 plates in one day -> armor
demand at the 0.15 floor and the last plate's ETA >= 15 days; demand back above 0.9 after 14 idle days; buyback removes
a row with no coin; restock reset re-adds the bonus and respects the cap; serialize round trip equal; same seed -> same ETAs.

## Step C: `native/src/merchant_main.cpp` phase 1 = read-only logging
Model it on `native/src/dice_main.cpp` (MinHook setup, log file, settings file). Hook the two functions from the
design; on each call log one line to `merchant.log` next to the plugin: checkout -> merchant name/id, each basket
item (class name, guid, count, base price), final price, direction (sell/buy); shop open -> merchant, purse.
Change NOTHING in game state. Guard every pointer read with the same SEH pattern dice_main.cpp uses. Add a
CMakeLists.txt target `merchant` next to `dice` (same flags). Mod folder `merchant/` with README.md (what it does,
phase 1 = logging only, commands) and `mod.manifest` modeled on `dice/mod.manifest`.

## Rules
- Edit only: docs/merchant_resale_native.md, native/src/merchant_logic.h, native/src/merchant_main.cpp,
  native/test/merchant_logic_test.cpp, native/CMakeLists.txt (one target), merchant/**, work/REPORT2.md.
- WSL has ~5.9 GB RAM: never unpack the whole decompile locally; query the ProLiant.
- Never run powershell.exe, cmd.exe or any Windows program (the user is playing the game on that PC).
- If a step fails, say where and why; never claim a check passed without its printed numbers.
- Commit only your paths on this branch (git add <paths>; git commit), no push, no merge. Trailer:
  `Co-Authored-By: GPT-6.1 Sol (opencode) <noreply@openai.com>`.
- `work/REPORT2.md` <= 40 lines: test output numbers, each native answer with V/I/U, commit hash, what you skipped.

# ROUND 3: phase 2, the real resale (decided by Claude + the user; follow it)

## Phase 1 result (in game 2026-10-30, `merchant/work/phase1_ingame.log`)
All hooks fired. Shop open (C_Shop 0x180c406d8, before original): shop_dbid 520, keeper WUID, purse_deci 3780.
Checkout: one basket mixed buy (8 repair kits) + sell (training club); direction per item right; purse at the next open
= 3780 + 4527 = 8307, so purse reads and deci units are right. Buyback of the club logged as direction=buy.
Still U: merchant name, entity id, position, item category, commit_success.

## Goal
Merchants resell what the player sold them (docs/merchant_resale_design.md rules 1-6, numbers in merchant_logic.h),
settled when the player opens that merchant's shop, before the UI fills.

## Step A: research (Ghidra, read-only) -> append to docs/merchant_resale_native.md, [V]/[I]/[U] per claim
1. Commit: how to know a checkout committed (not cancelled): return value of 0x1808d5150, or basket/purse after the
   original returns. Record only committed rows.
2. Game time in days (world time, the one Calendar.GetWorldTime uses), readable from native.
3. Merchant: keeper WUID -> entity (id, name, world position). Position decides the market (merchants within 300 m).
4. Item category: from the item class (its table type: MeleeWeapon/MissileWeapon -> weapon, Armor -> armor,
   Food -> food, Herb/Alchemy/Potion -> alchemy, else misc). Find where the class row/type is reachable from the item.
5. Add coins to a shop purse natively, before the UI opens: the game's own path (Shops.DoMoneyTransaction 0x182efe260
   and what it calls, or the money item create). Exact function, args, units, and that it is safe before 0x180c406d8.
6. Remove one resold item (by the instance token logged in phase 1) from the shop inventory natively, the game's own
   remove function; what happens if the item is gone already (bought back, vanilla destock).
7. Save/load: a hook point that gives the save's identity (file name or id) on save and on load, so state is kept
   per save in a sidecar file `merchant_state/<save id>.txt` next to the plugin (merchant_logic.h already serialises).
Any item you cannot verify: say so, then pick the safest fallback and mark it [I].

## Step B: implement in native/src/merchant_main.cpp (+ merchant_logic.h if needed)
- Committed sells -> ledger rows + demand drop; buyback (direction=buy of a ledger instance) removes the row.
- Shop open, before the original: settle that merchant (rows past their ETA: remove the item, add coins at base value,
  purse cap 10x preset; weekly-reset bonus rule 4), log one line per settled row and the purse before/after.
- `merchant_resale 1` default ON, `0` = phase-1 behaviour (logging only); setting saved in merchant_settings.txt.
- `merchant_status`: last opened merchant: purse, bonus, ledger rows with ETA in days, market demand per category.
- Every game write behind SEH and a per-call sanity check; one failure = log it, stop writing for the session.
- Hooks stay gated on the existing fingerprint check.

## Step C: checks
- Extend native/test/merchant_logic_test.cpp for anything new in merchant_logic.h; compile and run it with g++
  (as round 2 did) and paste the output. Claude builds the plugin on Windows; you cannot.

## Rules (unchanged from round 2, plus)
- Never run powershell.exe, cmd.exe or any Windows program; never touch the game folder; install or download nothing.
- Do not edit opencode.json. nice -n 19 for anything heavy. WSL has ~5.9 GB RAM: no whole-DLL scans locally,
  grep the decompile on the ProLiant.
- Commit only your own paths on branch merchant-research, no push/merge. Trailer:
  `Co-Authored-By: GPT-6.1 Sol (opencode) <noreply@openai.com>`.
- `merchant/work/REPORT3.md` (<= 60 lines): step A answers with addresses, what changed, test output, commit hashes,
  what is unverified and what the user must test in game.

## ROUND 3 resume note (Claude, 16:40)
The allow-list is fixed: `ssh -i /home/johnpork/.ssh/proliant_key ...` and `~/...` forms both work now.
Tests: compile to `./build_test/merchant_logic_test` (`mkdir -p build_test` first) and run it as its OWN command
(`nice -n 19 ./build_test/merchant_logic_test`), not chained with `&&`, and never from /tmp. Add build_test/ to
nothing (it is ignored by you: do not commit it). Now do Step A, B and C for real; replace REPORT3.md.
