# Merchant Resale Feasibility Research

Feasibility research for the "Merchant Resale" mod for Kingdom Come: Deliverance II (WHGame 1.5.6).
Markings: [V] = verified (exact file+line, XML row, or function address), [I] = inferred from code structure, [U] = unknown / unverified.

## 1. Shop API, Entity, & Mapping
- [V] Lua `Shops` table registered in `wh::shopmodule::C_ScriptBindShop::ctor` (WHGame.dll `0x1813368d0`) and initialized in `FUN_180b639c8` (`0x180b639c8`).
- [V] Every exposed `Shops` function (name, args, return):
  1. `Shops.OpenInventory(entityId)` (`0x182efe8fc`): resolves shopkeeper/linked entity, calls `C_UIShop::Open` (`0x180a4a118`). Returns `nil`.
  2. `Shops.OpenInventoryForItem(itemId)` (`0x180c820a0`): stub returning `nil` immediately.
  3. `Shops.AcceptTransaction(shopkeeperId, finalPrice, playGoodbyeBark)` (`0x18152ea94`): commits transaction via `C_UIShop` vtable `+0x18`. Returns `nil`.
  4. `Shops.DoMoneyTransaction(shopkeeperId, amount, playGoodbyeBark)` (`0x182efe260`): transfers money directly in deciGroschen without full trade UI. Returns `nil`.
  5. `Shops.CancelTransaction(entityId)` (`0x182efe1d4`): cancels pending trade transaction. Returns `nil`.
  6. `Shops.GetShopMoney(entityId)` (`0x18152ebfc`): returns current shop purse in deciGroschen (`number`).
  7. `Shops.IsLinkedWithShop(entityId)` (`0x182efe664`): returns linked `Shop` entity ID or invalid WUID.
  8. `Shops.FindItemInShop(entityId)` (`0x182efe344`): checks if item entity belongs to a shop. Returns item ref or invalid.
  9. `Shops.FindShopInventoryForItem(entityId)` (`0x182efe3d0`): returns inventory entity ID containing item if in a shop.
  10. `Shops.IsShopOpened(linkedEntityId)` (`0x182efe6b0`): returns `boolean` (`true` if shop UI is open; checks `*(param+0x198) != 0`).
  11. `Shops.GetShopDBIdByLinkedEntityId(entityId)` (`0x182efe608`): returns integer `shop_id` from `shop.xml` for linked entity (e.g. chest/stash), or `-1`.
  12. `Shops.GetShopDBIdByKeeper(entityId)` (`0x18152ec44`): returns integer `shop_id` from `shop.xml` for shopkeeper NPC entity, or `-1`.
- [V] `Shop` entity defined in `Scripts/Entities/WH/Shop.lua:1-24`: properties `iShopId` (int), `sShopName` (string, takes priority), `bOwnerIsSpawned`.
- [V] Mapping: merchant NPC maps to shop via `Shops.GetShopDBIdByKeeper(npc.id)` (`Scripts/Systems/KCD1/Negotiation.lua:548`), matching `shop_id` in `Libs/Tables/shop/shop.xml`. Shop stashes link via `UIShopLinkNode.lua:14` and are queried with `Shops.IsLinkedWithShop(stash.id)` (`StashCorpse.lua:48`).

## 2. Inventory Manipulation & Money
- [V] `inventory:GetInventoryTable` DOES NOT exist in KCD2 Lua (0 occurrences in all 290 scripts in `Scripts.pak`).
- [V] Lua cannot list arbitrary contents of an inventory; it only operates on known items/classes:
  - `inv:FindItem(itemId)`: checks presence of specific item (`Scripts/Utils/ItemUtils.lua:97`).
  - `inv:GetCountOfClass(classGuid)`: returns integer item count of class (`ItemUtils.lua:118`).
  - `inv:AddItem(itemId)` / `inv:AddItemWithId(itemId)`: moves existing item instance into inventory (`ItemUtils.lua:106`).
  - `inv:CreateItem(classGuid, health, amount)`: spawns item directly in inventory (`ItemUtils.lua:47`).
  - `inv:DeleteItem(itemId, amount)`: deletes amount of an item instance (`Scripts.pak`).
  - `inv:DeleteItemOfClass(classGuid, amount)`: deletes amount by class GUID (`ItemUtils.lua:61`).
  - `inv:GetMoney()`: returns Groschen as `number` (`Negotiation.lua:538`).
  - `inv:GetId()`: returns inventory entity ID (`ItemUtils.lua:81`).
  - `inv:RemoveAllItems()`: empties inventory (`Scripts.pak`).
  - `ItemManager.CreateItem(classGuid, health, amount)`: creates item instance in world / manager, returns `itemId` (`ItemUtils.lua:105`).
