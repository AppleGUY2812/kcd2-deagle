# Q4: batched, queued and off-thread rays in WHGame 1.5.6 (static RE)

RVAs are relative to the WHGame.dll image base. Capstone static reads only; nothing has run in game yet.

## 1. IPhysicalWorld
- **gEnv+0x30.** RTTI `.?AVCPhysicalWorld@@`, already confirmed by deagle_native.log.
- **Primary vftable:** RVA **0x407FC80**, 96 slots. Secondary vftables are at 0x407FF88 (+8) and 0x407FFC0 (+0x10).

| vt off | slot | RVA | what | confidence |
|---|---|---|---|---|
| +0x78 | 15 | 0x1A7FAB0 | GetPhysVars (`+0x138` bMultithreaded, `+0x188` p_num_threads) | high |
| +0xA8 | 21 | 0x52950C | GetPhysicalEntityId | high |
| +0xB0 | 22 | 0x667418 | GetPhysicalEntityById | high |
| +0xD8 | 27 | 0x6EFD50 | TimeStep(float dt, int flags), called with flags 0x19F | high |
| +0x110 | 34 | 0x84C354 | GetEntitiesInBox(min*, max*, IPhysicalEntity**& list, objtypes, prealloc) | high |
| +0x118 | 35 | 0x50A3E8 | **RayWorldIntersection(const SRWIParams&, const char* tag, int iCaller)** | high |
| +0x120 | 36 | 0x666FAC | **TracePendingRays(int mode)** (drains the queue) | high |
| +0x1C0 | 56 | 0xBA569C | RayTraceEntity(pent, Vec3* org, Vec3* dir, ray_hit*, pe_params_pos*, geomFlagsAny) | medium |
| +0x1C8 | 57 | 0x8115BC | PrimitiveWorldIntersection(const SPWIParams&, WriteLockCond*, tag) | high |
| +0x210 | 66 | 0xE1BF10 | AddEventClient(type, int(*)(const EventPhys*), bLogged, float prio) | high |
| +0x218 | 67 | 0xE1C028 | RemoveEventClient(same arguments) | high |
| +0x220 | 68 | 0x5D68E4 | PumpLoggedEvents() | high |
| +0x230 | 70 | 0x1701D0C | ClearLoggedEvents | medium |

- **Callers:** the engine's Lua `Physics.RayWorldIntersection` goes through the inline wrapper 0x4846E4. That wrapper zeroes 0x70 bytes, fills the fields and calls vt+0x118 with iCaller=4.
- **GetEntitiesInBox** takes the external lock (+0x5FF4) and returns entities from a per-caller buffer. It costs the same per grid cell as a ray, so it is not a cheap prefilter.

## 2. rwi_queue
- **Flag:** `rwi_queue = 0x800`. The check is `test [params+0x34], 0x800` at 0x50A4CE.
- **Enqueue (0x50A4DC..0x50A91F):**
  - Takes the spin lock at +0x601C.
  - The ring buffer pointer is at +0x5CB0, the head at +0x5CB8, the tail at +0x5CBC, the count at +0x5CC0 and the allocated size at +0x5CC4.
  - Each entry is 0x88 bytes. The grow function 0xFAE5A4 adds 64 entries at a time, so the queue has no cap.
  - The flags are copied with the queue bit cleared.
  - **If `hits == NULL`, the hits come from an internal ray_hit pool.** The pool lock is at +0x6020 and the "from pool" bit is bit 16 at entry +0x70. The low bits of +0x70 hold iCaller.
  - Skip entities are capped at 5. They are stored as ids at +0x74 and converted back to pointers when the ray is traced.
  - The call returns 1.
- **SRWIParams is 0x70 bytes:**

| offset | field |
|---|---|
| 0x00 | pForeignData |
| 0x08 | iForeignData |
| 0x10 | OnEvent `int(*)(const EventPhysRWIResult*)` |
| 0x18 | org (Vec3) |
| 0x24 | dir (Vec3, full length) |
| 0x30 | objtypes (engine default 0x11F = ent_all) |
| 0x34 | flags (default 0xF = rwi_stop_at_pierceable) |
| 0x38 | hits |
| 0x40 | nMaxHits |
| 0x48 | phitLast |
| 0x50 | nSkipEnts |
| 0x58 | pSkipEnts |
| 0x60 | 8 bytes (probably collclass); leave zeroed |
| 0x68 | int; leave zeroed |

- **ray_hit is 0x50 bytes:** dist +0, pCollider +8, ipart +0x10, partid +0x14, surface_idx (short) +0x18, pt +0x24, n +0x30, bTerrain +0x3C, next +0x48.
- **Drain:** TracePendingRays (0x666FAC) takes the lock at +0x6024. It then pops **every** queued entry and calls RWI(entry, "RayWorldIntersection(Queued)", iCaller). For each result it builds an **EventPhysRWIResult (id 9, 0x48 bytes):**

