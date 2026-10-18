# Ready or Not extraction: how session johnpork-9f pulled data (2026-10-12)

Rebuilt from 9f's transcript (`~/.claude/projects/-home-johnpork/81286427-298b-45b8-8c38-b64077090665.jsonl`, work at
13:08-15:54 UTC) and its scratchpad. The scripts are now in `tools/ron/`; each was re-run on 2026-10-12 against 9f's data
and reproduces its outputs byte for byte unless noted. Times are UTC.

Rules: everything is read-only on the game; Ready or Not (RoN) is never launched. Its assets are private use only:
extracted files, textures, sounds and meshes live in `E:\Tools\deagle-work\...` or gitignored repo folders, never in git.
The decoded numbers (`docs/ron/*.json`, tables) are in the repo.

## 1. The install
| What | Where / value |
|---|---|
| Game | Steam app 1144200, `C:\Program Files (x86)\Steam\steamapps\common\Ready Or Not` (WSL `/mnt/c/...`); buildid 24942528, updated 2026-09-04 |
| Paks | `ReadyOrNot\Content\Paks\pakchunk0..24-Windows.pak` (25, + `.sig`): UE 5.3, pak version 11, compression Oodle, index not encrypted (key GUID all zeros), so no AES key. RoN ships no Oodle DLL |
| Mappings | `Mappings.usmap` build 113804 (2026-09-04, "Mission Editor v1.5.2"), 2,102,252 bytes, from https://unofficial-modding-guide.com/downloads/Mappings.usmap; copy at `E:\Tools\deagle-work\ron\exporter\Mappings.usmap`. Unversioned properties can't be read without it; after a game update get a new one (the guide's page, or dump one with UE4SS) |
| Exe | `ReadyOrNot\Binaries\Win64\ReadyOrNotSteam-Win64-Shipping.exe`. `strings -n 5` gives every property / function name (how 9f first found the recoil / ballistics names). The formulas are compiled C++ and were not decompiled |
| Sounds | `ReadyOrNot\Content\FMOD\Desktop\*.bank` (51 files), FSB5 Vorbis inside; `Weapons.bank`, `Interactions.bank`, `SWAT.bank` used |
| Not used | A second copy at `D:\Games\Ready or Not` |