- [V] Purse storage: The purse is NOT an RPG stat; it is a regular item of class `Money` (Name="money", GUID `5ef63059-322e-4e1b-abe8-926e100c770e`, `Libs/Tables/item/item__system.xml:8`, `ItemUtils.lua:7`).
- [V] Shop presets explicitly define purse as money items, e.g. `<PresetItem Name="money" Amount="600" />` (`Libs/Tables/item/InventoryPreset__shops_components.xml:4`).
- [V] Currency unit: Lua `inv:GetMoney()` returns whole Groschen. In shop transactions, currency is tracked in **deciGroschen** (1 Groschen = 10 deciGroschen / halers; `DialogUtils.lua:168`, `Negotiation.lua:538`).
- [V] Reading/changing money: read via `Shops.GetShopMoney(npc.id)` or `npc.inventory:GetMoney() * 10`. Changed via `npc.inventory:CreateItem('5ef63059-322e-4e1b-abe8-926e100c770e', 1, amount)` / `DeleteItemOfClass`, or `Shops.DoMoneyTransaction(npc.id, deciGroschen, false)` (`DialogUtils.lua:170`).

## 3. Sale Event & Item Tagging
- [V] Sale completion: Player -> merchant sales commit at `0x1808d5150` (`FUN_1808d5150`), which is slot `+0x50` in `wh::shopmodule::C_ShoppingTransaction::vftable` (`0x183d15790`).
- [V] Arguments received:
  - `param_1`: `C_ShoppingTransaction*` context object.
    - `*(param_1 + 0x10)`: merchant entity pointer (`C_Entity*` / `C_Soul*`).
    - `*(param_1 + 0x18)`: shop inventory pointer.
    - `*(param_1 + 0x98)`: basket item vector `std::vector<S_BasketItem>`. Elements are 16-byte pairs: `*elem` = item pointer (`C_Item*`), `*(elem+1)` = `uint32 count`.
  - `param_2`: `int finalPrice` (in deciGroschen; negative for purchases, positive for sales to merchant).
  - `param_3`: transaction result pointer.
- [V] Transfer logic: If item belongs to player (`plVar5->vf18` returns 0), item is moved to shop inventory via `inv->vf40` (`*(param_1+0x18)`).
- [V] Lua-visible callback: NONE exists for shopping basket checkout. (Dialogue uses `Shops.AcceptTransaction` at `0x18152ea94`, but it receives only `shopkeeperId` and `finalPrice`, not items). A native hook on `0x1808d5150` is required to capture sold item IDs and counts.
- [V] Item marking: Sold items are NOT tagged. `item:IsFromShop()` (`FUN_182abe1b4` at `0x182abe1b4`) only checks if the item's current inventory owner is registered in `ShopsDatabase` (`0x182efe484`). Once sold into a shop inventory, all items in that inventory return `true`. Sold items are mixed directly into stock.

## 4. Shop Open Hook Point
- [V] Native hook point (BEST): `wh::guimodule::C_UIShop::Open` at `0x180a4a118` (slot `+0x38` in `C_UIShop::vftable` at `0x183a81ad8`), called by `Shops.OpenInventory` (`0x182efe8fc`).
  - Intercepting here executes strictly BEFORE shop inventories (`shop_inventory`, `shop_trader`, `ApseInventoryList`) are transferred into Scaleform movies (`FUN_180bc8f88`).
  - Updating items and purse at this point ensures the Flash UI renders the already-settled inventory, preventing visual pop-in and list desync.
- [V] Lua UI listener: `UIAction.RegisterElementListener(..., "ApseInventoryList", -1, "OnInvActive", "OnActive")` (`fastmove.lua:34,76`).
  - [I] Fires after Flash element activation. Modifying inventory while Scaleform is building/rendering causes severe desync or crash (see Risk 1).
- [V] Safety check: `Shops.IsShopOpened(entityId)` (`0x182efe6b0`) checks if the shop UI is currently active (`*(param+0x198) != 0`).

## 5. Restock & Destock Mechanics
- [V] `restock_period` is configured per shop in `shop.xml` in integer days (`Libs/Tables/shop/shop.xml`, e.g. 7 or 14 days), parsed by `FUN_180eae9bc` (`0x180eae9bc`). Stashes store `nRestockPeriodDays` and `nDestockPeriodDays` (`FUN_180a2d4b4` at `0x180a2d4b4`).
- [V] What is reset: BOTH items and money are regenerated by re-evaluating the shop's `inventory_preset` (`FUN_180a2e820` at `0x180a2e820`). Missing stock items are spawned, and purse money is reset to preset values (`inventory_shop_money_*`).
- [V] Timing: Evaluated daily at 3:00 AM game time (`wh_ent_RestockStartTimeMinutes = 180` at `0x1815aa080`), throttled over frames via `wh_ent_RestockPerFrameBudget`.
- [V] Non-preset items: Handled by destocking (`DestockEvenNondestockable` in `wh::entitymodule::FullyRestock` at `0x181abd150`), which cleans up non-preset items over time.
- [I] Conflict resolution: If player-sold items remain unsold after `nDestockPeriodDays`, vanilla destock removes them and resets purse to preset. Lazy settlement at shop open resolves this cleanly: sold items are settled into coins before the shop opens; if vanilla restock ran in the interim (e.g. 7+ days elapsed), the shop purse was already refreshed to preset.

