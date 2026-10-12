# Ballistics plan

Goal: bullets that fly (speed, drop, tracers) and **interact**: with each NPC's armor, their body, and the world.
Gun handling stays CS2's (done); each round's physics comes from Escape from Tarkov's ammo data (tarkov.dev public
API: speed, mass, drag, damage, penetration, armor damage, ricochet / fragmentation chance, tracer).

## Phase 0: probes (decide how much KCD2 already does for us)
- [ ] **Hand-cannon bullet**: find how KCD2's own firearm spawns its projectile (item + ammo + projectile class in the
      game tables and scripts; live: fire one and look at the spawned entity), then spawn that projectile from our gun with
      our speed and damage. If it works: KCD2's armor rules, body shove, knockdowns and AI reactions come for free.
- [ ] **Hit hand-off**: if we fly our own bullet, find how KCD2 delivers a projectile hit (arrows, bolts, hand cannon)
      so our bullet can pass its hit (position, direction, force, ammo stats) into the same pipeline. Lua first, then the
      native plugin (WHGame.dll: PlayRagdollHitReaction, wh_HitReaction_*, knockdownChance, hitStrength).
- Outcome decides: native projectile + our tracer / own flight + native hits / everything ours.

### Probe findings (offline, 2026-10-12)
- Firearms are MissileWeapon class 13 ("rifle", skill marksmanship: handgonneNormal01, handgunFancy01, hook guns),
  ammo classes 3 Ball / 4 Shotgun (shot_ball smash 250, shot_scatter smash 200, battle_shot stab 50), like bows.
- Projectiles are stock CryEngine weapon-system ammo: Scripts/Entities/Items/XML/Ammo/Arrow.xml (class Arrow; physics
  particle: mass 0.3, speed 60, gravity -9, water resistance, pierceability, trail effect, whiz sound, hitRecoil Level2
  / hitRecoilArmorMode Level1). No separate bullet file: guns fire the Arrow projectile with the ammo item's damage.
- Plan: our own Scripts/Entities/Items/XML/Ammo/DeagleBullet.xml (speed, mass, gravity, tracer trail, hitRecoil),
  then launch it: SpawnEntity of that class from Lua if it flies, else the native plugin calls the weapon system's
  ammo spawn + launch (find it in WHGame.dll like the camera hook).
- Live: list the projectile entity while a hand cannon round is in flight; try spawning the class from Lua.

## Phase 1: flight
- [ ] Projectile per round: position, velocity, gravity, air drag (ballistic coefficient), time of flight, max range
- [ ] Hit test each frame along the path (segment ray from last position to new position)
- [ ] Tarkov ammo import: tools script -> per-calibre table (.50 AE, 9x19, 7.62x39, 5.56...) like cs2.lua
- [ ] Each gun's calibre / default ammo in its profile; ammo types later (FMJ, hollow point, AP, tracer)
- [ ] Tracers: glowing streak + small light following the round; every round or every Nth; burn-out distance; colour
- [ ] Performance caps: rounds in flight, lifetime, decals and particles per second

## Phase 2: hitting people
- [ ] **Armor per body part** (your idea): read what the NPC wears on the hit zone (KCD2 layers: padding / gambeson,
      mail, plate, helmet) and its defense + condition
- [ ] **Penetration by energy** (your idea): each layer takes energy; enough left -> through (wound), not enough ->
      stopped. 9 mm vs heavy plate: likely stopped; .50 AE: through most plate; .50 BMG: through anything medieval
- [ ] **Exit wounds / over-penetration** (your idea): energy left after the body -> exit wound, bullet continues
      (can hit someone behind), blood out of the exit side (we already splatter behind targets)
- [ ] **Shallow angles** (your idea): effective thickness grows with angle; past a limit the round glances off ->
      dent on the armor, sparks, ricochet, no wound
- [ ] **Blunt trauma**: a stopped round still hurts and can knock down or wind (stamina) the target
- [ ] **Entry vs exit look** (your idea): different blood / tissue effect for entry and exit; armor shows a hole
      (penetrated) or a dent (stopped / glanced)
- [ ] **Armor damage**: rounds lower the piece's condition (KCD2 already tracks it); holes weaken it for later hits
- [ ] **Accurate hit zones**: use the physics part / bone the ray hit (head vs neck vs torso vs limbs) instead of the
      current height guess
