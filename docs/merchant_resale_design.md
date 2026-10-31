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
   unseen customers: the item leaves the stock and the merchant gets value x rate. Value = price x (0.01 + 0.99 x
   health), the game's own condition curve (FUN_18064a024, ItemHealthPriceStatusWeight 0.99). Rate = that shop's own
   price rule for the item (shop.xml, last matching ShopItemFilter): min(2 x price_sell_multiplier, max(1,
   price_buy_multiplier)); 0.7 when unknown (2026-10-30, user: specialists pay more, so they should make more).
   A trade's price in game = value x shop multiplier x a player factor (stats 6 and 0x81, FUN_180566530) x a
   reputation factor (FUN_1805665a8); the user's axe at Krejzl: 2217.5 x 0.35 x ~0.14 = 108 gr, no haggling. Buybacks: if the player buys a ledger item back, it leaves the ledger, no coin.
2. **Demand.** A market = merchants within 300 m of each other (Kuttenberg, Trosky village, ...); a category = the
   item's top-level class (armor, weapon, food, herb/alchemy, misc). Demand d per market x category, 0.15..1.0, starts
   at 1. Each sale lowers d by (value / market capacity). Capacity = per category (armor 15000 gr, weapon 10000, food
   1500, alchemy 4000, misc 5000) x (1 + W / 1500 gr), W = the normal purses of the market's shops the mod knows
   (2026-10-30, user: "4000 gr weapon market sounds too little"; one 2217 gr axe had cut a hamlet's weapon demand to
   0.45, now 0.82). d recovers toward 1 with a 4-day half-life.
3. **Speed.** An item resells after its expected time T = base_days[category] / d (armor 3 days, weapon 3, food 1,
   alchemy 2, misc 2), with +-30% random spread per item (seeded by item id, so reloads give the same answer).
   Flooding Kuttenberg with 30 plates drops armor demand to the floor: those take ~20 days, not 3. Prices are NOT
   changed (v2 maybe).
4. **Wealth survives the weekly reset.** The merchant's earned money above preset is kept as a bonus B. When the
   vanilla restock has reset the purse (detected: purse at open != purse at last close, no sale in between), the mod adds
   B back. B never goes below 0; total purse capped at 10x preset (a fat Kuttenberg merchant, not a bank).
5. **Lazy.** Nothing runs while the player is away. A shop settles when the player comes within 6 m of its keeper or
   stash (Lua, 1 s timer; never while its shop screen is open), so the list and purse the player then sees are settled.
   (Phase 2 change: settling inside `C_UIShop::Open` would have moved items from native code; Lua's inventory calls
   are the game's own and were verified live.)
6. **Per save.** The player entity's `OnSave` / `OnLoad` (wrapped, other mods' kept) write the state into every
   save and read it back. `Player:OnSaveAI` did not reach the save in game (2026-10-30), so the AI table copy is only a
   fallback until `OnLoad` has fired once in a session.
7. **No UI.** Console: `merchant_status` (nearest merchant: purse, bonus, ledger rows with ETA, market demand),
   `merchant_resale 0|1` (saved setting).

## Hooks (addresses verified 2026-10-30 in Ghidra, WHGame 1.5.6)
- Checkout: `C_ShoppingTransaction` vtable `0x183d15790` slot +0x50 = `0x1808d5150` (basket vector at +0x98).
- Shop open: `C_UIShop` vtable `0x183a81ad8` slot +0x38 = `0x180a4a118`.
Still unverified: that 0x1808d5150 runs for every sell and buyback; that the player's `OnSave` reaches the save.

## Phases
1. Read-only: hooks log every checkout (items, counts, prices, merchant) and every shop open (merchant, purse). The user
   sells and buys once; the log proves the hooks. No game state changed.
2. Ledger + demand + settlement on approach + save persistence (built 2026-10-30: plugin reports committed
   checkouts through `MRN.pull()`, `merchant.lua` does the rest; see `merchant/README.md`).
3. Live economy (decided 2026-10-30 with the user: "a dynamic live economy ... the player can see and be rewarded by
   his actions or punished, not broken but still a buff to the vanilla"; no fixed allowances):
   a. Prosperity per market, 0.5 .. 2, drifting back to 1 with a 14-day half-life. The weekly restock purse = normal
      purse x prosperity (on top of savings). Raised by money the player spends in its shops (x1) and its merchants'
      resale profit (x0.5); lowered by coins taken from its shop chests (x2); one unit = 4 x the market's known
      purses (at least 300 gr). Built (step 1).
   b. Seen: a line when the player walks up to a shop, only when something stands out ("Market: prosperous (purses
      +20%) - weapons flooded (slow to sell)"), at most once a game day per shop; `merchant_status` shows it all; the
      trade screen shows the purses. Built (step 1).
   c. Demand sets what merchants pay the player: x (0.70 + 0.40 d): fresh +10 %, half -10 %, flooded -24 %. Only the
      price they pay, never what they charge (no buy-low/sell-high loop: vanilla buys at 0.7-0.8, sells at
      0.35-0.5). The merchant's own resale income gets the same factor. Needs a hook on the trade price
      (FUN_180566410). Step 2, not built.

## Reference: the Legacy of the Forge sales chest (read 2026-10-30 from Scripts.pak)
`Quests/Final/Barbora/kutnohorsko/legacy_of_the_forge/selling_stuff*` (Skald graphs; settings on the
`balancing_variables` node in `selling_stuff.xml`):
- Once per game day (`selling_period` 1d, world time), only if the chest holds weapons, helmets, hoods, armour, ammo or
  crafting material. Days spent in another level are caught up (`selling_after_levelswitch`).
- Daily budget = 2 x (random 50..100 gr, shuffled) x prestige factor (linear: prestige level x 5/30, so 5x at level 30).
  Unused budget carries over to the next day.
- Each item in turn: worth = its current stack price (condition included) / 1.43, i.e. 70 %. Sold (deleted with
  `Inventory.DeleteItems`) if it still fits in the day's budget; items that don't fit wait, smaller ones go on.
- Every day nothing sells, the divisor grows by 10 % (max 5 days): payout falls to 47 % of the price.
- The money goes to the PLAYER (collected later, `createmoney_player`); no demand, no per-town market.
