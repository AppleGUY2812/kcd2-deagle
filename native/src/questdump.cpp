// Quest graph dump: see questdump.h.
#include "questdump.h"
#include "log.h"
#include "MinHook.h"
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

uintptr_t g_base = 0;
long long g_freq = 1;
inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }

// ---- game code (WHGame.dll 1.5.6), first bytes from the installed DLL (re_quest_dump_binary.md part 1, rechecked)
struct Code { const char* name; uintptr_t rva; int n; unsigned char bytes[16]; };
enum {
	kBuild,        // bool FUN_181e39280(deser*, path, instance*, int, bool): one project (hooked)
	kConvert,      // bool variant::convert(variant* src, type* target, variant* out)
	kStrType,      // type* type::get<std::string>(type* ret)
	kMeta,         // bool FUN_1804fbb98(property**, variant* key, bool* default) (questfast caches it)
	kExtract,      // variant* extract_wrapped_value(variant* src, variant* out)
	kSeqView,      // view* sequential view(variant*, view*): +0x10 container, +0x20 size, +0xa0 get
	kSeqSize,      // size_t view size(view*)
	kSeqGet,       // variant* view get(view*, variant* out, size_t i): a reference_wrapper
	kAssocView,    // view* associative view(variant*, view*): +0x08 key type, +0x10 value type, +0x18 container
	kStringLike,   // bool FUN_1804f9140(type*): the deserializer reads this type from an attribute string
	kUnwrap,       // instance* get_wrapped_instance(instance*, instance* ret)
	kInstance,     // instance* instance(variant*, instance* ret) (policy op 10)
	kVarType,      // type* variant::get_type(variant*, type* ret)
	kEmptyPolicy,  // the empty variant's policy
	kKeyPolicy,    // E_PropertyMetadata key policy
	kStrPolicy,    // std::string variant policy (heap stored: d[0] = std::string*)
	kGetPtr,       // RTTR_ENABLE get_ptr (mov rax, rcx; ret): vt slot 6 of nodes, ports and projects
	kCodeN
};
const Code kCode[kCodeN] = {
	{ "project build", 0x1e39280, 16, { 0x48,0x83,0xec,0x48,0x48,0x83,0xb9,0xb0,0x01,0x00,0x00,0x00,0x48,0x8b,0x05,0xed } },
	{ "variant convert", 0x4f9640, 16, { 0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8b,0xec,0x48,0x81 } },
	{ "type<std::string>", 0x4f9f08, 16, { 0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x8b,0x15,0x84,0xe6,0xbc,0x04 } },
	{ "metadata bool", 0x4fbb98, 16, { 0x40,0x53,0x48,0x83,0xec,0x50,0x48,0x8b,0x05,0x1b,0x34,0x40,0x04,0x48,0x33,0xc4 } },
	{ "extract wrapped", 0x69f0d4, 16, { 0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8d,0x05,0x37,0xcb,0xe5,0xff,0x48,0x8b,0xda } },
	{ "sequential view", 0x6abf94, 16, { 0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x48,0x8b,0xfa } },
	{ "view size", 0x23cbbd0, 16, { 0x48,0x8b,0x41,0x20,0x48,0x8b,0x49,0x10,0x48,0xff,0xe0,0xcc,0x48,0x83,0xec,0x38 } },
	{ "view get", 0x4f7230, 16, { 0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x81,0xa0,0x00,0x00,0x00,0x48,0x8b,0xda } },
	{ "associative view", 0x787ce8, 16, { 0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x48,0x8b,0xfa } },
	{ "string-like type", 0x4f9140, 16, { 0x40,0x53,0x48,0x83,0xec,0x20,0x8b,0x15,0x50,0xf4,0xbc,0x04,0x48,0x8b,0xd9,0x65 } },
	{ "wrapped instance", 0x69e9dc, 16, { 0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xda,0x48,0x8b,0xf9 } },
	{ "instance of variant", 0x69d270, 16, { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7c,0x24,0x18,0x41 } },
	{ "variant type", 0x4f9cec, 16, { 0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x44,0x8b,0x05,0x9f,0xe8,0xbc } },
	{ "empty policy", 0x4fbc18, 16, { 0x40,0x53,0x48,0x83,0xec,0x20,0x0f,0xb6,0xd1,0x49,0x8b,0xd8,0x83,0xfa,0x0b,0x77 } },
	{ "metadata key policy", 0x4fb9e0, 16, { 0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x70,0x18,0x48,0x89,0x78,0x20,0x55 } },
	{ "std::string policy", 0x4f9d78, 16, { 0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x70,0x18,0x48,0x89,0x78,0x20,0x55 } },
	{ "get_ptr", 0x5f5da0, 4, { 0x48,0x8b,0xc1,0xc3 } },
};
inline uintptr_t A(int i) { return g_base + kCode[i].rva; }

// port link storage, picked by the identity of vt slots 25 / 26 (both are "add a link" functions, disassembled)
constexpr uintptr_t kOutTrigAdd = 0x693758;                      // s25: vector<WeakId> at +0x30 (OutputTrigger)
constexpr uintptr_t kIfTrigAdd = 0xd2ae80, kIfTrigAdd2 = 0xd2ae1c; // s25 / s26: vectors at +0x38 and +0x50 (Interface/StaticTrigger)
constexpr uintptr_t kIfDataAdd = 0xd2aff8, kIfDataSet = 0xd2af04;  // s25 / s26: vector at +0x40, WeakId at +0x34 (Interface/Static data, const, asset)
constexpr uintptr_t kInDataSet = 0xd2af28;                       // s26: WeakId at +0x30 (InputData)

// ---- rttr ABI (customised rttr master, statically linked)
using TD = uint8_t;   // type_data: +0 raw type, +8 wrapped type, +0x38/+0x40 name, +0x48 sizeof, +0xb8 class data getter,
                      // +0xc0 valid, +0xc4 traits
struct Var { uint64_t d[2]; uintptr_t pol; };              // rttr::variant
struct Inst { TD* t; TD* wt; void* data; void* wdata; };   // rttr::instance
struct DInfo { void* ptr; TD* t; };                         // derived_info
constexpr uint32_t kClass = 0x1, kEnum = 0x2, kPointer = 0x8, kArith = 0x10, kAssoc = 0x100, kSeq = 0x200;

