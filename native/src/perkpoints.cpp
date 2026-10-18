// deagle_points: Henry's unspent perk points, per skill and per stat. Owner: Claude 1 (found in game 2026-10-18).
//   wh::rpgmodule::C_PerkList (RTTI ".?AVC_PerkList@rpgmodule@wh@@", vtable in WHGame.dll .rdata) +0x58 -> a small
//   heap block of points:  +0x10 uint16[4] stat points,  +0x24 uint16[] skill points indexed by skill_id
//   (Libs/Tables/rpg/skill.xml: stealth 0 ... alchemy 6 ... houndmaster 34).
//   Every soul has a C_PerkList (about 3000 in a town); only the player's three have a points block (the game keeps
//   two copies in sync with the real one). Writing alchemy's u16 in the real block showed on the perk screen at once;
//   writing a copy alone didn't (the copy is refreshed from the real one). So we write every block, and only when all
//   blocks agree: blocks that differ could mean an NPC with its own points, and then we write nothing.
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
	{ "shield", "weapon_shield" }, { "dagger", "weapon_dagger" }, { "polearm", "weapon_large" }, { "polearms", "weapon_large" },
	{ "unarmed", "weapon_unarmed" }, { "heavy", "heavy_weapons" }, { "archery", "marksmanship" }, { "bow", "marksmanship" },
	{ "craft", "craftsmanship" }, { "smithing", "craftsmanship" }, { "herbalism", "survival" }, { "hunting", "survival" },
	{ "lockpicking", "thievery" }, { "reading", "scholarship" }, { "dog", "houndmaster" }, { "warfare", "defense" },
};
const int kStatOff = 0x10, kSkillOff = 0x24, kBlockPtr = 0x58;

Module g_game;
uintptr_t g_vtable = 0;

// RTTI type name -> the class's primary vtable: TypeDescriptor (name - 0x10) <- CompleteObjectLocator (signature 1,
// offset 0, its own RVA at +20) <- the qword before the vtable. Only reads WHGame.dll's own image.
uintptr_t FindVtable(const Module& m, const char* name) {
	size_t len = strlen(name);
	auto dos = (IMAGE_DOS_HEADER*)m.base;
	auto nt = (IMAGE_NT_HEADERS*)(m.base + dos->e_lfanew);
	auto first = IMAGE_FIRST_SECTION(nt);
	int nsec = nt->FileHeader.NumberOfSections;
	uintptr_t td = 0;
	for (int i = 0; i < nsec && !td; i++) {
		auto s = first + i;
		if (s->Characteristics & IMAGE_SCN_MEM_EXECUTE) continue;
		auto p = (const char*)(m.base + s->VirtualAddress);
		size_t n = s->Misc.VirtualSize;
		for (size_t o = 0; o + len < n; o++) if (p[o] == name[0] && !memcmp(p + o, name, len + 1)) { td = (uintptr_t)(p + o) - 0x10; break; }
	}
	if (!td) return 0;
	uint32_t tdRva = (uint32_t)(td - m.base);
	for (int i = 0; i < nsec; i++) {
		auto s = first + i;
		if (s->Characteristics & IMAGE_SCN_MEM_EXECUTE) continue;
		auto p = (const uint8_t*)(m.base + s->VirtualAddress);
		size_t n = s->Misc.VirtualSize;
		for (size_t o = 0; o + 24 <= n; o += 4) {
			auto u = (const uint32_t*)(p + o);
			if (u[0] != 1 || u[1] != 0 || u[3] != tdRva || u[5] != (uint32_t)((uintptr_t)(p + o) - m.base)) continue;
			uintptr_t col = (uintptr_t)(p + o);
			for (int j = 0; j < nsec; j++) {   // the qword holding the COL's address sits right before the vtable
				auto t = first + j;
				if (t->Characteristics & IMAGE_SCN_MEM_EXECUTE) continue;
				auto q = (const uint8_t*)(m.base + t->VirtualAddress);
				size_t qn = t->Misc.VirtualSize;
				for (size_t k = 0; k + 16 <= qn; k += 8) if (*(const uintptr_t*)(q + k) == col) return (uintptr_t)(q + k + 8);
			}
		}
	}
	return 0;
}

// one committed region: every 8-aligned qword equal to vt; faults (a region freed meanwhile) end the region quietly
void ScanRegion(uintptr_t start, size_t size, uintptr_t vt, uintptr_t* out, int cap, int* n) {
	__try {
		auto q = (const uintptr_t*)start;
		size_t count = size / 8;
		for (size_t i = 0; i < count; i++) if (q[i] == vt && *n < cap) out[(*n)++] = start + i * 8;
	} __except (EXCEPTION_EXECUTE_HANDLER) {}
}

