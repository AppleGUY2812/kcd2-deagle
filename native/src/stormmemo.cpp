// Storm's per-character rule pass with remembered condition sets: see stormmemo.h. WHGame.dll 1.5.6, checked at start.
#include "stormmemo.h"
#include "stormmemo_core.h"
#include "log.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include "MinHook.h"

namespace {

// FUN_1806b6a08(rules, soul, mask): for level 0..max, every task group (0x30 stride, mask test, FUN_181fec2d0) and
// sub-group (0x60 stride, FUN_180698e2c) runs FUN_180698e84(group, soul, level).
// FUN_180698e84: group+0x58 max level, rules group+0x40..+0x48 (0x50 stride); a rule: level +0x48, conditions
// +0x30..+0x38 and actions +0x18..+0x20 (16-byte counted pointers); condition vt+8(cond, soul) -> al;
// action vt+8(action, soul, rule).
constexpr uintptr_t kRvaPass = 0x6b6a08, kRvaWalk = 0x698e84, kRvaCustom = 0x6990f4;
const unsigned char kPassBytes[] = { 0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x68,0x10,0x48,0x89,0x70,0x18,0x48,
                                     0x89,0x78,0x20,0x41,0x56,0x48,0x83,0xec,0x50 };
const unsigned char kWalkBytes[] = { 0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x68,0x10,0x48,0x89,0x70,0x18,0x48,
                                     0x89,0x78,0x20,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x20,0x45,0x8b,0xf8,
                                     0x4c,0x8b,0xe2,0x44,0x3b,0x41,0x58 };
const unsigned char kCustomBytes[] = { 0x48,0x83,0xec,0x28,0x83,0x79,0x28,0x00,0x75,0x10,0x48,0x83,0xc1,0x08 };

// The character state groups (re_storm_invalidate.md, 2026-10-24, static RE): what each condition kind reads and each
// action kind writes. Identity, faction and global reads (name, archetype, gender, faction, game mode ...) are in no
// group: no action writes them, only a new pass (Changed(kAll)) makes them stale.
using Mask = smemo::Memo::Mask;
constexpr Mask kAll = smemo::Memo::kAll;
constexpr Mask kGRoles = 1, kGCtx = 2, kGSched = 4, kGPerks = 8, kGAttr = 16, kGCombat = 32, kGName = 64;
constexpr Mask kGState = kGRoles | kGCtx | kGSched | kGPerks | kGAttr | kGCombat | kGName;

// Every storm condition class (RTTI: all 27 that derive from I_Selector), its vtable, its evaluate (vt+8) and the
// groups it reads. The 23 leaves read the character (or a global like the game mode) and return a bool: no writes, no
// random draws (hasRandomValue hashes the character's id). Composites: and / or / named set keep their children at
// +8..+0x10 (16-byte entries), not at +8.
enum Kids { kLeaf, kVector, kOne };
struct Kind { uintptr_t vt, eval; Kids kids; const char* name; Mask reads; };
const Kind kKinds[] = {
	{ 0x3b30d50, 0x6990f4, kVector, "C_CustomSelector", 0 },   { 0x3b30da8, 0xf4df74, kVector, "C_AndSelector", 0 },
	{ 0x3b30cf8, 0x699070, kVector, "C_OrSelector", 0 },       { 0x3b770b8, 0x6990d0, kOne, "C_NotSelector", 0 },
	{ 0x3c35970, 0x3f4940, kLeaf, "C_HasACharacter", kGName },      { 0x3f505e0, 0x2d85064, kLeaf, "C_HasAFaction", 0 },
	{ 0x3b756d0, 0x11730d4, kLeaf, "C_HasAttribute", kGAttr },      { 0x3b7db58, 0x2d850a0, kLeaf, "C_HasCharacterName", kGName },
	{ 0x3bc6588, 0xa2f214, kLeaf, "C_HasCombatLevel", kGCombat },     { 0x4733e08, 0x2d8510c, kLeaf, "C_HasContext", kGCtx },
	{ 0x3b65318, 0xc95ccc, kLeaf, "C_HasFaction", 0 },         { 0x3be9ba8, 0x4756b8, kLeaf, "C_HasFactionLabel", 0 },
	{ 0x3be67b0, 0x14bc310, kLeaf, "C_HasGender", 0 },         { 0x3f510b0, 0x2d85144, kLeaf, "C_HasLocation", 0 },
	{ 0x3a4fc40, 0x698f40, kLeaf, "C_HasName", 0 },            { 0x3f50548, 0x2d851a0, kLeaf, "C_HasNameNumberModulo", 0 },
	{ 0x3f51118, 0x23d1160, kLeaf, "C_HasPerk", kGPerks },           { 0x3badde0, 0x105df58, kLeaf, "C_HasRandomValue", 0 },
	{ 0x3c0bc98, 0xc960a4, kLeaf, "C_HasRole", kGRoles },            { 0x3f50578, 0x2dab1a4, kLeaf, "C_HasScheduleItemCount", kGSched },
	{ 0x3b41798, 0x69911c, kLeaf, "C_HasSocialClass", 0 },     { 0x3b8e460, 0xdcc048, kLeaf, "C_HasSoulArchetype", 0 },
	{ 0x3f505b0, 0x2d853ac, kLeaf, "C_HasSoulCrimeRole", 0 },  { 0x3b48180, 0xcf5f9c, kLeaf, "C_HasVoice", 0 },
	{ 0x3c352d0, 0xe55ae8, kLeaf, "C_IsContentFilterActive", 0 }, { 0x3f50f80, 0x1fea310, kLeaf, "C_IsGameMode", 0 },
	{ 0x3be4530, 0x4755e4, kLeaf, "C_IsPublicEnemy", 0 },
};
uintptr_t g_base = 0;

// Every storm action class (RTTI: the 34 concrete ones that derive from I_Operator), its vtable, its execute (vt+8)
// and the groups it writes. kAll where the RE didn't follow every call: PerkRemove (an un-apply virtual),
// SetReputation (the faction's shared totals: C_IsPublicEnemy may read them), SetEquipDirectives and
// SetReincarnationParams (virtuals on soul+0x198). Attribute actions fire an attribute-change event to listeners that
// weren't followed: they mark every state group. C_CustomOperator = its children (+8..+0x10, 16-byte entries).
// A class not in this table, or whose execute moved, writes kAll.
struct Op { uintptr_t vt, exec; Mask writes; const char* name; };
const Op kOps[] = {
	{ 0x3a4f980, 0x46bb94, kGRoles, "RoleAdd" },          { 0x3b43aa8, 0x46bf14, kGRoles, "RoleRemove" },
	{ 0x3f4c190, 0x1a94dd0, kGRoles, "ClearRoles" },
	{ 0x3b79f50, 0xff3b18, kGCtx, "ContextAdd" },         { 0x3c33b60, 0x19d24dc, kGCtx, "ContextRemove" },
	{ 0x3c14368, 0xff3acc, kGCtx, "ContextPresetAdd" },   { 0x4733868, 0x2d4df18, kGCtx, "ContextPresetRemove" },
	{ 0x3f4c1d8, 0x2d4e0b0, kGCtx, "ClearContexts" },
	{ 0x47338b0, 0x2d4df3c, kGSched, "AddScheduleItem" }, { 0x3f4c220, 0x2d4e0c8, kGSched, "ClearScheduleItems" },
	{ 0x4733e38, 0x2d85034, kGSched, "RemoveScheduleActivity" },
	{ 0x3be6438, 0x3c5678, 0, "MetaRoleAdd" },            { 0x4733dc0, 0x2d84ffc, 0, "MetaRoleRemove" },
	{ 0x4733790, 0x2d4de10, kGName, "CharacterAdd" },
	{ 0x3a6b690, 0xa203d0, kGPerks, "PerkAdd" },          { 0x3c359c8, 0x2d4dfd8, kGPerks, "AutolearnPerks" },
	{ 0x3a6b700, 0xa20370, kAll, "PerkRemove" },
	{ 0x3b2e668, 0x46bdbc, kGState, "AttributeSet" },     { 0x3b72c40, 0x46cd8c, kGState, "AttributeModify" },
	{ 0x47337e8, 0x2d4dde8, kGState, "AttributeRemove" },
	{ 0x47338f8, 0x2d4dfa0, 0, "AssignVoiceGroup" },      { 0x3b81c88, 0x13c5ba8, 0, "SetUiName" },
	{ 0x3b0ff60, 0x973318, 0, "SetBody" },                { 0x3b0fe70, 0x973318, 0, "SetHead" },
	{ 0x3b0fdf8, 0x973318, 0, "SetHair" },                { 0x3b0fee8, 0x973318, 0, "SetBeard" },
	{ 0x3b0ffd8, 0x973318, 0, "SetUnderwear" },
	{ 0x3b2f280, 0x1220ba8, 0, "SetInventory" },          { 0x3b2f2f8, 0x1220ba8, 0, "AddInventory" },
	{ 0x3b2f370, 0x1220ba8, 0, "SetLootInventory" },
	{ 0x3bccf78, 0x3f2dd0, kAll, "SetReputation" },       { 0x3c15bf8, 0x16b4edc, kAll, "SetEquipDirectives" },
	{ 0x3bf7898, 0x12d8244, kAll, "SetReincarnationParams" },
};
constexpr uintptr_t kVtCustomOp = 0x3bdcaa0, kExecCustomOp = 0x1397d70;
constexpr int kNOps = sizeof kOps / sizeof kOps[0];
bool g_opOk[kNOps] = {};   // its execute is where 1.5.6 has it (checked at start)
bool g_customOk = false;

using PassFn = void(__fastcall*)(void* rules, void* soul, uint16_t mask);
using WalkFn = void(__fastcall*)(uint8_t* group, void* soul, int level);
using CondFn = bool(__fastcall*)(void* cond, void* soul);
using ActionFn = void(__fastcall*)(void* action, void* soul, void* rule);
PassFn g_origPass = nullptr;
WalkFn g_origWalk = nullptr;
CondFn g_origCustom = nullptr;

std::atomic<int> g_mode{1};   // 0 off, 1 on, 2 check every remembered answer
std::atomic<int> g_index{1};  // 1: each group's rules sorted by level once (the walk visits only the level's own)
std::atomic<int> g_census{1}; // 1: count which condition kind ends each rule, time the walk and the actions
std::atomic<long long> g_checkLeft{100000};
long long g_freq = 1;

thread_local smemo::Memo* t_m = nullptr;   // on the heap (32 KB), only for threads that run Storm
thread_local smemo::Stats t_st;
thread_local long long t_souls = 0, t_ticks = 0, t_hitNo = 0;

// The census (g_census): rules looked at, rules whose conditions all passed, the condition kind that ended the others
// (kKinds index; kNoKind = a class not in kKinds), the walk's time and the actions' time.
constexpr int kNKinds = 27, kNoKind = kNKinds;
struct Census { long long rules, passed, walkTicks, actTicks, actions, ended[kNKinds + 1]; };
thread_local Census t_cen = {};

// Per-thread totals, merged after every character.
struct Totals { DWORD tid; long long souls, ticks, looked, hits, checked, bad; Census cen; };
Totals g_tot[16] = {};
SRWLOCK g_totLock = SRWLOCK_INIT;
std::atomic<long long> g_bad{0};

void Flush() {
	const DWORD tid = GetCurrentThreadId();
	AcquireSRWLockExclusive(&g_totLock);
	for (Totals& t : g_tot)
		if (t.tid == tid || t.tid == 0) {
			t.tid = tid;
			t.souls += t_souls; t.ticks += t_ticks; t.looked += (long long)t_st.looked; t.hits += (long long)t_st.hits;
			t.checked += (long long)t_st.checked; t.bad += (long long)t_st.bad;
			t.cen.rules += t_cen.rules; t.cen.passed += t_cen.passed; t.cen.walkTicks += t_cen.walkTicks;
			t.cen.actTicks += t_cen.actTicks; t.cen.actions += t_cen.actions;
			for (int k = 0; k <= kNKinds; k++) t.cen.ended[k] += t_cen.ended[k];
			break;
		}
	ReleaseSRWLockExclusive(&g_totLock);
	t_souls = t_ticks = 0;
	t_st = {};
	t_cen = {};
}

const Kind* KindOf(const void* p) {
	const uintptr_t vt = *(const uintptr_t*)p - g_base;
	for (const Kind& k : kKinds) if (k.vt == vt) return &k;
	return nullptr;
}
static_assert(sizeof kKinds / sizeof kKinds[0] == kNKinds, "kNKinds");
// The census's kind lookup: a small per-thread cache by vtable (the linear search above only on a miss).
int KindIndex(const void* p) {
	struct Slot { uintptr_t vt; int k; };
	thread_local Slot cache[64] = {};
	const uintptr_t vt = *(const uintptr_t*)p;
	Slot& s = cache[(vt >> 3) & 63];
	if (s.vt != vt) { const Kind* k = KindOf(p); s = { vt, k ? (int)(k - kKinds) : kNoKind }; }
	return s.k;
}

// A named set is remembered only when everything under it is a known read-only kind; it remembers the groups its
// leaves read (worked out once per set). -1 = not remembered.
int64_t ReadMask(const void* p, int depth) {
	if (!p) return 0;
	const Kind* k = KindOf(p);
	if (!k || depth > 64) return -1;
	int64_t m = k->reads;
	const uint8_t* b = (const uint8_t*)p;
	if (k->kids == kOne) {
		const int64_t c = ReadMask(*(void* const*)(b + 8), depth + 1);
		return c < 0 ? -1 : m | c;
	}
	if (k->kids == kVector) {
		void* const* e = *(void* const* const*)(b + 0x10);
		for (void* const* c = *(void* const* const*)(b + 8); c != e; c += 2) {
			const int64_t x = ReadMask(*c, depth + 1);
			if (x < 0) return -1;
			m |= x;
		}
	}
	return m;
}
int64_t Cacheable(const void* self) {
	thread_local std::unordered_map<const void*, int64_t> known;
	auto it = known.find(self);
	if (it != known.end()) return it->second;
	const int64_t m = ReadMask(self, 0);
	if (m < 0) Log("stormmemo: named set \"%.60s\" has a condition kind I don't know - not remembered",
	               *(const char* const*)((const uint8_t*)self + 0x20));
	known.emplace(self, m);
	return m;
}

// The groups an action writes (worked out once per action object; a custom operator = its children).
Mask WriteMask(const void* a, int depth) {
	if (!a) return 0;
	const uintptr_t vt = *(const uintptr_t*)a - g_base;
	if (vt == kVtCustomOp && g_customOk && depth < 16) {
		const uint8_t* b = (const uint8_t*)a;
		Mask m = 0;
		void* const* e = *(void* const* const*)(b + 0x10);
		for (void* const* c = *(void* const* const*)(b + 8); c != e && m != kAll; c += 2) m |= WriteMask(*c, depth + 1);
		return m;
	}
	for (int i = 0; i < kNOps; i++) if (kOps[i].vt == vt) return g_opOk[i] ? kOps[i].writes : kAll;
	return kAll;
}
Mask ActionWrites(const void* a) {
	thread_local std::unordered_map<const void*, Mask> known;
	auto it = known.find(a);
	if (it != known.end()) return it->second;
	const Mask m = WriteMask(a, 0);
	known.emplace(a, m);
	return m;
}

inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }

