#pragma once
#include <algorithm>
// farkle_core.h below uses std::min/std::max. windows.h's min/max macros would break those tokens in a plugin that
// includes it first, so they are undefined here for every translation unit that includes this header.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include "advisor.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace diceadvisor {

struct DieState {
	int id = 0, state = 0, value = 0;
	bool joker = false;
	float p[6] = {};
	float x = 0, y = 0;
};

struct Snapshot {
	bool playerTurn = false, human = false;
	int me = 0, target = 0, rolledMask = 0, heldMask = 0;
	int banked[2] = {}, turn[2] = {};
	int count[2] = {};
	DieState dice[2][6];
};

// Everything the Lua panel needs, already expressed as numbers (Lua does the wording and the existing HUD text call).
struct Presentation {
	// the advice for the current throw
	int keep = 0, score = 0;
	bool recommendBank = false;
	std::vector<int> selectorIds;   // 100 + die index, for dice to set aside that are not held yet
	double finish = 0, hot = 0, opponentFinish = 0;   // probabilities
	double throwPoints = 0, bankPoints = 0;           // average points of the next throw / points banked now
	// the player's own held selection (die state 1)
	bool hasPick = false;
	int pickScore = 0, pickDiceLeft = 0;
	double pickBust = 0, pickWinBank = 0, pickWinRoll = 0, pickFinish = 0, pickHot = 0;
	double pickThrowPoints = 0, pickBankPoints = 0;
	// "take fewer": the advice keep vs the largest scoring keep, both from the same solve
	bool hasTakeFewer = false;
	int tfKeep = 0, tfScore = 0, tfAllKeep = 0, tfAllScore = 0;
	double tfWin = 0, tfAllWin = 0;
	// committed-choice cost, percent (0 = none); set by the plugin worker
	double mistake = 0;
};

inline bool MakeDie(const DieState& src, farkle::Die& d, std::string& error) {
	d.joker = src.joker;
	double sum = 0;
	for (int j = 0; j < 6; j++) {
		if (!std::isfinite(src.p[j]) || src.p[j] < 0) { error = "invalid die probability"; return false; }
		d.p[j] = src.p[j];
		sum += src.p[j];
	}
	if (!(sum > 0)) { error = "die probabilities have zero weight"; return false; }
	for (double& p : d.p) p /= sum;
	d.name = farkle::DieKeyName(d.p, d.joker);   // cache identity: probabilities + joker, not "live"
	return true;
}

inline bool MakeDice(const Snapshot& s, int side, std::array<farkle::Die, 6>& out, std::string& error) {
	if (side < 0 || side > 1) { error = "invalid player"; return false; }
	if (s.count[side] != 6) { error = "the player does not have exactly six dice"; return false; }
	for (int i = 0; i < 6; i++) if (!MakeDie(s.dice[side][i], out[i], error)) return false;
	return true;
}

inline bool MakeDice(const Snapshot& s, std::array<farkle::Die, 6>& out, std::string& error) {
	return MakeDice(s, s.me, out, error);
}

inline int HumanFaces(const Snapshot& s, int* faces) {
	for (int i = 0; i < 6; i++) faces[i] = s.dice[s.me][i].value == 6 ? 0 : s.dice[s.me][i].value;
	return s.me;
}

inline int StateAfter(const farkle::Model& md, int rolledMask, int keep) {
	const int left = rolledMask & ~keep;
	if (!left) return md.full;   // everything set aside: six fresh dice
	int state = 0;
	for (int i = 0; i < 6; i++) if (left >> i & 1) state += md.mul[farkle::KindOf(md, i)];
	return state;
}

