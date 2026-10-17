# Native rays and water for the bullet sim (static RE, WHGame 1.5.6)

What the engine's Lua `Physics.RayWorldIntersection` and `CryAction.GetWaterInfo` do, call by call, so the native sim
can do the same without Lua. RVAs are relative to the WHGame.dll image base. Everything here was read statically
(capstone); nothing has run in game. gEnv is the static struct at RVA 0x492D800 (MODLOG). Read `rays.md` first for
SRWIParams, ray_hit and the RWI locks.

## 1. Physics.RayWorldIntersection (ScriptBind_Physics)
- **Body:** RVA **0x39AA4B0**. It is registered at 0x144AB01..0x144AB72 (memfn 0x39AA4B0, functor thunk 0x41C37C,
  name "RayWorldIntersection", AddFunction vt+0xB0).
- **Lua signature:** `(vPos, vDir, nMaxHits, iEntTypes [, skipId1, skipId2 [, hitTable]])`. The parser 0x39A4E6C reads
  params 1 and 2 as Vec3 and params 3 and 4 as int.
- **What it passes to the inline wrapper 0x4846E4** (call at 0x39AA6FC):

| field | value | evidence | conf. |
|---|---|---|---|
| world | `m_pSystem(this+0x60)->vt+0x260()`. CSystem 0x531230 returns `[[this+0x20]+0x30]`, which is **gEnv+0x30** | 0x39AA685 | high |
| objtypes | param 4. When it is missing, **0x11F** (`mov [rsp+0x74],0x11F` at 0x39AA52C) | 0x39AA6AB | high |
| flags | **0x1000F**, not 0xF. Bits 16+ are the geometry colltype mask, so this means geom_colltype0. See the note below | 0x39AA6F4 | high |
| hits | ray_hit[10] on the stack (array ctor 0x50B734; the ray_hit ctor 0x5F5DA0 does nothing) | 0x39AA63D | high |
| nMaxHits | param 3, **clamped to 10** (`cmovg r15d, 10`) | 0x39AA635 | high |
| pSkipEnts / nSkipEnts | an array of 2 built from params 5 and 6 (below) | 0x39AA642..67D | high |
| pForeignData / iForeignData | 0 / 0 | 0x39AA6C0, 6CC | high |
| tag / iCaller | "RayWorldIntersection(Script)" / 4 (the wrapper puts `r9d=4`) | 0x39AA6B0, 0x484729 | high |

- **Colltype note.** RWI (0x50ABAA..0x50ABBF): `colltypes = flags>>16; if 0 → 0x8000` (geom_colltype_ray). If flags
  has 0x40 set, it ORs in 1. So Lua rays test **geom_colltype0** parts, where the engine's default would test the
  ray-only proxies. To match Lua hit-for-hit, pass `flags = 0x1000F`.
- **No implicit skips.** It skips only what Lua passes in params 5 and 6. The player is not added. (ballistics.lua
  passes `p.id` itself.)

### Skip ids → IPhysicalEntity* (0x39AA569..0x39AA67D)
```
if (paramCount>=5 && GetParamType(5)!=0 && GetParam(5, ScriptHandle h))      // 0x712164: GetParamAny type 3
    e1 = gEnv->pEntitySystem(gEnv+0xA0)->vt[0x70](es, (EntityId)h.n);        // 0x39AA59C: mov rcx,[0x492D8A0]; call [rax+0x70]
... same for param 6 → e2
n=0; skip[0]=skip[1]=0;
if (e1) { n=1; skip[0] = e1->vt[0x278](e1); }                                // 0x39AA65C  IEntity::GetPhysics
if (e2) { n++;  skip[1] = e2->vt[0x278](e2); }                               // 0x39AA673  (note: index 1, not n-1)
```
- **Engine bug.** When id1 is nil or unknown but id2 is valid, `nSkipEnts` is 1 and `pSkipEnts[0]` is NULL, so **id2 is
  not skipped**. A NULL GetPhysics() result also goes into the array. Natively, compact the array and drop NULLs. (high)
- **IEntitySystem::GetEntity** (CEntitySystem vtable 0x4077048, vt+0x70 → 0x4B4F98):
  - It takes a reader count at `es+0x120` (`lock xadd`, spins while bit 16 is set).
  - Index = `id & 0x3FFFF`, salt = `id >> 18`. The table entry is 0x18 bytes at `es+0x200` (salt word) and `es+0x210`
    (CEntity*), and the entity's own id at `+0xC` is checked.
  - Returns NULL for stale ids. (high)
