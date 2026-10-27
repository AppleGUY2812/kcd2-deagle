# Arm swap: Henry's own arms and clothing on any gun

The gun's arms come from another game (Ready or Not, Call of Duty via CS:S mods, ...). The arm swap replaces them
with Henry's skeleton wearing whatever Henry wears right now, so changing clothes shows on the gun arms.

**Scaling rule:** nothing in this process is per clothing item or per gun. The per-game work is one bone-name table.
If a step ever needs per-item or per-gun hand work, it is the wrong step: fix the step.

| Work | Done once per | Size |
|---|---|---|
| bone table (`PROFILES` in `tools/armswap/retarget.py`) | source game (RoN, CoD, ...) | ~15 lines |
| retarget every clip of a gun onto Henry's skeleton | gun (automatic, part of the gun build) | machine time |
| clothing on the arms | nothing: the game's own worn skins + materials are copied at run time | 0 |

Status (2026-10-26): proven offline in Blender on two games' arms (RoN Grizzly, CoD Glock). In game: the clothing
copy test is built (`deagle_clothcopy npc|vm|off`, `native/src/gore.cpp`, 4c508cb), not run yet; the Henry-skeleton
viewmodel is **not built yet**; see "Not proven yet". Videos:
Desktop\Deagle Progress\ARM SWAP DEMO 3 - *.mp4 (both reloads, same fit code, only the bone table differs).

## How it works

1. **Skeleton.** The viewmodel uses Henry's skeleton (`tools/henry_rig.py` imports it with bone frames equal to the
   game's joints). Clothing skins bind to joints by name, so Henry's skeleton is what lets every clothing item fit.
2. **Scale the viewmodel to Henry's size** (`Fit.source_scale()`): shooters often shrink first-person arms (CoD's
   hand is 6.3 cm wrist to knuckle; Henry 8.3, RoN 9.3, a real adult ~9). Scale the whole viewmodel, gun included,
   by Henry's arm length / the source's, about the camera: the viewmodel's origin is the camera, so the picture
   doesn't change, and the source arms become Henry's size (CoD x1.24, RoN x0.95). Without it Henry's real-size
   gauntlets look huge on a miniature gun.
3. **Retarget** (`tools/armswap/retarget.py`, class `Fit`), per frame, per side:
   1. Copy each source bone's rest-to-pose rotation onto Henry's matching bone (gives every bone its twist).
   2. Walk back from the source wrist along the source forearm and upper arm using **Henry's** lengths, and place
      Henry's shoulders there (root follows the midpoint, clavicles aim at them). Forearm and upper arm keep the
      source's directions even when the arms differ in length; the shoulders are off screen.
   3. Two-bone IK: elbow bent toward the source elbow, **palm centre** (wrist-knuckle midpoint) on the source's palm
      centre. Pinning only the wrist leaves the knuckles short when the hands differ in size.
   4. Hand rotation copied, then every finger bone (thumb included) **aimed at the source's next joint**.
      Copying finger angles alone misses by 2-6 cm on a different-sized hand (the "thumb sticks out" bug).
   It uses joint positions and each source bone's own rest-to-pose rotation only, so bone axes, rolls and rest poses
   of the source game don't matter.
4. **Clothing** (in game, to build): the plugin reads Henry's `clothing_arms` and `clothing_hands` skin attachments
   (one skin each + 6 LOD materials with the item's generated colours) and binds copies onto the viewmodel; rebinds
   when Henry re-dresses. Henry's body stays visible with those two attachments hidden (`hideatt`).
5. **Hiding rules** come with the game: vertex colour alpha is a bitmask, a cleared bit i = the vertex is in hiding
   group i (`Libs/Tables/Character/ClothingHidingGroup.xml`); worn items hide groups (`HidingGroups=` in
   CharacterComponent.xml, e.g. gauntlets hide "UnderGloves" = bit 3 of arms pieces). The game applies these itself;
   the offline demo reproduces them.

## Add a new game's arms

1. Import the gun's viewmodel into Blender (camera at the origin looking +X, right = -Y, like the RoN and CoD
   blends; `tools/vm_export.py` documents the CoD orientation).