using PolFn = uint8_t(__fastcall*)(uint8_t, Var*, void*);
using ConvertFn = uint8_t(__fastcall*)(Var*, TD**, Var*);
using TypeRetFn = TD**(__fastcall*)(TD**);
using MetaFn = uint8_t(__fastcall*)(void**, Var*, uint8_t*);
using ExtractFn = Var*(__fastcall*)(Var*, Var*);
using ViewFn = void*(__fastcall*)(Var*, void*);
using SeqSizeFn = size_t(__fastcall*)(void*);
using SeqGetFn = Var*(__fastcall*)(void*, Var*, size_t);
using StringLikeFn = uint8_t(__fastcall*)(TD**);
using UnwrapFn = Inst*(__fastcall*)(Inst*, Inst*);
using InstFn = Inst*(__fastcall*)(Var*, Inst*);
using VarTypeFn = TD**(__fastcall*)(Var*, TD**);
using ClassDataFn = uint8_t*(__fastcall*)();
using DerivedFn = DInfo*(__fastcall*)(DInfo*, void*);      // class_data+0: derived_info(ret, object)
using ObjDerivedFn = DInfo*(__fastcall*)(void*, DInfo*);   // object vt+0x38: get_derived_info(this, ret)
using PwIntFn = int(__fastcall*)(void*);
using PwBoolFn = uint8_t(__fastcall*)(void*);
using PwTypeFn = TD**(__fastcall*)(void*, TD**);
using PwGetFn = Var*(__fastcall*)(void*, Var*, Inst*);

