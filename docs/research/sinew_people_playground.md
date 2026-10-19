# Sinew study: People Playground (decompiled, private study)

Source: ~/re/decomp/PeoplePlayground on the ProLiant (ilspycmd). V = read in code, I = inferred. The break-force numbers for the body joints live in prefab data, not code, so only the formulas are known (V), not the per-limb values.

## 1. Mechanics

**Limb joint (LimbBehaviour, HingeJoint2D, V).** Each limb has one hinge to its parent, a motor (`maxMotorTorque = MotorStrength`) that drives toward the animated pose, and angle limits. `SetJointFragility()` runs every update and rewrites the break values from live state:
- `base = BreakingThreshold * 120 * clamp(1 - Rotten, 0.1, 1) * massRatio / FragilityMultiplier`; cold scales it by 0.1 to 1.
- `breakForce = base * 2.5` (androids `* 80`), `breakTorque = base * 5000`.
- `massRatio = rigidbody.mass / TrueInitialMass`, so a limb that lost mass is weaker.
- Break thresholds are therefore dynamic, not fixed at spawn.

**Stage 1, bone "crack" (V).** `CalculateJointStress()` runs each tick: if the angle is more than 5 degrees past the original limit, `JointStress += 0.25`; `JointStress *= 0.982` per tick (decay). If `JointStress > BreakingThreshold * massRatio`, `BreakBone()` is called. Over-bending accumulates and heals slowly, which makes it fatigue, not a single spike. `BreakBoneInternal()` sets `Broken` and widens the limits halfway toward +-180 degrees: `limits.max = Lerp(max, 180, 0.5)`, `limits.min = Lerp(min, -180, 0.5)`. The joint stays attached but floppy. This is the "sag" stage.
- Impact path (`ActOnImpact`): `impulse > BreakingThreshold * massRatio / ImpactDamageMultiplier` and `random > 0.2` gives BreakBone.
- Impact propagation (`PropagateImpactDamage`): impulse passes to connected limbs times `dot(direction) * 0.9`, up to 8 hops, only to limbs "downstream" of the hit direction. Stress spreads through the chain, with 10% loss per hop.
- Crush (`OnCollisionStay2D`): min contact impulse above `max(10, BreakingThreshold) * lerp(posIterations/16, 1, 0.2)`. This couples the threshold to solver iteration count.

**Stage 2, tear (V).** Unity's `breakForce` or `breakTorque` is exceeded and `OnJointBreak2D` fires. Then: pain, the node graph is disconnected (`NodeBehaviour.DisconnectFrom`), the HingeJoint is destroyed, a blood decal and impact effect are spawned at `Joint.anchor`, and a skin wound texture is stamped there (`AddDamagePoint(Dismemberment, pos, 25)`).

**Stage 3, the sinew itself: GoreStringBehaviour (V).** In `OnJointBreak2D`, if `random > 0.5` (and acid < 0.7, burn < 0.8), `goreString.CreateJoint()` is called, so only about half of rips leave a string:
- A **SpringJoint2D** connects the two bodies: `distance = 0.05`, `frequency = JointFreq (4 Hz)`, `dampingRatio = JointDampening (0.5)`, anchors at the midpoints of the two wound edge lines (`OriginLine` / `OtherLine`).
- The joint starts with `breakForce = Infinity`. After `InvincibilityTime = 0.8 s`, `SetBreakForce()` sets `breakForce = JointBreakThreshold (4) * 1.5 = 6`. This grace window stops the string snapping on the same frame as the rip.
- Visual: 5 to 13 random thin lines (`Linefy Lines`) from random points on one wound edge to random points on the other. Strand 0 is permanent (strength infinity, thickness 2). The rest get `thickness 0.5 to 2`, `strength 0.1 to 1`.
- Width scales as `LineWidthMultiplier / max(dist, 0.25)`, so strands thin as they stretch (volume conservation look).
- **Progressive snap**: each frame, with `d` = anchor distance, a strand dies (thickness 0, snap sound at 0.5 volume) when `d / strength^2 > breakForce / 12` (= 0.5 with default 6). Weak strands go first as the gap grows, and the joint finally breaks (`OnJointBreak2D` then `DestroyJoint`) when the physics force passes 6. The visible thinning is driven by distance, the final cut by force.
- Cleanup: the string is destroyed if the other body is deactivated or disintegrated.

