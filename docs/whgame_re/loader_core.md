# Q6: loader / core split (Claude 5, from native/src and precedent)

## What the plugin holds today (native/src, 2026-10-15)
- Hooks (MinHook, all in main.cpp Init + freecam.cpp): CCamera::UpdateFrustumPlanes (HkFrustum, the per-frame step),
  the System.LogAlways script handler (HkLog, the [DN] bridge), freecam's SetView + Render.
- State: held emitters (g_held ring 256, g_traces 128, flashes 8), the viewmodel id, motion springs, settings
  (settings.cpp file), the KCD2 FMOD core system (g_fmod + loaded sounds), Ready or Not's FMOD 2.03 (fmor.dll /
  fmorstudio.dll loaded with LoadLibraryEx, 7 banks = 531 ms to load), the console queue.
- Threads: ron.cpp says Lua commands and the camera step "can come from different threads" (it locks g_mu);
  freecam.cpp says its hooks run on the main thread. Not settled: one log line with GetCurrentThreadId() in HkLog and
  HkFrustum settles it.
- CRT: static (/MT). A static-CRT DLL has its own heap: memory it allocates must never be freed by another module, and
  every std::string / vector / map it owns dies with it.

## Design
1. `deagle_native.asi` (loader, rarely changes): MinHook, the patterns, gEnv, all hooks, the typed Lua functions'
   C entry points, a POD state block it owns (VirtualAlloc, fixed layout + version number). Each hook is a thunk:
   `inflight++; core = g_core (atomic); if (core) core->OnCamera(...); else original only; inflight--`.
2. `deagle_core.dll` (everything else): built separately; the loader copies it to `deagle_core_<n>.dll` before
   LoadLibrary, so the build can overwrite the original while the game runs (Handmade Hero day 21 does the same).
   Exports one function: `CoreApi* CoreStart(const LoaderApi*, StateBlock*, uint32_t version)`.
3. Reload (`deagle_native_reload` console command or `DN.reload()`): sets a flag. The loader acts at the start of
   the next camera step, outside any core frame: `g_core = nullptr`, wait until `inflight == 0` (other threads),
   `core->Stop(state)` (writes POD state, releases its own heap objects, joins any thread it started), FreeLibrary,
   copy + LoadLibrary the new DLL, `CoreStart`. Hooks are never removed or re-enabled during a reload.
4. What survives in the state block (handles only, plain numbers): held emitter pointers + their times, tracer slots,
   flash state, viewmodel id, springs, the RoN FMOD system / studio / bank handles (fmor.dll stays loaded: the new
   core's LoadLibrary only bumps its count, so no 531 ms bank reload), KCD2 FMOD system + sound handles.
5. Fail safe, loud (no silent fallback): version mismatch, missing export, or a crash inside CoreStart (SEH) ->
   g_core stays null, hooks call only the originals, the loader logs it and queues a console line so Lua's
   `DN.version()` returns the error and the mod prints it on screen. Lua refuses to run its native-backed paths
   until the version matches.
6. Lua functions registered by the loader stay valid across reloads (their C functions live in the loader and
   dispatch through g_core); while g_core is null they raise a Lua error ("deagle core not loaded"), never crash.
7. New hooks wanted by the core go through `LoaderApi::Hook()`; the loader removes them before FreeLibrary
   (MH_DisableHook while no thread is inside, i.e. at the frame boundary with inflight 0).

## Risks
- A core pointer left anywhere the engine calls later (a callback, a vtable we built) = crash after unload. Rule:
  the core registers nothing with the engine directly; everything goes through the loader.
- FMOD callbacks: ron.cpp uses built-in DSP types only (SEND / RETURN), no user callbacks, so FMOD's mixer thread never
  runs our code. Keep it so (a test can grep for `setCallback` / `FMOD_DSP_DESCRIPTION` in core sources).
- State layout change = the state block's version changes; the new core then starts clean (releases nothing it
  doesn't know: the loader keeps a release list of emitter pointers it can free itself).

## Sources
- Handmade Hero day 21, loading game code dynamically: https://guide.handmadehero.org/code/day021
- Handmade forum, hot reload and the engine core (state in platform memory, refresh function pointers, layout
  changes break it): https://handmade.network/forums/t/2690/p/13382
- MinHook wiki (create / enable / disable / remove lifecycle, trampolines): https://github.com/TsudaKageyu/minhook/wiki
