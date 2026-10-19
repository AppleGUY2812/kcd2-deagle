#pragma once
// Lean (Q / E with a gun out): Ready or Not's quick lean (docs/ron/ron_grizzly_interactions.md, its PlayerCharacter
// FUN_144b3da80 / FUN_144e6df70 and ANIMBP_Player_FP_Master). The VIEW slides sideways and dips, it doesn't roll; the
// arms and gun cant about the view's forward axis. The camera CSystem keeps (what Lua's System.GetViewCamera*, the
// shots, the culling and the render read) is moved right after ISystem::SetViewCamera copies the game's own view
// camera (freecam.cpp AfterSetView); the game's own camera (CView+0xE8) is never written, so nothing builds up. The
// viewmodel is placed on the moved camera, canted (main.cpp OnCamera).
//   quick lean q (cm): toward Lua's target (+-15, cut short where a wall is: Lua's trace) at a constant rateIn; a
//     smaller target of the same side (a wall) is taken at once (its trace clamps the value itself); back to 0 by an
//     FInterpTo at outSpeed
//   view offset: toward (side q, down dip x |q|) by a VInterpTo at camSpeed
//   cant (deg): toward clamp(q, -cantMax, cantMax) (the cm number as degrees) by an RInterpTo at cantSpeed
// Portable (no Windows): native/test/lean_test.cpp runs it offline. Times are game seconds.

struct LeanParams {
	float rateIn = 400;      // cm/s
	float outSpeed = 30;     // /s (FInterpTo)
	float camSpeed = 8;      // /s (VInterpTo)
	float dip = 0.5f;        // cm down per cm of lean
	float cantSpeed = 8;     // /s (RInterpTo)
	float cantMax = 25;      // deg
};

struct LeanState {
	LeanParams p;
	float target = 0;        // cm, + right (from Lua: "lean <cm>")
	float q = 0;             // the quick lean now (cm)
	float side = 0, down = 0;   // the view's offset now (cm: right, down)
	float cant = 0;          // the arms' cant now (deg, + = the top toward the right)
};
extern LeanState lean;

void LeanReset();                                   // upright, target 0 (the parameters stay)
bool LeanParam(const char* key, float v);           // in / out / cam / dip / cant / cantmax; false = unknown key
void LeanSet(float targetCm);
void LeanStep(float dt);                            // once a frame, game seconds
// the camera (CryEngine Matrix34 rows; columns right, forward, up; translation [3] [7] [11]) moved by the lean; with
// cant, also rolled about its forward axis by the arms' cant (the viewmodel's camera). false (out untouched) upright
bool LeanApply(const float in12[12], float out12[12], bool cant);
