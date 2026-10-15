# Q9: our own copy of the static collision world (WHGame 1.5.6, static RE)

Can the mod copy KCD2's static collision near the player (for Embree on worker threads, NPCs as capsules) and get
results identical to `RayWorldIntersection`? RVAs are image-relative. Capstone static reads only; nothing has run in
game yet. Scratch (asm dumps, CE sources): `scratchpad/q9/` (`w.py` helper, `rwi.asm`, `gea.asm`, `pe_getparams.asm`,
`pe_getstatus.asm`, `ce/*.cpp|h` from MergHQ/CRYENGINE `release`).

**Short answer: amber-green.** All the data is reachable through stable CryEngine 5 layouts, and they check out in
the binary. KCD2's ray code is CE's `rwi.cpp` almost byte for byte (the only WH change is an extra per-surface table).
What keeps it from plain green: physics on demand (static entities exist only around recent queries), entity-owned
statics that move (doors), water areas, foliage ropes, material substitutors, breakables, and terrain sectors that
stream out. All of these can be handled. Details below.

## 1. Enumerating static entities: GetEntitiesInBox (world vt +0x110, 0x84C354) — confidence high
- **Signature:** `int GetEntitiesInBox(Vec3 ptmin, Vec3 ptmax, IPhysicalEntity**& pList, int objtypes, int szListPrealloc)`.
  - The wrapper takes the external write lock `m_lockCaller[4]` (world +0x5FF4).
  - It then calls the internal `GetEntitiesAround` (0x6EE310) with (…, objtypes, pPetitioner=0, szListPrealloc,
    iCaller=4).
- **List ownership:**
  - By default `pList` is the engine's per-caller temp list (`m_threadData[4].pTmpEntList`, world +0x5D58 + 0x58*iCaller).
    It stays valid only until the next iCaller-4 query **from any thread**, and the lock is already released when the
    call returns.
  - **Use `szListPrealloc`:** pass `pList = our buffer` and `szListPrealloc = N`. If `nout <= N` the engine copies into
    our buffer under the lock, so there is no race. If `nout > N`, `pList` is swapped for the internal list: detect it
    by pointer compare and retry with a bigger buffer.
  - `ent_allocate_list` (0x40000, checked at 0x6EED97) makes the engine `new[]` the list with its own allocator.
    Don't use it across the module boundary.
  - `ent_addref_results` (0x100000) AddRefs every result; Release is vt +0x18.
- **Result cache:** a call with the same box and objtypes returns the cached previous list (`m_prevGEABBox`
  +0x464 stride 0x18, `m_prevGEAobjtypes` +0x4DC, `m_nprevGEAEnts` +0x4F0).
- **Physics on demand (POD) is live** and matters a lot:
  - With `(objtypes & (ent_static|ent_no_ondemand_activation 0x80000)) == ent_static`, every POD cell the box touches
    calls `m_pPhysicsStreamer` (world +0x250, set by world vt +0x1D8; CPhysStreamer vt 0x40AFA38) through vt +0x18
    `CreatePhysicalEntitiesInBox`. That goes into the 3D engine's physicalize-in-box (0x530704, next to CBrush
    physicalize).
  - It sets the cell's lifeTime to **8.0 s** (1E10 on failure); constants are read at 0x6EEA6B and 0x6EEA73.
  - TimeStep later calls `DestroyPhysicalEntitiesInBox` (vt +0x20 → 0x530DB8) on cells that nobody touched.
  - `e_OnDemandPhysics` is registered with **default 3** (brushes and vegetation; 0x15C3A24, even though the help text
    says 1). There are also WH cvars `wh_e_PernamentPhysicalization` (default 1, inferred from e_Objects/e_Brushes) and
    `...SyncLoad`, described as "streaming of physical files".
  - RWI itself does the same on-demand creation (CE `rwi.cpp` check_cell; KCD sets `bUsePhysOnDemand` at 0x50ABF7).
    So enumerating with ent_static sees exactly the set KCD's ray would.
- **Rules:**
  - Call it on the **main thread**. On-demand creation runs 3D-engine physicalization; RWI does this only under the
    POD-thread marking.
  - Query per tile (32–64 m), not one 600 m box: the cost is per grid cell, and `p_GEB_max_cells` only logs a warning.
  - Placeholders come back already converted to full entities (`GetEntity`, entity vt +0xE0).