- [ ] **Wound effects per zone**: head (helmet decides), torso (heavy bleeding), arms (drop weapon?), legs (limp, slow)
- [ ] **Impact force at the hit spot** (asked): shove that part of the body by the round's momentum; big hits stagger or
      knock down (KCD2's own physical hit reaction if Phase 0 finds it, else our own bone flinch in the plugin)
- [ ] Shields: wood stops small rounds, big rounds go through with splinters

## Phase 3: hitting the world
- [ ] **Sparks on metal** (your idea): armor, weapons, metal props; size by energy
- [ ] **Water** (your idea): splash column scaled by energy; bullets slow fast in water (about 1 m to stop)
- [ ] **Dirt / grass** (your idea): dust and debris kick scaled by energy
- [ ] Wood: splinters, penetrates planks / doors / fences with energy loss; stone: chips + dust, no penetration
- [ ] **Ricochets**: chance by angle + material (stone, metal, water at shallow angles); spawn the deflected round with less
      energy (tracer ricochets look great)
- [ ] **Bullet holes / dents** (your idea): decals per material (wood splinter hole, stone chip, metal dent / hole, cloth
      tear), with a cap so they don't pile up
- [ ] Impact sounds per material (thud, ping, splash, crack)

## Phase 4: feel and reactions
- [ ] Near-miss crack / whiz (mostly for when NPCs get guns), distant shots heard late (sound is slower than the round)
- [ ] NPC reactions: hearing shots, fleeing, taking cover, morale; KCD2's crime system already reacts to killings
- [ ] Balance decisions (below)

## Phase 5: gore tie-ins
- [ ] Head pop when a round goes through the head (Phase 2 decides "through")
- [ ] Exit-wound gore scaled by the energy left; dismemberment by big energy at limbs (native bone scaling)

## Cartridge-driven handling (user, 2026-10-12): one system instead of hand-tuned numbers
Each cartridge: bullet mass, muzzle velocity, powder charge. Each gun: mass, bore height over the grip (leverage),
support (one hand / two hands / shoulder stock), mechanism (slide or bolt + spring: spreads the impulse), muzzle device,
barrel length. Derived:
- recoil impulse p = bullet mass x velocity + powder gas (~1.5x bullet velocity); recoil velocity = p / gun mass,
  energy = p^2 / 2m (light guns snappy)
- muzzle flip = p x bore height around the support pivot (wrist / elbows / shoulder); push-back along the bore;
  side kick + twist small, by hand
- recovery = a spring-damper pulling back on target, stiffer with two hands / stock / crouch / leaning; later KCD2
  stamina + marksmanship skill. Full auto: kicks stack before recovery -> the spray pattern emerges
- flash size + brightness + its light + smoke + loudness from the powder charge (short barrel: more unburnt powder);
  damage + penetration from bullet energy (Phase 2)
- one calibration constant (physics -> screen motion), set against the user's Ready or Not recording
Games: Tarkov = per-gun vertical / horizontal recoil + ergonomics, modified by stock / muzzle / ammo (tuned stats);
Arma 3 = weight -> inertia, resting / bipod cut recoil; Ready or Not = procedural recoil with visible recovery.

## Ready or Not reference (from another session's check of the user's install, 2026-10-12)
- Paks are legacy .pak v11, NOT encrypted (EncryptionKeyGuid zero, index unencrypted), no anti-cheat strings: FModel
  (UE 5.3 + a Mappings.usmap) reads them; weapons under ReadyOrNot/Assets/Weapons/<Gun>, anims export as .psa; sounds
  are FMOD banks (Content/FMOD/Desktop/weapons.bank). Guide: https://claude.ai/artifact/JBEUcq8rpza61kB4y2Hoaj
- Recoil (property names only, values unread): procedural rotation + translation kick (ProcRecoil_Rot / _Trans) with
  buildup over sustained fire, first-shot strength, pitch / yaw multipliers, damping, partial return to aim
  (RecoilReturnPercentage / Rate / InterpSpeed), randomness, ADS multiplier, movement multiplier, per-attachment
  modifiers. Looks per-gun hand-tuned, not derived from caliber or mass -> use its model shape (kick + buildup +
  damped partial return) and derive our parameters from the cartridge; read one gun's values to calibrate.
- Ballistics: real projectiles (speed, drag, gravity scale), damage falloff, penetration by material depth / density,
  ricochet chance, armor-piercing, armor coverage / level, an ammo data table per caliber: same shape as our plan.

## Decided
- **Ready or Not guns keep Ready or Not's feel** (user, 2026-10-12): our recoil model uses Ready or Not's parameter set
  (fire / first-shot strength, angle strength, pitch / yaw multipliers, buildup, damping, return percentage / rate /
  interp speed, randomness, ADS multiplier, movement multiplier). Guns imported from Ready or Not use their own values
  1:1 (read from their files); every other gun derives the same parameters from the cartridge + mass + leverage, scaled
  to Ready or Not's range. Their exact math isn't in the files (names only): match it against the user's recordings.
- **Small rounds bounce off plate** (2026-10-12, for fun, even though a 9 mm would often get through): plate deflects
  anything under an energy threshold with sparks, a ping and a ricochet; big rounds (.50 AE and up) go through.
- **Shove = momentum (mass x speed)**; penetration and wound size = energy (1/2 x mass x speed^2). Real momentum barely
  moves a person (.50 BMG: ~0.5 m/s on 80 kg), so the shove uses momentum for the ratio between rounds, times a
  tunable "punch" multiplier. 9 mm 2.9 / .50 AE 9.1 / .50 BMG 38 kg m/s; 520 J / 2,100 J / 17,000 J.

## Decisions for you
- Realism vs fun for everything else: how fast should guns kill unarmored and lightly armored people?
- Tracers on every round (looks great) or every 3rd-5th (realistic)?
- Ammo: limited and scarce, or unlimited like now?
- Guards reacting to the noise of a gunshot?
