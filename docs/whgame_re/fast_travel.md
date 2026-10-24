# Fast travel: the destination and the precache (WHGame.dll 1.5.6)

Found 2026-10-23 for the fast-travel preload (`native/src/ftpreload.cpp`). Addresses are image-base 0x180000000;
the plugin checks the bytes of `0x182de27d0` and `0x181899654` against the installed DLL before it reads anything.

## The fast-travel manager
Class `wh::playermodule::C_FastTravel` (vtable `0x183a84288` in the Ghidra project, `qk sym FastTravel`): its
entries point into the same `0x182dd…`/`0x182de…` code as the functions below.
`mgr = *(*(game + 0x128) + 0x28)`, `game = *(WHGame + 0x549d388)` (`FUN_1809155c8` returns it).

| offset | what | evidence |
|---|---|---|
| +0x48 u8 | travelling | set (LOCK) by the start `FUN_182de28f4`, cleared by `FUN_182de2d98`; read by `FUN_180534e5c` (IsFastTraveling) |
| +0x49 bits | bit 2 = the 3rd arg of the route builder | `FUN_182de27d0` |
| +0x58 ptr | the player actor | vfuncs 0x5f8 / 0x660 / 0x668 called on it at start |
| +0x68 f32 x3 | **destination** | `FUN_182de27d0(mgr, pos, flag, outRoute)`: `movsd [rcx+0x68]` + z at +0x70 |
| +0x90/+0x98 | route, `Vec3` (12-byte) begin/end | `FUN_182dde634` counts it as `(end-begin)/12` |
| +0x110 ptr, +0x68 u8 | the "AfterFastTravel" readiness observer, +0x68 = armed | `FUN_180534e5c` ORs it with +0x48; see "Arrival" below |

The route builder runs from the map screen (`FUN_182affe7c`: point id -> point object -> its position -> route ->
`ShowFastTravelPath`), so +0x68 also changes on map hover; it holds the real target when +0x48 goes to 1.
The console `wh_pl_FastTravelTo` goes through a different function (`0x182eaa218`) that the map never calls.

UI side (map, Scaleform): `FastTravelStarted` (`FUN_182afeae0`), `UpdateFastTravelProgress` (`FUN_182b02938`, the
piece's position each step), `FastTravelPath` / `ShowFastTravelPath` (`FUN_182b016b0`, the route as x,y pairs).

## AddPrecachePoint
`I3DEngine::AddPrecachePoint(const Vec3& pos, const Vec3& dir, float timeout, float importance)` returns a future
(hidden out pointer after `this`): `FUN_181899654`, a 3D-engine vfunc, so the plugin finds the slot by address. It
queues a task (`FUN_1820ceb70` -> `FUN_1820cecf0`) that runs `FUN_1820d1790`: at most 4 points (the oldest is evicted,
log "Precache points full - evicting oldest"), a streaming box of `e_StreamPredictionBoxRadius` (3D-engine cvars
+0x128) around the point, expiry `now + timeout`. This is the same streaming the cutscenes use to precache their
camera cuts. The future's reference is dropped by the plugin (a few bytes a call).

Not the same as `e_AutoPrecacheCameraJumpDist` (cvars +0x51c): that one fires on a one-frame camera jump and forces a
full precache of the *current* view (the load after arrival). Do not set it to 0 (handoff 2026-10-23 section 5).

## Arrival (decoded 2026-10-23, ~/re/work/fast_travel_arrival/report.md on the ProLiant)
The black screen after the walk is the game's "AfterFastTravel" readiness wait, the same system as a save load's
(`docs/PERFORMANCE.md` "Step 5", `native/src/readiness.cpp`):
1. Stop (`FUN_182de2d30` -> `FUN_182de2d98`) clears +0x48, jumps the camera, and queues a 0 s callback
   (`FUN_181fef770(ft+0x118, ...)`). The callback `FUN_181ff87e0` starts the observer at ft+0x110 with mask 0x3f91
   (`FUN_180fc3ec0`, call site 0x181ff8814): +0x68 goes 1.
2. The observer fires once every readiness task in those categories is ready (or after
   `wh_sys_ReadinessObserverTimeoutDuration`, 50 s). On the 3 trips measured it always ended by readiness.
3. Fired: `FUN_181ff8970` -> `FUN_182de1714` drops the "AfterFastTravelSpeed" time-speed modifier and releases save
   lock 5 "FastTravel" (taken at the start in `FUN_182de28f4`; saves wait while it is held), so a pending autosave
   runs in that frame.
4. The loading circle appears once a fader has been fully black for `wh_ui_FaderProgressIndicatorTimerDuration`
   (5.0 s, `C_FaderManager` `FUN_180d26844`): arrivals under 5 s show no circle. `wh_ui_FaderProfiler 1` writes the
   fader timeline to `%USER%/FaderProfiler/`. Which fader holds the black, and whether its fade-in waits for the
   observer, is not decoded yet.

Measured from outside (2026-10-23, read-only watcher + thread sampler, 4 trips; times after the camera jump):

| trip | walk | observer armed | streaming tasks |
|---|---|---|---|
| 195 m (a random encounter stopped it) | 7.3 s | 4.7 s | the encounter NPCs (260 in flight) at +0.1 s, then textures on and off until +4.0 s |
| 1.3 km | 14.0 s | 3.7 s | not recorded |
| 450 m | 11.7 s | 4.0 s | textures and animations last, until +4.1 s; all idle +2.0 to +3.4 s |
| 240 m | 2.7 s (too short to preload) | 6.7 s | textures, terrain, animations finish together at +6.2 to +6.8 s |

The observer fired right after the last listed task went idle on the three trips with full data. The +2.0 to +3.4 s
idle stretch did not fire it: the 6 tasks of other classes (not readable from outside) are the suspects.

On every trip: 1.5 to 1.8 s of 150 to 490 ms frames starting about 1.2 s after the jump. Main waits in the render
frame sync (`FUN_1809a6308`, 200 ms condvar rounds on renderer+0x50); RenderThread and ShaderCompile are the busy
threads. Likely the camera-jump precache (`e_AutoPrecacheCameraJumpDist`) or shader work: not decoded. Then, in the
frame the observer fires, a 450 to 830 ms main-thread stall: an XML file read from a pak (`FUN_1804617fc`) and parsed
by expat (`FUN_1806e6b70` tokenizer, `FUN_1804faed8` node builder). Which file is not known yet.
