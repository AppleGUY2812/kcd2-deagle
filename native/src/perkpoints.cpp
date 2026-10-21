// deagle_points: Henry's unspent perk points, per skill and per stat. Found in game by Claude 1 2026-10-18; owner now Claude 4 (🟪).
//   wh::rpgmodule::C_PerkList (RTTI ".?AVC_PerkList@rpgmodule@wh@@", vtable in WHGame.dll .rdata) +0x58 -> a small
//   heap block of points:  +0x10 uint16[4] stat points,  +0x24 uint16[] skill points indexed by skill_id
//   (Libs/Tables/rpg/skill.xml: stealth 0 ... alchemy 6 ... houndmaster 34).
//   Every soul has a C_PerkList; Henry's has the points block. Found from his soul since 2026-10-20 (Claude 4): Lua sends
//   tostring(player.soul.__ThisWUID) ("points_soul"), the game's own WUID -> soul lookup gives the C_Soul, and his live
//   list is soul+0x758 (HenryList has the layout). Before, a scan of all memory for the C_PerkList vtable took 2-21 s
//   and 6 GB per call and refused once a 4th points block appeared (2026-10-20); the scan and its "all copies agree"
//   rule are gone. The live list is the one the game reads (the earlier finding: writing the real block showed at once,
//   a copy alone didn't, and copies follow the real one).
// deagle_respec <skill>: the owned perks are C_PerkList +0x40..+0x48, an array of perk object pointers (C_BuffPerk,
// C_MetaPerk, C_SoulAbilityPerk...), the perk's GUID at object +0x10 (in game 2026-10-18 it held exactly the save's
// bought perks plus free ones like Lab Dweller; +0x28..+0x30 held the not-yet-owned buyable ones). Lua removes them
// (soul:RemovePerk, which works), then respec_refund counts how many are really gone from the live list and adds that
// many points: one point per perk, as the Lethean Water refund was (alchemy 0 -> 7 for 7 perks). Lua asks for the refund 0.5 s later, not in the same call (RemovePerk's effect on +0x40 timing
// is unmeasured). If fewer come off than were asked, the reply says how many were asked and how to add them by hand. perkdefs.inc says which perks cost a
// point and their sub-perks (tools/gen_perkdefs.py).
// Skill ids 0..34 only (skill.xml's highest id is 34; in game the u16 after id 35 already held other data), and only
// by name: no raw index reaches a write.
// Unverified: which stat is which (named stat0-stat3 until each is checked on the perk screen, as alchemy was), and
// whether a written value survives save + reload or a level-up (the game may recompute points and overwrite them).
#include "perkpoints.h"
#include "scan.h"
#include "log.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

const char* kSkills[] = {   // skill_id -> name (skill.xml); nullptr = no skill with that id
	"stealth", "horse_riding", "fencing", "bard", "thievery", nullptr, "alchemy", "cooking", "craftsmanship", nullptr,
	"fishing", "mining", "first_aid", "drinking", "survival", "defense", "weapon_sword", "heavy_weapons", nullptr,
	"marksmanship", "weapon_shield", nullptr, "weapon_dagger", "weapon_large", "weapon_unarmed", nullptr, "scholarship",
	"tailoring", "armourer", "weaponsmithing", "shoemaking", "gunsmithing", "bowyery", "gambling", "houndmaster",
};
const int kSkillCount = sizeof(kSkills) / sizeof(kSkills[0]);   // 35: ids 0..34
const char* kStats[] = { "stat0", "stat1", "stat2", "stat3" };   // order unverified (see the top)
struct Alias { const char* name; const char* is; };
const Alias kAliases[] = {   // the menu's names for a few skills
	{ "horsemanship", "horse_riding" }, { "riding", "horse_riding" }, { "sword", "weapon_sword" }, { "swords", "weapon_sword" },
	{ "shield", "weapon_shield" }, { "dagger", "weapon_dagger" }, { "polearm", "weapon_large" },
	{ "unarmed", "weapon_unarmed" }, { "heavy", "heavy_weapons" }, { "archery", "marksmanship" }, { "bow", "marksmanship" },
	{ "craft", "craftsmanship" }, { "smithing", "craftsmanship" }, { "herbalism", "survival" }, { "hunting", "survival" },
	{ "lockpicking", "thievery" }, { "reading", "scholarship" }, { "dog", "houndmaster" }, { "warfare", "fencing" },
	{ "defence", "defense" }, { "polearms", "weapon_large" },   // the menu says Warfare for fencing (text_ui_soul.xml)
};
const int kStatOff = 0x10, kSkillOff = 0x24, kBlockPtr = 0x58, kBackPtr = 0x08;
const int kOwnedBegin = 0x40, kOwnedEnd = 0x48, kPerkGuid = 0x10, kMaxOwned = 4000;
const int kLiveList = 0x758, kTemplateList = 0xa38, kListSoul = 0x08;   // soul+0x560+0x1f8, soul+0x840+0x1f8; C_PerkList+8 = its soul

