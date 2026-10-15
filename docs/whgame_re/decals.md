# WHGame 1.5.6 decals: what Particle.CreateMatDecal does (static RE, RVAs from image base 0x180000000)

## 1. The CreateMatDecal path. It is already deferred by default, with no mesh clip.
- The Lua binding is registered at 0x144ad3d. The thunk at 0x39a5e10 calls CreateDecalInternal at 0x39a5a6c with bMat=1.
  - It fills a CryEngineDecalInfo of 0x170 bytes: +0x18 pos, +0x24 normal, +0x30 size, +0x34 life, +0x38 angle, +0x48 hitDir, +0x54/+0x58 grow times, +0x60 = arg 12 (bool), +0x66 material name.
  - Arg 8 is entityId. Its value goes through IEntity vtbl+0x250 (proxy) and then +0xd0 (GetRenderNode), and the result becomes the owner render node. The slot id stays -1.
  - When arg 8 is nil, arg 9 is read as a raw ScriptHandle and stored as the IRenderNode* owner. This is CryEngine's `[renderNode]` argument. hits.lua passes `hit.partid` there. That is a hazard if partid is ever non-zero for a hit with no entity.
  - It then calls I3DEngine vtbl+0x218.
- C3DEngine::CreateDecal is at 0x13b021c, with a cold chunk at 0x2356362. The CVars live at [0x5496fd0].
  - It returns early if e_DecalsAllowGameDecals (+0x58) is 0. When e_Decals == 2 it logs.
  - `if (e_DecalsDeferredDynamic(+0x358) == 1 && !info[0x63]) info.bDeferred(+0x64) = 1;`
  - Otherwise, for non-deferred decals with an owner and size > 0.5, it spreads the decal over the nearby render nodes.
  - It then calls CDecalManager::SpawnHierarchical at 0x13b0360. With an owner, that walks the compound stat object's sub-objects (stride 0xb0) and calls Spawn once per overlapping part.
- CDecalManager::Spawn is at 0x34b67d0, with the manager at [0x5496f40].
  1. It culls by distance.
  2. The overlap scan (512 slots) runs only if e_DecalsOverlapping == 0. The neighbour-fade scan runs only if size > 1.
  3. It picks a slot with `m_nCurDecal = (m_nCurDecal+1) & 0x1FF`, which is the 512-slot ring.
  4. It looks up the material by name (IMaterialManager vtbl+0x38, bMakeIfNotFound=false).
  5. The mesh branch runs only if `owner && slotId >= 0 && !bDeferred`. It goes through AdjustDecalPosition (+0x62), MakeBigDecalRenderMesh at 0x34b12fc and the e_DecalsClip / MaxTrisInObject checks.
  6. For deferred decals it sets m_eDecalType = 5 (7 on characters or vegetation), clears the owner, builds the right/up/front axes and marks the slot active.
  7. Lua decals carry slot id -1, so they never take the mesh branch. The deferred path is a constant amount of math plus one material-name lookup.
- CDecal::Render is at 0x34b38d4. Type 5 projects with depth = size × e_DecalsDeferredDynamicDepthScale (cvar +0x24c, default 4) and calls IRenderer vtbl+0x568, which is EF_AddDeferredDecal at 0x659864. That function locks and push_backs into a growing vector at renderer+0x8d00 (entries of 0x58 bytes).
- The end of Spawn also runs `m_nCurDecal++`. The ring therefore advances by 2 per decal, so about **256** dynamic decals are alive at once (medium confidence). The oldest are overwritten no matter what lifetime they were given.

## 2. CVar defaults (registration at 0x15c183d onward, CVars object [0x5496fd0])

