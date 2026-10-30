# Merchant Resale — Round 2 native evidence (WHGame.dll 1.5.6)

[V] Addresses below are Ghidra VAs (image base `0x180000000`), queried read-only with `~/re/scripts/qk`.
[V] V = observed static code/data; I = interpretation/design choice; U = not established. No runtime validation was performed.

## Corrections to the supplied design / Round 1

- [V] Checkout remains `0x1808d5150`, at `0x183d15790+0x50` (qk data `0x183d157d0`).
- [V] `transaction+0x98/+0xa0` delimit **8-byte item pointers**, NOT 16-byte basket records:
  `0x1804187bc` copies `(end-begin)>>3`; `0x182047d80` walks pointers and constructs temporary 16-byte `{item,count}` records.
- [V] Their count comes from `C_Item+0x50` (`0x182047d80`). The temporary records are local to checkout, not the transaction field.
- [V] `transaction+0x10` is initialized from the global player path, NOT a merchant pointer:
  `0x180c47d14` calls framework `vt+0x200`, then `0x1808d285c`; `0x180c408b4` uses the same path for shop UI's player.
- [V] `0x1808d285c` resolves `actor+0x668` through `vt+0x420`; checkout `+0x18` is resolved from transaction `+8` by `0x180c4865c`.
- [I] Therefore `+0x10` is the player-side inventory owner and `+8` is the shop inventory WUID; do not label `+0x10` “merchant”.
- [V] Supplied “open” `0x180a4a118` has only `this`; it registers callbacks for `0x117/0x74`, then calls `0x180a4a260`.
- [V] `0x183a81ad8` has ONE method (`0x1808c22f8`), then string `wh_ui_DebugShop` and another RTTI/vtable region.
  `0x180a4a118` is at `0x183a81b10`, but this is NOT a valid `+0x38` slot of that one-method subobject's vtable.
- [V] `0x180bc8f88` disassembly installs `0x183a81ad8` at `C_UIShop+0x18`; primary vtable is `0x183d2d628`.
- [I] Preserve the requested address as a clearly labelled diagnostic probe, not proof of shop opening or a settlement hook.

## Step A: item fields

- [V] Item class definition pointer: `*(C_Item+0x48)` (`0x1804695b4`, returning a default definition if null).
- [V] Class GUID: 16 bytes at definition `+8`; `0x182abc0bc` obtains this via `0x1804695b4`, then formats it with `0x18070849c`.
- [V] GUID format uses u32/u16/u16 + eight bytes (`0x18070849c`), not two guessed u64 printouts.
- [V] Instance token: `*(item+0x30)` is copied during checkout (`0x1808d5150`) and returned as Lua item ID (`0x182abc0bc`).
- [U] Its internal representation/lifetime beyond the call is not established; logger records only the opaque token.
- [V] `ItemManager.GetItemName` = `0x182abc25c`, `GetItemUIName` = `0x182abc354`, registered at `0x1812b2d38`.
  Class definition `vt+0xc8(this,0)` yields a record; `+0x18` is class-name string, `+0xa0` the UI-name string/key.
- [I] UI-name is not guaranteed localized display text; the logger labels it `ui_name_key`.
- [V] Base-price getter `0x180469510`: definition `vt+0x18(this,0x16)` tests presence; if present,
  definition `vt+0xc0(this,0)` yields record, integer `record+0x5c` is returned; absent component returns zero.
- [V] Reflection `0x180fcd148` registers `wh::entitymodule::PickableItem`, field `Price` (`0x183b2a024`) at `+0x5c`.
- [V] Current valuation uses this base field plus condition/other modifiers (`0x18096f700`, `0x18096f77c`, `0x180469988`).
- [V] Conversion constant `0x18409ee58` is float `0.1` (`qk data`: low word `0x3dcccccd`), used in `0x18096f77c`.
- [I] Integer base field is deciGroschen; model inputs are Groschen, so future integration must divide by 10.
- [U] Native category/type/subtype path used by shop filter matching remains unresolved. Searched `armor`, `GetItemType`,
  filter reflection `0x18023ca20`, category-negation enum `0x181eea6b0`, and the PickableItem reflection; no proven accessor chain.
- [V] Table filters include `armor.Cuirass.*`, `helmet.*`, `weapon.*`, `food.*`, `crafting_material.herb.*`, `poison.*`:
  `Tables.pak:Libs/Tables/shop/shop.xml`, rows `common_blacksmith`, `common_herbalist`, `common_butcher`.