**Mod paks (gotcha).** The Paks folder now also holds the user's mods: `pakchunk97-Restoration_AIO_Nexus_BP_P`,
`pakchunk99-*` (GodMode, MCR_LMG, SimpleBlueprintLoader, SimpleModMenu, FasterScoreV2, Increased_Movement_Speed, Qjb201,
RespawnPlayer), `pakchunk999-AttachmentsLibGTSI`, `pakchunk9999-*` (CODMG338, CazanusVisceralBlud, CazanusVisceralGore,
wound). Some replace vanilla files at the same path: Visceral Blud replaces `Data/Blood_Standard`, the blood decals and
particles, `BP_RoNBloodPool` (97 of 9f's extracted files); Restoration AIO replaces `CyberneticsSuspect_V2` /
`Cybernetics_Civilian_V2`; Increased_Movement_Speed ships its own `BasePlayer`. They were moved in after 9f's run (a
listing of the folder now has 470,532 entries; vanilla alone has 466,963, exactly 9f's count), and 9f's extracted files
match the vanilla paks. Paks are read in sorted order and a later match overwrites, so `ronx.py` now reads only
`pakchunk<N>-Windows.pak` (set `RON_ALL_PAKS=1` for all). CUE4Parse (RonExport) mounts every pak in the folder: none of
the current mods touches the 40 G19 export paths, but check before exporting another gun.

## 2. Setup
- WSL Python 3 with numpy and Pillow. Run the decoders as `python3 -I tools/ron/<script>.py`; each adds its own folder
  to `sys.path` (`-I` drops it, and keeps the extracted game files from shadowing a module).
- Paths come from `tools/ron/ronpaths.py`, overridable by environment variable:

  | Variable | Default | Holds |
  |---|---|---|
  | `RON_PAKS` | `<game>/ReadyOrNot/Content/Paks` | read-only |
  | `RON_BANKS` | `<game>/ReadyOrNot/Content/FMOD/Desktop` | read-only |
  | `RON_WORK` | `/mnt/e/Tools/deagle-work/ron/work` (= `E:\Tools\deagle-work\ron\work`, empty at the handoff) | intermediate JSON, `fxout/`, `tif_impacts/` |
  | `RON_OUT` | `$RON_WORK/ronout` | extracted `.uasset/.uexp/.ubulk`, pak layout (9f's was 142 MB) |
  | `RON_USMAP` | `/mnt/e/Tools/deagle-work/ron/exporter/Mappings.usmap` | |
  | `RON_OOZ` | `~/.cache/kcd2-deagle/libooz.so` | |
  | `RON_ALL_PAKS` | unset | `1` = also read mod paks |

- Oodle: `bash tools/ron/build_ooz.sh` clones powzix/ooz at commit 05038060 (2019), applies `tools/ron/ooz_stdafx.h`
  (Linux shim), keeps `kraken.cpp` up to line 4150 plus an `extern "C" ooz_decompress` and builds `libooz.so` with g++
  (~30 s). The result is byte-identical to 9f's build.
- E: is mounted in WSL at `/mnt/e` now. Earlier that day it wasn't, so 9f wrote to its scratchpad and copied with
  `cmd.exe /c robocopy \\wsl.localhost\Ubuntu\... E:\...` (and `.bat` files for paths with spaces: bash `read` ate
  backslashes and args split on spaces).
- Sounds: a venv with `fsb5` (+ system libvorbis) and `numpy`, and ffmpeg. 9f's was `/tmp/fsbenv` (temporary):
  `python3 -m venv ~/.venvs/deagle-audio && ~/.venvs/deagle-audio/bin/pip install fsb5 numpy matplotlib`.
- RonExport (meshes, animations, textures): .NET SDK 10.0.201 on Windows (`C:\Program Files\dotnet`; 7.0.102 is there
  too), CUE4Parse + CUE4Parse-Conversion 1.2.2.202610 from NuGet. Source `tools/ron/RonExport/` (Program.cs,
  RonExport.csproj); 9f built it in `E:\Tools\deagle-work\ron\exporter\RonExport` (bin/obj there):
  `cmd.exe /c "cd /d E:\Tools\deagle-work\ron\exporter\RonExport && dotnet build -c Release"`. NuGet warns about known
  vulnerabilities in Microsoft.Bcl.Memory 9.0.0 and SixLabors.ImageSharp 3.1.12 (offline, local files only). The first
  run downloads the Oodle DLL the way FModel does (`OodleHelper.DownloadOodleDll`). The CUE4Parse 1.2.2 API differs from
  older examples: `ExportSession` (Add, then `RunAsync(outDir, options)`) and a 13-argument `ExportOptions`; 9f found it
  by reflection.
- AES key: none needed. `Program.cs` takes an optional 5th argument, a key file; `ronexport.bat` passes
  `tools\ron\local_keys.txt` (gitignored) when it exists. Never commit a key.
- Blender for renders: `E:\Tools\blender-4.3.2-windows-x64\blender.exe`.

## 3. The tools (`tools/ron/`)
| Script | Does |
|---|---|
| `pakls.py <paksdir> <out.txt> <pak>...` | lists every file in the given paks (mount + path per line) |
| `ronx.py <paksdir> <outdir> <libooz> <regex>` | extracts matching files: pak v11 index, encoded entries, Oodle via ctypes (zlib fallback) |
| `usmap.py`, `uasset.py` | usmap reader; cooked UE5.3 package reader (unversioned properties, blueprint-added properties, package-local structs, Niagara variables). "consumed" = export size minus a 4-byte trailer means a byte-exact decode |
| `dump.py <pkg>...` | decodes every export of a package, extracting parent blueprints on demand (`ensure`) |
| `dt.py <usmap> <pkg> [chars]` | DataTable rows |
| `ronrecoil.py <usmap> <dir> <out.json>` | every weapon blueprint's class defaults in a folder |
| `build_recoil_md.py <json>` | the recoil table (stdout) |
| `ballist.py <out.json>` | ammo / suspect armour tables, damage curves, armour, armour materials, phys mats, projectile, AP damage type, penetration |
| `build_out.py <out.json>`, `build_md.py <json> <md>` | `docs/ron/ron_ballistics.json` and its table |
| `fp_cdo.py` | class defaults of `ANIMBP_Player_FP_Master` -> `$RON_WORK/fp_cdo.json` |
| `build_move.py <out.json>`, `build_move_md.py <json> <md>` | `docs/ron/ron_movement.json` and its table |
| `skel_dump.py` | player skeleton + ref pose -> `$RON_WORK/ron_skel.json` (`skel.py` reads it) |
| `animdec.py`, `movecurves.py <list> <out.json>`, `build_curves.py <in> <out>` | AnimSequence decoder (AnimCompress_PerTrackCompression), weapon_right / fp_camera curves in our camera axes, `docs/ron/ron_movement_curves.json` |
| `fx1.py <out.json>` | the 8 ImpactEffects blueprints -> `impact_map.json` |
| `decal_list.py` | extracts the bullet-hole decal materials, lists their textures -> `decal_textures.txt` |
| `tex.py <list> <outdir>` | Texture2D mip 0 -> PNG (BC1/3/4/5/7, B8G8R8A8, G8; BC5 gets its Z rebuilt) |
| `niagara.py <pkg> [chars]`, `cascade_dump.py <pkg>` | Niagara system / Cascade module dumps |
| `fxcollect.py <out.json>`, `fxtex.py`, `fxmd.py <md>` | per-surface FX + decal materials, their textures, `docs/ron/ron_impact_fx.md` |
| `mk_impact_tif.py` | `fxout/` PNGs -> `tif_impacts/` (particles; bullet holes with green flipped, smoothness in alpha; `FLIP_G=0` to skip the flip) |
| `blood_collect.py <out.json>`, `blood_tex.py`, `mk_blood_tif.py` | blood FX / decals, their 86 textures, blood TIFs |
| `fsblist.py <banksdir> <out.json>`, `fsbx.py <bank> <outdir> <regex> [--wav]` | FSB5 sample names; decode samples (`--wav`: ffmpeg to 16-bit WAV) |
| `anim_list.py <AnimData pkg> [out]` | a gun's first-person montages -> sequences (+ notifies) |
| `RonExport/`, `ronexport.bat <list.txt> [outDir]` | CUE4Parse export: ActorX `.psk` / `.psa`, PNG textures, material JSON |
| `anim_dump.py <skeleton> <out.json> <anim>...`, `acl_dump/` (`acl_dump.cpp` + `build.ps1`: ACL 2.1 / RTM 2.2 headers, MSVC) | decode AnimSequences to JSON tracks per bone: ACL-compressed ones (the DLC3 Grizzly's: CUE4Parse 1.2.2 says "Unsupported compressed data type") through `acl_dump.exe`, the others through `animdec.py`. Grizzly MK V 2026-10-18: 17 arm + 13 gun clips, 0 failed (`E:\Tools\deagle-work\ron\grizzly\{arms,weapon}_clips.json`); its mesh / textures / casing / mag through `grizzly_export.bat` (full plugin paths `ReadyOrNot/Plugins/GameFeatures/ReadyOrNotDLC3/Content/...`, not `/ReadyOrNotDLC3/...`) |
| `psk_render.py`, `render.bat <psk> <png> <prefix>` | headless Blender render of a .psk from 3 sides |
| `build_ooz.sh`, `ooz_stdafx.h` | the Oodle decompressor |

`tools/pakx.ps1` (`-Pak <pak> -Pattern <regex> -Out <dir> [-ListOnly]`) is a different thing: it extracts from
**KCD2's** zip-format paks (9f used it at 17:55 for the flash-mesh research: `.mtl`, `Libs/Particles/*.xml`, `.cdf`).

## 4. Recipes
Shorthand used below (WSL, from the repo):
```
T=tools/ron; P="/mnt/c/Program Files (x86)/Steam/steamapps/common/Ready Or Not/ReadyOrNot/Content/Paks"
mkdir -p /mnt/e/Tools/deagle-work/ron/work && cd /mnt/e/Tools/deagle-work/ron/work   # = $RON_WORK
R=~/kcd2-deagle/tools/ron; X() { python3 -I $R/ronx.py "$P" ronout ~/.cache/kcd2-deagle/libooz.so "$1"; }
U=/mnt/e/Tools/deagle-work/ron/exporter/Mappings.usmap; D=~/kcd2-deagle/docs/ron
```
1. **File list** (9f 08:48): `python3 -I $R/pakls.py "$P" ron_files.txt $(ls "$P" | grep -E '^pakchunk[0-9]+-Windows\.pak$')`
   (466,963 lines), then `grep '\.uasset$' ron_files.txt | sed 's#^\.\./\.\./\.\./##' > ron_assets.txt`. Most later
   searches are greps of these two files.
2. **Recoil** (13:10-13:19, commit bb23dca):
   `X 'Items/WeaponsRevised/(Secondary_[^/]+|Primary_(MP5A3|M4A1|870mcs|B1301|HK416|MP5A2))\.(uasset|uexp)$'`,
   `python3 -I $R/ronrecoil.py $U ronout/ReadyOrNot/Content/Blueprints/Items/WeaponsRevised ron_recoil.json` (24 guns
   with only those extracted; = `docs/ron/ron_recoil.json`), `python3 -I $R/build_recoil_md.py ron_recoil.json`
   (= `ron_recoil_table.md` as committed; another session has since appended a "Measured from the user's recordings"
   section, uncommitted at the handoff).
3. **Ballistics** (14:07-14:11, commit 28478c0):
   - `X '(Curve_Damage_[^/]*|DataTables/SuspectArmourDataTable|Items/ArmorRevised/[^/]*|ArmorRevised/ArmorMaterials/[^/]*|ArmorRevised/Helmets/[^/]*|BallisticsVFX/PhysMats/[^/]*|EngineMaterials/(PhysMat_[^/]*|Default[^/]*PhysicalMaterial)|Logic/ArmorPiercingBulletDamage|Projectiles/Projectile_Base)\.(uasset|uexp)$'`
   - `X 'Blueprints/Info/PenetrationData\.'`, `X 'Blueprints/Items/ItemData\.|DataTables/AmmoDataTable\.'`
   - `X 'Blueprints/Characters/(BasePlayer|AI/CyberneticsSuspect_V2|AI/Cybernetics_Civilian_V2|AI/CyberneticsSwat_V2)\.(uasset|uexp)$|DataTables/AI/AIDataTable_Hotel\.'`
   - `X '^ReadyOrNot/Config/DefaultEngine\.ini$|Items/WeaponsRevised/(Primary|Secondary|Sniper)_[^/]*\.(uasset|uexp)$'`
   - `python3 -I $R/ronrecoil.py $U ronout/ReadyOrNot/Content/Blueprints/Items/WeaponsRevised ron_weapons_all.json` (68)
   - `python3 -I $R/ballist.py ron_ballistics_raw.json`, `python3 -I $R/build_out.py $D/ron_ballistics.json`,
     `python3 -I $R/build_md.py $D/ron_ballistics.json $D/ron_ballistics_table.md`. 9f then hand-edited the curve-unit
     sentence in section 1 of the table (14:16); `build_md.py` doesn't write it.
4. **Weapon movement** (14:12-14:16 and 15:18-15:23, commits e0fac24, 3d65077):
   - `X 'Graphs_Body/ANIMBP_Player_FP_Master\.(uasset|uexp)$|/MB_[^/]*\.(uasset|uexp)$'`
   - `X 'AnimData/(Mainline/BP_(G19G5|M92FS|Python|Colt1911_V2|M4A1|MP5A3)_M_AnimData|BP_USP_AnimData)\.(uasset|uexp)$|Attachments/Pistol_Suppressor_GM9\.(uasset|uexp)$'`
   - `python3 -I $R/fp_cdo.py`, `python3 -I $R/build_move.py $D/ron_movement.json`,
     `python3 -I $R/build_move_md.py $D/ron_movement.json $D/ron_movement_table.md`
   - Curves: `python3 -I $R/skel_dump.py`; the 25 sprint / jump / crouch / loco anims:
     `grep -E '/(fp_(g19|m4a1|gen)_add_(loco_sprint_(fwd|start|stop)|tac_up_sprint_(fwd|start|stop)|jump_(start|loop|end)|sprint_(fwd|start|stop)|loco_(m|f)_(fwd|bwd|l|r))|fp_m4a1_(stand_to_crouch|crouch_to_stand))\.uasset' ron_assets.txt | sed 's/\.uasset$//' > move_anims.txt`,
     `X "^($(sed 's/[.]/\\./g' move_anims.txt | paste -sd'|'))\.(uasset|uexp)$"`,
     `python3 -I $R/movecurves.py move_anims.txt move_curves_raw.json`,
     `python3 -I $R/build_curves.py move_curves_raw.json $D/ron_movement_curves.json`
5. **Impact FX** (14:17-14:29, commit 18ab64f):
   - `X 'Blueprints/Logic/[^/]*ImpactEffects[^/]*\.(uasset|uexp)$|VFXImpacts/P_BulletTracer\.(uasset|uexp)$'`,
     `python3 -I $R/fx1.py impact_map.json`
   - `python3 -I $R/decal_list.py`, `python3 -I $R/tex.py decal_textures.txt fxout/decals` (64 PNG; `/Engine/` textures
     are skipped)
   - `python3 -I $R/fxcollect.py fx_collect.json` (extracts the Niagara / Cascade systems and materials it needs),
     `python3 -I $R/fxtex.py` (95 particle PNG), `python3 -I $R/fxmd.py $D/ron_impact_fx.md`. 9f hand-added the "Cascade
     value notation" sentence afterwards.
   - 9f then moved `fx_collect.json`, `impact_map.json`, `fxout/particles_index.json` and `fxout/decals/_index.json`
     (as `decals_index.json`) into `fxout/_data/` and copied `fxout/` to `E:\Tools\deagle-work\ron_fx\`. `fxmd.py` needs
     the index files in their first place, so run it before moving them.
6. **Impact textures for KCD2** (14:39-14:45, commit f9738fa): `python3 -I $R/mk_impact_tif.py` (reads
   `$RON_WORK/fxout`; to use the E: export, link `$RON_WORK/fxout` to `/mnt/e/Tools/deagle-work/ron_fx`) ->
   `tif_impacts/` -> copy to `E:\Tools\deagle-work\fx_tif_impacts\` -> `powershell.exe -ExecutionPolicy Bypass -File
   tools\fx_impact_textures.ps1` (rc.exe; docs/impact_fx_build.md).
7. **Blood** (14:54-15:00, commit b65ab03): `X 'ReadyOrNot/Data/Blood_Standard\.(uasset|uexp)$'` (9f read it with
   `dump.py` to pick the effects; the lists are written into `blood_collect.py`), `python3 -I $R/blood_collect.py
   blood_collect.json`, `python3 -I $R/blood_tex.py` (86 PNG -> `fxout/blood/`, copied to
   `E:\Tools\deagle-work\ron_fx\blood\`), `python3 -I $R/mk_blood_tif.py` (now also does the 512 x 512 resize of
   spatter_2 that 9f did by hand), then as in 6. All 58 files in `E:\Tools\deagle-work\fx_tif_impacts\` rebuild
   byte-identical.
8. **Sounds** (15:06-15:10, commit fa040b8): `fsblist.py <banks> fsb_names.json` (all banks but Music.bank) to find
   sample names; the impact set is built by the repo's `tools/impact_sounds.py <repo>` (decode, ffmpeg, levels, Lua;
   docs/impact_fx_build.md section 7). The Colt Python / .357 set for the echo tests (17:15):
   `fsbx.py <banks>/Weapons.bank <out> '^(Python_(Fire|Fire_Snub|MidShot|MidTail|MidReflection|Reflection_(Ext|Int)_\w+)|357_Dist\w+)[-_ ]?\d*$' --wav`
   (40 WAVs, 48 kHz stereo; now in `E:\Tools\deagle-work\sound\ron_python\`, docs/sound_echo_plan.md).
9. **Muzzle flash** (15:49-15:54, commit e1150cc): `X 'WeaponFlash/(WeaponFlash_Pistol_V2|WeaponFlash_Rifle_V2|P_MuzzleFlash)\.(uasset|uexp)$'`,
   `python3 -I $R/cascade_dump.py ReadyOrNot/Content/ReadyOrNot/Assets/Particles/WeaponFlash/WeaponFlash_Pistol_V2`;
   `docs/muzzle_flash_research.md` was written from that output.
10a. **Grizzly muzzle flash** (2026-10-14, Claude 4): `X 'Weapons/Grizzly/Blueprints/BP_Secondary_LARGrizzly\.(uasset|uexp)$'`
   and `dump.py` it (MuzzleFlash = `WeaponFlash_Pistol_V3`); then `python3 -I $R/flash_collect.py
   /mnt/e/Tools/deagle-work/ron_fx/_data/flash_collect.json` (V3 + V2, materials, PNG in `ron_fx/flash/`), from the repo
   `python3 tools/ron/flash_bake.py .` (cards.json, TIFs, the 3 mtls), rc on `E:\Tools\deagle-work\flash_ron\tif\*.tif`
   (preset AlbedoWithGenericAlpha, as tools/fx_textures.ps1) into `src/Textures/particles/deagle/ron_flash/`, and Blender
   with `tools/flash_mesh_ron.py` (as flash_mesh.py) into `src/Objects/manmade/weapons/deagle/vm/`.
10. **G19 export** (15:35-15:44): `python3 -I $R/anim_list.py ReadyOrNot/Content/Blueprints/Animation/Revised/AnimData/Mainline/BP_G19G5_M_AnimData g19_seqs.txt`
    (49 montages, 30 first-person, 30 sequences; 9f's mesh lookup: BasePlayer `CharacterMesh1P` = `Skinning_Arms_03`
    with `ANIMBP_Player_FP_Master`). The list `E:\Tools\deagle-work\ron\exporter\g19_list.txt` (40 paths) = the G19
    mesh + the arms + those sequences + idle pose / sprint / fire-single / tac-reload-empty extras picked by name.
    Then `cmd.exe /c "\\wsl.localhost\Ubuntu\home\johnpork\kcd2-deagle\tools\ron\ronexport.bat E:\Tools\deagle-work\ron\exporter\g19_list.txt"`
    -> `E:\Tools\deagle-work\ron\g19\ReadyOrNot\Content\ReadyOrNot\...`: `SK_Pistol_G19.psk`, `Skinning_Arms_03.psk`,
    32 `.psa` (`fp_g19_idle_pose` twice: arms and weapon), textures `G19_B / _N / _ORM`, `FP_Arms_01_B / _N / _ORM`,
    material JSON. 6 sequences failed in CUE4Parse ("Unsupported compressed data type"): five under
    `Animations/Revised/...` and `FP/G19/weapon/fp_g19_add_holster`; `animdec.py` is the fallback decoder.
    Renders: `render.bat <psk> <basecolour png> E:\Tools\deagle-work\ron\g19\render_g19` (and `render_arms`); contact
    sheet `Desktop\Deagle Progress\ron_g19_export_sheet.png`.

## 5. What 9f built for the mod from RoN data
| Feature | Where | Commit |
|---|---|---|
| Recoil values, 20 guns | `docs/ron/ron_recoil.json`, `ron_recoil_table.md`; summary in BALLISTICS.md | bb23dca |
| Ballistics read-out (41 ammo rows incl. .50 AE, 26 damage curves, armour, 43 surfaces, health) | `docs/ron/ron_ballistics.json`, `ron_ballistics_table.md` | 28478c0 |
| Weapon movement read-out | `docs/ron/ron_movement.json`, `ron_movement_table.md` | e0fac24 |
| Movement design + decoded sprint / jump / crouch curves | `docs/weapon_movement_design.md`, `docs/ron/ron_movement_curves.json` | 3d65077 |
| Weapon movement layer (RoN sprint pose, jump / land, crouch, dodge, inertia; Deagle = RoN .357 values) | `native/src/motion.cpp/.h`, `src/Scripts/Deagle/motion.lua`, `tests/run_motion.lua` (27 checks), `deagle_mparam` | 3020b5c; jitter fix b9ccc23 |
| Impact / tracer visuals read-out | `docs/ron/ron_impact_fx.md`; PNG + raw JSON in `E:\Tools\deagle-work\ron_fx\` | 18ab64f |
| Bullet impacts: 10 surface effects, 10 bullet-hole decals | `src/Libs/Particles/deagle_impacts.xml`, `src/Materials/deagle/decals/bullet_hole_*.mtl`, `tools/fx_impact_textures.ps1`, DDS in `src/Textures/particles/deagle/impacts/` (gitignored), `docs/impact_fx_build.md` | f9738fa |
| Wallbang / exit / ricochet | `src/Scripts/Deagle/guns/penetration.lua` (GUESS-marked values), exit + ricochet effects in `deagle_impacts.xml`, impact_fx_build.md section 5 | 11a761d |
| Blood (entry / exit / head / armor_entry; 7 spatters + pool) | `src/Libs/Particles/deagle_blood.xml`, `src/Materials/deagle/decals/blood_*.mtl`, impact_fx_build.md section 6 | b65ab03 |
| Impact sounds, 235 WAVs in 26 sets | `tools/impact_sounds.py`, `src/Scripts/Deagle/guns/impact_sounds.lua`, WAVs in `assets/build/sounds/impacts/` (gitignored); hearing ranges are guesses | fa040b8 |
| Muzzle flash research (RoN `WeaponFlash_Pistol_V2`: one particle per part for 0.035-0.04 s, full size at once) | `docs/muzzle_flash_research.md` | e1150cc |
| Flash mesh plan (from KCD2 files, not RoN; 9f's background worker) | `docs/flash_mesh_plan.md`, prototypes in `E:\Tools\deagle-work\flash_proto\`, preview `Desktop\Deagle Progress\09 flash mesh plan preview.png` | built as flashMode "mesh" in bfcf18b |
| RoN gun importer design + G19 prototype export | `docs/ron_importer_design.md`, `E:\Tools\deagle-work\ron\g19\` | uncommitted at the time, in the repo now |
| Echo listening tests (Colt echo files) | docs/sound_echo_plan.md | not in the mod |

## 6. Gotchas
- Mod paks override vanilla files (section 1). Keep the vanilla filter on.
- A property missing from a decoded object means "not overridden": the value is the C++ default in the exe, which was
  not read. 0 / false shown = explicitly set.
- Damage-curve X is taken as cm (inferred from the ranges; there's no UI range text to check against).
- `uasset.py` quirks found on the way: package header at 0x1c; `KeyHandleMap` is serialized as 0 bytes; property
  order is most-derived class first; blueprint-added variables shift every index (read them from the class export);
  some anim blueprints define structs inside their own package; Niagara variables are custom-serialized.
- UE5.3 textures keep mip data through a data-resource table at the end of the `.uasset` and in `.ubulk`; BC5 normals
  need Z rebuilt; UE normals are green-down, CryEngine green-up (`mk_impact_tif.py` flips; 9f checked against a KCD2 decal).
- Niagara spawn counts, lifetimes and velocities are compiled into VM bytecode: not readable. Cascade (`P_`) systems are.
- Decal lifetime / fade / rotation are set in C++ (names only).
- `fp_cdo.py` decodes 19,819 of 39,292 bytes of the anim blueprint's defaults (the rest isn't property data).
- RoN first-person anims are additive montages (arms + weapon) over an idle pose; nothing is a full pose
  (docs/ron_importer_design.md).
- Python's `-I` ignores `PYTHONDONTWRITEBYTECODE`: runs leave `tools/ron/__pycache__/` (gitignored).

## 7. Not read / still missing
- The C++ side: recoil / damage / penetration formulas, limb health, flinch and stagger thresholds, bullet gravity and
  sub-steps, weapon / camera bob defaults (21 `CameraBob*` / `WeaponBob*` fields are never overridden), decal lifetime.
- Camera-shake assets (`C_Generic_Fire_Shaky_*`) weren't decoded.
- FMOD event data: hearing / attenuation distances and where the reload sounds are triggered (`SoundData_G19` holds
  only shot, dry fire, mag drop, ADS and whizz events).
- `fp_g19/m4a1_add_tac_up_*` float curves were skipped.
- The 6 G19 sequences CUE4Parse couldn't decode; the importer itself (`tools/ron/ron_import.py` in the design doc) isn't
  written; the arms + weapon additive bake isn't done.
- A RoN Desert Eagle doesn't exist (only `50AEFMJ` / `50AEJHP` ammo rows and `Curve_Damage_Pistol_50AE`).
