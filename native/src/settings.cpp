#include "settings.h"
#include "log.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <utility>

static std::string g_path;
static std::vector<std::pair<std::string, std::string>> g_set;
static std::mutex g_mu;

// a key is a cfg path (accuracy.scale); a value is one line of plain text that can't end the console command it's
// replayed in (no ';' or '"')
static bool GoodKey(const char* k) {
	size_t n = strlen(k);
	if (n == 0 || n > 64) return false;
	for (size_t i = 0; i < n; i++) {
		char c = k[i];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.')) return false;
	}
	return true;
}
static bool GoodValue(const char* v) {
	size_t n = strlen(v);
	if (n == 0 || n > 200) return false;
	for (size_t i = 0; i < n; i++) if (v[i] < 0x20 || v[i] > 0x7E || v[i] == ';' || v[i] == '"') return false;
	return true;
}
static std::string Trim(const char* s) {
	while (*s == ' ' || *s == '\t') s++;
	std::string out(s);
	while (!out.empty() && (out.back() == ' ' || out.back() == '\t' || out.back() == '\r' || out.back() == '\n')) out.pop_back();
	return out;
}

static void Write() {   // whole file, through a temporary one so a crash mid-write can't leave half of it
	std::string tmp = g_path + ".tmp";
	FILE* f = nullptr;
	if (fopen_s(&f, tmp.c_str(), "w") || !f) { Log("settings: can't write %s", tmp.c_str()); return; }
	fprintf(f, "# Desert Eagle mod settings (deagle_set); deagle_reset clears them\n");
	for (auto& kv : g_set) fprintf(f, "%s %s\n", kv.first.c_str(), kv.second.c_str());
	fclose(f);
	if (!MoveFileExA(tmp.c_str(), g_path.c_str(), MOVEFILE_REPLACE_EXISTING)) Log("settings: can't replace %s (%lu)", g_path.c_str(), GetLastError());
}

void SettingsInit(void* module) {
	std::lock_guard<std::mutex> lk(g_mu);
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)module, path, MAX_PATH);
	char* slash = strrchr(path, '\\');
	if (!slash) return;
	strcpy_s(slash + 1, path + MAX_PATH - (slash + 1), "deagle_settings.txt");
	g_path = path;
	FILE* f = nullptr;
	if (fopen_s(&f, path, "r") || !f) { Log("settings: none yet (%s)", path); return; }
	char line[512];
	while (fgets(line, sizeof(line), f)) {
		std::string l = Trim(line);
		if (l.empty() || l[0] == '#') continue;
		size_t sp = l.find(' ');
		if (sp == std::string::npos) continue;
		std::string k = l.substr(0, sp), v = Trim(l.c_str() + sp + 1);
		if (!GoodKey(k.c_str()) || !GoodValue(v.c_str())) { Log("settings: skipped line \"%s\"", l.c_str()); continue; }
		bool found = false;
		for (auto& kv : g_set) if (kv.first == k) { kv.second = v; found = true; }
		if (!found) g_set.emplace_back(k, v);
	}
	fclose(f);
	Log("settings: %zu from %s", g_set.size(), path);
}

void SettingsSave(const char* key, const char* value) {
	std::lock_guard<std::mutex> lk(g_mu);
	std::string v = Trim(value);
	if (!GoodKey(key) || !GoodValue(v.c_str())) { Log("settings: not kept: %s = \"%s\"", key, value); return; }
	for (auto& kv : g_set) {
		if (kv.first != key) continue;
		if (kv.second == v) return;
		kv.second = v; Write(); Log("settings: %s = %s", key, v.c_str());
		return;
	}
	g_set.emplace_back(key, v); Write(); Log("settings: %s = %s", key, v.c_str());
}

void SettingsReset() {
	std::lock_guard<std::mutex> lk(g_mu);
	g_set.clear();
	if (!g_path.empty()) DeleteFileA(g_path.c_str());
	Log("settings: cleared");
}

std::vector<std::string> SettingsReplay() {
	std::lock_guard<std::mutex> lk(g_mu);
	std::vector<std::string> out;
	for (auto& kv : g_set) out.push_back("deagle_set " + kv.first + " " + kv.second);
	return out;
}