- [U] Native enum-to-prefix mapping is not verified; logger deliberately emits `category=U`, not a guessed class-name prefix.

## Step A: merchant, opening, purse

- [U] Merchant ID/name/position cannot be read “from transaction+0x10”: the premise is false (player path above).
- [V] Correct script entry: `Shops.OpenInventory` `0x182efe8fc` resolves keeper entity, then `0x18152eda4` / `0x18152edc8`
  find a shop and call that shop's `vt+0x38(shop, keeperWuid)`.
- [V] Actual `C_Shop` primary vtable is `0x183a949b8` (qk sym `C_Shop`, RTTI locator `0x184117b08` has subobject offset 0).
  Its `+0x38` points to `0x180c406d8` (qk data `0x183a949f0`).
- [V] `0x180c406d8(this, keeperWuid)` stores keeper WUID at `this+0x188`, builds UI context (`0x180c4093c`),
  obtains a transaction (`0x180c47c10`), then dispatches the UI context through the UI service `vt+8`.
- [I] This is the proven pre-UI shop entry, added as a third observation hook alongside the two requested addresses.
- [V] Shop DB ID is integer `shop+0x1b0` (`0x18152ec44`, `0x182efe608`).
- [V] Lua purse path: `Shops.GetShopMoney` `0x18152ebfc` -> `0x18152ed60` -> resolved shop `+8` -> `0x1808c57ac`.
- [V] `0x1808c57ac(collection,int* out)` enumerates the collection with its `vt+8` and returns accumulated integer in `out[0]`.
- [I] Same getter can observe a verified `C_Shop` directly at `shop+8`; plugin gates on its primary vtable and getter prologue.
- [U] Merchant human-readable name, 32-bit entity ID and world position remain unresolved for this WUID, as does confirmed
  correspondence between checkout `+0x18` and that exact concrete vtable. Failed type gates emit U, never guessed fields.
- [V] Existing position research (`docs/whgame_re/sim_world.md:55-60`) concerns Cry `CEntity`, not checkout's player inventory owner.
- [I] Reusing its `+0x64/+0x74/+0x84` offsets on checkout `+0x10` would read the wrong object; logger does not do so.

## Checkout semantics / limits of phase 1

- [V] Signature observed at `0x1808d5150`: void `(transaction, int finalPrice, resultPointer)`; third argument is dereferenced.
- [V] The original first calls payment routine `0x181951800` and may skip item transfers on failure (`0x18197b1fc` test).
- [I] A hook-entry snapshot is an **attempt**, not a confirmed sale; phase 2 needs an outcome gate, not blind ledger insertion.
- [V] Shop collection `(transaction+0x18)+8`, `vt+0x18(collection,item)` decides transfer branch (`0x1808d5150`).
  True moves item to `transaction+0x10` through `0x1808d534c`; false moves it into shop through shop `vt+0x40`.
- [I] These branches correspond to per-item buy / sell; mixed baskets require per-item classification, not the price sign.
- [V] Payment `0x181951800` positive price consumes money from owner `+0x10`'s `+0xe8`; negative price consumes shop money
  via `0x182a20398`. [I] Thus positive is player pays / negative shop pays, opposite Round 1's sign claim.
- [U] Coverage of every sell/buyback, partial stacks and UI rejection is untested in a running game.

## Step A: Player:OnSaveAI

- [V] String `OnSaveAI` is at `0x184777948`, with code refs in `0x18358320c` and `0x183584328`.
- [V] Both require `OnSaveAI` and `OnLoadAI`, invoke `OnSaveAI` when serializer `vt+0x40` reports writing,
  serialize the resulting table under `ScriptData` (`0x183588a68`), and invoke `OnLoadAI` when reading.
- [V] `0x183583340` calls `0x183584328`; `0x1835835bc` calls `0x183583340`; `0x18358320c` is also a vtable target.
- [V] Packaged `Scripts/Entities/actor/player.lua:920-932` implements both callbacks and saves/loads `save.AI` / `saved.AI`.
- [I] Engine AI serialization supports this callback pair, so a save-string field is plausible.
- [U] Actual KCD2 player save route reaching these functions, and arbitrary added data surviving real save/load, are NOT proven.
  Need runtime save -> mutate -> load-old-save evidence in phase 2; no save hook or Lua table patch ships in phase 1.

## Implementation choices and remaining checks

- [V] Known-build gate before ANY hook: PE timestamp `0x6a350e20`, machine `0x8664`, 7 sections, SizeOfCode `0x03a01e00`,
  SizeOfImage `0x05b2d000`, entry RVA `0x01c1fe0c` (`qk data 0x1800001c0/0x180000200`).