struct GameRules {
	using Rule = uint8_t;
	int MaxLevel(uint8_t* g) { return *(int*)(g + 0x58); }
	Rule* Rules(uint8_t* g, Rule** end) { *end = *(Rule**)(g + 0x48); return *(Rule**)(g + 0x40); }
	Rule* Next(Rule* r) { return r + 0x50; }
	int Level(Rule* r) { return *(int*)(r + 0x48); }
	void** Conds(Rule* r, void*** end) { *end = *(void***)(r + 0x38); return *(void***)(r + 0x30); }
	void** Actions(Rule* r, void*** end) { *end = *(void***)(r + 0x20); return *(void***)(r + 0x18); }
	void** NextEntry(void** p) { return p + 2; }
	bool Eval(void* c, void* soul) { return ((CondFn)(*(void***)c)[1])(c, soul); }
	void Run(void* a, void* soul, Rule* r) {
		if (!census) return ((ActionFn)(*(void***)a)[1])(a, soul, r);
		const long long t0 = Now();
		((ActionFn)(*(void***)a)[1])(a, soul, r);
		t_cen.actTicks += Now() - t0;
		t_cen.actions++;
	}
	Mask Writes(void* a) { return ActionWrites(a); }
	void Ended(Rule*, void* cond) {
		if (!census) return;
		t_cen.rules++;
		if (!cond) t_cen.passed++;
		else t_cen.ended[KindIndex(cond)]++;
	}
	void Ran(Rule*) {}
	bool census = false;
};