## 6. Game Time API
- [V] Global time APIs in `Calendar` namespace (`Scripts/Utils/TimeUtils.lua:10-16,93`, `Scripts/Entities/actor/player.lua:416-432`):
  - `Calendar.GetWorldTime()`: returns continuous absolute world time in SECONDS (`float`; 86,400 seconds = 1 game day).
  - `Calendar.GetWorldHourOfDay()`: returns current hour of day (`float`, 0.0 to 24.0).
  - `Calendar.GetGameTime()`: returns gameplay session time in seconds (`BirdsTakeoff.lua:318`).
- [V] Independence from shop streaming: `Calendar.GetWorldTime()` is driven by CryAction / Game world clock. It continues advancing globally during sleep, fast-travel, waiting, or while the player is far away in another zone.
- [I] Elapsed days formula: `local elapsedDays = (Calendar.GetWorldTime() - lastSaleTime) / 86400.0`.

## 7. Savegame State Persistence
- [V] Existing mods (`inventory`, `dice`):
  - `inventory` stores global settings in `inventory_settings.txt` (`inventory_main.cpp:30`, `inventory_main.lua:47`), replayed via console commands (`inv_set`) on `hello`.
  - `dice` stores settings in `dice_settings.txt` and cached solutions in `dice_cache/` (`dice/README.md:37`). Neither mod maintains per-savegame state.
- [V] Lua globals (`_G.MerchantResale`): persist in memory across save/load, but DO NOT revert when the player loads an earlier save or dies, causing severe desync.
- [V] Engine Entity Serialization: CryEngine entities with `:OnSave(tbl)` and `:OnLoad(tbl)` have `tbl` serialized directly into `.whs` savegame files:
  - `Player` entity implements `Player:OnSaveAI(save)` and `Player:OnLoadAI(saved)` (`player.lua:920-932`). Attaching mod state to `Player` or `Shop` (`Shop.lua:1-24`) allows native per-save persistence without external files.
- [I] Sidecar file alternative: A native plugin can hook game save/load events and write `Bin/Win64MasterMasterSteamPGO/resale_saves/<save_guid>.json`, but requires hooking savegame identity.

## Recommended Design
1. **Sale Capture**: Native MinHook on `C_ShoppingTransaction` checkout method at `0x1808d5150` (`C_ShoppingTransaction::vftable` +0x50).
   - Extracts merchant entity ID (`*(param_1+0x10)`), shop ID (`Shops.GetShopDBIdByKeeper`), and for each item sold to merchant, records `{ classGuid, itemId, count, unitPrice, timestamp = Calendar.GetWorldTime() }`.
   - Passes data to Lua via `System.LogAlways("[RESALE] sold ...")` or directly stores in a shared struct.
2. **Shop Open & Lazy Settlement**: Native hook on `wh::guimodule::C_UIShop::Open` at `0x180a4a118` (`C_UIShop::vftable` +0x38).
   - Executes before Scaleform loads inventory lists.
   - Computes `elapsedDays = (Calendar.GetWorldTime() - lastVisitTime) / 86400.0`.
   - Linearly or probabilistically resells items: for each resold item, calls `shopInventory->DeleteItemOfClass(classGuid, count)` (or `DeleteItem(itemId)`) and adds coins via `shopInventory->CreateItem('5ef63059-322e-4e1b-abe8-926e100c770e', 1, totalCoin)`.
   - Calls original `C_UIShop::Open`, displaying updated stock and coins seamlessly.
3. **State Storage**: Hook `Player:OnSaveAI` / `Player:OnLoadAI` in `player.lua` (or `Shop:OnSave` / `Shop:OnLoad` in `Shop.lua`).
   - Stores `{ [shopId] = { soldItems = { ... }, lastVisit = timestamp } }` inside savegame `.whs`. Automatically tracks branching saves and reloads.
4. **Top 3 Risks**:
   1. **Modifying inventory while UI is open (CRASH RISK)**: Scaleform / C++ holds cached vector pointers (`sub+0x98`). Modifying inventories while `Shops.IsShopOpened` is true crashes the engine. Settlement MUST occur before `C_UIShop::Open` or after close.
   2. **Dangling item pointers from stolen/destocked items**: If an item was stolen, moved, or destocked prior to resale, deleting by raw `itemId` can corrupt inventory. The mod must check `inv:FindItem(itemId)` or use `DeleteItemOfClass(classGuid, count)`.
   3. **Savegame desynchronization on death or reload**: Using in-memory Lua tables causes items sold in timeline B to remain marked sold after reloading timeline A. Storing state inside savegame `.whs` via entity `OnSave`/`OnLoad` is required.