2. Add a profile to `PROFILES` in `tools/armswap/retarget.py`: a bone that identifies the rig, a function mapping
   Henry's joints to the source's joints (`ron_profile`, `cod_profile` are the examples), the side tokens and the arm
   mesh names. Henry's joints: `Shoulder Arm ForeArm Hand`, `HandThumb1-4`, `HandIndex1-4`, `HandMiddle1-4`,
   `InHandRing HandRing1-4`, `InHandPinky HandPinky1-4`. A source without fingertip joints uses `("ext", last, prev)`
   (tip = last joint + 0.8 x the previous segment). Henry has no index / middle metacarpals: skip the source's.
3. Measure: `PROOF_DIAG=1` (below) prints, per joint, mean / max cm off and the forearm / upper arm angle error over
   the clip, for the old angle copy and the full fit. Targets: forearm <= 3 deg mean, fingertips <= 2.5 cm max.
4. Render the reload (below) and look at it next to the source arms before anything goes in game.

## Commands (Blender, offline; run at Idle priority while the game runs)

```
# textures for the demo only (the game uses its own shader): split DDS -> PNG, then the stand-in clothing colours
python3 -I tools/armswap/ddsjoin.py <tex out> <clothing .dds files...>
python3 -I tools/armswap/tex_prep.py <tex out>

# measure a clip (old copy vs full fit)
PROOF_DIAG=1 blender -b <gun>_vm.blend -P tools/armswap/proof_anim.py -- x <tex dir> <action>
# render a clip: game camera + close-up, source arms and Henry's clothing, PNG frames per view
blender -b <gun>_vm.blend -P tools/armswap/proof_anim.py -- <out dir> <tex dir> <action> [step] [width] [samples]
```
From WSL: `powershell.exe -NoProfile -Command "(Get-Process -Id $PID).PriorityClass = 'Idle'; $env:PROOF_DIAG='1';
$env:BLENDER_USER_SCRIPTS='E:\Tools\blender-user'; & 'E:\Tools\blender-4.3.2-windows-x64\blender.exe' -b ..."`.
Rendering the 51-frame RoN reload (4 views, 640x360, Cycles CPU 24 samples) takes ~10 min on all 12 threads, which
makes a running KCD2 stutter: the script uses 3 threads by default (~3x slower); `PROOF_THREADS=0` = all, only with
the game closed. With the game closed, `PROOF_GPU=1` renders on the graphics card (OptiX); never while KCD2 runs (it
fills ~7.4 of the 8 GB video memory).

## Getting KCD2 clothing into Blender (demo only)

- Item -> files: `Libs/Tables/Character/CharacterComponent.xml` (in `Data/Tables.pak`), `<SkinElement
  EquipmentPart="arms|hands|torso..." Model=...>`. 140 garments have their own arms skin (`arms/arms_splited/`);
  mail shirts keep their sleeves in the torso skin.
- .skin -> .dae: KCD2-Convertor (`...\io_KCD2_Blender_Toolkit\External\KCD2-Convertor\KCD2-Convertor.exe`, needs
  `$env:DOTNET_ROOT='E:\Tools\dotnet9'`); gauntlets need `-notex -objectdir <dir>`. Its mesh is turned 180 deg about
  Z against `henry_rig.py`: negate x and y of every vertex (weights and joint names are fine).
- Split DDS: the base `.dds` holds the header (DX10) and small mips; the highest-numbered `.dds.N` is mip 0;
  `.Na` files are the separate alpha. `ddsjoin.py` writes header (mip count 1) + mip 0; BCn sRGB formats are
  relabelled UNORM for Pillow (same bytes).
- Colours: the `_id` mask texture picks a feature slot per pixel (`r + 3g + 9b`, each channel 0 / half / full); the
  item's `<Feature Name= Material=>` list (CharacterComponent.xml) and the `.mtl` `FeatureSlots` name the material per
  slot (ClothingMaterial.xml). Metals have a near-black diffuse and their look in `_spec`. `tex_prep.py` hard-codes
  approximate colours for the three demo items; the game doesn't need any of this.

## Engine facts the in-game step uses (WHGame 1.5.6, read in the binary, not tried in game)

