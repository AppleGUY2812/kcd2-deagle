# Why KCD2's map takes seconds to open, and how to speed it up

Sources: `IPL_GameData.pak` (`Libs/UI/*`, `Libs/UI/UIElements/*.xml`, `Libs/UI/TexturePreload.xml`,
`Libs/UI/Textures/Maps/*`, `Prefabs/ui/3dui_f_map_panel_elements.xml`) and WHGame.dll cvar strings / registration
code (1.5.6). **Guess** marks inference.

## TL;DR
- The map is part of the **"Apse"**, KCD2's 3D inventory / map / codex screen. Each panel is a **Scaleform movie**
  (`Libs/UI/ApseMap.gfx`, rendered into a 4096×2048 dynamic texture on a 3D panel; `Prefabs/ui/3dui_f_map_panel_*`).
  The camera **pans between panels** (`wh_ui_ApseCameraChangeTime` = **0.5 s**). On open, the Apse stays **faded out
  until its graphics are ready**, then fades in (`wh_ui_ApseOpenFaderFadeInDuration` = 0.1 s).
- The map's art is **285 streamed tile textures, 529 MB** in `Libs/UI/Textures/Maps/` (split mips `.dds`, `.dds.1`
  … `.dds.6`, top mip 2 MB per tile). The world maps alone: `global_map_kutnohorsko_*` 62 MB (≈27 tiles × 6 mips),
  `global_map_trosecko_*` 25 MB, plus per-town maps (kutna_hora 45 MB...).
- **`TexturePreload.xml` preloads the map icons but not the map tiles.** Every open streams the tiles from disk at
  the zoom shown, and the Apse waits for them ("faded out until graphics is ready"). Fast NVMe and big VRAM pools
  make that instant; HDD / SATA or tight streaming pools give 2–3+ s. **So it's load cost, plus ~0.6 s of designed
  camera pan and fade.**
- The map movie itself is **not** unloaded on close by default: `wh_ui_ApseUnloadMode` default is **2** = "unload
  flash elements when hidden, except the map".

## 1. What opening the map involves
- UI tech: CryEngine **Scaleform GFx** (`sys_flash_*` cvars; `.gfx` movies). `UIElements/ApseMap.xml`:
  `<UIElement name="ApseMap" lazy_update="0" mouseevents="1" cursor="0" controller_input="1">`,
  `<GFx file="ApseMap.gfx">`, `Align mode="fixdyntexsize"`, `Position width="4096" height="2048"`.
- Functions called on it: `Open`, `Activate` / `Deactivate`, `SetGlobalMap`, `SetLocalMaps`, `SetPlayer`,
  `AddQuestMarkers`, `AddPoiMarkers`, `SetMapLabels`... Events: `OnUpdatingChanged(updating)`, which signals tiles
  being updated (useful for timing), `OnPlayAudio`, tooltips, fast-travel highlights.
- Siblings: `ApseMapLegendList.gfx` (legend), `MapClouds.gfx` + `mapClouds.xml` (211 KB cloud / fog-of-war
  shapes).
- Textures: tiles named `<map>_<n>.dds` with mips in `.dds.1`..`.dds.6`. Higher-numbered mip files are the larger,
  more detailed levels (2 MB for `.dds.6`). Icons `Libs/UI/Textures/Icons/Map/*` are preloaded.

## 2. Lifetime and the cvars that matter
Apse cvars (WHGame.dll; defaults read from the registration code):

