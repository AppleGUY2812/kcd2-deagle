# Mesh muzzle flash on the viewmodel: plan

Goal: flash cards (meshes) on the muzzle bone of the viewmodel. They stay hidden except for the 1-2 frames after a shot
and are toggled with `HideAttachment`, so the flash has no particle delay
(`docs/flash_no_delay_research.md` (c), ranked first). For the target look, see `docs/muzzle_flash_research.md`
sections 1 and 7.

Status:
- Done: research, plus an offline prototype. Blender, the KCD2 toolkit and rc.exe built 4 flash CGFs without errors.
- Not done: nothing has been tried in game. No repo file was changed apart from this doc.
- Prototype files (outside the repo): `E:\Tools\deagle-work\flash_proto\` holds `flash_proto.py`,
  `deagle_vm_flash_1..4.cgf` and the DAE.
- Preview render: `Desktop\Deagle Progress\09 flash mesh plan preview.png`. It is a Blender preview, not a game capture.

Sources: game files extracted read-only to a scratchpad:
- 4,520 `.mtl` files (27,527 leaf materials)
- `Engine\Shaders.pak` (the Illum shader source)
- 246 `.cdf` files
- `WHGame.dll`, disassembled with capstone

Anything not read directly from a file is marked **[INFERRED]**.

## TL;DR
1. **Material:** `Shader="Illum"` with `Diffuse="0,0,0"`, an `Emittance="r,g,b,intensity"` attribute, and the flash
   sheet in both the `Diffuse` and `Emittance` texture slots. Sub-material `MtlFlags="524454"` (two-sided, additive,
   no shadow), with `Opacity="0.03"`.
   - This copies the game's own glowing card, `Objects/ui/panels/ui_lightshafts.mtl`.
   - KCD2 doesn't use Glow on meshes. Emittance is the parameter.
2. **Attachments:** add 4 `CA_BONE` attachments to `deagle_vm.cdf`, each with `BoneName="tag_flash"` and its own CGF.
   - Lua hides them all once the character is loaded.
   - Lua shows one for each frame the flash should be seen.
   - `SetAttachmentAngles(0, name, {x=0, y=roll, z=0})` sets a random roll around the barrel. Do it while the
     attachment is hidden, ready for the next shot.
   - Runtime route with no CDF edit: `CreateBoneAttachment` + `SetAttachmentCGF`. Note that `SetAttachmentCGF` always
     **un-hides** the attachment.
3. **Mesh:** each variant has 1-2 front cards facing down the barrel (orange "petals"), a smaller white-hot core card in
   front of the muzzle, and 3 tongue planes through the bore axis at 60° apart. The 4 variants use different tiles,
   sizes and built-in rolls.
4. **Build:** `flash_proto.py` becomes `tools/flash_mesh.py`. Steps:
   1. Blender + toolkit export the 4 CGFs (`deagle_vm_flash_1..4.cgf`).
   2. Copy them and a hand-written `deagle_vm_flash.mtl` to `src/Objects/manmade/weapons/deagle/vm/`.
   3. Add the CDF lines and run `build.sh`.
   4. Install with the game closed. A restart is needed.

## 1. How a KCD2 material glows and adds

### What the shader does (Engine\Shaders.pak, `Shaders/HWScripts/CryFX/Illum.cfx`)
- Line 953: `emittance = MatEmissiveColor.xyz * MatEmissiveColor.w * (1000.0 / 10000.0)`. The material's `Emittance`
  attribute is RGB plus an intensity, and the shader scales the intensity by 0.1.
- Emittance is multiplied by the **Emittance texture** (`GetEmittanceMask`): `rgb * pow(luminance, EmittanceMapGamma - 1)
  * alpha`. The texture's **alpha** therefore cuts out the shape.
  - Exception: if the material has `%BLENDLAYER` or `%DIR_OVERLAY` together with `%DETAIL_MAPPING` or `%DETAIL_ATLAS`,
    that slot holds a second detail map and emittance becomes flat. Don't set those flags.
- Line 971: when `Opacity` is exactly 1, the emissive pass outputs only the emittance, added on top of the deferred
  pass. The card itself is still opaque in the G-buffer, so it would show as a black square. **Opacity must be below
  1.**
- With `Opacity` below 1, the material goes through the forward transparent pass:
  - colour = lit diffuse + specular + emittance
  - alpha = `Opacity` × diffuse texture alpha × vertex alpha (`GatherAlpha`)
  - Fog is applied. It doesn't matter at 0.5 m.
- With `Diffuse="0,0,0"` and `Specular="0,0,0"` the lighting adds nothing, so only the emittance is left.

### MtlFlags bits (CryEngine `EMaterialFlags`; KCD2's data agrees)
| bit | meaning | KCD2 check |
|---|---|---|
| 0x2 | two-sided | on 3,993 Illum materials (foliage, cloth, windows) |
| 0x4 | additive | ui_lightshafts and the panorama sky glows (Opacity 0.01-0.25) |
| 0x20 | no shadow | on lightshafts, torch_fire, the fake window lights |
| 0x80 | sub-material of a multi-material | on 26,729 of 26,734 named sub-materials, 0 of 793 single materials |
| 0x100 | multi-material (the parent) | our `deagle_vm.mtl` parent is 524544 = 0x80100 |
| 0x400 | no draw | on 5,073 of 5,371 Nodraw-shader materials, 2 of 21,367 Illum materials |
| 0x80000 | 64-bit gen mask | on every material |

- **[INFERRED]** The blend factors are probably ONE/ONE for additive. That is the CryEngine renderer rule for a
  transparent material with 0x4, not checked in WHGame.
- The game's own glow cards only make sense if emittance isn't weakened by `Opacity`:
  - `fake_distance_light_window_b.mtl` "light_stronger": `Diffuse="0,0,0"`, `Opacity="0.01"`,
    `Emittance="0.7605245,0.3324516,0,5"`, `MtlFlags="524448"`, which has no additive bit.
  - ui_lightshafts: `Opacity="0.03"`, `Emittance` intensity 200.
- Proposed sub-material flags: **524454 = 0x800A6** (64-bit gen mask + sub-material + no shadow + additive +
  two-sided). The game uses this exact value, e.g. `house_1s_6r_a.mtl` "plaster_decal".

### Glow vs Emittance
- `Emittance` is used by 120 materials (84 with intensity above 0): candles, lanterns, forges, embers, glowing windows,
  torch fire, lightshafts.
- `GlowAmount` is only on 4 materials: two particle materials, one Water and a placeholder.
- `Emissive` is only on 61 materials, and all are 0 except one Water material.
- So use **Emittance** plus an **Emittance** texture slot (`Map="Emittance"`).

### Real examples (verbatim)
- `Objects/ui/panels/ui_lightshafts.mtl`. A glowing additive card, the model to copy:
  `Diffuse="1,0.2501583,0.006995411" Emittance="1,0.6514056,0.2874409,200" GenMask="20000000000000" MtlFlags="524324"
  Opacity="0.03" Shader="Illum" Specular="0,0,0" StringGenMask="%SUBSURFACE_SCATTERING"`. The same texture
  `lightshafts_diff.tif` is in `Map="Diffuse"` and `Map="Emittance"`, with `EmittanceMapGamma="1"`.
- `Materials/particles/torch_fire.mtl`. A **flame mesh**: the torch particle draws it as geometry
  (`objects/manmade/common_illumination/particle_torch.cgf`, `Libs/Particles/WH_Particels.xml`):
  `Emittance="0.9559733,0.7529422,0.4232677,25" MtlFlags="524320" Opacity="0.99" Shader="Illum"
  StringGenMask="%SUBSURFACE_SCATTERING%VERTCOLORS"`. `flame_torch_diff.tif` is in Diffuse and Emittance.
- `Objects/manmade/common_illumination/lantern_fancy_a_lit.mtl` "candle". An opaque wax glow:
  `Emittance="0.2015563,0.0451862,0.000910581,4" MtlFlags="540834" Opacity="1"`. The Emittance map is
  `candles_emissive_diff.tif`.
- Forge coal (`coal_forge.mtl`): `Emittance="1,1,1,90"`, `AlphaTest 0.48`, `EmittanceMapGamma="2"`.

### Gen flags
None are needed. Almost every KCD2 material carries `%SUBSURFACE_SCATTERING`, so copy lightshafts:
`GenMask="20000000000000" StringGenMask="%SUBSURFACE_SCATTERING"`. Don't add `%BILLBOARD` (Illum.ext mask 0x2000,
"rotate geometry towards camera"). It is untested on character attachments, and we want fixed planes.

### Proposed `src/Objects/manmade/weapons/deagle/vm/deagle_vm_flash.mtl`
The sub-material order must match the CGF's ids: `__01__front`, `__02__side`, `__03__core`.
```xml
<Material MtlFlags="524544" vertModifType="0">
  <SubMaterials>
    <Material Name="front" MtlFlags="524454" Shader="Illum" GenMask="20000000000000" StringGenMask="%SUBSURFACE_SCATTERING" SurfaceType="" MatTemplate="" Diffuse="0,0,0" Specular="0,0,0" Opacity="0.03" Emittance="1,0.82,0.64,25" Shininess="10" vertModifType="0" LayerAct="1">
      <Textures>
        <Texture Map="Diffuse" File="textures/particles/deagle/deagle_flash_front_diff.tif" />
        <Texture Map="Emittance" File="textures/particles/deagle/deagle_flash_front_diff.tif" />
      </Textures>
      <PublicParams EmittanceMapGamma="1" SSSIndex="0" />
    </Material>
    <!-- "side": the same with deagle_flash_side_diff.tif, Emittance="1,0.84,0.64,25" -->
    <!-- "core": the same with deagle_flash_ring_diff.tif, Emittance="1,0.94,0.84,60" -->
  </SubMaterials>
