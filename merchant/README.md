# Merchant Resale (phase 2)

Merchants resell what you sell them. Every item you sell to a shop goes into that shop's ledger; over the next game
days it leaves the shop's stock and the shop gets the item's full price in coin. Selling a lot of one kind in one town
(say 30 plates of armour in Kuttenberg) floods that market: those items take ~3 weeks instead of ~3 days, and demand
recovers with a 4-day half-life. Money a shop earns this way survives the weekly restock (capped at 10x its normal
purse). Rules and numbers: `docs/merchant_resale_design.md`.

## How it works
- `merchant.asi` (WHGame 1.5.6 only; any other build installs no hooks) watches the checkout. A checkout counts only
  when the shop's purse moved by exactly the trade's price (committed). It never moves items or money itself: it
  queues `sell|buy <shop> <class> <count> <base price> <item token>` lines for Lua, read with `MRN.pull()`.
- `Scripts/MerchantResale/merchant.lua` (1 s timer): records the trades, and settles a shop when you come within 6 m
  of its keeper or stash (before you can open the shop screen; never while it is open): due items leave the stash,
  coins go into the purse (the stash holding the most money), and a restocked purse gets its savings back.
- State (ledger, demand, purses) lives in the player's AI table, which every save stores: each save has its own.
- Any failed removal or a purse that moved by the wrong amount stops all changes for the session
  (`STOPPED for this session` in the log).
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