## 2. Per entity: parts, pe_params_part, status — confidence high
- **IPhysicalEntity vtable** (CPhysicalEntity 0x3A578C0):
  - +0x08 GetType, +0x10 AddRef, +0x18 Release
  - +0x20 SetParams, +0x28 GetParams (0x3D5634), +0x30 GetStatus (0x8A627C), +0x38 Action
  - +0x50 GetForeignData, +0x58 GetiForeignData
  - +0xE0 GetEntity, +0x1C0 RayTrace (used for entities with geometry callbacks)
- **Entity layout:**
  - BBox[2] +0x08, pForeignData +0x20, iForeignData (short) +0x28, iForeignFlags (short) +0x2A
  - pEntBuddy +0x38, bProcessed +0x40, iSimClass = top byte of +0x44 (5 = area), m_lockUpdate +0x48 (read lock =
    lock xadd +1, spin while bit 16), m_pWorld +0x68, m_flags +0x84
  - **m_pos +0xA0, m_qrot +0xAC (x,y,z,w)**, collisionClass +0xEC/+0xF0
  - **m_parts +0x118, m_nParts +0x120**
- **Part `geom` (0x88 bytes)**, read from GetParams, GetStatus and GetMatId 0x813914:

| offset | field |
|---|---|
| +0x00 | pos |
| +0x0C | q |
| +0x1C | scale |
| +0x20 | BBox[2] |
| **+0x38** | **pPhysGeom** (the one RWI intersects; verified at 0x5421A8) |
| +0x40 | pPhysGeomProxy |
| +0x48 | id |
| +0x4C | mass |
| **+0x50** | **flags** |
| +0x54 | flagsCollider |
| +0x5C | minContactDist |
| **+0x60** | **bitfield:** surface_idx:10 (signed), idmatBreakable:14, nMats:7 |
| **+0x68** | **pMatMapping** |
| +0x70 | pLattice |
| +0x80 | pPlaceholder |

- **pe_params_part (type 6, 0xA0 bytes)**, the CE layout, confirmed field by field in GetParams:

| offset | field |
|---|---|
| +4 | partid |
| +8 | ipart (0x80000000 or -1 means search by partid) |
| +0xC | bRecalcBBox |
| +0x10 | pos |
| +0x1C | q |
| +0x2C | scale |
| +0x30 | pMtx3x4 (filled entity-local) |
| +0x38 | pMtx3x3 |
| +0x40 | flagsCond |
| +0x44 / +0x48 | flagsOR / flagsAND (both = flags) |
| +0x4C / +0x50 | flagsColliderOR / AND |
| +0x54 | mass |
| +0x58 | density |
| +0x5C | minContactDist |
| **+0x60** | **pPhysGeom** |
| +0x68 | pPhysGeomProxy |
| +0x70 | idmatBreakable |
| +0x78 | pLattice |
| +0x80 | idSkeleton |
| **+0x88** | **pMatMapping** |
| **+0x90** | **nMats** |
| +0x94 | invTimeStep |
| +0x98 | bAddrefGeoms (keep 0) |
| +0x9C | idParent |

- **pe_status_nparts is type 12.** GetStatus returns `m_nParts` (0x8A6574).
- **World TM per part:** use pe_status_pos (type 1, 0x80 bytes) with `ipart = i`, `partid = -1`, flags 0. It returns
  world `pos = m_pos + m_qrot*part.pos` and `q = m_qrot*part.q` (0x8A657F…).
  - Layout: pos +0x18, BBox +0x24, q +0x3C, scale +0x4C, iSimClass +0x50, pMtx3x4 +0x58, pMtx3x3 +0x60, pGeom +0x68,
    pGeomProxy +0x70.
  - RWI uses the same transform: `R = Matrix33(m_qrot*part.q)`, `offset` as above, `scale = part.scale`.
  - So `v_world = offset + R*(v_local*scale)`.

## 3. Geometry — confidence high (layouts), medium (slot names past +0x108)
- **phys_geometry (0x58 bytes):**
  - pGeom +0x00, Ibody +0x08, q +0x14, origin +0x24, V +0x30, nRefCount +0x34
  - surface_idx +0x38, pMatMapping +0x40, nMats +0x48, pForeignData +0x50
- **RTTI vtables:**