bool ReadBlock(uintptr_t list, uintptr_t* block, uint16_t* stats, uint16_t* skills) {
	__try {
		uintptr_t b = *(uintptr_t*)(list + kBlockPtr);
		if (!b) return false;
		memcpy(stats, (const void*)(b + kStatOff), 4 * sizeof(uint16_t));
		memcpy(skills, (const void*)(b + kSkillOff), kSkillCount * sizeof(uint16_t));
		*block = b;
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool WriteU16(uintptr_t addr, uint16_t v) {
	__try { *(uint16_t*)addr = v; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

struct Block { uintptr_t addr; uint16_t stats[4]; uint16_t skills[35]; };

// the player's points blocks: every C_PerkList in private read-write memory with a non-null +0x58
std::vector<Block> FindBlocks(int* lists) {
	std::vector<Block> blocks;
	*lists = 0;
	if (!g_game.base) g_game = GetModule("WHGame.dll");
	if (!g_vtable && g_game.base) {
		g_vtable = FindVtable(g_game, ".?AVC_PerkList@rpgmodule@wh@@");
		Log("points: C_PerkList vtable %p (WHGame+0x%llx)", (void*)g_vtable, (unsigned long long)(g_vtable ? g_vtable - g_game.base : 0));
	}
	if (!g_vtable) return blocks;
	static std::vector<uintptr_t> hits(20000);
	int n = 0;
	MEMORY_BASIC_INFORMATION mbi{};
	for (uintptr_t a = 0x10000; VirtualQuery((void*)a, &mbi, sizeof(mbi)) == sizeof(mbi); a = (uintptr_t)mbi.BaseAddress + mbi.RegionSize) {
		if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && mbi.Protect == PAGE_READWRITE)
			ScanRegion((uintptr_t)mbi.BaseAddress, mbi.RegionSize, g_vtable, hits.data(), (int)hits.size(), &n);
		if ((uintptr_t)mbi.BaseAddress + mbi.RegionSize <= a) break;
	}
	*lists = n;
	for (int i = 0; i < n; i++) {
		Block b{};
		if (ReadBlock(hits[i], &b.addr, b.stats, b.skills)) blocks.push_back(b);
	}
	return blocks;
}

bool Same(const Block& a, const Block& b) { return !memcmp(a.stats, b.stats, sizeof(a.stats)) && !memcmp(a.skills, b.skills, sizeof(a.skills)); }

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

}  // namespace

std::string Points_Command(const char* rest) {
	char name[48] = {}, val[16] = {};
	int got = sscanf_s(rest, "%47s %15s", name, (unsigned)sizeof(name), val, (unsigned)sizeof(val));
	bool isStat = false;
	int idx = -1;
	if (got >= 1) {
		idx = Lookup(name, &isStat);
		if (idx < 0) return std::string("unknown skill or stat '") + name + "' (e.g. alchemy, survival, horsemanship, sword, stat0)";
		if (got < 2) return "usage: deagle_points [<skill|stat> <n> | +n | -n]";
	}
	int lists = 0;
	std::vector<Block> blocks = FindBlocks(&lists);
	char head[96];
	snprintf(head, sizeof(head), "%d perk lists, %zu with points", lists, blocks.size());
	Log("points: %s", head);
	if (blocks.empty()) return std::string(head) + ": Henry's points not found (load a save first)";
	for (auto& b : blocks) if (!Same(b, blocks[0])) {
		std::string s = std::string(head) + " that DIFFER, nothing changed:";
		for (auto& c : blocks) { char a[32]; snprintf(a, sizeof(a), " [%p] ", (void*)c.addr); s += a + List(c); }
		return s;
	}
	if (got < 1) return List(blocks[0]);
	uint16_t old = isStat ? blocks[0].stats[idx] : blocks[0].skills[idx];
	long v = strtol(val, nullptr, 10);
	if (val[0] == '+' || val[0] == '-') v += old;
	if (v < 0 || v > 999) return "points must end up between 0 and 999";
	int wrote = 0;
	for (auto& b : blocks) if (WriteU16(b.addr + (isStat ? kStatOff : kSkillOff) + idx * 2, (uint16_t)v)) wrote++;
	const char* label = isStat ? kStats[idx] : kSkills[idx];
	Log("points: %s %u -> %ld in %d of %zu blocks", label, old, v, wrote, blocks.size());
	char out[160];
	snprintf(out, sizeof(out), "%s %u -> %ld (%d of %zu copies written); reopen the perk screen", label, old, v, wrote, blocks.size());
	return out;
}
