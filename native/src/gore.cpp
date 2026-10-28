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

// ---- clothcopy: Henry's worn clothing skins onto another character (docs/ARMSWAP.md; RE notes on the ProLiant
// ~/re/work/clothing_attach/report.md). The same steps as KCD2's own clothing skin job (C_SkinLoaderJob 0x18093dd10):
// CreateAttachment(name, 2 = skin) -> a new CSKINAttachment object from the engine's allocator -> its skin interface
// pointer -> AddBinding(object, ISkin) -> the item's material on all 6 LODs. Every raw address is checked against
// WHGame 1.5.6 (prologue bytes / vtables) before use; every call is SEH-guarded.
constexpr uintptr_t kRvaMalloc = 0x4F75C0, kRvaObjCtor = 0x5ADAC4, kRvaAssignSkin = 0x5ADA88;   // CryModuleMalloc, CSKINAttachment ctor, smart-ptr assign
constexpr uintptr_t kRvaVtAttSkin = 0x3A65A08, kRvaVtSkinObj = 0x3A3F158;                       // CAttachmentSKIN, CSKINAttachment vtables
const uint8_t kSigMalloc[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x83, 0x3D, 0xA3, 0x5D, 0xFA, 0x04, 0x00, 0x48, 0x8B, 0xD9 };
const uint8_t kSigObjCtor[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0xBA, 0x08, 0x00, 0x00, 0x00, 0x48, 0xC7, 0x41, 0x08, 0x00 };
const uint8_t kSigAssign[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x19, 0x48, 0x8B, 0xF9 };

using CreateAttFn = uintptr_t(__fastcall*)(uintptr_t mgr, const char* name, uint32_t type, const char* joint, bool allowDup);  // mgr +0x10
using RemoveAttFn = int(__fastcall*)(uintptr_t mgr, uintptr_t att);                     // mgr +0x18
using PtrFn = uintptr_t(__fastcall*)(uintptr_t self);                                   // att +0xE0 object, +0xE8 skin iface; iface +0x10 ISkin
using SetFlagsFn = void(__fastcall*)(uintptr_t att, uint32_t flags);                    // att +0x38
using AddBindingFn = uint32_t(__fastcall*)(uintptr_t att, uintptr_t obj, uintptr_t skin, uint32_t flags);  // att +0xD0
using GetMtlFn = uintptr_t(__fastcall*)(uintptr_t obj, uint32_t lod);                   // obj +0x80 replacement material
using SetMtlFn = void(__fastcall*)(uintptr_t obj, uintptr_t mtl, uint32_t lod);         // obj +0x78
using StateFn = uint8_t(__fastcall*)(uintptr_t att);                                    // att +0x120: KCD2 stream state (+0x50)
using SetStateFn = void(__fastcall*)(uintptr_t att, uint8_t state);                     // att +0x128 (0x1820f0b00); 6/7 = ready
using LoadSkinFn = void(__fastcall*)(uintptr_t cm, uintptr_t* out, const char* path, uint32_t flags);  // CharacterManager +0x30 (CDF loader 0x1805ae65f)
constexpr uintptr_t kRvaCharMgr = 0x5189BE0;   // global ICharacterManager* (DAT_185189be0)
using MallocFn = uintptr_t(__fastcall*)(size_t);
using CtorFn = uintptr_t(__fastcall*)(uintptr_t mem);
using AssignFn = uintptr_t(__fastcall*)(uintptr_t slot, uintptr_t iface);

uintptr_t g_wh = 0; int g_whOk = -1;   // -1 unchecked, 0 wrong build, 1 WHGame 1.5.6
bool WhOk() {
	if (g_whOk >= 0) return g_whOk == 1;
	g_wh = (uintptr_t)GetModuleHandleA("WHGame.dll");
	__try {
		g_whOk = g_wh && !memcmp((void*)(g_wh + kRvaMalloc), kSigMalloc, sizeof(kSigMalloc)) && !memcmp((void*)(g_wh + kRvaObjCtor), kSigObjCtor, sizeof(kSigObjCtor))
			&& !memcmp((void*)(g_wh + kRvaAssignSkin), kSigAssign, sizeof(kSigAssign)) ? 1 : 0;
	} __except (EXCEPTION_EXECUTE_HANDLER) { g_whOk = 0; }
	return g_whOk == 1;
}