| cvar | default | notes |
|---|---|---|
| e_Decals | 1 | flags 0x1000000 |
| e_DecalsForceDeferred | 0 | no reader found in the decal code |
| e_DecalsDeferredDynamic | **1** | flags 0, so it can be set from the console |
| e_DecalsDeferredDynamicDepthScale | 4.0 | cheat |
| e_DecalsPlacementTestAreaSize | 0.08 | cheat |
| e_DecalsPlacementTestMinDepth | 0.05 | cheat |
| e_DecalsMaxTrisInObject | 8000 | |
| e_DecalsAllowGameDecals | 1 | |
| e_DecalsHitCache | 1 | cheat; mesh path only |
| e_DecalsMerge | 0 | |
| e_DecalsPreCreate | 1 | |
| e_DecalsClip | 1 | |
| e_DecalsRange | 20 | |
| e_DecalsLifeTimeScale | 1 | sys_spec ObjectDetail sets 1 or 2 |
| e_DecalsNeighborMaxLifeTime | 4 | |
| e_DecalsOverlapping | 1 | |
| e_DecalsMaxValidFrames | 600 | |
| e_DecalsMaxUpdatesPerFrame | 4 | |
| e_DecalsSpawnDistRatio | 4 | |
| r_DeferredDecals | 1 | |
| r_deferredDecalsMaxLimit | **1280** (0x500) | the help text says 1024 |

The deferred branch is still in the binary, and with the defaults every Lua decal takes it.

## 3. Decal render nodes (a ring pool you move is possible)
- C3DEngine vtable is at 0x40205e8 (RTTI .?AVC3DEngine@@).
  - +0x4e0 CreateRenderNode (0xa79410). Type 7 allocates `new CDecalRenderNode` (0xd8 bytes, ctor 0x5a71b8).
  - +0x130 RegisterEntity.
  - +0x218 CreateDecal.
- The CDecalRenderNode vftable is at 0x3a3e810 and has 74 slots. They are all IRenderNode methods: there is no SetDecalProperties or GetDecalProperties. WH's node is a pure deferred projector.

| slot (vtbl offset) | address | method |
|---|---|---|
| +0x18 | 0x6597f4 | Render: alpha = clamp((1 − dist/maxViewDist)×6, 0, 1) into +0x88; calls renderer vtbl+0x568(this+0x50) |
| +0x20 / +0x28 | | GetBBox / FillBBox (+0xa8) |
| +0x38 | | type → 7 |
| +0x70 | | Clone |
| **+0x80** | 0xf804d4 | **SetMatrix(const Matrix34&)**: no-op if unchanged; stores it at +0x50; WS AABB = M × box(±1, ±1, ±0.5); calls RegisterEntity |
| +0x90 | | GetPos |
| +0x98 | | SetBBox |
| +0xa0 | | OffsetPosition |
| **+0x128** | 0x123048c | **SetMaterial** (+0x80) |
| +0x130 / +0x138 | | GetMaterial |
| +0x158 / +0x160 | | Set/GetLayerId (+0xc0) |
| +0x168 | 0xfb5314 | GetMaxViewDist: \|axisX\| × 0.75 × e_ViewDistRatio × viewDistRatio(+0x4d)/100, at least e_ViewDistMin |

- Pool: create N nodes once, SetMaterial each, then on every hit call SetMatrix with columns = right × r, up × r, normal × depth. A move is an AABB recompute plus an octree re-register. There is no mesh and no allocation.
- Renderer limits:
  - The CDeferredDecalsStage execute function (0x7737a8) sorts the frame's list and draws min(count, r_deferredDecalsMaxLimit) decals; the cap is applied in 0x7cac38.
  - The primitive pool grows to the most decals seen in one frame and stays allocated.
  - Per decal and per frame, 0x774d24 looks up material params by name (DecalAlphaMult, DecalFalloff, ...).
  - The submission vector itself has no fixed size.

## 4. Fixed pools
- CDecalManager holds `CDecal m_arrDecals[512]` (stride 0xE0, from +0), `bool active[512]` at +0x1C000 and m_nCurDecal at +0x1C200.
- 512 is DECAL_COUNT, the `& 0x1FF` in the slot pick; about 256 of the slots are used in practice.

## Verdict and cheapest test
The engine side of creating a decal is cheap and already deferred. The 250 ms is not explained by mesh clipping (medium-high confidence).

Test it directly. Print `e_DecalsDeferredDynamic` (expect 1). Then time it with live.sh:
`local t=os.clock(); for i=1,200 do Particle.CreateMatDecal(p,n,0.05,300,"Materials/deagle/decals/bullet_hole_wood",0,d) end; return os.clock()-t`
- Run it once with no entity and once with an entity id.
- Then set `e_DecalsDeferredDynamic 2` (non-deferred) and run it again.

