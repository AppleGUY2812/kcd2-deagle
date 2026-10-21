# Muzzle flash: Ready or Not vs ours, from the user's slow-motion clips (2026-10-20)

Clips (user's, private): `Desktop\Deagle Progress\mkv sound clips\`
- `2026-10-20 21-11-46.mkv`: Ready or Not, Grizzly, RonSlowMo, 22.3 s, 1080p60, ~8 shots.
- `2026-10-20 20-43-01.mkv`: KCD2, our Grizzly, Deagle slowmo (cfg.slowmo 0.01), 9.2 s, 2 shots.
- `2026-10-20 20-48-58.mkv`: Ready or Not at normal speed (flash = frame 83 only, 1 frame).
**Correction (Claude 4, 2026-10-20 21:2x): the two clips are NOT at the same speed.** RonSlowMo ran at ~0.05 (RoN's
0.04 s fireball is on screen ~46 frames), ours at deagle_slowmo 0.01, so everything of ours is ~5x longer on screen.
Points "Ours 1" (size) and "Ours 2" (envelope) below were judged across that mismatch: RoN's fireball is about as big
on screen as ours (2/3 of a 900 px crop), and most of the "holds ~120 frames" is the 0.01 speed. The orange haze also
shows in RoN while its light is on (#376-410). The real differences (Claude 4's list): RoN re-draws the flipbook frame
and core colour every rendered frame (ours: 2 baked stages held still); RoN's light fades with the particle and
re-rolls brightness every frame, on 80 % of shots (ours: full then off); flashChance 1 in the user's settings vs RoN
0.65 removes the smoke-only shots; smoke is already RoN's (deagle.muzzle_smoke_ron). Compare next at 0.05 on both
(`deagle_slowmo 0.05`; RonSlowMo starts at 0.05).

User verdict: "all the flashes are different, some shots had no flash just the smoke, and ours is completely off;
even the heat distortion from the barrel you didn't add".

## Ready or Not (frame numbers in 21-11-46.mkv)
1. **Every shot is different.** Seen: smoke only, no fire at all (#272-294, #521-528); a red-orange glow over the
   whole scene with a small star-burst core (#376-465, #854-916, #975-1072); a sparks-then-fireball (#720-805);
   a small hot core with haze (#622-688). So the effect picks layers / variants per shot at random.
2. **Sparks first.** #720 sequence: a cone of tiny bright embers leaves the muzzle before / with the fire.
3. **Fire core = flipbook with radial streaks.** Hot white-yellow centre to orange-red edge, spiky fibrous edges,
   a new shape every few frames, sometimes two lobes (#774). Not a smooth round puff.
4. **Star-burst variant.** #376 sequence: thin radial spikes (a star), repeated / flickering, small.
5. **Light.** A strong red-orange light hits the scene (ground, pumps, canopy, the casing) at the start and fades
   fast; in the normal-speed clip the whole scene turns orange for exactly one frame (#83 of 20-48-58).
6. **Smoke.** Grey-white, thin, fills the area slowly; never orange except while the flash light is on.
7. **Heat distortion** from the barrel (user): refraction haze after the shot; not visible at 480 px here, needs a
   full-res look near the muzzle.
8. The casing is lit by the flash (orange while the light is on, brass after).

## Ours (20-43-01.mkv, frames at 60 fps)
1. One look for every shot: a big soft round peach-white cloud, ~3x the pistol's screen length (#160-#320).
2. Envelope wrong: grows for ~40 frames, then holds near full brightness ~120 frames (~27 ms game time at 0.01),
   instead of peaking at once and decaying fast.
3. No sparks, no streaky/spiky edges, no hot core gradient, no star variant, no smoke-only shots.
4. Smoke stays orange and covers half the screen (#60-#140).
5. Scene light barely changes the ground/trees.
6. No heat distortion.

## To do (owner of flash.lua / the flash assets: 🟪)
Rebuild from Ready or Not's actual effect definition (its Niagara system for the Grizzly: emitters, spawn
probabilities per shot, sizes, lifetimes, colour curves, flipbooks, the light's colour/radius/curve, the distortion
emitter), then compare in slow motion side by side with the same speed factor.
