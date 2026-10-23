# OptiScaler on KCD2 (1.5.6)

Status (2026-10-23): **frame generation ON — FSR 3.1 upscaler + XeFG**. Not yet
seen in a live session; the first launch after this change is the test.

## What is in the game folder

`Bin/Win64MasterMasterSteamPGO/`
- `dxgi.dll` — OptiScaler **v0.9.4** (`OptiScaler.dll` renamed to the loader name the KCD2 wiki page lists)
- `OptiScaler.ini` — release ini with the values below; the passthrough ini is kept as `OptiScaler.ini.bak-passthrough`
- `libxess_fg.dll`, `libxell.dll`, `fakenvapi.dll`, `fakenvapi.ini` — XeFG + the low-latency layer it wants on NVIDIA
- `Bin/Win64Shared/amd_fidelityfx_framegeneration_dx12.dll`, `amd_fidelityfx_dx12.dll` — the FSR-FG fallback, staged but unused while `FGOutput=xefg`

Deviations from the stock ini:

| Section | Key | Value | Why |
| --- | --- | --- | --- |
| `[Upscalers]` | `Dx12Upscaler` | `fsr31` | `FGInput=upscaler` needs OptiScaler's own upscaler: the game's DLSS call is answered with FSR 3.1 instead |
| `[FrameGen]` | `Enabled` | `true` | generate |
| `[FrameGen]` | `FGInput` | `upscaler` | FG frames come from OptiScaler's upscaler output (KCD2 has no DLSS-G and no FSR-FG API for `dlssg`/`fsrfg` to hook) |
| `[FrameGen]` | `FGOutput` | `xefg` | KCD2 wiki recommends OptiFG → XeFG: it composites the HUD itself, so no HUDFix ghosting |
| `[Spoofing]` | `Dxgi` | `false` | wiki: no spoofing needed, KCD2 has no Reflex/DLSS-FG |
| `[Menu]` | `ShowFps` / `FpsOverlayPos` / `FpsOverlayType` | `true` / `1` / `1` | an FPS + FG readout that survives OptiFG's overlay block (top right) |
| `[Log]` | `LogToFile` / `LogLevel` | `true` / `2` | OptiScaler.log is the only way to see whether XeFG initialised |

### Why XeFG and not Lossless Scaling

LS's own profile (`%LOCALAPPDATA%\Lossless Scaling\Settings.xml`, Default profile)
is LSFG **3**, mode **FIXED ×2**, scaling **Off**, `SyncMode=OFF`, capture **WGC**.
Fixed-×2 LS needs a *stable* base frame rate; with the RTSS 60 cap removed the base
wanders (~70-110), so every generated interval is a different length → judder that
reads as stuttering. In-engine FG paces against the real swapchain, so the base rate
may float and G-SYNC/V-Sync can absorb it.

### RTSS

`Profiles/KingdomCome.exe.cfg` now has `EnableHooking=0` (OSD off, `UseDetours=1`
left in place). OptiFG disables overlays anyway and RTSS' VEH hooking is documented
as crash-prone with it. RTSS keeps its profiles for other games. If a frame cap is
wanted, use the game's own V-Sync + Frame rate limit instead (see below).

## Why the 2026-10-21 attempt did nothing

`OptiScaler.log` shows it *was* loaded (as `version.dll`) and DLSS still worked
(DLSS v310.9.16 created normally), but `amd_fidelityfx_framegeneration_dx12.dll`
was never present in `Bin/Win64Shared`, and `FGInput`/`FGOutput` were left on
`auto` in a game that has no native FG. No FG backend + no FG input = no FG.

## Test it / what to expect

1. Launch. OptiScaler writes `OptiScaler.log`; look for the FSR 3.1 init line and the
   `XeFG` / `FrameGen` lines. `libxess_fg` can refuse a non-Intel GPU - if it does, the
   log says so and the fallback below is the next try.
2. FPS + FG readout sits top right (`ShowFps`), switchable with **Page Up**. FG on/off
   toggles live with **End** - that is the A/B: same spot, toggle, watch the numbers.
3. The base rate floats now (no RTSS cap). For a fixed 2x cadence instead, set the
   game's own **V-Sync ON + Frame rate limit 60** -> 120 presented, which divides 240 Hz
   evenly (KCD2's frame rate limit only applies while V-Sync is on).
4. Do **not** run Lossless Scaling with this. LS captures and re-presents an already
   generated image; the two FG layers fight.

## Fallback: FSR-FG instead of XeFG

```
FGOutput=fsrfg
```

with `amd_fidelityfx_framegeneration_dx12.dll` + `amd_fidelityfx_dx12.dll` in
`Bin/Win64Shared` (already there). FSR-FG needs HUDFix;
`[OptiFG] DisableHUDFix=auto` leaves it on. Restart needed either way.

## Rollback

```
del Bin\Win64MasterMasterSteamPGO\dxgi.dll
```

That removes OptiScaler entirely - back to the game's own DLSS, no FG. `OptiScaler.ini`
and `OptiScaler.log` are inert without it. To go back to *passthrough* instead of
removing it, copy `OptiScaler.ini.bak-passthrough` over `OptiScaler.ini`.

Note: DLSS Enabler leftovers from the earlier attempt are still in `Bin`
(`sl.*.dll`, `nvngx_dlssg.dll`, `dlssg_to_fsr3_amd_is_better.dll`,
`dlss-enabler.ini/log`). They have no loader so they do not load (checked: the only
`.dll` loaders next to the exe are `dinput8.dll` = Ultimate ASI Loader, `dxgi.dll` =
OptiScaler, and `winmm.dll` in the game root = SmokeAPI), but they are unused weight.
