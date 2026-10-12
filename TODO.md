# Deagle mod — status (updated 2026-10-11 20:00)

## Fixed after the first test (built, install when the game is closed)
- Slide stuck back after reload: the aimed recoil (additive, layer 1) held its last frame forever; layer now cleared
- Aimed empty mag stays aimed (new ads_empty pose, slide locked); reload while aiming returns to ADS
- Inspect: spare round under the grip, slide pulled back, mag hanging (importer zeroed 30-frame sections) fixed
- White triangle: an old viewmodel saved into the save file (engine default pyramid); our entities are no-save now
  and stale ones are removed on load

## Built tonight, installed, NOT yet tested in game (first launch checks all of it)
- Native plugin v1.1 (dinput8.dll + deagle_native.asi in Bin\Win64MasterMasterSteamPGO): zero-jitter viewmodel
  (placed inside the camera update), Lua channel, MWII sounds via the game's fmod.dll, procedural bob / turn sway /
  sprint pose / landing spring. Log: Bin\Win64MasterMasterSteamPGO\deagle_native.log
- MWII sounds: 8 random shot variants + every draw / reload / empty reload / inspect cue timed to the animation
- Slide / mag / stray-round fix (unkeyed gun bones), ADS pose (right click; sights on the crosshair), additive recoil
  while aiming, slide locked back when empty (idle_empty), holster animation
- Own muzzle VFX (deagle.muzzle: MW19 flash bloom / front burst / flame tongues / sparks + smoke + barrel wisp),
  ejection-port smoke; hand-cannon effect and its gunshot no longer used
- Ammo HUD (bottom right, System.DrawText), falls back to info text
- Arms look switch: deagle_arms medieval | mw (default mw)
- Mock-engine tests: luajit tests/run_flows.lua, luajit tests/run_fallback.lua (0 errors)

## First launch checklist
1. Launch, load, press H: arms appear, draw sound plays, no jitter when turning/standing
2. Shoot: MW shot sound, new muzzle flash, casing + port smoke; 7th shot locks the slide back
3. R: reload sounds in sync; right click: ADS (sights on centre); shoot while aiming stays aimed; Y: inspect
4. Walk / sprint / jump: bob, lowered sprint pose, landing dip (deagle_motion 0 turns it off, 0.5 halves it)
5. If anything is off: send kcd.log + deagle_native.log lines; native off = native/install.ps1 -Remove

## Next
- Movement poses: walk / sprint / crouch / jump hand animations (today only procedural bob + sprint tilt; the hands
  themselves don't change). Need clips (MW19/MWII pistol sprint + walk) or hand-made poses blended by speed/stance
- Tune from the first test (offsets, motion strength, flash size, HUD position, sound volume)
- Own bullets: projectile travel + drop, tracers (native or Lua per-frame)
- Option B proper: Henry's own hand/sleeve meshes on the viewmodel skeleton (medieval material is the quick version)
- Real Henry arms (native skeleton post-process callback) — big, later
- .50 cal casing model from the MW19 pack; clean up old hold/grip code; clean-install test; knowledge-base note

## Done earlier
- Gameplay: fire, damage, hit reactions, blood + decals, recoil, reload, item, status
- MWII viewmodel converted and playing in game, camera-locked; DOF off while up; body + worn gear hidden; FP guard