| offset | field |
|---|---|
| 0x00 | next |
| 0x08 | idval |
| 0x10 | pEntity |
| 0x18 | pForeignData |
| 0x20 | iForeignData |
| 0x28 | OnEvent |
| 0x30 | pHits |
| 0x38 | nHits |
| 0x3C | nMaxHits |
| 0x40 | bHitsFromPool |

  - The helper 0x6672D0 first calls the **non-logged** clients for id 9. They run on the calling thread (the physics thread) and their list is at +0x5AE8.
  - The helper then appends the event to the logged list (+0x59C8 / +0x59D0).
  - The PWI queue works the same way: PWI result events are id 10, entries are 0x138 bytes and the queue flag is `SPWIParams+0x98 & 0x800`.
- **Delivery:** PumpLoggedEvents (0x5D68E4, main thread) calls `ev->OnEvent(ev)` directly for ids 9 and 10. It skips the logged clients when OnEvent is set, then returns pooled hits. **pHits is valid only inside the callback.**
- **Who drains:**
  - **Physics thread** (CPhysicsThreadTask::Run 0xE61320, thread name "Physics"), once per step: TracePendingRays(1), then TimeStep(dt, 0x19F), then TracePendingRays(2). Mode 2 only syncs on the lock.
  - **Main thread** (CSystem::Update at 0x6678C4): PumpLoggedEvents comes first, then the step request (0x66852F..0x6686F1). When paused, or with no physics thread, the main thread runs TracePendingRays itself, so queued rays still resolve.
  - **Latency:** about 1–2 frames.
- **CryAction async casters:** only AI ones exist (`ai_RayCasterQuota`, `ai_IntersectionTesterQuota`, `C_RepeatedRayCastHelper`). There is no `g_IntersectionTestQueue` or GlobalRayCaster that is easy to reach. Use rwi_queue directly.

## 3. Threading
- **The physics thread is on by default.**
  - `sys_physics_CPU` defaults to 1 (CSystem+0xC68). It is not overridden in system.cfg or the Engine paks.
  - When it is above 0, CSystem::Update creates the thread (0xA65198, stored at CSystem+0xDF0); otherwise it kills it (0x245BAFC).
- **p_num_threads** (internal solver jobs) is clamped to `min(p_num_threads, cores-1)` and at least 1 by `sys_limit_phys_thread_count` (default 1). Its starting value is not visible in the static code.
- **Synchronous RWI from a worker thread is safe but serialised.**
  - With iCaller==4 (MAX_PHYS_THREADS, which every external caller uses), RWI takes the write lock m_lockCaller[4] at +0x5FF4 (the lock array is at +0x5FE4+4*i). It also takes the grid read lock at +0x5FE0.
  - So rays from any thread run one at a time and would contend with the main thread's Lua, AI and game rays.
  - iCaller 0–3 belong to the physics threads and are not locked: do not use them.
  - PWI also takes a mutex at +0x6048 when a TLS flag is not set.

## 4. Batching
- There is no batch API.
- The queue moves ray cost off the main thread, but each ray still costs one full RWI on the physics thread, run one after another before TimeStep. The ceiling is the physics thread's spare time per frame.
- Cheapest batching:
  - **One ray per bullet covering several frames of flight:** a long `dir` chord, re-cast only when it hits.
  - Native calls instead of Lua: the 12–20 µs measured per Lua ray is mostly binding overhead (an estimate; still to be measured).
- A PWI sweep (sphere/capsule) along a segment is heavier per call and returns one contact. Use it only for thick rounds.

## Cheapest native test
1. In the plugin, on the main thread (console command or the frustum hook), read `w = *(gEnv+0x30)`.
2. Build `alignas(16) char rp[0x70] = {}`:
   - org = camera position
   - dir = forward × 100
   - objtypes = 0x11F
   - flags = 0x80F
   - hits = NULL
   - nMaxHits = 1
   - OnEvent = `static int OnRwi(const char* ev)`, which logs `*(int*)(ev+0x38)` (nHits) and, from `*(char**)(ev+0x30)`, dist (+0) and pt (+0x24)
   - pForeignData = a counter
3. Call `((int(*)(void*, void*, const char*, int))vt[0x118/8])(w, rp, "deagle_q", 4)`. Expect the return value 1.
4. Expect the OnRwi log line within 1–2 frames on the main thread, with the same distance as a synchronous call that has flags 0xF. Log the thread id in both places.

**Unknowns:**
- The exact per-ray cost on the physics thread (measure TracePendingRays with QPC around vt+0x120, or time the physics thread's step).
- The meaning of SRWIParams +0x60..0x6F.
- RayTraceEntity slot 56 is medium confidence.
