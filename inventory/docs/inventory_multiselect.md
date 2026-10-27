# Inventory multi-select: what C++ does with several ids from GetFocusedItem (WHGame.dll 1.5.6)

Static analysis only (Ghidra project KCD2 on the ProLiant, decompile index + `qk` daemon). Nothing run in game.
Addresses are VAs at image base 0x180000000. "Guess" marks anything not read straight from the code.

## Bottom line
Returning several different items' ids joined with `|` goes down the path the game was built for. C++ turns the
string into a `std::vector<C_Item*>`, and move to horse, drop and the transfer screen's `transfer_item` all hand that
whole vector to one loop (`FUN_1808c43cc`) that acts on every item in order. Nothing in that loop checks that the
items share a class. Four things to get right:
1. **The amount dialog opens** for drop / move / transfer whenever the total piece count != 1 (vanilla does the same
   for a stack). Its maximum and default are the sum of the pieces, so confirming moves everything. Picking fewer
   fills the items in list order.
2. **The first id decides** the outfit warning (only item 0 is checked), the horse direction and capacity check, and
   the slider step. Leave equipped items out of a multi-selection, or put the equipped one first.
3. **Never send an empty or fully unresolvable list.** Two paths read `items[0]` without checking that the vector
   has anything in it (transfer and move-to-horse): a null read, so a crash (inferred from the code, not seen).
4. **No duplicate ids, and one side only** (all ids from the player, or all from the horse / chest). The target
   inventory is picked once per action, not per item.

