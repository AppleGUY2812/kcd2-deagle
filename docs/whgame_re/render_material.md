# Q8: the drawn material at a ray hit (WHGame 1.5.6), read from the binary

All RVAs are image-relative. Disassembly scratch is in `scratchpad/q8/` (`rayint.asm`, `impl.asm`, `lockvb.asm`, `phys.asm`).

## 1. ray_hit and the static brush (confidence: high)
- The Lua `Physics.RayWorldIntersection` hit builder is at 0x39AA4B0. Each `ray_hit` is 0x50 bytes, the same as CE:
  dist +0x00, pCollider +0x08, ipart +0x10, partid +0x14, surface_idx (short) +0x18, idmatOrg (short) +0x1A,
  foreignIdx +0x1C, iNode +0x20, pt +0x24, n +0x30, bTerrain +0x3C, iPrim +0x40, next +0x48.
- IPhysicalEntity (CPhysicalEntity vt 0x3A578C0): **+0x50 GetForeignData(int type)**: it returns the pointer at +0x20
  only when the short at +0x28 equals type. **+0x58 GetiForeignData()** returns the short at +0x28.
- CBrush physicalize (0x530A3C) calls IPhysicalWorld vtable +0x88 CreatePhysicalEntity(PE_STATIC=1, NULL, this,
  iForeignData = 1, id = -1, ...) and stores the result in CBrush+0x80. It then sets pe_params_foreign_data (type 0xB).
  So for a static brush, `GetForeignData(1)` is the `CBrush*`, which is also its `IRenderNode*` (offset 0).
- **The Lua hit table already carries it:** for iForeignData==1, the builder sets `hit.renderNode` to a ScriptHandle
  (ANY type 3, lightuserdata) holding `GetForeignData(1)`. For foliage (3) it uses foliage->vt+0x30, and for
  entities (2) it sets `hit.entity`.
- RTTI and vtables: CBrush `.?AVCBrush@@` vt **0x401FE70**. CMovableBrush vt 0x4020370 and COwnedBrush vt 0x3A6F148
  share the same slots below.

## 2. From the render node to the triangle test (confidence: high)
- CBrush layout: +0x50 Matrix34 world TM, +0x80 phys entity, +0x88 material override, +0x98 IStatObj*.
- IRenderNode **+0xB0 GetEntityStatObj(uint nSubPart, Matrix34A* outTM, bool)** (0x433F44, CE5 signature). It
  returns +0x98 and copies +0x50 into outTM when that pointer is given.
- IRenderNode **+0x130 GetMaterial(Vec3* = NULL)** (0x433F20). It returns the override (+0x88) if set, else
  `statobj+0xE8`.
- Probably IRenderNode +0x48 (0x3492CDC) gives the name: `statobj+0xA0`, the file path.
- **IStatObj +0x288 = CStatObj::RayIntersection(SRayHitInfo&, IMaterial* pCustomMtl)** at 0x3529890 (CStatObj
  `.?AUCStatObj@@` vt 0x3A2F888, slot 81). It works in the object's local space:
  - First an AABB reject against statobj +0x100 and +0x10C.
  - For a compound (+0x178 & 0x20, not merged), it recurses into each sub-object with the inverse sub TM.
  - Otherwise it takes m_pRenderMesh (+0x58), or a loaded LOD (+0x158[+0x16A]) if that is missing, and calls
    CRenderMeshUtils::RayIntersection (0x35043B8). With no mesh it returns false.
- CRenderMeshUtils::RayIntersection runs LockForThreadAccess (a mesh mutex plus a counter), then
  SIntersectionData::Init (0x35014C4). Init calls GetPosPtr(FSL_READ) at vt +0x158 and GetIndexPtr at +0x1B0, and
  **returns false when the counts or pointers are 0**. Then it runs RayIntersectionImpl (0x3505004), followed by the
  unlocks.
  - Impl loops linearly over the render chunks and their triangles; there is no BVH.
  - When pMtl is set it skips nodraw chunks.
  - It sets `nHitSurfaceID = pMtl->GetSafeSubMtl(nHitMatID)->GetSurfaceTypeId()`, **only if pMtl != NULL**.
  - Caveat: when Init fails, the thread-access counter is never released. CE has the same bug.
- **SRayHitInfo, 0xC0 bytes** (CE5 FEATURE_SVO_GI layout, checked against every access):
  - inReferencePoint +0x00, inRay.origin +0x0C, inRay.direction +0x18 (zero means a sphere test).
  - bInFirstHit +0x24, inRetTriangle +0x25, bUseCache +0x26, bOnlyZWrite +0x27, bGetVertColorAndTC +0x28 (must be 0),
    useBoxIntersection +0x29, pHitTris +0x30, vHitTC +0x38, vHitColor +0x40, tangents +0x50/+0x60.
  - **fMaxHitDistance +0x70**, vTri0..2 +0x74/+0x80/+0x8C.
  - Outputs: **fDistance +0x98, vHitPos +0x9C, vHitNormal +0xA8, nHitMatID +0xB4, nHitTriID +0xB8,
    nHitSurfaceID +0xBC**.
