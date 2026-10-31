#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "MinHook.h"
#include "log.h"
#include "scan.h"
#include "settings.h"
#include "merchant_build_guard.h"

// reach.asi (the Unarmed Reach mod), WHGame.dll 1.5.6 only: the faster Henry attacks, the less NPCs dodge.
// The game picks an NPC's defence from weights (C_RPGCombat vtable +0x1b0, FUN_180c5683c,
// "Combat_NpcDefenseChoiceWeights"); weight [4] is the dodge (CombatDodgeProb0/1 lerp x CombatDodgeProbCoef).
// Nothing in it looks at attack speed. Henry's attack speed multiplier comes from C_RPGCombat vtable +0x120
// (FUN_180d4de0c, "Combat_AttackSpeedMultiplier", (this, attacker, defender), 1 .. 1 + MaxAttackSpeedPlayerMod).
// This plugin records that multiplier for the player's attacks and divides the dodge weight of the NPC he attacks
// by speed ^ dodge_speed_exp (reach_settings.txt, default 1.5: x1.9 speed -> x0.38 dodge). The other weights
// (block, parry, ...) are untouched, so the NPC blocks instead. docs/whgame_re/combat_speed_dodge.md.
namespace {
Module g_game;
constexpr uintptr_t kWeights = 0xc5683c, kSpeed = 0xd4de0c;
const unsigned char kWeightsBytes[] = {0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x70,0x10,0x55,0x57};
const unsigned char kSpeedBytes[] = {0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x70,0x10,0x48,0x89,0x78,0x18};
std::atomic<float> g_exp{1.5f};
std::atomic<unsigned long long> g_speedCalls{0}, g_playerAttacks{0}, g_weightCalls{0}, g_scaled{0}, g_byTime{0};
// the player's last attack: its speed multiplier, the defender object passed with it, when (GetTickCount64)
std::atomic<float> g_lastSpeed{1.f};
std::atomic<uintptr_t> g_lastDefender{0};
std::atomic<unsigned long long> g_lastTick{0};
constexpr unsigned long long kRecentMs = 1500;

bool Match(uintptr_t rva, const unsigned char* bytes, size_t size) {
	if (rva > g_game.size || size > g_game.size - rva) return false;
	__try { return memcmp((const void*)(g_game.base + rva), bytes, size) == 0; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool ReadFingerprint(merchantresale::BuildFingerprint& out) {   // same check as merchant_main.cpp
	__try {
		if (g_game.size < sizeof(IMAGE_DOS_HEADER)) return false;
		const auto* dos = (const IMAGE_DOS_HEADER*)g_game.base;
		if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
		    (size_t)dos->e_lfanew > g_game.size || sizeof(IMAGE_NT_HEADERS64) > g_game.size - dos->e_lfanew) return false;
		const auto* nt = (const IMAGE_NT_HEADERS64*)(g_game.base + dos->e_lfanew);
		if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
		    nt->FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64) ||
		    nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_DEBUG ||
		    nt->OptionalHeader.SizeOfImage != g_game.size) return false;
		out.machine = nt->FileHeader.Machine; out.sections = nt->FileHeader.NumberOfSections;
		out.timestamp = nt->FileHeader.TimeDateStamp; out.codeSize = nt->OptionalHeader.SizeOfCode;
		out.imageSize = nt->OptionalHeader.SizeOfImage; out.entryPoint = nt->OptionalHeader.AddressOfEntryPoint;
		const auto debug = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
		if (!debug.VirtualAddress || !debug.Size || debug.Size % sizeof(IMAGE_DEBUG_DIRECTORY) ||
		    debug.Size / sizeof(IMAGE_DEBUG_DIRECTORY) > 64 || debug.VirtualAddress > g_game.size ||
		    debug.Size > g_game.size - debug.VirtualAddress) return false;
		const auto* entries = (const IMAGE_DEBUG_DIRECTORY*)(g_game.base + debug.VirtualAddress);
		for (size_t i = 0; i < debug.Size / sizeof(IMAGE_DEBUG_DIRECTORY); ++i) {
			if (entries[i].Type != IMAGE_DEBUG_TYPE_CODEVIEW) continue;
			const auto rva = entries[i].AddressOfRawData;
			if (!rva || rva > g_game.size || entries[i].SizeOfData < 24 ||
			    entries[i].SizeOfData > g_game.size - rva) return false;
			const auto address = g_game.base + rva;
			if (*(const std::uint32_t*)address != 0x53445352) return false; // RSDS
			memcpy(out.pdbGuid.data(), (const void*)(address + 4), out.pdbGuid.size());
			out.pdbAge = *(const std::uint32_t*)(address + 20);
			return true;
		}
		return false;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// the game's own first call in FUN_180d4de0c: attacker->vtbl[0x410](attacker, 0) != 0 selects the PLAYER speed mod
bool IsPlayer(uintptr_t actor, bool& out) {
	__try {
		const uintptr_t vt = *(const uintptr_t*)actor;
		const auto fn = *(uintptr_t(__fastcall* const*)(uintptr_t, int))(vt + 0x410);
		out = fn(actor, 0) != 0;
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

using SpeedFn = float(__fastcall*)(uintptr_t, uintptr_t, uintptr_t);
SpeedFn g_origSpeed = nullptr;
float __fastcall HkSpeed(uintptr_t self, uintptr_t attacker, uintptr_t defender) {
	const float m = g_origSpeed(self, attacker, defender);
	++g_speedCalls;
	bool player = false;
	if (attacker && IsPlayer(attacker, player) && player && std::isfinite(m) && m > 0.f) {
		g_lastSpeed.store(m);
		g_lastDefender.store(defender);
		g_lastTick.store(GetTickCount64());
		if (++g_playerAttacks <= 20) Log("player attack speed x%.3f (defender %p)", m, (void*)defender);
	}
	return m;
}

using WeightsFn = void(__fastcall*)(uintptr_t, float*, uintptr_t*);
WeightsFn g_origWeights = nullptr;
bool ReadCtx(const uintptr_t* ctx, uintptr_t& a, uintptr_t& b) {
	__try { a = ctx[0]; b = ctx[1]; return true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void __fastcall HkWeights(uintptr_t self, float* out, uintptr_t* ctx) {
	g_origWeights(self, out, ctx);
	const auto n = ++g_weightCalls;
	uintptr_t a = 0, b = 0;
	if (!out || !ctx || !ReadCtx(ctx, a, b)) return;
	const unsigned long long age = GetTickCount64() - g_lastTick.load();
	if (age > kRecentMs) {
		if (n <= 20) Log("defence weights %.3f %.3f %.3f %.3f %.3f: no player attack in the last %llu ms (ctx %p %p)",
		                 out[0], out[1], out[2], out[3], out[4], kRecentMs, (void*)a, (void*)b);
		return;
	}
	const uintptr_t d = g_lastDefender.load();
	const bool match = d && (a == d || b == d);
	// matched by object = this NPC is the one Henry is hitting; otherwise only a just-started attack (0.6 s) counts
	if (!match && age > 600) return;
	const float m = g_lastSpeed.load(), k = g_exp.load();
	if (m <= 1.001f || k <= 0.f) return;
	const float scale = std::pow(m, -k), before = out[4];
	out[4] = before * scale;
	if (!match) ++g_byTime;
	if (++g_scaled <= 40)
		Log("dodge weight %.3f -> %.3f (speed x%.3f ^ -%.2f, %s, %llu ms after the attack; others %.3f %.3f %.3f %.3f)",
		    before, out[4], m, k, match ? "same defender" : "by time only", age, out[0], out[1], out[2], out[3]);
}

bool Install(uintptr_t rva, const unsigned char* bytes, size_t size, void* hook, void** original) {
	if (!Match(rva, bytes, size)) { Log("hook RVA %llx bytes differ; NOT installed", (unsigned long long)rva); return false; }
	void* target = (void*)(g_game.base + rva);
	if (MH_CreateHook(target, hook, original) != MH_OK) { Log("hook RVA %llx create failed", (unsigned long long)rva); return false; }
	if (MH_EnableHook(target) != MH_OK) { MH_RemoveHook(target); Log("hook RVA %llx enable failed", (unsigned long long)rva); return false; }
	return true;
}
void InstallHooks() {
	if (MH_Initialize() != MH_OK) { Log("MinHook init failed; nothing hooked"); return; }
	// the speed hook first: the dodge hook only acts on what it records
	const bool speed = Install(kSpeed, kSpeedBytes, sizeof kSpeedBytes, (void*)&HkSpeed, (void**)&g_origSpeed);
	const bool weights = speed && Install(kWeights, kWeightsBytes, sizeof kWeightsBytes, (void*)&HkWeights, (void**)&g_origWeights);
	Log("hooks: attack speed %s, NPC defence weights %s; dodge weight x speed ^ -%.2f", speed ? "on" : "OFF",
	    weights ? "on" : "OFF", g_exp.load());
}
DWORD WINAPI Init(LPVOID) {
	Log("Unarmed Reach plugin (WHGame 1.5.6): NPCs dodge less against faster attacks");
	for (int i = 0; i < 600 && !GetModuleHandleA("WHGame.dll"); ++i) Sleep(100);
	g_game = GetModule("WHGame.dll");
	if (!g_game.base) { Log("WHGame.dll not found; nothing hooked"); return 0; }
	for (const auto& line : SettingsReplay()) {   // "set dodge_speed_exp 1.5"
		char key[32] = {}; float v = 0.f;
		if (sscanf_s(line.c_str(), "set %31s %f", key, (unsigned)sizeof key, &v) == 2 && !strcmp(key, "dodge_speed_exp") &&
		    v >= 0.f && v <= 4.f) g_exp.store(v);
	}
	merchantresale::BuildFingerprint fingerprint;
	if (!ReadFingerprint(fingerprint) || !merchantresale::ForKnownBuild(fingerprint, InstallHooks))
		Log("WHGame build fingerprint missing/mismatched; ZERO hooks installed");
	return 0;
}
} // namespace

BOOL APIENTRY DllMain(HMODULE self, DWORD reason, LPVOID) {
	if (reason == DLL_PROCESS_ATTACH) {
		DisableThreadLibraryCalls(self);
		LogInit(self);
		SettingsInit(self, "reach_settings.txt", "set");
		HANDLE thread = CreateThread(nullptr, 0, Init, nullptr, 0, nullptr);
		if (thread) CloseHandle(thread);
	}
	return TRUE;
}
