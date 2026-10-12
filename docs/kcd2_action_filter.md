# KCD2 action filters (`no_torch`): how the plugin can turn one on

Static RE of `WHGame.dll`, game version 1.5.6. RVAs are relative to the image base. Disassembly was done with Python + capstone, using the `re/wh.py` helpers in the session scratchpad.
Goal: block `toggle_torch` / `xi_toggle_torch` (R) while the Deagle is drawn. Use the `no_torch` actionfilter from `Libs/Config/defaultProfile.xml` (~line 1804).

Status: **CONFIRMED** means it was read from the disassembly. **UNVERIFIED** means it was inferred, or still needs a live test.

---

## 1. Pointer chain to IActionMapManager

| Step | Evidence | Status |
|---|---|---|
| `CCryAction` (the IGameFramework implementation), RTTI `.?AVCCryAction@@`, vtable RVA **0x4047350** | RTTI scan | CONFIRMED |
| `CCryAction` vtable slot 28 (**+0xE0**) = `0x3F7B00`: `mov rax,[rcx+0x530]; ret` → **GetIActionMapManager()** | bytes `48 8B 81 30 05 00 00 C3` | CONFIRMED (the slot is a getter for `+0x530`) |
| `CCryAction+0x530` is the CActionMapManager. In the framework init (around `0xB83BA7`), `mov rcx,[rsi+0x530]` is stored into `CScriptBind_ActionMapManager+0x68` before the bind ctor `0x12DBB78` is called | disasm `0xB83BA7..0xB83BF4` | CONFIRMED |
| The Lua binding `ActionMapManager.IsFilterEnabled` (registered at `0x12DBCA7`, func **0x370E480**) does `mov rcx,[rcx+0x68]; call [rax+0x108]` | see below | CONFIRMED |
| `gEnv → pGameFramework` offset | no clean `gEnv+X → call [vt+0xE0]` pattern was found in time | **UNVERIFIED** |

Chain: `gEnv->pGameFramework` (offset unknown) → `vtbl+0xE0` GetIActionMapManager() → `CActionMapManager*` (RTTI `.?AVCActionMapManager@@`, vtable RVA **0x4048018**). The same pointer is also at `CCryAction+0x530`.

Robust way to find the framework without knowing the gEnv offset: the plugin already resolves gEnv slots by RTTI. Scan `gEnv+0x00..0x200` for a pointer whose vtable RVA is `0x4047350` (CCryAction). Another option is to scan for a pointer whose vtable is `0x4048018` (CActionMapManager) directly.

```
0370E480  push rbx
0370E486  mov  rcx, [rcx+0x68]        ; CScriptBind_ActionMapManager::m_pActionMapManager
0370E48D  mov  rdx, r8                ; filterName
0370E490  mov  rax, [rcx]
0370E493  call [rax+0x108]            ; IActionMapManager::IsFilterEnabled(name)
```

## 2. CActionMapManager vtable (0x4048018)

| Slot | Offset | RVA | Meaning | Evidence |
|---|---|---|---|---|
| 29 | +0xE8 | 0x85AB44 | `EnableActionMap(name, bool)`: looks up the maps at `this+0x50`. A null name means all maps. Logs "Missing action map '%s'" | CONFIRMED |
| **30** | **+0xF0** | **0x6169B8** | **EnableFilter(name, uint8 key)**: calls `0x6169F0(this,name, r8b=1, r9b=key, 0)` | CONFIRMED |
| **31** | **+0xF8** | **0x6169D4** | **Disable/release filter(name, uint8 key, bool all)**: calls `0x6169F0(this,name, r8b=0, r9b=key, [rsp+20]=all)` | CONFIRMED structurally; naming UNVERIFIED |
| **33** | **+0x108** | **0x35742A4** | **IsFilterEnabled(name)**: looks up `this+0x60`, then `filter->vtbl+0x50`. Returns false if the name is missing | CONFIRMED |
| 34 | +0x110 | 0x85AEB0 | Iterates the enabled action maps → `0x85B0AC` (probably releases filtered/held actions) | likely |

Shared worker `0x6169F0`:
```
00616A12 test rdx,rdx / cmp byte[rdx],0 → je 0x220E99E   ; null/"" name → apply to ALL filters (this+0x60)
00616A24 lea  r14,[rcx+0x60]                              ; std::map<string, IActionFilter*> m_filters
00616A40 call 0x3F2C5C / 0x3F2CC0                         ; find(name)
00616A66 je   0x220E984  → log "Missing action map filter '%s'"
00616A6C mov  rcx,[rbx+0x28]                              ; filter object
00616A76 test bpl,bpl
00616A7B call [rax+0x38]        ; enable path  : filter->Enable(key)
00616AA1 call [rax+0x40]        ; disable path : filter->Disable(key, all)
```

### Filter object: `.?AVCActionFilter@@`, vtable RVA 0x3A53840

| Slot | Offset | RVA | Meaning |
|---|---|---|---|
| 7 | +0x38 | 0x616AAC | Enable(key): `wasOn = IsEnabled()`, then `m_refs[key]++` (`inc dword [node+0x20]`, the std::map at `this+8` keyed by a byte at `node+0x1C`). On an off→on edge it iterates `this+0x28` (`call 0xA10A4C` with the manager at `this+0x18`), calls `manager->vtbl+0x110`, then calls `0x85B248` with event 2 = **listener notification** |
| 8 | +0x40 | 0x616B88 | Disable(key, all): `all` sets every count to 0. Otherwise `m_refs[key]--` if it is > 0. On an on→off edge it calls a global (`[rip+0x4316C3B]`) `vtbl+0x698` / `+0x678` (UNVERIFIED: probably input-system housekeeping) |
| 10 | +0x50 | 0x3F7E3C | IsEnabled(): true if **any** key has a count > 0 |