| class | vtable |
|---|---|
| CTriMesh | 0x3A56B98 |
| CBoxGeom | 0x3A567A8 |
| CCylinderGeom | 0x3A573D0 |
| CCapsuleGeom | 0x3A565B0 |
| CSphereGeom | 0x3A569A0 |
| CRayGeom | 0x3A570F8 |
| CHeightfield | 0x3BE1608 |
| CVoxelGeom | 0x407ED30 |
| CGeometry | 0x3A56F00 |

  - BV trees: CAABBTree 0x3A66638, COBBTree 0x3B22398, CSingleBoxTree 0x3A572F0.
  - Other classes: CGeomManager 0x3A66788, CLivingEntity 0x3A82570, CRigidEntity 0x3A48DC0, CArticulatedEntity
    0x3A48B30, CPhysArea 0x3AB6928.
- **IGeometry slots in this build.** There is one slot more than the CE header; +0x20 returns the refcount.

| vt off | method |
|---|---|
| +0x08 | GetType (box 0, trimesh 1, heightfield 2, ray 3, sphere 4, cylinder 5, capsule 6, voxel 7) |
| +0x10 / +0x18 | AddRef / Release |
| +0x28 / +0x30 | Lock(bWrite) / Unlock(bWrite) (m_lockUpdate +8; use 0 for read) |
| +0x38 | GetBBox |
| +0x58 | Intersect |
| +0x88 | GetPrimitiveId(iPrim) = `(char)m_pIds[i]` or -1 |
| +0x90 | GetPrimitive(i, prim*) |
| +0x98 | GetForeignIdx |
| +0xA0 | GetNormal |
| **+0xE8** | **GetPrimitiveCount** |
| **+0xF0** | **GetData** |
| +0x120 / +0x128 | GetForeignData / GetiForeignData |

- **CTriMesh** (GetData = this+0x30 = mesh_data):

| offset | field |
|---|---|
| +0x28 | m_pTree |
| **+0x30** | **m_pIndices (int32, 3 per triangle)** |
| **+0x38** | **m_pIds (signed char per triangle, may be NULL)** |
| +0x40 | m_pForeignIdx |
| **+0x48** | **m_pVertices.data** |
| **+0x50** | **stride (int, bytes)** |
| +0x58 | m_pNormals (per triangle) |
| +0x60 | m_pVtxMap |
| +0x68 | m_pTopology |
| **+0x70** | **m_nTris** |
| **+0x74** | **m_nVertices** |
| +0x90 | m_flags |

  Proof: GetPrimitive at 0xBA7F2C reads `idx[3i+k]*stride + vtx`, and the normal from +0x58.
- **Primitives** (local space, struct inside the geometry object; GetData returns the address):
  - box at +0x38: Basis 0x24, bOriented, center +0x28, size +0x34
  - cylinder and capsule at +0x28: center, axis, r, hh
  - sphere at +0x28: center, r
- **Triangle → surface_idx** (GetMatId 0x813914, identical to CE):
  1. `id = pIds ? pIds[tri] : -1`.
  2. If `id < 0`, use `id = part.surface_idx` (10-bit field at part +0x60).
  3. If `part.pMatMapping`, use `id = pMatMapping[id < nMats ? id : 0]`.

  The result goes into `ray_hit.surface_idx`. The raw id goes into `idmatOrg`, the triangle index into `iPrim`
  (+0x40), and `part.id` into `partid`.

## 4. Terrain — confidence high
- **It is a CHeightfield on the world's terrain entity.**
  - `m_pHeightfield[iCaller]` is at world +0x640 (6 slots).
  - Basis is at +0x670 and origin at +0x694 (identity / 0).
  - GetHeightfieldData (world vt +0x68, 0x38CC9CC) fills a `heightfield` (0x78 bytes) and returns the entity.
- **KCD's setup** (CTerrain::InitHeightfieldPhysics 0x95B684; SetHeightfieldData vt +0x60 → 0x95B838):

| field | value |
|---|---|
| step | `[0x49200E8]` (unit size) |
| size | terrainSize/unit (from `[0x49200EC]`) |
| stride | (size+1, 1) |
| heightscale | 1.0 |
| typemask | 0x00FF |
| heightmask | 0xFFF0 |
| **typehole** | **0x80** |
| fpGetHeightCallback | **0x5E5070** |
| fpGetSurfTypeCallback | **0x5E51C0** |
| mat mapping | 127 entries (terrain layer material → surface type id) |

  `typepower` is not written there; read it at runtime.
- **heightfield struct offsets:**