- `ICharacterInstance` +0x40 / +0x48 -> attachment manager. Manager: +0x10 `CreateAttachment(name, type, joint,
  allowDuplicate)` (type 2 = skin), +0x18 remove by interface, +0x48 by index, +0x68 count.
- Skin attachment: +0xD0 `AddBinding(object, ISkin*, flags)` (releases the previous object: every attachment needs
  its own new object), +0x38 / +0x40 set / get flags, +0xE0 attachment object, +0xE8 -> +0x10 the ISkin.
  New object: CryModuleMalloc 0x1804f75c0 (0x40 bytes) + constructor 0x1805adac4, skin pointer via
  0x1805ada88(obj+8, GetIAttachmentSkin()). Object +0x80 / +0x78: get / set the material per LOD (6 LODs).
- One skin per body part (`clothing_arms` = the top layer's arms skin); skins and materials are reference counted, so a
  copy outlives Henry re-dressing (it goes stale: rebind). VCLOTH pieces (type 5) bind differently: skip them.
- Full notes: MODLOG 2026-10-26 "arm swap, step 2"; ProLiant `~/re/work/clothing_attach/report.md`.

## Measured (reload clips, mean / max over the clip)

| | RoN: angle copy | RoN: full fit + scale | CoD: angle copy | CoD: full fit, no scale | CoD: full fit + scale |
|---|---|---|---|---|---|
| forearm direction | 18.9 / 24.2 deg | 1.0 / 3.2 deg | 21.4 / 48.0 deg | 2.5 / 6.1 deg | 2.7 / 9.7 deg |
| fingertips (worst finger) | 2.6 / 2.8 cm | 0.3 / 0.6 cm | 4.4 / 5.9 cm | 1.6 / 2.3 cm | 1.3 / 1.8 cm |
| elbow | 8.8 / 11.2 cm | 0.5 / 1.2 cm | 10.2 / 19.8 cm | 6.1 / 6.4 cm | 1.3 / 4.7 cm |

Arm lengths (upper arm / forearm / hand): Henry 26.6 / 26.2 / 8.3 cm; RoN 28.1 / 27.1 / 9.3 (scaled x0.947:
26.6 / 25.7 / 8.8); CoD 22.0 / 21.0 / 6.3 (scaled x1.239: 27.2 / 26.1 / 7.8). Measure with `PROOF_DIAG=1`;
`PROOF_NO_SCALE=1` turns the scaling off.

## Not proven yet (in order of risk)

1. Binding Henry's clothing skins on the viewmodel in game: the calls are read, not run. Test order: a male NPC first
   (same skeleton), then the viewmodel. Watch kcd.log for "Skin root joint ... was not found".
2. Skeleton extension: a skin with a joint the viewmodel lacks makes the engine extend and rebuild the skeleton (in
   public CryEngine that resets the character: a pop mid-clip). Henry's full skeleton on the viewmodel avoids it.
3. Per-character clothing looks (blood / dirt zones) may live on Henry's character, not the material: missing on the
   copy.
4. Mail shirts: sleeves are in `clothing_torso`, which also has the chest. Needs a decision (show the torso piece
   clipped by the camera, or hide).
5. Gauntlet thumb plates are bulkier than a bare thumb; the grip can look thicker than the source.
6. Viewmodel scale in game: the plugin places the viewmodel every frame; a scaled character must draw right, and
   everything measured on its bones (muzzle, ejection port, rigid pieces, flash) must scale with it.
7. Animation audit: retargeted clips must pass `tools/build_vm.sh --audit` like any clip.

## Files

| File | What |
|---|---|
| `tools/armswap/retarget.py` | the game-independent fit + one bone table per source game |
| `tools/armswap/proof_anim.py` | offline demo: Henry's clothing on a source clip, renders and `PROOF_DIAG` numbers |
| `tools/armswap/proof_swap.py` | first single-frame demo (superseded by proof_anim.py) |
| `tools/armswap/ddsjoin.py`, `tex_prep.py` | demo textures from KCD2's split DDS + stand-in colours |
| `tools/henry_rig.py` | Henry's skeleton with the game's joint frames |
| `native/src/gore.cpp` | `listatt` / `hideatt`, and `clothcopy` (`CopySkin`): Henry's worn skins + LOD materials bound onto another character |
