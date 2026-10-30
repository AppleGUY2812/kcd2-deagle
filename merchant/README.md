# Merchant Resale — phase 1

**Logging only, not yet a playable resale mod.** No item is removed, no purse is credited,
and no ledger or save data is changed. `native/src/merchant_logic.h` is a separate,
tested model for a later integration; the plugin does not include it.

## What it observes

For WHGame.dll **1.5.6**, `merchant.asi` first matches PE metadata and the CodeView PDB
GUID/age against the researched build, then checks function prologues. Missing or mismatched
build identity installs **zero hooks**, including the Lua channel (stripped metadata also fails closed):

- Checkout `0x1808d5150`: one line per attempt, with final net price, shop/keeper identity
  when its concrete type matches, and each item's class name, GUID, count, base price,
  and buy/sell direction. Snapshot is taken before the original moves/frees items.
- The supplied `0x180a4a118`: a diagnostic `shop_open_probe`, **not a genuine open event**.
  Static evidence shows listener registration, not a merchant-bearing open signature.
- Corrected `C_Shop` entry `0x180c406d8`: one `shop_open before_original` line with shop
  DB ID, keeper WUID and purse, before the original dispatches its UI context.

`U` means unavailable, **not zero**. Merchant display names, 32-bit entity IDs, positions,
and native categories are unresolved. A keeper WUID is not a verified 32-bit entity ID.
Known deferred logging caveat: failed keeper-WUID/instance-token reads can still print
`0x0` instead of U; do not interpret those two zero tokens as proven IDs.
Unexpected shop types produce U instead of unsafe casts. Each pointer read and game
query is SEH-guarded; a failed observation never prevents the original call.

Checkout lines are **attempts**, not proof of successful payment. Mixed baskets have
per-item directions; the net price sign alone is not a sale event. Full static evidence,
corrections to Round 1, and unknowns: `docs/merchant_resale_native.md`.

## Files and commands

`merchant.log` and `merchant_settings.txt` live beside `merchant.asi`. Logs rotate to
`.1.log` / `.2.log` on startup, using the shared logger. There are no per-frame callbacks.

The Lua command loader is `src/Scripts/Mods/merchant.lua`; its scripts must be packaged
as a stored ZIP at `Mods/merchant/data/merchant.pak`, alongside `mod.manifest`.
Packaging, installing, and the Windows build are **not** performed by this round.

- `merchant_log 0|1`: observation logging, saved (default 1).
- `merchant_status`: writes hook installation flags, call counts and the last observed
  shop to `merchant.log`. No nearest-merchant lookup or live ledger in phase 1.
- `merchant_resale 0|1`: saved **reserved setting**, default 0. Even `1` cannot activate
  resale in this binary. It never calls the model or changes money/items.

Commands use the existing Lua `System.LogAlways` -> native hook channel. Replies are
in `merchant.log`; there is no extra frame hook or native console executor. Settings
are read at plugin startup; changing the text file requires a restart (not a save load).

## Build / offline checks

The `merchant` target in `native/CMakeLists.txt` uses dice's MinHook/shared logger/settings
sources, MSVC x64 flags `/W3 /EHa /O2`, static CRT, and outputs `merchant.asi`.
**Windows compilation is still required by the build owner; it has not been checked here.**

From the repository root, Linux only:

```sh
nice -n 19 g++ -std=c++17 -O2 -Wall -I native/src native/test/merchant_logic_test.cpp -o /tmp/mlt && /tmp/mlt
luajit merchant/tests/commands_test.lua
nice -n 19 g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic merchant/tests/build_guard_test.cpp -o /tmp/opencode/merchant_build_guard_test && /tmp/opencode/merchant_build_guard_test
```

The model uses Groschen and absolute **game days** (native logged money is deciGroschen).
Markets use a fixed roster and single-link <=300m clustering (bridging merchants join
components); resolve the full roster before sales rather than re-clustering wandering NPCs.
Stacks become per-unit ledger rows, each using pre-sale demand and stable item-ID/ordinal
spread. Serialize/Deserialize produces one versioned string; no game persistence is wired.
Settlement returns due rows and gross earned coin; integration must reconcile actual stock,
then use the separate capped wealth helper. It must not blindly delete items by stale IDs.
Deferred format hardening: Deserialize accepts multi-unit rows, although AddSale generates
per-unit rows; only consume the model's own serialized state until that invariant is tightened.

## Later manual validation (only after the game is closed, build/install by its owner)

Open a shop, sell one item, buy it back, try a mixed basket and partial stack, and inspect
the log. Confirm `shop_open` identities and purse against the UI, and distinguish rejected
payments from completed trades. No hook coverage or crash-safety claim can come from the
offline model tests. Do not enable mutation until save rollback is also proven.