**Important: in KCD2 the filters are reference-counted per key, not a plain bool.** The second argument of `EnableFilter` is a small key, not "enable". Calling `+0xF0(name, 1)` twice needs two releases. Vanilla CryEngine's `EnableFilter(name, bool)` would silently turn into "add a ref with key 0/1". The disable path is a different vfunc (`+0xF8`). The meaning of the key (a slot/source id; compare the `CreateMultiSlotFilterDescriptor` string) is UNVERIFIED.

## 3. Does the game itself use `no_torch`?

* `WHGame.dll`: the string "no_torch" is **not present** (it lives only in data), so there are no code xrefs. It is only reached through generic filter-by-name paths.
* `defaultProfile.xml`: defines `<actionfilter name="no_torch">` with `toggle_torch` and `xi_toggle_torch`.
* Scripts.pak: **one** use. In `Scripts/Quests/Final/Barbora/kutnohorsko/damaVNesnazich/s_lackem_v_hospode/naliti_lektvaru_do_korbelu_vidi_pri_tom_lacek_hrace.xml` (line 95), `<Constant Name="A" Value="no_torch"/>` goes into a `MakeArray` that feeds a flowgraph `Filters` input (the quest where you pour a potion into Lacek's tankard). The game enables `no_torch` during that quest step.
* Risk: if the game's flow node and the mod use the **same key**, a mod release could cancel the game's ref, or the reverse. `Disable(..., all=true)` would wipe the game's ref as well. Mitigations: use a key the game is unlikely to use, keep every enable/disable strictly balanced (track a `g_noTorchOn` bool), and never pass `all=true`. Because the counting is per key, a balanced mod ref cannot switch off the quest's ref. The key the flow node uses is UNVERIFIED.

## 4. Is it safe to call from the Lua-log hook (main thread)?

* There is no lock, critical section or `lock`-prefixed instruction in `0x6169F0`, `0x616AAC` or `0x616B88`. It is plain main-thread STL map code, the same thread input runs on. OK.
* On an **edge** (off→on or on→off) it fires listener notifications (`0x85B248`, event 2) and releases filtered actions (`manager+0x110` → `0x85B0AC`). If a listener calls back into Lua, that is **Lua re-entrancy inside a Lua call**. UNVERIFIED: the listener list was not resolved. To be safe, set a flag inside the Lua hook and do the actual `EnableFilter` call at the start of the next frame (the existing frame hook), outside any Lua call.
* Call it only on state changes (draw/holster), never every frame. Each call is a ref increment.

## 5. Suggested plugin code (UNTESTED)

```cpp
// WHGame 1.5.6
constexpr uint32_t RVA_VT_CCryAction        = 0x4047350;
constexpr uint32_t RVA_VT_CActionMapManager = 0x4048018;
constexpr size_t   VT_GetIActionMapManager  = 0xE0;   // CCryAction
constexpr size_t   VT_EnableFilter          = 0xF0;   // (this, const char*, uint8 key)
constexpr size_t   VT_ReleaseFilter         = 0xF8;   // (this, const char*, uint8 key, bool all)
constexpr size_t   VT_IsFilterEnabled       = 0x108;  // (this, const char*) -> bool
constexpr uint8_t  DEAGLE_FILTER_KEY        = 1;      // UNVERIFIED: pick a key the game doesn't use

static uintptr_t g_amm = 0;
static uintptr_t FindActionMapManager() {
    if (g_amm) return g_amm;
    for (size_t off = 0; off < 0x200; off += 8) {        // gEnv slot of pGameFramework unknown → RTTI scan
        uintptr_t p = 0, vt = 0;
        if (!ReadPtr(g_env + off, p) || !p || !ReadPtr(p, vt)) continue;
        if (vt - g_base == RVA_VT_CCryAction) {
            uintptr_t amm = 0;
            __try { amm = ((uintptr_t(*)(uintptr_t))VFn(p, VT_GetIActionMapManager))(p); }
            __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
            uintptr_t avt = 0;
            if (amm && ReadPtr(amm, avt) && avt - g_base == RVA_VT_CActionMapManager) return g_amm = amm;
        }
    }
    return 0;
}

static bool g_noTorchOn = false;
static void SetNoTorch(bool on) {             // call from the frame hook, not inside Lua
    if (on == g_noTorchOn) return;            // keep refs balanced
    uintptr_t amm = FindActionMapManager(); if (!amm) return;
    __try {
        if (on) ((void(*)(uintptr_t, const char*, uint8_t))VFn(amm, VT_EnableFilter))(amm, "no_torch", DEAGLE_FILTER_KEY);
        else    ((void(*)(uintptr_t, const char*, uint8_t, bool))VFn(amm, VT_ReleaseFilter))(amm, "no_torch", DEAGLE_FILTER_KEY, false);
        g_noTorchOn = on;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
```
(`VFn(obj, off)` = `*(uintptr_t*)(*(uintptr_t*)obj + off)`. `g_base` = WHGame.dll base.)

## 6. Live tests that would settle the UNVERIFIED items
1. Log which gEnv offset holds the CCryAction vtable (the plugin's gEnv RTTI dump) and confirm that `+0xE0` returns a CActionMapManager.
2. Draw the Deagle, call `SetNoTorch(true)`, and check that `ActionMapManager.IsFilterEnabled("no_torch")` from Lua returns true and that R no longer equips the torch. Holster, call `SetNoTorch(false)`, and check that it returns false and R works again.
3. Call Enable twice with the same key, then Release once. IsFilterEnabled should still be true, which confirms the per-key refcount.
4. Breakpoint or log on `0x6169F0` during the Lacek tankard quest step to learn which key the flowgraph uses.