| offset | field |
|---|---|
| +0x34 | step |
| +0x3C | stepr |
| +0x44 | size |
| +0x4C | stride |
| +0x58 | heightscale |
| +0x5C | typemask |
| +0x5E | heightmask |
| +0x60 | typehole |
| +0x64 | typepower |
| +0x68 | fpGetHeightCallback |
| +0x70 | fpGetSurfTypeCallback |

  It is also inside CHeightfield at +0x270.
- **Sampling:** `h = cbH(ix,iy)*heightscale`; `t = (cbS(ix,iy) & typemask) >> typepower`.
  - `t == 0x80` is a **hole**: that cell is skipped entirely, so the ray passes. Holes exist.
  - Surface: `surface_idx = map[t < 127 ? t : 0]`, with map = terrain entity part 0 pMatMapping.
- **Callbacks:**
  - Both use a 64×64 direct-mapped global cache (qword entries `{u16 x, u16 y, f32/u32 val}` at 0x533CF00 / 0x5344F00).
  - On a miss they walk CTerrain (global 0x5496F68) sector nodes. **They return 0 if the sector isn't loaded**, and that
    0 gets cached.
  - They are safe from any thread in practice (the physics thread already uses them, with benign races).
  - Snapshot the area on the main thread.
- **Cell triangulation** (CE heightfieldgeom.cpp, used unchanged: Intersect 0x8141A4, RayHeightfield 0x8139C4):
  - A = (x0,y0),(x1,y0),(x0,y1)
  - B = (x0,y1),(x1,y0),(x1,y1)
  - x = ix*step, z = h

## 5. Keeping the copy in sync — confidence high (events), medium (streaming policy)
- **Event ids** (CE, with the table at world +0x5A58 + (id*2+bLogged)*8 confirmed):

| id | event |
|---|---|
| 0 | BBoxOverlap |
| 1 | JointBroken |
| 2 | Collision |
| 3 | EnvChange |
| 4 | PostStep |
| 5 | UpdateMesh |
| 6 | CreateEntityPart |
| 7 | RemoveEntityParts |
| 8 | StateChange |
| 9 | RWIResult |
| 10 | PWIResult |
| 11 | Area |
| 12 | AreaChange |
| 13 | RevealEntityPart |
| 14 | EntityDeleted |
| 15 | PostPump |

  There is no BBoxOverflow event.
- **EntityDeleted (14)** comes from DestroyPhysicalEntity (vt +0x98, 0x846E10, `idval 0xE` at 0x846EFB).
  - It is sent through SignalEvent **only to non-logged clients** (`AddEventClient(14, cb, 0, prio)`), on the
    destroying thread.
  - It is skipped for placeholders, for bThreadSafe calls, or when lastTimeStep (+0x19C) is 0.
  - **A callback that returns 0 vetoes the deletion. Always return 1.**
  - Event layout: pEntity +0x10, pForeignData +0x18, iForeignData +0x20, mode +0x28.
- **No creation event exists.** It also fires for POD expiry, not only for unloads. So:
  - Key the copy by foreign data (CBrush/render node), not by entity pointer.
  - Treat event 14 as "maybe stale".
  - Re-poll tiles (GetEntitiesInBox) on the main thread every ~1–2 s, plus on event 14.
  - Optionally hook world vt +0x80/+0x88 CreatePhysicalEntity and +0x90 CreatePhysicalPlaceholder.
- **Breakables:** logged events 5, 6 and 7 (with bLogStructureChanges).
- **Statics that move:** foreign id 2 (entity-owned, e.g. doors) can move with no event. Treat them as dynamic and
  re-read m_pos/m_qrot per frame. Brushes (foreign 1) and terrain don't move.

## 6. What RWI does that a plain tracer misses — confidence high (RWI = CE rwi.cpp)
RWI 0x50A3E8; cell checker 0x5412C0; per-contact code at 0x5422E6.
- **Order:**
  1. Terrain first (if ent_terrain 0x100); it shortens the ray.
  2. Water if `objtypes & m_bCheckWaterHits(+0x58DC) & 0x200` (water mat at +0x58D8). This also enables ent_areas.
  3. Entity grid.
- **Entity filter:**
  - `objtypes & 1<<iSimClass`; skip ents (5); skip `m_iDeletionTime`; collision-class ignore (SRWIParams +0x60).
  - Areas are traced only when iMedium==0 (water).
  - Entities with pef_use_geom_callbacks or 0 parts → `RayTrace` (vt +0x1C0).
