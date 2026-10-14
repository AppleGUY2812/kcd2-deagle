#include "ron.h"
#include "log.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

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
typedef int (__stdcall* C_CreateSound)(void* core, const char* name, unsigned mode, void* exinfo, void** snd);
typedef int (__stdcall* C_PlaySound)(void* core, void* snd, void* group, int paused, void** chan);
typedef int (__stdcall* C_Chan3D)(void* chan, const FV* pos, const FV* vel);
typedef int (__stdcall* C_ChanMinMax)(void* chan, float mn, float mx);
typedef int (__stdcall* C_ChanF)(void* chan, float v);
typedef int (__stdcall* C_ChanI)(void* chan, int v);
typedef int (__stdcall* C_Listener)(void* core, int idx, const FV* pos, const FV* vel, const FV* fwd, const FV* up);
typedef int (__stdcall* S_GetBus)(void* sys, const char* path, void** bus);
typedef int (__stdcall* S_BusCG)(void* bus, void** cg);
typedef int (__stdcall* C_CreateGroup)(void* core, const char* name, void** cg);
typedef int (__stdcall* C_CreateDSP)(void* core, int type, void** dsp);
typedef int (__stdcall* C_GroupN)(void* cg, int* n);
typedef int (__stdcall* C_GroupDSP)(void* cg, int i, void** dsp);
typedef int (__stdcall* C_GroupAddDSP)(void* cg, int index, void* dsp);
typedef int (__stdcall* C_DspType)(void* dsp, int* type);
typedef int (__stdcall* C_DspGetI)(void* dsp, int idx, int* v, char* s, int len);
typedef int (__stdcall* C_DspGetF)(void* dsp, int idx, float* v, char* s, int len);
typedef int (__stdcall* C_DspSetI)(void* dsp, int idx, int v);
typedef int (__stdcall* C_DspSetF)(void* dsp, int idx, float v);

static struct {
	S_Create create; S_Core core; S_Init init; S_LoadBank loadBank; S_Sys update, release, flush; S_GetEvent getEvent;
	S_SysParam sysParam; S_Listener listener; S_Desc loadSamples; S_CreateInst createInst; S_InstParam instParam;
	S_Inst3D inst3d; S_Inst start, instRelease; S_InstStop stop; S_Is3D is3d; S_InstVol instVol;
	C_Output output; C_Format format; C_Buffer buffer; C_Master master; C_GroupF pitch, volume;
	C_CreateSound createSound; C_PlaySound playSound; C_Chan3D chan3d; C_ChanMinMax chanMinMax; C_ChanF chanVol;
	C_ChanI chanPaused; C_Listener coreListener;
	S_GetBus getBus; S_Inst busLock; S_BusCG busCG; C_CreateGroup createGroup; C_CreateDSP createDSP; C_GroupN groupNumDSPs;
	C_GroupDSP groupDSP; C_GroupAddDSP groupAddDSP; C_DspType dspType; C_DspGetI dspGetI; C_DspGetF dspGetF; C_DspSetI dspSetI;
	C_DspSetF dspSetF;
} st{};

