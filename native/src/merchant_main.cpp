#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include "MinHook.h"
#include "log.h"
#include "scan.h"
#include "settings.h"
#include "merchant_build_guard.h"

// WHGame.dll 1.5.6 only. All addresses/field provenance and the supplied open-hook
// correction are in docs/merchant_resale_native.md. Phase 2: this binary still never moves items or money.
// It reports COMMITTED checkouts (the shop purse moved by exactly the basket price) to Lua through
// MRN.pull(); Lua (Scripts/MerchantResale/merchant.lua) keeps the ledger and settles with the game's own
// script calls.
namespace {
HMODULE g_self = nullptr;
Module g_game;
std::atomic<bool> g_logging{true}, g_resaleSetting{true}; // Lua reads it back through MRN.pull's first line
std::atomic<unsigned long long> g_checkouts{0}, g_opens{0}, g_probes{0}, g_committed{0}, g_rejected{0};
std::atomic<bool> g_checkoutHook{false}, g_openHook{false}, g_probeHook{false}, g_bound{false};
std::mutex g_lastMu;
std::string g_last = "no shop observed";
std::mutex g_queueMu;
std::string g_queue;          // committed sale/buy lines waiting for MRN.pull(); capped
uintptr_t g_env = 0;          // SSystemGlobalEnvironment*, found on the first "bind"

constexpr uintptr_t kCheckout = 0x8d5150, kSuppliedOpen = 0xa4a118, kShopOpen = 0xc406d8;
constexpr uintptr_t kShopVtable = 0x3a949b8, kMoney = 0x8c57ac;
// Bytes read with qk data, not invented signatures (each array covers whole instructions).
const unsigned char kCheckoutBytes[] = {0x48,0x89,0x5c,0x24,0x10,0x55,0x56,0x57,0x48,0x8d,0x6c,0x24,0xb9};
const unsigned char kSuppliedBytes[] = {0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x30};
const unsigned char kShopOpenBytes[] = {0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x55,0x57,0x41,0x56};
const unsigned char kMoneyBytes[] = {0x4c,0x8b,0xdc,0x53,0x48,0x81,0xec,0x80,0,0,0};
bool g_moneyReady = false;

// Keep SEH in POD-only leaf helpers, as dice_main.cpp does (no C++ unwinding / C2712).
template<class T> bool Read(uintptr_t at, T& out) {
	__try { out = *(const T*)at; return true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool ReadFingerprint(merchantresale::BuildFingerprint& out) {
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
bool Match(uintptr_t rva, const unsigned char* bytes, size_t size) {
	if (rva > g_game.size || size > g_game.size - rva) return false;
	__try { return memcmp((const void*)(g_game.base + rva), bytes, size) == 0; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
uintptr_t VFn(uintptr_t object, uintptr_t slot) {
	uintptr_t vt = 0, fn = 0;
	if (!object || !Read(object, vt) || !vt || !Read(vt + slot, fn)) return 0;
	return fn >= g_game.base && fn - g_game.base < g_game.size ? fn : 0;
}
bool CopyText(uintptr_t source, char* out, size_t size) {
	if (!source || !size) return false;
	__try {
		for (size_t i = 0; i + 1 < size; ++i) {
			const char c = ((const char*)source)[i];
			out[i] = c == '\n' || c == '\r' || c == '\t' ? ' ' : c;
			if (!c) return true;
		}
		out[size - 1] = 0; return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) { out[0] = 0; return false; }
}
struct Guid { std::uint32_t a; std::uint16_t b, c; unsigned char d[8]; };
std::string GuidText(const Guid& g) {
	char text[40];
	std::snprintf(text, sizeof text, "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", g.a,
	              g.b, g.c, g.d[0], g.d[1], g.d[2], g.d[3], g.d[4], g.d[5], g.d[6], g.d[7]);
	return text;
}
std::string Hex(uintptr_t n) {
	char b[32]; std::snprintf(b, sizeof b, "0x%llx", (unsigned long long)n); return b;
}
std::string Quoted(const char* text) {
	std::string s = "\"";
	for (; *text; ++text) { if (*text == '"' || *text == '\\') s += '\\'; s += *text; }
	return s + '"';
}

// Read-only virtual queries seen in GetItemName (182abc25c), GetItemUIName
// (182abc354), and base-price accessor 180469510. Never call a mutation method.
bool ClassRecord(uintptr_t cls, uintptr_t& out) {
	const auto fn = VFn(cls, 0xc8);
	if (!fn) return false;
	__try { out = ((uintptr_t(__fastcall*)(uintptr_t, int))fn)(cls, 0); return out != 0; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool BasePrice(uintptr_t cls, int& out) {
	const auto has = VFn(cls, 0x18), get = VFn(cls, 0xc0);
	if (!has || !get) return false;
	__try {
		if (!((bool(__fastcall*)(uintptr_t, int))has)(cls, 0x16)) { out = 0; return true; }
		const auto record = ((uintptr_t(__fastcall*)(uintptr_t, int))get)(cls, 0);
		if (!record) return false;
		out = *(const int*)(record + 0x5c); return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Owns(uintptr_t inventory, uintptr_t item, bool& out) {
	const auto fn = VFn(inventory + 8, 0x18);
	if (!fn) return false;
	__try { out = ((bool(__fastcall*)(uintptr_t, uintptr_t))fn)(inventory + 8, item); return true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Money(uintptr_t collection, int& out) {
	if (!g_moneyReady || !collection) return false;
	__try {
		int buffer[8] = {};
		((int*(__fastcall*)(uintptr_t, int*))(g_game.base + kMoney))(collection, buffer);
		out = buffer[0]; return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

std::string ShopText(uintptr_t shop, uintptr_t keeper = 0) {
	uintptr_t vt = 0;
	if (!shop || !Read(shop, vt) || vt != g_game.base + kShopVtable)
		return "shop_ptr=" + Hex(shop) + " merchant_name=U merchant_id=U purse=U reason=unverified_shop_type";
	int dbid = 0, purse = 0;
	const bool db = Read(shop + 0x1b0, dbid);
	if (!keeper) Read(shop + 0x188, keeper);
	const bool money = Money(shop + 8, purse);
	// keeper is the exact WUID argument stored by C_Shop's open path, NOT a guessed CEntity offset.
	return "shop_ptr=" + Hex(shop) + " shop_dbid=" + (db ? std::to_string(dbid) : "U") +
	       " merchant_wuid=" + Hex(keeper) + " merchant_name=U entity_id32=U position=U purse_deci=" +
	       (money ? std::to_string(purse) : "U");
}
std::string ItemText(uintptr_t item, uintptr_t shop) {
	std::uint32_t count = 0; uintptr_t cls = 0, record = 0, name = 0, ui = 0, id = 0;
	Guid guid{}; int base = 0; bool owns = false;
	const bool haveCount = item && Read(item + 0x50, count);
	const bool haveClass = item && Read(item + 0x48, cls) && cls;
	const bool haveGuid = haveClass && Read(cls + 8, guid);
	const bool haveBase = haveClass && BasePrice(cls, base);
	const bool direction = shop && item && Owns(shop, item, owns);
	char className[160] = {}, uiName[160] = {};
	if (haveClass && ClassRecord(cls, record)) {
		if (Read(record + 0x18, name)) CopyText(name, className, sizeof className);
		if (Read(record + 0xa0, ui)) CopyText(ui, uiName, sizeof uiName);
	}
	if (item) Read(item + 0x30, id); // opaque instance token, never dereferenced or retained for mutation
	return "{item=" + Hex(item) + " instance_token=" + Hex(id) + " class_name=" +
	       (className[0] ? Quoted(className) : "U") + " ui_name_key=" + (uiName[0] ? Quoted(uiName) : "U") +
	       " guid=" + (haveGuid ? GuidText(guid) : "U") + " count=" + (haveCount ? std::to_string(count) : "U") +
	       " base_price_deci=" + (haveBase ? std::to_string(base) : "U") + " category=U direction=" +
	       (direction ? (owns ? "buy" : "sell") : "U") + "}";
}
std::string CheckoutText(uintptr_t tx, int price) {
	uintptr_t player = 0, shop = 0, shopId = 0, begin = 0, end = 0;
	const bool context = Read(tx + 0x10, player) && Read(tx + 0x18, shop) && Read(tx + 8, shopId);
	std::string line = "checkout attempt final_price_deci=" + std::to_string(price) +
	                   " net_direction_I=" + (price > 0 ? "player_pays" : price < 0 ? "shop_pays" : "balanced") +
	                   " shop_inventory_wuid=" + Hex(shopId) + " player_owner_ptr=" + Hex(player) + " " + ShopText(shop);
	if (!context || !Read(tx + 0x98, begin) || !Read(tx + 0xa0, end) || end < begin ||
	    (end - begin) % sizeof(uintptr_t) || (end - begin) / sizeof(uintptr_t) > 4096 || (end != begin && !begin))
		return line + " basket=U_bad_or_oversized_vector";
	line += " basket_rows=" + std::to_string((end - begin) / sizeof(uintptr_t));
	for (uintptr_t at = begin; at != end; at += sizeof(uintptr_t)) {
		uintptr_t item = 0;
		line += Read(at, item) && item ? " " + ItemText(item, shop) : " {item=U_read_failed}";
	}
	return line + " commit_success=U"; // original may reject payment internally; hook entry is not proof of success
}

using CheckoutFn = void(__fastcall*)(uintptr_t, int, uintptr_t);
using SuppliedOpenFn = void(__fastcall*)(uintptr_t);
using ShopOpenFn = void(__fastcall*)(uintptr_t, uintptr_t);
CheckoutFn g_origCheckout = nullptr;
SuppliedOpenFn g_origSupplied = nullptr;
ShopOpenFn g_origOpen = nullptr;

// One basket row as Lua needs it. Read BEFORE the original runs (it moves/merges/frees the items).
struct Row { char dir; std::string guid; unsigned count; int base; uintptr_t token; };
bool Snapshot(uintptr_t tx, uintptr_t& shop, int& dbid, std::vector<Row>& rows) {
	uintptr_t vt = 0, begin = 0, end = 0;
	if (!Read(tx + 0x18, shop) || !shop || !Read(shop, vt) || vt != g_game.base + kShopVtable) return false;
	if (!Read(shop + 0x1b0, dbid)) return false;
	if (!Read(tx + 0x98, begin) || !Read(tx + 0xa0, end) || end < begin || (end - begin) % sizeof(uintptr_t) ||
	    (end - begin) / sizeof(uintptr_t) > 4096 || (end != begin && !begin)) return false;
	for (uintptr_t at = begin; at != end; at += sizeof(uintptr_t)) {
		uintptr_t item = 0, cls = 0, token = 0; std::uint32_t count = 0; Guid guid{}; int base = 0; bool owns = false;
		if (!Read(at, item) || !item || !Read(item + 0x50, count) || !count || !Read(item + 0x48, cls) || !cls ||
		    !Read(cls + 8, guid) || !BasePrice(cls, base) || !Owns(shop, item, owns)) return false;   // all or nothing
		Read(item + 0x30, token);
		rows.push_back({owns ? 'b' : 's', GuidText(guid), count, base, token});
	}
	return true;
}
void Queue(int dbid, const std::vector<Row>& rows) {
	std::lock_guard<std::mutex> lock(g_queueMu);
	for (const auto& r : rows) {
		if (g_queue.size() > 64 * 1024) { Log("queue full (Lua not pulling?): dropped the rest of this checkout"); return; }
		char line[160];
		std::snprintf(line, sizeof line, "%s %d %s %u %d %016llX\n", r.dir == 's' ? "sell" : "buy", dbid, r.guid.c_str(),
		              r.count, r.base, (unsigned long long)r.token);
		g_queue += line;
	}
}
void __fastcall HkCheckout(uintptr_t tx, int price, uintptr_t result) {
	++g_checkouts;
	// Snapshot BEFORE the original transfers/frees basket items. No raw pointer escapes the call.
	if (g_logging.load()) try { Log("%s", CheckoutText(tx, price).c_str()); }
	catch (...) { Log("checkout snapshot failed (C++ exception); original still called"); }
	std::vector<Row> rows; uintptr_t shop = 0; int dbid = -1, before = 0, after = 0; bool snap = false, haveBefore = false;
	try { snap = Snapshot(tx, shop, dbid, rows); } catch (...) { snap = false; }
	if (snap) haveBefore = Money(shop + 8, before);
	g_origCheckout(tx, price, result); // exactly once; never swallow an exception in the original
	if (!snap) { if (g_logging.load()) Log("commit check skipped: basket snapshot incomplete (nothing reported to Lua)"); return; }
	// Committed = the shop purse moved by exactly the basket price (+ = player paid the shop). A rejected
	// payment leaves the purse unchanged. A zero-price swap can't be told apart, so it is reported as committed.
	const bool haveAfter = Money(shop + 8, after);
	const bool committed = haveBefore && haveAfter && after - before == price;
	if (committed) { ++g_committed; if (g_bound.load()) try { Queue(dbid, rows); } catch (...) { Log("queue failed"); } }
	else ++g_rejected;
	if (g_logging.load())
		Log("commit %s shop_dbid=%d price_deci=%d purse_deci %d -> %d rows=%zu%s", committed ? "YES" : "NO", dbid, price,
		    before, after, rows.size(), committed && !g_bound.load() ? " (Lua not bound yet: not queued)" : "");
}
void __fastcall HkSupplied(uintptr_t self) {
	++g_probes;
	if (g_logging.load()) Log("shop_open_probe self=%p merchant=U purse=U supplied_address_is_listener_registration", (void*)self);
	g_origSupplied(self);
}
void __fastcall HkOpen(uintptr_t shop, uintptr_t keeper) {
	++g_opens;
	try {
		const auto line = "shop_open before_original " + ShopText(shop, keeper);
		{ std::lock_guard<std::mutex> lock(g_lastMu); g_last = line; }
		if (g_logging.load()) Log("%s", line.c_str());
	} catch (...) { Log("shop_open snapshot failed (C++ exception); original still called"); }
	g_origOpen(shop, keeper);
}

// ---- MRN: the Lua binding (docs/whgame_re/script_binding.md, same route as probe.cpp / rounds.cpp DN).
// IScriptSystem = gEnv+0x28: +0x68 CreateTable(bool), +0xF8 SetGlobalAny. IScriptTable +0x18 AddRef, +0xB0 AddFunction.
// IFunctionHandler +0x50 EndFunctionAny(a), +0x58 EndFunction(). ScriptAnyValue: type +0 (1 nil, 2 bool, 5 string).
struct Any { int type; int pad; union { bool b; const char* s; uintptr_t p; }; std::uint64_t extra; };
static_assert(sizeof(Any) == 0x18, "ScriptAnyValue is 0x18 bytes");
uintptr_t VF(uintptr_t object, int slot) { uintptr_t vt = 0, fn = 0; return object && Read(object, vt) && vt && Read(vt + slot, fn) ? fn : 0; }
bool ValidEnv(uintptr_t env) {
	uintptr_t rnd = 0, vt = 0;
	if (!Read(env + 0x108, rnd) || !rnd || !Read(rnd, vt)) return false;
	return RttiName(g_game, vt).find("Renderer") != std::string::npos;
}
uintptr_t FindEnv() {   // the same two RIP-relative patterns main.cpp uses
	uintptr_t cands[2] = {};
	if (uintptr_t a = FindUnique(g_game, "48 8D 05 ?? ?? ?? ?? 48 89 0F 4C 8D 67 28 48 8D 0D ?? ?? ?? ?? 48 89 47 20 48 89 4F 08"))
		cands[0] = RipTarget(a, 3, 7);
	if (uintptr_t b = FindUnique(g_game, "48 8B 51 08 48 8B C8 FF D2 48 8B 0D ?? ?? ?? ?? 48 85 C9"))
		cands[1] = RipTarget(b + 9, 3, 7);
	for (uintptr_t c : cands) {
		if (!c) continue;
		if (ValidEnv(c)) return c;
		uintptr_t d = 0;
		if (Read(c, d) && d && ValidEnv(d)) return d;
	}
	return 0;
}
std::string g_pulled;   // keeps the returned text alive until Lua has copied it
int __cdecl MrnPull(uintptr_t pH, void*, int) {
	Any r{}; r.type = 1;   // nil when nothing is waiting
	{
		std::lock_guard<std::mutex> lock(g_queueMu);
		g_pulled.swap(g_queue); g_queue.clear();
	}
	if (!g_pulled.empty()) { r.type = 5; r.s = g_pulled.c_str(); }
	auto end = (int(__fastcall*)(uintptr_t, const Any*))VF(pH, 0x50);
	return end ? end(pH, &r) : 0;
}
int __cdecl MrnEnabled(uintptr_t pH, void*, int) {
	Any r{}; r.type = 2; r.b = g_resaleSetting.load();
	auto end = (int(__fastcall*)(uintptr_t, const Any*))VF(pH, 0x50);
	return end ? end(pH, &r) : 0;
}
bool AddFn(uintptr_t table, const char* name, void* fn) {
	alignas(16) unsigned char desc[0x58] = {};
	static int marker = 0x4D52;   // a non-empty data buffer selects the user-data-function closure
	*(const char**)(desc + 0x00) = name;
	*(const char**)(desc + 0x08) = "";
	*(const char**)(desc + 0x10) = "MRN";
	*(void**)(desc + 0x40) = fn;
	*(void**)(desc + 0x48) = &marker;
	*(int*)(desc + 0x50) = sizeof(marker);
	auto add = (bool(__fastcall*)(uintptr_t, const unsigned char*))VF(table, 0xB0);
	return add && add(table, desc);
}
int SafeBind() {
	__try {
		uintptr_t ss = 0;
		if (!g_env || !Read(g_env + 0x28, ss) || !ss) return -1;
		auto create = (uintptr_t(__fastcall*)(uintptr_t, bool))VF(ss, 0x68);
		uintptr_t t = create ? create(ss, false) : 0;
		if (!t) return -2;
		((void(__fastcall*)(uintptr_t))VF(t, 0x18))(t);   // AddRef: kept for the session
		const int n = AddFn(t, "pull", (void*)&MrnPull) + AddFn(t, "enabled", (void*)&MrnEnabled);
		Any g{}; g.type = 6; g.p = t;
		auto setGlobal = (void(__fastcall*)(uintptr_t, const char*, const Any*))VF(ss, 0xF8);
		if (!setGlobal) return -3;
		setGlobal(ss, "MRN", &g);
		return n;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return -100; }
}
void Bind() {   // runs on the Lua thread (from HkLog); Lua re-sends "bind" after every load, so it must be repeatable
	if (!g_env) g_env = FindEnv();
	const int r = SafeBind();
	g_bound.store(r == 2);
	Log("bind: %s (%d, gEnv %p)", r == 2 ? "MRN.pull / MRN.enabled registered" : "FAILED", r, (void*)g_env);
}

void Status() {
	std::lock_guard<std::mutex> lock(g_lastMu);
	Log("status phase=2 logging=%d resale=%d lua_bound=%d checkout_hook=%d shop_open_hook=%d checkouts=%llu committed=%llu not_committed=%llu opens=%llu last=[%s]",
	    g_logging.load(), g_resaleSetting.load(), g_bound.load(), g_checkoutHook.load(), g_openHook.load(),
	    g_checkouts.load(), g_committed.load(), g_rejected.load(), g_opens.load(), g_last.c_str());
}
void Command(const char* line, bool persist = true) {
	char key[32] = {}, value[8] = {}, extra[8] = {};
	if (!strcmp(line, "hello") || !strcmp(line, "status")) { Status(); return; }
	if (!strcmp(line, "bind")) { Bind(); return; }
	if (sscanf_s(line, "set %31s %7s %7s", key, (unsigned)sizeof key, value, (unsigned)sizeof value,
	             extra, (unsigned)sizeof extra) != 2 || (strcmp(value, "0") && strcmp(value, "1"))) return;
	if (!strcmp(key, "logging")) g_logging.store(value[0] == '1');
	else if (!strcmp(key, "resale")) g_resaleSetting.store(value[0] == '1');
	else return;
	if (persist) SettingsSave(key, value);
	Log("setting %s=%s", key, value);
}
using LogHandlerFn = int(__fastcall*)(void*, uintptr_t);
using GetParamStrFn = bool(__fastcall*)(uintptr_t, int, const char**);
LogHandlerFn g_origLog = nullptr;
GetParamStrFn g_getParamStr = nullptr;
bool LuaLine(uintptr_t handler, char* out, size_t size) {
	__try {
		const char* source = nullptr;
		if (!g_getParamStr || !g_getParamStr(handler, 1, &source)) return false;
		return CopyText((uintptr_t)source, out, size);
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
int __fastcall HkLog(void* self, uintptr_t handler) {
	char line[256] = {};
	if (LuaLine(handler, line, sizeof line) && !strncmp(line, "[MERCHANT] ", 11)) {
		try { Command(line + 11); } catch (...) { Log("merchant command failed"); }
	}
	return g_origLog(self, handler); // do not suppress or rewrite even the mod's Lua logs
}

bool Install(uintptr_t rva, const unsigned char* bytes, size_t size, void* hook, void** original) {
	if (!Match(rva, bytes, size)) { Log("hook RVA %llx bytes differ; NOT installed", (unsigned long long)rva); return false; }
	void* target = (void*)(g_game.base + rva);
	const auto create = MH_CreateHook(target, hook, original);
	if (create != MH_OK) { Log("hook RVA %llx create failed: %d", (unsigned long long)rva, (int)create); return false; }
	const auto enable = MH_EnableHook(target);
	if (enable != MH_OK) { MH_RemoveHook(target); Log("hook enable failed: %d", (int)enable); return false; }
	return true;
}
void InstallObservedHooks() {
	if (MH_Initialize() != MH_OK) { Log("MinHook init failed; nothing hooked"); return; }
	g_moneyReady = Match(kMoney, kMoneyBytes, sizeof kMoneyBytes);
	g_checkoutHook.store(Install(kCheckout, kCheckoutBytes, sizeof kCheckoutBytes, (void*)&HkCheckout, (void**)&g_origCheckout));
	g_probeHook.store(Install(kSuppliedOpen, kSuppliedBytes, sizeof kSuppliedBytes, (void*)&HkSupplied, (void**)&g_origSupplied));
	// Additional read-only hook: evidence disproved the design's address as a true shop open.
	g_openHook.store(Install(kShopOpen, kShopOpenBytes, sizeof kShopOpenBytes, (void*)&HkOpen, (void**)&g_origOpen));
	const auto get = FindUnique(g_game, "48 89 5C 24 08 57 48 83 EC ?? 48 8B 01 49 8B F8 4C 8D 44 24 20 C7 44 24 20 05 00 00 00 FF 50 38 32 DB 84 C0 74 11");
	const auto log = FindLogAlwaysHandler(g_game);
	g_getParamStr = (GetParamStrFn)get;
	if (get && log && MH_CreateHook((void*)log, (void*)&HkLog, (void**)&g_origLog) == MH_OK &&
	    MH_EnableHook((void*)log) == MH_OK) Log("Lua command channel installed (replies in merchant.log)");
	else Log("Lua command channel unavailable; startup settings and default logging still apply");
	Status();
}
DWORD WINAPI Init(LPVOID) {
	Log("Merchant Resale phase 2 (WHGame 1.5.6): reports committed checkouts to Lua (MRN.pull); never moves items or money itself");
	for (int i = 0; i < 600 && !GetModuleHandleA("WHGame.dll"); ++i) Sleep(100);
	g_game = GetModule("WHGame.dll");
	if (!g_game.base) { Log("WHGame.dll not found; nothing hooked"); return 0; }
	for (const auto& saved : SettingsReplay()) Command(saved.c_str(), false);
	merchantresale::BuildFingerprint fingerprint;
	// No game getter, pattern scan, MinHook initialization, or hook installation before this gate.
	if (!ReadFingerprint(fingerprint) || !merchantresale::ForKnownBuild(fingerprint, InstallObservedHooks))
		Log("WHGame build fingerprint missing/mismatched; ZERO hooks installed (including Lua channel)");
	Status();
	return 0;
}
} // namespace

BOOL APIENTRY DllMain(HMODULE self, DWORD reason, LPVOID) {
	if (reason == DLL_PROCESS_ATTACH) {
		g_self = self;
		DisableThreadLibraryCalls(self);
		LogInit(self);
		SettingsInit(self, "merchant_settings.txt", "set");
		HANDLE thread = CreateThread(nullptr, 0, Init, nullptr, 0, nullptr);
		if (thread) CloseHandle(thread);
	}
	return TRUE;
}
