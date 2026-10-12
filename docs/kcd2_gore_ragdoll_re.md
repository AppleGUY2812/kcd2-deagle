# KCD2 gore, attachments and ragdoll pushes (static RE, WHGame 1.5.6)

Sources: WHGame.dll strings, RTTI and vtables (scratchpad copy, hash-identical to the game's), `male.cdf` / `male.chr`
(IPL_Characters-part3.pak), Scripts.pak. **Guess** marks anything not verified.

## TL;DR
- **KCD2 has no dismemberment or decapitation.** No `decapitat*`, `dismember*`, `sever*` or `behead*` code or
  strings anywhere in the DLL, scripts or tables. A head pop is ours to build: hide the head and hair / helmet
  attachments, then add blood FX and an optional gore mesh.
- **Bone scaling isn't available.** CryEngine 5 poses are QuatT (rotation + translation, no scale) and there is no
  joint-scale API. Hide attachments instead.
- **Hiding works from Lua:** `entity:HideAttachment(0, name, true, true)`. The head mesh is the skin attachment
  **`head`** (`male.cdf`: `CA_SKIN` "head", plus "body"). Clothing and helmet attachment names are created at runtime
  and Lua can't list them, so a small plugin `listatt` / `hideatt` command helps (section 4).
- **Ragdoll push from Lua:** `entity:AddImpulse(partId, pos, dir, impulse)` on the corpse. For immediate pushes,
  force the ragdoll (`actor:RagDollize`, **guess**) and push next frame. Part ids: section 2.
- **Living flinch push:** there is no per-bone push on a living NPC. Their physics is one capsule, so `AddImpulse` slides
  the whole capsule. Use `hitreact` with strength 6 / hitType 3 (major rows carry `hasRagdoll` = partial ragdoll), plus at
  most a small capsule impulse.

## 1. Attachments and hiding
**Lua binds** (entity; arg names from the DLL):

| bind | args |
|---|---|
| `HideAttachment` | `characterSlot, attachmentName, hide, hideShadow` |
| `HideAllAttachments` | `characterSlot, hide, hideShadow` |
| `HideAttachmentMaster` | `characterSlot, hide`: the base skin. MODLOG uses it to hide Henry's body |
| `ResetAttachment` / `GetAttachmentCGF` | `characterSlot, attachmentName` |
| `CreateBoneAttachment` | `characterSlot, boneName, attachmentName, bCallProjectAndSetDefaultPose` |
| `SetAttachmentObject` | `characterSlot, attachmentName, entityId, slot, flags` |
| `GetBonePos` / `GetBoneDir` / `GetBoneVelocity` / `GetBoneAngularVelocity` | `characterSlot?, boneName` |

`male.cdf` attachments: `CA_SKIN` **head**, **body**; `CA_BONE` eye_left / eye_right, hand_left / hand_right,
weapon_* holsters, scabbard_left, coin_*, dialog_cameras, water_collision; `CA_PROX` cloth proxies. KCD2 adds the
equipped clothing (helmets, coifs, hoods, hair, beard; the CharacterComponent `Clothing` entries) as more skin
attachments at runtime. Their names are **not** in the data files (**guess**: the Clothing name or a slot name). List
them natively once, then hard-code the patterns.

**Native** (if Lua isn't enough):
```
IEntity           vt+0x328  GetCharacter(slot)            (MODLOG)
ICharacterInstance vt+0x40  GetIAttachmentManager()       (MODLOG)
IAttachmentManager (CAttachmentManager vtable RVA 0x4060548):
  +0x40 GetInterfaceByName(const char*)   -> IAttachment*   (index via +0x70, then +0x48)
  +0x48 GetInterfaceByIndex(uint32)       -> IAttachment*   (vector at this+0x78)
  +0x50 GetInterfaceByNameCRC(uint32)
  +0x68 GetAttachmentCount()              -> (end-begin)/8 of this+0x78
  +0x70 GetIndexByName(const char*)       -> int (-1 = none)
IAttachment (CAttachmentSKIN vtable RVA 0x3A65A08; same order for BONE / FACE):
  +0x10 GetName()  -> const char* (this+0x18)
  +0x18 GetNameCRC()
  +0x28 GetType()  -> 1 bone / 2 skin / 3 face (CryEngine CA_* order; SKIN returns 2)
  +0x88 HideAttachment(uint32 hide)   flags this+0x10 |= 0x70000
  +0x90 IsAttachmentHidden()          flags & 0x10000
  +0x98 HideInRecursion(uint32), +0xA8 HideInShadow(uint32)
```

**Head pop recipe:**
1. `HideAttachment(0, "head", true, true)`
2. Hide hair, beard and headgear attachments (names from `listatt`).
3. `Particle.SpawnEffect` blood at `GetBonePos("Head")` (`collisions.combat.hit_passed_bullet_fabric`,
   `cinematics.blood.blood_stab_b`, `collisions.combat.blood_head`).
4. Optional stump: `CreateBoneAttachment(0, "Neck", "deagle_stump")` + `SetAttachmentObject` (or load our own CGF
   into a slot), plus a `cinematics.blood.blood_leaking` emitter on the Neck.

Limbs (forearm / hand): the body is one `body` skin, so there's no per-limb mesh to hide. Without bone scaling, only
a gore mesh plus a hidden glove / gauntlet attachment fakes it. Expect it to look rough.

## 2. Death → ragdoll and pushing parts
- **When the ragdoll exists.** Death plays a `hit_reaction` death row (`HitDeath` fragment). Rows with
  `hasRagdoll=true` go to ragdoll during or at the end of the animation (~1 s); type 1 `Ragdoll` rows go at once.
  So a corpse is **not** ragdolled on the frame `DealDamage` kills it (**guess** from the table and the
  `C_ProceduralClipPlayRagdollHitReaction` / `C_RagdollManager` / `QueueRagdollCreation` names: creation is queued).
- **Force it:** actor bind `RagDollize` (listed with `StandUp`, `Revive`, `PostPhysicalize` among actor binds)
  → `npc.actor:RagDollize()` right after the killing `DealDamage`, then `AddImpulse` on the **next** frame (queued
  creation). **Guess**: verify live that the corpse drops at once and still counts as dead (loot, crime).
- **Part ids:** the ragdoll has a physics part only for bones with a proxy (19, from `male.chr`
  CompiledPhysicalProxies). In CryEngine's character ragdoll the part id is the **joint index** (**guess**, CryEngine
  convention: `pe_params_part.partid = jointId`). Indices in KCD2's male skeleton:

| bone | joint id | bone | joint id |
|---|---|---|---|
| Hips | 1 | Head | 155 |
| Spine | 121 | Spine3 | 145 |
| LeftShoulder | 148 | RightShoulder | 149 |
| LeftArm | 161 | RightArm | 194 |
| LeftForeArm | 163 | RightForeArm | 196 |
| LeftHand | 166 | RightHand | 199 |
| LeftUpLeg | 122 | RightUpLeg | 120 |
| LeftLeg | 232 | RightLeg | 125 |
| LeftFoot | 236 | RightFoot | 129 |

  Non-proxy bones (Neck, Spine1/2/4) belong to the nearest proxy parent: Neck → Spine3, Spine4 → Spine3,
  Spine1/2 → Spine.
- **Most robust:** ray-cast the round against the corpse. A ragdoll returns the hit part id itself, unlike the living
  capsule. Use that `partid` in `AddImpulse(partid, hitPos, dir, impulse)`. Fallback: `AddImpulse(-1, hitPos, dir,
  impulse)` (whole entity at the point).
- Momentum: impulse (N·s) = m × v of the round (.50 AE 19 g × 450 m/s ≈ 8.5 N·s; 9 mm 8 g × 360 ≈ 2.9). That's
  physically tiny for a ~80 kg ragdoll, so scale it for the look (×5–15).

## 3. Living NPCs: physical flinch
- Living physics is one `PE_LIVING` capsule (live probe: rays report mat_flesh, no part). `AddImpulse` on it gives a
  whole-body slide or stumble, never a limb jerk.
- The limb jerk comes from **animation**: `hitreact` (our plugin) with strength 6 or hitType 3 picks major rows. Some
  of these carry `hasRagdoll` (KCD2's partial ragdoll hit reaction, `PlayRagdollHitReaction` procedural clip). Combine:
  hitreact + a small `AddImpulse(-1, pos, dir, ~50–150)` on the living entity for a stagger step (**guess** on the
  magnitude; test).
- `wh_HitReaction_PhysicalHitCoef` (cvar +0x39C) has no code reading it through the getter (see the hit-reaction RE).
  Don't count on it.

## 4. Plugin commands (only for discovery / hiding by pattern)
```cpp
// listatt <entityHex>        -> log every attachment: index, type, name, hidden
// hideatt <entityHex> <substr> <0|1>  -> hide / show every attachment whose name contains substr (case-insensitive)
using GetCharacterFn = uintptr_t(__fastcall*)(uintptr_t entity, int slot);        // IEntity +0x328
using GetAttMgrFn    = uintptr_t(__fastcall*)(uintptr_t chr);                       // ICharacterInstance +0x40
using AttCountFn     = int(__fastcall*)(uintptr_t mgr);                             // IAttachmentManager +0x68
using AttByIdxFn     = uintptr_t(__fastcall*)(uintptr_t mgr, uint32_t i);           // +0x48
using AttNameFn      = const char*(__fastcall*)(uintptr_t att);                     // IAttachment +0x10
using AttTypeFn      = uint32_t(__fastcall*)(uintptr_t att);                        // +0x28
using AttHideFn      = void(__fastcall*)(uintptr_t att, uint32_t hide);             // +0x88
using AttHiddenFn    = uint32_t(__fastcall*)(uintptr_t att);                        // +0x90

static void ForEachAttachment(uint32_t id, const char* match, int hide) {   // SEH-guard the whole body, like SafeGetActor
    uintptr_t es = 0; ReadPtr(g_env + 0xA0, es); if (!es) return;
    uintptr_t ent = SafeGetEntity(es, id); if (!ent) return;
    uintptr_t chr = ((GetCharacterFn)VFn(ent, 0x328))(ent, 0); if (!chr) return;
    uintptr_t mgr = ((GetAttMgrFn)VFn(chr, 0x40))(chr); if (!mgr) return;
    int n = ((AttCountFn)VFn(mgr, 0x68))(mgr);
    for (int i = 0; i < n && i < 256; i++) {
        uintptr_t a = ((AttByIdxFn)VFn(mgr, 0x48))(mgr, i); if (!a) continue;
        const char* nm = ((AttNameFn)VFn(a, 0x10))(a);
        uint32_t ty = ((AttTypeFn)VFn(a, 0x28))(a);
        if (match && nm && StrStrIA(nm, match)) ((AttHideFn)VFn(a, 0x88))(a, hide ? 1 : 0);
        Log("att %d type %u %s hidden %u", i, ty, nm ? nm : "?", ((AttHiddenFn)VFn(a, 0x90))(a));
    }
}
```
No patterns are needed: everything goes through vtables from gEnv. "ragpush" needs no plugin, since Lua `AddImpulse`
covers it.

## Live checks
1. `listatt` on an armored NPC: note the head / hair / beard / helmet names.
2. `HideAttachment(0, "head", true, true)` on a corpse: does the head vanish (and the helmet with it, or separately)?
3. Kill + `npc.actor:RagDollize()` + next-frame `AddImpulse(partFromRay, pos, dir, imp)`: does the hit part fly?
4. Try joint-id part ids (Head 155) when no ray part is available.

## Plugin commands (native/src/gore.cpp)
Sent as `System.LogAlways("[DN] ...")`. Main thread; every engine call is SEH-guarded. Results go to `deagle_native.log`.
- `listatt <entityHex>` logs every attachment of character slot 0: index, type (bone / skin / face), joint id (-1 for
  skins), hidden flag, name.
- `hideatt <entityHex> <substring> <0|1>` hides (1) or shows (0) every attachment whose name contains the substring
  (case-insensitive).
- `hideatt <entityHex> *head* <0|1>` is **head mode**:
  - bone / face attachments on joints at or below the Neck (male.chr ids: Neck 147, Neck1 154, Head 155, HeadEnd 156,
    eyes 157 / 158, Look 159, camera 160)
  - skin attachments (no joint) whose name contains head, hair, beard, mustache / moustache, brow, helm, bascinet,
    kettle, skullcap, coif, hood, hat, cap, bonnet, veil, wimple, wreath, eye, teeth, tongue or lash
  - never names containing "collar" or "body". Mail / padded collars stay to cover the stump.
  - This includes the CA_SKIN `head` itself.
- IAttachment `GetJointID` = vt+0xC8 (CAttachmentBONE: `word [this+0x78]`; CAttachmentSKIN: -1).

The name list is a **guess** until `listatt` shows the runtime names. Hoods that also cover the shoulders
(HoodDown / HoodOn archetypes) disappear whole.
