# Merchant Resale: design (decided 2026-10-30, Claude from the user's idea + agy's research)

## Why
KCD2 caps merchant purses on purpose: every shop restocks every 7 days (all 171 rows of `shop.xml`), and the restock
sets the purse back to its preset (`inventory_shop_money_350` .. `_15000`). What the player sold just sits in stock until
destock removes it. So a Kuttenberg armourer with 800 gr can never buy a 4000 gr plate, and robbing the chest pays better.
KCD1 turned destocked items into coin (merchants reached 40-60k); KCD2 dropped that. This mod brings it back, slowly
and with demand, so it feels like part of the game.

## Rules (decided)
1. **Resale.** Every item the player sells to a merchant goes into that merchant's ledger: class, count, base value
   (the item's full price), the price the merchant paid, and the game time. Over game days the merchant resells it to
   unseen customers: the item leaves the stock and the merchant gets its base value (his margin = what he paid you
   vs full price). Buybacks: if the player buys a ledger item back, it leaves the ledger, no coin.
2. **Demand.** A market = merchants within 300 m of each other (Kuttenberg, Trosky village, ...); a category = the
   item's top-level class (armor, weapon, food, herb/alchemy, misc). Demand d per market x category, 0.15..1.0, starts
   at 1. Each sale lowers d by (base value / market capacity) where capacity is per category (armor 6000 gr, weapon
   4000, food 600, alchemy 1500, misc 2000). d recovers toward 1 with a 4-day half-life.
3. **Speed.** An item resells after its expected time T = base_days[category] / d (armor 3 days, weapon 3, food 1,
   alchemy 2, misc 2), with +-30% random spread per item (seeded by item id, so reloads give the same answer).
   Flooding Kuttenberg with 30 plates drops armor demand to the floor: those take ~20 days, not 3. Prices are NOT
   changed (v2 maybe).
4. **Wealth survives the weekly reset.** The merchant's earned money above preset is kept as a bonus B. When the
   vanilla restock has reset the purse (detected: purse at open != purse at last close, no sale in between), the mod adds
   B back. B never goes below 0; total purse capped at 10x preset (a fat Kuttenberg merchant, not a bank).
5. **Lazy.** Nothing runs while the player is away. All settlement happens when the player opens that merchant's
   shop, before the UI fills (`C_UIShop::Open`), so the list and purse the player sees are already settled.
6. **Per save.** State lives in the save game, so loading an older save gives that save's ledger.
7. **No UI.** Console: `merchant_status` (nearest merchant: purse, bonus, ledger rows with ETA, market demand),
   `merchant_resale 0|1` (saved setting).

## Hooks (addresses verified 2026-10-30 in Ghidra, WHGame 1.5.6)
- Checkout: `C_ShoppingTransaction` vtable `0x183d15790` slot +0x50 = `0x1808d5150` (basket vector at +0x98).
- Shop open: `C_UIShop` vtable `0x183a81ad8` slot +0x38 = `0x180a4a118`.
Still unverified: that 0x1808d5150 runs for every sell and buyback; that `Player:OnSaveAI` data reaches the save.

## Phases
1. Read-only: hooks log every checkout (items, counts, prices, merchant) and every shop open (merchant, purse). The user
   sells and buys once; the log proves the hooks. No game state changed.
2. Ledger + demand + settlement on open + save persistence.
3. Tune numbers from play.
