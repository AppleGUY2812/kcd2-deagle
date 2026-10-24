// Quest node build record / replay: see questreplay.h. Function facts: re_quest_replay_spec.md (S = read in code).
#include "questreplay.h"
#include "log.h"
#include "MinHook.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

uintptr_t g_base = 0;
long long g_freq = 1;
inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }

// ---- game code (WHGame.dll 1.5.6); the first kHookN are hooked (record; the element also starts a replay)
struct Fn { const char* name; uintptr_t rva; unsigned char b[16]; };
enum {
	kElem, kProp, kChild, kSeq, kCreate, kDef, kTypeCreate, kAttrConv, kTextConv, kGetValue, kPend, kAssocMap, kAssocSet,
	kTypesByName, kPush260, kXSetPath, kInvoke1, kRegister, kUnregister, kHookN,
	kInvoke0 = kHookN, kUnwrap, kInvalidType, kMetaMap, kHandler, kMethValid, kArgInt, kWrapNonPtr, kMakeArg, kVarDestroy,
	kSeqView, kViewSetSize, kViewGet, kViewSet, kVarType, kUnwrapPtr, kVarInst, kExtract, kMove, kValid, kClone, kFilter,
	kListDestroy, kInstFromVar, kStaticSp, kStaticName, kSpRelease, kCanBeNode, kNodeSp, kCanBeX, kXSp, kSp2Release,
	kCryFromC, kCryRelease, kCryAssign, kCryClear, kEmptyPol, kXmlRelease, kDeserVt18, kConvert, kFnN
};
const Fn kFn[kFnN] = {
	{ "element", 0x1e25520, { 0x48,0x89,0x5c,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57 } },
	{ "property", 0x1e25ac0, { 0x48,0x89,0x5c,0x24,0x18,0x4c,0x89,0x4c,0x24,0x20,0x55,0x56,0x57,0x41,0x54,0x41 } },
	{ "child object", 0x1e25110, { 0x40,0x55,0x53,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24 } },
	{ "sequential", 0x4f8b4c, { 0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c } },
	{ "factory Create", 0x1e38fa0, { 0x40,0x53,0x56,0x57,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x81,0xec,0x08,0x01,0x00 } },
	{ "instantiate definition", 0x6906e0, { 0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c } },
	{ "type create", 0x69c708, { 0x48,0x89,0x5c,0x24,0x20,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57 } },
	{ "attribute convert", 0x4fb74c, { 0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8b,0xec,0x48,0x83 } },
	{ "text convert", 0x10c1dd8, { 0x48,0x89,0x5c,0x24,0x08,0x55,0x56,0x57,0x48,0x83,0xec,0x50,0x48,0x8b,0x05,0xd5 } },
	{ "get_value thunk", 0x3d23a0, { 0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x09,0x48,0x8b,0xda,0x48,0x8b,0x01,0xff } },
	{ "pending handler", 0x699fe0, { 0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x70,0x10,0x48,0x89,0x78,0x18,0x4c } },
	{ "associative map", 0x69c21c, { 0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24,0xd0 } },
	{ "associative set", 0x25d46ac, { 0x48,0x89,0x5c,0x24,0x20,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24 } },
	{ "types by name", 0x6a5798, { 0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x55 } },
	{ "+0x260 push", 0x6903d4, { 0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x30,0x49,0x8b,0xd8,0x48,0x81,0xc1 } },
	{ "definition path", 0x1a80abc, { 0x40,0x53,0x48,0x83,0xec,0x30,0x48,0x8b,0xd9,0x48,0x8d,0x4c,0x24,0x20,0xe8,0x9d } },
	{ "method invoke 1", 0x6aa718, { 0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x09,0x48,0x8b,0xda,0x48,0x8b,0x01,0xff } },
	{ "resolver Register", 0x7cec40, { 0x4c,0x8b,0xdc,0x49,0x89,0x5b,0x10,0x49,0x89,0x6b,0x18,0x49,0x89,0x73,0x20,0x57 } },
	{ "resolver Unregister", 0x7cebec, { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48 } },
	{ "method invoke 0", 0x699ef4, { 0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x09,0x48,0x8b,0xda,0x48,0x8b,0x01,0xff } },
	{ "instance unwrap", 0x69e9dc, { 0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xda,0x48,0x8b,0xf9 } },
	{ "invalid type", 0xc0cda4, { 0x48,0x83,0xec,0x28,0x8b,0x0d,0xee,0xb7,0x4b,0x04,0x65,0x48,0x8b,0x04,0x25,0x58 } },
	{ "element metadata map", 0x68d138, { 0x48,0x8b,0xc4,0x48,0x89,0x58,0x10,0x48,0x89,0x68,0x18,0x48,0x89,0x70,0x20,0x57 } },
	{ "handler slot", 0xaaf22c, { 0x40,0x53,0x48,0x83,0xec,0x30,0x49,0x63,0xc0,0x48,0x8b,0xda,0x48,0x8b,0x04,0xc1 } },
	{ "method valid", 0x61acc8, { 0x48,0x8b,0x09,0x48,0x8b,0x01,0x48,0xff,0x60,0x08,0xcc,0xcc,0x40,0x53,0x48,0x83 } },
	{ "int argument", 0xce0c70, { 0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x48,0x89,0x11,0x48,0xc7,0x41,0x08 } },
	{ "wrapper of non-pointer", 0x4f720c, { 0x48,0x8b,0x01,0x48,0x8b,0x40,0x08,0x80,0xb8,0xc0,0x00,0x00,0x00,0x00,0x75,0x03 } },
	{ "variant argument", 0x4f81f8, { 0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x20,0x48 } },
	{ "variant destroy", 0x4fb9d0, { 0x48,0x8b,0x41,0x10,0x48,0x8b,0xd1,0x33,0xc9,0x45,0x33,0xc0,0x48,0xff,0xe0,0xcc } },
	{ "sequential view", 0x6abf94, { 0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x48,0x8b,0xfa } },
	{ "view set_size", 0x240a800, { 0x48,0x8b,0x41,0x28,0x48,0x8b,0x49,0x10,0x48,0xff,0xe0,0xcc,0x48,0x8b,0x81,0x98 } },
	{ "view get", 0x4f7230, { 0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x81,0xa0,0x00,0x00,0x00,0x48,0x8b,0xda } },
	{ "view set", 0x240a80c, { 0x48,0x8b,0x81,0x98,0x00,0x00,0x00,0x48,0x8b,0x49,0x10,0x48,0xff,0xe0,0xcc,0xcc } },
	{ "variant type", 0x4f9cec, { 0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x44,0x8b,0x05,0x9f,0xe8,0xbc } },
	{ "unwrapped is pointer", 0x4f7254, { 0x48,0x83,0xec,0x28,0x48,0x8b,0x01,0x48,0x8b,0x48,0x08,0x80,0xb9,0xc0,0x00,0x00 } },
	{ "variant instance", 0x69d270, { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7c,0x24,0x18,0x41 } },
	{ "extract wrapped", 0x69f0d4, { 0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8d,0x05,0x37,0xcb,0xe5,0xff,0x48,0x8b,0xda } },
	{ "variant move", 0x4f8734, { 0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x41,0x10,0x48,0x8b } },
	{ "variant valid", 0x4f9cdc, { 0x48,0x8b,0x41,0x10,0x48,0x8b,0xd1,0xb1,0x0f,0x45,0x33,0xc0,0x48,0xff,0xe0,0xcc } },
	{ "variant clone", 0x4f8710, { 0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x42,0x10,0x48,0x8b,0xd9,0x48,0x89,0x41 } },
	{ "type filter", 0x4f4b14, { 0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x83,0xec,0x20,0x48 } },
	{ "type list destroy", 0x6a5fbc, { 0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x48,0x8b,0x49,0x20,0x48,0x85,0xc9 } },
	{ "instance of variant", 0x23cc2b4, { 0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xc2,0x48,0x8b,0xd9,0x48,0x8b,0xd1,0x48 } },
	{ "static smart pointer", 0x10b0408, { 0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xda,0x48,0xc7,0x02,0x00,0x00,0x00,0x00 } },
	{ "static name", 0x2628268, { 0x48,0x83,0xc1,0x40,0xe9,0xd3,0xb0,0xec,0xfd,0xcc,0xcc,0xcc,0x48,0x83,0xec,0x28 } },
	{ "smart pointer release", 0x6967cc, { 0x48,0x8b,0x09,0x48,0x85,0xc9,0x75,0x01,0xc3,0xe9,0x46,0x2b,0x7a,0x01,0xcc,0xcc } },
	{ "can be node", 0x6905e8, { 0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x48,0x8d,0x4c,0x24,0x38,0xe8,0x15 } },
	{ "node smart pointer", 0x6905b8, { 0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xda,0x48,0xc7,0x02,0x00,0x00,0x00,0x00 } },
	{ "can be definition user", 0x6b4d64, { 0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x48,0x8d,0x4c,0x24,0x38,0xe8,0xa1 } },
	{ "definition user pointer", 0x6b4b54, { 0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xda,0x48,0xc7,0x02,0x00,0x00,0x00,0x00 } },
	{ "smart pointer release 2", 0x4c44a0, { 0x48,0x83,0xec,0x28,0x48,0x8b,0x09,0x48,0x85,0xc9,0x74,0x05,0xe8,0x6f,0x4e,0x97 } },
	{ "CryString from text", 0x4f66cc, { 0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57 } },
	{ "CryString release", 0x4fc624, { 0x48,0x83,0xec,0x28,0x48,0x8b,0x09,0x48,0x83,0xc1,0xf4,0xe8,0x54,0x0d,0xef,0x01 } },
	{ "CryString assign", 0x4f6868, { 0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x4c,0x8b,0xca,0x48,0x8b,0xca,0xe8 } },
	{ "CryString clear", 0x5c3668, { 0x48,0x83,0xec,0x28,0x4c,0x8b,0xc9,0x48,0x8b,0x09,0x48,0x83,0xc1,0xf4,0x83,0x79 } },
	{ "empty variant policy", 0x4fbc18, { 0x40,0x53,0x48,0x83,0xec,0x20,0x0f,0xb6,0xd1,0x49,0x8b,0xd8,0x83,0xfa,0x0b,0x77 } },
	{ "XML ref release", 0x4fb980, { 0x48,0x83,0xec,0x28,0x48,0x8b,0x09,0x48,0x85,0xc9,0x74,0x06,0x48,0x8b,0x01,0xff } },
	{ "deserializer vt+0x18", 0xcf5f80, { 0x48,0x83,0xec,0x28,0x49,0x8b,0x08,0x48,0x85,0xc9,0x74,0x06,0x48,0x8b,0x01,0xff } },
	{ "variant convert", 0x4f9640, { 0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8b,0xec,0x48,0x81 } },
};
void* g_orig[kHookN] = {};
bool g_hooked = false, g_enabled = false;
// a hooked function is called through its trampoline (no hook), another one directly
inline void* F(int i) { return i < kHookN && g_orig[i] ? g_orig[i] : (void*)(g_base + kFn[i].rva); }
using F8 = uint64_t(__fastcall*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
#define ORIG(i) ((F8)g_orig[i])

struct Var { uint64_t d[2]; uintptr_t pol; };
struct Sv { const char* p; size_t n; };
struct DInfo { void* ptr; uintptr_t t; };
using PolFn = uint8_t(__fastcall*)(uint8_t, Var*, void*);
inline Var Empty() { Var v; v.d[0] = v.d[1] = 0; v.pol = g_base + kFn[kEmptyPol].rva; return v; }
inline void Destroy(Var* v) { ((void(__fastcall*)(Var*))F(kVarDestroy))(v); }
inline bool Valid(Var* v) { return ((uint8_t(__fastcall*)(Var*))F(kValid))(v) != 0; }
inline uintptr_t TypeOf(Var* v) { uintptr_t t = 0; ((uintptr_t*(__fastcall*)(Var*, uintptr_t*))F(kVarType))(v, &t); return t; }
inline bool IsPtr(uintptr_t t) { return ((uint8_t(__fastcall*)(uintptr_t*))F(kUnwrapPtr))(&t) != 0; }
inline bool WrapNonPtr(uintptr_t t) { return ((uint8_t(__fastcall*)(uintptr_t*))F(kWrapNonPtr))(&t) != 0; }
inline void MakeArg(void* arg, Var* v) { ((void*(__fastcall*)(void*, Var*))F(kMakeArg))(arg, v); }
inline void CryFrom(void* cs, const char* s) { ((void*(__fastcall*)(void*, const char*))F(kCryFromC))(cs, s); }
inline void CryRelease(void* cs) { ((void(__fastcall*)(void*))F(kCryRelease))(cs); }

// XML node virtuals the record reads (CryEngine IXmlNode, S)
inline const char* XTag(void* n) { return ((const char*(__fastcall*)(void*))(*(void***)n)[0x30 / 8])(n); }
inline const char* XAttr(void* n, const char* k) { return ((const char*(__fastcall*)(void*, const char*))(*(void***)n)[0x108 / 8])(n, k); }
inline int XCount(void* n) { return ((int(__fastcall*)(void*))(*(void***)n)[0x160 / 8])(n); }
inline void* XChild(void* n, int i, void** ref) {   // the returned ref is released with vt+0x20
	*ref = nullptr;
	((void*(__fastcall*)(void*, void**, int))(*(void***)n)[0x168 / 8])(n, ref, i);
	return *ref;
}
bool AnyChildTag(void* n, const char* tag, bool any) {
	const int c = XCount(n);
	for (int i = 0; i < c; i++) {
		void* ref = nullptr;
		void* ch = XChild(n, i, &ref);
		const bool hit = ch && (any || !strcmp(XTag(ch), tag));
		if (ref) ((void(__fastcall*)(void*))(*(void***)ref)[0x20 / 8])(ref);
		if (hit) return true;
	}
	return false;
}

// ---- types: what FUN_181e25520 does to find the element's type (S)
uintptr_t DynOf(const uint64_t* in, uint64_t I[4]) {
	const uintptr_t raw = *(uintptr_t*)in[0];
	if (*(uint8_t*)(*(uintptr_t*)(raw + 8) + 0xc0)) {
		uint64_t tmp[4];
		const uint64_t* p = ((uint64_t*(__fastcall*)(const uint64_t*, uint64_t*))F(kUnwrap))(in, tmp);
		memcpy(I, p, 32);
	} else memcpy(I, in, 32);
	const uintptr_t T = I[0];
	void* data = (void*)I[2];
	if (!data) return ((uintptr_t(__fastcall*)())F(kInvalidType))();
	if (*(uint8_t*)(T + 0xc4) & 2) return T;
	uint8_t* cd = ((uint8_t*(__fastcall*)())*(uintptr_t*)(*(uintptr_t*)T + 0xb8))();
	DInfo di{};
	((void*(__fastcall*)(DInfo*, void*))*(uintptr_t*)cd)(&di, data);
	return di.t;
}
inline bool NameIs(uintptr_t td, const char* s, size_t sn) {
	const char* p = *(const char**)(td + 0x38);
	const size_t n = *(size_t*)(td + 0x40);
	return n == sn && (!n || !memcmp(p, s, n));
}

// per build: the element type's properties (class_data+0x50, the deserializer's order) and its handler slots
struct TI { std::vector<void*> props; uintptr_t m[6]; bool valid[6]; bool handlers; std::unordered_map<void*, uint32_t> idx; };
std::unordered_map<uintptr_t, TI> g_ti;
TI& Ti(uintptr_t dyn) {
	auto it = g_ti.find(dyn);
	if (it != g_ti.end()) return it->second;
	TI& t = g_ti[dyn];
	memset(t.m, 0, sizeof t.m);
	memset(t.valid, 0, sizeof t.valid);
	t.handlers = false;
	const uintptr_t raw = *(uintptr_t*)dyn ? *(uintptr_t*)dyn : dyn;
	const uintptr_t getter = *(uintptr_t*)(raw + 0xb8);
	if (getter) {
		uint8_t* cd = ((uint8_t*(__fastcall*)())getter)();
		if (cd) {
			void** b = *(void***)(cd + 0x50);
			void** e = *(void***)(cd + 0x58);
			if (b && e > b) t.props.assign(b, e);
		}
	}
	for (uint32_t i = 0; i < t.props.size(); i++) t.idx.emplace(t.props[i], i);
	return t;
}
void Handlers(uintptr_t deser, uintptr_t dyn, TI& t) {   // FUN_18068d138 + FUN_180aaf22c + FUN_18061acc8, as the element does
	if (t.handlers) return;
	uintptr_t key = dyn, it[2] = {};
	uintptr_t* p = ((uintptr_t*(__fastcall*)(uintptr_t, uintptr_t*, uintptr_t*, uintptr_t*))F(kMetaMap))(deser + 0x10, it, &key, &key);
	alignas(16) uint8_t tbl[0x30];
	memcpy(tbl, (void*)(*p + 0x18), sizeof tbl);
	for (int k = 0; k < 6; k++) {
		((uintptr_t*(__fastcall*)(void*, uintptr_t*, int))F(kHandler))(tbl, &t.m[k], k);
		t.valid[k] = ((uint8_t(__fastcall*)(uintptr_t*))F(kMethValid))(&t.m[k]) != 0;
	}
	t.handlers = true;
}
struct PI { uintptr_t PT; bool cv4; };
std::unordered_map<void*, PI> g_pi;
PI& Pi(void* P) {
	auto it = g_pi.find(P);
	if (it != g_pi.end()) return it->second;
	PI& p = g_pi[P];
	p.PT = 0;
	((uintptr_t*(__fastcall*)(void*, uintptr_t*))(*(void***)P)[0x30 / 8])(P, &p.PT);
	p.cv4 = WrapNonPtr(p.PT);
	return p;
}

// ---- the record: one op stream, strings pooled
enum : uint8_t {
	OP_ELEM = 1, OP_ELEM_END, OP_P_ATTR = 0x10, OP_P_MD6, OP_P_CUR, OP_P_SEQ, OP_SEQ_END, OP_P_OBJ, OP_I_TEXT = 0x18,
	OP_CH_VAL = 0x20, OP_CH_NULL, OP_CH_PTR, OP_CH_END = 0x2f, OP_C_TYPE = 0x30, OP_C_SKIP, OP_C_NONE, OP_C_DEF, OP_DEF_END,
	OP_PEND = 0x40, OP_PROJ = 0x50, OP_PROJ_END
};
struct Record {
	std::vector<uint8_t> code;
	std::vector<std::string> str;
	std::unordered_map<std::string, uint32_t> idx;
	std::vector<size_t> proj;
	uint32_t S(const char* p, size_t n) {
		std::string k(p ? p : "", p ? n : 0);
		auto it = idx.find(k);
		if (it != idx.end()) return it->second;
		const uint32_t i = (uint32_t)str.size();
		str.push_back(k);
		idx.emplace(std::move(k), i);
		return i;
	}
	uint32_t S(const char* p) { return S(p, p ? strlen(p) : 0); }
	uint32_t T(uintptr_t td) { return S(*(const char**)(td + 0x38), *(size_t*)(td + 0x40)); }
	void B(uint8_t v) { code.push_back(v); }
	void U(uint64_t v) { while (v >= 0x80) { code.push_back((uint8_t)(v | 0x80)); v >>= 7; } code.push_back((uint8_t)v); }
	void clear() { code.clear(); str.clear(); idx.clear(); proj.clear(); }
};
Record g_rec;
// the record's strings as the replay reads them: one blob of NUL-terminated strings (also the file's layout)
std::vector<char> g_blob;
std::vector<uint32_t> g_so, g_sl;
inline const char* SP(uint64_t i) { return i < g_so.size() ? g_blob.data() + g_so[i] : ""; }
inline size_t SL(uint64_t i) { return i < g_sl.size() ? g_sl[i] : 0; }
void Pack() {   // after recording: strings into the blob, the record-side pool freed
	size_t total = 0;
	for (auto& x : g_rec.str) total += x.size() + 1;
	g_blob.assign(total, 0);
	g_so.resize(g_rec.str.size());
	g_sl.resize(g_rec.str.size());
	size_t o = 0;
	for (size_t i = 0; i < g_rec.str.size(); i++) {
		g_so[i] = (uint32_t)o;
		g_sl[i] = (uint32_t)g_rec.str[i].size();
		memcpy(g_blob.data() + o, g_rec.str[i].data(), g_rec.str[i].size());
		o += g_rec.str[i].size() + 1;
	}
	std::vector<std::string>().swap(g_rec.str);
	std::unordered_map<std::string, uint32_t>().swap(g_rec.idx);
}
std::atomic<int> g_mode{0};   // 0 off, 1 record, 2 replay
bool g_have = false, g_failed = false;
std::string g_bad, g_fail;    // first reason the record cannot be used / the replay failed
long long g_c[16] = {};       // record counts (see Disarm)
enum { cElem, cAttr, cMd6, cCur, cSeq, cItem, cText, cObj, cType, cSkip, cDef, cPend, cAssoc, cLine, cPath, cProj };
uintptr_t g_recDeser = 0;
bool g_deser2Logged = false;
long long g_t0 = 0;

enum : uint8_t { F_ELEM, F_PROP, F_CHILD, F_SEQ, F_CREATE, F_DEF, F_CONV, F_NONE = 0xff };
struct Frame {
	uint8_t k;
	bool got, emitted, hasLookup, hasLine;
	uint8_t pathKind;
	void* P;
	uint32_t idx, items, lookup, path;
	int line;
	size_t countPos;
	uintptr_t dyn;
	uint64_t inst[4];
};
thread_local bool t_2a = false, t_rec = false, t_rep = false;
thread_local int t_proj = 0;
thread_local std::vector<Frame> t_fr;
inline uint8_t TopK() { return t_fr.empty() ? F_NONE : t_fr.back().k; }
inline void Push(uint8_t k) { Frame f{}; f.k = k; t_fr.push_back(f); }
void Bad(const char* why) { if (g_bad.empty()) { g_bad = why; Log("questreplay: record unusable: %s (at op byte %zu)", why, g_rec.code.size()); } }

// ---- record hooks (pass-through unless recording on this thread)
uint64_t __fastcall HkProp(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t ReplayRoot(uint64_t deser, uint64_t xmlRef, uint64_t inst, uint64_t merge);

uint64_t __fastcall HkElem(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h) {
	if (t_2a && !t_rep && g_mode.load(std::memory_order_relaxed) == 2) return ReplayRoot(a, b, c, d);
	if (!t_rec) return ORIG(kElem)(a, b, c, d, e, f, g, h);
	const uint8_t k = TopK();
	if (k == F_NONE) {
		g_rec.proj.push_back(g_rec.code.size());
		g_rec.B(OP_PROJ);
		g_c[cProj]++;
		if (!g_recDeser) g_recDeser = a;
	} else if (k != F_CHILD && k != F_DEF) Bad("element under an unexpected frame");
	uint64_t I[4];
	const uintptr_t dyn = DynOf((const uint64_t*)c, I);
	g_rec.B(OP_ELEM);
	g_rec.U(g_rec.T(dyn));
	g_c[cElem]++;
	Push(F_ELEM);
	t_fr.back().dyn = dyn;
	memcpy(t_fr.back().inst, (void*)c, 32);
	TI& ti = Ti(dyn);
	Handlers(a, dyn, ti);
	const uint64_t r = ORIG(kElem)(a, b, c, d, e, f, g, h);
	const Frame fr = t_fr.back();
	t_fr.pop_back();
	g_rec.B(OP_ELEM_END);
	g_rec.U(fr.hasLine ? (uint64_t)(uint32_t)fr.line + 1 : 0);
	if (ti.valid[5] && !fr.hasLine) Bad("slot 5 without a line");
	if (!(r & 0xff)) Bad("an element failed");
	if (k == F_NONE) g_rec.B(OP_PROJ_END);
	return r;
}

uint64_t __fastcall HkProp(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h) {
	if (!t_rec) return ORIG(kProp)(a, b, c, d, e, f, g, h);
	uint32_t idx = 0;
	if (TopK() != F_ELEM) Bad("property outside an element");
	else {
		TI& ti = Ti(t_fr.back().dyn);
		auto it = ti.idx.find((void*)d);
		if (it == ti.idx.end()) Bad("property not in its type's list");
		else idx = it->second;
	}
	Push(F_PROP);
	t_fr.back().P = (void*)d;
	t_fr.back().idx = idx;
	const uint64_t r = ORIG(kProp)(a, b, c, d, e, f, g, h);
	const Frame fr = t_fr.back();
	t_fr.pop_back();
	if (fr.got && !fr.emitted) { g_rec.B(OP_P_CUR); g_rec.U(fr.idx); g_c[cCur]++; }
	if (!(r & 0xff)) Bad("a property failed");
	return r;
}

uint64_t __fastcall HkGetValue(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h) {
	if (t_rec && TopK() == F_PROP && t_fr.back().P == *(void**)a && !t_fr.back().got && !t_fr.back().emitted) t_fr.back().got = true;
	return ORIG(kGetValue)(a, b, c, d, e, f, g, h);
}

// FUN_1804fb74c(unused, XmlNodeRef*, const char* name, variant* out, property*)
uint64_t __fastcall HkAttrConv(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h) {
	if (!t_rec) return ORIG(kAttrConv)(a, b, c, d, e, f, g, h);
	if (TopK() == F_PROP && t_fr.back().P == (void*)e) {
		Frame& p = t_fr.back();
		if (p.emitted) Bad("two conversions in one property");
		const char* name = (const char*)c;
		const char* s = XAttr(*(void**)b, name);
		g_rec.B(p.got ? OP_P_MD6 : OP_P_ATTR);
		g_rec.U(p.idx);
		g_rec.U(g_rec.S(name));
		g_rec.U(g_rec.S(s));
		g_c[p.got ? cMd6 : cAttr]++;
		p.emitted = true;
	} else if (TopK() != F_CONV) Bad("attribute conversion outside a property");
	Push(F_CONV);
	const uint64_t r = ORIG(kAttrConv)(a, b, c, d, e, f, g, h);
	t_fr.pop_back();
	return r;
}

// FUN_1810c1dd8(unused, string_view*, variant* out, type*): a sequence item's text
uint64_t __fastcall HkTextConv(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h) {
	if (!t_rec) return ORIG(kTextConv)(a, b, c, d, e, f, g, h);
	if (TopK() == F_SEQ) {
		const Sv* sv = (const Sv*)b;
		if (sv->n > (1u << 20)) Bad("sequence text longer than 1 MB (string_view layout?)");
		g_rec.B(OP_I_TEXT);
		g_rec.U(g_rec.S(sv->p, sv->n));
		t_fr.back().items++;
		g_c[cText]++;
	}
	Push(F_CONV);
	const uint64_t r = ORIG(kTextConv)(a, b, c, d, e, f, g, h);
	t_fr.pop_back();
	return r;
}

// FUN_1804f8b4c(deser, XmlNodeRef*, view*, set*, md7, append, merge)
uint64_t __fastcall HkSeq(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h) {
	if (!t_rec) return ORIG(kSeq)(a, b, c, d, e, f, g, h);
	uint32_t idx = 0;
	if (TopK() != F_PROP || !t_fr.back().got || t_fr.back().emitted) Bad("sequence outside a property");
	else { idx = t_fr.back().idx; t_fr.back().emitted = true; }
	if ((f & 0xff) || (g & 0xff)) Bad("sequence append / merge");
	if (AnyChildTag(*(void**)b, "null", false)) Bad("a <null> sequence item");
	g_rec.B(OP_P_SEQ);
	g_rec.U(idx);
	const size_t pos = g_rec.code.size();
	g_rec.code.insert(g_rec.code.end(), 4, 0);
	g_c[cSeq]++;
	Push(F_SEQ);
	const uint64_t r = ORIG(kSeq)(a, b, c, d, e, f, g, h);
	const uint32_t n = t_fr.back().items;
	t_fr.pop_back();
	memcpy(&g_rec.code[pos], &n, 4);
	g_rec.B(OP_SEQ_END);
	if (!(r & 0xff)) Bad("a sequence failed");
	return r;
}

// FUN_181e25110(deser, XmlNodeRef*, variant*, mode, merge)
uint64_t __fastcall HkChild(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h) {
	if (!t_rec) return ORIG(kChild)(a, b, c, d, e, f, g, h);
	const uint8_t k = TopK();
	if (k == F_PROP) {
		Frame& p = t_fr.back();
		if (!p.got || p.emitted) Bad("child object in a property twice");
		g_rec.B(OP_P_OBJ);
		g_rec.U(p.idx);
		p.emitted = true;
	} else if (k == F_SEQ) t_fr.back().items++;
	else Bad("child object under an unexpected frame");
	g_c[cObj]++;
	if (!IsPtr(TypeOf((Var*)c))) g_rec.B(OP_CH_VAL);
	else if (!strcmp(XTag(*(void**)b), "null")) g_rec.B(OP_CH_NULL);
	else { g_rec.B(OP_CH_PTR); g_rec.B((uint8_t)d); }
	Push(F_CHILD);
	const uint64_t r = ORIG(kChild)(a, b, c, d, e, f, g, h);
	t_fr.pop_back();
	g_rec.B(OP_CH_END);
	if (!(r & 0xff)) Bad("a child object failed");
	return r;
}

// FUN_181e38fa0(factory, variant* out, info*, optional*, u8* applyBody, XmlNodeRef*, u8* skipped, merge)
uint64_t __fastcall HkCreate(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h) {
	if (!t_rec) return ORIG(kCreate)(a, b, c, d, e, f, g, h);
	const uint8_t k = TopK();
	if (k != F_CHILD && k != F_DEF) Bad("Create under an unexpected frame");
	Push(F_CREATE);
	const uint64_t r = ORIG(kCreate)(a, b, c, d, e, f, g, h);
	const Frame fr = t_fr.back();
	t_fr.pop_back();
	if (!fr.emitted) {
		if (*(uint8_t*)g) { g_rec.B(OP_C_SKIP); g_c[cSkip]++; }
		else { g_rec.B(OP_C_NONE); Bad("Create made nothing"); }
	}
	return r;
}

uint64_t __fastcall HkTypesByName(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h) {
	if (t_rec && TopK() == F_CREATE) { t_fr.back().lookup = g_rec.S(*(const char**)b); t_fr.back().hasLookup = true; }
	return ORIG(kTypesByName)(a, b, c, d, e, f, g, h);
}

// FUN_18069c708(type*, variant* out, vector<argument>*)
uint64_t __fastcall HkTypeCreate(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h) {
	if (!t_rec) return ORIG(kTypeCreate)(a, b, c, d, e, f, g, h);
	if (TopK() == F_CREATE) {
		Frame& cr = t_fr.back();
		if (cr.emitted) Bad("two creations in one Create");
		if (!cr.hasLookup) Bad("a type created without its name lookup");
		g_rec.B(OP_C_TYPE);
		g_rec.U(cr.lookup);
		g_rec.U(g_rec.T(*(uintptr_t*)a));
		cr.emitted = true;
		g_c[cType]++;
	}
	Push(F_CONV);
	const uint64_t r = ORIG(kTypeCreate)(a, b, c, d, e, f, g, h);
	t_fr.pop_back();
	return r;
}

// FUN_1806906e0(factory, variant* out, XmlNodeRef* definition, info*, u8* applyBody)
uint64_t __fastcall HkDef(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h) {
	if (!t_rec) return ORIG(kDef)(a, b, c, d, e, f, g, h);
	if (TopK() != F_CREATE) Bad("definition outside a Create");
	else { if (t_fr.back().emitted) Bad("two creations in one Create"); t_fr.back().emitted = true; }
	if (!g_deser2Logged && g_recDeser) {
		g_deser2Logged = true;
		const uintptr_t d2 = *(uintptr_t*)(a + 0x30);
		Log("questreplay: the definition bodies' deserializer is %s the project's", d2 == g_recDeser ? "the same as" : "NOT");
	}
	void* node = *(void**)c;
	const char* tag = XTag(node);
	const uint8_t st = *(uint8_t*)(d + 0x19);
	g_rec.B(OP_C_DEF);
	g_rec.B(st);
	g_rec.U(st ? g_rec.S(*(const char**)d) : 0);
	uint64_t lvl = 0;
	uint8_t rep = 0;
	if (!strcmp(tag, "Level")) lvl = (uint64_t)g_rec.S(XAttr(node, "Name")) + 1;
	else if (!strcmp(tag, "RandomEventPlace")) rep = 1;
	g_rec.U(lvl);
	g_rec.B(rep);
	g_c[cDef]++;
	Push(F_DEF);
	const uint64_t r = ORIG(kDef)(a, b, c, d, e, f, g, h);
	const Frame fr = t_fr.back();
	t_fr.pop_back();
	g_rec.B(OP_DEF_END);
	g_rec.B(fr.pathKind);
	g_rec.U(fr.pathKind ? fr.path : 0);
	g_rec.B(Valid((Var*)b) ? 1 : 0);
	if (fr.pathKind) g_c[cPath]++;
	return r;
}

uint64_t __fastcall HkPush260(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h) {
	if (t_rec && TopK() == F_DEF) { t_fr.back().pathKind = 1; t_fr.back().path = g_rec.S(*(const char**)b); }
	return ORIG(kPush260)(a, b, c, d, e, f, g, h);
}
uint64_t __fastcall HkXSetPath(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h) {
	if (t_rec && TopK() == F_DEF) { t_fr.back().pathKind = 2; t_fr.back().path = g_rec.S(*(const char**)b); }
	return ORIG(kXSetPath)(a, b, c, d, e, f, g, h);
}

// FUN_180699fe0(deser, instance*, XmlNodeRef*): Edge / Constant / Asset children nobody consumed
uint64_t __fastcall HkPend(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h) {
	if (t_rec) {
		if (TopK() != F_ELEM) Bad("pending handler outside an element");
		else {
			if (memcmp((void*)b, t_fr.back().inst, 32)) Bad("pending handler: not the element's instance");
			void* node = *(void**)c;
			const char* tag = XTag(node);
			int kind = -1;
			if (!strcmp(tag, "Edge")) kind = 0;
			else if (!strcmp(tag, "Constant")) kind = 1;
			else if (!strcmp(tag, "Asset")) kind = 2;
			if (kind >= 0) {
				g_rec.B(OP_PEND);
				g_rec.B((uint8_t)kind);
				g_rec.U(g_rec.S(XAttr(node, kind ? "Name" : "From")));
				g_rec.U(g_rec.S(XAttr(node, kind == 0 ? "To" : kind == 1 ? "Value" : "Alias")));
				g_c[cPend]++;
			}
		}
	}
	return ORIG(kPend)(a, b, c, d, e, f, g, h);
}

// slot 5 (ProcessDeserializationLineNumber, C_Objective): FUN_1806aa718(method*, ret, instance*, argument{int*, ...}*)
uint64_t __fastcall HkInvoke1(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h) {
	if (t_rec && TopK() == F_ELEM) {
		Frame& el = t_fr.back();
		TI& ti = Ti(el.dyn);
		if (ti.valid[5] && *(uintptr_t*)a == ti.m[5]) {
			el.hasLine = true;
			el.line = **(int**)d;
			g_c[cLine]++;
		}
	}
	return ORIG(kInvoke1)(a, b, c, d, e, f, g, h);
}

uint64_t AssocCommon(int i, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h) {
	if (t_rec) {
		g_c[cAssoc]++;
		if (TopK() != F_PROP) Bad("associative outside a property");
		if (AnyChildTag(*(void**)b, "Pair", i == kAssocSet)) Bad("an associative container with items");
		Push(F_CONV);
	}
	const uint64_t r = ORIG(i)(a, b, c, d, e, f, g, h);
	if (t_rec) t_fr.pop_back();
	return r;
}
uint64_t __fastcall HkAssocMap(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h) {
	return AssocCommon(kAssocMap, a, b, c, d, e, f, g, h);
}
uint64_t __fastcall HkAssocSet(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h) {
	return AssocCommon(kAssocSet, a, b, c, d, e, f, g, h);
}

// resolver id order (the dump prints weak ids as paths): Register gets the next ordinal of this build, Unregister names the
// ordinal of the object whose id it frees (or the id itself for an object from before the build). Equal hashes = the same
// Register / Unregister sequence, so the same ids from the same free list.
thread_local bool t_ids = false;
std::unordered_map<uint32_t, uint32_t> g_idOrd;
uint64_t g_idHash = 0;
uint32_t g_idReg = 0, g_idUnreg = 0, g_idOld = 0;
inline void IdMix(uint64_t v) { g_idHash = (g_idHash ^ v) * 0x100000001b3ull; }
uint64_t __fastcall HkRegister(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h) {
	const uint64_t r = ORIG(kRegister)(a, b, c, d, e, f, g, h);
	if (t_ids) {
		const uint32_t id = *(uint32_t*)(b + 8);
		g_idOrd[id] = g_idReg;
		IdMix(0x52000000ull + g_idReg++);
	}
	return r;
}
uint64_t __fastcall HkUnregister(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h) {
	if (t_ids) {
		const uint32_t id = (uint32_t)b;
		auto it = g_idOrd.find(id);
		if (it != g_idOrd.end()) { IdMix(0x55000000ull + it->second); g_idOrd.erase(it); }
		else { IdMix(0x4f00000000ull + id); g_idOld++; }
		g_idUnreg++;
	}
	return ORIG(kUnregister)(a, b, c, d, e, f, g, h);
}

void* const kHk[kHookN] = { (void*)&HkElem, (void*)&HkProp, (void*)&HkChild, (void*)&HkSeq, (void*)&HkCreate, (void*)&HkDef,
                            (void*)&HkTypeCreate, (void*)&HkAttrConv, (void*)&HkTextConv, (void*)&HkGetValue, (void*)&HkPend,
                            (void*)&HkAssocMap, (void*)&HkAssocSet, (void*)&HkTypesByName, (void*)&HkPush260, (void*)&HkXSetPath,
                            (void*)&HkInvoke1, (void*)&HkRegister, (void*)&HkUnregister };

// ---- stand-in XML node: getTag (vt+0x30), getAttr (vt+0x108), AddRef / Release (vt+0x18 / +0x20); anything else is noted
struct Stub { void** vt; const char* tag; const char* n1; const char* v1; const char* n2; const char* v2; };
std::atomic<int> g_stubBad{0};
uint64_t __fastcall StubTrap(void*) { g_stubBad++; return 0; }
uint64_t __fastcall StubNop(void*) { return 0; }
const char* __fastcall StubTag(Stub* s) { return s->tag; }
const char* __fastcall StubAttr(Stub* s, const char* k) {
	if (k && s->n1 && !strcmp(k, s->n1)) return s->v1;
	if (k && s->n2 && !strcmp(k, s->n2)) return s->v2;
	g_stubBad++;
	return "";
}
void* g_stubVt[64];

// ---- replay
const uint8_t* g_p = nullptr;
const uint8_t* g_end = nullptr;
std::vector<uintptr_t> g_tdOf;     // record type string -> the element type seen in this build
std::vector<uintptr_t> g_created;  // record lookup string -> the type it resolves to in this build
long long g_rOps = 0;

bool Fail(const char* why) {
	if (g_fail.empty()) {
		g_fail = why;
		Log("questreplay: replay failed: %s (op byte %zu of %zu)", why, (size_t)(g_p - g_rec.code.data()), g_rec.code.size());
	}
	return false;
}
inline uint8_t Peek() { return g_p < g_end ? *g_p : 0; }
inline uint8_t Op() { g_rOps++; return g_p < g_end ? *g_p++ : 0; }
inline uint8_t B8() { return g_p < g_end ? *g_p++ : 0; }
inline uint64_t U() {
	uint64_t v = 0;
	int s = 0;
	while (g_p < g_end) { const uint8_t b = *g_p++; v |= (uint64_t)(b & 0x7f) << s; if (!(b & 0x80)) break; s += 7; }
	return v;
}

bool RElem(uintptr_t deser, const uint64_t* in);
bool RChild(uintptr_t deser, Var* pv, int mode);

void Invoke0(uintptr_t* m, const uint64_t* orig) {
	uint64_t ic[4];
	memcpy(ic, orig, 32);
	Var r;
	((Var*(__fastcall*)(uintptr_t*, Var*, uint64_t*))F(kInvoke0))(m, &r, ic);
	((PolFn)r.pol)(0, &r, nullptr);
}

bool AttrConv(void* P, const char* name, const char* value, Var* v) {   // FUN_1804fb74c with a stand-in node
	Stub st{ g_stubVt, "", name, value, nullptr, nullptr };
	void* ref = &st;
	const int bad0 = g_stubBad.load();
	const bool ok = ((uint8_t(__fastcall*)(uint64_t, void**, const char*, Var*, void*))F(kAttrConv))(0, &ref, name, v, P) != 0;
	if (g_stubBad.load() != bad0) return Fail("the attribute converter asked the stand-in node for something else");
	return ok || Fail("attribute conversion failed");
}

bool RProp(uintptr_t deser, const uint64_t* I, TI& ti) {
	const uint8_t op = Op();
	const uint64_t idx = U();
	if (idx >= ti.props.size()) return Fail("property index out of range");
	void* P = ti.props[idx];
	PI& pi = Pi(P);
	void** vt = *(void***)P;
	using SetFn = uint8_t(__fastcall*)(void*, uint64_t*, void*);
	using GetFn = Var*(__fastcall*)(void*, Var*, uint64_t*);
	uint64_t ic[4];
	alignas(16) uint8_t arg[0x20];
	if (op == OP_P_ATTR) {   // the element's own attribute: no get_value, set_value without the wrapper check (S)
		const char* name = SP(U());
		const char* value = SP(U());
		Var v = Empty();
		if (!AttrConv(P, name, value, &v)) { Destroy(&v); return false; }
		MakeArg(arg, &v);
		memcpy(ic, I, 32);
		((SetFn)vt[0x40 / 8])(P, ic, arg);
		Destroy(&v);
		return true;
	}
	Var v;
	memcpy(ic, I, 32);
	((GetFn)vt[0x48 / 8])(P, &v, ic);
	bool ok = true;
	switch (op) {
	case OP_P_MD6: { const char* name = SP(U()); const char* value = SP(U()); ok = AttrConv(P, name, value, &v); break; }
	case OP_P_CUR: break;
	case OP_P_SEQ: {
		uint32_t n = 0;
		if (g_end - g_p < 4) { ok = Fail("truncated"); break; }
		memcpy(&n, g_p, 4);
		g_p += 4;
		alignas(16) uint8_t view[0xc0] = {};
		((void*(__fastcall*)(Var*, void*))F(kSeqView))(&v, view);
		using SetSize = uint8_t(__fastcall*)(void*, size_t);
		using Get = Var*(__fastcall*)(void*, Var*, size_t);
		using Set = uint8_t(__fastcall*)(void*, size_t, void*);
		((SetSize)F(kViewSetSize))(view, n);
		size_t esi = 0;
		uint32_t items = 0;
		while (ok) {
			const uint8_t o = Peek();
			if (o == OP_SEQ_END) { Op(); break; }
			items++;
			Var ev;
			((Get)F(kViewGet))(view, &ev, esi);
			if (o == OP_I_TEXT) {
				Op();
				const uint64_t si = U();
				uintptr_t T = *(uintptr_t*)(TypeOf(&ev) + 8);
				Var tmp = Empty();
				Sv sv{ SP(si), SL(si) };
				if (!((uint8_t(__fastcall*)(uint64_t, Sv*, Var*, uintptr_t*))F(kTextConv))(0, &sv, &tmp, &T)) ok = Fail("sequence text conversion");
				else { MakeArg(arg, &tmp); ((Set)F(kViewSet))(view, esi, arg); esi++; }
				Destroy(&tmp);
			} else if (!RChild(deser, &ev, 1)) ok = false;
			else if (Valid(&ev)) {
				if (!WrapNonPtr(TypeOf(&ev))) { MakeArg(arg, &ev); ((Set)F(kViewSet))(view, esi, arg); }
				esi++;
			}
			Destroy(&ev);
		}
		if (ok && items != n) ok = Fail("sequence item count differs");
		if (ok) ((SetSize)F(kViewSetSize))(view, esi);
		break;
	}
	case OP_P_OBJ: ok = RChild(deser, &v, 0); break;
	default: ok = Fail("unknown property op");
	}
	if (ok && !pi.cv4) {
		MakeArg(arg, &v);
		memcpy(ic, I, 32);
		((SetFn)vt[0x40 / 8])(P, ic, arg);
	}
	Destroy(&v);
	return ok;
}

uintptr_t ResolveType(uint64_t lookup, uint64_t tdName) {   // Create's type path (S): types by name, filter, exactly one
	if (lookup < g_created.size() && g_created[lookup]) return g_created[lookup];
	uintptr_t cs = 0;
	CryFrom(&cs, SP(lookup));
	alignas(16) uint8_t list[0x80] = {}, filt[0x80] = {};
	((void*(__fastcall*)(void*, void*))F(kTypesByName))(list, &cs);
	((void*(__fastcall*)(void*, void*))F(kFilter))(filt, list);
	uintptr_t* v = (uintptr_t*)(filt + 0x20);
	if (v[0] == v[1]) v = (uintptr_t*)(list + 0x20);
	uintptr_t td = 0;
	if (v[0] != v[1] && (v[1] - v[0]) / 8 == 1) td = *(uintptr_t*)v[0];
	((void(__fastcall*)(void*))F(kListDestroy))(filt);
	((void(__fastcall*)(void*))F(kListDestroy))(list);
	CryRelease(&cs);
	if (td && !NameIs(td, SP(tdName), SL(tdName))) td = 0;
	if (td) {
		if (g_created.size() <= lookup) g_created.resize(lookup + 1, 0);
		g_created[lookup] = td;
	}
	return td;
}

bool RCreate(uintptr_t deser, Var* out, bool* skipped) {   // out is uninitialised; on return it holds a variant
	*skipped = false;
	const uint8_t op = Op();
	const uintptr_t empty = g_base + kFn[kEmptyPol].rva;
	if (op == OP_C_SKIP) { out->pol = empty; *skipped = true; return true; }
	if (op == OP_C_TYPE) {
		const uint64_t lk = U(), tn = U();
		uintptr_t td = ResolveType(lk, tn);
		if (!td) { out->pol = empty; return Fail("type not found by name"); }
		uint64_t args[3] = {};
		Var tmp;
		((Var*(__fastcall*)(uintptr_t*, Var*, uint64_t*))F(kTypeCreate))(&td, &tmp, args);
		((Var*(__fastcall*)(Var*, Var*))F(kClone))(out, &tmp);
		Destroy(&tmp);
		return true;
	}
	if (op != OP_C_DEF) { out->pol = empty; return Fail("unknown create op"); }
	// FUN_1806906e0 (S)
	const uint8_t st = B8();
	const uint64_t tag = U(), lvl = U();
	const uint8_t rep = B8();
	const uintptr_t factory = *(uintptr_t*)(deser + 8);
	Var v70;
	bool innerSkip = false;
	if (!RCreate(deser, &v70, &innerSkip)) { out->pol = empty; return false; }
	bool ok = true, made = false;
	if (Valid(&v70)) {
		if (st) {
			uintptr_t sp = 0, cs = 0;
			((void*(__fastcall*)(Var*, uintptr_t*))F(kStaticSp))(&v70, &sp);
			CryFrom(&cs, SP(tag));
			((void(__fastcall*)(uintptr_t, void*))F(kStaticName))(sp, &cs);
			CryRelease(&cs);
			((Var*(__fastcall*)(Var*, Var*))F(kClone))(out, &v70);
			((void(__fastcall*)(uintptr_t*))F(kSpRelease))(&sp);
			made = true;
		} else {
			if (lvl) ((void*(__fastcall*)(void*, const char*))F(kCryAssign))((void*)(factory + 0x38), SP(lvl - 1));
			else if (rep) *(uint8_t*)(factory + 0x40) = 1;
			const uintptr_t deser2 = *(uintptr_t*)(factory + 0x30);
			uint64_t inst[4];
			((uint64_t*(__fastcall*)(uint64_t*, Var*))F(kInstFromVar))(inst, &v70);
			ok = RElem(deser2, inst);
			if (lvl) ((void(__fastcall*)(void*))F(kCryClear))((void*)(factory + 0x38));
			else if (rep) *(uint8_t*)(factory + 0x40) = 0;
			made = ok;
		}
	}
	if (ok && Op() != OP_DEF_END) ok = Fail("expected the definition end");
	if (ok) {
		const uint8_t pk = B8();
		const uint64_t path = U();
		const uint8_t recValid = B8();
		if (made && !st) {
			const bool node = ((uint8_t(__fastcall*)(Var*))F(kCanBeNode))(&v70) != 0;
			const bool user = !node && ((uint8_t(__fastcall*)(Var*))F(kCanBeX))(&v70) != 0;
			if ((pk == 1) != node || (pk == 2) != user) ok = Fail("definition path kind differs");
			else if (pk == 1) {
				uintptr_t sp = 0, cs = 0;
				((void*(__fastcall*)(Var*, uintptr_t*))F(kNodeSp))(&v70, &sp);
				CryFrom(&cs, SP(path));
				((void(__fastcall*)(uintptr_t, void*))(*(void***)sp)[0x170 / 8])(sp, &cs);
				((void(__fastcall*)(uintptr_t, void*, uintptr_t*))F(kPush260))(*(uintptr_t*)(factory + 0x30), &cs, &sp);
				CryRelease(&cs);
				((void(__fastcall*)(uintptr_t*))F(kSp2Release))(&sp);
			} else if (pk == 2) {
				uintptr_t sp = 0, cs = 0;
				((void*(__fastcall*)(Var*, uintptr_t*))F(kXSp))(&v70, &sp);
				CryFrom(&cs, SP(path));
				((void(__fastcall*)(uintptr_t, void*))F(kXSetPath))(sp, &cs);
				CryRelease(&cs);
				((void(__fastcall*)(uintptr_t*))F(kSp2Release))(&sp);
			}
			if (ok) ((Var*(__fastcall*)(Var*, Var*))F(kClone))(out, &v70);
		}
		if (ok && (made ? 1 : 0) != recValid) ok = Fail("definition result differs");
	}
	if (!made || !ok) out->pol = empty;
	Destroy(&v70);
	return ok;
}

bool RChild(uintptr_t deser, Var* pv, int mode) {   // FUN_181e25110 (S)
	const uint8_t op = Op();
	const uintptr_t typ0 = TypeOf(pv);
	const bool ptr = IsPtr(typ0);
	bool ok = true;
	if (op == OP_CH_VAL) {
		if (ptr) return Fail("child object: a pointer where the record has a value");
		uint64_t inst[4];
		((uint64_t*(__fastcall*)(Var*, uint64_t*))F(kVarInst))(pv, inst);
		ok = RElem(deser, inst);
	} else if (op == OP_CH_NULL) {
		if (!ptr) return Fail("child object: a value where the record has null");
	} else if (op == OP_CH_PTR) {
		if (!ptr) return Fail("child object: a value where the record has a pointer");
		if (B8() != mode) return Fail("child object mode differs");
		uintptr_t typ = typ0;
		Var ex;
		bool hasEx = false;
		if (mode == 1) {   // the existing item, held until after the conversion (vanilla: an optional)
			((Var*(__fastcall*)(Var*, Var*))F(kExtract))(pv, &ex);
			hasEx = true;
			typ = *(uintptr_t*)(typ + 8);
		}
		Var out;
		bool skipped = false;
		ok = RCreate(deser, &out, &skipped);
		((Var*(__fastcall*)(Var*, Var*))F(kMove))(pv, &out);
		Destroy(&out);
		if (ok) {
			if (!Valid(pv)) {
				if (hasEx) Destroy(&ex);
				hasEx = false;
				if (!skipped) ok = Fail("child object: nothing created");
			} else {
				uintptr_t t = typ;
				if (WrapNonPtr(t)) t = *(uintptr_t*)(t + 8);
				if (!((uint8_t(__fastcall*)(Var*, uintptr_t*, Var*))F(kConvert))(pv, &t, pv)) ok = Fail("child object conversion");
				if (hasEx) Destroy(&ex);
				hasEx = false;
				if (ok) {
					uint64_t inst[4];
					((uint64_t*(__fastcall*)(Var*, uint64_t*))F(kVarInst))(pv, inst);
					ok = RElem(deser, inst);
				}
			}
		}
		if (hasEx) Destroy(&ex);
	} else return Fail("unknown child op");
	if (ok && Op() != OP_CH_END) return Fail("expected the child object end");
	return ok;
}

bool RPend(uintptr_t deser, const uint64_t* orig) {   // the deserializer's own handler (vt+0x10) with a stand-in node
	Op();
	const uint8_t kind = B8();
	const char* a = SP(U());
	const char* b = SP(U());
	Stub st{ g_stubVt, kind == 0 ? "Edge" : kind == 1 ? "Constant" : "Asset", kind ? "Name" : "From", a,
	         kind == 0 ? "To" : kind == 1 ? "Value" : "Alias", b };
	void* ref = &st;
	uint64_t ic[4];
	memcpy(ic, orig, 32);
	const int bad0 = g_stubBad.load();
	((void(__fastcall*)(uintptr_t, uint64_t*, void**))(*(void***)deser)[0x10 / 8])(deser, ic, &ref);
	if (g_stubBad.load() != bad0) return Fail("the pending handler asked the stand-in node for something else");
	return true;
}

bool RElem(uintptr_t deser, const uint64_t* in) {   // FUN_181e25520 (S)
	if (Op() != OP_ELEM) return Fail("expected an element");
	const uint64_t ts = U();
	uint64_t I[4];
	const uintptr_t dyn = DynOf(in, I);
	if (ts >= g_tdOf.size()) g_tdOf.resize(ts + 1, 0);
	if (g_tdOf[ts] != dyn) {
		if (!dyn || !NameIs(dyn, SP(ts), SL(ts))) return Fail("element type differs from the record");
		g_tdOf[ts] = dyn;
	}
	TI& ti = Ti(dyn);
	Handlers(deser, dyn, ti);
	if (ti.valid[2]) Invoke0(&ti.m[2], in);
	for (;;) {
		const uint8_t op = Peek();
		if (op == OP_P_ATTR || op == OP_P_MD6 || op == OP_P_CUR || op == OP_P_SEQ || op == OP_P_OBJ) {
			if (!RProp(deser, I, ti)) return false;
		} else break;
	}
	while (Peek() == OP_PEND)
		if (!RPend(deser, in)) return false;
	if (Op() != OP_ELEM_END) return Fail("expected the element end");
	const uint64_t line1 = U();
	if (ti.valid[5]) {
		if (!line1) return Fail("slot 5 without a recorded line");
		int line = (int)(line1 - 1);
		alignas(16) uint8_t arg[0x20];
		((void*(__fastcall*)(void*, int*))F(kArgInt))(arg, &line);
		uint64_t ic[4];
		memcpy(ic, in, 32);
		Var r;
		((Var*(__fastcall*)(uintptr_t*, Var*, uint64_t*, void*))F(kInvoke1))(&ti.m[5], &r, ic, arg);
		((PolFn)r.pol)(0, &r, nullptr);
	} else if (line1) return Fail("a recorded line without slot 5");
	if (ti.valid[4]) Invoke0(&ti.m[4], in);
	return true;
}

bool SafeReplay(uintptr_t deser, const uint64_t* in, DWORD* code) {
	__try { return RElem(deser, in); }
	__except (*code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) { return false; }
}

uint64_t ReplayRoot(uint64_t deser, uint64_t xmlRef, uint64_t inst, uint64_t merge) {
	const int pi = t_proj++;
	const uintptr_t* vt = *(const uintptr_t* const*)deser;
	const bool vtOk = vt[0x10 / 8] == g_base + kFn[kPend].rva && vt[0x18 / 8] == g_base + kFn[kDeserVt18].rva;
	if (pi >= (int)g_rec.proj.size() || !vtOk) {
		Log("questreplay: project %d: %s: the game's own build", pi, !vtOk ? "another deserializer class" : "not in the record");
		t_rep = true;   // its nested elements are the game's too
		const uint64_t r = ORIG(kElem)(deser, xmlRef, inst, merge, 0, 0, 0, 0);
		t_rep = false;
		return r;
	}
	g_p = g_rec.code.data() + g_rec.proj[pi];
	g_end = g_rec.code.data() + g_rec.code.size();
	const long long t0 = Now();
	const long long ops0 = g_rOps;
	bool ok = Op() == OP_PROJ || Fail("expected a project");
	DWORD ex = 0;
	t_rep = true;
	if (ok) ok = SafeReplay(deser, (const uint64_t*)inst, &ex);
	t_rep = false;
	if (ex) { char m[64]; snprintf(m, sizeof m, "exception 0x%08lx", ex); Fail(m); ok = false; }
	if (ok && Op() != OP_PROJ_END) ok = Fail("expected the project end");
	((void(__fastcall*)(uint64_t))F(kXmlRelease))(xmlRef);
	if (!ok) g_failed = true;
	Log("questreplay: project %d replayed %s in %.0f ms (%lld ops)", pi, ok ? "OK" : "with a FAILURE", (Now() - t0) * 1000.0 / g_freq,
	    g_rOps - ops0);
	return ok ? 1 : 0;
}

void Enable(bool on, bool all) {
	for (int i = 0; i < kHookN; i++)
		if (g_orig[i] && (all || i == kElem || i == kRegister || i == kUnregister))
			(on ? MH_QueueEnableHook : MH_QueueDisableHook)((void*)(g_base + kFn[i].rva));
	MH_ApplyQueued();
}

// ---- the record file (deagle_questreplay.bin next to the plugin) and its key
constexpr uint32_t kFileVer = 1;
struct Hdr { char magic[4]; uint32_t ver; uint64_t key; uint32_t nStr, nProj; uint64_t blob, code; };
std::string g_dir;
HANDLE g_preloaded = nullptr;
struct Loaded { bool ok = false; Hdr h{}; std::vector<char> blob; std::vector<uint32_t> so, sl; std::vector<size_t> proj; std::vector<uint8_t> code; };
Loaded g_file;
uint64_t g_key = 0, g_memKey = 0;   // the key of these inputs / of the record in memory (0: not from a key)
std::string g_keyWhy;

inline void Mix(uint64_t& h, const void* p, size_t n) {
	for (size_t i = 0; i < n; i++) h = (h ^ ((const uint8_t*)p)[i]) * 0x100000001b3ull;
}
inline void MixS(uint64_t& h, const std::string& s) { Mix(h, s.data(), s.size()); Mix(h, "\0", 1); }

void MixDir(uint64_t& h, const std::string& dir, const char* pattern, int* files) {   // names, sizes, write times (sorted)
	std::vector<std::string> v;
	WIN32_FIND_DATAA fd;
	HANDLE f = FindFirstFileA((dir + pattern).c_str(), &fd);
	if (f == INVALID_HANDLE_VALUE) return;
	do {
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
		char b[512];
		snprintf(b, sizeof b, "%s|%lu|%lu|%lu|%lu", fd.cFileName, fd.nFileSizeHigh, fd.nFileSizeLow, fd.ftLastWriteTime.dwHighDateTime,
		         fd.ftLastWriteTime.dwLowDateTime);
		v.emplace_back(b);
	} while (FindNextFileA(f, &fd));
	FindClose(f);
	std::sort(v.begin(), v.end());
	MixS(h, dir);
	for (auto& x : v) MixS(h, x);
	*files += (int)v.size();
}

void LoadFile() {
	FILE* f = fopen((g_dir + "deagle_questreplay.bin").c_str(), "rb");
	if (f) {
		Loaded& L = g_file;
		bool ok = fread(&L.h, sizeof L.h, 1, f) == 1 && !memcmp(L.h.magic, "DQRP", 4) && L.h.ver == kFileVer &&
		          L.h.nStr < (1u << 26) && L.h.nProj < 4096 && L.h.blob < (1ull << 31) && L.h.code < (1ull << 31);
		if (ok) {
			L.so.resize(L.h.nStr); L.sl.resize(L.h.nStr); L.proj.resize(L.h.nProj); L.blob.resize(L.h.blob); L.code.resize(L.h.code);
			std::vector<uint64_t> pj(L.h.nProj);
			ok = (!L.h.nStr || (fread(L.so.data(), 4, L.h.nStr, f) == L.h.nStr && fread(L.sl.data(), 4, L.h.nStr, f) == L.h.nStr)) &&
			     (!L.h.nProj || fread(pj.data(), 8, L.h.nProj, f) == L.h.nProj) &&
			     (!L.h.blob || fread(L.blob.data(), 1, L.h.blob, f) == L.h.blob) && (!L.h.code || fread(L.code.data(), 1, L.h.code, f) == L.h.code);
			for (size_t i = 0; ok && i < pj.size(); i++) { L.proj[i] = (size_t)pj[i]; ok = pj[i] < L.h.code; }
			for (size_t i = 0; ok && i < L.so.size(); i++) ok = (uint64_t)L.so[i] + L.sl[i] < L.h.blob;
		}
		fclose(f);
		L.ok = ok;
		if (!ok) { Loaded e; std::swap(L, e); Log("questreplay: deagle_questreplay.bin unreadable (another version?): ignored"); }
	}
}
DWORD WINAPI PreloadThread(LPVOID) {
	LoadFile();
	SetEvent(g_preloaded);
	return 0;
}

}  // namespace

void Questreplay_Preload(const char* pluginDir) {
	g_dir = pluginDir ? pluginDir : "";
	g_preloaded = CreateEventA(nullptr, TRUE, FALSE, nullptr);
	HANDLE t = CreateThread(nullptr, 0, PreloadThread, nullptr, 0, nullptr);
	if (t) CloseHandle(t);
	else SetEvent(g_preloaded);
}

int Questreplay_Plan(const char* inputs) {
	if (!g_hooked || g_mode.load()) return 0;
	const long long t0 = Now();
	uint64_t h = 0xcbf29ce484222325ull;
	Mix(h, "DQRP", 4);
	Mix(h, &kFileVer, 4);
	const uintptr_t nt = g_base + *(int*)(g_base + 0x3c);
	Mix(h, (void*)(nt + 8), 4);      // WHGame.dll TimeDateStamp
	Mix(h, (void*)(nt + 0x50), 4);   // SizeOfImage
	MixS(h, inputs ? inputs : "");
	const std::string root = g_dir + "..\\..\\";
	int files = 0;
	MixDir(h, root + "Data\\", "*.pak", &files);
	WIN32_FIND_DATAA fd;
	HANDLE f = FindFirstFileA((root + "Mods\\*").c_str(), &fd);
	if (f != INVALID_HANDLE_VALUE) {
		std::vector<std::string> mods;
		do {
			if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != '.') mods.emplace_back(fd.cFileName);
		} while (FindNextFileA(f, &fd));
		FindClose(f);
		std::sort(mods.begin(), mods.end());
		for (auto& m : mods) {
			MixDir(h, root + "Mods\\" + m + "\\Data\\", "*.pak", &files);
			MixDir(h, root + "Mods\\" + m + "\\", "*.manifest", &files);
		}
	}
	MixDir(h, root + "Mods\\", "mod_order.txt", &files);
	g_key = h;
	if (g_have && g_memKey == h) {
		Log("questreplay: key #%016llx (%.0f ms): the record in memory matches: this build replays", (unsigned long long)h,
		    (Now() - t0) * 1000.0 / g_freq);
		return 2;
	}
	if (g_preloaded) WaitForSingleObject(g_preloaded, 10000);
	if (!g_file.ok) LoadFile();   // released after an earlier build (questbench N file)
	const bool match = g_file.ok && g_file.h.key == h;
	Log("questreplay: key #%016llx over %d paks / manifests + the job inputs (%.0f ms); %s", (unsigned long long)h, files,
	    (Now() - t0) * 1000.0 / g_freq, match ? "deagle_questreplay.bin matches: this build replays" :
	    g_file.ok ? "deagle_questreplay.bin is for other inputs: this build records" : "no record file: this build records");
	if (match) {
		g_rec.clear();
		g_rec.code.swap(g_file.code);
		g_rec.proj.swap(g_file.proj);
		g_blob.swap(g_file.blob);
		g_so.swap(g_file.so);
		g_sl.swap(g_file.sl);
		Loaded e;
		std::swap(g_file, e);
		g_have = true;
		g_memKey = h;
		return 2;
	}
	{ Loaded e; std::swap(g_file, e); }
	return 1;
}

