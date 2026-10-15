# WHGame 1.5.6: typed Lua binding (all RVAs read from the binary)

## 1. Lua version
It's PUC Lua 5.1, not LuaJIT: the strings include "Lua 5.1", `newproxy`, `gfind` and `LUA_PATH`, and there is no "LuaJIT" or `jit.`. It is CryEngine's build: **`lua_Number` is `float`** (`lua_tonumber` @0x41BFA0 returns `movss`). Floats are exact only up to 2^24, so pass entity ids as ScriptHandles, not numbers. Type tags: nil 0, bool 1, lightuserdata 2, number 3, string 4, table 5, function 6. A TValue is 16 bytes, with the tag (int) at +8.
- `lua_State`: top +0x10, base +0x18, l_G +0x20, errorJmp +0xA8, errfunc +0xB0.
- **`CScriptSystem::L` = this+0x10.** CScriptTable keeps statics: `L` @0x549CFA0 and `m_pSS` @0x549CF98.
- API functions, located from the ScriptSystem code (none are exported):
  - pushstring 0x7182C4, pushlstring 0x7177DC, settop 0x717AEC, type 0x715E00
  - rawgeti 0x7163DC, getfield 0xAD496C, setfield 0xA26D60, rawset 0x719AA8
  - createtable 0x718408, newuserdata 0xB965E0, touserdata 0x71805C, **pushcclosure 0xB9663C**
  - tonumber 0x41BFA0, tostring 0x718A18, ref 0x716488, unref 0x7167F4
  - pcall 0x713914, **luaL_error 0x39946E8**, luaG_errormsg 0x3996FE8, luaD_throw 0x39975CC (longjmp)

## 2. CryEngine interfaces
**IScriptSystem** (`*(gEnv+0x28)`, vtable 0x3B8C610, .?AVCScriptSystem@@):
- +0x28 ExecuteFile, +0x30 ExecuteBuffer, **+0x68 CreateTable(bool bEmpty)** (+0x60 is a sized variant)
- BeginCall: +0x78 (table, fn), +0x80 (tableName, fn), +0x88 (fn), +0x90 (handle)
- +0x98 EndCall, +0xA0 EndCallAny, +0xA8 EndCallAnyN
- +0xF0 PushFuncParamAny, **+0xF8 SetGlobalAny(key, any)**, +0x100 GetGlobalAny, +0x108 SetGlobalToNull
- +0x198 RaiseError (vsnprintf, no longjmp), +0x1F0 Allocate

Game code confirms the offsets: 19 calls of gEnv+0x28 → +0x68 and a call to +0xF8.

**IScriptTable** (vtable 0x3A4A700): +0x18 AddRef, +0x20 Release, +0x38 SetValueAny, +0x40 GetValueAny, **+0xB0 AddFunction** @0xB963D4. CScriptTable is { vt, int refs +8, int luaRef +0xC }. CreateTable returns it with refs = 0.

**IFunctionHandler** (CFunctionHandler { vt, L +8, pSS +0x10, name +0x18, paramIdOffset +0x20 }):
- +0x08 GetIScriptSystem, +0x20 GetFuncName, **+0x28 GetParamCount**, +0x30 GetParamType, **+0x38 GetParamAny(idx 1-based, any&)**
- +0x40 EndFunctionAny(a,b,c) → 3, +0x48 (a,b) → 2, **+0x50 (a) → 1** (0 for nil), +0x58 EndFunction → 0
- When GetParamAny's type doesn't match, it returns false **and logs a Lua call stack**.

**ScriptAnyValue** (0x18 bytes): { int type; pad; value @+8 }. Types: 0 any, 1 nil, 2 bool, 3 handle (lightuserdata, which is what entity ids use), 4 number (float), 5 string, 6 table, 7 function, 8 userdata (ref @+0x10), 9 vec3.

**SUserFunctionDesc** (0x58 bytes):
- +0x00 sFunctionName, +0x08 sFunctionParams, +0x10 sGlobalName
- +0x18 Functor (32 bytes: func/memfn +0, +8 unused, callee +0x10, thunk +0x18)
- +0x38 nParamIdOffset (read as a byte), +0x40 pUserDataFunc, +0x48 pDataBuffer, +0x50 nDataSize

**Registration** (ScriptBind_System, e.g. LogAlways @0x144B65F): `MakeFunctor(&tmp, this, &memfn)` @0x23D2A48, then `CScriptableBase::RegisterFunction(this, "LogAlways", functor)` @0xB9588C. RegisterFunction fills the desc (global = this+8, offset = this+0x58) and calls `m_pMethodsTable(this+0x48)->vt[0xB0]`.

**What AddFunction does:** it sprintf's `"%s.%s(%s)"` into a 256-byte stack buffer with no bound, so keep names short. It then `newuserdata`'s a copy of the functor (or of {fn, size, data}), pushes a 3-upvalue C closure (0x7137AC, or 0x719A48 for a userdata function) and rawsets it on the table. **The desc and its strings don't have to outlive the call.**
- A closure with a functor calls `thunk(&functorCopy, pH)`.
- A closure with a userdata function calls **`fn(pH, dataCopy, size)`**. It is used when the functor is all zero, and it is the simplest ABI.

## 3. Recommendation: (b) AddFunction with pUserDataFunc
This needs no new patterns: only gEnv (already found) plus the vtable offsets above, which have been stable across CryScriptSystem versions.
1. `ss = *(gEnv+0x28)`
2. `t = ss->vt[0x68](ss, false)`, then `t->vt[0x18](t)` (AddRef)
3. For each function, fill a desc {name, "", "DN", functor = 0, offset = 0, fn, &ctx, sizeof ctx} and call `t->vt[0xB0](t, &desc)`
4. `any = {6, t}`, then `ss->vt[0xF8](ss, "DN", &any)`