static std::mutex g_mu;            // Lua commands and the camera step can come from different threads
static void* g_sys = nullptr;
static void* g_core = nullptr;
static float g_speed = 1.0f, g_vol = 1.0f;
static F3D g_listener = { {0, 0, 0}, {0, 0, 0}, {0, 0, 1}, {0, 1, 0} };
static std::unordered_map<std::string, void*> g_desc;     // event path -> description (sample data loaded on first use)
static std::unordered_map<std::string, void*> g_held;     // snapshots / loops started by RonHold
static std::unordered_map<std::string, void*> g_files;    // "file:<path>" -> FMOD sound (our own WAVs, kept loaded)
static std::string g_soundDir;                              // <mod>\sounds\: where "file:" paths start
static float g_ear[3] = {};                                 // the listener, CryEngine axes (the free camera when it's on)
struct Pending { double due; std::string ev, params; float at[3]; float vol; };
static std::vector<Pending> g_pending;                      // RonPlayAt sounds still on their way (speed of sound)
// The room echo for our files. Ready or Not's hits play through bus:/Headphones/SFX/Special Destructables Duck, whose
// send into the room reverb (Reverb_Global, where the gun sends too) the place's snapshot sets: 0.447 indoors, ~0.01
// outdoors (mixer dump, tools/ron/FmodRender DUMP_MASTER, 2026-10-14). Our files play in a group of their own with a send
// into the same reverb whose level follows that bus's every update (rooms, doorway blends), so the steel plate echoes
// like RoN's own hits (pack 22). Set up once the buses have channel groups (a Studio update after the lock).
static const char* kHitsBus = "bus:/Headphones/SFX/Special Destructables Duck";
static const char* kRoomReverb = "bus:/Headphones/Reverbs/2D/Reverb_Global";
static const int kDspSend = 21, kDspReturn = 22;               // FMOD_DSP_TYPE_SEND / RETURN in 2.03 (as its DSPs report)
static void* g_hitsBus = nullptr; static void* g_roomBus = nullptr;
static void* g_hitsSend = nullptr;                          // RoN's send on the hits bus: the level we copy
static void* g_filesGroup = nullptr; static void* g_filesSend = nullptr;
static int g_echoTries = 0;
static double Now() {
	static LARGE_INTEGER f{}; LARGE_INTEGER c;
	if (!f.QuadPart) QueryPerformanceFrequency(&f);
	QueryPerformanceCounter(&c);
	return (double)c.QuadPart / (double)f.QuadPart;
}
static bool PlayLocked(const char* event, const char* params, const float* at, float vol);
static void* FindDSP(void* cg, int type) {
	int n = 0;
	st.groupNumDSPs(cg, &n);
	for (int i = 0; i < n; i++) { void* d = nullptr; int t = -1; if (st.groupDSP(cg, i, &d) == 0 && d && st.dspType(d, &t) == 0 && t == type) return d; }
	return nullptr;
}
static void FilesEcho() {
	void* hits = nullptr; void* room = nullptr;
	if (st.busCG(g_hitsBus, &hits) || !hits || st.busCG(g_roomBus, &room) || !room) return;   // not mixed yet: next update
	void* ret = FindDSP(room, kDspReturn); g_hitsSend = FindDSP(hits, kDspSend);
	int id = -1;
	if (!ret || !g_hitsSend || st.dspGetI(ret, 0, &id, nullptr, 0) || id < 0) {
		Log("ron: room echo for our files: no %s on the %s bus", ret ? "send" : "return", ret ? "hits" : "reverb"); g_hitsSend = nullptr; g_echoTries = 1000; return;
	}
	void* group = nullptr; void* send = nullptr;
	if (st.createGroup(g_core, "deagle files", &group) || !group || st.createDSP(g_core, kDspSend, &send) || !send) { g_echoTries = 1000; return; }
	st.dspSetI(send, 0, id);
	float v = 0; st.dspGetF(g_hitsSend, 1, &v, nullptr, 0); st.dspSetF(send, 1, v);
	st.groupAddDSP(group, 0, send);   // at the head: after the group's fader, like RoN's own sends
	g_filesGroup = group; g_filesSend = send;
	Log("ron: our files send into the room echo (return %d) like Ready or Not's hits (now %.3f)", id, v);
}

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
		& Get(core, st.pitch, "FMOD_ChannelGroup_SetPitch") & Get(core, st.volume, "FMOD_ChannelGroup_SetVolume")
		& Get(core, st.createSound, "FMOD_System_CreateSound") & Get(core, st.playSound, "FMOD_System_PlaySound")
		& Get(core, st.chan3d, "FMOD_Channel_Set3DAttributes") & Get(core, st.chanMinMax, "FMOD_Channel_Set3DMinMaxDistance")
		& Get(core, st.chanVol, "FMOD_Channel_SetVolume") & Get(core, st.chanPaused, "FMOD_Channel_SetPaused")
		& Get(core, st.coreListener, "FMOD_System_Set3DListenerAttributes")
		& Get(studio, st.getBus, "FMOD_Studio_System_GetBus") & Get(studio, st.busLock, "FMOD_Studio_Bus_LockChannelGroup")
		& Get(studio, st.busCG, "FMOD_Studio_Bus_GetChannelGroup") & Get(core, st.createGroup, "FMOD_System_CreateChannelGroup")
		& Get(core, st.createDSP, "FMOD_System_CreateDSPByType") & Get(core, st.groupNumDSPs, "FMOD_ChannelGroup_GetNumDSPs")
		& Get(core, st.groupDSP, "FMOD_ChannelGroup_GetDSP") & Get(core, st.groupAddDSP, "FMOD_ChannelGroup_AddDSP")
		& Get(core, st.dspType, "FMOD_DSP_GetType") & Get(core, st.dspGetI, "FMOD_DSP_GetParameterInt")
		& Get(core, st.dspGetF, "FMOD_DSP_GetParameterFloat") & Get(core, st.dspSetI, "FMOD_DSP_SetParameterInt")
		& Get(core, st.dspSetF, "FMOD_DSP_SetParameterFloat");
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
	if (st.getBus(sys, kHitsBus, &g_hitsBus) == 0 && st.getBus(sys, kRoomReverb, &g_roomBus) == 0) { st.busLock(g_hitsBus); st.busLock(g_roomBus); }
	else { g_hitsBus = g_roomBus = nullptr; Log("ron: no %s / %s: our files get no room echo", kHitsBus, kRoomReverb); }
	g_sys = sys;
	g_soundDir = dir + "..\\sounds\\";
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
	g_sys = nullptr; g_core = nullptr; g_desc.clear(); g_held.clear(); g_files.clear();   // sounds go with the system
	g_hitsBus = g_roomBus = g_hitsSend = g_filesGroup = g_filesSend = nullptr; g_echoTries = 0;
}