- **CPU mesh data:** CRenderMesh::LockVB (0x57FFD8) with FSL_READ returns the system copy if one exists. If not:
  - In MT render mode it runs CopyStreamToSystemForUpdate (0x14ED798): it maps the GPU buffer through DevBufMan
    BeginRead (0x2536048), memcpys it into a new system buffer and keeps that buffer.
  - In ST mode it maps the GPU buffer directly.
  - It returns NULL when no GPU buffer exists (the buffer id is ~0, i.e. streamed out), and the ray test then returns
    false.

  So streamed `.cgfm` meshes work whenever they're resident, and visible objects are resident.
- **The decal path is still there:** CDecalManager::Spawn (0x34B67D0, the "CDecalManager::Spawn" strings) calls
  CRenderMeshUtils::RayIntersection. e_DecalsMaxTrisInObject defaults to 8000. I3DEngine **+0x770
  RenderMeshRayIntersection(IRenderMesh*, SRayHitInfo&, IMaterial*)** is a thunk to the same function.

## 3. Materials and surface types (confidence: high)
- IMaterial (CMatInfo vt 0x40283F0):
  - +0x40 GetName (m_sMaterialName at +0x28)
  - +0x58 IsDefault
  - **+0x60 GetSurfaceTypeId** (+0x38)
  - +0x68 SetSurfaceType
  - **+0x70 GetSurfaceType() → ISurfaceType*** (it calls matman+0xA0 with id and name)
  - +0xE0 GetSubMtlCount (DynArray at +0x70, with MTL_FLAG_MULTI_SUBMTL 0x100 at +0x40)
  - +0xF0 GetSubMtl(i)
  - **+0x140 GetSafeSubMtl(i)** (falls back to the default material)
  - +0xA8 GetShaderItem(i)
- I3DEngine **+0x628 GetMaterialManager()** returns the global at **0x5496EF0** (CMatMan, vt 0x4028938):
  - +0x70 GetDefaultMaterial
  - **+0xA0 GetSurfaceType(int id, const char* why)**
  - **+0xA8 GetSurfaceTypeManager()** (at +0xB8)
- ISurfaceTypeManager (vt 0x3A404B8): +0x10 GetSurfaceTypeByName(name, why, warn), **+0x18 GetSurfaceType(id, why)**.
- ISurfaceType (CMaterialSurfaceType vt 0x3A40528): **+0x10 GetId** (u16 at +0x18), **+0x18 GetName**, +0x20 GetType.

## 4. Cost
- AABB reject first, then a brute-force triangle loop: O(tris of the hit object), with no acceleration structure.
  CE does the same per bullet decal on objects with up to 8000 tris (e_DecalsHitCache is a tiny 8-entry
  last-triangle cache, global at 0x533C2C0, with 0xF0-byte entries).
- The first test on a mesh pays a one-off GPU→CPU copy of its position and index streams, and keeps that memory.
- My estimate is tens of µs per test for a prop of a few thousand triangles. Gate it to suspicious surfaces and time
  it in game.
- Call it only on the main thread: there is a global cache plus mesh locks.

## Cheapest native test (one call on a known cauldron hit)
Lua passes `hit.renderNode` (lightuserdata; `tostring` gives the pointer), `hit.pos` and the ray dir. Then natively:
```
vt=*(void***)rn;  require vt==base+0x401FE70 (or 0x4020370 / 0x3A6F148)
Matrix34 W; so = vt[0xB0/8](rn, 0, &W, false); mtl = vt[0x130/8](rn, NULL); if(!so||!mtl) fail
L = W.GetInverted(); o = L*(pos - dir*0.25); ld = L.TransformVector(dir*0.5)
uint8 hi[0xC0]={0}; ref(+0)=o; origin(+0xC)=o; dir(+0x18)=ld; *(float*)(hi+0x70)=2.0f
ok = (*(void***)so)[0x288/8](so, hi, mtl)
id=*(int*)(hi+0xB4); sid=*(int*)(hi+0xBC)
sub = mtl_vt[0x140/8](mtl,id); name = sub_vt[0x40/8](sub); st = sub_vt[0x70/8](sub); stName = st_vt[0x18/8](st)
log ok, id, name, sid, stName, rn_vt[0x48/8](rn) (cgf path), and the QPC time
```
Expected on the iron pot: ok=1, a sub-material whose name or surface isn't mat_wood.

## Unknowns
- Whether the pot's render sub-material carries a SurfaceType in its .mtl. Render-only sub-mtls may be blank or
  mat_default. In that case, fall back to the sub-material name (or its texture).
- The real per-call and first-call costs, and whether DevBufMan BeginRead stalls on DX12. Measure both.
- A ScriptHandle round-trip through the text bridge needs the pointer parsed from the `tostring` text.
