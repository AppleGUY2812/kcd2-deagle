# MoreSlots (Ready or Not) rebuilt for the current game

`build_moreslots.py` rebuilds ExsaitX's "MoreSlots1000" mod from the game's own current files. No game assets are in
git; the build output lives in `E:\Tools\deagle-work\ron\moreslots_rebuilt\`.

## What the mod changes
One property on the class defaults of the four body-armour blueprints in
`ReadyOrNot/Content/Blueprints/Items/ArmorRevised/`: `SWATArmour.TotalSlots` (the loadout's slot budget).

| Blueprint | Game (build 25784851, 2026-10-13 update) | Mod |
|---|---|---|
| Armor_Heavy_V2 | 11 | 1000 |
| Armor_Light_V2 | 13 | 1000 |
| Armor_NoArmor | 9 | 1000 |
| Armor_StabVest | 15 | 1000 |

The old pak (`pakchunk99-Mods_ExsaitX_MoreSlots1000_P.pak`, 07-18 build) changed nothing else in the `.uexp`s (the
4-byte value plus one byte of the unversioned-property header). That header byte is why it broke: in the old file
`TotalSlots` sits one property index earlier than in the current class (a property was added ahead of it since), so
the old data is read into the wrong fields. Its `.uasset`s are also the old ones (an extra `EnumProperty` name).
Decoding the old files against the current layout gives junk fields (`bDisableAnimInstanceWhenNotEquipped`,
`Children`, ...).

## How the rebuild works
1. Extract the 4 packages (`.uasset` + `.uexp`) from the vanilla `pakchunk<N>-Windows.pak` files with `../ronx.py`
   (mod paks are ignored).
2. Decode the class defaults (`../uasset.py`, `../dump.py`, `Mappings.usmap`); it must decode byte-exact.
3. Find the `TotalSlots` int32: every offset in the class-defaults export holding the current value is patched on a
   copy and re-decoded; exactly one offset must change `TotalSlots` and nothing else. The `.uasset` is copied
   unchanged (same sizes, so no offsets move).
4. Pack with repak 0.2.3: pak v11, uncompressed, no encryption, mount point
   `../../../ReadyOrNot/Content/Blueprints/Items/ArmorRevised/` (the old mod's), 8 files.
5. Verify: pak version / mount / index-not-encrypted, `../pakls.py` listing = the old mod's listing, re-extract with
   `ronx.py` and compare byte for byte with the staged files, re-decode every export and diff against the game's
   files (the only difference allowed is `TotalSlots`). Results go to `build_report.json` (values, offsets, SHA-1s).

## Rebuild after the next RoN update
```
python3 -I ~/kcd2-deagle/tools/ron/moreslots/build_moreslots.py /mnt/e/Tools/deagle-work/ron/moreslots_rebuilt [--slots 1000]
```
Needs: repak at `/mnt/e/Tools/repak-v0.2.3/repak_cli-x86_64-unknown-linux-gnu/repak` (github.com/trumank/repak
release v0.2.3, `--repak` to override), `~/.cache/kcd2-deagle/libooz.so` (`../build_ooz.sh`), the usmap at
`/mnt/e/Tools/deagle-work/ron/exporter/Mappings.usmap` (`--usmap`). It stops with a message if:
- "class defaults decoded x/y bytes": the update changed the native class layout; get a new `Mappings.usmap`
  (docs/ron_extraction.md section 1).
- "TotalSlots is 0/None": the game no longer serializes the value in that blueprint (it inherits it); the property
  would have to be added to the unversioned header, which this script doesn't do.
- "expected exactly one TotalSlots offset": the value is ambiguous; look at it by hand.

Install (only with the game closed): copy `pakchunk99-Mods_MoreSlots_Rebuilt_P.pak` into
`ReadyOrNot/Content/Paks/`, and keep the old `pakchunk99-Mods_ExsaitX_MoreSlots1000_P.pak` and
`pakchunk9999-ArmorRevised_125_slots_gc_patch_P_v152.pak` off (`.pak.off`): they override the same files.
Other mods that override these four blueprints would conflict the same way.
