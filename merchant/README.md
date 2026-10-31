# Merchant Resale (phase 3)

Merchants resell what you sell them. Every item you sell to a shop goes into that shop's ledger; over the next game
days it leaves the shop's stock and the shop gets coin for it: the item's value at its condition (the game's own
curve) times that merchant's resale rate, twice what his own price rule pays you, at most full value (a blacksmith's
armour or a fence's rings: full value; a fence's or general trader's anything else: 70 %; `prices.lua`, generated
by `python3 merchant/tools/build_prices.py <game dir> merchant/src/Scripts/MerchantResale/prices.lua` from shop.xml). Selling a lot of one kind in one town floods that market: those items take ~3 weeks instead of ~3 days, and
demand recovers with a 4-day half-life. A market's size grows with the shops the mod knows there: a lone 339 gr fence
absorbs ~12 000 gr of weapons, a town of 20 shops ~90 000 gr. Money a shop earns this way survives the weekly restock (capped at 10x its normal
purse). Live economy (phase 3): what a shop pays you follows its market's demand for that kind of item (fresh +10 %,
flooded down to -24 %; what shops charge stays vanilla), and each town's prosperity (0.5x .. 2x weekly purses) rises
with money you spend there and its merchants' resale profit, and falls twice as hard with coins stolen from its shop
chests. A line on screen says when a market stands out. Rules and numbers: `docs/merchant_resale_design.md`.

## How it works
- `merchant.asi` (WHGame 1.5.6 only; any other build installs no hooks) watches the checkout. A checkout counts only
  when the shop's purse moved by exactly the trade's price (committed). It never moves items or money itself: it
  queues `sell|buy <shop> <class> <count> <base price> <item token>` lines for Lua, read with `MRN.pull()`.
  It also hooks the game's trade price (FUN_180566410): when you sell to a shop, the price is x the factor Lua sent
  for that shop and item category (`MRN.cats` once, `MRN.prices` on change; clamped 0.5 .. 1.5 in the plugin; no
  factor = vanilla). `merchant.log` shows the first 40 changed prices.
- `Scripts/MerchantResale/merchant.lua` (1 s timer): records the trades, and settles a shop when you come within 6 m
  of its keeper or stash (before you can open the shop screen; never while it is open): due items leave the stash,
  coins go into the purse (the stash holding the most money), and a restocked purse gets its savings back.
- State (ledger, demand, prosperity, purses) goes into each save through the player entity's `OnSave` / `OnLoad`
  (`written into the save` / `read from the save` in kcd.log).
- Sold items stay the merchant's property in his chest, and the chest's `DeleteItem` skips them; the mod then uses
  `ItemManager.RemoveItem` (deletes from the owner's inventory, whole stack). If the game refuses both, the item stays
  in stock and the shop gets no coins (`would not remove` in the log). Half a stack removed, or a purse that moved by
  the wrong amount, stops all changes for the session (`STOPPED for this session`).
- `categories.lua` is generated from the game's item tables: `python3 merchant/tools/build_categories.py <game dir>
  merchant/src/Scripts/MerchantResale/categories.lua` (weapon / armor / food / alchemy; everything else is misc).

## Commands
`merchant_status` (nearest shop: purse, savings, market demand, items waiting), `merchant_resale 0|1` (saved; 0 keeps
recording but resells nothing), `merchant_log 0|1` (checkout lines in `merchant.log` next to the plugin).

## Build, test, install
- Tests: `luajit merchant/tests/logic_test.lua`, `luajit merchant/tests/commands_test.lua`;
  the older native model (`native/src/merchant_logic.h`, not used by the game) has `native/test/merchant_logic_test.cpp`.
- Plugin: the `merchant` target of `native/CMakeLists.txt` -> `native/dist/merchant.asi`.
- `bash merchant/build.sh --install` (refuses while the game runs).
