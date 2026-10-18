// Free camera module (WHGame 1.5.6). Design and RE notes: docs/kcd2_freecam.md.
//
// One frame, in the order the engine runs it (CCryAction::PostUpdate 0x669C88, after Lua and input in ISystem::Update):
//   CViewSystem::Update 0x7F0220
//     CView::Update 0x7F0EC0           -> m_camera (CView+0xE8) matrix, UpdateFrustumPlanes   (main.cpp OnCamera: real eye)
//     CView::UpdateAudioListener        -> the game's listener entity = real eye
//     camera shake, UpdateFrustumPlanes again
//     ISystem::SetViewCamera(m_camera)  -> copied into CSystem+0x288                            << HkSetView: ours goes in
//   I3DEngine::PrepareOcclusion(GetViewCamera())                                                 (sees ours)
//   ISystem::Render 0x43E464 -> RenderWorld(CreateGeneralPassRenderingInfo(CSystem+0x288))        (sees ours)
//                                                                                                << HkRender: real put back
// Lua's System.GetViewCameraPos/Dir and the game's own gameplay reads of the view camera run in the next frame's
// ISystem::Update, so they get the real camera. All of it runs on the main thread.
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "MinHook.h"
#include "log.h"
#include "freecam.h"

namespace {

using Pass4Fn = uintptr_t(__fastcall*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
using FrustumFn = uintptr_t(__fastcall*)(uintptr_t camera);

constexpr int kSysOffInEnv = 0xC0;        // gEnv->pSystem
constexpr int kVtRender = 0x48;           // ISystem::Render                 (CSystem 0x43E464)
constexpr int kVtSetViewCamera = 0x430;   // ISystem::SetViewCamera          (0x7F0AE0: add rcx,0x288; jmp CCamera::operator=)
constexpr int kVtGetViewCamera = 0x438;   // ISystem::GetViewCamera          (0x5D77F0: lea rax,[rcx+0x288]; ret)

uintptr_t g_sys = 0;
uintptr_t g_camOff = 0;                   // CSystem::m_ViewCamera (0x288), read from GetViewCamera's own code
Pass4Fn g_origSetView = nullptr, g_origRender = nullptr;
FrustumFn g_frustum = nullptr;
bool g_installed = false;

std::atomic<bool> g_want{ false };        // asked for (command / key); the camera step acts on it
bool g_on = false;                        // flying: g_pos is valid
bool g_applied = false;                   // CSystem's camera holds our matrix now (between SetViewCamera and Render)
float g_pos[3] = {};
float g_real[12] = {}, g_free[12] = {};
float g_lastEye[3] = {}; bool g_haveLastEye = false;

// the camera's own aim (Lua's freecam look: the mouse turns the camera, Henry's aim stays): pitch / yaw in radians
// as Lua reads them from a view direction (yaw = atan2(-x, y), pitch = asin(z)); off = the game camera's rotation
bool g_rotOn = false; float g_rotPitch = 0, g_rotYaw = 0;
float g_speed = 4.0f;                     // m/s, wall-clock (the same in F9 slow motion)
float g_fast = 5.0f;                      // x speed while Shift is held
float g_maxDist = 500.0f;                 // from Henry's eye
const float kJump = 25.0f;                // the real eye moved this far in one frame: load / fast travel / cutscene -> off

// fwd, back, left, right, up, down. WASD by default: main.cpp turns KCD2's "no_move" filter on while the camera flies,
// so they move the camera and not Henry (Space = jump and Shift = sprint are in that filter too; Left Ctrl is only the
// horse brake). The user's keyboard has no numpad and maybe no Home/End (the first default didn't move, 2026-10-12).
const int kWasdKeys[6] = { 'W', 'S', 'A', 'D', VK_SPACE, VK_LCONTROL };
const int kNavKeys[6] = { VK_HOME, VK_END, VK_DELETE, VK_NEXT, VK_PRIOR, VK_INSERT };   // Ins Home PgUp / Del End PgDn = Q W E / A S D
const int kArrowKeys[6] = { VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_PRIOR, VK_NEXT };      // the arrows walk Henry too unless unbound
int g_keys[6] = { 'W', 'S', 'A', 'D', VK_SPACE, VK_LCONTROL };
int g_toggleVk = VK_OEM_5;                // '\' on US / UK-ISO keyboards; free in defaultProfile.xml and keybindSuperactions.xml
bool g_toggleHeld = true;                 // a press counts only after a release was seen with the game focused
LARGE_INTEGER g_last{}, g_freq{};

// ---- guarded memory (no C++ objects in these: MSVC forbids __try next to unwinding)
bool ReadBytes(uintptr_t a, void* out, size_t n) {
	__try { memcpy(out, (const void*)a, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool ReadU64(uintptr_t a, uintptr_t& out) { return ReadBytes(a, &out, sizeof(out)); }
// write the 12 matrix floats and rebuild the frustum planes from them (what CCamera::SetMatrix does)
bool SafeSetMatrix(uintptr_t cam, const float* m12) {
	__try { memcpy((void*)cam, m12, 12 * sizeof(float)); g_frustum(cam); return true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool Finite(const float* v, int n) { for (int i = 0; i < n; i++) if (!std::isfinite(v[i])) return false; return true; }

bool Focused() {
	HWND w = GetForegroundWindow();
	if (!w) return false;
	DWORD pid = 0; GetWindowThreadProcessId(w, &pid);
	return pid == GetCurrentProcessId();
}
bool Down(int vk) { return vk > 0 && (GetAsyncKeyState(vk) & 0x8000) != 0; }

float Tick() {
	LARGE_INTEGER now; QueryPerformanceCounter(&now);
	if (!g_freq.QuadPart) QueryPerformanceFrequency(&g_freq);
	float dt = g_last.QuadPart ? (float)((now.QuadPart - g_last.QuadPart) / (double)g_freq.QuadPart) : 0.0f;
	g_last = now;
	return dt < 0 ? 0 : dt > 0.1f ? 0.1f : dt;   // a hitch or a pause must not fling the camera
}

// the view offset (Freecam_ViewOffset): this frame's, and whether CSystem's camera holds it now (with the real fov)
float g_voRot[3] = {}, g_voLoc[3] = {}, g_voFov = 0;
bool g_voApplied = false; float g_voReal[12] = {}, g_voShaken[12] = {}, g_voRealFov = 0;
constexpr int kCamFov = 0x30;             // CCamera::m_fov (vertical, radians), as main.cpp's CopyCamera reads it

bool SafeSetFov(uintptr_t cam, float fov) {
	__try { *(float*)(cam + kCamFov) = fov; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool SafeGetFov(uintptr_t cam, float& fov) {
	__try { fov = *(const float*)(cam + kCamFov); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void Restore() {
	if (g_voApplied) {
		g_voApplied = false;
		uintptr_t cam = g_sys + g_camOff;
		if (!SafeSetFov(cam, g_voRealFov) || !SafeSetMatrix(cam, g_voReal)) Log("view offset: restoring the view camera faulted");
	}
	if (!g_applied) return;
	g_applied = false;
	if (!SafeSetMatrix(g_sys + g_camOff, g_real)) Log("freecam: restoring the view camera faulted");
}

// the game window's width / height, for turning a horizontal field of view into KCD2's vertical one
float Aspect() {
	HWND w = GetActiveWindow();
	RECT r{};
	if (!w || !GetClientRect(w, &r) || r.bottom <= r.top || r.right <= r.left) return 0;
	return float(r.right - r.left) / float(r.bottom - r.top);
}

// After SetViewCamera with the free camera off: this frame's view offset into the render camera
void ApplyViewOffset(const float* m) {
	if (g_voRot[0] == 0 && g_voRot[1] == 0 && g_voRot[2] == 0 && g_voLoc[0] == 0 && g_voLoc[1] == 0 && g_voLoc[2] == 0 && g_voFov == 0) return;
	uintptr_t cam = g_sys + g_camOff;
	float fov = 0;
	if (!SafeGetFov(cam, fov)) return;
	// camera-local: pitch about right (+ = up), yaw about up (+ = right, so -z), roll about forward
	const float k = 3.14159265f / 180.0f;
	float p = g_voRot[0] * k, y = -g_voRot[1] * k, r = g_voRot[2] * k;
	float Rz[9] = { cosf(y), -sinf(y), 0, sinf(y), cosf(y), 0, 0, 0, 1 };
	float Rx[9] = { 1, 0, 0, 0, cosf(p), -sinf(p), 0, sinf(p), cosf(p) };
	float Ry[9] = { cosf(r), 0, sinf(r), 0, 1, 0, -sinf(r), 0, cosf(r) };
	auto mul = [](const float* a, const float* b, float* o) {
		for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) o[i * 3 + j] = a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] + a[i * 3 + 2] * b[6 + j];
	};
	float t[9], L[9], C[9] = { m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10] }, R[9];
	mul(Rz, Rx, t); mul(t, Ry, L); mul(C, L, R);
	float out[12];
	for (int i = 0; i < 3; i++) {
		out[i * 4 + 0] = R[i * 3 + 0]; out[i * 4 + 1] = R[i * 3 + 1]; out[i * 4 + 2] = R[i * 3 + 2];
		out[i * 4 + 3] = m[i * 4 + 3] + C[i * 3 + 0] * g_voLoc[0] + C[i * 3 + 1] * g_voLoc[1] + C[i * 3 + 2] * g_voLoc[2];
	}
	if (!Finite(out, 12)) return;
	float newFov = fov;
	if (g_voFov != 0) {
		float a = Aspect();
		if (a > 0) {
			float h = 2 * atanf(tanf(fov / 2) * a) + g_voFov * k;
			newFov = 2 * atanf(tanf(h / 2) / a);
		} else { static bool said = false; if (!said) { said = true; Log("view offset: no game window size: no fov part"); } }
	}
	memcpy(g_voReal, m, sizeof(g_voReal)); g_voRealFov = fov;
	if (SafeSetFov(cam, newFov) && SafeSetMatrix(cam, out)) { g_voApplied = true; memcpy(g_voShaken, out, sizeof(out)); }
	else { SafeSetFov(cam, fov); SafeSetMatrix(cam, m); }
}

void TurnOff(const char* why) {
	g_want = false;
	g_rotOn = false;
	if (g_on) Log("freecam off (%s)", why);
	g_on = false;
	g_haveLastEye = false;
}

// After CSystem copied the frame's view camera: fly, then hand the render our matrix.
void AfterSetView() {
	uintptr_t cam = g_sys + g_camOff;
	float m[12];
	if (!ReadBytes(cam, m, sizeof(m))) return;
	// A second SetViewCamera in the same frame (before Render) may have been built from GetViewCamera, i.e. from ours:
	// keep the real eye then.
	if (g_applied && m[3] == g_free[3] && m[7] == g_free[7] && m[11] == g_free[11]) {
		m[3] = g_real[3]; m[7] = g_real[7]; m[11] = g_real[11];
	}
	g_applied = false;
	if (g_voApplied) {   // the same for the view offset: a copy of the offset view is the real one again
		g_voApplied = false;
		if (!memcmp(m, g_voShaken, sizeof(g_voShaken))) { memcpy(m, g_voReal, sizeof(g_voReal)); SafeSetFov(cam, g_voRealFov); }
	}
	memcpy(g_real, m, sizeof(m));
	float dt = Tick();

	bool focus = Focused();
	if (!focus) g_toggleHeld = true;
	else {
		bool d = Down(g_toggleVk);
		if (d && !g_toggleHeld) { g_want = !g_want; }
		g_toggleHeld = d;
	}

	const float eye[3] = { m[3], m[7], m[11] };
	if (!Finite(m, 12)) { TurnOff("the view camera is not finite"); return; }
	if (g_on && g_haveLastEye) {
		float dx = eye[0] - g_lastEye[0], dy = eye[1] - g_lastEye[1], dz = eye[2] - g_lastEye[2];
		if (dx * dx + dy * dy + dz * dz > kJump * kJump) { TurnOff("the view jumped: load / travel / cutscene"); }
	}
	memcpy(g_lastEye, eye, sizeof(eye)); g_haveLastEye = true;

	if (g_want && !g_on) {
		g_on = true;
		memcpy(g_pos, eye, sizeof(eye));
		Log("freecam on at %.2f %.2f %.2f", eye[0], eye[1], eye[2]);
	}
	else if (!g_want && g_on) TurnOff("asked");
	if (!g_on) { ApplyViewOffset(m); return; }

	if (focus) {
		// right = column 0, forward = column 1 of the camera matrix (CryEngine: Y forward, Z up); up/down = world Z
		const float R[3] = { g_rotOn ? cosf(g_rotYaw) : m[0], g_rotOn ? sinf(g_rotYaw) : m[4], g_rotOn ? 0.f : m[8] };
		const float F[3] = { g_rotOn ? -sinf(g_rotYaw) * cosf(g_rotPitch) : m[1], g_rotOn ? cosf(g_rotYaw) * cosf(g_rotPitch) : m[5],
			g_rotOn ? sinf(g_rotPitch) : m[9] };
		float f = (Down(g_keys[0]) ? 1.f : 0.f) - (Down(g_keys[1]) ? 1.f : 0.f);
		float r = (Down(g_keys[3]) ? 1.f : 0.f) - (Down(g_keys[2]) ? 1.f : 0.f);
		float u = (Down(g_keys[4]) ? 1.f : 0.f) - (Down(g_keys[5]) ? 1.f : 0.f);
		float v = g_speed * (Down(VK_SHIFT) ? g_fast : 1.f) * dt;
		for (int k = 0; k < 3; k++) g_pos[k] += (F[k] * f + R[k] * r) * v;
		g_pos[2] += u * v;
	}
	// stay within reach of Henry (streaming and AI around him are still his)
	float d[3] = { g_pos[0] - eye[0], g_pos[1] - eye[1], g_pos[2] - eye[2] };
	float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	if (len > g_maxDist && len > 0) for (int k = 0; k < 3; k++) g_pos[k] = eye[k] + d[k] * (g_maxDist / len);

	memcpy(g_free, m, sizeof(m));
	if (g_rotOn) {   // columns: right, forward, up (CryEngine: Y forward, Z up)
		float cp = cosf(g_rotPitch), sp = sinf(g_rotPitch), cy = cosf(g_rotYaw), sy = sinf(g_rotYaw);
		const float F[3] = { -sy * cp, cy * cp, sp }, R[3] = { cy, sy, 0 };
		const float U[3] = { R[1] * F[2] - R[2] * F[1], R[2] * F[0] - R[0] * F[2], R[0] * F[1] - R[1] * F[0] };
		for (int r = 0; r < 3; r++) { g_free[r * 4 + 0] = R[r]; g_free[r * 4 + 1] = F[r]; g_free[r * 4 + 2] = U[r]; }
	}
	g_free[3] = g_pos[0]; g_free[7] = g_pos[1]; g_free[11] = g_pos[2];
	if (!Finite(g_free, 12)) { TurnOff("non-finite position"); return; }
	if (SafeSetMatrix(cam, g_free)) g_applied = true;
	else { TurnOff("writing the view camera faulted"); SafeSetMatrix(cam, g_real); }
}

uintptr_t __fastcall HkSetView(uintptr_t sys, uintptr_t camera, uintptr_t a3, uintptr_t a4) {
	uintptr_t r = g_origSetView(sys, camera, a3, a4);
	if (sys == g_sys) AfterSetView();
	return r;
}

uintptr_t __fastcall HkRender(uintptr_t sys, uintptr_t a2, uintptr_t a3, uintptr_t a4) {
	uintptr_t r = g_origRender(sys, a2, a3, a4);
	if (sys == g_sys) Restore();
	return r;
}

int ParseVk(const char* s) {
	while (*s == ' ') s++;
	return (int)strtol(s, nullptr, (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) ? 16 : 10);
}

} // namespace

void Freecam_ViewOffset(const float rotDeg[3], const float loc[3], float fovDeg) {
	for (int i = 0; i < 3; i++) { g_voRot[i] = rotDeg[i]; g_voLoc[i] = loc[i]; }
	g_voFov = fovDeg;
}

bool Freecam_Install(uintptr_t env, void* updateFrustum) {
	if (g_installed) return true;
	if (!env || !updateFrustum) { Log("freecam: no gEnv / UpdateFrustumPlanes: not installed"); return false; }
	uintptr_t sys = 0, vt = 0, render = 0, setView = 0, getView = 0;
	if (!ReadU64(env + kSysOffInEnv, sys) || !sys || !ReadU64(sys, vt) || !vt ||
		!ReadU64(vt + kVtRender, render) || !ReadU64(vt + kVtSetViewCamera, setView) || !ReadU64(vt + kVtGetViewCamera, getView)) {
		Log("freecam: can't read ISystem's vtable"); return false;
	}
	// The three must agree on where CSystem keeps the view camera (0x288 in 1.5.6):
	//   GetViewCamera  48 8D 81 <off32> C3     lea rax,[rcx+off]; ret
	//   SetViewCamera  48 81 C1 <off32> E9     add rcx,off; jmp CCamera::operator=
	//   Render         4C 8D B6 <off32>        lea r14,[rsi+off] near its start (the camera handed to RenderWorld)
	uint8_t g[8] = {}, s[12] = {}, rb[0x100] = {};
	if (!ReadBytes(getView, g, sizeof(g)) || !ReadBytes(setView, s, sizeof(s)) || !ReadBytes(render, rb, sizeof(rb))) {
		Log("freecam: can't read the ISystem functions"); return false;
	}
	uint32_t offG = 0, offS = 0;
	memcpy(&offG, g + 3, 4); memcpy(&offS, s + 3, 4);
	bool okG = g[0] == 0x48 && g[1] == 0x8D && g[2] == 0x81 && g[7] == 0xC3;
	bool okS = s[0] == 0x48 && s[1] == 0x81 && s[2] == 0xC1 && s[7] == 0xE9;
	bool okR = false;
	for (int i = 0; i + 7 <= (int)sizeof(rb); i++) {
		uint32_t o; memcpy(&o, rb + i + 3, 4);
		if (rb[i] == 0x4C && rb[i + 1] == 0x8D && rb[i + 2] == 0xB6 && o == offG) { okR = true; break; }
	}
	if (!okG || !okS || offG != offS || offG < 0x40 || offG > 0x4000 || !okR) {
		Log("freecam: ISystem layout differs (Get %d Set %d Render %d off %X/%X): not installed (game updated?)",
			(int)okG, (int)okS, (int)okR, offG, offS);
		return false;
	}
	g_sys = sys; g_camOff = offG; g_frustum = (FrustumFn)updateFrustum;
	if (MH_CreateHook((void*)setView, (void*)&HkSetView, (void**)&g_origSetView) != MH_OK) {
		Log("freecam: SetViewCamera hook failed"); return false;
	}
	if (MH_CreateHook((void*)render, (void*)&HkRender, (void**)&g_origRender) != MH_OK) {
		MH_RemoveHook((void*)setView);
		Log("freecam: Render hook failed"); return false;
	}
	// Render first: a SetViewCamera without its restore must never run
	if (MH_EnableHook((void*)render) != MH_OK || MH_EnableHook((void*)setView) != MH_OK) {
		MH_DisableHook((void*)setView); MH_DisableHook((void*)render);
		MH_RemoveHook((void*)setView); MH_RemoveHook((void*)render);
		Log("freecam: enabling the hooks failed"); return false;
	}
	g_installed = true;
	Log("freecam installed: CSystem %p camera +0x%X SetViewCamera %p Render %p", (void*)sys, (unsigned)offG,
		(void*)setView, (void*)render);
	return true;
}

void Freecam_Command(const char* args) {
	char w[16] = {};
	const char* rest = args ? args : "";
	while (*rest == ' ') rest++;
	int n = 0;
	while (rest[n] && rest[n] != ' ' && n < 15) { w[n] = rest[n]; n++; }
	rest += n;
	while (*rest == ' ') rest++;
	if (!g_installed && strcmp(w, "status") != 0) { Log("freecam: not installed (see the log above)"); return; }
	if (!w[0] || !strcmp(w, "toggle")) g_want = !g_want;
	else if (!strcmp(w, "on") || !strcmp(w, "1")) g_want = true;
	else if (!strcmp(w, "off") || !strcmp(w, "0")) g_want = false;
	else if (!strcmp(w, "speed")) { float v = (float)atof(rest); if (v > 0 && v < 1000) g_speed = v; }
	else if (!strcmp(w, "fast")) { float v = (float)atof(rest); if (v >= 1 && v < 100) g_fast = v; }
	else if (!strcmp(w, "rot")) {   // rot <pitch> <yaw> (radians): the camera aims by itself (Lua's look mode)
		float p = 0, y = 0;
		if (sscanf_s(rest, "%f %f", &p, &y) == 2 && std::isfinite(p) && std::isfinite(y)) { g_rotPitch = p; g_rotYaw = y; g_rotOn = true; }
		return;   // every frame: no log
	}
	else if (!strcmp(w, "rotoff")) { g_rotOn = false; return; }
	else if (!strcmp(w, "nudge")) {   // nudge <x> <y> <z>: move the camera by metres (world), for tests without keys
		float d[3] = {}; if (sscanf_s(rest, "%f %f %f", &d[0], &d[1], &d[2]) == 3 && g_on) for (int k = 0; k < 3; k++) g_pos[k] += d[k];
		return;   // every kill-cam frame: no log
	}
	else if (!strcmp(w, "max")) { float v = (float)atof(rest); if (v >= 1 && v < 100000) g_maxDist = v; }
	else if (!strcmp(w, "keys")) {
		if (!strncmp(rest, "arrows", 6)) memcpy(g_keys, kArrowKeys, sizeof(g_keys));
		else if (!strncmp(rest, "nav", 3)) memcpy(g_keys, kNavKeys, sizeof(g_keys));
		else memcpy(g_keys, kWasdKeys, sizeof(g_keys));
	}
	else if (!strcmp(w, "key")) { int vk = ParseVk(rest); if (vk >= 0 && vk < 256) { g_toggleVk = vk; g_toggleHeld = true; } }
	else if (strcmp(w, "status") != 0) { Log("freecam: unknown '%s'", w); return; }
	Log("freecam: installed %d want %d on %d pos %.2f %.2f %.2f speed %.1f fast x%.1f max %.0f keys %s toggle vk 0x%02X",
		(int)g_installed, (int)g_want.load(), (int)g_on, g_pos[0], g_pos[1], g_pos[2], g_speed, g_fast, g_maxDist,
		g_keys[0] == VK_UP ? "arrows" : g_keys[0] == 'W' ? "wasd" : "nav", g_toggleVk);
}

void Freecam_Reset(const char* why) {
	g_rotOn = false;
	Restore();                 // only does something if called between SetViewCamera and Render
	TurnOff(why ? why : "reset");
	g_toggleHeld = true;
	g_last.QuadPart = 0;
}

bool Freecam_Active() { return g_on; }

bool Freecam_Matrix(float out12[12]) {
	if (!g_on) return false;
	memcpy(out12, g_free, sizeof(g_free));
	return true;
}
