# Muzzle flash with no frame delay: research and ranking

Goal: a flash that appears on the first recoil frame, is visible for 1-2 frames at 60 fps, and is glued to the muzzle (see `docs/muzzle_flash_research.md` section 7).

Everything below comes from static analysis of WHGame.dll (RVAs disassembled with capstone). **Nothing has been tested in game.** No game files were changed.

## Section 6 check

Section 6 holds up. The CParticleEmitter vtable at RVA 0x3A55F48 matches:

| Slot | Offset | RVA |
|---|---|---|
| Activate | +0x258 | 0x422664 |
| Restart | +0x268 | 0x421B20 |
| SetSpawnParams | +0x288 | 0x738FFC |

SetSpawnParams compares the incoming params with the emitter's copy at +0x110. It calls the full reset (0x422F28) only when fSizeScale (+0x0C), fSpeedScale (+0x10) or eAttachType (+0x28) changes. It then copies the params (0x7390CC) and tail-jumps to **0x422608**. A strength-only change therefore does not reset the emitter.

## New finding: 0x422608 empties the bounding box

Kill, SetSpawnParams and Activate all call 0x422608. Activate also calls 0x4226FC, which advances the age of each component. 0x422608 does two things:

1. It calls 0x42320C, which takes the emitter off its list (node at +0xD0) and re-links it on one of the manager's lists, +0x348 or +0x358. Which list it uses depends on bit 15 of the manager flags (+0x1E0) AND the emitter flags (+0x178).
2. It writes the empty bounding box: +0x1A8..+0x1B0 = 0x58635fa9 (1e15) and +0x1B4..+0x1BC = 0xd8635fa9 (-1e15).

SetLocation does the same box reset, and that reset caused the earlier problem of flashes not being drawn. **As written, the strength-gating sketch in section 6 would hide the emitter on every frame where strength changes, which includes the shot frame.**

Fix: do what main.cpp already does after SetLocation. Read the box (ReadBox / BoxValid) before calling SetSpawnParams or Activate, then WriteBox it back right after. The Restart path (0x421B20) does not go through 0x422608. It is unchecked whether 0x4226FC touches the box.

## (a) Do live particles follow a strength change?

Probably not, though the binary doesn't settle it. Every read of +0x12C (the emitter's copy of fStrength) found in the particle code turned out to be part of a +0x12C/+0x130/+0x134 vector triple (a position). No strength reader was found.

KCD2's particle XML (`EmitterStrength=` curves) comes from CryEngine 3's legacy pfx1. In pfx1, the emitter-strength part of a parameter is baked into each particle when it spawns. Only the particle-age curve is evaluated after that.

Effect on gating: a live emitter held at strength 0 spawns invisible particles. Only particles spawned while strength is 1 are visible. Particles already on screen don't disappear when strength drops back to 0. The flash particles therefore need a very short life (about 1-2 frames, allowing for the first-frame ageing noted in MODLOG) so the flash ends on time.

Test: switch strength from 1 to 0 on a particle with a long life and see whether it disappears at once.

## (b) Why a new emitter is drawn late

- A new emitter starts with the empty box above, so it is culled until the update job computes a real box.
- That update runs after the culling for the frame.
- Particles are also aged a whole frame at their first update (already in MODLOG).

So a new emitter can't reliably be drawn in the frame it spawns. Writing a box around the muzzle into the new emitter right after spawning might get it drawn one frame sooner. That only helps if particles already exist by then, so it is a guess, not a fix.

## (c) Mesh flash on an attachment

- The bindings exist in WHGame.dll strings: `SetAttachmentCGF`, `CreateBoneAttachment`, `HideAttachment`, `SetAttachmentLight`, `DrawSlot`.
- `HideAttachment` already runs every frame on the viewmodel (`vm_rounds`, `vm_round_N`, `vm_mag1`/`vm_mag2` in deagle_main.lua around line 1765). It sets a flag on the attachment (`flags |= 0x70000`, see `docs/kcd2_gore_ragdoll_re.md`). The character reads that flag when it is drawn, with no particle job in between, so the flash would very likely show in the same frame. This hasn't been measured.
- The game has no muzzle-flash mesh. A pak search found only fireplace meshes and the hook-gun skins. One would have to be built: a few crossed quads with an additive glowing material, parented to the muzzle bone in the viewmodel .chr/.cdf the same way the magazines are.
- Variety: make 2-3 mesh variants, or roll a random one in Lua.

## (d) Instant light on each shot

`LoadLight` already works in v0.1, and `SetLightColorParams` and `ActivateLight` are both in the strings. Plan:

1. Load the light once into slot 2 of the flash entity and keep it off.
2. On the shot, turn it on.
3. Fade it with `SetLightColorParams` over 1-2 frames.

Entity lights reach the renderer from the main thread, not through the particle update, so the light should show in the same frame. It is unverified whether `SetLightColorParams` changes a light that is already loaded. If it doesn't, fall back to `FreeSlot` + `LoadLight`.

## Ranking (most likely to have no delay first)

1. **Mesh flash attachment toggled with HideAttachment, plus the light from (d).** It uses a mechanism the mod already relies on and doesn't depend on the particle job. Cost: building the mesh and material.
2. **Pre-warmed live emitter with strength gating, with the box restored after each SetSpawnParams/Activate.** It works given the box fix and a particle life of about 1-2 frames, but depends on the unproven per-particle baking in (a).
3. **Light from (d) on its own.** The scene flashes on the shot frame even if the particle sprite is a frame late.
4. **New emitter with a pre-seeded box.** Least likely to work, for the reasons in (b).

## In-game tests

- **Gating:** gate a live emitter with the box saved and restored, record at high fps, and count which frame the flash appears on (as in the earlier check_fx captures).
- **Mesh:** toggle a test `vm_flash` attachment and confirm it shows on the shot frame.
- **Light:** check that `SetLightColorParams` actually changes a light that is already loaded.
