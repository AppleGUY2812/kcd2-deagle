# KCD2 free camera ("shoot and fly away"): RE and design (WHGame 1.5.6, static)

Code: `native/src/freecam.cpp` / `freecam.h` (new module, not wired in yet: see "Lines to add"). It compiled cleanly
(MSVC, /W3, no warnings) in a scratch copy of `native/` with the lines below applied. The repo's `main.cpp` and
`CMakeLists.txt` were left untouched.

## TL;DR
- Don't move the CView camera. Swap the matrix of **CSystem's view camera** (`CSystem+0x288`, the copy that
  `ISystem::SetViewCamera` makes) right after `SetViewCamera`, and **put the real one back right after
  `ISystem::Render`**. Render, culling, occlusion, LOD and streaming then use the freecam. Everything that runs in the
  game update (Lua `System.GetViewCamera*`, so the Deagle's shots, the game's interaction rays, the audio listener) and
  the plugin's own `OnCamera` (viewmodel, FMOD listener) keep Henry's real eye.
- Rotation = the game camera's rotation every frame. The mouse turns Henry, so his shots go where the freecam looks
  (along a parallel line from his eye).
- Two MinHook hooks: `SetViewCamera` and `Render`. Their addresses come from gEnv->pSystem's vtable, so no byte
  pattern is needed. The install checks the `+0x288` offset in all three ISystem functions and refuses if they
  disagree.

## 1. RE findings

### The frame (CCryAction::PostUpdate, `0x669C88` = CCryAction vtable slot 11)
Lua, input actions (`OnAction` → `Fire`) and entity updates run earlier in the frame, in `ISystem::Update` (CSystem
vtable +0x28). Then PostUpdate runs:

| step | address | what |
|---|---|---|
| `CViewSystem::Update(min(dt, 0.1))` | `0x7F0220`, called at `0x669DD7` | loops over the views |
| ↳ `CView::Update(dt, isActive)` | `0x7F0EC0` (CView vtable +0x10) | returns at once if not active. It ends by writing the 3x4 matrix into `m_camera` (`CView+0xE8`) and calling `UpdateFrustumPlanes` (`0x537BA4`) at `0x7F1A63`. **Call #1 seen by HkFrustum.** |
| ↳ `CView::UpdateAudioListener(cam)` | `0x7F0AEC` | `IEntity::SetWorldTM` (+0xF0) on the listener entity at `CView+0x418`: the **game's audio listener = this camera** |
| ↳ `view->GetCurrentParams` / `SetCurrentParams` | CView vtable +0x38 / +0x30 | clears `justActivated` (copied into `cam+0x2F4`) |
| ↳ optional rotation override | `0x7F03CC`..`0x7F04EE` | `UpdateFrustumPlanes` again (`0x7F04EE`) |
| ↳ camera shake (`wh_c_CameraShake`) | `0x7F04F8`..`0x7F0914` | rewrites the matrix, `UpdateFrustumPlanes` at `0x7F0914`: **the final camera** |
| ↳ **`ISystem::SetViewCamera(m_camera)`** | call at `0x7F0952`; CSystem vtable **+0x430** = `0x7F0AE0`: `add rcx,0x288; jmp CCamera::operator= (0x426E80)` | copies the whole CCamera into **`CSystem+0x288`** (`m_ViewCamera`) |
| ↳ duplicate-frame check | `0x7F09BF` | `r_DuplicateLastFrameOnCameraChangeWH`: GetViewCamera → `justActivated` → renderer +0xA40 |
| `I3DEngine::PrepareOcclusion(GetViewCamera())` (likely) | `0x669E0F` → 3DEngine vtable +0x40 = `0x4E46AC` | skips a camera at the origin, then 0x4E4C50 on the occlusion object. **Must already see the freecam**, otherwise the coverage buffer culls things the freecam can see. |
| **`ISystem::Render`** | CSystem vtable **+0x48** = `0x43E464` ("CSystem::Render"), called through a lambda (`0x20E11C0`, `[this+0x48]->vt+0x48`) | `PrepareOcclusion`-like 3DEngine +0xD0 (`m_ViewCamera`), then **`I3DEngine::RenderWorld` (+0x18 = `0x43EBC8`) with `CreateGeneralPassRenderingInfo(CSystem+0x288)`**, then 3DEngine +0xD8 |

- `ISystem::GetViewCamera` = vtable **+0x438** = `0x5D77F0`: `lea rax,[rcx+0x288]; ret`.
- Lua `System.GetViewCameraPos` (`0x39A8F90`) and `GetViewCameraDir` (`0x39A8EEC`) read `m_pSystem->GetViewCamera()`:
  translation `+0x0C/+0x1C/+0x2C`, forward = column 1 (`+0x04/+0x14/+0x24`). `GetViewCameraFov` reads `+0x30`.