void __fastcall HkPass(void* rules, void* soul, uint16_t mask) {
	if (!g_mode.load(std::memory_order_relaxed) || !soul) return g_origPass(rules, soul, mask);
	if (!t_m) t_m = new smemo::Memo;
	smemo::Memo& m = *t_m;
	const void* prevSoul = m.soul;   // an action can start another character's pass
	const int prevIn = m.inAction;
	m.soul = soul;
	m.inAction = 0;
	m.Changed();
	const long long t0 = Now();
	g_origPass(rules, soul, mask);
	t_ticks += Now() - t0;
	t_souls++;
	m.Changed();
	m.soul = prevSoul;
	m.inAction = prevIn;
	if (!prevSoul) Flush();
}

void __fastcall HkWalk(uint8_t* group, void* soul, int level) {
	if (!t_m || !t_m->soul) return g_origWalk(group, soul, level);
	smemo::Memo& m = *t_m;
	GameRules a;
	a.census = g_census.load(std::memory_order_relaxed) != 0;
	const long long t0 = a.census ? Now() : 0;
	if (g_index.load(std::memory_order_relaxed)) {
		thread_local std::unordered_map<const uint8_t*, smemo::Levels<uint8_t>> idx;
		smemo::ApplyLevelIndexed(a, group, soul, level, m, idx[group]);
	} else {
		smemo::ApplyLevel(a, group, soul, level, m);
	}
	if (a.census) t_cen.walkTicks += Now() - t0;
}

