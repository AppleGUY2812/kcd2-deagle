#pragma once
// Free camera ("shoot and fly away"), WHGame 1.5.6. RE notes and design: docs/kcd2_freecam.md.
// The render, culling, occlusion, LOD and streaming use a camera that flies on its own keys. Its rotation is the game
// camera's: the mouse still turns Henry. The game itself keeps Henry's real eye: Lua (System.GetViewCamera*), shots,
// interaction rays, the viewmodel and the audio listener. How: after ISystem::SetViewCamera copies the view camera
// into CSystem, its matrix is swapped for ours. Right after ISystem::Render it's put back.
#include <cstdint>

// Hooks ISystem::SetViewCamera (vtable +0x430) and ISystem::Render (+0x48) of gEnv->pSystem (gEnv+0xC0). Both hooks or
// neither. updateFrustum is CCamera::UpdateFrustumPlanes (main.cpp's g_origFrustum: the trampoline, or the function
// itself). Call it once, after MH_Initialize and after gEnv is found.
bool Freecam_Install(uintptr_t env, void* updateFrustum);

// Lua: "[DN] freecam <args>"
//   on | off | toggle (default) | status
//   speed <m/s> (default 4) | fast <x> (Shift multiplier, default 5) | max <m> (how far from Henry's eye, default 500)
//   keys nav | arrows | key <vk> (toggle key: hex 0xDC or decimal; 0 = none)
void Freecam_Command(const char* args);

// Off now and forget the position (a save load, the "hello"). Safe to call at any time from the main thread.
void Freecam_Reset(const char* why);

bool Freecam_Active();
// The matrix the render uses this frame (Matrix34 rows, translation in [3],[7],[11]); false while off.
bool Freecam_Matrix(float out12[12]);