- Neither `SetViewCamera` nor `Render` has a direct call or a second vtable (both appear only in `.?AVCSystem@@`).
- gEnv in this build is at `0x492D800` (gEnv+0xA0 EntitySystem is used by CView::Update; +0xC0 System; +0x08 3DEngine;
  +0x108 Renderer). That matches the members in `main.cpp`.

### Answers to the questions
- **Is writing the matrix in HkFrustum (before `g_origFrustum`) enough?** For the picture, yes, if it's written on
  the *last* call (`0x7F0914`), because `SetViewCamera` copies that camera. But it leaks:
  - `CSystem::m_ViewCamera` stays at the freecam until the next PostUpdate, so in the next frame Lua's
    `GetViewCameraPos` (the shot origin, `deagle_main.lua:1567`), every other `GetViewCamera*` use in the mod, and the
    game's interaction / look-at rays all start from the freecam.
  - `CView::UpdateAudioListener` and the plugin's own `OnCamera` (viewmodel placement, FMOD listener) read the same
    `m_camera`, so the gun would be glued to the freecam.
  - HkFrustum fires 2-3 times on that camera per frame. Only a write before the last call survives the shake block.

  So no: override the system copy and restore it after the render.
- **`ISystem::SetViewCamera`?** Yes, as the *entry point*: hook it and replace the matrix in the copy just made (then
  `UpdateFrustumPlanes` on that copy). The matching **exit point is `ISystem::Render`**: restore after it.
- **`CView::SetCurrentParams`?** No. It moves the view itself: Lua, the listener and the viewmodel follow, and it's
  view state the CView keeps.
- **Other readers of the view camera:**
  - *Mod viewmodel*: `OnCamera` copies `CView+0xE8` (the real camera), so the gun stays at Henry's real eye. No
    change needed. From the freecam you see the floating viewmodel (arms + gun) at his head. While the gun is up,
    Lua's `hideBody` hides his body.
  - *Mod FMOD listener*: `UpdateListener(g_cam)` stays at Henry. So does the game's own listener (`0x7F0AEC`), which
    keeps the two consistent. Optional line below moves ours.
  - *Lua*: real eye, because Lua runs in `ISystem::Update`, before the next `SetViewCamera`. Shots, tracers' start
    points and impact effects are unchanged.
  - *Game*: interaction / pick-up rays, the HUD, animation / particle distance LODs that run during the update all
    stay at Henry. Things near the freecam but far from Henry may animate at a lower LOD rate. Render-side LOD and
    streaming (RenderWorld gets the freecam) do follow the freecam.

## 2. Design (`freecam.cpp`)
- `Freecam_Install(env, g_origFrustum)`: reads gEnv+0xC0 → vtable. It checks GetViewCamera
  (`48 8D 81 <off> C3`), SetViewCamera (`48 81 C1 <off> E9`) and Render (`4C 8D B6 <off>` within its first 0x100
  bytes). The offset must be the same in all three. Then it hooks both, Render first. If either hook fails, neither
  is installed.
- `HkSetView` (after the original): remembers the real matrix and polls the keys. Only while the game window is in
  the foreground (`GetForegroundWindow` → our pid) does it:
  - move `g_pos` by wall-clock time (QPC; dt clamped to 0.1 s), so it flies at the same speed in F9 slow motion;
  - clamp `g_pos` to `max` metres from the eye;
  - write `rotation(real) + g_pos` and call `UpdateFrustumPlanes`.
- `HkRender` (after the original): writes the real matrix back and calls `UpdateFrustumPlanes`. A second
  `SetViewCamera` in the same frame that was built from ours keeps the real eye.