// ---- guarded game reads and calls (no C++ objects in these: MSVC C2712)
std::atomic<long long> g_destroyErr{0};
bool GRead(const void* p, void* out, size_t n) {
	__try { memcpy(out, p, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template <class T> bool GR(const void* p, T& out) { return GRead(p, &out, sizeof(T)); }
bool GCStr(const char* p, char* out, size_t cap) {
	__try {
		size_t i = 0;
		for (; i + 1 < cap && p[i]; i++) out[i] = p[i];
		out[i] = 0;
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) { out[0] = 0; return false; }
}
Var EmptyVar() { Var v; v.d[0] = v.d[1] = 0; v.pol = A(kEmptyPolicy); return v; }
bool IsEmpty(const Var& v) { return !v.pol || v.pol == A(kEmptyPolicy); }
void GDestroy(Var* v) {
	if (v->pol && v->pol != A(kEmptyPolicy)) {
		__try { ((PolFn)v->pol)(0, v, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) { g_destroyErr++; }
	}
	v->d[0] = v->d[1] = 0;
	v->pol = A(kEmptyPolicy);
}
bool GVarType(Var* v, TD** t) {
	__try { *t = nullptr; ((VarTypeFn)A(kVarType))(v, t); return *t != nullptr; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool GInstance(Var* v, Inst* in) {
	__try { ((InstFn)A(kInstance))(v, in); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool GUnwrap(Inst* in, Inst* out) {
	__try { ((UnwrapFn)A(kUnwrap))(in, out); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool GStrType(TD** t) {
	__try { *t = nullptr; ((TypeRetFn)A(kStrType))(t); return *t != nullptr; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
struct TRaw { TD* raw; TD* wrapped; uint8_t wrappedValid, valid; uint32_t traits; size_t size; const char* name; size_t nameLen; };
bool GTypeRaw(TD* t, TRaw* r) {
	__try {
		r->raw = *(TD**)t;
		r->wrapped = *(TD**)(t + 8);
		r->wrappedValid = r->wrapped ? r->wrapped[0xc0] : 0;
		r->valid = t[0xc0];
		r->traits = *(uint32_t*)(t + 0xc4);
		r->size = *(size_t*)(t + 0x48);
		r->name = *(const char**)(t + 0x38);
		r->nameLen = *(size_t*)(t + 0x40);
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool GClassVec(TD* raw, size_t off, void*** b, size_t* n) {   // class_data vectors: +0x08 base types, +0x50 properties
	__try {
		uint8_t* cd = (*(ClassDataFn*)(raw + 0xb8))();
		if (!cd) return false;
		void** x = *(void***)(cd + off);
		void** e = *(void***)(cd + off + 8);
		if (e < x || e - x > 100000) return false;
		*b = x;
		*n = (size_t)(e - x);
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool GDerived(TD* raw, void* obj, DInfo* di) {   // what the deserializer does (FUN_181e25520)
	__try {
		uint8_t* cd = (*(ClassDataFn*)(raw + 0xb8))();
		di->ptr = nullptr;
		di->t = nullptr;
		(*(DerivedFn*)cd)(di, obj);
		return di->t != nullptr;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool GObjDerived(void* obj, uintptr_t getPtr, DInfo* di) {   // a raw pointer to an RTTR_ENABLE object
	__try {
		void** vt = *(void***)obj;
		if ((uintptr_t)vt[6] != getPtr) return false;   // slots 5-7 are not get_type / get_ptr / get_derived_info
		di->ptr = nullptr;
		di->t = nullptr;
		((ObjDerivedFn)vt[7])(obj, di);
		return di->ptr && di->t;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// mode 1: what the deserializer may write (FUN_181e25520: public, not read-only, not static, metadata 0 not false)
bool GPropFilter(void* pw, uint8_t* ok) {
	__try {
		void** vt = *(void***)pw;
		*ok = 0;
		if (((PwIntFn)vt[3])(pw) != 0) return true;
		if (((PwBoolFn)vt[4])(pw) || ((PwBoolFn)vt[5])(pw)) return true;
		Var key;
		key.d[0] = 0;   // E_PropertyMetadata 0
		key.d[1] = 0;
		key.pol = A(kKeyPolicy);
		uint8_t dflt = 1;
		void* p = pw;
		*ok = ((MetaFn)A(kMeta))(&p, &key, &dflt) ? 1 : 0;
		((PolFn)key.pol)(0, &key, nullptr);
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool GPropName(void* pw, const char** n, size_t* len) {
	__try {
		*n = *(const char**)((uint8_t*)pw + 8);
		*len = *(size_t*)((uint8_t*)pw + 0x10);
		return *n && *len < 4096;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool GPropType(void* pw, TD** t) {
	__try { *t = nullptr; ((PwTypeFn)(*(void***)pw)[6])(pw, t); return *t != nullptr; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool GGetValue(void* pw, Var* out, Inst* in) {
	__try { ((PwGetFn)(*(void***)pw)[9])(pw, out, in); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool GStringLike(TD* t, uint8_t* r) {
	__try { TD* x = t; *r = ((StringLikeFn)A(kStringLike))(&x) ? 1 : 0; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool GConvert(Var* v, TD* target, Var* out) {
	__try { TD* x = target; return ((ConvertFn)A(kConvert))(v, &x, out) != 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool GSeqView(Var* v, void* view) {
	__try { ((ViewFn)A(kSeqView))(v, view); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool GAssocView(Var* v, void* view) {
	__try { ((ViewFn)A(kAssocView))(v, view); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool GSeqSize(void* view, size_t* n) {
	__try { *n = ((SeqSizeFn)A(kSeqSize))(view); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool GSeqGet(void* view, size_t i, Var* out) {
	__try { ((SeqGetFn)A(kSeqGet))(view, out, i); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool GExtract(Var* src, Var* out) {
	__try { ((ExtractFn)A(kExtract))(src, out); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool GStdStr(const void* s, const char** p, size_t* n) {   // MSVC std::string: buffer or pointer, size +0x10, capacity +0x18
	__try {
		const size_t size = *(const size_t*)((const uint8_t*)s + 0x10), cap = *(const size_t*)((const uint8_t*)s + 0x18);
		if (size > cap || size > (1u << 24)) return false;
		*p = cap > 15 ? *(const char* const*)s : (const char*)s;
		*n = size;
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool GCryStr(const void* s, const char** p, size_t* n) {   // CryStringT<char>: char* after {refs, length, alloc}
	__try {
		const char* c = *(const char* const*)s;
		if (!c) { *p = ""; *n = 0; return true; }
		const int len = *(const int*)(c - 8);
		if (len < 0 || len > (1 << 24)) return false;
		*p = c;
		*n = (size_t)len;
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// MSVC std::map: {head, size}; node: left +0, parent +8, right +0x10, isnil +0x19, value +0x20
bool GTreeFirst(const void* map, uint8_t** head, uint8_t** first, size_t* size) {
	__try {
		*head = *(uint8_t* const*)map;
		*size = *(const size_t*)((const uint8_t*)map + 8);
		*first = *(uint8_t**)*head;
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool GTreeNext(uint8_t* node, uint8_t** next) {
	__try {
		uint8_t* r = *(uint8_t**)(node + 0x10);
		if (!r[0x19]) {
			while (!(*(uint8_t**)r)[0x19]) r = *(uint8_t**)r;
			*next = r;
			return true;
		}
		uint8_t* p = *(uint8_t**)(node + 8);
		while (!p[0x19] && node == *(uint8_t**)(p + 0x10)) { node = p; p = *(uint8_t**)(p + 8); }
		*next = p;
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool GCopyInst(const uint64_t* p, uint64_t* out) {
	__try { memcpy(out, p, 32); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool ReadStr(const char* p, size_t n, std::string& out) {
	out.resize(n);
	if (n && !GRead(p, &out[0], n)) { out.clear(); return false; }
	return true;
}
bool CryAt(const void* obj, size_t off, std::string& out) {
	const char* p; size_t n;
	return GCryStr((const uint8_t*)obj + off, &p, &n) && ReadStr(p, n, out);
}
bool StdAt(const void* s, std::string& out) {
	const char* p; size_t n;
	return GStdStr(s, &p, &n) && ReadStr(p, n, out);
}

// ---- text
std::string Quote(const std::string& in) {
	std::string s = "\"";
	for (unsigned char ch : in) {
		if (ch == '"' || ch == '\\') { s += '\\'; s += (char)ch; }
		else if (ch < 0x20 || ch == 0x7f) { char b[8]; snprintf(b, sizeof b, "\\x%02x", ch); s += b; }
		else s += (char)ch;
	}
	return s + "\"";
}
std::string PathPart(const std::string& in) {   // a name inside a path: one token, no braces or quotes
	std::string s;
	for (unsigned char ch : in) {
		if (ch <= 0x20 || ch == 0x7f || ch == '{' || ch == '}' || ch == '"' || ch == '\\' || ch == '#') {
			char b[8]; snprintf(b, sizeof b, "\\x%02x", ch); s += b;
		} else s += (char)ch;
	}
	return s;
}
std::string Short(const std::string& n) {
	for (const char* p : { "class ", "struct ", "enum " })
		if (!n.compare(0, strlen(p), p)) return n.substr(strlen(p));
	return n;
}
std::string Hex(uint64_t h) { char b[20]; snprintf(b, sizeof b, "%016llx", (unsigned long long)h); return b; }
constexpr uint64_t kFnv0 = 0xcbf29ce484222325ull;
uint64_t Fnv(uint64_t h, const std::string& s) {
	for (unsigned char ch : s) { h ^= ch; h *= 0x100000001b3ull; }
	h ^= '\n';
	return h * 0x100000001b3ull;
}

// ---- type and property caches (types and property wrappers are static in the game)
enum Kind { kOther = 0, kNode = 1, kPort = 2, kProject = 3 };
const char* const kStdString = "class std::basic_string<char,struct std::char_traits<char>,class std::allocator<char> >";
struct TInfo {
	bool ok = false;
	TRaw r{};
	std::string name;   // rttr name, e.g. "class wh::conceptmodule::C_Node"
	int kind = kOther;
	bool shared = false, weak = false, stdstr = false, crystr = false, isMap = false;
	int strLike = -1;
};
std::unordered_map<TD*, TInfo> g_types;   // node based: references stay valid
std::string TypeName(const TRaw& r) {
	std::string s;
	if (r.name && r.nameLen && r.nameLen < 4096) ReadStr(r.name, r.nameLen, s);
	const size_t k = s.rfind(">(void) noexcept");
	if (k != std::string::npos && k + 16 == s.size()) s.resize(k);
	return s;
}
void KindOf(const std::string& n, TInfo& ti) {
	int k = kOther;
	if (n == "class wh::conceptmodule::C_Node") k = kNode;
	else if (n == "class wh::conceptmodule::I_Port") k = kPort;
	else if (n == "class wh::conceptmodule::C_Project") k = kProject;
	else if (n == "class wh::conceptmodule::C_SharedResource") ti.shared = true;
	if (k && (!ti.kind || k == kProject)) ti.kind = k;
}
const TInfo& Info(TD* t) {
	auto it = g_types.find(t);
	if (it != g_types.end()) return it->second;
	TInfo& ti = g_types[t];
	if (!t || !GTypeRaw(t, &ti.r)) return ti;
	ti.ok = true;
	ti.name = TypeName(ti.r);
	ti.weak = !ti.name.compare(0, 30, "class wh::shared::C_WeakPtrId<");
	ti.stdstr = ti.name == kStdString;
	ti.crystr = ti.name == "class CryStringT<char>";
	ti.isMap = !ti.name.compare(0, 15, "class std::map<");
	if ((ti.r.traits & kClass) && ti.r.raw) {   // the class and its bases name the kind
		KindOf(ti.name, ti);
		void** b = nullptr; size_t n = 0;
		if (GClassVec(ti.r.raw, 0x08, &b, &n) && n < 256) {
			std::vector<TD*> bases(n);
			if (!n || GRead(b, bases.data(), n * sizeof(TD*)))
				for (TD* bt : bases) { TRaw br{}; if (bt && GTypeRaw(bt, &br)) KindOf(TypeName(br), ti); }
		}
	}
	return ti;
}
bool StringLike(TD* t) {
	TInfo& ti = const_cast<TInfo&>(Info(t));
	if (ti.strLike < 0) { uint8_t r = 0; ti.strLike = GStringLike(t, &r) ? r : 0; }
	return ti.strLike == 1;
}
TD* Outer(const TInfo& ti, TD* t) { return ti.r.wrappedValid ? ti.r.wrapped : t; }   // wrapped type, else itself
const TInfo& RawInfo(TD* t) {   // raw type of the (wrapped) type, as FUN_181e25ac0 classifies values
	const TInfo& ti = Info(t);
	TD* o = Outer(ti, t);
	const TInfo& oi = Info(o);
	return Info(oi.r.raw ? oi.r.raw : o);
}

struct PInfo { bool ok = false, writable = false, objects = true; std::string name; };
std::unordered_map<void*, PInfo> g_props;
const PInfo& Prop(void* pw) {
	auto it = g_props.find(pw);
	if (it != g_props.end()) return it->second;
	PInfo& p = g_props[pw];
	const char* n = nullptr; size_t len = 0; uint8_t w = 0;
	if (!GPropName(pw, &n, &len) || !ReadStr(n, len, p.name) || !GPropFilter(pw, &w)) return p;
	p.ok = true;
	p.writable = w != 0;
	TD* t = nullptr;
	if (p.writable && GPropType(pw, &t)) {   // the path walk skips properties that can hold no object
		const TInfo& ti = Info(t);
		const TInfo& ri = RawInfo(t);
		p.objects = (ri.r.traits & (kSeq | kAssoc)) || (ti.r.traits & kPointer) || ti.r.wrappedValid ||
		            ((ri.r.traits & kClass) && !ri.weak && !ri.stdstr && !ri.crystr && !StringLike(t));
	}
	return p;
}

// ---- state while armed (used only on the arming thread)
std::atomic<bool> g_armed{false}, g_ready{false};
std::atomic<DWORD> g_tid{0};
char g_path[1024] = {};
TD* g_strType = nullptr;
std::unordered_map<uint32_t, std::string> g_ids;   // resolver id (object +0x08) -> path, over the projects of one arming
int g_index = 0;
long long g_totLines = 0, g_totErr = 0, g_totObjects = 0;
bool g_fileErrLogged = false;

constexpr long long kMaxLines = 200000000;   // per project: the walk stops (a safety stop)
constexpr long long kTextLines = 40000000;    // per project: text written up to here; the block hashes cover the whole walk
constexpr int kMaxDepth = 400;
constexpr size_t kMaxItems = 2000000;      // per container

struct Ctx {
	int pass = 1;   // 1: assign paths (no text), 2: print
	FILE* f = nullptr;
	std::string out;
	std::vector<uint64_t> hs;   // hash of each open block (Merkle: a nested block counts as its two lines)
	long long lines = 0, objects = 0, opaque = 0, errors = 0, err1 = 0, unresolved = 0, mismatch = 0, idClash = 0;
	bool capped = false, quiet = false;
	void* root = nullptr;
	std::string projPath;
	std::unordered_map<void*, std::string> addr;   // pass 1: object -> path
	std::unordered_set<void*> printed;             // pass 2
	std::unordered_map<std::string, int> used;     // path -> uses (a second use gets #2)
	std::vector<size_t> unreached;                 // project children (+0x40) the rttr walk did not reach
	size_t children = 0;
	~Ctx() { Flush(); if (f) fclose(f); }
	void Flush() { if (f && !out.empty()) fwrite(out.data(), 1, out.size(), f); out.clear(); }
	void Raw(const std::string& s) {
		lines++;
		if (!quiet) {
			out.append(hs.size(), ' ');
			out += s;
			out += '\n';
			if (out.size() >= (1u << 20)) Flush();
		}
		if (!hs.empty()) hs.back() = Fnv(hs.back(), s);
	}
	void Line(const std::string& s) {
		if (pass != 2 || capped) return;
		if (lines >= kMaxLines) { capped = true; Raw("!capped at " + std::to_string(kMaxLines) + " lines"); return; }
		if (!quiet && lines >= kTextLines) {   // not hashed: the same text in every dump of the same walk length
			quiet = true;
			out += "!text stops at " + std::to_string(kTextLines) + " lines; the hashes below cover the whole walk\n";
		}
		Raw(s);
	}
	void Leaf(const std::string& label, const std::string& v) { Line(label + " = " + v); }
	void Open(const std::string& s) { if (pass != 2 || capped) return; Line(s); if (!capped) hs.push_back(kFnv0); }
	void Close() { if (pass != 2 || capped || hs.size() < 2) return; const uint64_t h = hs.back(); hs.pop_back(); Line("} #" + Hex(h)); }
	void Err(const std::string& label, const std::string& what) {
		if (pass == 2) { errors++; Line(label + " = ?err " + what); }
		else err1++;
	}
};

std::string Comp(const std::string& label) { return label[0] == '[' ? label : "." + label; }
std::string Unique(Ctx& c, const std::string& p) { int& n = c.used[p]; return n++ ? p + "#" + std::to_string(n) : p; }
std::string WeakText(Ctx& c, uint32_t id) {
	if (id == 0xffffffffu) return "~null";
	auto it = g_ids.find(id);
	if (it != g_ids.end()) return "~" + it->second;
	c.unresolved++;
	return "~?";
}
void RegisterId(Ctx& c, void* obj, const std::string& path) {
	uint32_t id = 0;
	if (!GR((uint8_t*)obj + 8, id) || id == 0xffffffffu) return;
	std::string& p = g_ids[id];
	if (!p.empty() && p != path) c.idClash++;   // the last registration wins (a later build of the same arming)
	p = path;
}
std::string RefPath(Ctx& c, int kind, void* id, const std::string& path, const std::string& anchor) {
	std::string nm;
	if (kind == kProject && id == c.root) return c.projPath;
	if (kind == kNode && CryAt(id, 0x10, nm) && !nm.empty()) return anchor + "/" + PathPart(nm);
	if (kind == kPort && CryAt(id, 0x28, nm)) return anchor + ":" + PathPart(nm);
	return path;
}
std::string RttiName(void** vt) {   // ".?AVC_InputDataPort@conceptmodule@wh@@" -> "C_InputDataPort"
	uintptr_t col = 0; uint32_t td = 0;
	char b[256];
	if (!GR(vt - 1, col) || !GR((const void*)(col + 12), td) || !GCStr((const char*)(g_base + td + 0x10), b, sizeof b)) return "?";
	std::string s = b;
	if (!s.compare(0, 4, ".?AV") || !s.compare(0, 4, ".?AU")) s = s.substr(4);
	const size_t at = s.find('@');
	return at == std::string::npos ? s : s.substr(0, at);
}
std::string ConstReturn(void* fn) {   // "mov al, n; ret" or "mov eax, n; ret": the value, without calling it
	unsigned char b[6] = {};
	if (!GRead(fn, b, sizeof b)) return "?";
	if (b[0] == 0xb0 && b[2] == 0xc3) return std::to_string(b[1]);
	if (b[0] == 0xb8 && b[5] == 0xc3) return std::to_string(b[1] | b[2] << 8 | b[3] << 16 | (unsigned)b[4] << 24);
	return "fn";
}

bool ToString(Var& v, std::string& out) {   // the game's conversion to std::string (enum names, string-like classes)
	if (!g_strType) return false;
	Var o = EmptyVar();
	bool ok = false;
	if (GConvert(&v, g_strType, &o) && o.pol == A(kStrPolicy)) ok = StdAt((const void*)o.d[0], out);
	GDestroy(&o);
	return ok;
}
bool Number(const void* p, const TInfo& ri, std::string& s) {
	const std::string& n = ri.name;
	const size_t sz = ri.r.size;
	char b[64];
	if ((n == "float" && sz == 4) || (n == "double" && sz == 8)) {
		double d; uint64_t bits = 0;
		if (sz == 4) { float f; if (!GR(p, f)) return false; d = f; uint32_t u; memcpy(&u, &f, 4); bits = u; }
		else { if (!GR(p, d)) return false; memcpy(&bits, &d, 8); }
		if (std::isnan(d)) snprintf(b, sizeof b, "nan:0x%llx", (unsigned long long)bits);
		else snprintf(b, sizeof b, "%a", d);
		s = b;
		return true;
	}
	uint64_t u = 0;
	if (sz != 1 && sz != 2 && sz != 4 && sz != 8) return false;
	if (!GRead(p, &u, sz)) return false;
	if (n == "bool") { s = u == 0 ? "false" : u == 1 ? "true" : "bool:" + std::to_string(u); return true; }
	if (n.find("unsigned") != std::string::npos) { s = std::to_string(u); return true; }
	if (sz < 8 && ((u >> (sz * 8 - 1)) & 1)) u |= ~0ull << (sz * 8);   // sign extend
	s = std::to_string((long long)u);
	return true;
}

// ---- the walk
void WalkVar(Ctx& c, Var& v, const std::string& label, const std::string& path, const std::string& anchor, int depth);
void WalkObject(Ctx& c, const Inst& in, void* id, TD* dyn, bool ref, Var* src, const std::string& label, const std::string& path,
                const std::string& anchor, int depth);

void WalkRaw(Ctx& c, void* obj, const std::string& label, const std::string& path, const std::string& anchor, int depth) {
	if (!obj) { c.Leaf(label, "null"); return; }
	DInfo di{};
	if (!GObjDerived(obj, A(kGetPtr), &di)) {
		if (c.pass == 2) { c.opaque++; c.Leaf(label, "?opaque raw object"); }
		return;
	}
	Inst fake{ nullptr, di.t, (void*)1, di.ptr }, in{};   // the deserializer's form: {type, invalid, object, 0}
	if (!GUnwrap(&fake, &in)) { c.Err(label, "instance"); return; }
	WalkObject(c, in, di.ptr, di.t, true, nullptr, label, path, anchor, depth + 1);
}

void WeakVec(Ctx& c, const void* vec, const std::string& label) {   // vector<{u32 id, u8 gen}> (8 B elements)
	uintptr_t b = 0, e = 0;
	if (!GR(vec, b) || !GR((const uint8_t*)vec + 8, e) || e < b || (e - b) % 8 || (e - b) / 8 > kMaxItems) { c.Err(label, "links"); return; }
	std::vector<uint64_t> ids((e - b) / 8);
	if (!ids.empty() && !GRead((const void*)b, ids.data(), e - b)) { c.Err(label, "links"); return; }
	c.Open(label + " = [" + std::to_string(ids.size()) + "] {");
	for (size_t i = 0; i < ids.size(); i++) c.Leaf("[" + std::to_string(i) + "]", WeakText(c, (uint32_t)ids[i]));
	c.Close();
}
void Weak1(Ctx& c, const void* p, const std::string& label) {
	uint32_t id = 0;
	if (GR(p, id)) c.Leaf(label, WeakText(c, id));
	else c.Err(label, "read");
}

void PtrVec(Ctx& c, const void* vec, const std::string& label, const std::string& path, const std::string& anchor, int depth,
            std::vector<void*>& items) {   // vector of 8-byte smart pointers
	uintptr_t b = 0, e = 0;
	items.clear();
	if (!GR(vec, b) || !GR((const uint8_t*)vec + 8, e) || e < b || (e - b) % 8 || (e - b) / 8 > kMaxItems) { c.Err(label, "vector"); return; }
	items.resize((e - b) / 8);
	if (!items.empty() && !GRead((const void*)b, items.data(), e - b)) { items.clear(); c.Err(label, "vector"); return; }
	if (path.empty()) return;   // read only
	c.Open(label + " = [" + std::to_string(items.size()) + "] {");
	for (size_t i = 0; i < items.size() && !c.capped; i++) {
		const std::string li = "[" + std::to_string(i) + "]";
		WalkRaw(c, items[i], li, path + li, anchor, depth);
	}
	c.Close();
}

void Extras(Ctx& c, int kind, void* obj, const std::string& me, const std::string& anchor, int depth) {
	const uint8_t* o = (const uint8_t*)obj;
	std::vector<void*> items;
	if (kind == kNode) {
		uint32_t r18 = 0;
		if (c.pass == 2) {
			if (GR(o + 0x18, r18)) c.Leaf(".raw18", std::to_string(r18)); else c.Err(".raw18", "read");
			Weak1(c, o + 0x1c, ".parent");
		}
		PtrVec(c, o + 0x28, ".ports", me + ".ports", anchor, depth, items);
	} else if (kind == kPort && c.pass == 2) {
		void** vt = nullptr;
		if (!GR(o, vt) || !vt) { c.Err(".port", "vtable"); return; }
		void* s[27] = {};
		if (!GRead(vt, s, sizeof s)) { c.Err(".port", "vtable"); return; }
		c.Leaf(".rtti", RttiName(vt));
		c.Leaf(".kind", ConstReturn(s[17]));
		const uintptr_t s25 = (uintptr_t)s[25] - g_base, s26 = (uintptr_t)s[26] - g_base;
		if (s25 == kOutTrigAdd) WeakVec(c, o + 0x30, ".links");
		else if (s25 == kIfTrigAdd && s26 == kIfTrigAdd2) { WeakVec(c, o + 0x38, ".links"); WeakVec(c, o + 0x50, ".links2"); }
		else if (s25 == kIfDataAdd && s26 == kIfDataSet) { WeakVec(c, o + 0x40, ".links"); Weak1(c, o + 0x34, ".link"); }
		else if (s26 == kInDataSet) Weak1(c, o + 0x30, ".link");
		else c.Leaf(".links", "none");
	} else if (kind == kProject && obj == c.root) {
		PtrVec(c, o + 0x40, ".children", std::string(), anchor, depth, items);   // read only
		if (c.pass == 1) {   // which children did the rttr walk reach (decided once, used by both passes)
			c.children = items.size();
			c.unreached.clear();
			for (size_t i = 0; i < items.size(); i++) {
				DInfo di{};
				void* id = items[i] && GObjDerived(items[i], A(kGetPtr), &di) ? di.ptr : items[i];
				if (id && !c.addr.count(id)) c.unreached.push_back(i);
			}
		}
		c.Leaf(".children", std::to_string(c.children) + " rttr-reached " + std::to_string(c.children - c.unreached.size()));
		if (c.unreached.empty()) return;
		c.Open(".unreached = [" + std::to_string(c.unreached.size()) + "] {");
		for (size_t i : c.unreached) {
			if (c.capped) break;
			const std::string li = "[" + std::to_string(i) + "]";
			WalkRaw(c, i < items.size() ? items[i] : nullptr, li, me + ".unreached" + li, anchor, depth);
		}
		c.Close();
	}
}

int WalkProps(Ctx& c, const Inst& in, TD* dyn, const std::string& path, const std::string& anchor, int depth) {
	const TInfo& di = Info(dyn);
	void** b = nullptr; size_t n = 0;
	if (!GClassVec(di.r.raw ? di.r.raw : dyn, 0x50, &b, &n)) { c.Err(".props", "class data"); return -1; }
	std::vector<void*> pws(n);
	if (n && !GRead(b, pws.data(), n * sizeof(void*))) { c.Err(".props", "read"); return -1; }
	int writable = 0;
	for (void* pw : pws) {
		if (c.capped) break;
		const PInfo& p = Prop(pw);
		if (!p.ok) { c.Err(".prop", "property info"); continue; }
		if (!p.writable) continue;
		writable++;
		if (c.pass == 1 && !p.objects) continue;
		Var v = EmptyVar();
		Inst inst = in;
		if (!GGetValue(pw, &v, &inst)) { c.Err(p.name, "get_value"); continue; }   // v may be half built: leaked, not destroyed
		WalkVar(c, v, p.name, path + "." + p.name, anchor, depth);
		GDestroy(&v);
	}
	return writable;
}

void WalkObject(Ctx& c, const Inst& in, void* id, TD* dyn, bool ref, Var* src, const std::string& label, const std::string& path,
                const std::string& anchor, int depth) {
	if (c.capped) return;
	const TInfo& di = Info(dyn);
	if (!di.ok) { c.Err(label, "type data"); return; }
	if (di.kind == kProject && id != c.root) {   // another project: by its path, never walked from here
		std::string pp;
		if (c.pass == 2) c.Leaf(label, "@project " + Quote(CryAt(id, 0xa0, pp) ? pp : std::string("?")));
		return;
	}
	if (depth > kMaxDepth) { c.Err(label, "depth"); return; }
	std::string me;
	if (ref) {
		if (c.pass == 1) {
			if (c.addr.count(id)) return;
			me = Unique(c, RefPath(c, di.kind, id, path, anchor));
			c.addr.emplace(id, me);
			if (di.shared || di.kind) RegisterId(c, id, me);
		} else {
			auto it = c.addr.find(id);
			if (!c.printed.insert(id).second) { c.Leaf(label, "@" + (it != c.addr.end() ? it->second : std::string("?"))); return; }
			if (it == c.addr.end()) { c.mismatch++; me = RefPath(c, di.kind, id, path, anchor); }
			else me = it->second;
		}
	} else me = path;
	if (c.pass == 2) c.objects++;
	c.Open(label + " = " + Short(di.name) + (ref ? " @= " + me : "") + " {");
	const std::string sub = di.kind ? me : anchor;
	const int n = WalkProps(c, in, dyn, me, sub, depth + 1);
	if (n == 0 && !di.kind && c.pass == 2) {   // no writable property: its string form, else opaque
		std::string s;
		if (src && ToString(*src, s)) c.Leaf(".str", Quote(s));
		else { c.opaque++; c.Leaf(".opaque", std::to_string(di.r.size)); }
	}
	Extras(c, di.kind, id, me, sub, depth + 1);
	c.Close();
}

void Scalar(Ctx& c, Var& v, const TInfo& ti, const TInfo& ri, const std::string& label) {
	Inst in{};
	if (!GInstance(&v, &in)) { c.Err(label, "instance"); return; }
	const void* p = ti.r.wrappedValid ? in.wdata : in.data;
	if (!p) { c.Leaf(label, "null"); return; }
	std::string s;
	if (ri.stdstr || ri.crystr) {
		if (ri.stdstr ? StdAt(p, s) : CryAt(p, 0, s)) c.Leaf(label, Quote(s));
		else c.Err(label, "string");
		return;
	}
	if (ri.r.traits & kArith) {
		if (Number(p, ri, s)) c.Leaf(label, s);
		else c.Err(label, "number");
		return;
	}
	if (ToString(v, s)) { c.Leaf(label, Quote(s)); return; }
	if ((ri.r.traits & kEnum) && Number(p, ri, s)) { c.Leaf(label, "#" + s); return; }
	c.opaque++;
	c.Leaf(label, "?opaque " + Short(ri.name) + " " + std::to_string(ri.r.size));
}

void WalkSeq(Ctx& c, Var& v, const std::string& label, const std::string& path, const std::string& anchor, int depth) {
	std::vector<uint64_t> view(0x40);   // 0x200 B (the game's view is 0xb0)
	size_t n = 0;
	if (!GSeqView(&v, view.data()) || !GSeqSize(view.data(), &n)) { c.Err(label, "sequential view"); return; }
	if (n > kMaxItems) { c.Err(label, "size " + std::to_string(n)); return; }
	c.Open(label + " = [" + std::to_string(n) + "] {");
	for (size_t i = 0; i < n && !c.capped; i++) {
		const std::string li = "[" + std::to_string(i) + "]";
		Var e = EmptyVar(), x = EmptyVar();
		if (!GSeqGet(view.data(), i, &e)) { c.Err(li, "element"); continue; }
		if (!GExtract(&e, &x)) x = EmptyVar();   // the element is a reference_wrapper: its value, as the deserializer does
		WalkVar(c, IsEmpty(x) ? e : x, li, path + li, anchor, depth + 1);
		GDestroy(&x);
		GDestroy(&e);
	}
	c.Close();
}

void MapValue(Ctx& c, TD* vt, const uint8_t* p, const std::string& label, const std::string& path, const std::string& anchor, int depth) {
	const TInfo& vi = Info(vt);
	const TInfo& ri = RawInfo(vt);
	if (vi.r.wrappedValid || (vi.r.traits & kPointer)) {   // _smart_ptr<T> / T*: the object pointer is the first qword
		void* obj = nullptr;
		if (!GR(p, obj)) { c.Err(label, "read"); return; }
		WalkRaw(c, obj, label, path, anchor, depth);
		return;
	}
	if (c.pass != 2) return;
	std::string s;
	if (ri.stdstr) { if (StdAt(p, s)) c.Leaf(label, Quote(s)); else c.Err(label, "string"); }
	else if (ri.crystr) { if (CryAt(p, 0, s)) c.Leaf(label, Quote(s)); else c.Err(label, "string"); }
	else if ((ri.r.traits & (kArith | kEnum)) && Number(p, ri, s)) c.Leaf(label, (ri.r.traits & kEnum) ? "#" + s : s);
	else { c.opaque++; c.Leaf(label, "?opaque " + Short(ri.name) + " " + std::to_string(ri.r.size)); }
}

void WalkAssoc(Ctx& c, Var& v, const TInfo& ri, const std::string& label, const std::string& path, const std::string& anchor, int depth) {
	std::vector<uint64_t> view(0x40);
	if (!GAssocView(&v, view.data())) { c.Err(label, "associative view"); return; }
	TD* kt = (TD*)view[1];
	TD* vt = (TD*)view[2];
	const void* cont = (const void*)view[3];
	if (!ri.isMap || !RawInfo(kt).stdstr || !cont || !Info(vt).ok) {   // only std::map<std::string, V> is read
		if (c.pass == 2) { c.opaque++; c.Leaf(label, "?assoc " + Short(ri.name)); }
		return;
	}
	uint8_t* head = nullptr; uint8_t* node = nullptr; size_t n = 0;
	if (!GTreeFirst(cont, &head, &node, &n) || n > kMaxItems) { c.Err(label, "map"); return; }
	c.Open(label + " = map " + std::to_string(n) + " {");
	for (size_t k = 0; node && node != head && k < n && !c.capped; k++) {
		std::string key;
		if (!StdAt(node + 0x20, key)) { c.Err("[?]", "key"); break; }
		const std::string li = "[" + Quote(key) + "]";
		MapValue(c, vt, node + 0x40, li, path + li, anchor, depth + 1);
		if (!GTreeNext(node, &node)) { c.Err(li, "next node"); break; }
	}
	c.Close();
}

void WalkVar(Ctx& c, Var& v, const std::string& label, const std::string& path, const std::string& anchor, int depth) {
	if (c.capped) return;
	if (IsEmpty(v)) { c.Leaf(label, "?empty"); return; }
	TD* t = nullptr;
	if (!GVarType(&v, &t)) { c.Err(label, "variant type"); return; }
	const TInfo& ti = Info(t);
	const TInfo& ri = RawInfo(t);
	if (!ti.ok || !ri.ok) { c.Err(label, "type data"); return; }
	if (ri.weak) {   // C_WeakPtrId {u32 id, u8 gen}: the path of the object with that resolver id
		Inst in{};
		uint32_t id = 0;
		if (c.pass != 2) return;
		if (GInstance(&v, &in) && in.data && GR(in.data, id)) c.Leaf(label, WeakText(c, id));
		else c.Err(label, "weak id");
		return;
	}
	if (ri.r.traits & kSeq) { WalkSeq(c, v, label, path, anchor, depth); return; }
	if (ri.r.traits & kAssoc) { WalkAssoc(c, v, ri, label, path, anchor, depth); return; }
	// an object as FUN_181e25ac0 decides it: a class not read from a string, or any pointer
	if (((ri.r.traits & kClass) && !ri.stdstr && !ri.crystr && !StringLike(t)) || (ti.r.traits & kPointer)) {
		Inst in{};
		if (!GInstance(&v, &in)) { c.Err(label, "instance"); return; }
		if (Info(Info(in.t).r.raw).r.wrappedValid) {   // _smart_ptr<T> and other wrappers: the wrapped object
			Inst w{};
			if (!GUnwrap(&in, &w)) { c.Err(label, "unwrap"); return; }
			in = w;
		}
		if (!in.data) { c.Leaf(label, "null"); return; }
		TD* dyn = in.t;
		void* id = in.data;
		const TInfo& ii = Info(in.t);
		if (!(ii.r.traits & kEnum)) {
			DInfo di{};
			if (!ii.r.raw || !GDerived(ii.r.raw, in.data, &di)) { c.Err(label, "derived type"); return; }
			dyn = di.t;
			if (di.ptr) id = di.ptr;
		}
		const bool ref = ti.r.wrappedValid || (ti.r.traits & kPointer);
		WalkObject(c, in, id, dyn, ref, &v, label, path, anchor, depth + 1);
		return;
	}
	if (c.pass == 2) Scalar(c, v, ti, ri, label);
}

void DeserMap(Ctx& c, uintptr_t deser) {   // deser+0x2d0: map<CryString, vector<{WeakId, WeakId, CryString, CryString, u8}>>
	const uint8_t* m = (const uint8_t*)(deser + 0x2d0);
	uint8_t* head = nullptr; uint8_t* node = nullptr; size_t n = 0;
	if (!GTreeFirst(m, &head, &node, &n) || n > kMaxItems) { c.Err("deser2d0", "map"); return; }
	c.Open("deser2d0 = map " + std::to_string(n) + " {");
	for (size_t k = 0; node && node != head && k < n && !c.capped; k++) {
		std::string key;
		if (!CryAt(node, 0x20, key)) { c.Err("[?]", "key"); break; }
		uintptr_t b = 0, e = 0;
		const std::string li = "[" + Quote(key) + "]";
		if (!GR(node + 0x28, b) || !GR(node + 0x30, e) || e < b || (e - b) % 0x28 || (e - b) / 0x28 > kMaxItems) c.Err(li, "vector");
		else {
			c.Open(li + " = [" + std::to_string((e - b) / 0x28) + "] {");
			for (uintptr_t p = b, i = 0; p < e; p += 0x28, i++) {
				uint32_t a = 0, z = 0; uint8_t flag = 0;
				std::string s1, s2;
				if (!GR((const void*)p, a) || !GR((const void*)(p + 8), z) || !CryAt((const void*)p, 0x10, s1) ||
				    !CryAt((const void*)p, 0x18, s2) || !GR((const void*)(p + 0x20), flag)) { c.Err("[" + std::to_string(i) + "]", "read"); continue; }
				c.Leaf("[" + std::to_string(i) + "]", WeakText(c, a) + " " + WeakText(c, z) + " " + Quote(s1) + " " + Quote(s2) + " " +
				       std::to_string(flag));
			}
			c.Close();
		}
		if (!GTreeNext(node, &node)) { c.Err(li, "next node"); break; }
	}
	c.Close();
}

void BeginProject(Ctx& c, const std::string& head) {   // the header line is the first line of the project's hash
	c.hs.assign(1, Fnv(kFnv0, head));
	c.out += head + "\n";
	c.lines++;
}
uint64_t EndProject(Ctx& c, const std::string& pp) {   // a capped walk leaves blocks open: the hash covers what was written
	const uint64_t h = c.hs.empty() ? 0 : c.hs[0];
	c.hs.clear();
	char tail[256];
	snprintf(tail, sizeof tail, " objects=%lld lines=%lld opaque=%lld err=%lld unresolved=%lld #", c.objects, c.lines + 1, c.opaque,
	         c.errors, c.unresolved);
	c.out += "end " + Quote(pp) + tail + Hex(h) + "\n";
	c.Flush();
	return h;
}

void DumpProject(uintptr_t deser, const uint64_t* q, uint64_t ret) {
	const long long t0 = Now();
	if (!g_strType) GStrType(&g_strType);
	Ctx c;
	// the root as the deserializer sees it: {_smart_ptr<C_Project>, C_Project*, &sp, C_Project*} -> unwrapped
	Inst in{ (TD*)q[0], (TD*)q[1], (void*)q[2], (void*)q[3] };
	if (Info(Info(in.t).r.raw).r.wrappedValid) { Inst w{}; if (GUnwrap(&in, &w)) in = w; }
	TD* dyn = in.t;
	c.root = in.data;
	DInfo di{};
	if (in.data && Info(in.t).r.raw && GDerived(Info(in.t).r.raw, in.data, &di)) { dyn = di.t; if (di.ptr) c.root = di.ptr; }
	std::string pp = "?";
	if (in.data) CryAt(in.data, 0xa0, pp);
	c.projPath = PathPart(pp);
	for (int pass = 1; pass <= 2; pass++) {
		c.pass = pass;
		if (pass == 2) {
			if (fopen_s(&c.f, g_path, "ab")) c.f = nullptr;
			if (!c.f) {
				if (!g_fileErrLogged) Log("questdump: cannot open %s for appending: nothing written", g_path);
				g_fileErrLogged = true;
				g_index++;
				return;
			}
			BeginProject(c, "project " + Quote(pp) + " ret=" + std::to_string(ret & 0xff) + " index=" + std::to_string(g_index));
		}
		if (!in.data || !dyn) c.Err("root", "no project");
		else WalkObject(c, in, c.root, dyn, true, nullptr, "root", c.projPath, c.projPath, 0);
		if (pass == 2) DeserMap(c, deser);
	}
	const uint64_t h = EndProject(c, pp);
	const double ms = (Now() - t0) * 1000.0 / g_freq;
	Log("questdump: project %d %s (ret %d): %lld objects, %lld lines, %lld opaque, %lld errors (+%lld in the path walk), %lld "
	    "unresolved weak ids, %lld id clashes, %lld path mismatches, %zu of %zu children reached by rttr, #%016llx, %.0f ms%s",
	    g_index, pp.c_str(), (int)(ret & 0xff), c.objects, c.lines + 1, c.opaque, c.errors, c.err1, c.unresolved, c.idClash, c.mismatch,
	    c.children - c.unreached.size(), c.children, (unsigned long long)h, ms, c.capped ? " CAPPED" : "");
	g_totLines += c.lines + 1;
	g_totErr += c.errors;
	g_totObjects += c.objects;
	g_index++;
}

void SafeDump(uintptr_t deser, const uint64_t* q, uint64_t ret) {
	try { DumpProject(deser, q, ret); }
	catch (...) { Log("questdump: exception in the dump of project %d (skipped)", g_index); g_index++; }   // /EHa: SEH too
}

using BuildFn = uint64_t(__fastcall*)(uintptr_t, uintptr_t, uint64_t*, uint64_t, uint64_t);
BuildFn g_orig = nullptr;
uint64_t __fastcall HkBuild(uintptr_t deser, uintptr_t path, uint64_t* inst, uint64_t a4, uint64_t a5) {
	if (!g_armed.load(std::memory_order_relaxed) || GetCurrentThreadId() != g_tid.load(std::memory_order_relaxed))
		return g_orig(deser, path, inst, a4, a5);
	uint64_t q[4] = {};
	const bool have = GCopyInst(inst, q);   // the caller's instance; still valid at return, copied anyway
	const uint64_t r = g_orig(deser, path, inst, a4, a5);
	if (have) SafeDump(deser, q, r);
	return r;
}

}  // namespace

void Questdump_Install(uintptr_t base) {
	g_base = base;
	LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = f.QuadPart;
	std::string bad;
	for (int i = 0; i < kCodeN; i++) {
		const unsigned char* p = (const unsigned char*)(base + kCode[i].rva);
		// another hook of ours (questfast's metadata cache, questprof) starts with a jmp: calls still reach the game's code
		const bool ok = !memcmp(p, kCode[i].bytes, kCode[i].n) || (i != kBuild && p[0] == 0xe9);
		if (!ok) bad += std::string(bad.empty() ? "" : ", ") + kCode[i].name;
	}
	if (!bad.empty()) { Log("questdump: OFF (code differs: %s)", bad.c_str()); return; }
	void* t = (void*)(base + kCode[kBuild].rva);
	const bool hooked = MH_CreateHook(t, (void*)&HkBuild, (void**)&g_orig) == MH_OK && MH_EnableHook(t) == MH_OK;
	g_ready = hooked;
	Log("questdump: %s", hooked ? "ready (the quest graph dump is off until armed)" : "OFF (hook failed)");
}

void Questdump_Arm(const char* path) {
	if (!g_ready.load()) { Log("questdump: not armed (the hook is off)"); return; }
	if (!path || !*path) { Log("questdump: not armed (no path)"); return; }
	g_armed = false;
	strncpy_s(g_path, path, _TRUNCATE);
	g_ids.clear();
	g_index = 0;
	g_totLines = g_totErr = g_totObjects = 0;
	g_fileErrLogged = false;
	g_tid = GetCurrentThreadId();
	g_armed = true;
	Log("questdump: armed: every project built on thread %lu is appended to %s", (unsigned long)g_tid.load(), g_path);
}

void Questdump_Disarm() {
	if (!g_armed.exchange(false)) return;
	Log("questdump: disarmed after %d projects: %lld objects, %lld lines, %lld errors, %lld failed variant releases", g_index,
	    g_totObjects, g_totLines, g_totErr, g_destroyErr.load());
}