void RonUpdate(const float* cam) {
	std::lock_guard<std::mutex> lk(g_mu);
	if (!g_sys) return;
	if (cam) {
		g_ear[0] = cam[3]; g_ear[1] = cam[7]; g_ear[2] = cam[11];
		g_listener.pos = ToF(cam[3], cam[7], cam[11]);
		g_listener.fwd = ToF(cam[1], cam[5], cam[9]);
		g_listener.up = ToF(cam[2], cam[6], cam[10]);
		int r = st.listener(g_sys, 0, &g_listener, nullptr);
		FV still = { 0, 0, 0 };
		st.coreListener(g_core, 0, &g_listener.pos, &still, &g_listener.fwd, &g_listener.up);
		static int said = 0;
		if (r && said++ < 3) Log("ron: listener rejected (%d): fwd %.3f %.3f %.3f up %.3f %.3f %.3f", r, g_listener.fwd.x, g_listener.fwd.y,
			g_listener.fwd.z, g_listener.up.x, g_listener.up.y, g_listener.up.z);
	}
	if (!g_pending.empty()) {   // sounds whose travel time is up
		double t = Now();
		std::vector<Pending> due;
		for (size_t i = 0; i < g_pending.size();) {
			if (g_pending[i].due <= t) { due.push_back(std::move(g_pending[i])); g_pending.erase(g_pending.begin() + i); }
			else i++;
		}
		for (auto& p : due) PlayLocked(p.ev.c_str(), p.params.c_str(), p.at, p.vol);
	}
	st.update(g_sys);
	if (!g_filesGroup && g_hitsBus && g_echoTries < 50) { g_echoTries++; FilesEcho(); }
	if (g_filesSend && g_hitsSend) { float v = 0; if (st.dspGetF(g_hitsSend, 1, &v, nullptr, 0) == 0) st.dspSetF(g_filesSend, 1, v); }
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

// "file:<path>": one of our own sound files (Mods\deagle\sounds\<path>) instead of a Ready or Not event, e.g. a real
// plate hit lifted from a recording. Loaded once and kept in memory, played on a core channel into RoN's master bus
// (its limiter, the mod's volume, slow motion) through our files group (the room echo), 3D-panned from the listener; the distance law is the event's
// (Falloff=), FMOD's own roll-off is off (min distance 10 km).
static bool IsFile(const char* event) { return !strncmp(event, "file:", 5); }
static void* FileSound(const char* event) {
	auto it = g_files.find(event);
	if (it != g_files.end()) return it->second;
	void* snd = nullptr;
	std::string path = g_soundDir + (event + 5);
	int r = st.createSound(g_core, path.c_str(), 0x00000001 | 0x00000010 | 0x00000100, nullptr, &snd);   // LOOP_OFF|3D|CREATESAMPLE
	if (r || !snd) { Log("ron: can't load %s (%d)", path.c_str(), r); snd = nullptr; }
	g_files[event] = snd;
	return snd;
}
static bool PlayFile(const char* event, const float* at, float vol) {
	void* snd = FileSound(event);
	void* ch = nullptr;
	if (!snd || st.playSound(g_core, snd, g_filesGroup, 1, &ch) || !ch) return false;   // paused: placed before it sounds
	FV pos = at ? ToF(at[0], at[1], at[2]) : g_listener.pos, still = { 0, 0, 0 };
	st.chan3d(ch, &pos, &still);
	st.chanMinMax(ch, 10000.0f, 20000.0f);
	st.chanVol(ch, vol);
	return st.chanPaused(ch, 0) == 0;
}

bool RonPlay(const char* event, const char* params, const float* at, float vol) {
	std::lock_guard<std::mutex> lk(g_mu);
	return g_sys && PlayLocked(event, params, at, vol);
}

static float EarDistance(const float* at) {
	float dx = at[0] - g_ear[0], dy = at[1] - g_ear[1], dz = at[2] - g_ear[2];
	return sqrtf(dx * dx + dy * dy + dz * dz);
}

bool RonPlayAt(const char* event, const char* params, const float* at, float vol) {
	std::lock_guard<std::mutex> lk(g_mu);
	if (!g_sys || !at) return false;
	float d = EarDistance(at);
	double delay = d / 343.0 / (g_speed > 0.01f ? g_speed : 0.01f);   // slow motion stretches it like the sound itself
	if (delay < 0.01) return PlayLocked(event, params, at, vol);
	Pending p{ Now() + delay, event, params ? params : "", { at[0], at[1], at[2] }, vol };
	g_pending.push_back(std::move(p));
	return IsFile(event) ? FileSound(event) != nullptr : Desc(event) != nullptr;   // loaded now, played on time
}

static bool PlayLocked(const char* event, const char* params, const float* at, float vol) {
	// "PropagateDistance=@": RoN's game code gives its impact events the length of the sound's path; ours is the straight
	// line from the listener, worked out now (the free camera may have moved while the sound travelled)
	std::string ps = params ? params : "";
	// "Falloff=<m>": our distance law instead of RoN's (its PropagateDistance fades impacts out by ~12 m, a
	// close-quarters game's choice): full level within <m> of the listener, then -6 dB per doubling (inverse distance),
	// and RoN's own fade off (PropagateDistance 0). An older plugin ignores the marker and keeps RoN's fade.
	size_t f = ps.find("Falloff=");
	if (f != std::string::npos) {
		float ref = (float)atof(ps.c_str() + f + 8);
		size_t e = ps.find(' ', f);
		ps.erase(f, e == std::string::npos ? std::string::npos : e - f + 1);
		if (at && ref > 0) { float d = EarDistance(at); vol *= ref / (d > ref ? d : ref); }
		size_t pd = ps.find("PropagateDistance=@");
		if (pd != std::string::npos) ps.replace(pd + 18, 1, "0");
	}
	size_t k = ps.find("PropagateDistance=@");
	if (k != std::string::npos) {
		char num[32];
		snprintf(num, sizeof(num), "%.1f", at ? (EarDistance(at) < 250.0f ? EarDistance(at) : 250.0f) : 0.0f);
		ps.replace(k + 18, 1, num);
	}
	params = ps.c_str();
	if (IsFile(event)) return PlayFile(event, at, vol);
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
	return g_sys && (IsFile(event) ? FileSound(event) : Desc(event)) != nullptr;
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
