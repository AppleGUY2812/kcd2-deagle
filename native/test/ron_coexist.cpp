// Coexistence test for ron.cpp, outside the game: load KCD2's own FMOD 2.02 (fmod.dll + fmodstudio.dll) and start a
// 2.02 core system the way the game and the plugin do, THEN bring up Ready or Not's renamed 2.03.10 and render one
// MK V shot in a small room offline. If both versions can't live in one process, this fails or crashes.
//   ron_coexist <kcd2 Bin\Win64Shared\> <Mods\deagle\ron\> <out.wav> [snapshot or event to hold] [IsOutside] [global=value ...]
#include "../src/ron.h"
#include "../src/log.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
	if (argc < 4) { printf("usage: ron_coexist <kcd2 Win64Shared dir\\> <ron dir\\> <out.wav> [snapshot] [IsOutside]\n"); return 2; }
	LogInit(nullptr);
	std::string kcd = argv[1];
	HMODULE c = LoadLibraryExA((kcd + "fmod.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
	HMODULE s = LoadLibraryExA((kcd + "fmodstudio.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
	printf("KCD2 fmod.dll %p fmodstudio.dll %p\n", (void*)c, (void*)s);
	if (!c || !s) return 1;
	typedef int (__stdcall* Create)(void**, unsigned); typedef int (__stdcall* Init)(void*, int, unsigned, void*);
	void* kcdSys = nullptr;
	int r1 = ((Create)GetProcAddress(c, "FMOD_System_Create"))(&kcdSys, 0x00020221);
	int r2 = ((Init)GetProcAddress(c, "FMOD_System_Init"))(kcdSys, 64, 0, nullptr);
	printf("KCD2's 2.02 core system: create %d init %d\n", r1, r2);
	printf("module fmod.dll is %p (must stay KCD2's)\n", (void*)GetModuleHandleA("fmod.dll"));
	bool live = !strcmp(argv[3], "-");   // "-": play to the speakers, like in game
	if (!RonInit(argv[2], live ? nullptr : argv[3])) { printf("RonInit FAILED (see ron_coexist.log)\n"); return 1; }
	printf("module fmor.dll %p fmorstudio.dll %p\n", (void*)GetModuleHandleA("fmor.dll"), (void*)GetModuleHandleA("fmorstudio.dll"));
	const char* snap = argc > 4 ? argv[4] : "snapshot:/Reverb_IN_Small01";
	const char* outside = argc > 5 ? argv[5] : "0";
	printf("hold %s: %d\n", snap, (int)RonHold(snap, true));
	for (int i = 6; i < argc; i++) {
		char name[64] = {}; float v = 0;
		if (sscanf_s(argv[i], "%63[^=]=%f", name, (unsigned)sizeof(name), &v) == 2) { RonGlobal(name, v); printf("global %s = %.2f\n", name, v); }
	}
	if (live) { for (int i = 0; i < 20; i++) { RonUpdate(nullptr); Sleep(20); } } else RonStep(200);
	std::string params = std::string("IsOutside=") + outside;
	bool ok = RonPlay("event:/Weapons/Grizzly/1P Grizzly Fire", params.c_str(), nullptr);
	printf("play: %d\n", (int)ok);
	if (live) { for (int i = 0; i < 200; i++) { RonUpdate(nullptr); Sleep(20); } }
	else RonStep(190);   // ~4 s at 1024 samples / 48 kHz
	RonRelease();
	printf("wrote %s\n", argv[3]);
	return ok ? 0 : 1;
}
