#include "ron.h"
#include "log.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

// The FMOD C API, by GetProcAddress from RoN's renamed DLLs (KCD2's own fmod.dll / fmodstudio.dll 2.02 are loaded
// under the original names; the studio copy's import was patched to fmor.dll, so the two versions never mix).
struct FV { float x, y, z; };
struct F3D { FV pos, vel, fwd, up; };
typedef int (__stdcall* S_Create)(void** sys, unsigned ver);
typedef int (__stdcall* S_Core)(void* sys, void** core);
typedef int (__stdcall* S_Init)(void* sys, int maxch, unsigned sflags, unsigned flags, void* extra);
typedef int (__stdcall* S_LoadBank)(void* sys, const char* file, unsigned flags, void** bank);
typedef int (__stdcall* S_Sys)(void* sys);
typedef int (__stdcall* S_GetEvent)(void* sys, const char* path, void** desc);
typedef int (__stdcall* S_SysParam)(void* sys, const char* name, float v, int ignoreseek);
typedef int (__stdcall* S_Listener)(void* sys, int idx, const F3D* attr, const FV* attenPos);
typedef int (__stdcall* S_Desc)(void* desc);
typedef int (__stdcall* S_CreateInst)(void* desc, void** inst);
typedef int (__stdcall* S_InstParam)(void* inst, const char* name, float v, int ignoreseek);
typedef int (__stdcall* S_Inst3D)(void* inst, const F3D* attr);
typedef int (__stdcall* S_Inst)(void* inst);
typedef int (__stdcall* S_InstStop)(void* inst, int mode);
typedef int (__stdcall* S_Is3D)(void* desc, int* is3d);
typedef int (__stdcall* S_InstVol)(void* inst, float v);
typedef int (__stdcall* C_Output)(void* core, int type);
typedef int (__stdcall* C_Format)(void* core, int rate, int mode, int raw);
typedef int (__stdcall* C_Buffer)(void* core, unsigned len, int num);
typedef int (__stdcall* C_Master)(void* core, void** group);
typedef int (__stdcall* C_GroupF)(void* group, float v);

static struct {
	S_Create create; S_Core core; S_Init init; S_LoadBank loadBank; S_Sys update, release, flush; S_GetEvent getEvent;
	S_SysParam sysParam; S_Listener listener; S_Desc loadSamples; S_CreateInst createInst; S_InstParam instParam;
	S_Inst3D inst3d; S_Inst start, instRelease; S_InstStop stop; S_Is3D is3d; S_InstVol instVol;
	C_Output output; C_Format format; C_Buffer buffer; C_Master master; C_GroupF pitch, volume;
} st{};

static std::mutex g_mu;            // Lua commands and the camera step can come from different threads
static void* g_sys = nullptr;
static void* g_core = nullptr;
static float g_speed = 1.0f, g_vol = 1.0f;
static F3D g_listener = { {0, 0, 0}, {0, 0, 0}, {0, 0, 1}, {0, 1, 0} };
static std::unordered_map<std::string, void*> g_desc;     // event path -> description (sample data loaded on first use)
static std::unordered_map<std::string, void*> g_held;     // snapshots / loops started by RonHold

static const char* kBanks[] = { "Master.bank", "Master.strings.bank", "Weapons.bank", "Interactions.bank", "UI.bank", "SWAT.bank",
	"Global Amb.bank" };

template <class T> static bool Get(HMODULE h, T& fn, const char* name) {
	fn = (T)GetProcAddress(h, name);
	if (!fn) Log("ron: %s missing", name);
	return fn != nullptr;
}

// CryEngine (right-handed, Z up) -> FMOD (left-handed, Y up): swap y and z, as main.cpp does for its own sounds
static FV ToF(float x, float y, float z) { return { x, z, y }; }

static void ApplyMaster() {
	void* g = nullptr;
	if (g_core && st.master(g_core, &g) == 0 && g) { st.pitch(g, g_speed); st.volume(g, g_vol); }
}