- **IEntity::GetPhysics**, vt+0x278 (CEntity vtable 0x4072C78 → 0x8A49FC):
  - It returns `GetProxy(1)->vt+0xB0()`.
  - When that is NULL, it falls back to `GetProxy(0xC)->vt+0x88()->vt+0xF0()`.
  - GetProxy(type) is vt+0x250 (0x5D70C4): a list walk from `+0xA8`. (high)

## 2. Hits → Lua table (0x39AA71F..0x39AA839)
The loop runs for i < the RWI return value over `hits[i]` (stride 0x50). Lua gets only these fields:

| Lua key | source | conf. |
|---|---|---|
| `pos` | hit+0x24 (Vec3) | high |
| `normal` | hit+0x30 (Vec3) | high |
| `dist` | hit+0x00 (float) | high |
| `surface` | `(float)(short)hit+0x18` (0x3697B3C: `movsx` → `cvtdq2ps`) | high |
| `entity` | the entity's **script table**, not an id (below) | high |
| `renderNode` | lightuserdata handle (type 3) of an IRenderNode* (below) | high |

There is **no `partid` or `ipart`** in the Lua table.

**Entity:**
- `e = pCollider->vt[0x50](pCollider, 2)` is GetForeignData(PHYS_FOREIGN_ID_ENTITY=2), which returns an IEntity*
  (0x39AA78E).
- If `e` is non-NULL, 0x5D7094 returns `e->vt[0x250](e,2)` (GetProxy(script)), then `->vt[0x90]()` (GetScriptTable),
  and that becomes `entity`. So Lua's `hit.entity.id` comes from the script table.
- **Native EntityId:** `IEntity vt+0x8` (CEntity 0x1A76130: `mov eax,[rcx+0xC]; ret`), so the id is the dword at
  **IEntity+0xC**. (high)
- An entity without a script proxy still has an id natively. Lua sees `entity = nil` for it.

**renderNode:** used only when GetForeignData(2) is NULL.
- `t = pCollider->vt[0x58]()` (GetiForeignData).
- t==1: `GetForeignData(1)` is the IRenderNode* (brush or vegetation).
- t==3: `GetForeignData(3)->vt[0x30]()`. This is probably foliage → GetIRenderNode (medium).
- Other types give no renderNode.

**pCollider is not NULL-checked** (0x39AA783 calls through `[r15]` directly). This matches the crash on water hits noted
in ballistics.lua. Natively, check `dist >= 0 && pCollider` first.

**Hit count:**
- RWI fills empty slots with `dist = -1.0` (0x50B574) and returns only the number of filled slots (r12d, 0x50B5E8).
  Slots are not compacted: hits[0] is the solid hit.
- With flags & 0xF == 0xF (stop at pierceable), all surfaces count as solid, so expect 0 or 1 hit whatever nMaxHits is.
  This last point comes from CryEngine's flag meaning and is not traced in this code (medium).

**Return:** when param 7 (a table) is given, the binding fills `hitTable[i+1]` and returns the count. Otherwise it returns
a new table.

## 3. CryAction.GetWaterInfo
- **Body:** RVA **0x370C97C**. It is registered at 0xB63D75 through the helper 0xB65494 ("GetWaterInfo", params "pos").
  It uses the userdata function thunk 0x2AB9058, which reads param 1 as a Vec3 and calls the member function.
- **One engine call:** `IPhysicalWorld vt+0x270` = **CheckAreas** (CPhysicalWorld 0x445F0C), called at 0x370CA13:

```
int CheckAreas(const Vec3& pt, Vec3& gravity, pe_params_buoyancy* pb, int nMaxBuoys, int iMedium,
               const Vec3& vel, IPhysicalEntity* pent, int iCaller);
GetWaterInfo: pb[8] (ctor 0x546E10), n = w->CheckAreas(pos, grav, pb, 8, -1, Vec3(0), nullptr, 4)
```
- **pe_params_buoyancy** (0x48 bytes; the ctor sets type 0xC, iMedium 0, and 0xFFBFFFFF = unused in the float fields):

| off | field | | off | field |
|---|---|---|---|---|
| 0x00 | type = 12 | | 0x18 | waterFlow (Vec3) |
| 0x04 | waterDensity | | 0x24 | flowVariance |
| 0x08 | kwaterDensity | | 0x28 | waterPlane.n (Vec3) |
| 0x0C | waterDamping | | 0x34 | waterPlane.origin (Vec3) |
| 0x10 | waterResistance | | 0x40 | waterEmin |
| 0x14 | kwaterResistance | | 0x44 | **iMedium** (0 = water, 1 = air) |

- **"Is water":** the first i < n with `pb[i].iMedium == 0` (0x370CA28: `cmp [pb+0x44],0`). If there is none, it
  returns nothing (vt+0x58). (high)
