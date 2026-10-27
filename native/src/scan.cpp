#include "scan.h"
#include <windows.h>
#include <psapi.h>

Module GetModule(const char* name) {
	Module m;
	HMODULE h = GetModuleHandleA(name);
	if (!h) return m;
	MODULEINFO mi{};
	if (GetModuleInformation(GetCurrentProcess(), h, &mi, sizeof(mi))) { m.base = (uintptr_t)mi.lpBaseOfDll; m.size = mi.SizeOfImage; }
	return m;
}

static bool Parse(const char* p, std::vector<int>& out) {
	while (*p) {
		if (*p == ' ') { p++; continue; }
		if (*p == '?') { out.push_back(-1); p++; if (*p == '?') p++; continue; }
		char hex[3] = { p[0], p[1], 0 };
		if (!p[1]) return false;
		out.push_back((int)strtoul(hex, nullptr, 16)); p += 2;
	}
	return !out.empty();
}

std::vector<uintptr_t> FindAll(const Module& m, const char* pattern, size_t limit) {
	std::vector<uintptr_t> hits;
	std::vector<int> pat;
	if (!m.base || !Parse(pattern, pat)) return hits;
	auto dos = (IMAGE_DOS_HEADER*)m.base;
	auto nt = (IMAGE_NT_HEADERS*)(m.base + dos->e_lfanew);
	auto sec = IMAGE_FIRST_SECTION(nt);
	for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
		if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
		auto start = (const uint8_t*)(m.base + sec->VirtualAddress);
		size_t n = sec->Misc.VirtualSize;
		if (n < pat.size()) continue;
		for (size_t off = 0; off + pat.size() <= n; off++) {
			size_t k = 0;
			for (; k < pat.size(); k++) if (pat[k] >= 0 && start[off + k] != pat[k]) break;
			if (k == pat.size()) { hits.push_back((uintptr_t)(start + off)); if (hits.size() >= limit) return hits; }
		}
	}
	return hits;
}

uintptr_t FindUnique(const Module& m, const char* pattern) {
	auto h = FindAll(m, pattern, 2);
	return h.size() == 1 ? h[0] : 0;
}

uintptr_t FindLogAlwaysHandler(const Module& m) {
	static const char* kFull =
		"40 53 48 83 EC ?? 48 8B 02 48 8B CA 48 8B DA FF 50 28 BA 01 00 00 00 48 8B CB 3B C2 74 27 48 8B 03 "
		"FF 50 28 BA 02 00 00 00 4C 8D 0D ?? ?? ?? ?? 4C 8D 05 ?? ?? ?? ?? 89 44 24 20 8D 4A 05 E8 ?? ?? ?? ?? EB 35";
	// the same without the 6 bytes MinHook overwrites (40 53 48 83 EC ??)
	static const char* kTail =
		"48 8B 02 48 8B CA 48 8B DA FF 50 28 BA 01 00 00 00 48 8B CB 3B C2 74 27 48 8B 03 FF 50 28 BA 02 00 00 00 "
		"4C 8D 0D ?? ?? ?? ?? 4C 8D 05 ?? ?? ?? ?? 89 44 24 20 8D 4A 05 E8 ?? ?? ?? ?? EB 35";
	if (uintptr_t a = FindUnique(m, kFull)) return a;
	for (uintptr_t hit : FindAll(m, kTail, 8)) {
		if (hit >= m.base + 6 && *(const uint8_t*)(hit - 6) == 0xE9) return hit - 6;   // a jump in front: hooked
	}
	return 0;
}

uintptr_t RipTarget(uintptr_t insn, int dispOffset, int insnLen) {
	return insn + insnLen + *(int32_t*)(insn + dispOffset);
}

static bool RttiNameRaw(const Module& m, uintptr_t vtable, char* out, size_t cap) {
	__try {
		if (vtable < m.base || vtable >= m.base + m.size) return false;
		auto col = *(const uint8_t**)(vtable - sizeof(void*));                 // CompleteObjectLocator
		if ((uintptr_t)col < m.base || (uintptr_t)col >= m.base + m.size) return false;
		if (*(const uint32_t*)col != 1) return false;                          // x64 COL signature
		auto td = (const char*)(m.base + *(const uint32_t*)(col + 12));       // TypeDescriptor RVA
		const char* name = td + 16;                                            // after vftable ptr + spare
		size_t i = 0;
		for (; i + 1 < cap && name[i]; i++) out[i] = name[i];
		out[i] = 0;
		return i > 0;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

std::string RttiName(const Module& m, uintptr_t vtable) {
	char buf[256];
	return RttiNameRaw(m, vtable, buf, sizeof(buf)) ? std::string(buf) : std::string();
}