bool RonInit(const std::string& dir, const char* wavOut) {
	std::lock_guard<std::mutex> lk(g_mu);
	if (g_sys) return true;
	HMODULE core = LoadLibraryExA((dir + "fmor.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
	HMODULE studio = core ? LoadLibraryExA((dir + "fmorstudio.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH) : nullptr;
	if (!core || !studio) { Log("ron: can't load %sfmor.dll / fmorstudio.dll (%lu)", dir.c_str(), GetLastError()); return false; }
	bool ok = Get(studio, st.create, "FMOD_Studio_System_Create") & Get(studio, st.core, "FMOD_Studio_System_GetCoreSystem")
		& Get(studio, st.init, "FMOD_Studio_System_Initialize") & Get(studio, st.loadBank, "FMOD_Studio_System_LoadBankFile")
		& Get(studio, st.update, "FMOD_Studio_System_Update") & Get(studio, st.release, "FMOD_Studio_System_Release")
		& Get(studio, st.flush, "FMOD_Studio_System_FlushCommands") & Get(studio, st.getEvent, "FMOD_Studio_System_GetEvent")
		& Get(studio, st.sysParam, "FMOD_Studio_System_SetParameterByName") & Get(studio, st.listener, "FMOD_Studio_System_SetListenerAttributes")
		& Get(studio, st.loadSamples, "FMOD_Studio_EventDescription_LoadSampleData") & Get(studio, st.createInst, "FMOD_Studio_EventDescription_CreateInstance")
		& Get(studio, st.is3d, "FMOD_Studio_EventDescription_Is3D")
		& Get(studio, st.instParam, "FMOD_Studio_EventInstance_SetParameterByName") & Get(studio, st.inst3d, "FMOD_Studio_EventInstance_Set3DAttributes")
		& Get(studio, st.start, "FMOD_Studio_EventInstance_Start") & Get(studio, st.stop, "FMOD_Studio_EventInstance_Stop")
		& Get(studio, st.instRelease, "FMOD_Studio_EventInstance_Release") & Get(studio, st.instVol, "FMOD_Studio_EventInstance_SetVolume")
		& Get(core, st.output, "FMOD_System_SetOutput") & Get(core, st.format, "FMOD_System_SetSoftwareFormat")
		& Get(core, st.buffer, "FMOD_System_SetDSPBufferSize") & Get(core, st.master, "FMOD_System_GetMasterChannelGroup")
		& Get(core, st.pitch, "FMOD_ChannelGroup_SetPitch") & Get(core, st.volume, "FMOD_ChannelGroup_SetVolume");
	if (!ok) return false;
	void* sys = nullptr;
	int r = st.create(&sys, 0x00020310);   // FMOD 2.03.10, the version Ready or Not ships
	if (r || !sys) { Log("ron: create %d", r); return false; }
	st.core(sys, &g_core);
	// SYNCHRONOUS_UPDATE: Studio's command and loading work runs inside our RonUpdate (every camera step) instead of on
	// its own threads; the mixer still has its own thread. With Studio's own threads, loading Master.bank fails with
	// FMOD_ERR_PLUGIN_MISSING (54), in game and in ron_coexist live (2026-10-12); in this mode all 7 banks load.
	unsigned sflags = 0x2;
	// live: 256-sample mix blocks x 4 (~21 ms at 48 kHz) instead of FMOD's 1024 x 4 (~85 ms): the bang comes with the
	// muzzle flash, not two frames after it
	if (!wavOut) st.buffer(g_core, 256, 4);
	if (wavOut) {
		st.output(g_core, 5);                 // WAVWRITER_NRT
		st.format(g_core, 48000, 3, 0);       // 48 kHz stereo, like the FmodRender renders
		st.buffer(g_core, 1024, 4);           // offline: each update mixes one block
	}
	r = st.init(sys, 512, sflags, 0, (void*)wavOut);
	if (r) { Log("ron: init %d", r); st.release(sys); g_core = nullptr; return false; }
	DWORD t0 = GetTickCount();
	for (const char* b : kBanks) {
		void* bank = nullptr;
		r = st.loadBank(sys, (dir + b).c_str(), 0, &bank);
		if (r) { Log("ron: bank %s failed %d", b, r); st.release(sys); g_core = nullptr; return false; }
	}
	st.flush(sys);
	g_sys = sys;
	ApplyMaster();
	Log("ron: Ready or Not's FMOD 2.03.10 up, %zu banks in %lu ms%s", sizeof(kBanks) / sizeof(*kBanks), GetTickCount() - t0,
		wavOut ? " (offline)" : "");
	return true;
}

bool RonReady() { return g_sys != nullptr; }

void RonRelease() {
	std::lock_guard<std::mutex> lk(g_mu);
	if (!g_sys) return;
	st.release(g_sys);    // the wav writer finishes its file here
	g_sys = nullptr; g_core = nullptr; g_desc.clear(); g_held.clear();
}

void RonUpdate(const float* cam) {
	std::lock_guard<std::mutex> lk(g_mu);
	if (!g_sys) return;
	if (cam) {
		g_listener.pos = ToF(cam[3], cam[7], cam[11]);
		g_listener.fwd = ToF(cam[1], cam[5], cam[9]);
		g_listener.up = ToF(cam[2], cam[6], cam[10]);
		int r = st.listener(g_sys, 0, &g_listener, nullptr);
		static int said = 0;
		if (r && said++ < 3) Log("ron: listener rejected (%d): fwd %.3f %.3f %.3f up %.3f %.3f %.3f", r, g_listener.fwd.x, g_listener.fwd.y,
			g_listener.fwd.z, g_listener.up.x, g_listener.up.y, g_listener.up.z);
	}
	st.update(g_sys);
}

static void* Desc(const char* path) {
	auto it = g_desc.find(path);
	if (it != g_desc.end()) return it->second;
	void* d = nullptr;
	int r = st.getEvent(g_sys, path, &d);
	if (r || !d) { Log("ron: no event %s (%d)", path, r); g_desc[path] = nullptr; return nullptr; }
	st.loadSamples(d);    // compressed audio stays in memory from now on: the next shot starts without a load
	g_desc[path] = d;
	return d;
}

bool RonPlay(const char* event, const char* params, const float* at, float vol) {
	std::lock_guard<std::mutex> lk(g_mu);
	if (!g_sys) return false;
	void* d = Desc(event);
	if (!d) return false;
	void* inst = nullptr;
	if (st.createInst(d, &inst) || !inst) return false;
	for (const char* p = params ? params : ""; *p;) {
		while (*p == ' ') p++;
		char name[64] = {}; float v = 0; int used = 0;
		if (sscanf_s(p, "%63[^=]=%f%n", name, (unsigned)sizeof(name), &v, &used) < 2 || used == 0) break;
		if (st.instParam(inst, name, v, 1)) st.sysParam(g_sys, name, v, 1);
		p += used;
	}
	st.instVol(inst, vol);
	int is3d = 0;
	st.is3d(d, &is3d);
	if (is3d) {
		F3D a = g_listener;
		if (at) a.pos = ToF(at[0], at[1], at[2]);
		a.vel = { 0, 0, 0 };
		int r3 = st.inst3d(inst, &a);
		static int said = 0;
		if (r3 && said++ < 3) Log("ron: %s position rejected (%d)", event, r3);
	}
	int r = st.start(inst);
	st.instRelease(inst);   // freed by the engine when it finishes
	return r == 0;
}

bool RonLoad(const char* event) {
	std::lock_guard<std::mutex> lk(g_mu);
	return g_sys && Desc(event) != nullptr;
}

void RonGlobal(const char* name, float value) {
	std::lock_guard<std::mutex> lk(g_mu);
	if (g_sys) st.sysParam(g_sys, name, value, 0);
}

bool RonHold(const char* event, bool on) {
	std::lock_guard<std::mutex> lk(g_mu);
	if (!g_sys) return false;
	auto it = g_held.find(event);
	if (!on) {
		if (it == g_held.end()) return true;
		st.stop(it->second, 0);   // ALLOWFADEOUT: as the sound designer authored it
		st.instRelease(it->second);
		g_held.erase(it);
		return true;
	}
	if (it != g_held.end()) return true;
	void* d = Desc(event);
	void* inst = nullptr;
	if (!d || st.createInst(d, &inst) || !inst) return false;
	if (st.start(inst)) { st.instRelease(inst); return false; }
	g_held[event] = inst;
	return true;
}

void RonSpeed(float speed) { std::lock_guard<std::mutex> lk(g_mu); g_speed = speed; ApplyMaster(); }
void RonVolume(float vol) { std::lock_guard<std::mutex> lk(g_mu); g_vol = vol; ApplyMaster(); }

void RonStep(int n) {
	for (int i = 0; i < n; i++) RonUpdate(nullptr);
}