// The panel for the current position. The solver always plays the human as player 0 (see AdviseSnapshot), while the live
// snapshot's human may be side 0 or 1, so all scores and dice below use `h = s.me` with solver player 0.
inline Presentation Present(const farkle::Game& g, const Snapshot& s, const farkle::Advice& a,
                            const farkle::Selection& sel = farkle::Selection{}) {
	Presentation p;
	const int h = s.me;
	const farkle::Model& md = *g.m[0];
	int faces[6] = {};
	HumanFaces(s, faces);
	const int aStep = s.banked[h] / farkle::STEP, bStep = s.banked[1 - h] / farkle::STEP;
	const int tStep = s.turn[h] / farkle::STEP;
	farkle::Scorer scorer;

	p.keep = a.keep;
	p.recommendBank = a.bank;
	p.opponentFinish = farkle::FinishFromTurnStart(g, 1, s.banked[1 - h], s.banked[h]);
	const farkle::TurnTables tt = farkle::TurnTablesFor(g, 0, aStep, bStep);

	if (a.keep) {
		p.score = scorer.Score(md.dice, faces, a.keep);
		const int leftState = StateAfter(md, s.rolledMask, a.keep);
		p.bankPoints = s.turn[h] + p.score;
		p.throwPoints = farkle::ExpectedThrowPoints(md, leftState);
		p.hot = farkle::HotDice(md, leftState);
		p.finish = farkle::FinishFromSelection(tt, g, 0, s.turn[h], p.score, leftState, a.bank);
		// the largest scoring keep the roll allows; only mention it when the advice deliberately keeps fewer dice
		int allKeep = 0, allCount = 0, allScore = 0;
		for (int keep = s.rolledMask; keep; keep = (keep - 1) & s.rolledMask) {
			const int score = scorer.Score(md.dice, faces, keep);
			if (score <= 0) continue;
			const int n = farkle::Popcount6(keep);
			if (n > allCount || (n == allCount && score > allScore)) { allCount = n; allKeep = keep; allScore = score; }
		}
		if (allKeep && allCount > farkle::Popcount6(a.keep)) {
			const farkle::Selection alt = farkle::EvaluateSelection(
				g, 0, s.banked[h], s.banked[1 - h], s.turn[h], faces, s.rolledMask, allKeep);
			if (alt.valid) {
				p.hasTakeFewer = true;
				p.tfKeep = a.keep;
				p.tfScore = p.score;
				p.tfWin = std::max(a.winBank, a.winRoll);
				p.tfAllKeep = allKeep;
				p.tfAllScore = allScore;
				p.tfAllWin = std::max(alt.winBank, alt.winRoll);
			}
		}
	} else {
		p.score = 0;
		p.bankPoints = s.turn[h];
	}

	// the game's own player selector for every advised die that is not already held
	for (int i = 0; i < 6; i++)
		if ((a.keep >> i & 1) && !(s.heldMask >> i & 1)) p.selectorIds.push_back(100 + s.dice[h][i].id);

	if (sel.valid && s.heldMask) {
		p.hasPick = true;
		p.pickScore = sel.score;
		p.pickDiceLeft = sel.diceLeft;
		p.pickBust = sel.bust;
		p.pickWinBank = sel.winBank;
		p.pickWinRoll = sel.winRoll;
		const int leftState = StateAfter(md, s.rolledMask, s.heldMask);
		p.pickHot = farkle::HotDice(md, leftState);
		p.pickThrowPoints = farkle::ExpectedThrowPoints(md, leftState);
		p.pickBankPoints = s.turn[h] + sel.score;
		const bool pickBank = sel.winBank >= sel.winRoll;
		p.pickFinish = farkle::FinishFromSelection(tt, g, 0, s.turn[h], sel.score, leftState, pickBank);
	}
	return p;
}

inline farkle::Advice AdviseSnapshot(const farkle::Game& g, const Snapshot& s) {
	int faces[6] = {};
	HumanFaces(s, faces);
	return farkle::Advise(g, 0, s.banked[s.me], s.banked[1 - s.me], s.turn[s.me], faces, s.rolledMask);
}

inline farkle::Selection SelectSnapshot(const farkle::Game& g, const Snapshot& s) {
	int faces[6] = {};
	HumanFaces(s, faces);
	return farkle::EvaluateSelection(g, 0, s.banked[s.me], s.banked[1 - s.me], s.turn[s.me],
	                                 faces, s.rolledMask, s.heldMask);
}

inline std::string MatchupKey(const Snapshot& s) {
	char b[2048];
	// `me` is part of the identity: the models are built human-first, so the same two dice sets swapped between the
	// players are a different matchup.
	int n = std::snprintf(b, sizeof b, "%d|%d", s.target, s.me);
	for (int side = 0; side < 2; side++) for (int i = 0; i < 6; i++) {
		const DieState& d = s.dice[side][i];
		n += std::snprintf(b + n, sizeof b - (size_t)n, "|%d", d.joker ? 1 : 0);
		for (float p : d.p) n += std::snprintf(b + n, sizeof b - (size_t)n, ",%08x", (unsigned)std::lround(p * 1000000.0));
	}
	return std::string(b, (size_t)n);
}
}  // namespace diceadvisor