uintptr_t FindAtt(uintptr_t mgr, const char* name) {
	int n = Count(mgr);
	for (int i = 0; i < n && i < 1024; i++) { AttInfo a{}; if (Info(mgr, (uint32_t)i, a) && a.name && !_stricmp(a.name, name)) return a.att; }
	return 0;
}

bool RemoveAtt(uintptr_t mgr, uintptr_t att) {
	__try { auto f = (RemoveAttFn)Vf(mgr, 0x18); if (!f) return false; f(mgr, att); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Copies src's skin attachment onto dstMgr as dstName. Returns a short reason on failure, nullptr when bound.
uint8_t State(uintptr_t att) { __try { auto f = (StateFn)Vf(att, 0x120); return f ? f(att) : 0xFF; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0xFF; } }

// Logs the qwords of two CAttachmentSKIN objects (0x198 bytes) that differ, to see what the game sets on its own
// clothing attachments that a copy lacks. Pointers differ anyway; small values and flags are the interesting ones.
void DiffAtt(const char* tag, uintptr_t a, uintptr_t b) {
	char line[1024]; int len = sprintf_s(line, "clothcopy diff %s (own vs copy):", tag), shown = 0;
	for (int off = 0; off < 0x198 && len < 900; off += 8) {
		uintptr_t x = 0, y = 0; if (!Rd(a + off, x) || !Rd(b + off, y) || x == y) continue;
		len += sprintf_s(line + len, sizeof(line) - len, " +%X %llX/%llX", off, (unsigned long long)x, (unsigned long long)y); shown++;
	}
	Log("%s%s", line, shown ? "" : " none");
}

const char* CopySkin(uintptr_t src, uintptr_t dstMgr, const char* dstName, uint32_t& outFlags, int& outMtls) {
	__try {
		uintptr_t obj = ((PtrFn)Vf(src, 0xE0))(src); if (!obj) return "source has no object";
		if (((U32Fn)Vf(obj, 0x08))(obj) != 3) return "source object is not a skin mesh (cloth sim / other)";
		uintptr_t iface = ((PtrFn)Vf(src, 0xE8))(src); if (!iface) return "source has no skin interface";
		uintptr_t skin = ((PtrFn)Vf(iface, 0x10))(iface); if (!skin) return "source has no ISkin";
		// Henry's live skin is a streamed clothing mesh that draws only through his own attachment's stream data (test
		// 2026-10-26: bound, never drawn; his attachment holds per-LOD entries the copy lacked). Load the same .skin file
		// fresh, as the CDF loader does for every viewmodel skin (those draw).
		const char* path = (const char*)((PtrFn)Vf(skin, 0x20))(skin);   // CSkin vt 0x18406c9f0 +0x20 -> +0x28
		char pbuf[260] = {}; size_t pl = 0;
		if (path) for (; pl < sizeof(pbuf) - 1 && path[pl]; pl++) pbuf[pl] = path[pl];
		if (pl < 6 || _stricmp(pbuf + pl - 5, ".skin")) return "source skin path unreadable (not a .skin)";
		uintptr_t cm = 0; if (!Rd(g_wh + kRvaCharMgr, cm) || !cm) return "no character manager";
		uintptr_t fresh = 0; ((LoadSkinFn)Vf(cm, 0x30))(cm, &fresh, pbuf, 0);   // the reference stays with the copy
		Log("clothcopy %s: %s -> %s", dstName, pbuf, fresh ? "loaded" : "LOAD FAILED");
		if (!fresh) return "LoadModelSKIN failed";
		skin = fresh;
		uintptr_t mtl[6] = {}; outMtls = 0;
		for (uint32_t l = 0; l < 6; l++) { mtl[l] = ((GetMtlFn)Vf(obj, 0x80))(obj, l); outMtls += mtl[l] != 0; }
		outFlags = ((U32Fn)Vf(src, 0x40))(src);
		if (uintptr_t old = FindAtt(dstMgr, dstName)) RemoveAtt(dstMgr, old);
		uintptr_t att = ((CreateAttFn)Vf(dstMgr, 0x10))(dstMgr, dstName, 2, nullptr, false); if (!att) return "CreateAttachment failed";
		uintptr_t vt = 0; Rd(att, vt);
		if (vt != g_wh + kRvaVtAttSkin) { RemoveAtt(dstMgr, att); return "new attachment has an unexpected vtable"; }
		((SetFlagsFn)Vf(att, 0x38))(att, outFlags & ~1u);   // bit 0 = hidden (Henry's are hidden in first person)
		uintptr_t mem = ((MallocFn)(g_wh + kRvaMalloc))(0x40); if (!mem) { RemoveAtt(dstMgr, att); return "engine allocator failed"; }
		uintptr_t o2 = ((CtorFn)(g_wh + kRvaObjCtor))(mem);
		Rd(o2, vt); if (vt != g_wh + kRvaVtSkinObj) { RemoveAtt(dstMgr, att); return "new object has an unexpected vtable"; }
		((AssignFn)(g_wh + kRvaAssignSkin))(o2 + 8, ((PtrFn)Vf(att, 0xE8))(att));
		if (!((AddBindingFn)Vf(att, 0xD0))(att, o2, skin, 0x800)) { RemoveAtt(dstMgr, att); return "AddBinding refused"; }
		for (uint32_t l = 0; l < 6; l++) if (mtl[l]) ((SetMtlFn)Vf(o2, 0x78))(o2, mtl[l], l);
		Hide(att, false);
		// Stream state, log only: Henry's and the NPC's clothing sit at 5; forcing 7 on the copy (3f53402) changed nothing.
		Log("clothcopy %s: stream state source %u, copy %u", dstName, State(src), State(att));
		return nullptr;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return "engine call faulted (caught)"; }
}

bool ClothCopy(const char* rest, uintptr_t env) {
	char dstTok[24] = {}, parts[96] = {}, srcTok[24] = {};
	int n = sscanf_s(rest, "%23s %95s %23s", dstTok, (unsigned)sizeof(dstTok), parts, (unsigned)sizeof(parts), srcTok, (unsigned)sizeof(srcTok));
	if (n < 1) { Log("clothcopy: usage clothcopy <dst entityHex> [arms,hands|off] [src entityHex, default 7777]"); return true; }
	if (!WhOk()) { Log("clothcopy: not WHGame 1.5.6 (signature mismatch), refusing"); return true; }
	uint32_t dst = ParseId(dstTok), src = n >= 3 ? ParseId(srcTok) : 0x7777;
	uintptr_t dmgr = env ? AttachmentManager(env, dst) : 0, smgr = env ? AttachmentManager(env, src) : 0;
	if (!dmgr || !smgr) { Log("clothcopy %08X <- %08X: no attachment manager (dst %p, src %p)", dst, src, (void*)dmgr, (void*)smgr); return true; }
	if (n < 2) strcpy_s(parts, "arms,hands");
	if (!_stricmp(parts, "off")) {
		int removed = 0;
		static const char* const kParts[] = { "arms", "hands", "torso", "legs", "feet", "head", "neck", "waist" };
		for (const char* p : kParts) {
			char nm[64]; sprintf_s(nm, "dn_clothing_%s", p);
			if (uintptr_t a = FindAtt(dmgr, nm)) removed += RemoveAtt(dmgr, a);
		}
		Log("clothcopy %08X off: %d removed", dst, removed); return true;
	}
	char buf[96]; strcpy_s(buf, parts); char* ctx = nullptr;
	for (char* p = strtok_s(buf, ",", &ctx); p; p = strtok_s(nullptr, ",", &ctx)) {
		char sname[64], dname[64]; sprintf_s(sname, "clothing_%s", p); sprintf_s(dname, "dn_clothing_%s", p);
		uintptr_t sa = FindAtt(smgr, sname);
		if (!sa) { Log("clothcopy %08X <- %08X %s: source has no %s", dst, src, p, sname); continue; }
		uint32_t fl = 0; int mt = 0; LARGE_INTEGER t0, t1, fq; QueryPerformanceCounter(&t0);
		const char* why = CopySkin(sa, dmgr, dname, fl, mt);
		QueryPerformanceCounter(&t1); QueryPerformanceFrequency(&fq);
		Log("clothcopy %08X <- %08X %s: %s (flags %08X, %d LOD materials, %.3f ms)", dst, src, p, why ? why : "bound", fl, mt,
			(t1.QuadPart - t0.QuadPart) * 1000.0 / fq.QuadPart);
		uintptr_t own = FindAtt(dmgr, sname), cp = FindAtt(dmgr, dname);
		if (own && cp) DiffAtt(p, own, cp);
		if (own) { uint32_t of = 0; __try { of = ((U32Fn)Vf(own, 0x40))(own); } __except (EXCEPTION_EXECUTE_HANDLER) {} Log("clothcopy %s: the NPC's own %s flags %08X state %u", p, sname, of, State(own)); }
	}
	// The manager keeps a sorted copy of its attachment list (+0xB8 from +0x78) and registers the skins from that copy;
	// CreateAttachment only marks it dirty (+0xFC). The CDF loader rebuilds it at its end (0x180aba494); vt +0x138
	// (0x1813e03d8) is the manager's own rebuild + "attachments changed" on the character (0x40000). Without it the
	// copies were bound but never on the drawn list (tests 193 / 197 / 199).
	__try { ((PtrFn)Vf(dmgr, 0x138))(dmgr); Log("clothcopy %08X: attachment list rebuilt", dst); }
	__except (EXCEPTION_EXECUTE_HANDLER) { Log("clothcopy %08X: attachment list rebuild faulted (caught)", dst); }
	static const char* const kShown[] = { "arms", "hands" };
	for (const char* p : kShown) {
		char dname[64]; sprintf_s(dname, "dn_clothing_%s", p);
		if (uintptr_t cp = FindAtt(dmgr, dname)) Log("clothcopy %s after rebuild: state %u", dname, State(cp));
	}
	return true;
}

// clothmtl <dst entityHex> <dstAtt>:<part>[,...] [src entityHex, default 7777]: puts the material of Henry's worn
// clothing_<part> (all 6 LODs) on dst's attachment <dstAtt>. KCD2 clothing materials use the CLOTHING_SYSTEM shader:
// the .mtl file alone has empty feature slots and draws grey; the clothing system fills the slots (colours, leather,
// metal) on the live material of the worn item, so that live material is the one to share. dstAtt is an attachment a
// CDF made (the CDF loader draws KCD2 clothing skins: MVP 2026-10-27), not a clothcopy copy.
bool ClothMtl(const char* rest, uintptr_t env) {
	char dstTok[24] = {}, pairs[160] = {}, srcTok[24] = {};
	int n = sscanf_s(rest, "%23s %159s %23s", dstTok, (unsigned)sizeof(dstTok), pairs, (unsigned)sizeof(pairs), srcTok, (unsigned)sizeof(srcTok));
	if (n < 2) { Log("clothmtl: usage clothmtl <dst entityHex> <dstAtt>:<part>[,...] [src entityHex]"); return true; }
	uint32_t dst = ParseId(dstTok), src = n >= 3 ? ParseId(srcTok) : 0x7777;
	uintptr_t dmgr = env ? AttachmentManager(env, dst) : 0, smgr = env ? AttachmentManager(env, src) : 0;
	if (!dmgr || !smgr) { Log("clothmtl %08X <- %08X: no attachment manager", dst, src); return true; }
	char buf[160]; strcpy_s(buf, pairs); char* ctx = nullptr;
	for (char* p = strtok_s(buf, ",", &ctx); p; p = strtok_s(nullptr, ",", &ctx)) {
		char* colon = strchr(p, ':'); if (!colon) { Log("clothmtl: '%s' is not <dstAtt>:<part>", p); continue; }
		*colon = 0; const char* part = colon + 1;
		char sname[64]; sprintf_s(sname, "clothing_%s", part);
		uintptr_t sa = FindAtt(smgr, sname), da = FindAtt(dmgr, p);
		if (!sa || !da) { Log("clothmtl %s <- %s: %s", p, sname, !sa ? "Henry has no such attachment" : "no such attachment on dst"); continue; }
		int set = 0;
		__try {
			uintptr_t so = ((PtrFn)Vf(sa, 0xE0))(sa), dobj = ((PtrFn)Vf(da, 0xE0))(da);
			if (!so || !dobj) { Log("clothmtl %s: no attachment object (src %p, dst %p)", p, (void*)so, (void*)dobj); continue; }
			for (uint32_t l = 0; l < 6; l++)
				if (uintptr_t m = ((GetMtlFn)Vf(so, 0x80))(so, l)) { ((SetMtlFn)Vf(dobj, 0x78))(dobj, m, l); set++; }
		} __except (EXCEPTION_EXECUTE_HANDLER) { Log("clothmtl %s: engine call faulted (caught)", p); continue; }
		Log("clothmtl %08X %s <- %08X %s: %d LOD materials", dst, p, src, sname, set);
	}
	return true;
}

// clothinst <dst entityHex> <dstAtt>:<part>[,...] [src entityHex, default 7777]: dst's attachment <dstAtt> draws with
// Henry's live clothing data for clothing_<part>. The colours, dirt, scratches and blood of a KCD2 clothing part are
// not in its material: the item's runtime data fills a wh::engine3d::C_MaterialInstance (feature + instance ids into
// the shader's FeatureBuffer / MaterialInstanceBuffer), held per attachment at CAttachmentSKIN +0x38 (smart pointer,
// assigned through the skin interface att+8 vt +0x60 = 0x180ab965c, which takes over one reference). RE: ProLiant
// ~/re/work/clothing_colour/report.md; the NPC diff (+0x38 set on his clothing, 0 on the copy) agrees.
constexpr uintptr_t kRvaVtMatInst = 0x3B2DCD8;   // wh::engine3d::C_MaterialInstance vtable
using SetInstFn = void(__fastcall*)(uintptr_t iface, uintptr_t* inst);   // skin iface vt +0x60
bool ClothInst(const char* rest, uintptr_t env) {
	char dstTok[24] = {}, pairs[160] = {}, srcTok[24] = {};
	int n = sscanf_s(rest, "%23s %159s %23s", dstTok, (unsigned)sizeof(dstTok), pairs, (unsigned)sizeof(pairs), srcTok, (unsigned)sizeof(srcTok));
	if (n < 2) { Log("clothinst: usage clothinst <dst entityHex> <dstAtt>:<part>[,...] [src entityHex]"); return true; }
	if (!WhOk()) { Log("clothinst: not WHGame 1.5.6 (signature mismatch), refusing"); return true; }
	uint32_t dst = ParseId(dstTok), src = n >= 3 ? ParseId(srcTok) : 0x7777;
	uintptr_t dmgr = env ? AttachmentManager(env, dst) : 0, smgr = env ? AttachmentManager(env, src) : 0;
	if (!dmgr || !smgr) { Log("clothinst %08X <- %08X: no attachment manager", dst, src); return true; }
	char buf[160]; strcpy_s(buf, pairs); char* ctx = nullptr;
	for (char* p = strtok_s(buf, ",", &ctx); p; p = strtok_s(nullptr, ",", &ctx)) {
		char* colon = strchr(p, ':'); if (!colon) { Log("clothinst: '%s' is not <dstAtt>:<part>", p); continue; }
		*colon = 0; const char* part = colon + 1;
		char sname[64]; sprintf_s(sname, "clothing_%s", part);
		uintptr_t sa = FindAtt(smgr, sname), da = FindAtt(dmgr, p);
		if (!sa || !da) { Log("clothinst %s <- %s: %s", p, sname, !sa ? "Henry has no such attachment" : "no such attachment on dst"); continue; }
		uintptr_t vs = 0, vd = 0, mi = 0, mvt = 0, before = 0, after = 0;
		Rd(sa, vs); Rd(da, vd);
		if (vs != g_wh + kRvaVtAttSkin || vd != g_wh + kRvaVtAttSkin) { Log("clothinst %s: not skin attachments (vt %llX / %llX)", p, (unsigned long long)vs, (unsigned long long)vd); continue; }
		if (!Rd(sa + 0x38, mi) || !mi || !Rd(mi, mvt) || mvt != g_wh + kRvaVtMatInst) {
			Log("clothinst %s: Henry's %s has no material instance at +0x38 (%llX, vt %llX)", p, sname, (unsigned long long)mi, (unsigned long long)mvt); continue;
		}
		Rd(da + 0x38, before);
		__try {
			uintptr_t iface = ((PtrFn)Vf(da, 0xE8))(da);
			if (iface != da + 8) { Log("clothinst %s: skin interface not at att+8 (%llX)", p, (unsigned long long)(iface - da)); continue; }
			((PtrFn)Vf(mi, 0x08))(mi);                  // AddRef: the setter takes over this reference
			uintptr_t sp = mi;
			((SetInstFn)Vf(iface, 0x60))(iface, &sp);
		} __except (EXCEPTION_EXECUTE_HANDLER) { Log("clothinst %s: engine call faulted (caught)", p); continue; }
		Rd(da + 0x38, after);
		Log("clothinst %08X %s <- %08X %s: instance %llX (was %llX, now %llX)%s", dst, p, src, sname, (unsigned long long)mi,
			(unsigned long long)before, (unsigned long long)after, after == mi ? "" : " NOT SET");
	}
	return true;
}

}  // namespace

bool GoreCommand(const char* cmd, const char* rest, uintptr_t env) {
	if (!strcmp(cmd, "clothinst")) return ClothInst(rest, env);
	if (!strcmp(cmd, "clothcopy")) return ClothCopy(rest, env);
	if (!strcmp(cmd, "clothmtl")) return ClothMtl(rest, env);
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
