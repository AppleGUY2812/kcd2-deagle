# Cartridge-driven handling, part 2: design

Draft for discussion (2026-10-12). **Nothing in this document is built.** It extends the first version of the simulator
(BALLISTICS.md, "Cartridge-driven handling"; code in `Deagle:ShotPhysics`) to six more things: how far the aim comes
back after a shot, sideways recoil, the flash's light, damage, fire rate and loudness.

How to read it:
- Every section has the same parts: what decides it in real life, the formula, what it needs and where that comes from,
  the tuning knobs, what would change in which file, and the risks.
- Units are SI (kg, m, s, J, N s). "≈" marks an approximation. "Unverified" means a game or engine function we have not
  yet seen work in KCD2.
- The worked numbers are for our two guns (Deagle .50 AE 6", Glock 17 9 mm) from `guns/calibers.lua`.

## Recommendations

1. **Return share (now a fixed 65 %)**: replace it with an *elastic hold*. Each way of holding the gun has an elastic
   range (in camera degrees). The part of a kick inside the range springs back by itself; the rest stays and you pull it
   down. A Glock shot comes back about 95 %. A calm Deagle shot comes back 65 % (today's value), but only 20-45 % when
   you fire fast. Full-auto climb then happens without any extra rule. Stamina, Marksmanship, stance, aiming and moving
   all widen or narrow the range.
2. **Horizontal recoil**: give each gun a fixed drift direction (from its muzzle device, or for rifles from the stock
   sitting in the right shoulder) plus a random part sized by the kick and the hold. Bullet spin (rifling torque) is
   under 1 % of the flip, so leave it out. Add a roll channel to the gun-model kick in the plugin. Full-auto patterns
   come from the elastic hold filling up, the drift adding up, and an optional springy body sway.
3. **Flash light (your question)**: **drive it from the simulator as its own light.** Use a dynamic light in a spare
   slot of the flash entity, which the plugin already keeps glued to the muzzle, and remove the light from the particle.
   The flash sprite's size, the light's radius and the light's intensity all scale with one number, the cube root of
   the energy of the gas leaving the muzzle, and the light follows the same time curve as the sprite. If the light
   can't be animated in KCD2, the fallback is lights inside the particle, one particle variant per powder class.
4. **Damage**: make health damage proportional to the square root of the energy the bullet leaves in the body, using a
   simple tissue-drag model. One number is tuned by hand: a 9 mm torso hit takes 35 HP. The .50 AE then comes out about
   1.8 times the 9 mm, the same ratio CS2 uses (1.77). Use real air drag for range loss: about 8 % less damage at 50 m,
   where CS2's falloff gives 47 % less. The same function becomes the last step of the Phase 2 bullet, which subtracts
   armour energy before it.
5. **Fire rate**: keep a mechanical limit per gun: its published cyclic rate, or else the mod's cycle time
   (0.08 / 0.075 s, close to real cycling speed). Drop CS2's 0.225 / 0.15 s for guns that have cartridge data. Let
   recovery limit how fast you can usefully shoot: the bullet leaves along the barrel, which is still flipped up from
   the last shot. A Deagle fired every 0.08 s throws its shots about 4° high, and nothing else needs to cap it.
6. **Loudness**: work out the muzzle-blast level from the same muzzle-gas energy. The formula is +(20/3)·log10 of the
   energy ratio, in dB. It matches the measured 9 mm vs .357 Magnum gap to within 1 dB. The Deagle comes out about
   6.5 dB louder than the Glock. First even out each sound file's own loudness, then play the physics difference at
   about half strength. A suppressor takes off 15-28 dB. The AI hearing radius uses the same dB value later.
7. **Small fix in today's code**: SAAMI's powder-gas factor for pistols is **1.5**. For rifles it is 1.75, and
   `ShotPhysics` uses 1.75 for every gun. Switching by `hold` lowers the Deagle's impulse by 2 % (10.15 to 9.93 N s).
8. **One new shared number in `ShotPhysics`**: `gasEnergy`, the energy that leaves the muzzle as hot gas. Flash size,
   light and loudness all come from it, so they always stay consistent with each other.

---

## 0. The shared numbers

What `ShotPhysics` computes today, plus the two additions this design needs.

| Quantity | Formula | Deagle .50 AE (6") | Glock 17 9 mm (4.5") |
|---|---|---|---|
| bullet speed v | v_ref · (barrel / ref barrel)^0.25 | 442 m/s | 360 m/s |
| bullet energy E_b | ½ m_b v² | 1,899 J | 521 J |
| recoil impulse p | m_b v + m_powder · f_gas · v (f_gas: pistol 1.5, rifle 1.75, SAAMI) | 9.93 N s (10.15 with 1.75) | 3.05 N s |
| powder gas share of p | | 13 % | 5 % |
| free recoil | v_r = p / M, E_r = p² / 2M | 4.84 m/s, 24 J | 3.37 m/s, 5.1 J |
| gun + hand inertia I | M · 0.1² + 0.004 kg m² | 0.0245 | 0.0131 |
| muzzle flip ω | p · bore height / I | 20.3 rad/s | 7.0 rad/s |
| **new: muzzle gas energy E_gas** | m_powder · Q − E_b, Q ≈ 4.5 MJ/kg | ≈ 7,100 J | ≈ 760 J |
| **new: blast scale G** | (E_gas / E_gas,9mm)^(1/3) | 2.11 | 1.00 |
| today's flash scale | 0.476 · (powder g)^(1/3) · (ref/barrel)^0.5 | 0.60 | 0.30 |

- **E_gas**: Q is the heat the powder releases when it burns. It is about 4-5 MJ/kg for double-base pistol powders
  (≈, a typical value, not measured for these loads). Whatever doesn't end up in the bullet leaves the muzzle as hot,
  high-pressure gas. That gas makes the flash and the bang. A short barrel gives the bullet less of the energy, so more
  leaves as gas. That is already inside this formula, so no separate barrel factor is needed.
- **Blast scale G**: in physics, blast waves and fireballs grow with the cube root of their energy (Hopkinson-Cranz
  scaling). Today's hand-made flash ratio (0.60 / 0.30 = 2.0) is already almost the same as G's ratio (2.11), so moving
  to G changes nothing visible for these two guns. It just gives the number a physical meaning.
- Only ratios matter (everything is divided by the 9 mm reference), so the exact value of Q matters little.

---

## 1. Recoil return share

### What decides it in real life
- Each shot turns the gun and hands upward around the wrist (pistol) or the shoulder (rifle). Muscles hold a joint
  like a spring for small movements. This is called *short-range stiffness*. Past an *elastic limit* that stiffness
  collapses (the muscle fibres slip) and the joint simply gives way (Milner et al. 1995). A relaxed wrist is only about
  3 N m/rad stiff; gripping and tensing raise that a lot.
- **The part of the kick inside the elastic range springs back by itself. The part beyond it stays** until the shooter
  re-aims. In the game, that means you pull the mouse down.
- What sets the elastic range:
  - the hold (two hands > one hand; a stock in the shoulder is different again);
  - stance (braced or crouched > standing > moving);
  - grip tension (aiming down sights means a tighter hold);
  - fatigue (tired muscles give way earlier);
  - technique (skill).
- **Follow-up timing**: a shot fired while the last kick is still springing back finds the spring partly loaded
  already. There is less room, so more of the new kick stays. This is why fast fire climbs and full auto climbs a lot.
- **Return speed**: spring stiffness against the inertia of the gun and hands. A heavier gun comes back slower; tired
  arms come back slower.

### Model: an elastic hold, in camera degrees
This keeps today's structure in `PhysicsRecoil` / `CamRecoilTick`: a returning part T plus a permanent part P.
Only the split between them changes.

```
up    = camPerFlip · ω · (1 ± vary) · kickMult(ADS, moving)        camera kick, degrees (today's formula)
room  = max(0, θ_hold − T)                                          elastic range still free
back  = min(R_max · up, room)                                       springs back by itself
stays = up − back                                                   you pull this down
T += back;  P += stays;   every frame: T ← T · e^(−λ·dt)            (today's decay)
return share of this shot  R = back / up

θ_hold = θ_ref · H(hold) · S(stance, moving) · A(ADS) · F(stamina) · K(skill)
λ      = λ_ref · √(I_ref / I) · F(stamina)            heavier gun + hand: slower return
F(stamina) = 0.6 + 0.4 · s        s = current / max stamina (0..1)
K(skill)   = 0.8 + 0.4 · k        k = Marksmanship / max level (0..1)
```
Mechanically, this is an elastic-perfectly-plastic spring: it behaves like a spring up to the limit and gives way past
it. R_max ≈ 0.95 stands for the small error a perfect shooter still leaves.

Worked numbers. θ_ref = 2.9° makes a calm, two-handed Deagle shot return 65 %, today's value. λ = 5/s (today), and
√(I_ref/I) gives the Glock 6.8/s.

| Situation | Deagle (kick 4.4°) | Glock (kick 1.5°) |
|---|---|---|
| single calm shot, two hands | 65 % back | 95 % back |
| second shot 1.0 / 0.5 / 0.3 s later | 65 / 60 / 50 % | 95 % |
| second shot 0.225 s later (CS2's rate) | 44 % | 95 % |
| second shot 0.08 s later (the mod's rate) | 21 % | 95 % |
| 7 shots, 0.08 s apart | ≈ 22° climb left for you to pull down (+ 3° that returns) | |
| Glock 18, 17 rounds at 1,200 rpm | | ≈ 10° climb + 3° that returns (R drops to 54 % from the 4th round on) |
| one hand (H = 0.5) | 33 % | 94 % |
| exhausted (s = 0), two hands | 39 % | 95 % |
| one hand and exhausted | 20 % | 56 % |
| aiming (A = 1.3) / crouched (S = 1.2) | 84 % / 78 % | 95 % |

### Inputs and where they come from
| Input | Source |
|---|---|
| flip ω, inertia I | `ShotPhysics` (calibers.lua + gunPhysics) |
| hold | `gunPhysics.hold` (add `"pistol1h"`; later the player's actual hold) |
| aiming | `state.aiming` |
| moving | player velocity (Lua), or the speed the plugin already measures (`mo.speed`) |
| stance (crouched) | KCD2 actor stance: **unverified** function name |
| stamina, Marksmanship | KCD2 soul. KCD1-era names such as `soul:GetState("stamina")` and `soul:GetSkillLevel("marksmanship")` are **unverified for KCD2**. Check the game scripts / WHGame.dll strings. KCD2 skills go to level 30 (≈, check). |
| time since last shot | `state.lastShot` (already kept) |

### Tuning knobs
- `θ_ref` (one number): calibrate on Ready or Not's `RecoilReturnPercentage` for a reference pistol, or on the user's
  recording.
- `R_max` ≈ 0.95.
- `λ_ref`: Ready or Not's `RecoilReturnRate` / `RecoilReturnInterpSpeed`.
- Hold, stance and ADS factors (H, S, A): their kick-size halves come from Ready or Not's `ADSRecoilMultiplier` and
  `VelocityRecoilMultiplier`; the range factors are set by eye.
- Stamina and skill spans (0.6-1.0 and 0.8-1.2): game feel.

### What changes where
- `deagle_main.lua` `cfg.physics`:
  - `returnPct` becomes `holdRange` (θ_ref), `holdMax` (R_max), and `hold` / `stance` / `ads` / `moving` factor tables;
  - add `staminaMin` and `skillSpan`;
  - `adsMult` stays as the kick-size factor.
- `Deagle:PhysicsRecoil`: room / back / stays, as above; the sideways axis gets the same treatment with its own range
  (section 2).
- `Deagle:CamRecoilTick`: per-gun λ, stored in `camRecoil` when the shot lands.
- New `Deagle:HoldState()`: reads stance, speed, stamina and skill. Each read is guarded with pcall and falls back to
  "fresh, standing, two hands" when a function is missing.
- `Deagle:PrintPhysics` (`deagle_physics`): print R for the current state.
- `tests/mock_engine.lua`: stubs for stamina, skill and stance; a test for calm vs fast fire.
- Ready or Not guns: R comes straight from their `RecoilReturnPercentage` (1:1, as decided). The hold model is only
  used for derived guns.

### Risks and unknowns
- The KCD2 functions for stamina, skill and stance are unverified.
- Real recovery is partly active (the shooter sees the sights and corrects). The elastic model folds that into one
  return speed.
- Ready or Not's exact formula is not in its files (names only). We match its *output* on a recording, not its code.
- Still open from v1: per-frame `PlayerSetViewAngles` deltas might eat mouse input. The return is fought by the
  player's own correction, so check this in game before tuning.

---

## 2. Horizontal recoil

### Physical sources, and how big they are
| Source | Size | Use it? |
|---|---|---|
| **Rifling torque** (the gun twists opposite to the bullet's spin) | angular impulse I_b·ω_spin: Deagle (1:19" twist) 0.0023 N m s = **0.45 %** of the flip; Glock (1:10") 0.0007 = **0.8 %**. It is a roll, not a yaw. | No (at most a cosmetic roll) |
| **Grip asymmetry** (hand not centred behind the bore, uneven support-hand pressure) | random; grows with the kick; bigger one-handed | Yes: random part |
| **Bore offset from the pivot, sideways** | pistols ≈ 0. Rifles: the stock sits in the right shoulder, ≈ 0.15-0.2 m right of the spine. The push turns the torso, so the muzzle drifts right for a right-handed shooter. | Yes: per-hold bias |
| **Muzzle devices** | they vent part of the gas momentum up or sideways at the muzzle, which is far from the pivot (long lever). The AKM slant brake vents up-right to push the muzzle down-left, against the usual up-right climb. | Yes: per device |
| **Trigger jerk / anticipation** (the sight picture drifts *before* the shot) | right-handers jerk low-left (classic pistol diagnosis target) | Yes, but on the bullet direction, not the camera; skill-based |

### Model
```
side   = up · (b + σ · n)                      n: random, normal(0,1) clamped to ±2 (today: uniform ±0.25)
b      = gun bias = L_yaw / L_pitch            L_pitch = p · bore height (the flip's angular impulse)
         device:  L_yaw,dev = η_side · p_gas · ℓ_muzzle         p_gas = m_powder · f_gas · v
         rifle:   b_hold (shoulder offset), sign by handedness
σ      = σ_hold · (1.3 − 0.6 k) · (1 + 0.5 (1 − s))      worse grip: one hand, low skill, tired
device on pitch:  up · (1 − η_up · p_gas · ℓ_muzzle / L_pitch)
device on push-back: p − η_back · p_gas
```
Example: a compensator venting 30 % of the gas upward (η_up = 0.3) at ℓ_muzzle ≈ 0.22 m from the wrist cuts the
Deagle's flip by about 18 % and a Glock 17C's by about 10 %. Magnums gain more because more of their impulse is gas
(13 % vs 5 %).

**Camera vs gun model.**
- The camera gets (up, side) through the elastic hold from section 1. Yaw has its own range, about half the pitch
  range (≈, by eye), because sideways control is weaker.
- The gun model gets the fast flip through the plugin springs:
  - back: `gunBackPerVel · v_r`
  - pitch: `gunFlipPerFlip · ω`
  - yaw: pitch × side / up
  - roll: κ_roll × yaw (cosmetic, plus the tiny rifling roll)

**How full-auto spray patterns appear.**
- The elastic hold fills up (section 1), so the climb grows.
- The bias b adds up the same way, so the stream leans to one side.
- Optional: turn the sideways return into an underdamped spring for the body's sway (≈ 1.5 Hz, damping ≈ 0.4). The
  lean then overshoots and swings back, giving an S-shaped pattern over about 0.6 s. This is a plausible hypothesis,
  to be checked against a Ready or Not full-auto recording.
- Randomness can be seeded per gun, so each gun has a learnable pattern (CS2-style), or drawn fresh each time (Ready
  or Not's `RecoilRandomness`). This is your decision.

### Inputs
- From `ShotPhysics`: p, p_gas, bore height.
- New in `gunPhysics`: `device = { up, side, back, flash, db }`, where `none` / comp / brake / flash hider / suppressor
  are presets; `side` (optional per-gun bias).
- New in `cfg`: handedness (`cfg.hand = "right"`).
- From KCD2: skill and stamina, as in section 1.

### Tuning knobs
- `σ_hold` per hold: Ready or Not's `RecoilRandomness` and the `RecoilMultiplierYaw / Pitch` ratio for a pistol.
- `b_hold` for rifles: Ready or Not rifle values.
- The device η values: makers' claims (e.g. "x % less flip") or by eye.
- The body-sway spring (2 numbers): a full-auto recording.

### What changes where
- `calibers.lua` (gunPhysics): `device`, `side`.
- `ShotPhysics`: return p_gas, L_pitch, the device-adjusted flip and the bias b.
- `PhysicsRecoil`: compute side with b + σ·n; yaw through the hold; optional seeded random (`math.random` with a
  per-gun seed); trigger-jerk offset applied to `dir` in `Deagle:Shoot`.
- `CamRecoilTick`: optional underdamped yaw (use the same implicit spring step as the plugin).
- Plugin `native/src/main.cpp`: `kick back pitch yaw [roll]`, adding a `kickRoll` Spring added into `mo.rot[1]`. The
  three-argument form keeps working.

### Risks and unknowns
- Which way a pistol drifts for a right-hander is not well established, so the pistol bias defaults to 0 (pure random).
- The S-shape is a hypothesis.
- The rifle shoulder-offset numbers (torso inertia, offset) are rough. Calibrate on Ready or Not.

---

## 3. Flash light: inside the particle, or driven by the simulator?

### Short answer
**Driven by the simulator, as a separate dynamic light**, kept on the flash entity that the plugin already glues to
the muzzle. Remove the `LightSource` from the `bloom` particle so there is only one light. If animating the light turns
out not to work in KCD2, fall back to particle lights with one particle variant per powder class.

### The real flash
- A real flash has three parts (Klingenberg & Heimerl, *Gun Muzzle Blast and Flash*, 1992):
  - a small dim *primary* flash of hot gas at the muzzle;
  - a shock-heated *intermediate* flash;
  - the big bright *secondary* flash, where fuel-rich gas (H₂, CO) re-ignites with air.
- It lasts about 1-2 ms (Burke & Bratlie 2011), much shorter than one frame (16 ms). How long the game shows it is
  therefore a look choice: 2-4 frames (60-100 ms) with a fast fade works best.
- How bright it is depends on how much burnable gas leaves the muzzle hot. That grows with the powder charge and is
  larger from short barrels. Flash hiders, suppressors and flash-reducing powders (potassium salts) cut it.

### Comparison
| | A: light inside the particle (variants per powder class) | B: simulator-driven light (recommended) |
|---|---|---|
| Follows the cartridge | in steps (one effect per class). The particle `scale` *may* scale the light radius (**unverified**); intensity is fixed per variant. | continuously: radius, intensity and colour per shot |
| Suppressor, flash hider, low-flash powder | needs more variants | one factor |
| Timing matches the sprite | automatically (it lives on the bloom particle) | copy the same curve; keep it in one place |
| Dark rooms (auto-exposure already blows out the flash, MODLOG) | fixed | can be capped or tuned at runtime |
| Cost | nothing new | one deferred light for about 0.1 s and a few Lua updates per shot |
| Known to work in KCD2 | yes (today's flash) | `LoadLight` works: v0.1's script light rendered (too strong: 9 m / intensity 6 whited out the scene). **Animating** it is unverified: `SetLightColorParams` may not exist, and reloading per frame needs testing. |
| Glued to the muzzle | yes | yes: a light in a slot of the flash entity, which the plugin places every frame |

### Model (one number drives sprite and light)
```
G      = (E_gas / E_gas,9mm)^(1/3) · D_flash(device) · P_flash(powder)   P_flash: 1 default, ≈ 0.3-0.5 low-flash military
sprite scale   = flashPerUnit · G                  (today's knob, new input)
light radius   = r_ref · G                          m
light intensity = I_ref · G
lit amount ∝ intensity · radius² ∝ G³ ∝ E_gas        → light output proportional to the gas energy
colour  ≈ flame, 2,000-2,500 K: (1, 0.72, 0.42), as the old script light (constant; optional slight whitening with G)
time    = the bloom's own curve: 0.45 → 1.0 at 20 % → 0.55 at 50 % → 0 at 100 % of its 0.1 s life
```
- Example: with r_ref = 2.5 m and I_ref = 0.6 for the 9 mm, the Deagle gets 5.3 m and 1.27. For comparison, today's
  particle light is 3.5 m / 0.9 for both guns.
- **Keeping the flash and the light consistent**: both read the same G. The light's lifetime and curve live in
  `cfg.physics.light` and copy the bloom's `ParticleLifeTime` and curve. A mock test parses `deagle.xml` and fails if
  the two drift apart. With a suppressor, G is near 0: no sprite and almost no light.

### What changes where
- `src/Libs/Particles/deagle.xml`: remove `LightSource=` from `bloom`. Optionally keep a `muzzle_lit` variant as the
  fallback.
- `deagle_main.lua`:
  - New `Deagle:MuzzleLight(phys)`: `LoadLight` into slot 2 of `FlashEntity()` (the flash uses slots 0/1) with radius,
    colour × intensity, `deferred_light = 1`, `cast_shadow = 0`.
  - Step the curve by elapsed time (not by frame count) in the existing per-frame tick while `flashUntil` runs: use
    `SetLightColorParams` if it exists, otherwise reload with new values (`FreeSlot` first, as with the particle slots).
  - `FreeSlot` at the end of the light's life.
  - In world mode, use the same entity placed at the muzzle.
- `ShotPhysics`: `flash = G`.
- Plugin: no change; the entity is already glued. If Lua stepping looks choppy, the curve could later move into the
  plugin.

### Risks and unknowns
- Whether `SetLightColorParams` exists in KCD2's binding: check WHGame.dll strings, as was done for the other Lua
  binds.
- Whether `LoadLight` into an occupied slot replaces the light.
- Whether the particle `scale` changes `LightSource` radius. This only matters for fallback A.
- In dark rooms the extra light also feeds auto-exposure. Plan a darkness cap if it blows out.

---

## 4. Damage from physics

### What wounds
- Bullets wound in two ways:
  - **crush**: the tissue the bullet itself cuts through, roughly its frontal area × its path;
  - **stretch**: the temporary cavity, which grows with the energy the bullet leaves in the body. Elastic tissue
    mostly survives handgun-level stretch.
- Sources: Fackler's wound-ballistics work; FBI, *Handgun Wounding Factors and Effectiveness*, 1989.
- Muzzle energy alone overstates big rounds. The .50 AE has 3.6× the 9 mm's energy but doesn't make 3.6× the wound.
  Momentum alone ignores speed. The useful middle is **the energy deposited in the body**: a bullet that passes
  through carries the rest out with it.

### Model
```
Tissue drag (quadratic):   v(x) = v_in · e^(−k x),   k = ρ_tissue · C_d · A / (2 m)    ρ ≈ 1,000 kg/m³, C_d ≈ 0.3
Energy left in the body over a path L:   E_dep = E_in · (1 − e^(−2 k L))
Health damage:             D = D_ref · √(E_dep / E_dep,ref)          (square root: wounds grow slower than energy)
Range (air drag):          E_in(d) = E_b · e^(−2 d / λ),   λ = 2 m / (ρ_air · C_d,air · A) ≈ 550-680 m
Stamina hit (blunt / shove, as decided: momentum):  S = S_ref · (m_b v_in) / (m_b v)_9mm
```
- **Approximation**: pure quadratic drag never fully stops a bullet. In gelatin, FFI (2016) needed C_d ≈ 0.30 plus an
  extra linear term. Phase 2 adds a stopping speed. Over a torso-sized path, the interim form is good enough.
- The air C_d ≈ 0.37 is fitted to factory tables (a 9 mm 124 gr loses about 8 % of its speed by 50 yd). A Doppler-radar
  study measured 0.60 for a supersonic 9 mm FMJ, so treat this as a fitted value. Phase 1 replaces it with real
  ballistic coefficients (Tarkov data).

Worked numbers. Torso path L = 0.3 m; D_ref = 35 HP for a 9 mm FMJ (3 torso hits on a 100 HP NPC).

| Round | Energy in | Left in the body | Health damage | CS2 today |
|---|---|---|---|---|
| 9 mm 124 gr FMJ | 521 J | 266 J (51 %) | **35** | 30 |
| .50 AE 300 gr | 1,899 J | 843 J (44 %) | **62** (1.78×) | 53 (1.77×) |
| 9 mm hollow point (opens to 15 mm) | 521 J | 449 J | 45 | – |
| 5.56 NATO (later) | 1,767 J | 772 J | 60 | – |
| 7.62×39 (later) | 2,019 J | 864 J | 63 | – |
| .50 BMG (later) | 16,600 J | 4,060 J | 137 | – |

- **Range**: at 50 m the energy is about 0.85× and the damage about 0.92×. CS2's Deagle falloff (0.85 per 12.7 m) gives
  0.53× at 50 m. That is a game-balance number, not physics.
- **Body parts (interim)**:
  - keep KCD2's own factors (head 1.5, limbs 0.7; `DealDamage` applies them);
  - set `headshotMult = 2`, so a head hit is 3× and both rounds kill a 100 HP NPC (a round that gets through the skull
    kills);
  - Phase 2 lets the helmet decide.
- **Stamina hit**: replaces today's fixed 40. With S_ref = 20, the 9 mm (2.9 kg m/s) takes 20 and the .50 AE
  (8.6 kg m/s) takes 59.
- Rifle note: 5.56 wounds much more in reality when it tumbles or fragments. Add a tumble/fragment factor from the
  Tarkov ammo data later.

### Hand-over to the Phase 2 bullet
```
muzzle → air drag to the target (E_in at range)
       → armour layers on the hit zone (each takes energy, or stops / deflects: plate rule already decided)
       → body path for the hit zone (from the hit bone: head ≈ 0.2 m, torso ≈ 0.3 m, limb ≈ 0.1 m)
       → E_dep → D (this section's formula)       E_out → exit wound, next target
       → stopped round: blunt trauma = stamina from momentum
```
The interim function is the last link of that chain, so nothing gets thrown away later.

### Inputs
- `calibers.lua`: add `d` (bullet diameter, m), optional `expand` (opened diameter, m) and `cd` (later a ballistic
  coefficient).
- `ShotPhysics`: v and E_b.
- Hit: distance, body part.
- KCD2: NPC health (100 for a normal NPC, verified: 60 damage left 40/100).

### Tuning knobs
- `D_ref` (35): a balance decision; Ready or Not's 9 mm damage is a cross-check.
- `S_ref` (20).
- Tissue C_d (0.3) and torso L (0.3): fixed physics guesses.

### What changes where
- `calibers.lua`: `d`, `expand`, `cd`.
- `deagle_main.lua`:
  - new `Deagle:Wound(eIn, cal, part)` returns health and stamina damage;
  - `Deagle:ApplyHit` uses it with E_in(d) in place of `c.damage · rangeMod^(d/12.7)`;
  - `Deagle:ApplyGunStats` stops copying CS2's damage, head and rangeMod for guns that have cartridge data (they
    stay as the fallback);
  - `AddImpulse` force = bullet momentum × the decided "punch" multiplier;
  - tests.

### Risks and unknowns
- Whether `soul:DealDamage` applies KCD2 armour at all. Test: shoot an armoured and an unarmoured NPC and compare the
  health loss.
- `bodyPartFor` is still a height guess until Phase 2.
- Some NPCs (bosses) may have more than 100 HP.

---

## 5. Fire rate from physics

### What limits a real gun
- **The action's cycle (mechanical limit).**
  - In a recoil-operated pistol, the slide takes the recoil momentum: v_slide ≈ p / m_slide. It runs back in about
    L / v and the spring pushes it home in about √(2 L m / F̄), plus friction from feeding the next round.
  - Glock 17 example: slide + barrel 0.477 kg (Fikus et al. 2016), travel ≈ 35 mm, mean spring force ≈ 55 N (≈). That
    gives 6.4 m/s, 5.5 ms back and 25 ms forward: 30-40 ms, or 1,500-2,000 rpm.
  - The real Glock 18 fires 1,100-1,200 rpm (50-55 ms). The derivation comes out 25-45 % too fast, so it needs a
    calibration factor.
  - Rifles depend on their gas system (port pressure, carrier mass, buffer). Published rates are better: AK ≈ 600 rpm,
    M4 700-950 rpm.
- **Trigger reset**: a few mm of trigger travel. The player's mouse finger *is* the trigger finger, so no extra cap.
- **Practical semi-auto rate = recovery**: how fast the shooter gets the sights back. Good shooters fire about 0.15-0.25
  s apart with a 9 mm and slower with magnums (≈, general shooting knowledge, no single source).

### Model: cap mechanically, let recovery limit the rest
```
interval   = 60 / rpm_gun          rpm: published cyclic rate; else the mod's cycle time; else derived (below)
burst gap  = 60 / rpm_gun
shot direction = camera direction + κ · (gun model's un-returned flip and yaw at the moment of the shot)
derived cycle (only for guns with no data):  t = L/v_slide + c_feed · √(2 L m_slide / F̄),  c_feed fitted on the Glock 18
```
- The plugin's gun-kick spring (ω = 28 rad/s, damping 0.89) still holds 64 % of the flip at 0.08 s, 13 % at 0.15 s and
  2 % at 0.2 s.
- With κ = 1 (the physical setting), a Deagle shot 0.08 s after the last leaves ≈ 3.8° high (5.9° flip × 0.64) on top
  of the camera climb from section 1. A Glock at 0.075 s leaves about 1.4° high.
- **Spamming the Deagle then limits itself through accuracy, not through a timer.** CS2's 0.225 s was a stand-in for
  exactly this.

### Inputs
- `gunPhysics.rpm`: published, optional.
- The mod cycle time `g.cycle`: already in the profiles.
- The kick history: Lua already sends each kick, so it can add up the same spring responses itself.
- Ready or Not guns: their own rate of fire, 1:1. The field name hasn't been read yet.

### Tuning knobs
- κ (1 = physical, 0 = off).
- `c_feed`: only if derived cycles are ever used.

### What changes where
- `cfg.fireRate = "physics"` (the new default for guns with cartridge data): `ApplyGunStats` sets `interval` and
  `burstShotGap` from rpm, else from `g.cycle`.
- New `Deagle:GunKickNow()`: the Lua mirror of the plugin spring (sum of each kick's response); `Deagle:Shoot` adds it
  to `dir`.
- Plugin (optional): `kickspring w zeta`, so Lua owns the spring constants and the mirror can't drift from the plugin.

### Risks and unknowns
- Players may read "shots go high" as unfair hit registration, since there is no crosshair and today's spread is
  random. The visible gun model rotates the same way, which helps.
- If the plugin is missing, the gun model has no kick spring. Use κ = 0 in that case.

---

## 6. Loudness

### What makes the bang
- The bang is the **muzzle blast**: high-pressure gas leaving the barrel. Its strength follows E_gas, the same number
  that drives the flash.
- At a fixed distance, the peak pressure grows with the cube root of the blast energy (Hopkinson-Cranz again). So:

```
ΔL (dB) = 20 · log10( (E_gas / E_gas,ref)^(1/3) ) = (20/3) · log10(E_gas / E_gas,ref)
```

- **Check against measurements**:
  - A widely reproduced pistol table (Krammer, Ball State University; measuring position not stated) gives 9 mm
    159.8 dB and .357 Magnum 164.3 dB, a gap of +4.5 dB.
  - The formula, for a typical .357 load (125 gr, 1,450 fps, ≈ 20 gr powder), gives +5.5 dB: within 1 dB.
  - A plain energy rule (10·log10) would give +8.2 dB, a worse fit.
- **Deagle vs Glock**: +6.5 dB, so the Deagle is ≈ 166 dB. No measured .50 AE figure was found; online claims of
  173-177 dB give no method.
- **Barrel length**: a shorter barrel leaves more gas energy, so it is louder. E_gas already includes this.
- **Suppressors**:
  - −15 to −28 dB at the peak (Murphy et al. 2016 / 2018; Lobarinas et al. 2016: 7-32 dB on AR-15s);
  - a supersonic bullet still cracks, and the suppressor doesn't touch that. Our 9 mm at 360 m/s is just supersonic;
    the .50 AE at 442 m/s is supersonic too;
  - muzzle brakes make it louder for the shooter (≈ a few dB).
- **Distance**: −6 dB per doubling of distance (spherical spreading), plus air absorption of about 0.005-0.03 dB/m
  depending on pitch (ISO 9613-1). Distant shots sound duller, not just quieter.

### From dB to the game
1. **Even out the samples first.**
   - Each gunshot file has its own mastering loudness. MWII's files were not recorded to physical scale.
   - A tools script measures each file once (ffmpeg loudness / peak) and stores a per-file gain `n_file` in the gun
     profile.
   - Without this step, physics volumes mean nothing.
2. **Own gun (2D, the plugin's `snd file vol`)**:
   `vol = n_file · 10^(s · (L − L_max) / 20) · soundVol.shot`.
   - `L_max` is the loudest gun in the mod, so vol ≤ 1. FMOD gains above 1 can clip.
   - s ≈ 0.5 plays real differences at half strength, so the Glock is 0.69× the Deagle's volume (s = 1 would give
     0.47×).
   - Suppressed guns also need suppressed samples; volume alone won't sound right.
3. **Other people's guns (later)**: FMOD 3D sounds with inverse rolloff from 1 m (−6 dB per doubling), plus a low-pass
   or a distant-shot sample with distance.
4. **AI hearing (later)**: hearing radius `r = R_ref · 2^((L − L_ref)/10)`. In loudness perception, +10 dB sounds about
   twice as loud. With a 9 mm heard at R_ref = 150 m (a game choice), the Deagle is heard at ≈ 235 m and a suppressed
   9 mm at ≈ 27 m. Real gunshots carry for kilometres, so this is deliberately compressed.

### Inputs
- `E_gas`: from `ShotPhysics` (powder, bullet energy).
- `gunPhysics.device`: the suppressor's dB.
- `n_file`: from the tools script.
- The reference L for the 9 mm: 160 dB (only differences are used).

### Tuning knobs
- s (≈ 0.5): set by ear. Ready or Not's relative event volumes in `weapons.bank` are a cross-check if they can be read.
- `R_ref`: a game decision.

### What changes where
- `ShotPhysics`: `db`.
- `Deagle:PlayCues`: `Snd(file, v.shot · Deagle:ShotGain(file))`.
- `tools/gb` + a new loudness probe script: writes `shotGain` per sample into `guns/<id>.lua`.
- Plugin: nothing for our own shots (per-sound volume already exists). Later 3D sound needs FMOD 3D mode plus
  `Channel_Set3DAttributes` and the listener position.
- AI: find how KCD2 raises noise for guards. Check what the hand cannon's shot does. The API is **unknown**.

### Risks and unknowns
- Our dB values are relative and approximate, and the measured anchors come from different setups.
- How a sound is perceived depends more on the sample's character than on its volume.
- KCD2's AI noise hook is unknown.

---

## Ready or Not: how its parameters map onto the simulator

Guns imported from Ready or Not use their own values 1:1 (decided). Derived guns fill the same slots from physics.
The meanings below are **hypotheses from the names**: the values and Ready or Not's math are still unread.

| Ready or Not | Meaning in our model | Derived guns get it from |
|---|---|---|
| `RecoilFireStrength` | camera kick per shot | `camPerFlip · ω` |
| `RecoilFireStrengthFirst`, `FirstShotRecoil` | first-shot multiplier | 1. The elastic hold already makes later shots keep more of their kick. |
| `RecoilAngleStrength` | gun-model rotation strength (guess) | `gunFlipPerFlip · ω` |
| `RecoilMultiplierPitch` / `Yaw` | up vs sideways split | up, and the bias + random side (section 2) |
| `RecoilRandomness` | random share | σ |
| `RecoilReturnPercentage` | share that comes back | R from the elastic hold (calm-shot value) |
| `RecoilReturnRate`, `RecoilReturnInterpSpeed` | return speed, how fast the kick lands | λ, `riseRate` |
| `RecoilDampStrength` | damping of the return | spring damping (camera / plugin) |
| `RecoilHasBuildup`, `RecoilBuildupDampStrength`, `ProcRecoil_*_Buildup` | growth under sustained fire, and its decay | the hold filling up (room) and λ |
| `ProcRecoil_Rot` / `ProcRecoil_Trans` | gun-model kick rotation / push-back | `gunFlipPerFlip · ω` / `gunBackPerVel · v_r` |
| `ADSRecoilMultiplier`, `VelocityRecoilMultiplier` | aiming / moving | the A and S factors |
| `ProjectileMovementSpeed`, `BulletDrag`, `DamageFalloff` | v, air drag λ, range loss | calibers.lua, section 4 |
| `PenetrationData`, `ArmorPiercing`, `ArmourCoverage/Level`, `RicochetChance` | Phase 2 armour stage | energy per layer |

To calibrate, read one pistol and one rifle in FModel. Fit θ_ref, λ_ref, σ and camPerFlip so that our formulas give
their numbers for those two guns. Then check against the user's recording.

---

## What changes where (summary)

| File / place | Change |
|---|---|
| `src/Scripts/Deagle/guns/calibers.lua` | bullets: `d`, `expand`, `cd`; guns: `device`, `side`, `rpm`, hold `"pistol1h"` |
| `deagle_main.lua` `ShotPhysics` | SAAMI f_gas by hold; `gasEnergy`, `G` (flash), `db`, `pGas`, bias b |
| `deagle_main.lua` `cfg.physics` | `holdRange`, `holdMax`, hold/stance/ADS/move factors, stamina/skill spans, `sideRandom`, `light{}`, `loudnessComp`, `damageRef`, `staminaRef`, `kappa` |
| `PhysicsRecoil`, `CamRecoilTick` | elastic hold, per-gun λ, side model, optional yaw spring |
| new `HoldState`, `MuzzleLight`, `Wound`, `GunKickNow`, `ShotGain` | as in sections 1, 3, 4, 5, 6 |
| `ApplyHit`, `ApplyGunStats`, `Shoot`, `PlayCues` | physics damage, rates, bore-offset direction, volume |
| `src/Libs/Particles/deagle.xml` | light removed from `bloom` (fallback variant kept) |
| `native/src/main.cpp` | `kick … roll` + kickRoll spring; optional `kickspring w zeta`; later 3D sounds |
| `tools/` | loudness probe → per-sample gains |
| `tests/` | mock stamina/skill/stance/LoadLight; return-share, light-vs-flash-curve, damage-table tests |

## Open decisions for you
1. Keep a calm Deagle shot at 65 % return as the anchor until Ready or Not's numbers are read?
2. Should shots go where the still-flipped barrel points (κ = 1)? Hip fire only, or aiming too?
3. Spray randomness: a learnable pattern per gun (seeded, CS2-like) or fresh every time (Ready or Not-like)?
4. Damage anchor: 9 mm torso = 35 HP (3 hits on a normal NPC), any head hit that gets through kills? Hollow points as
   a later ammo type?
5. For guns with cartridge data, drop CS2's damage, range falloff and 0.225 / 0.15 s rates?
6. Loudness: play real differences at half strength (s = 0.5)? How far should a 9 mm be heard by guards (R_ref)?
7. Stamina: should firing *cost* stamina (in proportion to recoil energy: Deagle 24 J vs Glock 5 J per shot), on top of
   tired arms recoiling more?
8. Light: remove the particle's light once the simulator light is confirmed in game?

## Unknowns to probe (offline first, then one live session)
- WHGame.dll strings: `SetLightColorParams`, the stamina / skill / stance getters, an AI sound or noise event.
- Live:
  - `LoadLight` on the flash entity, and updating it;
  - whether particle `scale` changes `LightSource` radius;
  - `DealDamage` against an armoured NPC.
- FModel: one Ready or Not pistol's and one rifle's recoil, rate and damage values.

## Sources
- SAAMI recoil formula and gas factors (pistol 1.5, rifle 1.75, shotgun 1.5 / 1.25), as implemented from SAAMI's "Gun
  Recoil – Technical" in the `ballistics-engine` crate:
  https://docs.rs/ballistics-engine/0.33.0/src/ballistics_engine/recoil.rs.html (the SAAMI PDF itself was not read).
- Pistol peak levels (9 mm 159.8 dB, .357 Magnum 164.3 dB; Krammer, Ball State University), as reproduced in hearing
  charts, e.g. https://www.arhfoundation.org/gunfire-decibels-noise-chart.
- Flamme et al. 2011, *J Am Acad Audiol* 22(2): bystander peaks 149-167 dB for 15 civilian firearms:
  https://www.thieme-connect.de/products/ejournals/pdf/10.3766/jaaa.22.2.4.pdf
- Murphy et al. 2016 / 2018, suppressors and low-velocity ammunition (15-25 dB; 17-24 dB at the ear; supersonic crack
  unaffected): https://stacks.cdc.gov/view/cdc/111710 , https://stacks.cdc.gov/view/cdc/211059
- Lobarinas, Scott, Spankovich, Le Prell 2016, *Int J Audiol*: AR-15 suppressors, 7-32 dB.
- Muzzle flash structure: Klingenberg & Heimerl, *Gun Muzzle Blast and Flash* (AIAA, 1992); overview
  https://en.wikipedia.org/wiki/Muzzle_flash ; durations of 1-2 ms: Burke & Bratlie 2011.
- Wrist short-range stiffness and elastic limit: Milner, Cloutier, Leger, Franklin 1995, *Exp Brain Res*:
  https://www.hs.mh.tum.de/fileadmin/w00bbr/nd/pdf/MilnerCloutierLegerFranklin_EBR_1995.pdf ; relaxed wrist ≈ 3 N m/rad
  (De Serres & Milner 1991).
- Glock 17 slide 0.363 kg, barrel 0.114 kg: Fikus et al., *Problemy Mechatroniki* 2016:
  https://yadda.icm.edu.pl/baztech/element/bwmeta1.element.baztech-217a55a1-af13-4b45-b8e6-76da61b0f88d/c/B_Fikus.pdf
- Desert Eagle Mark XIX .50 AE: 1:19" right-hand polygonal twist:
  https://athlonoutdoors.com/article/de50srmb-mris-desert-eagle-mark-xix-pistol-with-integral-muzzle-brake/ ; Glock 17
  1:10" (≈ 250 mm), from barrel listings.
- Drag in gelatin: FFI 2016 (C_d 0.30 with a linear term):
  https://publications.ffi.no/en/item/on-the-study-of-ricochet-and-penetration-in-sand-water-and-gelatin-by-spheres-7-62-mm-apm2-and-25-mm-projectiles ;
  retarding force from high-speed video, Gaylord / Courtney et al.: https://arxiv.org/pdf/1305.5215 ; free-flight C_d
  0.60 for a supersonic 9 mm FMJ: https://arxiv.org/pdf/1608.06500
- Wound mechanisms: M. L. Fackler (wound ballistics reviews, 1980s-90s); U. W. Patrick, FBI, *Handgun Wounding Factors
  and Effectiveness* (1989). Not re-read for this draft.
- Blast scaling: Hopkinson-Cranz cube-root scaling (standard; e.g. Kinney & Graham, *Explosive Shocks in Air*).
  Air absorption: ISO 9613-1. Loudness doubling per +10 dB: Stevens' sone scale.
