// Gore: list / hide a character's attachments through the engine's vtables (RE: docs/kcd2_gore_ragdoll_re.md, WHGame 1.5.6).
// gEnv+0xA0 IEntitySystem vt+0x70 GetEntity(id) -> IEntity vt+0x328 GetCharacter(slot) -> ICharacterInstance vt+0x40
// GetIAttachmentManager -> IAttachmentManager vt+0x68 GetAttachmentCount / +0x48 GetInterfaceByIndex ->
// IAttachment vt+0x10 GetName, +0x28 GetType (1 bone, 2 skin, 3 face), +0xC8 GetJointID (-1 for skins),
// +0x88 HideAttachment(uint32), +0x90 IsAttachmentHidden. Every engine call is SEH-guarded.
#include <windows.h>
#include <cstdio>
#include <cstring>
#include "gore.h"
#include "log.h"

namespace {

bool Rd(uintptr_t addr, uintptr_t& out) {
	__try { out = *(uintptr_t*)addr; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { out = 0; return false; }
}
uintptr_t Vf(uintptr_t obj, int off) { uintptr_t vt; return Rd(obj, vt) && Rd(vt + off, vt) ? vt : 0; }

using GetEntityFn = uintptr_t(__fastcall*)(uintptr_t es, uint32_t id);   // IEntitySystem +0x70
using GetCharacterFn = uintptr_t(__fastcall*)(uintptr_t ent, int slot);  // IEntity +0x328
using GetAttMgrFn = uintptr_t(__fastcall*)(uintptr_t chr);               // ICharacterInstance +0x40
using CountFn = int(__fastcall*)(uintptr_t mgr);                         // IAttachmentManager +0x68
using ByIndexFn = uintptr_t(__fastcall*)(uintptr_t mgr, uint32_t i);     // +0x48
using NameFn = const char* (__fastcall*)(uintptr_t att);                 // IAttachment +0x10
using U32Fn = uint32_t(__fastcall*)(uintptr_t att);                      // +0x28 type, +0x90 hidden, +0xC8 joint id
using HideFn = void(__fastcall*)(uintptr_t att, uint32_t hide);          // +0x88

uintptr_t AttachmentManager(uintptr_t env, uint32_t id) {
	__try {
		uintptr_t es = 0; if (!Rd(env + 0xA0, es) || !es) return 0;
		auto getEnt = (GetEntityFn)Vf(es, 0x70); uintptr_t ent = getEnt ? getEnt(es, id) : 0; if (!ent) return 0;
		auto getChr = (GetCharacterFn)Vf(ent, 0x328); uintptr_t chr = getChr ? getChr(ent, 0) : 0; if (!chr) return 0;
		auto getMgr = (GetAttMgrFn)Vf(chr, 0x40); return getMgr ? getMgr(chr) : 0;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

struct AttInfo { uintptr_t att; const char* name; uint32_t type, hidden; int joint; };

int Count(uintptr_t mgr) { __try { auto f = (CountFn)Vf(mgr, 0x68); return f ? f(mgr) : 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; } }

bool Info(uintptr_t mgr, uint32_t i, AttInfo& o) {
	__try {
		auto byIdx = (ByIndexFn)Vf(mgr, 0x48); o.att = byIdx ? byIdx(mgr, i) : 0; if (!o.att) return false;
		auto nm = (NameFn)Vf(o.att, 0x10); o.name = nm ? nm(o.att) : nullptr;
		auto ty = (U32Fn)Vf(o.att, 0x28); o.type = ty ? ty(o.att) : 0;
		auto hd = (U32Fn)Vf(o.att, 0x90); o.hidden = hd ? hd(o.att) : 0;
		auto jt = (U32Fn)Vf(o.att, 0xC8); o.joint = jt ? (int)jt(o.att) : -1;
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool Hide(uintptr_t att, bool hide) {
	__try { auto f = (HideFn)Vf(att, 0x88); if (!f) return false; f(att, hide ? 1 : 0); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Joints at or below Neck in KCD2's male skeleton (male.chr): Neck 147, Neck1 154, Head 155, HeadEnd 156,
// eye_right_bone 157, eye_left_bone 158, Look 159, camera 160.
bool HeadJoint(int j) { return j == 147 || (j >= 154 && j <= 160); }

// Skin attachments carry no joint: match head gear / hair by name. ArmorTypes covering head subparts (armor_type.xml)
// + hair / beard. Collars (CollarMail / CollarPadded) stay: they cover the neck stump.
const char* const kHeadWords[] = { "head", "hair", "beard", "mustache", "moustache", "brow", "helm", "bascinet", "kettle", "skullcap",
	"coif", "hood", "hat", "cap", "bonnet", "veil", "wimple", "wreath", "eye", "teeth", "tongue", "lash" };

bool HeadName(const char* n) {
	if (!n) return false;
	char low[128]; size_t k = 0;
	for (; n[k] && k < sizeof(low) - 1; k++) low[k] = (char)((n[k] >= 'A' && n[k] <= 'Z') ? n[k] + 32 : n[k]);
	low[k] = 0;
	if (strstr(low, "collar") || strstr(low, "body")) return false;
	for (const char* w : kHeadWords) if (strstr(low, w)) return true;
	return false;
}

bool ContainsI(const char* s, const char* sub) {
	if (!s || !sub) return false;
	for (; *s; s++) { const char* a = s; const char* b = sub;
		while (*a && *b && ((*a | 32) == (*b | 32))) { a++; b++; }
		if (!*b) return true; }
	return false;
}

uint32_t ParseId(const char* tok) {
	unsigned long long v = 0;
	if (tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X')) tok += 2;
	sscanf_s(tok, "%llx", &v); return (uint32_t)v;
}

const char* TypeName(uint32_t t) { return t == 1 ? "bone" : t == 2 ? "skin" : t == 3 ? "face" : "?"; }

}  // namespace

bool GoreCommand(const char* cmd, const char* rest, uintptr_t env) {
	bool list = !strcmp(cmd, "listatt"), hide = !strcmp(cmd, "hideatt");
	if (!list && !hide) return false;
	char idTok[24] = {}, match[96] = {}; int on = 1;
	int n = sscanf_s(rest, "%23s %95s %d", idTok, (unsigned)sizeof(idTok), match, (unsigned)sizeof(match), &on);
	if (n < 1 || (hide && n < 2)) { Log("%s: bad command: %s", cmd, rest); return true; }
	uint32_t id = ParseId(idTok);
	if (!env) { Log("%s %08X: no gEnv yet", cmd, id); return true; }
	uintptr_t mgr = AttachmentManager(env, id);
	if (!mgr) { Log("%s %08X: no entity / character / attachment manager", cmd, id); return true; }
	bool headMode = hide && !_stricmp(match, "*head*");
	int count = Count(mgr), changed = 0;
	if (count < 0 || count > 1024) { Log("%s %08X: bad attachment count %d", cmd, id, count); return true; }
	Log("%s %08X: manager %p, %d attachments%s", cmd, id, (void*)mgr, count, headMode ? " (head mode)" : "");
	for (int i = 0; i < count; i++) {
		AttInfo a{};
		if (!Info(mgr, (uint32_t)i, a)) { Log("  #%d unreadable", i); continue; }
		if (list) { Log("  #%d %-4s joint %4d hidden %u %s", i, TypeName(a.type), a.joint, a.hidden ? 1u : 0u, a.name ? a.name : "?"); continue; }
		bool hit = headMode ? ((a.joint >= 0 && HeadJoint(a.joint)) || (a.joint < 0 && HeadName(a.name))) : ContainsI(a.name, match);
		if (!hit) continue;
		bool ok = Hide(a.att, on != 0);
		changed += ok;
		Log("  %s #%d %s joint %d %s%s", on ? "hide" : "show", i, TypeName(a.type), a.joint, a.name ? a.name : "?", ok ? "" : " (FAILED)");
	}
	if (hide) Log("hideatt %08X %s %d: %d changed", id, match, on, changed);
	return true;
}