bool __fastcall HkCustom(void* self, void* soul) {
	if (!t_m || !t_m->Active(soul) || !g_mode.load(std::memory_order_relaxed)) return g_origCustom(self, soul);
	smemo::Memo& m = *t_m;
	auto check = [] {   // per remembered answer
		if (g_mode.load(std::memory_order_relaxed) == 2) return true;
		if (g_checkLeft.load(std::memory_order_relaxed) > 0) return g_checkLeft.fetch_sub(1) > 0;
		return (++t_hitNo & 63) == 0;
	};
	bool mismatch = false;
	auto game = [&] { return g_origCustom(self, soul); };
	const bool r = smemo::EvalNamed(m, self, soul, [&] { return Cacheable(self); }, check, game, t_st, &mismatch);
	if (mismatch) {
		g_bad++;
		if (g_mode.exchange(0))
			Log("stormmemo: MISMATCH in named set \"%.60s\" for \"%.60s\" (remembered %d, the game's %d) - off for this "
			    "session, the game's answer kept", *(const char* const*)((uint8_t*)self + 0x20),
			    *(const char* const*)((uint8_t*)soul + 0x360), (int)!r, (int)r);
		m.soul = nullptr;   // nothing more from the memo in this pass either
	}
	return r;
}

DWORD WINAPI Reporter(LPVOID) {
	long long last = -1;
	for (int i = 0; i < 180; i++) {   // every 5 s while it runs, the first 15 minutes
		Sleep(5000);
		Totals snap[16];
		AcquireSRWLockShared(&g_totLock);
		memcpy(snap, g_tot, sizeof snap);
		ReleaseSRWLockShared(&g_totLock);
		long long souls = 0;
		for (const Totals& t : snap) souls += t.souls;
		if (souls == last) continue;
		last = souls;
		for (const Totals& t : snap) {
			if (!t.tid) break;
			Log("stormmemo: thread %lu: %lld characters, %.0f ms in Storm; named sets looked up %lld, remembered %lld "
			    "(%.0f %%), checked %lld, mismatches %lld%s", t.tid, t.souls, 1000.0 * t.ticks / g_freq, t.looked,
			    t.hits, t.looked ? 100.0 * t.hits / t.looked : 0.0, t.checked, t.bad, g_mode.load() ? "" : " (off)");
			if (!t.cen.rules) continue;
			// the five kinds that ended the most rules
			int top[5] = { -1, -1, -1, -1, -1 };
			for (int k = 0; k <= kNKinds; k++)
				for (int j = 0; j < 5; j++)
					if (top[j] < 0 || t.cen.ended[k] > t.cen.ended[top[j]]) {
						for (int q = 4; q > j; q--) top[q] = top[q - 1];
						top[j] = k;
						break;
					}
			char buf[512] = {};
			int n = 0;
			for (int j = 0; j < 5 && top[j] >= 0 && t.cen.ended[top[j]]; j++)
				n += snprintf(buf + n, sizeof buf - n, "%s%s %lld", j ? ", " : "",
				              top[j] < kNKinds ? kKinds[top[j]].name + 2 : "other", t.cen.ended[top[j]]);
			Log("stormmemo: census thread %lu: %lld rules looked at (%lld passed, %lld actions %.0f ms); rule walk %.0f ms "
			    "(index %s); stopped by: %s", t.tid, t.cen.rules, t.cen.passed, t.cen.actions,
			    1000.0 * t.cen.actTicks / g_freq, 1000.0 * t.cen.walkTicks / g_freq, g_index.load() ? "on" : "off", buf);
		}
	}
	return 0;
}

}  // namespace

