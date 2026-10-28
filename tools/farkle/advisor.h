#pragma once
#include "farkle_core.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace farkle {
struct Advice {
	int keep = 0;
	bool bank = false;
	double winBank = 0, winRoll = 0;
};

// A stable, readable identity for a live die: its six normalized face probabilities (rounded, as the task requires) and
// the joker flag. The disk cache is keyed by die names, so every live die used to be called "live" and two different
// dice sets shared one solution. Identical dice still get the same name (and merge in Model); different dice cannot.
inline std::string DieKeyName(const std::array<double, 6>& p, bool joker) {
	char b[160];
	std::snprintf(b, sizeof b, "p=%.4f,%.4f,%.4f,%.4f,%.4f,%.4f;j=%d",
	              p[0], p[1], p[2], p[3], p[4], p[5], joker ? 1 : 0);
	return b;
}

// The player's own selection: the dice they have set aside (held) out of the rolled hand. `score` is in points;
// `diceLeft` is how many dice the next throw would use (6 when none are left), `bust` is its chance of scoring nothing,
// and winBank / winRoll are the win chances of banking now or throwing those dice, from the same solved game.
struct Selection {
	bool valid = false;
	int score = 0;
	int diceLeft = 0;
	double bust = 0, winBank = 0, winRoll = 0;
};

inline int KindOf(const Model& md, int die) {
	for (int k = 0; k < md.kinds; k++)
		if (md.kind[k]->p == md.dice[die]->p && md.kind[k]->joker == md.dice[die]->joker) return k;
	throw std::logic_error("die is absent from its model");
}

inline Selection EvaluateSelection(const Game& g, int me, int myScore, int oppScore, int turnPts,
                                   const int* faces, int rolledMask, int keep) {
	Selection out;
	if (me < 0 || me > 1) throw std::invalid_argument("player must be 0 or 1");
	if (!keep || (keep & ~rolledMask)) return out;
	const Model& md = *g.m[me];
	Scorer scorer;
	const int score = scorer.Score(md.dice, faces, keep);
	if (score <= 0) return out;
	out.valid = true;
	out.score = score;
	int left = rolledMask & ~keep;
	int state = 0;
	if (left) {
		for (int i = 0; i < 6; i++) if (left >> i & 1) { state += md.mul[KindOf(md, i)]; out.diceLeft++; }
	} else {
		state = md.full;   // all dice set aside: hot dice, six fresh
		out.diceLeft = 6;
	}
	const int a = myScore / STEP, b = oppScore / STEP, t = turnPts / STEP;
	const double lose = 1.0 - g.Get(1 - me, b, a);
	out.bust = md.bust[state];
	const int t2 = t + score / STEP;
	out.winBank = g.Bank(me, a, b, t2);
	if (t2 >= g.K - a) {
		out.winRoll = 1.0;
		return out;
	}
	std::vector<double> V((size_t)g.K * 64), D((size_t)g.K * 64);
	g.Turn(me, a, b, lose, V, D);
	double roll = md.bust[state] * lose;
	for (const RollClass& rc : md.rolls[state]) {
		double best = 0.0;
		for (uint32_t k = 0; k < rc.on; k++) {
			const Opt& o = md.opts[rc.o0 + k];
			const int t3 = t2 + o.sc;
			if (t3 >= g.K - a) { best = 1.0; break; }
			best = std::max(best, V[(size_t)t3 * 64 + o.rem]);
		}
		roll += rc.p * best;
	}
	out.winRoll = roll;
	return out;
}

inline Advice Advise(const Game& g, int me, int myScore, int oppScore, int turnPts,
                     const int* faces, int rolledMask) {
	if (me < 0 || me > 1) throw std::invalid_argument("player must be 0 or 1");
	const int a = myScore / STEP, b = oppScore / STEP, t = turnPts / STEP;
	const Model& md = *g.m[me];
	const double bust = 1.0 - g.Get(1 - me, b, a);
	Scorer scorer;
	std::vector<std::pair<int, int>> choices;
	for (int keep = rolledMask; keep; keep = (keep - 1) & rolledMask) {
		const int score = scorer.Score(md.dice, faces, keep);
		if (score > 0) choices.push_back({ keep, score / STEP });
	}
	if (choices.empty()) return { 0, false, bust, bust };

	int dieKind[6];
	for (int i = 0; i < 6; i++) dieKind[i] = KindOf(md, i);
	std::vector<double> V((size_t)g.K * 64), D((size_t)g.K * 64);
	g.Turn(me, a, b, bust, V, D);
	Advice best;
	double bestValue = -1.0;
	for (const auto& [keep, score] : choices) {
		const int left = rolledMask & ~keep;
		int state = 0;
		for (int i = 0; i < 6; i++) if (left >> i & 1) state += md.mul[dieKind[i]];
		if (!state) state = md.full;
		const int t2 = t + score;
		const double winBank = g.Bank(me, a, b, t2);
		double winRoll = 0.0;
		if (t2 < g.K - a) {
			winRoll = md.bust[state] * bust;
			for (const RollClass& rc : md.rolls[state]) {
				double value = 0.0;
				for (uint32_t k = 0; k < rc.on; k++) {
					const Opt& o = md.opts[rc.o0 + k];
					const int t3 = t2 + o.sc;
					if (t3 >= g.K - a) { value = 1.0; break; }
					value = std::max(value, V[(size_t)t3 * 64 + o.rem]);
				}
				winRoll += rc.p * value;
			}
		}
		const double value = std::max(winBank, winRoll);
		if (value > bestValue) best = { keep, winBank >= winRoll, winBank, winRoll }, bestValue = value;
	}
	return best;
}