- Keys (all free in `defaultProfile.xml` and `keybindSuperactions.xml`, checked):
  - toggle: `\` (VK_OEM_5);
  - fly: **Home / End = forward / back, Delete / PgDn = left / right, PgUp / Insert = up / down** (the six-key block
    laid out like Q W E / A S D);
  - **Shift** ×5.

  **Arrow keys are NOT free.** The superactions `up / down / left / right` bind them as second keys for
  `moveforward / moveback / moveleft / moveright` and photo mode, so arrows would walk Henry. `keys arrows` switches to
  arrows + PgUp / PgDn, for after unbinding them in Controls. Delete and Home are bound only in the menu map
  (`menu_clear`, `menu_reset_keybind`). F1 = photo mode, F2 and F9 = the mod, F8 = cheat menu, F10 / F11 = debug draw,
  F12 = Steam screenshot.
- **Never stuck on:**
  - `Freecam_Reset("hello")` on every save load / script start;
  - auto-off when the real eye jumps more than 25 m in one frame (load, fast travel, cutscene) or turns non-finite;
  - when off, nothing is written. Since the real camera is restored after every Render, turning it off is exactly the
    old first person. A frame where Render doesn't run gets overwritten by the next `SetViewCamera`.
  - focus loss: keys are ignored, and the toggle needs a release first (no toggle on alt-tab back).
- Commands (`[DN] freecam ...`): `on | off | toggle | status | speed <m/s> (4) | fast <x> (5) | max <m> (500) |
  keys nav|arrows | key <vk> (0 = no toggle key)`. Every command logs the state to `deagle_native.log`.

## 3. Lines to add (another session owns these files)
`native/CMakeLists.txt` lists the sources, so add the file there:
```cmake
add_library(deagle_native SHARED src/main.cpp ... src/settings.cpp src/freecam.cpp ${MINHOOK_SRC})
```
`native/src/main.cpp`:
```cpp
#include "freecam.h"                                                    // with the other includes
// Init(), right after  Log("gEnv %p", (void*)g_env);
if (g_env) Freecam_Install(g_env, (void*)g_origFrustum);
// Command(): first statement inside  if (!strcmp(cmd, "hello")) {
Freecam_Reset("hello");
// Command(): a new branch, e.g. before  else if (!strcmp(cmd, "vm")) {
else if (!strcmp(cmd, "freecam")) Freecam_Command(rest);
```
Optional: the mod's sounds heard from the freecam (one frame late, harmless). In `OnCamera`:
`float fc[12]; if (g_fmod) { UpdateListener(Freecam_Matrix(fc) ? fc : g_cam); fm.update(g_fmod); }`

Lua (`deagle_main.lua`, next to the other `AddCCommand`s):
```lua
System.AddCCommand("deagle_freecam", "Deagle:Native('freecam ' .. Deagle.Arg([[%line]]))", "deagle_freecam [on|off|status|speed <m/s>|fast <x>|max <m>|keys nav|arrows|key <vk>]: free camera. \\ toggles; Home/End/Del/PgDn fly, PgUp/Ins up/down, Shift faster")
```
No action-map entry is needed: the plugin reads the toggle key itself. If you'd rather bind it in
`deagle_actionmap.xml` (`<action name="deagle_freecam" onPress="1" keyboard="backslash" />` + an `OnAction` branch
that calls `self:Native("freecam toggle")`), also send `freecam key 0`, or the key toggles twice.

## 4. Risks
- **Occlusion:** the call I take for `PrepareOcclusion` (3DEngine +0x40) runs after `SetViewCamera`, so it gets the
  freecam. If KCD2 also builds occlusion from another camera elsewhere, objects could pop or vanish far from Henry.
- **Things that stay at Henry:**
  - animation / particle update LODs and other distance checks that run in the game update, plus jobs still running
    after Render returns;
  - entity / AI streaming that follows the player, not the camera. That's why the default `max` is 500 m.
- **Henry's model:** while the gun is up his body is hidden (`cfg.hideBody`). When it's not, the first-person body
  may show without a head. Live check.
- **Toggle frame:** the jump can smear one frame of motion blur / TAA. If it does, set `justActivated`
  (`cam+0x2F4`) on the toggle frame.
- **Other views:** photo mode (F1), dialogs and cutscenes may switch the active view. The 25 m jump guard turns the
  freecam off on a cut. A dialog camera that eases in may not trigger it, but nothing breaks.
- **Game update:** if a patch changes ISystem's vtable or the `0x288` offset, the install refuses and logs
  `ISystem layout differs`.
- **`justActivated` / `r_DuplicateLastFrameOnCameraChangeWH`** are evaluated on the CView camera, before our swap,
  so they're unaffected.

## 5. Live test plan (game closed while installing)
1. Wire in the lines, build, install. In `deagle_native.log`, look for
   `freecam installed: CSystem … camera +0x288 SetViewCamera … Render …`.
2. Load a save, then `deagle_freecam status`.
3. Press `\` → `freecam on at …`. Home / End / Del / PgDn / PgUp / Ins fly, Shift is faster. The mouse turns the view
   and Henry together, and Henry doesn't walk.
4. **Shot origin** (the key check). Draw the gun, then:
   - `deagle_lua System.LogAlways(tostring(System.GetViewCameraPos().x))` and compare with the plugin's
     `view frame … cam pos` line. It must be Henry's eye, not the freecam.
   - Fire: the tracer starts at the gun, seen from the side; the hit lies on the line from his eye.
5. F9 slow motion: the camera still flies at full speed, and the bullets crawl.
6. Fly 100-300 m away:
   - no missing or popping geometry (occlusion);
   - LODs and textures sharpen as you approach (streaming);
   - shadows are fine.
7. `\` again: exactly the old first person. Also try during ADS / F2 zoom (the FOV follows the game).
8. Load a save with the freecam on → `freecam off (hello)`, and first person after the load. Try fast travel too
   (`view jumped`).
9. Alt-tab while holding a fly key or `\` → no drift and no toggle on return.
10. Holster the gun in the freecam and look at Henry: is his body and head visible? Decide whether Lua should unhide
    the body while the freecam is on.
