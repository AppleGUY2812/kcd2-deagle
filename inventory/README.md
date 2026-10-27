# Inventory Tools for Kingdom Come: Deliverance II

A small mod of its own for KCD2's inventory screens (1.5.x), split out of the gun mod in the repo root on 2026-10-26:

- a **weight map**: every item in the list as a box sized by its total weight (weight x count), a WizTree-style view;
- a **total weight column** next to the game's per-piece weight, with its own sort;
- **multi-select** in the map and the list (drag a box, Ctrl-click, Shift-click, Ctrl+Shift-click);
- a **stolen filter**, **All stolen** and **Hide worn** buttons;
- **G fast transfer**: moves the selection (or the item under the mouse) at the full amount with no quantity box,
  and repeats while held;
- a **faster inventory / map screen**: skips the Apse "clothes ready" gate (`invtime`).

Without the native plugin (`inventory.asi`) the movies, the weight map and the list still work; only the G key and the
inventory-open speed need it (KCD2's menus pass Lua no keys, and the gate hook is native).

## Build and install

```
bash inventory/tools/weightmap/build.sh   # rebuild the 3 patched movies from the game's paks (needs Java + JPEXS)
bash inventory/build.sh                   # pack dist/invtools (data/invtools.pak, Localization/english_xml.pak)
bash inventory/build.sh --install         # ... and copy Mods/invtools + Bin/.../inventory.asi (game must be closed)
native/build.ps1                          # build native/dist/inventory.asi (MSVC, Windows side)
```

`inventory/tools/weightmap/build.sh` reads `Data/IPL_GameData.pak` (only reads it; `KCD2_DIR` overrides the game
folder) and writes `inventory/src/Libs/UI/{ApseInventoryList,ItemTransfer,ApseModalDialog}.gfx` and their
`UIElements/*.xml`. It needs `ffdec-cli.jar` (`FFDEC=path`, default `~/.local/share/ffdec/ffdec-cli.jar`).
`build.sh` packs `inventory/src` as a **stored** zip (KCD2 requires it) into `data/invtools.pak`, plus the
localization pak and the manifest. `--install` refuses while `KingdomCome.exe` runs.

The plugin's settings and log live next to the `.asi` in `Bin/Win64MasterMasterSteamPGO/`:
`inventory_settings.txt` (kept settings, replayed as `inv_set` lines at every load) and `inventory.log`.

## Keys and commands

| Key / command | Does |
|---|---|
| the weight-map icon in the sort row (between A-Z and the first column icon) | show / hide the map; again returns to the list |
| **Ctrl-click** / **Shift-click** / **Ctrl+Shift-click** in the map | add or remove one box / select the rectangle from the last clicked box / add that rectangle |
| drag in the map | select every box the rectangle touches |
| the stolen-glove icon | show only stolen items, in the list and the map (the map keeps the selection rules) |
| the two-glove icon (**All stolen**) | select every stolen item, then **Move**, **Drop** or **G** act on all of them; pressed again it clears |
| the shield icon (**Hide worn**) | hide everything worn in any outfit, in the list and the map |
| the weight icon next to the total-weight column | sort by total weight (again for ascending); the game's own saved sort config is untouched |
| **G** / `inv_fastmove_key <key>` / `inv_fastmove_key off` | fast transfer, default **G**: the map's selection or the item under the mouse, full amount, no quantity box; hold to repeat (first repeat after 0.4 s, then every 0.15 s). Inventory: to the horse (or back from the horse tab); the plugin presses the game's move key **X**. Chest / horse / loot: across to the other side. Shop: into the basket (only items not in it yet; held, it steps to the next item). Items worn in the current outfit never go as part of a group. The key is kept |
| `inv_invtime 0` / `1` / `2` / `3` / `4` | inventory / map open speed. **2** (default) skips the "clothes ready" gate: measured open -> fade in 1.12 s -> ~0.65 s over 5+ opens; `1` logs the steps only, `0` off, `3` also skips the readiness wait (test, no gain over 2), `4` makes the readiness service answer ready (test, no gain). Kept. The plugin log (`inventory.log`, `invtime` lines) gets the time of each step after the key. Save first for 2-4: Henry may show missing clothing for a moment |
| `inv_set <key> <value>` | internal: what the plugin replays from `inventory_settings.txt` at every load |
| `inv_native_ok` / `inv_fastkey` / `inv_mods` | internal: the plugin's handshake / its read of the fast-transfer key (1 press / repeat, 0 release) / its read of Shift (1) and Ctrl (2) for the map's multi-select |

The **G** path, from the game's code: KCD2's C++ opens a quantity dialog for any multi-piece move and only checks its
own "dialog open" byte before calling `OpenAmountDialog`, so the movie confirms it one frame later with exactly the
maximum. A `|`-joined id list is what the game's move / drop / transfer loops act on (see
`docs/inventory_multiselect.md`). Never an empty list, no duplicates, one side only, and no worn item in a group.

## Logs and tests

- KCD2's `kcd.log`: `[Inv] fastmove:` lines (screen open / closed, the map's build time, `shop:` when the shop path
  ran) and `$4[Inv]` for a failed script load.
- `inventory.log` next to the `.asi`: the plugin's commands, key reads, settings and `invtime` steps.
- `luajit inventory/tests/run_inventory.lua`: the Lua side without the game (commands, handshake, the key -> movie
  calls). `bash tests/run_all.sh` runs it with everything else.
- `node inventory/tools/weightmap/test_layout.js`: the map's treemap code in Node (boxes inside their rectangle, no
  overlaps, areas proportional to weight). `--dump <json>` writes a layout for `mock_render.py`.

## Docs

- `docs/inventory_load.md`: why the inventory / map screen takes ~1-2 s, what the `invtime` hook changes, and the
  measurements behind it.
- `docs/inventory_multiselect.md`: what C++ does with a `|`-joined id list (move, drop, transfer, amount dialog) and
  the rules the movie follows.