If deferred costs only microseconds per call, the stall is elsewhere: per-frame draw of the decals, the Lua hole bookkeeping, or the fx and sound per hit.

## Follow-up: what can cost tens of ms at ~200 holes/s (static ranking)

The A/B measurement: holes on gives 92-240 ms frames, building over ~1.5 s and clearing about 1 s after firing stops. Live decals still exist after firing stops, so pure per-live-decal draw cost cannot explain the recovery. The cost must follow *change* in the decal set.

1. **Most likely (medium-low confidence): render-stage churn in CDeferredDecalsStage.**
   - Every frame, CDecalManager::Render (0xc3411c, cold chunk 0x229e42a) walks the 512 slots in order. Each visible decal goes through CDecal::Render, then EF_AddDeferredDecal (locked push_back).
   - The stage (0x7737a8) stable-sorts this list (0xb19354, key = u32 at SDeferredDecal+0x50, which is equal for all our holes) and keeps the first min(n, r_deferredDecalsMaxLimit).
   - It then calls 0x774d24 once per decal, re-binding primitive *i* to decal *i*:
     - fetches the material's shader resources (vtbl+0xa8, +0x88 texture slots 1/5/0xb);
     - sets the technique with the "DeferredDecalVolumeMerged" name (0x78fc2c);
     - looks up params by string name ("DecalAlphaMult", "DecalFalloff", ...);
     - sets cull/depth state depending on whether the camera is inside the box (0x78fe10/fe28);
     - compiles the primitive (0x42acec), then updates its constant buffer.
   - A new decal inserted into the slot order shifts which decal every later primitive gets, so that primitive's state/texture binding changes and it is likely recompiled. While holes keep arriving this is O(live) recompiles per frame. When spawning stops the order is stable and recompiles stop.
   - This fits "builds while the ring fills, clears when firing stops". Not verified: the dirty/compile cost inside 0x42acec.
2. **Low: per-call Spawn work** (0x13b021c → 0x13b0360 → 0x34b67d0). The work is constant per call:
   - a global lock plus a std::map name lookup (CMatMan::LoadMaterial 0x454a3c → FindMaterial 0x454de0). A hit returns at 0x454adc → 0x454da9, so there is no file or pak access after the first load.
   - camera distance cull, vis-area ambient (0x3491820), owner matrix (0x34af5a4).
   - No ray, physics or box queries on the deferred path:
     - e_DecalsPlacementTest* has no reader in the decal code.
     - e_DecalsSpawnDistRatio is only a distance compare.
     - The GetObjectsInBox-style query (0x5dfdec / 0x76a930) sits only in CreateDecal's *non-deferred* owner branch (0x23563ee).
   - The 512-slot scans run only for e_DecalsOverlapping=0, or for size > 1 m.
   - With an entity owner, SpawnHierarchical walks the compound object's sub-objects (stride 0xb0) and spawns once per overlapping part, so one hit can use several slots.
3. **Low: ring overwrite.** It releases a null render mesh and swaps the material smart pointer (0x3c88cc, 0x6bcf48).
4. **Low: CDecalManager::Update** (0xa3bdc8). Per active decal: lifetime update plus a linear std::find over an owner list. Only owned decals take part, and it does not clear when firing stops.

### One test that separates them
While firing the stress test with holes on, set `r_deferredDecalsMaxLimit 0` from the console. Creation and submission stay the same, but the stage builds no primitives. Then set it back to 1280.
- If FPS recovers, the cost is the render stage (item 1).
- If FPS stays low, time creation directly over rcon:
  `local d=System.GetViewCameraDir(); local c=System.GetViewCameraPos(); local p={x=c.x+d.x*3,y=c.y+d.y*3,z=c.z+d.z*3}; local n={x=-d.x,y=-d.y,z=-d.z}; local t=os.clock(); for i=1,200 do Particle.CreateMatDecal(p,n,0.05,300,"Materials/deagle/decals/bullet_hole_wood",0,d) end; return (os.clock()-t)*1000/200`
  This gives ms per call. Run it once without and once with an entity id as argument 8.
