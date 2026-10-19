#pragma once
// Lean (Q / E with a gun out, Ready or Not's): the view itself leans, not only the picture. The camera CSystem keeps
// (what Lua's System.GetViewCamera*, the shots, the culling and the render read) is turned and moved in camera space
// right after ISystem::SetViewCamera copies the game's own view camera (freecam.cpp AfterSetView); the game's own
// camera (CView+0xE8) is never written, so nothing builds up from frame to frame. The viewmodel is placed on the
// same leaned camera (main.cpp OnCamera). Lua sends the target (already cut short where a wall is in the way).
// Portable (no Windows): native/test/lean_test.cpp runs it offline.

struct LeanParams {
	float side = 0;       // m to the right at full lean (camera right axis)
	float down = 0;       // m down at full lean either way (camera up axis)
	float roll = 0;       // deg, the top of the view toward the lean side at full lean
	float speedIn = 0;    // FInterpTo speed (/s) toward a lean
	float speedOut = 0;   // toward upright
};

struct LeanState {
	LeanParams p;
	float target = 0;     // -1 (left) .. 1 (right), from Lua ("lean <t>")
	float now = 0;        // where it is
};
extern LeanState lean;

void LeanReset();                                   // upright, target 0 (the parameters stay)
bool LeanParam(const char* key, float v);           // side / down / roll / in / out; false = unknown key
void LeanSet(float target);                         // clamped to -1 .. 1
void LeanStep(float dt);                            // once a frame, game seconds
// the camera (CryEngine Matrix34 rows; columns right, forward, up; translation [3] [7] [11]) leaned by `now`; false
// (out untouched) while upright
bool LeanApply(const float in12[12], float out12[12]);
