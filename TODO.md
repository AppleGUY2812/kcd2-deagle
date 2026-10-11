# Deagle mod — what's left (updated 2026-10-11 03:50)

Legend: [live] = no game restart needed · [restart] = needs a restart (batched together)

## Now (in progress)
- [live] Jitter when turning: ride Henry's Head bone instead of placing per frame (calibration needs ~10 s in game, focused)
- [live] Hide Henry's own body while the Deagle is out, keep his shadow (option A) — test
- [live] Shrink the viewmodel toward the camera so it doesn't clip into walls (same image) — test scale
- [live] Inspect (Y) can be replayed — fixed, verify
- [live] Casings take different paths — fixed, verify

## Next restart batch
- [restart] Slide out of place at idle + stray bullet in reload: bones a clip doesn't animate fall back to a wrong
  default pose; fill them with the model's real default pose and re-export
- [restart] Sounds: MWII shot (8 variants) + reload / empty reload / inspect / draw foley, timed to the animations
  (KCD2 plays FMOD banks: build a bank, load it)
- [restart] ADS: generated "arms extended, sights on centre" pose (Blender IK from idle) + additive recoil so shooting
  while aimed stays aimed; right click blends in/out (~0.15 s) with the existing zoom
- [restart] Own muzzle flash + smoke (replace the hand cannon effect), light flash already works

## After that
- Ammo HUD (non-janky, KCD2 style)
- Holster animation (draw reversed), slide locked back on empty, walk/sprint bob, small sway
- Option B: viewmodel sleeves/gloves retextured to a medieval look (match Henry)
- World model in Henry's hand for 3rd person / NPCs seeing it (optional)
- Remove the old Henry-arm hold code (grip kit / TACZ path kept as tools only)
- Clean install test from a fresh save, then final MODLOG + knowledge-base note

## Done
- Gameplay: fire, damage, hit reactions, blood + decals, recoil, reload, item, status text
- MWII viewmodel converted (model, 8 animations, textures) and playing in game, camera-locked

## Native plugin (decided 2026-10-11): deagle_native.asi via Ultimate ASI Loader (dinput8.dll) in Bin\Win64MasterMasterSteamPGO
- v0 (built, ships next restart): finds gEnv, hooks the camera frustum update (final first-person camera), logs to deagle_native.log
- v1: place the viewmodel inside the camera hook (zero jitter) — needs IEntity move + entity lookup from WHGame.dll (RE in progress)
- v2: Lua <-> native channel; MWII sounds played through the game's own fmod.dll; own bullets (projectiles, drop, tracers); HUD
- Uninstall: native/install.ps1 -Remove (or delete dinput8.dll + deagle_native.asi)
