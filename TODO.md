# Deagle mod — status (updated 2026-10-12 morning)

## Working (user-confirmed in game 2026-10-12)
- Muzzle flash glued to the muzzle on every shot, both guns (plugin-spawned emitter, bounding box put back after each
  move; MODLOG "why every glued flash vanished"). Deagle muzzle bone fixed in every clip.
- Kick feel: Deagle 7.1 deg a shot (above Ready or Not's .357), Glock 3.5, gun model kick, at most 60 % springs back;
  Glock louder (0.83x the Deagle). Settings: docs/DEBUG.md (Desktop "DEBUG TOOLS.md").

## Installed 2026-10-12 (md5 checked: pak 6e51280c, plugin b528da07), user check pending
- Clean flash build (diagnosis switches removed), muzzle light reach 7 m / 21 m (inverse-square), kick / spring-back /
  loudness defaults, torch held off while the gun is out (live in the running game via hot reload only), dev-reload
  gun loading fix.
- After the install: R with a gun out = reload, never the torch; flash lights the room (Glock smaller than the Deagle).

## Installed 2026-10-12 afternoon (pak 893c4450, plugin 4fbb1f55): live checks pending
- Bullets: flight / drop / drag, tracers, our impacts + holes + sounds per surface, invisible blockers / foliage passed,
  wallbang + exit holes, ricochets, armour per worn layer (9 mm vs plate by range + angle), through-and-through, hit
  zones from bones, Ready or Not blood, hit reactions (plugin), kill shove, head pop, size-scaled wounds, AI hearing.
- Handling: CS2 accuracy (stand / crouch / move / air / fire penalty), flash 3-frame bloom, slow motion slows our
  sounds, weapon movement layer (sprint / jump / land / crouch / dodge / strafe; roll-sign check first).
- Debug: F10 zoom, deagle_paths, deagle_set booleans / lists, docs page https://claude.ai/artifact/2pJ2zJeNNAfxa5MD9VyitH
- Checks: tracer look; holes / wallbang on a plank; Glock vs a bandit in plate (bounce far, through close); flinch
  direction; crouch vs run accuracy; flash snappiness; deagle_mparam test_roll 10 (top tilts left?); sprint / jump;
  head pop on a Deagle head kill; kill shove; impact sound levels; guards hearing shots.

## Inventory mod (user's plan 2026-10-26, in this order)
1. Split: move the inventory features out of the Deagle mod into their own mod, then clean up. Today they are
   `tools/ui/weightmap/` (build) -> `src/Libs/UI/{ApseInventoryList,ItemTransfer,ApseModalDialog}.gfx` + `UIElements/*.xml`,
   `src/Scripts/Deagle/fastmove.lua`, plugin `fastkey` / `mods` (native/src/main.cpp) and `invtime` (native/src/invtime.cpp),
   the `ui_tip_deagle_weightmap` strings in `loc/english/text__deagle.xml`, docs `inventory_load.md`,
   `whgame_re/inventory_multiselect.md`, DEBUG.md rows.
2. Search box with fuzzy search (port a small scorer such as fzy's to ActionScript 2; first check the inventory screen
   lets a text field receive typed keys).
3. More filters.
4. Sort by stat (charisma, armour, ...): the list's item entries carry no stats today (Id, Name, Category, Weight,
   Stolen...), so the values need a source first.
5. Equip best for a stat (user 2026-10-26): one click wears the carried set with the most charisma / armour / ...
   Needs step 4's stat source plus which slots / layers each piece takes, so pieces that can't be worn together are
   never picked together. Then equip through the game's own path, or build it as one of the game's outfits.

## Next (user's list 2026-10-12, ordered: each builds on the one before)
1. Bullet system v1 (BALLISTICS.md): a real projectile (speed, drop, drag), tracer, impacts per surface (holes / decals,
   sparks, dirt, water), armor vs flesh vs clothes (plate deflects small rounds), knockback from momentum.
2. Hit reactions: live NPCs flinch / stagger by momentum and hit place, dead ones ragdoll with the bullet's push.
3. NPC awareness: guards hear a 9 mm at ~80 m (bigger guns farther), fear / flee / alarm through KCD2's crime system.
4. Weapon movement (Ready or Not style, their numbers in docs/ron/): walk sway from WASD, sprint pose, jump / land,
   crouch, dodge, inertia drag (InertiaDragAim / Strafe), spread from movement, recoil pattern buildup. Generic layer
   for every gun; Ready or Not imports bring their own sprint / jump animations on top.
5. Gore and injuries (wounds, blood, limb damage) on KCD2's injury system; gun UI polish.
- Ready or Not importer (gun + animations + arms + its recoil values 1:1) after the bullet system, so imported guns
  plug into it.
- Live check of KCD2 functions, then stamina / skill / stance / moving in the grip hold; flash light from the simulator
  directly.
- ARC9 MW2019 .50 GS.
- Ready or Not flash set per gun, not per blast (Claude 4, 2026-10-18): RoN picks it per weapon (G19 -> V2, Grizzly ->
  V3); we pick by blast (flashRon.large / small at physics.largeFlashBlast 1.5). Same result for the Deagle and Glock,
  but a third gun near 1.5 would flip with the cartridge. A per-gun field (guns/<gun>.lua flashRonSet) would match RoN
  and let each .cdf carry only its own set (64 cards, not 96: tools/flash_skin.sh / flash_cdf.py per gun).