bool Questreplay_Save() {
	if (!g_have || !g_key) return false;
	const long long t0 = Now();
	const std::string out = g_dir + "deagle_questreplay.bin", tmp = out + ".tmp";
	FILE* f = fopen(tmp.c_str(), "wb");
	if (!f) { Log("questreplay: cannot write %s", tmp.c_str()); return false; }
	Hdr h{};
	memcpy(h.magic, "DQRP", 4);
	h.ver = kFileVer;
	h.key = g_key;
	h.nStr = (uint32_t)g_so.size();
	h.nProj = (uint32_t)g_rec.proj.size();
	h.blob = g_blob.size();
	h.code = g_rec.code.size();
	std::vector<uint64_t> pj(g_rec.proj.begin(), g_rec.proj.end());
	bool ok = fwrite(&h, sizeof h, 1, f) == 1;
	if (ok && h.nStr) ok = fwrite(g_so.data(), 4, h.nStr, f) == h.nStr && fwrite(g_sl.data(), 4, h.nStr, f) == h.nStr;
	if (ok && h.nProj) ok = fwrite(pj.data(), 8, h.nProj, f) == h.nProj;
	if (ok && h.blob) ok = fwrite(g_blob.data(), 1, h.blob, f) == h.blob;
	if (ok && h.code) ok = fwrite(g_rec.code.data(), 1, h.code, f) == h.code;
	ok = fclose(f) == 0 && ok;
	if (ok) ok = MoveFileExA(tmp.c_str(), out.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
	else DeleteFileA(tmp.c_str());
	if (ok) g_memKey = g_key;
	Log("questreplay: %s deagle_questreplay.bin (%.1f MB, key #%016llx) in %.0f ms", ok ? "wrote" : "could NOT write",
	    (sizeof h + h.nStr * 8.0 + h.nProj * 8.0 + h.blob + h.code) / 1048576.0, (unsigned long long)g_key, (Now() - t0) * 1000.0 / g_freq);
	return ok;
}

void Questreplay_Drop() {
	DeleteFileA((g_dir + "deagle_questreplay.bin").c_str());
	g_have = false;
	g_memKey = 0;
	Log("questreplay: record dropped (deagle_questreplay.bin deleted): the next session records again");
}

void Questreplay_Release() {   // the record is not needed after the menu build (questbench rec records its own)
	if (g_mode.load()) return;
	if (g_preloaded) WaitForSingleObject(g_preloaded, 10000);
	{ Loaded e; std::swap(g_file, e); }
	g_have = false;
	g_memKey = 0;
	std::vector<uint8_t>().swap(g_rec.code);
	std::vector<size_t>().swap(g_rec.proj);
	std::vector<char>().swap(g_blob);
	std::vector<uint32_t>().swap(g_so);
	std::vector<uint32_t>().swap(g_sl);
	std::unordered_map<uintptr_t, TI>().swap(g_ti);
	std::unordered_map<void*, PI>().swap(g_pi);
}

void Questreplay_Install(uintptr_t base) {
	g_base = base;
	LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = f.QuadPart;
	std::string bad;
	for (int i = 0; i < kFnN; i++)
		if (memcmp((void*)(base + kFn[i].rva), kFn[i].b, 16)) bad += std::string(bad.empty() ? "" : ", ") + kFn[i].name;
	if (!bad.empty()) { Log("questreplay: OFF (code differs: %s)", bad.c_str()); return; }
	int ok = 0;
	for (int i = 0; i < kHookN; i++)
		if (MH_CreateHook((void*)(base + kFn[i].rva), kHk[i], &g_orig[i]) == MH_OK) ok++;
		else g_orig[i] = nullptr;
	if (ok != kHookN) {
		for (int i = 0; i < kHookN; i++)
			if (g_orig[i]) MH_RemoveHook((void*)(base + kFn[i].rva)), g_orig[i] = nullptr;
		Log("questreplay: OFF (%d of %d hooks)", ok, kHookN);
		return;
	}
	for (auto& p : g_stubVt) p = (void*)&StubTrap;
	g_stubVt[0x18 / 8] = (void*)&StubNop;
	g_stubVt[0x20 / 8] = (void*)&StubNop;
	g_stubVt[0x30 / 8] = (void*)&StubTag;
	g_stubVt[0x108 / 8] = (void*)&StubAttr;
	g_hooked = true;
	Log("questreplay: ready (%d hooks, off until questbench N rec)", kHookN);
}

void Questreplay_Phase2a(bool enter) {
	t_2a = enter;
	t_rec = enter && g_mode.load() == 1;
	t_ids = enter && g_mode.load() != 0;
	if (enter) t_fr.clear();
}

bool Questreplay_Arm(int mode) {
	if (!g_hooked || g_mode.load()) return false;
	if (mode == 2 && !g_have) return false;
	g_ti.clear();
	g_pi.clear();
	g_tdOf.clear();
	g_created.clear();
	g_fail.clear();
	g_failed = false;
	g_rOps = 0;
	t_proj = 0;
	g_idOrd.clear();
	g_idHash = 0xcbf29ce484222325ull;
	g_idReg = g_idUnreg = g_idOld = 0;
	if (mode == 1) {
		g_rec.clear();
		g_rec.code.reserve(64u << 20);
		g_bad.clear();
		memset(g_c, 0, sizeof g_c);
		g_recDeser = 0;
		g_have = false;
		g_memKey = 0;
	}
	Enable(true, mode == 1);
	g_t0 = Now();
	g_mode = mode;
	return true;
}

void Questreplay_Disarm() {
	const int mode = g_mode.exchange(0);
	if (!mode) return;
	Enable(false, mode == 1);
	Log("questreplay: resolver ids in phase 2a: %u Register, %u Unregister (%u of objects from before), order hash #%016llx", g_idReg,
	    g_idUnreg, g_idOld, (unsigned long long)g_idHash);
	if (mode == 1) {
		g_have = g_bad.empty() && !g_rec.proj.empty();
		size_t sb = 0;
		const size_t ns = g_rec.str.size();
		for (auto& s : g_rec.str) sb += s.size();
		Pack();
		Log("questreplay: recorded %zu projects, %.1f MB of ops, %zu strings (%.1f MB): %lld elements, %lld attributes, %lld md6, "
		    "%lld unchanged, %lld sequences (%lld text items), %lld child objects, %lld types, %lld definitions (%lld paths), %lld skips, "
		    "%lld pending, %lld lines, %lld associative; %s%s",
		    g_rec.proj.size(), g_rec.code.size() / 1048576.0, ns, sb / 1048576.0, g_c[cElem], g_c[cAttr], g_c[cMd6], g_c[cCur],
		    g_c[cSeq], g_c[cText], g_c[cObj], g_c[cType], g_c[cDef], g_c[cPath], g_c[cSkip], g_c[cPend], g_c[cLine], g_c[cAssoc],
		    g_have ? "usable" : "NOT usable: ", g_have ? "" : (g_bad.empty() ? "no project" : g_bad.c_str()));
	} else {
		Log("questreplay: replay build %s (%lld ops)%s%s", g_failed ? "FAILED" : "done", g_rOps, g_fail.empty() ? "" : ": ", g_fail.c_str());
	}
}

bool Questreplay_HaveRecord() { return g_have; }
bool Questreplay_LastFailed() { return g_failed; }