**Ropes and wires (V).**
- `RopeBehaviour : DistanceJointWireBehaviour`: one `DistanceJoint2D`, `autoConfigureDistance = false` then set true after setup, so rest length = spawn distance. `RopeTool` break force = `CalculateBreakForceForCable(joint, 350, refMass 1, massInfluence 0.5)` = `350 * lerp(maxMass/1, 1, 0.5)`, i.e. scaled by the heavier body. Every physics tick `breakForce = buffer * (1 - BurnProgress)` (fire weakens it); `Slice()` sets the buffer to 0.
- Rope visual is a fake: `CalculateParabola` draws a catenary `cosh(2x*1.316958 - 1.316958) - 2` sagging by `(1 - d^2/L^2)^0.65 * L/2`. It sags when slack and goes straight at full length. There is no physics on the sag.
- Real physical rope (`PhysicalRopeCreator`): chain of N rigid nodes (`PointsPerUnit 4`, `NodeMass 0.05`, box colliders, small random impulse 0.5), each linked to the previous by `DistanceJoint2D` with `maxDistanceOnly = true`. This gives rope that is slack-capable and cannot stretch. No break force is set, so it never tears.
- Struts: wood strut `breakForce = 700` (via same mass formula); steel binding `6000`; `maxDistanceOnly = false` makes a rigid strut. Car wheel joints `15000`. `FixedJointWire` uses frequency 0 / damping 1 (rigid); `breakForce` is read from currentBreakingForce.
- Wire stress readout: `reactionForce^2 / breakForce^2`, sqrt for colour (used for a red tint as the load rises).

**Other break drivers (V).** `BreakJointOnShotBehaviour`: `p = Chance(0.05) + shot.damage * ShotIntensityInfluence`; if `random < p`, `breakForce = -1` (instant break). `Slice()` does the same. `BreakJointOnBurnBehaviour`: `breakForce = initial * clamp01(1 - burn * rand(1..2))`, so fire drains strength continuously. Shots: a shot above 149 damage crushes with probability `clamp((dmg-149)*0.005, 0.3, 0.8)`. Heat shots slice with 20% chance. Destroying a limb sets its joint `breakForce = 0`.

**Stability tricks (V).** `HingeJointLimitAutofixBehaviour` is not stability: it only swaps and negates hinge/slider limits when the object is mirrored (`localScale.x < 0`) and re-attaches FixedJoints. Real stability tricks seen: break values scale by mass ratio, the gore string has a 0.8 s invulnerability, and the joint stress is low-pass filtered (0.982) so one-frame solver spikes do not tear. I found no custom solver-iteration setup in code (it is in project settings, not decompiled): not verified.

## 2. Translating to CryEngine 3D (I)
- `breakForce` maps to CryPhysics `maxForcePull/Push/Shift`; `breakTorque` to `maxTorqueBend/Twist`. Unity's break force is a one-number magnitude; Cry splits axes, so set pull highest, shift lower, bend/twist lowest for wood beams.
- Hinge limits map to `limit` / bend limits on a structural joint. Motor/pose-drive has no use for debris.
- The GoreString is the best fit: a spring plus a distance-triggered fibre fade. In Cry, use a rope entity (or a spring-damper constraint) between two chunk anchors, plus skinned fibre strips.
- The visible-thinning-before-break trick needs no physics: it is a per-strand `d / strength^2 > threshold` test, cheap per frame.
- Risk (unverified in KCD2): whether script-created joints/ropes are accepted on pre-fractured chunks at all; this is the S0 probe from destruction_feel.md.

## 3. Recommendations for crack -> sag -> dangle -> tear
1. **Two-phase joint, as in Limb.** Keep a structural joint with high force but low bend limit. Accumulate a stress value (excess bend over limit per tick, 0.982 decay) rather than reacting to peak force; when it passes a threshold, widen the limit about half-way to free (the "crack/sag" step) but do not break. Break force separately, higher.
2. **Sinew on a coin flip.** On tear, 50% of the time spawn a spring/rope between the two wound edges: rest 5 cm, about 4 Hz, damping 0.5, break threshold about 1.5x the nominal (PP uses 6 vs 4), with a 0.8 s invincibility so it does not pop on the rip frame. Not every tear gets one, which avoids a uniform look.
3. **Many strands, staggered death.** 5 to 13 fibre strips between random points on the two torn faces, each with random strength 0.1 to 1 and thickness 0.5 to 2; one strand never snaps until the joint does. Kill strand when `dist / strength^2 > breakForce/12` (tune), taper width as `1/max(dist, 0.25)`. Gives the progressive "pop pop pop" tear.
4. **Mass-scaled thresholds, fire-drained.** Multiply break values by `mass / referenceMass` (PP rope: `lerp(m, 1, 0.5)` so heavy bodies get stronger but not linearly); for burning thatch/timber multiply by `1 - burn`. Cheap way to get hits weakening things without a damage system.
5. **Fake the sag.** For slack beams/thatch strands, draw a catenary on a skinned strip (`cosh` shape, sag `(1 - d^2/L^2)^0.65 * L/2`) rather than simulating a chain; use real chain (distance joints, max-only, node mass 0.05) only for the few hero ropes. Propagate impact stress to neighbours (0.9 per hop, direction-gated, 8 hops) so a hit on one rafter loads the next.

Weak points: PP is 2D (Unity Box2D joint, single force number); damping and mass ratios will need retuning in 3D. Actual per-limb `BreakingThreshold` values live in prefabs, not read. Nothing here is tested in KCD2.
