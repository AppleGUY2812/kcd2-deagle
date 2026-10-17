# Native world queries for the bullet sim (static RE, WHGame 1.5.6)

What the Lua calls the sim makes about the world (nearby NPCs, their position, alive or dead, the game clock, the
player) do natively, so the plugin can do them on the main thread without Lua. RVAs are relative to the WHGame.dll image
base. Everything was read statically (capstone); nothing has run in game. gEnv is at RVA 0x492D800. Read
`sim_binding.md`, `rays.md` and `script_binding.md` first. Scripts: scratchpad `re5/`.

## 1. Physics.SamplePhysEnvironment (ScriptBind_Physics)
- **Body:** RVA **0x39AB354**. It is registered by the helper 0x1449F04 (called at 0x144ABB1; "SamplePhysEnvironment",
  params "vPoint,fRadius") as a userdata function: the thunk 0x4CFA8C calls `fn(this, pH)`.
- **Lua:** `(vPoint, fRadius [, objtypes])`. The parser 0x39A4CF8 reads a Vec3 and a float; if that fails it returns nil.
  Param 3 is an int. **Default objtypes 0x20007** (static|sleeping_rigid|rigid|sort_by_mass): living is not included
  unless passed. `ent_living` = 8 (registered at 0x14518B6).
- **What it does** (this mirrors CryEngine's own SamplePhysEnvironment):
  1. `w = m_pSystem(this+0x60)->vt+0x260()` (gEnv+0x30). It makes a sphere: `w->vt+0x20()` (geometry manager)
     `->vt+0x28(4, &{center, r})`, which is CreatePrimitive(sphere).
  2. **`n = w->vt+0x110(w, &(c-r·1), &(c+r·1), &list, objtypes, 0)`** (0x39AB541). This is GetEntitiesInBox on the cube
     around the sphere, with prealloc 0.
  3. For each entity it calls `GetStatus(vt+0x30)` with `pe_status_nparts` (type **12**) to get the part count. Then for
     each `ipart` from nparts-1 down to 0 it makes a `pe_status_pos` (partid -1, ipart) and calls GetStatus. A part
     counts when `(flagsOR & 0x1000001) == 1` (colltype0, not no_coll_response) **and** `pGeom->vt+0x50` (Intersect)
     with the sphere at the part's pos, q and scale is non-zero (0x39AB709). So the sphere test is exact per-part geometry,
     not a centre distance.
  4. For hit k (1-based) it fills a **flat array**:
     - `t[3k-2]` is the entity's script table (`GetForeignData(2)` → 0x5D7094), or a nil hole
     - `t[3k-1]` is `sp.partid`
     - `t[3k]` is `w->vt+0xA8(pe)` (GetPhysicalEntityId)
     The numbers are Lua floats (0x358D4B8: `cvtdq2ps`, then SetAtAny vt+0x68).
  5. After a hit it goes to the next entity unless `pe->vt+8()` (GetType) is **6** (PE_ARTICULATED, a ragdoll). For a
     ragdoll every hit part is listed (0x39AB79F).
  6. It releases the sphere (`vt+0x18`) and returns the table.
- **IPhysicalEntity slots:** +0x08 GetType, +0x10 AddRef, +0x30 GetStatus(pe_status*), +0x50 GetForeignData(int),
  +0x58 GetiForeignData; +0x18 Release by CryEngine order (medium).

### GetEntitiesInBox, IPhysicalWorld vt+0x110 (0x84C354)
- **Signature:** `int GetEntitiesInBox(Vec3* min, Vec3* max, IPhysicalEntity**& list, int objtypes, int szListPrealloc)`.
- It takes the iCaller-4 write lock (`lock cmpxchg [w+0x5FF4], 0x10000`, the same lock as RWI, `rays.md` §3). It calls
  the internal GetEntitiesAround 0x6EE310 `(w, min, max, list, objtypes, pPetitioner=0, prealloc, iCaller=4)`, then
  unlocks (0x84C3DC).
- **Filter:** `objtypes & (1 << simclass)`, where simclass is 3 bits at bit 50 of the grid entry (0x6EEF3D..4A). So
  ent_living (8) means simclass 3 = PE_LIVING. (high)
- **The list buffer** (0x6EED7A..0x6EEDB4, 0x6EFAD0..0x6EFC43):
  - With `prealloc == 0` and no `ent_allocate_list` (0x40000, `bt esi,0x12`), `*list` is the per-caller temp array
    `*(w+0x5D58 + iCaller·0x58)` (iCaller 4: **w+0x5EB8**, capacity w+0x5EC0). It is shared by **every iCaller-4
    query from any thread**, may be reallocated, and is valid only until the next such query; the lock is already
    released when the call returns.
  - So **copy the n pointers out at once**, or pass your own array: set `list = myArray` and `prealloc = cap`. When
    `n <= cap` the result is copied into myArray. When `n > cap`, `*list` is replaced by the temp array, so check
    `list == myArray` after the call.
  - `ent_allocate_list` gives a heap copy from the engine allocator (0x549D378): avoid it. `ent_addref_results`
    (0x100000, 0x6EFC4A) calls `vt+0x10` (AddRef) on each result. The same-box cache applies only to iCaller < 4.
- Lua `SamplePhysEnvironment(.., ent_living)` measured 0.12 ms at 30 m and 1.9 ms at 300 m (PERFORMANCE.md).
  The grid walk is probably most of that (not measured natively), so keep the box small.

## 2. An NPC's position
- **Lua `entity:GetWorldPos()`** (ScriptBind_Entity memfn **0x4D131C**, registered at 0x144EA14) gets the entity from
  `self.id` (0x712810 → GetEntity vt+0x70) and returns **CEntity+0x64, +0x74, +0x84**, the translation of the world
  Matrix34 at **CEntity+0x58**, as a new {x,y,z} table. (high)
- **Natively:** IEntity **vt+0x100** returns `&worldTM` (0x1A74140: `lea rax,[rcx+0x58]`); IEntity **vt+0x170** is
  `Vec3* GetWorldPos(Vec3* ret)` (0x648030). Or read the three floats directly (plain fields, main thread). (high)
- **Physics alternative:** `pe->vt+0x30(pe, &sp)` with **pe_status_pos** (0x80 bytes; ctor 0x44931C):

| off | field | | off | field |
|---|---|---|---|---|
| 0x00 | type = **1** | | 0x3C | q (x,y,z,w) |
| 0x04 | partid = -1 | | 0x4C | scale |
| 0x08 | ipart = -1 | | 0x50 | iSimClass |
| 0x0C | flags = 0 (world CS) | | 0x58/0x60 | pMtx3x4 / pMtx3x3 = 0 |
| 0x10/0x14 | flagsOR / flagsAND | | 0x68/0x70 | pGeom / pGeomProxy |
| 0x18 | **pos** (Vec3) | | 0x78 | timeBack = 0 |
| 0x24 | BBox[2] (relative to pos) | | | |

  The GetStatus return value is non-zero on success. This is the physics copy, which can lead or lag the entity matrix
  by a frame. Use the entity matrix to match Lua.

## 3. Is it dead?
- **Lua `entity.actor:IsDead()`:** class `C_ScriptBindActor@entitymodule@wh` (vtable 0x3ECB620, "IsDead" registered at
  0x128AA3F). The memfn is a vcall thunk 0x21393A0 → binding vt+0x1A8 → **0x82A85C**: `actor = GetActor(pH)`
  (0x82B834), then `return actor->vt+0x130()`.
- **GetActor** (0x82B834): `id = (EntityId)pH->vt+0x10()` (GetThis: an actor table's `this` is the EntityId);
  `fw = this+0x70` = `ISystem vt+0x2C8()` (gEnv+0x90) `->vt+0x80()`; `as = fw->vt+0xC8()` (CCryAction 0x641150 →
  `+0x518`); `actor = as->vt+0x18(as, id)` (CActorSystem 0x647E40, map lookup). Natively `fw =
  (*(gEnv+0x90))->vt+0x80()`; game code uses the same chain at 0x6444C3 and 0x956974. (high)
- **IActor vt+0x130 = C_Actor::IsDead (0x64583C)**, shared by C_Actor, C_Human, C_NPCActor and C_Player:
  ```
  if (soul = *(actor+0x668)) return soul->vt+0x30();   // C_Soul 0x645860: *(float*)(soul+0x708) <= 0
  return *(float*)(actor+0x164) <= 0;
  ```
  Call the virtual (high). The field offsets are given as evidence only (medium-high).
- **GetHealth:** binding vt+0x198 → actor **vt+0xF0** (returns a float).
- **Dead and ent_living:** ent_living selects simclass 3 (PE_LIVING) only (high). A ragdoll is PE_ARTICULATED
  (type 6); its simclass was not traced, but in CryEngine it is not 3, so a ragdolled corpse is very likely excluded
  (medium). **Unknown:** whether the living entity stays during the death animation before the queued ragdoll
  (`kcd2_gore_ragdoll_re.md` §2, ~1 s). Unconscious NPCs are alive but ragdolled. **Always check IsDead.**

## 4. Game time in double
- **gEnv+0x80 = ITimer** (the CSystem ctor stores `&CSystem.m_Time` there at 0xA53199; CTimer vtable 0x3A6CCD0).
  **Lua `System.GetCurrTime()`** (0x39A7270) is `m_pTimer(this+0x80)->vt+0x18(timer, 0)`; `GetFrameTime()` (0x39A7D54)
  is `vt+0x48(timer, 0)`. ETimer 0 = game, 1 = UI.

| vt | RVA | what |
|---|---|---|
| +0x18 | 0x509C50 | GetCurrTime(which) float = `(float)ticks[which] * 1e-5f` |
| +0x20 | 0x1A73920 | the same as a **double**: `(double)ticks[which] * 1e-5` |
| +0x28 | 0x5D5F60 | GetFrameStartTime(which) → `const int64*` = `timer+0x10+8·which` |
| +0x40 | 0x4611DC | what Lua GetCurrAsyncTime calls |
| +0x48 | 0x5D5F7C | GetFrameTime(which): `!timer[0x60]` → 0; which 1 → `+0x4C`; which 0 → `timer[0x54] ? 0 : +0x48` |
| +0x50 | 0x666794 | real frame time (`+0x50`, 0 when `timer[0x60]` is 0) |

- **Ticks** are int64 in units of 1e-5 s: `*(int64*)(timer+0x10)` for game, `+0x18` for UI. The constants are 1e-5f at
  0x4099F20 and 1e-5 at 0x409EA58. Use vt+0x20 or the raw ticks for full precision.
- To reproduce Lua's value bit-for-bit: `(float)((float)ticks * 1e-5f)`. That is where the 3.9 ms steps at ~51600 s
  come from. (high)

## 5. IScriptTable::SetValueAny and SetAtAny (CScriptTable vtable 0x3A4A700)
- **Slots:** +0x18 AddRef, +0x20 Release, **+0x38 SetValueAny 0x71789C**, +0x40 GetValueAny 0x71624C, +0x48/+0x50
  Begin/EndSetGetChain, **+0x68 SetAtAny 0xAD6AF8** `(int idx, const ScriptAnyValue&)`, +0x70 GetAtAny 0x7147C0.
- **`void SetValueAny(const char* key, const ScriptAnyValue& any, bool bChain)`** (rdx, r8, r9b). Pass
  **bChain = false**: then it pushes the table itself (0x71793E → 0x715F60); true assumes a BeginSetGetChain.
  - If the Lua stack is non-empty and its top has a metatable, it reads the old value and skips the write when it is
    equal (0x7178EC..92D).
  - Writes are rawset (no `__newindex`). (high)
- **ScriptAnyValue type 9 (vec3)** is {type 9 @0, x @+8, y @+0xC, z @+0x10}.
  - SetValueAny **reuses the table already at that key** when it is a table whose `x` is a number. It rawsets x, y and z
    into it in place (0x717964..0x717A85).
  - Otherwise it pushes a **new plain table {x,y,z}** with no metatable (0x15A5348) and rawsets it.
  - So writing `pos` updates any Lua alias of the old `pos` table too. Give each round its own table. (high)
- **SetAtAny** always pushes a fresh value (PushAny 0x717B60 → rawseti 0xB957C4): a vec3 is always a new table.

## 6. The player's EntityId
- **IGameFramework vt+0x218** (0x9F3CD0) returns `[[[fw+0x88]+0x48]+0x10]+0x20`. It is 0 when there is no client
  (main menu).
- **vt+0x210** (0x35711A4) is `gEnv->pEntitySystem->GetEntity(vt+0x218())`, the client entity. This is
  GetClientActorId / GetClientEntity in CryEngine order (medium-high).
- Simplest and certain: Lua hands `player.id` to the plugin once per load.

## 7. Threads
- Run all of it on the **main thread**. GetEntitiesInBox takes the same iCaller-4 lock as RWI and CheckAreas; the rest
  takes no lock. Use the pointers within the same tick, or keep EntityIds / physical ids (vt+0xA8) and re-resolve.

## Native recipe (C++ sketch, untested)
```cpp
// main thread, once per sim tick
void* w = *(void**)(gEnv+0x30); void* es = *(void**)(gEnv+0xA0); void* timer = *(void**)(gEnv+0x80);
double now = VF<double(*)(void*,int)>(timer,0x20)(timer,0);            // full precision game clock
float  dt  = VF<float(*)(void*,int)>(timer,0x48)(timer,0);
void* fw = VF<void*(*)(void*)>(*(void**)(gEnv+0x90),0x80)(*(void**)(gEnv+0x90));
void* as = VF<void*(*)(void*)>(fw,0xC8)(fw);
uint32_t playerId = VF<uint32_t(*)(void*)>(fw,0x218)(fw);              // or player.id from Lua
Vec3 lo = c - Vec3(r), hi = c + Vec3(r); void* buf[64]; void** list = buf;
int n = VF<int(*)(void*,Vec3*,Vec3*,void***,int,int)>(w,0x110)(w,&lo,&hi,&list,8 /*ent_living*/,64);
for (int i = 0; i < n && i < 64; i++) {                                // if list != buf, it is the shared temp array: copy now
  void* pe = list[i]; void* e = VF<void*(*)(void*,int)>(pe,0x50)(pe,2); if (!e) continue;   // IEntity*
  uint32_t id = *(uint32_t*)((char*)e+0xC); if (id == playerId) continue;
  void* a = VF<void*(*)(void*,uint32_t)>(as,0x18)(as,id); if (!a || VF<bool(*)(void*)>(a,0x130)(a)) continue; // dead
  float* m = (float*)((char*)e+0x58); Vec3 pos{m[3], m[7], m[11]};       // = entity:GetWorldPos()
}
// writing back: ScriptAnyValue v{9}; v.x..z = pos; VF<void(*)(void*,const char*,void*,bool)>(tbl,0x38)(tbl,"pos",&v,false);
```

## Unknowns
- Whether a freshly killed NPC keeps its PE_LIVING entity during the death animation, and the ragdoll's simclass. Test:
  run the query on a body just after the kill and again 2 s later.
- IPhysicalEntity +0x18 = Release, IPhysicalWorld +0x20 = GetGeomManager, and the extra Intersect arguments
  (lock, iCaller 4) are read from call sites, not from their bodies (medium).
- The names of IGameFramework vt+0x218 / vt+0x210 come from CryEngine's order (medium-high). The behaviour (an EntityId,
  then GetEntity of it) is read from the code.
- No runtime check yet. First test: compare the native n, ids and positions with `SamplePhysEnvironment(c, r, ent_living)`
  and `GetWorldPos` at the same frame, and compare `now` with `System.GetCurrTime()`.