// ---- more numbers for the moment of choosing (CONTINUE_TASK.md steps 6-7) ----
inline int Popcount6(int mask) {
	int n = 0;
	for (int i = 0; i < 6; i++) n += (mask >> i) & 1;
	return n;
}

// Chance that a throw of `state` scores with every die, so all of them are set aside and six fresh dice follow. This is
// not 1 - bust: with two dice, 1 + 5 scores both dice, but 1 + 2 scores only one (the 2 is thrown again).
inline double HotDice(const Model& md, int state) {
	if (state <= 0 || state >= md.N) return 0.0;
	double p = 0.0;
	for (const RollClass& rc : md.rolls[state]) {
		for (uint32_t k = 0; k < rc.on; k++) {
			const Opt& o = md.opts[rc.o0 + k];
			if (o.rem == md.full && o.sc > 0) { p += rc.p; break; }   // all dice taken -> six fresh
		}
	}
	return p;
}

// Expected points of the NEXT throw with hand `state`, taking the most points each roll can give. It deliberately stops
// after that throw (no hot-dice continuation): it is the "average points if I throw on" number, not a win chance.
inline double ExpectedThrowPoints(const Model& md, int state) {
	if (state <= 0 || state >= md.N) return 0.0;
	double e = 0.0;
	for (const RollClass& rc : md.rolls[state]) {
		int best = 0;
		for (uint32_t k = 0; k < rc.on; k++) best = std::max(best, (int)md.opts[rc.o0 + k].sc);
		e += rc.p * best * STEP;
	}
	return e;
}

struct TurnTables {
	std::vector<double> V, D, F;   // V = value before a throw; F = chance to reach the target before this turn ends
	int top = 0;                   // banked points at or past `top` (steps) finish the game
};

// V is Game::Turn's table. F follows the same win-maximising choice at every (turn points, hand) and tracks only whether
// the turn reaches the target, which is exactly "finish this turn, playing the advice from here".
inline TurnTables TurnTablesFor(const Game& g, int p, int a, int b) {
	TurnTables t;
	t.top = g.K - a;
	t.V.assign((size_t)g.K * 64, 0.0);
	t.D.assign((size_t)g.K * 64, 0.0);
	t.F.assign((size_t)g.K * 64, 0.0);
	const Model& md = *g.m[p];
	g.Turn(p, a, b, 1.0 - g.Get(1 - p, b, a), t.V, t.D);
	for (int tp = std::min(t.top - 1, g.K - 1); tp >= 0; tp--) {
		const double bank = tp > 0 ? g.Bank(p, a, b, tp) : -1.0;
		for (int S = 1; S < md.N; S++) {
			double rollV = 0.0, rollF = 0.0;
			for (const RollClass& rc : md.rolls[S]) {
				double bestV = -1.0, bestF = 0.0;
				for (uint32_t k = 0; k < rc.on; k++) {
					const Opt& o = md.opts[rc.o0 + k];
					const int t2 = tp + o.sc;
					const double v = t2 >= t.top ? 1.0 : t.V[(size_t)t2 * 64 + o.rem];
					const double f = t2 >= t.top ? 1.0 : t.F[(size_t)t2 * 64 + o.rem];
					if (v > bestV) { bestV = v; bestF = f; }
				}
				rollV += rc.p * bestV;
				rollF += rc.p * bestF;
			}
			t.F[(size_t)tp * 64 + S] = bank >= rollV ? 0.0 : rollF;
		}
	}
	return t;
}