## 1. The parse and the container
- `FUN_180563a40` (the list sub-object's "get focused", sub = screen+0x60): if dirty flag `sub+0x1bb` is set, calls the
  movie's `GetFocusedItem`, then `FUN_1805596e0(sub, str)`, clears the flag, and returns `sub+0x98`.
- `FUN_1805596e0`: `FUN_1806b8690(sub+0x98)` clears a vector (`if (begin != end) { destroy; end = begin; }`).
  It builds the delimiter string from `DAT_183a55d18` = `0x7c` `'|'`, then calls `FUN_1804fc340(str, "|", fn)`. That
  splitter honours `'...'` quoting and `\` escapes. It calls `fn` for every token, including empty tokens in the
  middle of the string. A trailing empty token is dropped.
- Lambda `<lambda_419040f0…>` vftable `0x183edd790`: slot0/1 `FUN_181f52f50` (copy/move), **slot2 = call operator
  `FUN_181f53990`**, slot3 `FUN_181f54080` (target_type), slot4 `FUN_181ddda20`.
  `FUN_181f53990: FUN_18069b858(*(this+8) + 0x98)`, which is `push_back` on a vector of `CryStringT` (8-byte elements:
  `FUN_1804fd208` copies the string, `end += 8`).
  **So `sub+0x98` is a `std::vector<CryStringT>` of the raw id strings, in order, with no dedupe.**
- OnFocusChanged (`FUN_1805595c8`, registered in `FUN_1805646f8`) parses its own string argument into the same vector.
  The actions set `screen+0x21a = 0x101` (busy byte + `sub+0x1bb` dirty), so **at action time the vector is
  re-fetched from GetFocusedItem**.
- Turning ids into items: `FUN_180564e6c(screen, out)` loops over **every** string and calls `screen->vf+0xc8`. Apse =
  `FUN_180565488`: `FUN_180565508` parses only `'w' + 16 hex` (17 chars). Anything else becomes an invalid WUID. Then
  `(screen+0x5a8)->vf+0x10(&wuid)` checks that this screen owns the item, and the item manager looks it up. A
  non-null result is pushed into a `std::vector<C_Item*>`. **Ids that are invalid or belong to another screen are
  skipped silently.**
- First-only getters exist too: `FUN_180563a14` returns `vec[0]` or `""`, and `FUN_180563840` resolves `vec[0]` to a
  `C_Item*`. They serve tooltips, quests and so on, not the move / drop / transfer below.

## 2. The actions
Action map `apse_inventory` (`FUN_1808c0a88`) and the transfer map (`FUN_1808c1578`):

| action | handler chain | mode |
|---|---|---|
| `move_item` (to horse / back) | lambda `0x181f4dd10` → `FUN_182aff490` (all items) → `FUN_182aff4e8` → `FUN_182b12da8` → `FUN_1808ebdd4` | 3 |
| `drop_item` | lambda `0x181f4d120` → `FUN_181741778` (all items) → `FUN_1817417d0` → `FUN_1808ebdd4` | 2 |
| `transfer_item` (ItemTransfer) | lambda `0x181f7cba0` → `FUN_1817419ec` → `FUN_181741af0(this, this[0xbc])` → `FUN_1808ebd24` (all items) → `FUN_1808ebdd4` | 3 |

`FUN_1808ebdd4(screen, items, cb, mode)`:
```
n = screen->vf88(items, mode);
if (mode != 1) {
  if (mode != 0 && n != 1) { step = FUN_180469590(items[0], 6) ? 0.1 : 1.0;
                             def = screen->vf90(...);   // = vf88 (FUN_181f52700 jumps to +0x88)
                             amount dialog FUN_1808ec58c(..., n*step, def*step); return; }
  n = 1; }
FUN_1808ec294(cb, items, n);       // cb(items, amount)
```
- vf88: base `FUN_1808ec278` = **sum of `item+0x50` (amount) over all items** (C_UIInventoryBase vt `0x183ede8a8`, and the
  transfer inventory, vt `0x183a80d48` (guess: primary vtable, from the constructor `FUN_180bc6cd0`)). Apse override
  `FUN_1808ec088` (vt `0x183d28030`): for mode 3 with a horse (`+0xab8`) and `items[0]` in the player inventory
  (`FUN_180560040`), it walks the items in order using each item's own unit weight (`FUN_180469550`) against the horse's
  free weight (`FUN_18055e3c0`) and returns how many pieces fit. Otherwise it returns the sum.
- Callbacks: move = lambda `6e8e…` (vt `0x183ede5e0`, op `FUN_181f53a60`). Transfer = lambda `d99a…` (vt
  `0x183ede5a8`, **same op `FUN_181f53a60`**). Both call `FUN_1819b157c(screen, target, items, amount)`, which calls
  `FUN_1808c43cc(…, items, amount, perItem{screen,target})` (argument order confirmed in the disassembly at
  `0x1819b15c4`). Drop = lambda `cab4…` (vt `0x183ede618`, op `FUN_181f53d80`), which calls `FUN_1819b1610`, then
  `FUN_1808c43cc(…, perItem{screen})`.
- **`FUN_1808c43cc` iterates the whole vector**:
  `for each item: take = min(remaining, item+0x50); remaining -= take; if (!perItem(item, take)) stop;` and stops early
  when `remaining == 0`.
- Per item, move (`FUN_181f53b90` → `FUN_181f520f0`): if the screen is still valid (`vf+0xa0`), then
  `item+0x60 & 2 ? FUN_1804cc9f8(target, item, 0) : FUN_1808d534c(target, item, take, 1)`, plus an equip-related
  branch (`FUN_1818867f0` / `FUN_181f0f730`, purpose unread). It returns 1, so the loop never stops early. Drop
  (`FUN_181f53840`): `FUN_1818523b4(owner, item+0x30, take, …)` spawns the item in the world, then returns 1.

## 3. Different classes: checks, asserts, crash paths
- No class comparison and no assert anywhere in the parse → resolve → dialog → loop → per-item chain above. Every
  amount and weight is read per item (`item+0x50`, `FUN_180469550(item)`).
- Only `items[0]` is looked at for:
  - the outfit warning (`FUN_18174182c` / `FUN_1817419ec` → `FUN_181741b24(items[0])`). An equipped item later in the
    list moves or drops with no warning.
  - the 0.1 slider step (`FUN_180469590(items[0], 6)`, meaning of flag 6 unknown).
  - the horse direction and capacity (`FUN_180560040`).
- `move_item`'s target is fixed per action (`FUN_182aff4e8`: `+0xab8` horse or `+0xab0` player, from the screen
  state). Mixing player and horse items would send some items to the inventory they are already in. Effect unread.
- **Null read on an empty vector**: `FUN_1817419ec` does `FUN_181741b24(*local_d8)` with no empty check. Apse vf88
  mode 3 does `FUN_180560040(this, *items)`. `FUN_1808ebdd4` does `FUN_180469590(*items, 6)` for mode 2/3. Only drop
  is guarded (`FUN_18174182c` checks `begin != end`). Vanilla must never get here with nothing focused. A mod string
  where every id fails to resolve would.
- **Duplicates**: the vector isn't deduped. The same `C_Item*` twice is counted twice in vf88 and acted on twice.
  Whether the second pass is safe after a split, move or drop is untested: avoid it.
- Re-entry: every handler checks the busy byte `screen+0x21a` and sets it. `FUN_180533900` (in a vtable at
  `0x183a81308`) clears `sub+0x1ba`. Guess: once per list update, so at most one action per frame.

## 4. Alternatives
- **Recommended: multi-id GetFocusedItem**, with these rules in the movie: valid `w`+16-hex ids only, deduped, all
  from the visible side, no equipped items (or the equipped one first). Return `""` and never fire when nothing
  resolves.
- Per-item re-fire (focus an item, fire the action, repeat): blocked within one frame by the busy byte, and it would
  open one amount dialog per stack. Worse.
- Script route (guess-level signature): the Lua inventory binding registers `MoveItemOfClass`, doc
  `"inv guid, count, changeOwner"` (`FUN_1812b40b4`, impl `FUN_182abf1c8` → `FUN_182a1fa94(src, dst, classGuid,
  count)`). It moves by **class**, not by a specific item instance, so it can pick a different instance (condition,
  equipped). Only a fallback.

## Not checked
In game: nothing. Not read: what `FUN_1808d534c`, `FUN_1804cc9f8`, `FUN_1818523b4` do inside (split, merge, holder
change), the equip branch in `FUN_181f520f0`, the dialog body `FUN_1808ec58c`, the meaning of flag 6, the
`transfer_item_all` path, and the double-click / drag paths (`OnDoubleClicked` `FUN_182b13c80`, `OnStartDrag`
`FUN_18164f804`), which read the same vector.

---

# Follow-up: a "fast transfer" button (move the selection with no dialog, repeat while held)

Static analysis only, as above. The game XMLs were read from `IPL_GameData.pak`: `Libs/UI/UIElements/ItemTransfer.xml`,
`ApseInventoryList.xml`, `ApseModalDialog.xml` and `Libs/Config/defaultProfile.xml`.

## Bottom line
- No game path moves several items **without** the amount dialog. `FUN_1808ebdd4` opens it whenever the mode is 2 or
  3 and the total is not 1, and the no-dialog mode 1 has no caller. A total of 1 (one single-piece item) skips it.
- Auto-confirming from the movie does work. C++ only checks its own "dialog open" byte, which it sets **before** it
  calls `OpenAmountDialog`. It then reads `GetAmountDialogValue`, closes the dialog itself and runs the move.
  Send the confirm **one frame later** (onEnterFrame), not from inside `fc_openAmountDialog`.
- In ItemTransfer, `onDoubleClicked` runs the same transfer code as the transfer key, on the cached focus list.
  In the inventory screen, double-click is "action" (use / equip), not move.
- The inventory screen has no movie event that moves the whole selection. Drag and drop onto `horseShield` /
  `playerShield` moves one item only. To trigger the action by key, use `move_item` / `drop_item` in action map
  `apse_inventory`, or `transfer_item` in `item_transfer_first` / `item_transfer_second`.

## 1. onDoubleClicked
- The C++ handler is generic for every TreeList: `FUN_182b13c80` (registered in `FUN_1805646f8`). It runs only if
  `ClientId == list+0xb0`. If a callback is stored at `list+0x130` (impl pointer at `+0x168`), it calls it with the
  Ids string. Otherwise, if the string at `list+0x1b0` is non-empty, it passes that string to `FUN_182af9960(...)+0x58`
  vf0 with `(str, 0, 0)` (guess: shows it as a "you can't do this" notice).
- Setter: `FUN_180859dac(list, fn, reason)` stores `fn` at `+0x130`. When `fn` is empty it stores `reason` at
  `+0x1b0` instead.
- **ItemTransfer**: `FUN_18155d660` (the hint refresh, run on focus) checks the first focused item
  (`FUN_180563840`, `FUN_180560380`). If it can be transferred, it installs lambda `0b3a…` (op `FUN_181f7beb0`),
  which calls `FUN_1817419ec`: **the transfer key's function**. That leads on to `FUN_181741af0` → `FUN_1808ebd24`
  (mode 3) → `FUN_1808ebdd4`, then the amount dialog or `FUN_1808c43cc`. Otherwise it installs an empty callback
  plus the reason string.
  - The callback **ignores the event's Ids**. It reads the focus list at `list+0x98`, which the last
    `onFocusChanged` filled. The dirty flag isn't set, so GetFocusedItem isn't called.
  - So fire `onFocusChanged(joinedIds, false, false, clientId)` before `onDoubleClicked`. Several `|` ids are
    accepted, with the same rules as in sections 1-3.
  - Unlike the key lambda `FUN_181f7cba0`, `FUN_181f7beb0` doesn't check or set the busy byte.
- **ApseInventoryList**: `FUN_180560954` installs `FUN_1808ebce0` ("action", the use / equip lambda `ea8d…`, mode 0,
  amount 1). Confirmed: not a move.

## 2. The amount dialog (`C_UIModalDialogAmount`, screen + 0x318 = `param_1[99]`)
- Open, `FUN_1808ec58c`: stores the confirm callback (`+0x58`), the cancel callback and the listener (`+0xd8`).
  It registers the `modal_dialog_amount` action handlers ("action", "close", left/right/up/down, scroll) and
  turns that action map on. The map has `exclusivity="1"`, so it **blocks the inventory and transfer action maps
  while open**. Then `*(dlg+0xe0) = 1`, and only then the `OpenAmountDialog` invoke (`FUN_1808ecb60`).
- Confirm: `OnAmountDialogConfirmClicked` → `FUN_180b5bb38` (registered in `FUN_180c416ac`).
  ```
  if (dlg+0xe0) { GetAmountDialogValue -> float v;  sound;  FUN_180b5c990(dlg);  // close: +0xe0=0, CloseAmountDialog, map off
                  if (callback +0x58) callback(v); }
  ```
  The callback is lambda `2b1a…` (op `FUN_181f53910`): `amount = (int)(step_flag ? v / step : v)`, then
  `cb(items, amount)`. `items` is the vector **copied when the dialog opened** (`FUN_181f51230` copies `+0x48`).
- **C++ does not clamp the value.** `FUN_1808c43cc` caps each item at its own amount, so too large a value just
  moves everything. Two exceptions:
  - In the inventory screen, move-to-horse's horse-capacity limit exists only as the dialog maximum. A larger
    value overloads the horse.
  - The value is truncated to int: with a 0.1 step, send exactly `Max` (or `Max` plus a small epsilon) so it
    doesn't round down to n-1.
- Cancel: `OnAmountDialogCancelClicked` → `FUN_182b138fc` (same `+0xe0` gate, closes, cancel callback `+0xd0`).
- **Timing**: the movie's `fc_openAmountDialog` runs inside C++'s invoke. A confirm fired there would run the whole
  move nested inside that invoke, with Flash calls inside a Flash call. Reentrant, untested. Fire it on the next
  frame: `+0xe0` stays 1 until confirm or cancel. Nothing else has to be set first.

## 3. Inventory screen: starting a move without the key
- `OnGeneralEvent` (`FUN_18164f7d8`) and `OnDropActiveAreaChanged` (`FUN_182b13cf0`) only pass the event on to
  listeners (`list+0x70` / `+0x80`). No `move_item` call from either was found.
- Drag and drop: `OnStartDrag` (`FUN_18164f804`) splits the Ids on `|` and passes the vector to `list+0xf0`.
  `FUN_1811a2850` tells the movie which drop areas are active (`horseShield` / `playerShield`, from the item's side).
  The drop handler `FUN_1811a17f0(screen, id, area, …)` resolves **one** id (`vf+0xc8`). For `horseShield` /
  `playerShield` it calls `FUN_182aff4e8(screen, {item}, 1)`, the same move path, dialog included. **One item per
  drop.**
- Key route: action `move_item` / `drop_item` in action map `apse_inventory` (included from `apse_item_actions`;
  `drop_item` has `onHold` with a 0.25 s delay). For the chest: `transfer_item` in `item_transfer_first` /
  `item_transfer_second` (from `apse_transfer`). Handlers as listed in section 2 above.

## 4. Repeating every ~150 ms
- **Busy byte** `screen+0x21a` (= `list+0x1ba`): the key handlers check it and set it to `0x101`. It is cleared to 0
  by `FUN_180533900`, slot `+0x20` of TreeList vtable `0x183a81308`, which also runs queued callbacks and steps
  the tweens with `param_2`. Guess: per-frame Update, so at most one key action per frame. 150 ms is fine.
- **The dialog blocks the next action** (exclusive action map) until confirm or cancel closes it. Each repeat
  costs one open plus one deferred confirm.
- **Stale ids are the main danger.** Once the items have moved, repeating with the same ids either resolves
  nothing, or resolves items that now sit on the other side (unknown whether the membership check `+0x5a8` accepts
  both sides). An empty list crashes the transfer path (`FUN_1817419ec` reads `items[0]` unchecked), and items on
  the target side would be "moved" to the inventory they're already in. Refresh the focus or selection from the
  new list contents before every repeat, and stop when nothing is left.
- A native route with no dialog exists: `FUN_1819b157c(screen, target, vector<C_Item*>&, amount)` (target =
  `screen[0xbc]` in transfer, `+0xab8` / `+0xab0` in the inventory). It skips the dialog and the outfit warning.
  No game code calls it like that; untested.

Not checked: whether Scaleform delivers fscommands at once or queued (decides whether a confirm sent inside the
invoke runs nested), which caller drives `FUN_180533900`, and what `+0x5a8` accepts.

---

# Follow-up 2: shop basket (put the focused items into the basket in one go)

## Bottom line
- One function puts items into the basket: `FUN_1808ec0e8(screen, checkOutfit)`. The key, the double-click and the
  outfit-warning "Yes" all lead to it.
- It takes the full `|` id list. The first item decides the direction: in the basket → take out, else → put in.
- A multi-piece selection opens the amount dialog with **default 1**. The "confirm one frame later with exactly
  Max" trick fits it.
- Repeating with the same ids **toggles** them back out of the basket.

## 1. Entry points (all → `FUN_1808ec0e8`)
- Key: action `action` (map `apse_action`, keyboard bind, `xi_a`), included in action maps `shop_inventory` and
  `shop_trader` (defaultProfile.xml 1221-1235). The map names are passed in ctor `FUN_180bc8f88`. Registered in
  `FUN_1808c168c` on the map name at `screen+0x698`: lambda `43244…` → `FUN_181f90400` → `FUN_1808ec0e8(this, 1)`.
- Double-click: `FUN_180561098` (hint refresh) installs lambda `bc9a…` (`FUN_181f81010`) through `FUN_180859dac`.
  Same op `FUN_181f90400`.
- Outfit-warning "Yes": lambda `4b79…` → `FUN_181f90540` → `FUN_1808ec0e8(this, 0)` (skips the warning).
- `FUN_1808ec0e8`:
  ```
  if (busy +0x21a == 0) { busy = 0x101;
    if (!checkOutfit || !FUN_1808ec2c0()) {          // outfit warning, items[0] only
      items = FUN_180564e6c(this)                      // all '|' ids (refetched: dirty set)
      n = vf88(items)                                  // shop vt 0x183a81940: FUN_1808ec278 = sum of amounts
      if (n == 1) vfD8(items, !inBasket(items[0]), 1)  // no dialog
      else FUN_1808ec58c(dlg, min 1.0, max (float)n, def 1.0, step 1.0, confirm lambda a840…) } }
  ```
  The dialog arguments are from the disassembly at `0x1808ec1c8..0x1808ec23f`: `XMM1 = XMM3 = 1.0f`
  (`0x184099f44`), `XMM2 = cvt(ESI = n)`.
- vfD8 = `FUN_1808c42dc(this, items, toBasket, amount)` → `FUN_1808c43cc` over **every** item. Per item, to the
  basket: `FUN_181f915c0`: `FUN_1808d59a8(this+0x5f8, item, amt)` splits or moves the item, then
  `FUN_1804ca7ec(this+0x5d0, …)` selects it. It then refreshes the basket (`FUN_180393524`, `FUN_1808c44e8`).

## 2. The dialog
It uses the same `C_UIModalDialogAmount` object (`screen[99]`), so the gate and the confirm work as in Follow-up 1:
`dlg+0xe0`, then `FUN_180b5bb38`. Guess: the events come from ApseModalDialog.gfx in this screen.
Send value = Max, one frame later.
- The confirm op (`FUN_181f90d40` → `FUN_181f8a9f0`) **refetches the focus list at confirm time** with
  `FUN_180564e6c` and uses `(uint)value`. The focus must therefore still point at the same items when the confirm
  arrives.
- The direction comes from the `items[0]` captured when the dialog opened.

## 3. Repeating
- Busy byte: checked and set inside `FUN_1808ec0e8` for every entry point. At most one call per frame (guess,
  as above).
- **Toggle**: after a move, the items are in the basket, so the same ids take them out again. Refresh or stop
  after each step.
- `FUN_181f915c0` reads `result+0x50` **before** its null check. If `FUN_1808d59a8` fails (returns null), that is
  a crash. Keep amounts at or below each item's own count (`FUN_1808c43cc` already does) and never send stale ids.
- No money check was found on this path: the basket only stages items. Guess: money is checked at `close_deal`
  (not read).