struct PerkDef { int kind, idx; const char* guid; const char* subs[4]; };
const PerkDef kPerks[] = {
#include "perkdefs.inc"
};

// "1071e935-a667-47c1-8b2d-e0818434a471" -> the 16 bytes as a GUID sits in memory (first three fields little-endian)
bool GuidBytes(const char* g, uint8_t out[16]) {
	unsigned d1, d2, d3, b[8];
	if (sscanf_s(g, "%8x-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x", &d1, &d2, &d3, &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &b[6], &b[7]) != 11) return false;
	memcpy(out, &d1, 4);
	uint16_t w2 = (uint16_t)d2, w3 = (uint16_t)d3;
	memcpy(out + 4, &w2, 2); memcpy(out + 6, &w3, 2);
	for (int i = 0; i < 8; i++) out[8 + i] = (uint8_t)b[i];
	return true;
}

Module g_game;
using SoulLookupFn = uintptr_t(__fastcall*)(uintptr_t registry, const uint64_t* wuid);
uintptr_t g_soulGlobal = 0;        // the qword at VA 0x1853322a0 (1.5.6): +0x80 -> the soul registry, +0x38 its table
SoulLookupFn g_soulLookup = nullptr;   // FUN_181f985d0: WUID -> C_Soul* (pure table read: type byte 5, generation, soul+0x30)
uint64_t g_wuid = 0;               // Henry's soul WUID, from Lua (points_soul) before every points / respec call

const char* kSoulRtti = ".?AVC_Soul@rpgmodule@wh@@";
const char* kListRtti = ".?AVC_PerkList@rpgmodule@wh@@";