- **Part filter:**
  - `flagsColliderAll = flags>>16 ? : 0x8000`; with 0x40, `|= 1`; with 0x400 it is "any" instead of "all"; any = 0xFFFF.
  - Require `(part.flags & All)==All && (part.flags & Any)`.
  - **Uses pPhysGeom, not the proxy.**
- **Triangles are two-sided** (no culling); the normal is the face normal.
  - 0x80 rwi_ignore_back_faces skips facing>0 hits.
  - 0x100 skips solid back faces.
- **Pierceability:**
  - `p = SurfaceFlags[imat & 0x1FF] & 0xF` (table at world +0x48D8).
  - With 0x1000, non-colliding parts (no 0xFFB|0x8000) count as 16.
  - If `(flags & 0xF) < p` the hit is a pass-through (sorted, sf_important 0x200); otherwise it is solid and the ray is
    cut there.
  - With the default 0xF every hit is solid, so first hit wins.
- **Material substitutor parts** (flags 0x2000000): they don't stop rays, but change the surface of the current best hit
  on a static entity (m_id==-1) when the hit point is inside them.
- **Also replicate:**
  - Terrain holes (0x80).
  - POD-created set (enumerate with ent_static).
  - Foliage ropes (foreign 3, moving: dynamic).
  - Sleeping rigids (ent 2: semi-dynamic).
  - Water: ocean plane plus water volumes (CPhysArea). Easier: a cheap engine RWI with objtypes 0x200 only, or copy the
    areas.
- **Output fields:** dist; surface_idx (mapped); idmatOrg; iPrim (+0x40); partid = part id; foreignIdx; bTerrain.

## 7. NPCs — confidence high (flags), medium (bone parts)
- The living cylinder/capsule is part 0, id 100, flags 0x41FFFF (CLivingEntity init 0xBCE7DE).
- But character physicalize (0xB729F5, CE PhysicsProxy) runs
  `SetParams(pe_params_part{ipart=0, flagsAND=~(0x8000|0x2000)})` for PE_LIVING. So **rays don't hit the capsule.**
- They hit the skeleton's bone parts. CE CSkeletonPhysics adds one part per physicalized joint: partid = joint index,
  flags 0x1FFFF, geometry usually capsule/box/sphere from the .chr.
- "partid nil" in Lua is because the Lua hit builder (0x39AA4B0) only sets pos, normal, dist, surface, entity and
  renderNode. It does not mean the capsule was hit.
- **Per frame:** IEntity +0x278 → pe; nparts (status 12); for each part with flags & 0x8000, pe_status_pos(ipart) for
  the world TM, plus a primitive from GetData. That is ~20 parts × a few vcalls per NPC.
- Raw reads (+0xA0/+0xAC and m_parts) are cheaper but race the animation sync. Take the +0x48 read lock.

## Verdict: AMBER-GREEN (feasible; exact up to float ties)
**Cheapest probe** (console command, main thread). Log everything to `deagle_native.log`.
1. Cast an RWI (flags 0xF, objtypes 0x11F) from the camera.
2. If `bTerrain`:
   - GetHeightfieldData; sample the 4 corners through the callbacks; intersect A/B ourselves.
   - Compare dist, map[t] and surface_idx.
   - Log typepower, step and size.
3. Otherwise:
   - pe = pCollider; nparts; for hit.ipart: pe_params_part plus pe_status_pos(pMtx3x4); GetType.
   - For a trimesh: log nTris, nVerts, stride and world bounds. Run two-sided Möller–Trumbore over all triangles.
   - Compare t (< 1e-4 m), iPrim (+0x40) and surface_idx (via pIds, pMatMapping, nMats); log part flags and foreign id.
4. Run GetEntitiesInBox(64 m around the player, ent_static, our 4096 buffer). Log count, total parts and triangles,
   geometry-type histogram and QPC time.
5. Log `e_OnDemandPhysics` and `*(world+0x250)`.
6. Enumerate the parts of the NPC under the crosshair: id, flags, type, primitive.

**Unknowns:**
- typepower.
- The KCD cfg value of e_OnDemandPhysics and how wh_e_PernamentPhysicalization behaves.
- The +0x24 field in EventPhysEntityDeleted.
- SRWIParams +0x68.
- Whether the KCD2 .chr files use mesh bone proxies.
- How often dephysicalize fires near the player.