- **Returns** `level, normal, flow` (0x36FD5C4 → IFunctionHandler vt+0x40):
  - `level = pb[i].waterPlane.origin.z` (+0x3C)
  - `normal = waterPlane.n` (+0x28) as a {x,y,z} table
  - `flow = waterFlow` (+0x18) as a table (0x41BD44)
  - The density and damping fields are ignored. (high)
- **The level is only the plane origin's z.** For a tilted plane (a river), the exact height at (x,y) is
  `origin.z - (n.x*(x-ox) + n.y*(y-oy))/n.z`. Lua and this mod use the flat origin.z.
- **Inside CheckAreas** (medium; read only far enough to call it safely):
  - If the global area `world+0x5920` is NULL, it returns 0.
  - Otherwise it applies the global area (the sea) through 0x446518.
  - Then it takes `m_lockCaller[iCaller]` (+0x5FE4+4·iCaller, the 0x10000 write bit; for iCaller 4 that is +0x5FF4) and
    the areas reader count at +0x6000. It walks the area grid (0x6EE310) and tests each area (0x448A70, point inside).
    For each hit it fills pb through 0x446518.
  - It writes `gravity` too.
  - iCaller > 4 is replaced by the thread's TLS caller id (0xAF5EF4).
  - A point counts only when it is inside an area's volume. A point above a lake surface is "dry". (This is the
    existing mod behaviour; whether the volume extends above the surface was not checked.)

## 4. What a native caller must hold or do
- **RWI, iCaller=4:** it takes the exclusive per-caller lock at +0x5FF4 and the grid reader lock at +0x5FE0
  (`rays.md` §3). It is safe from any thread but runs one at a time against all Lua, AI and game rays on iCaller 4.
  Never use iCaller 0–3.
- **CheckAreas, iCaller=4:** it takes the **same** +0x5FF4 lock, plus the +0x6000 reader count. So it is serialised
  with the rays. It was measured at ~9 µs from Lua (PERFORMANCE.md); the native cost is unmeasured.
- **GetEntity:** it holds only its own reader count, and only during the lookup. **GetPhysics, GetProxy and
  GetScriptTable take no lock** and walk component lists that the main thread changes.
  - So resolve skip ids to IPhysicalEntity* **on the main thread** (at the shot or the frame tick) and hand the pointers
    to the sim.
  - Or keep physical entity ids (`GetPhysicalEntityId` vt+0xA8) and turn them back with `GetPhysicalEntityById` vt+0xB0.
    That is what the rwi_queue does with skips.
- **pCollider → IEntity → id on a worker thread:** GetForeignData and GetiForeignData are plain field reads. Reading
  `IEntity+0xC` races only with the entity being deleted. To be safe, keep the raw `pCollider`/`IEntity*` off-thread and
  re-validate the id with GetEntity on the main thread before using it.
- The binding itself takes no extra locks and has no thread checks. It always runs on the Lua (main) thread.

## Native recipe (C++ sketch, untested)
```cpp
// skips (main thread): compact, drop NULLs
void* es = *(void**)(gEnv+0xA0); void* skip[2]; int ns = 0;
for (uint32_t id : {playerId, otherId}) if (id) if (void* e = VF(es,0x70)(es,id)) if (void* p = VF(e,0x278)(e)) skip[ns++] = p;
// ray: SRWIParams rp{} (0x70, rays.md); org, dir, objtypes 0x11F, flags 0x1000F, hits=&h[0], nMaxHits 1,
//      pSkipEnts=skip, nSkipEnts=ns;  n = VF(w,0x118)(w,&rp,"deagle_sim",4)
// hit: if (n>0 && h[0].dist>=0 && h[0].pCollider) { void* e = VF(h.pCollider,0x50)(pc,2);
//      id = e ? *(uint32_t*)((char*)e+0xC) : 0;  surface = h.surface_idx; t = VF(pc,0x58)(pc) for render nodes }
// water: pe_params_buoyancy pb[8] initialised like 0x546E10; Vec3 g, v{};
//      n = VF(w,0x270)(w,&pos,&g,pb,8,-1,&v,nullptr,4); first pb[i].iMedium==0 → level=pb[i].origin.z, n, flow
```

## Unknowns
- Foreign-data type 3 → vt+0x30 is assumed to be foliage → GetIRenderNode (medium).
- That 0xF flags give only one hit is taken from CryEngine semantics, not traced here (medium).
- How far CheckAreas walks the area grid, and whether water volumes include points just above the surface, were not
  traced (medium/low).
- No runtime check yet. The first test: compare a native `0x1000F` ray and `CheckAreas` against the Lua results at the
  same point.