| cvar | default | meaning (game's own description) |
|---|---|---|
| `wh_ui_ApseUnloadMode` | **2** | 0 = no unload flash, 1 = unload flash elements after close apse, 2 = unload f.e. when hide except map, 3 = unload when hide |
| `wh_ui_ApseOpenFaderFadeInDuration` | 0.1 | "When apse opens, it is faded out until graphics is ready, this is how many seconds the fade-in takes." |
| `wh_ui_ApseCameraChangeTime` | **0.5** | "Set time in sec to move on next panel" |
| `wh_ui_ApseLightsProlong` | 0.15 | delay of lights |
| `wh_ui_ApseShieldActivateAnimationDuration` / `...Deactivate...` | 0.05 / 0.2 | panel light animations |
| `wh_ui_ApsePauseRatio` | 1000 | game time divided by this while the Apse is open |
| `wh_ui_ListShieldMemoryTimeLimit` | 30 | seconds the focused tab is remembered |

Texture streaming (CryEngine): `r_TexturesStreaming`, `r_TexturesStreamPoolSize`, `r_TexturesStreamDynamicPool*`
(`...MaxSizeWH`, `...TargetVramUsage`), `r_TexturesStreamingMipBias`, `r_TexturesStreamingMaxRequestedMB`,
`r_TexturesStreamingMaxRequestedJobs`, `r_TexturesStreamingPrecacheRounds`, `r_TexturesStreamingDebug` (+
`...DebugDumpIntoLog`).

## 3. Mod options (most to least likely to help)
1. **Preload the world-map tiles** (high confidence it's the main cost; ~90 MB VRAM). Ship an override
   `Libs/UI/TexturePreload.xml` in our pak: a copy of the original plus, under `<Common>`:
   ```xml
   <Preload file="Libs/UI/Textures/Maps/global_map_kutnohorsko_*"/>
   <Preload file="Libs/UI/Textures/Maps/global_map_trosecko_*"/>
   ```
   (or `Libs/UI/Textures/Maps/*` for all 529 MB, too much). **Guess**: a filename wildcard works (the file only shows
   `dir/*`). If not, list the ~40 tiles by name. Without `streamable="1"` they're loaded whole at level load and stay
   resident. Cost: longer level load and ~90 MB of VRAM.
2. **Cvars** (sure they exist; the effect needs measuring). Set them in the user's `user.cfg` (game root), from our
   mod's cfg if KCD2 runs one (**guess**: `Mods/deagle/mod.cfg` like KCD1), or from the plugin at startup (we already
   run console commands: `g_pendingConsole` / ExecuteString):
   - `wh_ui_ApseCameraChangeTime 0.15`: cuts the designed pan (0.5 s → 0.15 s).
   - `wh_ui_ApseOpenFaderFadeInDuration 0`: no fade.
   - `wh_ui_ApseUnloadMode 0`: keeps every Apse movie (legend, inventory...) loaded. The map already stays, so this
     only helps switching tabs.
   - `r_TexturesStreamPoolSize` higher (e.g. 2048+ on 8 GB GPUs; check the current value first) so tiles survive
     between opens. Helps every open after the first.
3. **Plugin hook:** not needed unless 1 and 2 fail. Candidates: force `ApseMap` `Open` at level start (pre-warm),
   or request the tile textures' streaming at full mip on load.

**How to verify:**
- Screen-record the M key → map fully sharp (60 fps frame count), 3 opens each, before and after.
- Or run `r_TexturesStreamingDebug 1` / `...DebugDumpIntoLog 1` during an open and see `Libs/UI/Textures/Maps/*`
  requests in kcd.log.
- The `OnUpdatingChanged` true → false span is the tile update time. A UI event listener could log it.

## 4. Tried 2026-10-23 and pulled: preloading the region maps
- The idea: `src/Libs/UI/TexturePreload.xml` = the game's file plus two lines under `<Common>`:
  `<Preload file="Libs/UI/Textures/Maps/global_map_trosecko_*"/>` and the same for `global_map_kutnohorsko_*`, so
  the 25.3 + 61.7 MB of region tiles stay resident instead of being re-streamed every open.
- **It emptied the in-game map.** First session with it: the Apse map came up with no tiles at all (14:43, no error
  in the log). It also cost 87 MB of VRAM during a period when VRAM was already at 7.5 / 8.2 GB.
- Why it was never going to work as written: a tile is a base `.dds` **plus six separate mip files** (`.dds.1` ..
  `.dds.6`), and the wildcard matches all of them (68 files for trosecko, 159 for kutnohorsko). Preloading a mip file
  as a texture is not the same thing as preloading the texture. If this is ever tried again, list the 27 base `.dds`
  names explicitly, one line each, and never a `.dds.N`.
- Pulled the same day; the game's own `TexturePreload.xml` is back (the file is gone from `src/`).

## 5. What's on purpose## 5. What's on purpose
- The camera pan between Apse panels (0.5 s), the open fade (0.1 s), and the panel light animations (0.05–0.2 s,
  0.15 s light delay) are designed. Together ≈ 0.6–0.8 s, the same on every PC.
- Anything beyond ~1 s is the "faded out until graphics is ready" wait, i.e. tile streaming. That matches the
  reports: instant on fast PCs, 3+ s on weak ones.