// Player p starts a turn at myScore against oppScore: chance to reach the target before that turn ends.
inline double FinishFromTurnStart(const Game& g, int p, int myScore, int oppScore) {
	const int a = myScore / STEP, b = oppScore / STEP;
	if (a >= g.K) return 1.0;
	const TurnTables t = TurnTablesFor(g, p, a, b);
	return t.F[(size_t)0 * 64 + g.m[p]->full];
}

// After a selection that scores `score` and leaves hand `leftState`, the rest of this turn. `bank` means the advice is to
// bank now, which only finishes the turn when the bank reaches the target.
inline double FinishFromSelection(const TurnTables& t, const Game& g, int p, int turnPts, int score,
                                  int leftState, bool bank) {
	const int t2 = turnPts / STEP + score / STEP;
	if (t2 >= t.top) return 1.0;
	if (bank) return 0.0;
	if (leftState <= 0) leftState = g.m[p]->full;
	return t.F[(size_t)t2 * 64 + leftState];
}

// Win chance given up by a committed choice, in probability (never negative). choiceWin is the value of the choice.
inline double MistakeCost(const Advice& advice, double choiceWin) {
	return std::max(0.0, std::max(advice.winBank, advice.winRoll) - choiceWin);
}

struct CachedGame {
	std::array<Model, 2> models{};
	Game game;
};

namespace advisor_detail {
inline uint64_t CacheHash(const std::array<const Die*, 6>& mine,
                          const std::array<const Die*, 6>& opp, int target) {
	std::string key;
	auto addDice = [&](const std::array<const Die*, 6>& dice) {
		for (const Die* d : dice) key += std::to_string(d->name.size()) + ":" + d->name + ";";
		key += "|";
	};
	addDice(mine);
	addDice(opp);
	key += std::to_string(target);
	uint64_t hash = 14695981039346656037ULL;
	for (unsigned char c : key) {
		hash ^= c;
		hash *= 1099511628211ULL;
	}
	return hash;
}

inline std::filesystem::path CachePath(const std::string& cacheDir,
                                       const std::array<const Die*, 6>& mine,
                                       const std::array<const Die*, 6>& opp, int target) {
	std::ostringstream name;
	name << std::hex << std::setfill('0') << std::setw(16) << CacheHash(mine, opp, target) << ".bin";
	return std::filesystem::path(cacheDir) / name.str();
}

inline bool LoadCache(const std::filesystem::path& path, Game& g) {
	std::ifstream in(path, std::ios::binary);
	if (!in) return false;
	char magic[8];
	int32_t K;
	in.read(magic, sizeof magic);
	in.read(reinterpret_cast<char*>(&K), sizeof K);
	if (!in || std::string(magic, sizeof magic) != "FARKLEW1" || K != g.K) return false;
	const size_t n = (size_t)g.K * g.K;
	for (int p = 0; p < 2; p++) {
		g.W[p].resize(n);
		in.read(reinterpret_cast<char*>(g.W[p].data()), (std::streamsize)(n * sizeof(double)));
		if (!in) return false;
	}
	return true;
}

inline void SaveCache(const std::filesystem::path& path, const Game& g) {
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out) throw std::runtime_error("cannot create Farkle cache: " + path.string());
	const int32_t K = g.K;
	out.write("FARKLEW1", 8);
	out.write(reinterpret_cast<const char*>(&K), sizeof K);
	for (int p = 0; p < 2; p++)
		out.write(reinterpret_cast<const char*>(g.W[p].data()), (std::streamsize)(g.W[p].size() * sizeof(double)));
	if (!out) throw std::runtime_error("cannot write Farkle cache: " + path.string());
}
}  // namespace advisor_detail

inline std::unique_ptr<CachedGame> SolveCached(const std::array<const Die*, 6>& mine,
                                               const std::array<const Die*, 6>& opp, int target,
                                               const std::string& cacheDir) {
	if (target <= 0 || target % STEP) throw std::invalid_argument("target must be a positive multiple of 50");
	auto out = std::make_unique<CachedGame>();
	Scorer scorer;
	Build(mine, scorer, out->models[0]);
	Build(opp, scorer, out->models[1]);
	out->game.K = target / STEP;
	out->game.m[0] = &out->models[0];
	out->game.m[1] = &out->models[1];
	if (!cacheDir.empty()) std::filesystem::create_directories(cacheDir);
	const auto path = advisor_detail::CachePath(cacheDir, mine, opp, target);
	if (!advisor_detail::LoadCache(path, out->game)) {
		out->game.Solve();
		if (!cacheDir.empty()) advisor_detail::SaveCache(path, out->game);
	}
	return out;
}
}  // namespace farkle
