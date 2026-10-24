#pragma once
#include <cstdint>
#include <cstring>
// "Let me in early" for a readiness gate (readiness.cpp): the decision only, no game access, so it is tested outside
// the game (native/test/readiness_early_test.cpp). The user's wish (2026-10-23): "just put me in the game and the extra
// stuff u can load later".
//
// A gate holds the screen until every streaming task in its mask is ready. Some tasks only add detail that can arrive
// after the player has control: texture mips, merged meshes (grass), far LODs, voxel GI. The rest (terrain, octree,
// animations, entities, objects, and every task this file doesn't name) must be ready, as in the game.
// The gate is let in early once the must-have tasks have stayed ready for `hold` seconds and the gate is at least
// `minAge` old (requests from a camera jump take a frame or two to queue; terrain reads come in waves), while a
// detail task is still busy. Only for the save-load / level-change gate (0x3f93) and the fast-travel arrival (0x3f91).

namespace early {

inline bool Detail(const char* task) {   // may finish after the gate opens
	static const char* const kDetail[] = { "TextureStreaming", "MergedMeshStreaming", "HLodStreaming",
	                                       "SvotiStreaming", "SvotiPropagation", "SvotiUpload" };
	for (const char* d : kDetail) if (task && !strcmp(task, d)) return true;
	return false;
}

// A must-have task that is down to its last few requests and stuck there: AnimationStreaming only. The user's
// 2026-10-24 09:11 load: everything else was ready at 5.4 s, then one animation request sat "1 in flight" from 6.1 s
// to 10.1 s and held the screen alone (let in early at 10.2 s). Such a task counts as ready once it has had at most
// kStragglerMax requests in flight for kStragglerHold s in a row; the last animation arrives after the screen opens.
constexpr int kStragglerMax = 2;
constexpr double kStragglerHold = 1.0;
inline bool StragglerKind(const char* task) { return task && !strcmp(task, "AnimationStreaming"); }

// Once a frame per busy must-have task. since: gate age when it last went down to kStragglerMax or fewer (-1: more
// now). inFlight < 0 = unknown (never a straggler). True = treat it as ready this frame.
inline bool Straggler(const char* task, int inFlight, double age, double& since) {
	if (!StragglerKind(task) || inFlight < 0 || inFlight > kStragglerMax) { since = -1; return false; }
	if (since < 0) since = age;
	return age - since >= kStragglerHold;
}

inline bool Eligible(uint32_t mask) { return mask == 0x3f93 || mask == 0x3f91; }

struct Cfg {
	double minAge = 1.0;   // s since the gate started
	double hold = 0.3;     // s the must-have tasks stay ready in a row
};

struct State {
	double mustSince = -1;   // gate age when the must-have tasks last became ready (-1: not ready now)
	double wouldOpen = -1;   // gate age of the first moment it may be let in early (-1: never so far)
};

// Once a frame while the gate waits. age: s since the gate started; mustReady: every must-have task ready;
// detailBusy: a detail task still busy. True = it may be let in now (the first true also sets wouldOpen).
inline bool Step(State& s, const Cfg& c, double age, bool mustReady, bool detailBusy) {
	if (!mustReady) { s.mustSince = -1; return false; }
	if (s.mustSince < 0) s.mustSince = age;
	if (!detailBusy) return false;   // nothing to defer: the game opens the gate itself
	if (age < c.minAge || age - s.mustSince < c.hold) return false;
	if (s.wouldOpen < 0) s.wouldOpen = age;
	return true;
}

}  // namespace early
