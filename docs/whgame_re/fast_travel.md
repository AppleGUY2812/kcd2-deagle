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
| +0x110 ptr, +0x68 u8 | a second "travelling" state | `FUN_180534e5c` ORs it with +0x48 (meaning not yet known) |

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
