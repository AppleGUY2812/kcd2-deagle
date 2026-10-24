# Handoff 2026-10-23: fast-travel preload (destination during the walk)

Written by the session that owns **load times / fast travel** (`ses_e961db75`, "Enable background compaction")
for the next Claude. The mirror work runs in parallel in `ses_e955d780` — see `docs/INCIDENT_2026-10-23_mirror_flash.md`.

Read `CLAUDE.md` first. The rules that matter most here:
- **Never launch or close the game.** Say "restart needed" and let the user do it. Install only while
  `KingdomCome.exe` is not running (check with
  `powershell.exe -NoProfile -Command "if (Get-Process KingdomCome -ErrorAction SilentlyContinue) {'RUNNING'} else {'closed'}"`).
- Show visual checks in `C:\Users\<user>\Desktop\Deagle Progress\` (that is `/mnt/c/Users/<user>/Desktop/Deagle Progress`).
- Commit only your own hunks. Keep `bash tests/run_all.sh` passing.
- The user is short-tempered after weeks of failed experiments. Short answers. Do not stage blind builds.

---

## 1. The goal

Long fast travel shows a long black screen and is not instant. The user wants the destination **preloaded
during the pawn/animation walk**, so that arrival is instant instead of "walk, then load".

## 2. Where it stands (measured, not guessed)

Instrumentation: `native/src/main.cpp` logs every frame gap ≥ 100 ms and every camera jump ≥ 30 m as
`timing: frame N after X ms, camera moved Y m to x y z` in
`Bin/Win64MasterMasterSteamPGO/deagle_native.log`. `tools/loadprofile.ps1` samples CPU/disk/GPU/VRAM at 1 Hz.

Long trip (8670 m) and short trip (~330 m), same session, 22 mods (2026-10-21, table from
`docs/HANDOFF_2026-10-21_weightmap_fasttravel.md`):

| | long trip (8670 m) | short trip (~330 m) |
|---|---|---|
| map open → pawn animation | 14:55:44.9 → 14:56:06.7 (**21.8 s**) | 14:56:19.5 → 14:56:26.3 (6.8 s incl. load) |
| during the animation | disk **0–7 MB/s**, CPU ~3.2 of 12 threads, GPU 50–70 %, VRAM flat 6.85 GB | – |
| black after arrival | 14:56:07.1 → 14:56:19.6 (**12.5 s**) | 14:56:26.4 → 14:56:29.3 (**~2.7 s**) |
| during the black | disk **145–158 MB/s**, CPU 7–8 threads, VRAM 6.85 → 7.2 GB | disk ~90 MB/s, CPU 3.5–5.7 threads |

Conclusions:
- **Nothing is preloaded during the 21.8 s walk.** The SSD is idle; the user's read was correct.
- The black screen is not a stuck frame or fader: the frame counter advanced 6173 → 7115 (~75 fps) while the
  screen was black, so the engine is rendering behind it while streaming.
- Length scales with how much has to stream (12.5 s vs 2.7 s) = dest stream-in, ~1.9 GB at ~150 MB/s on the
  SATA 870 EVO plus decompression. Not NPC clothing builds.

So the only real win is: **start that 12.5 s of work during the 21.8 s the engine spends idle.**

## 3. What the engine does (verified in the decompile, 1.5.6)

### Fast travel entry points
- `wh_pl_FastTravelTo` CCMD at RVA **0x2ea894c** → parser **0x2eaf1a4** → **0x2eaa218**.
- `wh_pl_FakeFastTravelTo` CCMD at RVA **0x2ea8920** → parser → **0x2eaa218(pos, 1)**.
- **0x2eaf1a4** parses the console arguments and produces a 3-float position:
  - 2 args → treats arg 1 as a **POI name**, looks it up in the manager at `DAT_18492d8a0 + 0x80` and calls
    vfunc `+0x140` to get the position;
  - 4 args (x y z) → `atof` each, direct position.
- **0x2eaa218(float* pos, int fake)** is the "start a travel to this position" call.

**Critical finding (2026-10-23):** `FUN_182eaa218` has **exactly two callers in the whole DLL**, and they are
those two console commands (lines 9333540 and 9333593 of the decompile). The **map's** fast travel does *not*
go through it. So hooking 0x2eaa218 catches console travel only, not the thing the user does.

### What is *not* there
- The destination is **not exposed to Lua**. Whole `Data/Scripts.pak` was scanned; the only fast-travel script is
  `Scripts/Entities/WH/CutsceneData/FastTravelCutsceneData.lua` — a 12-line stub with `esFastTravelIcon` and
  `esDisableRandomEvents` and nothing else.
- There is **no fast-travel cinematic track** anywhere in `Data/*.pak` (searched every pak for `fasttravel` /
  `fast_travel`; only UI icon DDS files and quest XML).
- `C_FastTravelCutscene` is `wh::guimodule::C_FastTravelCutscene`; the factory that allocates it is
  **0x1811cf500** (`FUN_1811cf500`, 0x70 bytes, sets vftable, calls `FUN_1811d0064`). Its only two references
  are RTTR factory tables, so it gives no argument to read.
- `wh::playermodule::RandomEventFastTravelData` / `PlayerInitiatedFastTravelData` / `EventInitiatedFastTravelData`
  exist, but they describe the **random encounter**, not the destination point (RTTR class registration around
  decompile line 3225273 / 3064979).

### The precache mechanism (the thing we want to trigger)
- Cvar `e_AutoPrecacheCameraJumpDist`, default **16 m**, storage at **renderer + 0x51c**
  (`DAT_185496fd0`), registered near decompile line 4435882. Help: *"When not 0 – Force full pre-cache of
  textures, procedural vegetation and shaders if camera moved for more than X meters in one frame or on new
  cut scene start"*.
- The consumer is in the view/camera update function around decompile line 1314158 (function near `0x1806bb…`):
  ```c
  if (*(int *)(DAT_185496fd0 + 0x51c) != 0 && (float)*(int *)(DAT_185496fd0 + 0x51c) < jumpDistance) {
      *(undefined1 *)(view + 0x1021) = 1;   // request full precache
      *(undefined1 *)(view + 0x6a4)  = 1;
  }
  ```
  So a big one-frame camera jump is enough to make the engine precache everything around the new position.
  **We already have the camera**: `native/src/main.cpp` `g_cam` (Matrix34 copied from `CCamera::UpdateFrustumPlanes`)
  and `native/src/freecam.cpp` moves `CSystem`'s camera through the `SetViewCamera` hook (`CSystem + 0x288`).
- Opening the map jumps the camera 8.6 km to (-3921, -4002, 51), which is why every map open triggers three
  precache passes (the user's "3 black flashes"). **Do not set `e_AutoPrecacheCameraJumpDist` to 0** — that was
  tried live and removed the precache that keeps the world resident: 1–2 fps and 6.9–7.3 GB of 8.19 GB VRAM.
  The precache is load-bearing.

## 4. The plan that is left

The destination is unknown at travel start in any place we can read today, so:

1. **Probe build** (log only, changes nothing in game): hook the map travel start, ring-buffer candidate
   vectors from the player actor / action list / cutscene object while the walk runs, and when the camera
   finally jumps > 100 m (the arrival, which `main.cpp` already detects), search the buffer for a vector within
   ~5 m of the arrival point. A hit identifies the field and the offset chain. **One long trip gives the answer.**
2. Once the field is known: on travel start, write the destination into the view for one frame (freecam's
   `SetViewCamera` path) with the screen black, let one frame run so the jump check above fires the precache,
   then restore. Measure with `tools/loadtime.py` + the `timing:` lines: disk must move during the walk, and
   the 12.5 s black after arrival must collapse.
3. If the probe finds nothing, say so plainly and stop — do not ship a guess.

Open question the probe also answers: what the 21.8 s walk actually *is* (a `C_FastTravelCutscene` /
`PMS_SkipTime` pass, per `docs/HANDOFF_2026-10-21_weightmap_fasttravel.md`, or a pawn walk). During it the game
is in `wh::C_PrecacheMode`, reason `PMS_SkipTime`; the black end is `PMS_Fader`
(reason→string table `FUN_182443338`).

## 5. Disproven — do not repeat

- `wh_cc_RequiredPostLoadReadyManagers` 10 / `...ReadyTime` 0.2: 7 s vs the game's own 35 / 0.5 at 8 s in the
  same session. Removed from `user.cfg`, corrected in `docs/PERFORMANCE.md` §7.
- `wh_pl_SkipTimeEnableOptiCVars 0`: disk still 0–10 MB/s during the walk. Reverted to 1.
- `e_AutoPrecacheCameraJumpDist 0`: see above — 1–2 fps, VRAM saturated. This was the user's worst stall.
- `wh_cc_LRUCache` / texture-streaming cvars as a *load-time* fix: they raise VRAM pressure and per-frame I/O.
- `wh_ui_ApseUnloadMode 0`: caused blank/frozen inventory after a sale. Pulled.
- Map `TexturePreload.xml` override: the wildcard matched `.dds.1`..`.dds.6` mip files and emptied the in-game
  map. The game's own file was restored. See `docs/kcd2_map_open.md`.
- `wh_pl_FastTravelDebug`: the map stopped responding (game kept rendering). Do not set it.

## 6. Current state of the machine and the files (2026-10-23 ~18:10)

Game: `E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2` (Samsung 870 EVO SATA, 85 GB free).
Hardware: Ryzen 5 3600, 2×8 GB DDR4-3200, RTX 3070 Ti 8 GB, 16 GB RAM, 1920×1080 @ 240 Hz.

**Owned by the mirror session — do not edit, write the change in chat instead:**
`Mods/kcd2render/krender.ini`, `Bin/Win64MasterMasterSteamPGO/kcd2_render.asi`, `kcd2_render.log`,
`Bin/Win64MasterMasterSteamPGO/OptiScaler.ini`, `user.cfg`.
(Their current values: `krender.ini` `enabled=1, mirror=1, mirror_size=256, face_every=8, refresh_move=2.5,
refresh_frames=900`; `OptiScaler.ini` `Dx12Upscaler=auto`, `[FrameGen] Enabled=false`, `LogToFile=true`;
`user.cfg` has the four Deagle lines active — `wh_cc_LRUCache 1`, `wh_cc_MaxSkinLoaderJobs 4`,
`wh_ui_ApseOpenFaderFadeInDuration 0`, `wh_ui_ApseCameraChangeTime 0.15`.)

**Ours:** `native/src/*`, `native/build*`, `tools/*`, `docs/*`, `Tests`. The plugin installed in game is
`Bin/Win64MasterMasterSteamPGO/deagle_native.asi` (531968 bytes, 12:10 today).

Backups floating around (all reversible): `user.cfg.bak-1701`, `user.cfg.bak-flashfix`,
`krender.ini.bak-flashfix`, `OptiScaler.ini.bak-flashfix`, `OptiScaler.ini.broken-1648`,
`OptiScaler.ini.deagle-bak`, `OptiScaler.ini.bak-20261023`, `OptiScaler.ini.bak-passthrough`.

## 7. Logs that matter

- `Bin/Win64MasterMasterSteamPGO/deagle_native.log` — our plugin, rewritten every launch. Carries the
  `timing:` lines, `invtime` spans, camera positions.
- `kcd.log` + `logbackups/kcd Build(0) <date>.log` — the engine log; one backup per previous launch.
- `Bin/Win64MasterMasterSteamPGO/kcd2_render.log` — mirror session only.
- `Bin/Win64MasterMasterSteamPGO/OptiScaler.log` — only written when `LogToFile=true`.

The 18:02–18:08 run (in `deagle_native.log`) shows the current behaviour on a *short* trip:
```
18:06:44.696 invtime open (fade to black)        <- map opened
18:07:00.286 frame 15991  camera moved 8666 m to 2281 2050 110   <- map closed, camera back on the player
18:07:00.352 frame 15992  camera moved 1337 m to 1131 1375 12    <- at the destination 66 ms later
18:07:00.802 frame 15993  after 449 ms
18:07:04.954 frame 16293  after 703 ms
```
So the teleport is already instant; everything the user calls "black screen" is the load after it, plus the
21.8 s walk on long trips. Do not spend time on the fade.

## 8. Decompiler access

`/tmp/wh.c` on the ProLiant (300 MB decompile of WHGame.dll, `//==== <addr> FUN_<addr>` markers):
```
ssh -i ~/.ssh/proliant_key <user>@<server>
# if /tmp/wh.c is gone (temp file, lost on reboot):
zstd -dc ~/re/index/KCD2/WHGame.dll.c.zst > /tmp/wh.c
```
Useful one-liners (they are cheap, run them over ssh, do not copy the file):
```
grep -n "^//==== 182eaa218 " wh.c        # find a function
grep -inE "\"[a-zA-Z0-9_]*keyword[a-zA-Z0-9_]*\"" wh.c | sort -u   # find cvar / class names
```
Do not echo the ProLiant password anywhere; the key works.

## 9. Also open: the per-frame DLSS rebuild (unresolved, probably a red herring)

In the 16:46 run `OptiScaler.log` showed `NVSDK_NGX_D3D12_CreateFeature` + `CreateRenderTargetDx12` repeating
about once per frame, with the render resolution alternating `1279x720` ↔ `1280x720`. That run was also
1–2 fps, but the mirror was live at `512 / face_every=2` then, and the mirror session attributed the 100–1000 ms
frames to it (a 1277 ms stall is in `kcd2_render.log`). So the mirror is the proven cause of the frame times;
whether the DLSS rebuild is independent was never checked with the mirror off. `LogToFile=true` is now set so
the next run will show it. If a black flash comes back with the mirror idle, look there first.

## 10. Two things the user has already said

- "I don't see the loading circle but the black screen is still there, and it's not instant." (2026-10-23 18:0x,
  about long fast travel)
- The fader line `wh_ui_ApseOpenFaderFadeInDuration = 0` is still a suspect for a map/inventory-open flash.
  If a flash appears on open, that single line is the first to pull.