bool ReadBlock(uintptr_t list, uintptr_t* block, uint16_t* stats, uint16_t* skills) {
	__try {
		uintptr_t b = *(uintptr_t*)(list + kBlockPtr);
		if (!b || *(uintptr_t*)(b + kBackPtr) != list) return false;
		memcpy(stats, (const void*)(b + kStatOff), 4 * sizeof(uint16_t));
		memcpy(skills, (const void*)(b + kSkillOff), kSkillCount * sizeof(uint16_t));
		*block = b;
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

uintptr_t CallLookup(uint64_t wuid) {
	__try { return g_soulLookup(*(uintptr_t*)(*(uintptr_t*)g_soulGlobal + 0x80) + 0x38, &wuid); }
	__except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

uintptr_t Qword(uintptr_t a) { uintptr_t v = 0; __try { v = *(uintptr_t*)a; } __except (EXCEPTION_EXECUTE_HANDLER) { v = 0; } return v; }

bool WriteU16(uintptr_t addr, uint16_t v) {
	__try { *(uint16_t*)addr = v; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

struct Block { uintptr_t addr, list; uint16_t stats[4]; uint16_t skills[35]; };

double Ms(const LARGE_INTEGER& t0) {
	LARGE_INTEGER t1, f;
	QueryPerformanceCounter(&t1); QueryPerformanceFrequency(&f);
	return (t1.QuadPart - t0.QuadPart) * 1000.0 / f.QuadPart;
}

// Henry's live perk list and its points block: soul (from his WUID) + 0x758, every link checked. No memory scan.
//   C_Soul ctor 0x1803f1c7c: soul+0x498 struct (ctor 0x18070f31c) holds two stat/skill/perk sets (ctor 0x18070f3e8):
//   the live one at soul+0x560 (stats at +0xc: soul+0x56c, the base getter's) and the template at soul+0x840 (Storm's
//   starting values, docs/whgame_re/stat_base.md fact 2). Each set has its owner soul at +0 and a C_PerkList at +0x1f8
//   whose +8 is the soul again. So the live list is soul+0x758; the template's (soul+0xa38) is only logged.
bool HenryList(Block* out, std::string* err) {
	if (!g_soulLookup) { *err = "Henry's perks not found: the plugin didn't find the game's soul lookup (log: points:)"; return false; }
	if (!g_wuid) { *err = "Henry's perks not found: no soul handle from Lua (update the Deagle pak, or load a save first)"; return false; }
	uintptr_t soul = CallLookup(g_wuid);
	if (!soul) { *err = "Henry's perks not found: his soul isn't loaded (load a save first)"; return false; }
	if (RttiName(g_game, Qword(soul)) != kSoulRtti) { *err = "Henry's perks not found: the soul handle gave something else, nothing changed"; return false; }
	uintptr_t list = soul + kLiveList;
	if (RttiName(g_game, Qword(list)) != kListRtti || Qword(list + kListSoul) != soul) {
		*err = "Henry's perks not found: no perk list at soul+0x758 (game update?), nothing changed"; return false;
	}
	Block b{};
	if (!ReadBlock(list, &b.addr, b.stats, b.skills)) { *err = "Henry's points not found: his perk list has no points block, nothing changed"; return false; }
	b.list = list;
	*out = b;
	static uintptr_t saidSoul = 0;
	if (soul != saidSoul) {   // once per soul: where things are, and whether the template list has a points block
		saidSoul = soul;
		Block t{};
		bool tpl = ReadBlock(soul + kTemplateList, &t.addr, t.stats, t.skills);
		Log("points: Henry's soul %p (WUID %016llx), live perk list %p, points block %p; template list %s", (void*)soul,
			(unsigned long long)g_wuid, (void*)list, (void*)b.addr, tpl ? "has a points block too" : "has no points block");
	}
	return true;
}

std::string List(const Block& b) {
	std::string s;
	char buf[64];
	for (int i = 0; i < 4; i++) if (b.stats[i]) { snprintf(buf, sizeof(buf), "%s %u, ", kStats[i], b.stats[i]); s += buf; }
	for (int i = 0; i < kSkillCount; i++) if (kSkills[i] && b.skills[i]) { snprintf(buf, sizeof(buf), "%s %u, ", kSkills[i], b.skills[i]); s += buf; }
	if (s.empty()) return "no unspent points";
	s.resize(s.size() - 2);
	return s;
}

// name -> (isStat, index); -1 if unknown
int Lookup(const char* name, bool* isStat) {
	for (auto& a : kAliases) if (!_stricmp(name, a.name)) name = a.is;
	for (int i = 0; i < 4; i++) if (!_stricmp(name, kStats[i])) { *isStat = true; return i; }
	for (int i = 0; i < kSkillCount; i++) if (kSkills[i] && !_stricmp(name, kSkills[i])) { *isStat = false; return i; }
	return -1;
}

// the owned perks' GUIDs of one C_PerkList; false if the array can't be read
bool ReadOwned(uintptr_t list, uint8_t (*out)[16], int cap, int* n) {
	*n = 0;
	__try {
		auto b = *(uintptr_t**)(list + kOwnedBegin), e = *(uintptr_t**)(list + kOwnedEnd);
		if (!b || e < b || e - b > cap) return false;
		for (auto p = b; p < e; p++) if (*p) memcpy(out[(*n)++], (const void*)(*p + kPerkGuid), 16);
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

std::vector<int> g_pending;   // kPerks indexes Lua was asked to remove (respec), counted by respec_refund
int g_pendKind = -1, g_pendIdx = -1;

// kPerks indexes of one skill / stat that Henry's live perk list owns now
bool OwnedOf(int kind, int idx, std::vector<int>* found, std::string* err) {
	LARGE_INTEGER t0; QueryPerformanceCounter(&t0);
	Block b{};
	if (!HenryList(&b, err)) return false;
	static uint8_t owned[kMaxOwned][16];
	int n = 0;
	if (!ReadOwned(b.list, owned, kMaxOwned, &n)) { *err = "Henry's owned-perk list didn't read, nothing changed"; return false; }
	found->clear();
	for (int i = 0; i < (int)(sizeof(kPerks) / sizeof(kPerks[0])); i++) {
		if (kPerks[i].kind != kind || kPerks[i].idx != idx) continue;
		uint8_t g[16];
		if (!GuidBytes(kPerks[i].guid, g)) continue;
		for (int k = 0; k < n; k++) if (!memcmp(owned[k], g, 16)) { found->push_back(i); break; }
	}
	Log("points: respec read: %d owned, %zu of this skill (%.3f ms)", n, found->size(), Ms(t0));
	return true;
}

std::string Said(std::string s) {   // goes back through Lua's [[%line]]: no "]]" may reach it
	for (auto& c : s) if (c == ']') c = ')';
	return s;
}

std::string PointsCommand(const char* rest);

}  // namespace

void Points_Install(const Module& game) {
	g_game = game;
	// FUN_18041ed20 (the soul Lua bindings' "which soul": the table's __ThisWUID, then the registry lookup):
	// lea rdx,"__ThisWUID"; mov [rsp+30h],rbx; call GetValue; test al,al; je; mov rax,[rsp+30h]; lea rdx,[rsp+30h];
	// mov [rsp+30h],rax; mov rax,[G]; mov rcx,[rax+80h]; add rcx,38h; call lookup
	uintptr_t a = FindUnique(game, "48 8D 15 ?? ?? ?? ?? 48 89 5C 24 30 E8 ?? ?? ?? ?? 84 C0 74 ?? 48 8B 44 24 30 48 8D 54 24 30 "
		"48 89 44 24 30 48 8B 05 ?? ?? ?? ?? 48 8B 88 80 00 00 00 48 83 C1 38 E8");
	if (a) {
		g_soulGlobal = RipTarget(a + 0x24, 3, 7);
		g_soulLookup = (SoulLookupFn)RipTarget(a + 0x36, 1, 5);
	}
	Log("points: soul registry global %p, lookup %p", (void*)g_soulGlobal, (void*)g_soulLookup);
}

// "userdata: 0500000000012345" (Lua's tostring of the soul's __ThisWUID, a light userdata) -> the WUID
void Points_Soul(const char* rest) {
	const char* p = strrchr(rest, ':');
	p = p ? p + 1 : rest;
	while (*p == ' ') p++;
	char* end = nullptr;
	uint64_t w = _strtoui64(p, &end, 16);
	if (end == p || (w >> 56) != 5) {   // the lookup only takes type 5 (a soul) in the top byte
		if (g_wuid) Log("points: bad soul handle '%s', forgot the old one", rest);
		else { static int said = 0; if (said++ < 5) Log("points: bad soul handle '%s'", rest); }
		g_wuid = 0;
		return;
	}
	g_wuid = w;
}

std::string Points_Command(const char* rest) { return Said(PointsCommand(rest)); }

std::string Respec_Command(const char* rest) {
	char name[48] = {};
	bool isStat = false;
	int idx = sscanf_s(rest, "%47s", name, (unsigned)sizeof(name)) == 1 ? Lookup(name, &isStat) : -1;
	if (idx < 0) return Said(std::string("usage: deagle_respec <skill> (e.g. alchemy); '") + name + "' isn't one");
	std::vector<int> found;
	std::string err;
	if (!OwnedOf(isStat ? 1 : 0, idx, &found, &err)) return Said(err);
	const char* label = isStat ? kStats[idx] : kSkills[idx];
	if (found.empty()) return Said(std::string("no ") + label + " perks to reset");
	g_pending = found; g_pendKind = isStat ? 1 : 0; g_pendIdx = idx;
	std::string cmd = std::string("REMOVE ") + label;   // main.cpp turns this into deagle_respec_remove
	for (int i : found) {
		cmd += std::string(" ") + kPerks[i].guid;
		for (auto sub : kPerks[i].subs) if (sub) cmd += std::string(" ") + sub;
	}
	Log("points: respec %s: %zu owned perks to remove", label, found.size());
	return cmd;
}

std::string Respec_Refund() {
	if (g_pendKind < 0) return "respec: nothing pending";
	int kind = g_pendKind, idx = g_pendIdx;
	g_pendKind = -1;
	const char* label = kind ? kStats[idx] : kSkills[idx];
	int asked = (int)g_pending.size();
	char fix[160];
	snprintf(fix, sizeof(fix), " (%d were asked to come off; if they're gone from the perk screen, deagle_points %s +%d gives the points)", asked, label, asked);
	std::vector<int> still;
	std::string err;
	if (!OwnedOf(kind, idx, &still, &err)) return Said("respec: " + err + fix);
	int gone = 0;
	for (int i : g_pending) { bool kept = false; for (int k : still) if (k == i) kept = true; if (!kept) gone++; }
	if (!gone) return Said(std::string("respec ") + label + ": no perk came off by the list, no points added" + fix);
	char arg[64];
	snprintf(arg, sizeof(arg), "%s +%d", label, gone);
	std::string r = Points_Command(arg);
	if (r.compare(0, strlen(label), label) != 0) r += fix;   // the points write itself refused: say how to add them by hand
	Log("points: respec %s: %d of %d perks removed; %s", label, gone, asked, r.c_str());
	char head[96];
	snprintf(head, sizeof(head), "respec %s: %d of %d perks removed, refund: ", label, gone, asked);
	return Said(head + r);
}

namespace {

// replies keep the shapes tools/points_ui.ps1 parses: the list ("alchemy 6, survival 2" / "no unspent points"),
// "<key> <old> -> <new> ...", and refusals containing "not found" / "nothing changed" / "isn't"
std::string PointsCommand(const char* rest) {
	LARGE_INTEGER t0; QueryPerformanceCounter(&t0);
	char name[48] = {}, val[16] = {};
	int got = sscanf_s(rest, "%47s %15s", name, (unsigned)sizeof(name), val, (unsigned)sizeof(val));
	bool isStat = false;
	int idx = -1;
	if (got >= 1) {
		idx = Lookup(name, &isStat);
		if (idx < 0) return std::string("unknown skill or stat '") + name + "' (e.g. alchemy, survival, horsemanship, sword, stat0)";
		if (got < 2) return "usage: deagle_points [<skill|stat> <n> | +n | -n]";
	}
	Block b{};
	std::string err;
	if (!HenryList(&b, &err)) { Log("points: %s (%.3f ms)", err.c_str(), Ms(t0)); return err; }
	if (got < 1) { std::string l = List(b); Log("points: list: %s (%.3f ms)", l.c_str(), Ms(t0)); return l; }
	uint16_t old = isStat ? b.stats[idx] : b.skills[idx];
	char* end = nullptr;
	long v = strtol(val, &end, 10);
	if (end == val || *end) return std::string("'") + val + "' isn't a number (e.g. 7, +2, -1); nothing changed";
	if (val[0] == '+' || val[0] == '-') v += old;
	if (v < 0 || v > 999) return "points must end up between 0 and 999";
	const char* label = isStat ? kStats[idx] : kSkills[idx];
	if (!WriteU16(b.addr + (isStat ? kStatOff : kSkillOff) + idx * 2, (uint16_t)v)) {
		Log("points: %s write to %p FAILED", label, (void*)b.addr);
		return std::string(label) + ": the write failed, nothing changed";
	}
	double ms = Ms(t0);
	Log("points: %s %u -> %ld (block %p, %.3f ms)", label, old, v, (void*)b.addr, ms);
	char out[160];
	snprintf(out, sizeof(out), "%s %u -> %ld; reopen the perk screen (%.2f ms)", label, old, v, ms);
	return out;
}

}  // namespace