</Material>
```
- The colours are the particle flash's own (`tools/gen_flash.py`: front (1, 0.82, 0.64), tongues (1, 0.84, 0.64),
  core (1, 0.94, 0.84)).
- The intensities are starting guesses: torch_fire uses 25, lightshafts 200. Expect to tune them.
  - MODLOG: in a dark room, auto-exposure blows out an additive flash.
- **Tuning without a restart [INFERRED]:** ship 2-3 copies of the mtl at different intensities. Swap them live with
  `vm:ChangeAttachmentMaterial("vm_flash_1", mtl)`. That bind takes `attachmentName, materialName` (WHGame strings)
  and is already used for `vm_mesh` in `VmApplyArms`.
- The textures are our particle sheets: BC3 sRGB (DXGI 78) with alpha. Front and side are 512 px, ring is 1024 px.
  They're already in `src/Textures/particles/deagle/`, so nothing new is needed.

## 2. Viewmodel attachments

### Today
- `deagle_vm.cdf` (hand-written) loads `deagle_vm.chr` with material `deagle_vm`.
- Its attachments are all `CA_SKIN`: `vm_mesh`, `vm_mag1`, `vm_mag2`, `vm_round_1..6`. They are `.skin` files exported
  by `tools/vm_export.py`, with `Flags="0"`.
- Converted guns get the same layout from `tools/gb/gb_stage.py` (around line 89-98).
- The muzzle bone is `gun.bones.muzzle = "tag_flash"` for both guns (`deagle_main.lua` line 240,
  `guns/glock_17.lua` line 37).
  - The exporters keep it rigid on the bore with **+Y down the barrel** (`vm_export.py` RIGID_TAGS,
    `Deagle:MuzzleLocal`).
- MODLOG: a `CreateBoneAttachment` on this bone carried a flash that rode the gun.

### Add (game format)
The game's own bone attachment with a CGF (`Objects/characters/humans/male/skeleton/preview/male_preview_weapon_slots.cdf`):
`<Attachment Type="CA_BONE" AName="weapon_dagger" Binding="objects/manmade/weapons/daggers/dagger_a.cgf" RelRotation="1,0,0,0" RelPosition="0,0,0" BoneName="DaggerSlot" Flags="0" .../>`
`hand_cannon_a.cdf` also gives a `CA_BONE` attachment a `Material="..."` attribute.

Lines for `deagle_vm.cdf` (4 of them, `_1` to `_4`):
```xml
<Attachment Type="CA_BONE" AName="vm_flash_1" Binding="objects/manmade/weapons/deagle/vm/deagle_vm_flash_1.cgf" Material="objects/manmade/weapons/deagle/vm/deagle_vm_flash" RelRotation="1,0,0,0" RelPosition="0,0,0" BoneName="tag_flash" Flags="0" />
```
- `RelRotation` must stay identity, because `SetAttachmentAngles` replaces the rotation (see below).
- The CGF already names its material library `deagle_vm_flash`. The prototype CGF strings are `deagle_vm_flash`,
  `front`, `side`, `core`. So the `Material` attribute is a belt-and-braces addition.
- **Hidden at start:** don't rely on a CDF flag. Hide all 4 from Lua right after `LoadCharacter`, the same way the
  rounds are handled. Until then they would show as a flash stuck on the gun.

### Lua binds (WHGame.dll strings and disassembly)
| bind | args |
|---|---|
| `HideAttachment` | `characterSlot, attachmentName, hide, hideShadow` |
| `SetAttachmentAngles` | `characterSlot, attachmentName, angles` |
| `SetAttachmentPos` | `characterSlot, attachmentName, pos` |
| `SetAttachmentCGF` | `characterSlot, attachmentName, filePath` |
| `GetAttachmentCGF`, `ResetAttachment` | as listed in `kcd2_gore_ragdoll_re.md` |
| `CreateBoneAttachment` | `characterSlot, boneName, attachmentName, bCallProjectAndSetDefaultPose` |
| `ChangeAttachmentMaterial` | `attachmentName, materialName` |

- **`SetAttachmentAngles`** (impl RVA 0x37DC7C0):
  1. Looks the attachment up by name.
  2. Copies its default transform: the QuatT from attachment vtable +0x60.
  3. Builds a quaternion from the 3 angles (call 0x53F6C0) and **replaces the rotation, keeping the position**.
  4. Writes the transform back through vtable +0x58.
  - **[INFERRED]** In CryEngine's IAttachment order, which lines up with `GetType` at +0x28 in the gore doc, those slots
    are Get/SetAttRelativeDefault. So the angles are a **bone-relative** rotation.
  - **[INFERRED]** The angles are in radians (CryEngine `Ang3`).
  - For a roll around the barrel (bone +Y): `{x = 0, y = roll, z = 0}`.
- **`SetAttachmentCGF`** (impl RVA 0x37DC8AC):
  1. Calls `I3DEngine` vtable +0xF0 with `(path, 0, 0, true, 0)`. That is LoadStatObj with streaming on.
  2. Wraps the result in a new 0x18-byte CGF attachment object.
  3. Binds it (attachment vtable +0xD0).
  4. Then **always** calls attachment +0x88 with 0, which is HideAttachment(false), even when loading failed.
  - So swapping shapes per shot works, but each call allocates, re-binds and **shows** the attachment.
  - Prefer 4 fixed attachments toggled with `HideAttachment`.

### When it applies
- `HideAttachment` sets a flag that the renderer reads at draw time (`flash_no_delay_research.md`), so a shot can show
  on its own frame. This hasn't been measured.
- **[INFERRED]** A CGF on a bone attachment is drawn from the character's pose times the entity's matrix at render
  time, like the mags and rounds. It shouldn't trail the gun the way effect attachments did (MODLOG 2026-10-12 night).
  Check it in game.
- `SetAttachmentAngles` may take effect a frame late, depending on whether the character update runs before or after
  Lua. Avoid the question: set the roll for the **next** shot as soon as a flash is hidden.

### Runtime route, for a first test with no CDF edit
1. After `LoadCharacter`: `vm:CreateBoneAttachment(0, "tag_flash", "vm_flash_1", false)`.
2. Then `vm:SetAttachmentCGF(0, "vm_flash_1", "objects/manmade/weapons/deagle/vm/deagle_vm_flash_1.cgf")`.
3. Then `vm:HideAttachment(0, "vm_flash_1", true, true)` straight away, because the CGF call showed it.

Repeat for 1 to 4. Runtime attachments are gone after a `VmRespawn` or reload, so recreate them there. The CDF route
doesn't have that problem.

## 3. Mesh design
Bone space: the origin is on the bore at the muzzle and +Y points down the barrel. The prototype (`flash_proto.py`)
builds it in Blender with the same axes, as `build_model.py` does for the gun. Sizes are in metres for the Deagle.

| part | shape | size (Deagle) | UVs |
|---|---|---|---|
| front (petals) | quad facing +Y at y = 0.03 (a 2nd layer at 0.04), rolled per variant | 0.30-0.42 m wide | `deagle_flash_front_diff` 2x2, tiles 0 and 1 (2 and 3 are faint) |
| core | quad facing +Y at y = 0.012 | 0.10-0.13 m wide | `deagle_flash_ring_diff` 2x2, tile 1 (the solid hot blob) |
| tongues ×3 | planes through the bore axis at θ, θ+60°, θ+120°, from y = -0.005 to L | L 0.22-0.36 m, width L/2 (tile aspect 1:2) | `deagle_flash_side_diff` 4x2, any tile; tile **bottom** (hot base) at the muzzle |

- **Tiles:** tile 0 is top-left, row-major, as in the particle sheets. UVs are inset by 2 px so mips don't bleed in
  the next tile.
- **UV direction [INFERRED]:** lay the UVs out as the image looks in Blender. The gun's own glTF → toolkit → rc path
  keeps textures the right way up.
- **Sizes:** taken from RoN's G19 numbers (section 1) times the mod's blast scale G = 2.08 for the Deagle
  (`flashPerUnit`: Glock 0.30, Deagle 0.62). The prototype starts at the **low end**:

  | part | RoN G19 | × G for the Deagle | prototype |
  |---|---|---|---|
  | front pop | 17-32 cm | 35-67 cm | 0.30-0.42 m |
  | pistol core | 8-12 cm | 17-25 cm | 0.10-0.13 m |
  | side flame, mid-life (about 15-20 cm long) | – | 31-42 cm | L 0.22-0.36 m |

  - The textures only fill part of each tile. The front puff covers about half its tile, so a 0.36 m card reads as a
    ~0.2 m flash.
  - For comparison, today's particle front is `Size` 0.2 × 0.62 (about 0.25 m across if Size is a radius
    **[INFERRED]**).
  - Tune everything with `SCALE` or the per-part widths, then rebuild. Attachments can't be scaled at runtime: QuatT
    has no scale.
- **Variety:** 4 meshes, each with different tiles, widths, plane angles and its own core roll. Lua adds a random
  roll per shot.
  - Frame 2 of a shot shows a **different** variant. Section 7 says the second frame can be another shape at ±30 %.
  - Optional: 2 "soft" variants on the faint front tiles 2 and 3, with a dimmer 4th sub-material, for frame 2.
- **What first person sees:** from behind the gun, the front and core cards face the camera. The tongues are seen at a
  grazing angle as short slivers, which is how RoN's look too. They show more during an inspect or a cant.
  - Preview, left half: the view from behind. Right half: the side view.
- **Glock:** the same design at G = 1 (0.48× the Deagle sizes). It needs its own 4 CGFs, and the CDF lines go in
  through `gb_stage.py`.

Prototype output, all variants with 10-12 triangles and materials `front`, `side`, `core`:
`VARIANT 1 min (-0.18, -0.005, -0.18) max (0.18, 0.315, 0.18)`, up to `max y 0.355` for variants 3 and 4.

## 4. Build steps
1. Copy `E:\Tools\deagle-work\flash_proto\flash_proto.py` to `tools/flash_mesh.py`.
   - Drop the preview block, or keep it for `--preview`.
   - It names materials `deagle_vm_flash__NN__<name>__physNone` (the `build_model.py` rule), adds one `cgf` export
     node per variant and calls `bcry.export_to_game(... generate_materials=False, convert_textures=False ...)`.
2. Run it (PowerShell, the way `build_assets.sh` does):
   `$env:BLENDER_USER_SCRIPTS='E:\Tools\blender-user'; & 'E:\Tools\blender-4.3.2-windows-x64\blender.exe' -b --factory-startup -P <flash_mesh.py> -- 'E:\Tools\deagle-work\flash' 'E:\SteamLibrary\steamapps\common\KCD2Mod\Tools\rc\rc.exe' 'E:\Tools\deagle-work\fx_tif'`.
   - Expect 4 `VARIANT` lines and `EXPORT DONE`.
   - rc writes `deagle_vm_flash_1..4.cgf`, each about 2.3-2.5 KB.
3. Copy the 4 CGFs to `src/Objects/manmade/weapons/deagle/vm/`. Use PowerShell, because E: isn't mounted in WSL.
4. Write `deagle_vm_flash.mtl` there (section 1). The textures are already packed.
5. Add the 4 `CA_BONE` lines to `deagle_vm.cdf`, or use the runtime route.
6. Lua (`deagle_main.lua`, yours to apply):
   1. Hide all 4 after `LoadCharacter` and after every `VmRespawn`.
   2. On the shot frame, show variant a: `HideAttachment(0, name, false, true)`. Keep hideShadow true.
   3. On the next frame, hide a and show b, where b ≠ a.
   4. Then hide b. That makes 2 frames at 60 fps, 1 under 40 fps (`cfg.flashLive`'s rule).
   5. Set the next roll with `SetAttachmentAngles` while it's hidden.
   6. Count frames in `vm.OnUpdate` (`VmPlace`).
7. Build and install:
   - `./build.sh`, then `--install` once `tasklist.exe | grep -i kingdom` is empty.
   - **The game must restart:** new pak files, and the CDF is cached.

### In-game tests (record at 60 fps, as for check_fx)
1. **Shot frame:** does the mesh show on the shot's first recoil frame? Count frames as in MODLOG.
2. **Glued:** swing the mouse while firing. The flash must stay on the muzzle with no 1-frame trail.
3. **Look:** additive glow vs a black or grey square, which would mean the opacity or flags are wrong. Check
   brightness in daylight and in a dark room.
4. **Roll:** does `SetAttachmentAngles` turn it, and in radians?
5. **First shot after loading:** are the textures sharp?

## What could go wrong
| risk | sign | fix |
|---|---|---|
| Opacity 1 or no additive bit | a dark or black card, or the sky behind is darkened | `Opacity` < 1, flags 524454, `Diffuse="0,0,0"` |
| Wrong sub-material order | the core shows the tongue texture | mtl order front, side, core = CGF ids 01, 02, 03 |
| Emittance texture ignored | flat glowing squares | don't set BLENDLAYER/DIR_OVERLAY + DETAIL flags; the texture alpha is the cut-out |
| Too bright / blown out (MODLOG: dark rooms) | a white blob | lower the intensity (swap mtls live with `ChangeAttachmentMaterial`) |
| Hidden flag not applied before the first frame | a flash stuck on the gun at draw | hide in the same call that loads the character |
| `SetAttachmentCGF` used per shot | the attachment shows when it shouldn't; allocation per call | 4 fixed attachments + `HideAttachment` |
| Roll a frame late | the first frame keeps the old roll | set the roll for the next shot when hiding |
| Textures stream in on the first shot | blurry first flash | prime: show the variants for a frame inside the slide (`SetAttachmentPos` y = -0.15) while drawing, then put them back |
| Attachments trail the gun like effect attachments | the flash lags on turns | unlikely for CGF (drawn with the character), but test 2 checks it |
| CDF edits break the viewmodel | gun missing | keep `RelRotation`/`RelPosition`/`BoneName` exactly; the runtime route needs no CDF edit |
| Tongues edge-on from the eye | thin flash in hip fire | rely on the front + core; tilt the tongue planes 10-20° toward the camera side, or add a 4th plane |
| UV upside down [INFERRED] | tongues' hot base at the far end | flip V on the side tiles in the script |