- [V] CodeView directory: debug RVA `0x04099930` (`PE+0x278`), RSDS record RVA `0x047af510`, GUID
  `db30851b-bffe-461c-97b6-3848a30e9325`, age 3 (`qk data 0x184099930/0x1847af510`). Missing/mismatched identity fails closed.
- [V] Offline gate regression: matching prologues + wrong fingerprint -> `0` installation callbacks; `9` mismatches rejected;
  known-build fixture permits `4` hook-installation attempts. This is metadata identity plus prologue checks, not cryptographic attestation.
- [I] Pure model uses absolute game days, per-unit rows for stacks, pre-sale demand for ETA, stable item-ID/ordinal RNG,
  fixed roster single-link 300m markets, demand recovery at query, and close/open observations for restock bonus.
- [I] Rule 4's mismatch heuristic cannot distinguish theft from restock; preserve it as specified, with explicit intervening-trade flag.
- [I] Cap applies to added coins, not deletion of pre-existing vanilla over-cap money; spent bonus is recomputed at close.
- [V] Local C++17 tests print fresh ETA `2.302483`, demand floor `0.150000`, last ETA `25.102867`, recovered demand `0.924870`;
  buyback coin `0`, restored bonus `1200`, capped purse `8000`, roundtrip equality and 30 deterministic ETAs.
- [U] MSVC build and live hook validation are intentionally not performed here; no Windows program or game process was invoked.
- [V] `grep -i shop ~/re/index/KCD2/names.tsv` found no matching named functions; exact qk address/symbol evidence used instead.
- [V] Broad `qk sym` scans delayed other queries (20/30/60s timeouts); a background scan finished; targeted queries then resumed.
- [I] Before enabling mutations: prove real checkout outcome, reconcile stock against destock/theft/buybacks, verify entity mapping
  and save rollback. Do not settle on the supplied listener probe; do not retain item pointers or modify an open inventory.
- [U] Review minors deferred: failed keeper-WUID/instance-token reads can print `0x0` rather than U; deserialization permits
  multi-unit rows despite the generating API's per-unit format. Neither is used for live mutation in phase 1.

## Round 3 — blocked investigation (2026-10-30; phase 2 NOT implemented)
- [V] Runtime evidence supplied by the user: `merchant/work/phase1_ingame.log:15-19` records shop 520, purse 3780 -> 8307, mixed directions and club buyback; this is not a native commit-result specification.
- [U] New SSH/qk queries and the requested Linux g++ test command were rejected before execution (`permission.rejected: shell`); no new decompilation or test output was obtained in Round 3.
| Step A | Evidence retained / remaining unknown |
|---|---|
| 1 Commit | [V] Prior checkout/payment evidence: `0x1808d5150`, `0x181951800`, `0x18197b1fc` (above). [U] Exact success result/layout and cancellation gate. |
| 2 Time | [U] Native accessor, signature and units behind `Calendar.GetWorldTime`; Round 1's Lua claim does not establish a native read path. |
| 3 Keeper | [V] WUID stored at shop+0x188 by `0x180c406d8`; local soul lookup lead `0x181f985d0(*(u64*)(G+0x80)+0x38,&wuid)`, G=`*(u64*)0x1853322a0` (`docs/whgame_re/stat_base.md:23-29`), name at soul+0x360 (`native/src/stormmemo.cpp:251-252,289`). [U] This keeper's resolution, localized name, soul -> entity/position and 300m roster. |
| 4 Category | [V] item+0x48 -> definition, getters `0x182abc25c`/`0x180469510` (above). [U] Table-type accessor; a class-name guess is not verification. |
| 5 Coins | [V] Purse getter `0x1808c57ac`; Round 1 identifies binding `0x182efe260`. [U] Safe native credit callee, arguments, units and pre-open mutation contract. |
| 6 Remove | [V] Instance token observed at item+0x30 (`0x1808d5150`, above). [U] Token lookup/remove, stack splitting, missing-instance outcome; never reuse retained item pointers. |
| 7 Saves | [V] Local instrumentation names load candidates `0x1825bdc7c`/`0x1825bda54` (`native/src/samelevel.cpp:14-24,40-61`). [U] Stable identity on save AND load, exact signatures, completion/failure; no safe sidecar key. |
- [I] Safest fallback for all seven gaps: retain logging-only phase 1, no ledger integration or game writes; do not substitute wall time, global state, newest-save selection or unverified native calls. Resume Step A when execution permissions are available.