In the function, read numbers with `GetParamAny(i, {type 4})` and check the bool it returns; return with +0x50 / +0x48 / +0x40.

**Do the registration on the Lua thread**, for example from `HkLog` on `[DN] bind`. Don't do it from the Init thread.

Caveat: params and returns use `pSS->L`, the main state, so don't call DN.* from inside coroutines (the same rule applies to every ScriptBind).

(a) The raw lua_State route needs these patterns, each verified to match exactly once in .text:
- luaL_error: `48 8B C4 48 89 50 10 4C 89 40 18 4C 89 48 20 53 56 57 48 83 EC 20 BA 01 00 00 00 48 8D 70 18 48 8B D9 E8`
- lua_pushcclosure: `48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 4C 8B 49 20 48 8B F2 49 63 F8 48 8B D9 49 8B 41 70 49 39 41 78`
- lua_tonumber: `48 83 EC 38 E8 ?? ?? ?? ?? 83 78 08 03 75 09 F3 0F 10 00 48 83 C4 38 C3`
- lua_setfield: `48 89 5C 24 08 57 48 83 EC 30 4D 8B D0 48 8B D9 E8 ?? ?? ?? ?? 48 8B F8 49 83 C8 FF 49 FF C0 43 80 3C 02 00 75 F6 49 8B D2 48 8B CB E8 ?? ?? ?? ?? 4C 8B 4B 10 4C 8D 44 24 20 49 83 E9 10` (it's this long because a shorter version also matches getfield)
- pushnumber is inlined everywhere, so it would have to be hand-written from the lua_State layout.

(a) is more fragile; use it only for luaL_error (see 4). Also unique, for (b) cross-checks: AddFunction `48 89 5C 24 18 55 56 57 41 56 41 57 48 81 EC 40 01 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 84 24 30 01 00 00 48 8B 42 08 4C 8B F2 4C 8B 0A`, RegisterFunction `4C 8B DC 48 81 EC 88 00 00 00 4C 8B 49 48 45 33 D2 4D 85 C9 74 ?? 41 0F 28 00`.

## 4. Errors
`luaD_throw` → **VCRUNTIME `longjmp`**, and `rawrunprotected` sets up `__intrinsic_setjmp(buf, rsp)`. That makes it an unwinding longjmp (RtlUnwindEx): it can cross our frames, but C++ destructors aren't guaranteed under /EHsc.
- Call `luaL_error(*(lua_State**)(pH+8), ...)` only as a tail, from a frame with no live C++ objects and no `__try`.
- Never let a C++ exception or SEH escape into Lua frames. Catch inside and convert.
- Simplest option, with zero patterns: return `nil, "msg"` and let a Lua wrapper call `error(msg, 2)`.

Without `-lua_storedebug 1`, runtime errors (luaG_runerror @0x39970D8) lose their text ("[Error] Lua error. Please run with -lua_storedebug 1"). luaL_error and error() messages keep it, as seen in the logbackups ("bad argument #1 to 'tostring'"). The pcall error handler (@0x39AF4C8, registry ref at ss+0x58) only writes the "$6    > fn (file: line)" stack to kcd.log when storedebug is on, so the caller's pcall has to log the error.

## 5. Hot reload
The closure's Lua userdata keeps raw code pointers into our DLL (fn or thunk). Today the .asi is never unloaded, so this is safe. If a reloadable core is ever split off:
- Register a stable trampoline that lives in the never-unloaded loader and dispatches through an atomic table (`impl[id]`). On unload, swap in a stub that returns `nil, "DN unloaded"`.
- Also release any IScriptTable or function refs we hold, unhook, and unload only from outside Lua.
- Setting `DN = nil` alone isn't enough, because cached `local f = DN.fxr` references still crash.
- Re-binding is idempotent: create a new table and SetGlobalAny again.

## Smallest test
```cpp
struct Any { int type, pad; union { float n; void* p; const char* s; }; uint64_t x; };
static int __cdecl DnAdd(uintptr_t pH, void*, int) {
  auto get = (bool(__fastcall*)(uintptr_t,int,Any*))VFn(pH,0x38);
  Any a{4}, b{4};
  if (!get(pH,1,&a) || !get(pH,2,&b)) { Any e{5}; e.s = "DN.add: numbers expected"; Any z{1}; return ((int(__fastcall*)(uintptr_t,Any*,Any*))VFn(pH,0x48))(pH,&z,&e); }
  Any r{4}; r.n = a.n + b.n; return ((int(__fastcall*)(uintptr_t,Any*))VFn(pH,0x50))(pH,&r);
}
// in HkLog on "[DN] bind": CreateTable(+0x68,false), AddRef(+0x18), desc{ "add","","DN", {0}, 0, DnAdd, nullptr, 0 } -> AddFunction(+0xB0), SetGlobalAny(+0xF8,"DN",{6,t})
```
In Lua: `System.LogAlways("[DN] bind"); System.LogAlways("[Deagle] DN.add=" .. tostring(DN and DN.add(2, 3)))` should print 5. Time 1e5 calls with os.clock, aiming for under 1 us each.

Confidence: high for offsets and layouts (read from code and confirmed at call sites). Medium for the coroutine and destructor-under-longjmp caveats. Not yet tested at runtime.
