# Why the inventory / map ("Apse") takes ~1-2 s to open, and how to make it faster

Static analysis only (2026-10-18): WHGame.dll in Ghidra (ProLiant project `~/re/proj/KCD2`, addresses below are
VAs at image base 0x180000000) plus level / UI data from the paks. Nothing was run in game.
Extracted data: `E:\Tools\deagle-work\inv_re\` (UI prefabs, UI XMLs, dummy scripts, `kutnohorsko_apse_entities.xml`
= the 770 room entities pulled out of `Levels/kutnohorsko/level.pak:objects_mission0.xml`).
Older, related notes: `docs/kcd2_map_open.md` (map tiles, Apse cvars).

## TL;DR
- The room is real level geometry: prefab `ui/Apse2` (+ `ui/Shop2` for trading), parked **~4 km off the map at
  (-3908, -3995, 52)** in every level (kutnohorsko, trosecko, klaster). Editor layer `showroom`, no streamed `Layer=`,
  so it is **always loaded**. Nothing about the room is spawned on open.
- On open the screen goes black **instantly** (fade-out time 0), the camera jumps to the room, the 3 dummies
  (player, horse, dog) get **re-dressed** with copies of your gear, and then the game **waits on a ReadinessObserver**
  until two things are idle: **all "Graphics" streaming** (textures, terrain, merged meshes, animations, CGF octree)
  and the **clothing system**. Only then a 0.1 s fade-in.
- Why it's the same every time: the room's textures are huge (129 textures, **434 MB**, the backplates are 4K) and
  get thrown out of VRAM after you close (KCD2's idle trim: up to **1 GB per frame** of unused mips). Every open
  re-streams them at the default budget of **4 MB per frame** (`r_TexturesStreamingMaxRequestedMB 4`). ~150-250 MB /
  (4 MB x 60-100 fps) ≈ 0.5-1 s, more at lower fps. Plus the dummy re-dress (it is undressed on every close).
- Best bets (ranked below): raise `r_TexturesStreamingMaxRequestedMB`, stop idle trim
  (`wh_r_TexturesStreamingIdleTrimLimitKB`), cap the wait (`wh_sys_ReadinessObserverTimeoutDuration`), or a 1-byte
  native patch of the wait mask (0x81 → 0x80 or 0).

## 1. What happens on open
Classes (RTTI): `wh::guimodule::C_UIApse`, `C_UIApseCamera`, `C_UICharacterBase` → `C_UIApseCharacter` (and
`C_UIShopCharacter` for shops), `C_UIApseInventory`, `wh::C_ReadinessObserver` / `C_ReadinessService`,
`wh::entitymodule::C_ClothingSystem`.

| step | function | what it does |
|---|---|---|
| 1 | `0x1816e9f70` (C_UIApseCharacter open, vtable `0x183ed8fe0` +0x70) → `0x1816e9f8c` | looks up the `CharacterHub` link of the `UIApseLinkNode` entity (`Scripts/Entities/WH/UI/UIApseLinkNode.lua` registers it via `UI.OnApseLinkNodeSpawn`) to find the dummies |
| 2 | `0x180c47894` (C_UICharacterBase open) | sets "open" flag, calls the dress function, subscribes to inventory events (`0x180c486e4`), creates a **fader** (`0x181f476b0`) at `this+0x3e8` and fades out with duration **0** (`0x181f461a0`) |
| 3 | `0x1808efa98` (vfunc +0xc8) | **dresses the dummies**: player → `InventoryDummyPlayer` (`0x1808ee70c` → link `0x1808f052c`, copy `0x1808f009c`, place/rotate), then horse and dog dummies, `0x180ad4150` positions them |
| 4 | `0x181f4d820` (fade-out done callback) | switches the Apse scene on (`0x181f52540`, also sets the Apse audio state `0x180558908`) and runs the big UI setup `0x181f4a860` |
| 5 | `0x180567d70` (C_UICharacterBase update, every frame) | once the fader exists and the dummy's character is loaded (`0x181f4a260`), creates `C_ReadinessObserver` (`0x180b3ff14`) with **mask 0x81**: `MOV EDX,0x81` at **`0x180567e99`** → `0x180fc3ec0` |
| 6 | lambda `0x181f4dc00` (vtable `0x183ed6b98`) | when the observer fires: fade-in over `wh_ui_ApseOpenFaderFadeInDuration` (0.1 s), fader released |

The observer fires when **every readiness task whose category matches the mask is idle, or on timeout**
(`0x181ddcdc0`: timed-out OR `C_ReadinessService::IsReady(mask)` `0x1808baee0`). Timeout = 
`wh_sys_ReadinessObserverTimeoutDuration` (default **50 s**, read at `0x180fc3f40`).

Readiness tasks and their category bits (read from each class's `GetCategory`):

| bit | task (name) | where |
|---|---|---|
| **0x01** | `TextureStreaming`, `TerrainStreaming`, `MergedMeshStreaming`, `AnimationStreaming` (CStreamEngine, ready = 0 requests in flight) | `0x180d26eb8` |
| **0x01** | `OctreeStreaming` (C3DEngine, CGF/object streaming) | `0x180a66278` |
| 0x02 | C_XGenAIModule (AI) | |
| 0x10/0x20 | C_GameProfileManager | |
| 0x40 | C_SaveGameManager | |
| **0x80** | **C_ClothingSystem** (ready = progress 1.0 = no character's clothing still loading; global, NPCs included) | `0x1814d8714` / `0x1814d8738` |
| 0x100 | MovedObservers | `0x180f4eb14` |
| 0x200 | HLodStreaming | `0x1813f3b1c` |
| 0x400 | C_LODManager | |
| 0x800 | EntityStreaming | `0x181684a64` |
| 0x1000 | SvotiStreaming / Propagation / Upload | `0x180a5e5ac` |
| 0x2000 | ObjStreaming (CObjManager) | `0x180b6cec4` |

So the Apse (mask 0x81) waits for: **all texture / terrain / vegetation / animation / CGF streaming in flight
anywhere** + **all clothing loads**. (For comparison, TimeDemo waits on 0x3f93 "GraphicsAndScripts", the AI
ReadinessWait node defaults to 0x3f91.)

### What the room costs to show
- Room: 659 Apse entities = 469 GeomEntity (`objects/ui/panels/*.cgf`, 186 CGFs, 6.6 MB), **137 lights (57 cast
  shadows)**, env probe (`textures/cubemaps/_ui/ui_3d/ui3d_cm.tif`), 2 fog volumes, 2 particle effects, 5 cameras,
  dummies `InventoryDummyPlayer` (+Female), `InventoryDummyHorse`, `InventoryDummyDog`
  (`Scripts/Entities/AI/InventoryDummy*.lua`, `ControlProfile = 2 -- hidden, hide status is controlled by Apse`).
- Textures: `Objects/ui/panels/*` (IPL_Objects-part5.pak) = **129 textures, 434 MB of mips**. Biggest:
  `ui3d_character_backplate_a/b_ddna` and `ui3d_questlog_panel_ddna` 33.6 MB each, the `_diff`/`_spec` of those 11 MB
  each, ~20 more at 8.4 MB. None of it is in `Libs/UI/TexturePreload.xml` (that file only preloads `Libs/UI/Textures/*`
  icons and a few Apse UI sheets).
- Flash: `Libs/UI/Apse*.gfx` (30-90 KB each) on 4096x2048 dynamic textures. `wh_ui_ApseUnloadMode` 2 unloads every
  Apse movie except the map when hidden → reloaded on the next open (small CPU cost).

## 2. What happens on close
`0x180c41e00` (C_UICharacterBase close, vtable +0x90): Apse scene/audio off (`0x181f52540(…,0)`), kills observer +
fader, then **`0x1808f070c` (vfunc +0xd0) tears the dummy copies down**: for every linked source→dummy it
unsubscribes, calls the dummy entity's vfunc +0xc0 `(0x20000000, 2, 0)` and **deletes the object at
`dummy+0x180`** (most likely the dummy's equipment/clothing instance), then clears the map. Next open dresses again.

Meanwhile the texture streamer trims: `wh_r_TexturesStreamingIdleTrimLimitKB` default **0x100000 KB = 1 GB/frame**
("Max amount to trim each frame when there are no streaming request"). The room's 4K mips are unused the moment you
close, so they are likely gone within frames. (Probably also why the world can look blurry right after closing: the
world's mips get trimmed while you sit in the room. Unverified.)

## 3. Cvars (defaults read from the registration code)

| cvar | default | flags | effect on the Apse |
|---|---|---|---|
| `r_TexturesStreamingMaxRequestedMB` | **4.0** MB/frame | 0 | streaming budget. The devs' env-probe baker raises it to 256 and `...MaxRequestedJobs` to 128 (`0x182529b9c`) |
| `r_TexturesStreamingMaxRequestedJobs` | 32 | 0 | parallel stream jobs |
| `wh_r_TexturesStreamingIdleTrimLimitKB` | 1048576 (1 GB) | 0 | how much unused mip data is dropped per idle frame |
| `wh_r_texturesstreamingScheduleTimeLimitUs` | 3000 | 0 | per-frame time for trim/kick/stream |
| `r_TexturesStreamPoolSize` | 1024 MB | | pool (dynamic pool cvars also exist) |
| `wh_sys_ReadinessObserverTimeoutDuration` | **50 s** | **2 (VF_CHEAT)** | caps every C++ readiness wait (Apse included) |
| `wh_sys_ReadinessObserverEnabled` | 1 | 2 (VF_CHEAT) | 0 = service fires every observer immediately (`0x180e88e10`); global |
| `wh_sys_ReadinessObserverDebugDraw` | 0 | 2 | 1 = observers, 2 = + tasks on screen |
| `wh_sys_ReadinessObserverProfiler` / `wh_sys_ReadinessTaskProfiler` | 0 / 1 | 2 | saves profiles to `%USER%/ReadinessProfiler/` |
| `wh_ui_FaderDebug` / `wh_ui_FaderProfiler` | 0 | | active faders / profile to `%USER%/FaderProfiler/` |
| `wh_ui_ApseOpenFaderFadeInDuration` | 0.1 s | 0 | the fade-in after the wait |
| `wh_ui_ApseCameraChangeTime` | 0.5 s | 0 | pan between panels (tab switches, not open) |
| `wh_ui_ApseUnloadMode` | 2 | 0 | 0 = keep all Apse movies loaded |

AI / quest nodes have their own timeouts (`WH_AI_ReadinessWait_TimeoutDuration`, the FaderBarrier one), so the global
timeout cvar mostly hits C++ users (Apse, shop, TimeDemo...). Cheat-flagged cvars: the console blocks VF_CHEAT
**commands** unless cheats/devmode are on (`0x18079f2f8`); whether it blocks VF_CHEAT **variables** set from
user.cfg / `System.SetCVar` is unverified (our killcam already sets `t_Scale` from Lua). A native plugin can always
set them via `ICVar::Set`.

No "keep loaded" layer flag applies: the room is not in a streamed layer at all. The thing that gets unloaded is its
texture mips (and the dummy's gear).

## 4. Mod options, ranked (gain vs risk)
1. **`r_TexturesStreamingMaxRequestedMB 32` (try 16-64) + `r_TexturesStreamingMaxRequestedJobs 64`.** Data-only
   (user.cfg / console / our Lua at start-up). Gain: the room streams up to 8x faster → expect most of the wait gone.
   Risk: low-medium, bigger I/O + upload bursts in the open world (possible hitches when riding fast). Not cheat.
2. **`wh_r_TexturesStreamingIdleTrimLimitKB 0`** (or a few MB). Data-only. Gain: room mips may stay in VRAM between
   opens → 2nd+ opens near instant, and less blur after closing. Risk: pool stays full, trimming only under pressure;
   needs VRAM headroom (8 GB+ GPUs fine, unverified). Not cheat.
3. **`wh_ui_ApseOpenFaderFadeInDuration 0`** + **`wh_ui_ApseUnloadMode 0`**. Data-only, safe. Gain: -0.1 s fade,
   no movie reloads (tens of ms, a few MB RAM).
4. **`wh_sys_ReadinessObserverTimeoutDuration 0.3`**. Gain: hard cap, open ≤ ~0.4 s. Risk: global for C++ fades
   (fast travel, sleep etc. may fade in with blurry textures / half-dressed NPCs); cheat-flagged, may need the
   plugin. `wh_sys_ReadinessObserverEnabled 0` is the same but uncapped and worse: don't ship it.
5. **Native 1-byte patch, Apse/shop only**: at VA `0x180567e9a` (RVA `0x567e9a`, imm of `MOV EDX,0x81` = bytes
   `BA 81 00 00 00` at RVA `0x567e99`) write `0x80` (wait only for the dressed dummy, textures sharpen in after)
   or `0x00` (no wait at all). Verify the 5 bytes before writing (game-version specific). Gain: the whole streaming
   wait, scoped to Apse + shop. Risk: low-medium, you see the room sharpen for a moment; shop screen also affected.
6. **Smaller room textures** (data-only): ship `Objects/ui/panels/*` at 2K/1K (the 4K backplates + questlog first).
   Gain: 4-16x less to stream every open. Risk: blurrier panels, and re-encoding into KCD2's split-mip `.dds.N`
   format (needs our texture tools).
7. **Preload the room textures** via an override `Libs/UI/TexturePreload.xml` (`<Preload file="Objects/ui/panels/*"/>`).
   Unverified that the preloaded (non-streamed) copy is the one the room's materials use; CryEngine keys textures by
   name + flags, so it may load a second copy and change nothing. ~434 MB VRAM. Last resort.
8. **Don't undress on close** (skip `0x1808f070c`). High risk: stale gear on the dummy, broken change listeners. Skip.

## 5. Unverified, and the tests that settle it (the user runs these; never from a Claude session while the game runs)
Unverified: which task actually blocks (texture vs clothing vs other streaming); that idle trim really drops the
room mips; how many MB the inventory view needs; that `0x80` still shows a fully dressed dummy; whether cheat cvars can
be set from user.cfg / Lua; `0x1808f070c`'s deleted object really being the gear copy.

Tests (stand in the same spot, wait 10 s, open the inventory 5 times, before vs after each change):
1. **Who blocks:** console `wh_sys_ReadinessObserverDebugDraw 2` while opening: shows the observer and the tasks it
   waits on (TextureStreaming / OctreeStreaming / ClothingSystem). Then `wh_sys_ReadinessObserverProfiler 1`
   (+ `wh_sys_ReadinessTaskProfiler 2`) and `wh_ui_FaderProfiler 1`: profiles land in `%USER%/ReadinessProfiler/` and
   `%USER%/FaderProfiler/` (user folder, probably `Saved Games\kingdomcome2`): exact wait per open.
2. **Timing without the profiler:** screen-record at 60 fps, count frames from the key press to the first lit frame.
3. **A/B** one change at a time: (a) `r_TexturesStreamingMaxRequestedMB 32`, (b) `wh_r_TexturesStreamingIdleTrimLimitKB 0`
   (open, close, open again within 10 s: 2nd open should be faster), (c) `wh_ui_ApseOpenFaderFadeInDuration 0`,
   (d) `wh_sys_ReadinessObserverTimeoutDuration 0.3` (then read it back to see if the cheat flag blocked it).
4. Cross-check: `r_TexturesStreamingDebug` / `...DebugDumpIntoLog 1` during an open should show `Objects/ui/panels/*`
   requests in kcd.log.

## Tools left on the ProLiant
`~/re/scripts/kcd2_daemon.py` keeps the KCD2 project open read-only (JVM `-Xmx4g`, ~1.2 GB RSS); ask it with
`~/re/scripts/qk <cmd>`: `dec`, `refs`, `callers`, `callees`, `data`, `str`, `strref`, `strin <fn> [1]`, `sym`,
`near`, `rtti`, `dis`, `rangestr`, `insgrep <start> <end> <text>`. Queues: `~/re/qk/in|out`. Stop:
`kill $(cat ~/re/qk/daemon.pid)` (don't `pkill -f`, it matches your own ssh shell).

## Measured, then decompiled (2026-10-18, later)
- Live (`deagle_invtime 1`): the black screen is mostly one gate, `0x181f4a260`, called each frame by the screen's
  update (`0x180567d70`, its only caller): 450-700 ms every open. Then the readiness wait (40-360 ms), then the fade.
  Streaming cvars, the observer timeout / off, `wh_ui_ApseUnloadMode 0`, `wh_ui_ApseCameraChangeTime 0.05` and short
  fades changed nothing.
- The gate: `RCX = [screen+0x20]` (the dummy actor), `0x18068c6c4` returns its `+0x688`, made lazily by
  `C_ClothingAttachmentManagerFactory` (`gEnv+0xe0 -> +0xb0`, vfunc 0 = `0x180753f90`; the concrete class when
  `wh_cc_ClothingAM` (cvar struct `+0x264`) is 1: `wh::entitymodule::clothing::C_ComponentAttachmentManager`,
  vt `0x183e8b740`). The gate calls its vfunc `+0x80` = `0x181e96650`: `return state(+0x38) == 1`.
- `0x181e96a00` (the manager's update) moves it on only when every clothing part in its list (`+0x20`, count `+0x28`,
  part state `+0x58`: 0/1 = not loaded) has loaded. So the 450-700 ms is the dummy's clothing skins loading, every open,
  because the dummy is undressed on close and re-dressed on open.
- Clothing cvars (registration `0x1815a7864`, struct from `0x1804aade0`; defaults decoded from the register args, no
  config file in the paks overrides them):
  `wh_cc_MaxSkinLoaderJobs 1` ("maximum count of simultaneously running loader jobs": the skins load one at a time),
  `wh_cc_LRUCache 0` ("LRU cache for clothing system": off), `wh_cc_MaxSkinLoadingInProgress 40`,
  `wh_cc_MaxSkinLoaderJobQueueSize 20`, `wh_cc_PreloadMaterials 1`, `wh_cc_HiddenInvisibleManagerPriorityDecrease 100`
  (the dummy is behind the black screen), `wh_cc_TotalUpdateBudget 10`.
- Next, in game (a setting, not a patch): A/B with `deagle_invtime 1`, 5 opens each: defaults, then
  `wh_cc_LRUCache 1` (2nd open on should be faster if the skins stay cached), then `wh_cc_MaxSkinLoaderJobs 4`. If a
  cvar only applies at start, it goes in `user.cfg`. `deagle_invtime 2` (the gate skipped, a test, save first) tells
  whether the readiness wait (it watches clothing too) just takes the time over.