void Stormmemo_Install(uintptr_t base, void* self) {
	LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = f.QuadPart;
	g_base = base;
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	std::string file = path;
	file = file.substr(0, file.find_last_of('\\') + 1) + "fastload_stormmemo.txt";
	if (FILE* fp = fopen(file.c_str(), "r")) {
		int m = 1, ix = 1, ce = 1;
		const int got = fscanf(fp, "%d %d %d", &m, &ix, &ce);
		if (got >= 1 && m >= 0 && m <= 2) g_mode = m;
		if (got >= 2) g_index = ix != 0;
		if (got >= 3) g_census = ce != 0;
		fclose(fp);
	}
	if (!g_mode) { Log("stormmemo: off (fastload_stormmemo.txt)"); return; }
	bool ok = !memcmp((void*)(base + kRvaPass), kPassBytes, sizeof kPassBytes) &&
	          !memcmp((void*)(base + kRvaWalk), kWalkBytes, sizeof kWalkBytes) &&
	          !memcmp((void*)(base + kRvaCustom), kCustomBytes, sizeof kCustomBytes);
	int badKind = -1;
	for (int i = 0; ok && i < (int)(sizeof kKinds / sizeof kKinds[0]); i++)
		if (((uintptr_t*)(base + kKinds[i].vt))[1] != base + kKinds[i].eval) { ok = false; badKind = i; }
	int opsOk = 0;
	for (int i = 0; i < kNOps; i++) opsOk += g_opOk[i] = ((uintptr_t*)(base + kOps[i].vt))[1] == base + kOps[i].exec;
	g_customOk = ((uintptr_t*)(base + kVtCustomOp))[1] == base + kExecCustomOp;
	if (opsOk < kNOps || !g_customOk)
		Log("stormmemo: %d of %d action kinds where 1.5.6 has them%s: the others make every answer stale", opsOk, kNOps,
		    g_customOk ? "" : ", custom operator not");
	if (!ok) {
		Log("stormmemo: code check failed%s%s - not installed (other game version?)", badKind >= 0 ? " at " : "",
		    badKind >= 0 ? kKinds[badKind].name : "");
		return;
	}
	void* fp = (void*)(base + kRvaPass);
	void* fw = (void*)(base + kRvaWalk);
	void* fc = (void*)(base + kRvaCustom);
	// The pass hook opens the memo's scope, so it goes on last: the walk and the set's evaluate alone change nothing,
	// but a scope without the walk (which marks the actions) would keep stale answers.
	bool h = MH_CreateHook(fp, (void*)&HkPass, (void**)&g_origPass) == MH_OK &&
	         MH_CreateHook(fw, (void*)&HkWalk, (void**)&g_origWalk) == MH_OK &&
	         MH_CreateHook(fc, (void*)&HkCustom, (void**)&g_origCustom) == MH_OK &&
	         MH_EnableHook(fw) == MH_OK && MH_EnableHook(fc) == MH_OK && MH_EnableHook(fp) == MH_OK;
	if (!h) { MH_DisableHook(fp); MH_DisableHook(fc); MH_DisableHook(fw); }
	Log("stormmemo: Storm's named condition sets remembered per character %s, kept across actions that don't touch "
	    "what they read (%s); rules sorted by level %s, census %s", h ? "on" : "FAILED",
	    g_mode == 2 ? "every remembered answer checked" : "the first 100000 remembered answers checked, then every 64th",
	    g_index.load() ? "on" : "off", g_census.load() ? "on" : "off");
	if (h) CloseHandle(CreateThread(nullptr, 0, Reporter, nullptr, 0, nullptr));
}
