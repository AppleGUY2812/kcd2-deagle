// KCD2 dice (Farkle): scoring, dice models and the two-player win-chance solver. Header-only, shared by the CLI tools
// (tools/farkle) and the in-game advisor.
//
// Rules (user's table, 2026-10-27): single 1 = 100, single 5 = 50; three of a kind = 1000 for 1s, 100 x face otherwise,
// each die past three doubles it; straights 1-5 = 500, 2-6 = 750, 1-6 = 1500. A joker ("1" face of Tengri's die, user;
// of the Devil's head die, assumed) stands in for any face inside a three-or-more of a kind or a straight, never alone.
// A turn: roll the dice in hand, set aside at least one scoring group, then bank or roll the rest; nothing to set aside
// loses the turn's points; all six set aside = all six again. First to the target score wins.
//
// The win solver: W[p][a][b] = chance that player p, at the start of their turn with a points against b, wins, both
// players playing to win (each with their own dice). Banking moves to a larger a + b, so pairs are solved from the
// largest score sum down; within a pair the two turn-start values depend on each other only through a turn lost
// (u = H0(1 - v), v = H1(1 - u)), solved by a secant step on that one number.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace farkle {
constexpr int STEP = 50;   // every score is a multiple of 50
constexpr int NEG = -1;

struct Die { std::string name; std::array<double, 6> p{}; bool joker = false; };

// ---- scoring: the best score that uses every die of a group (count per face + jokers), NEG if some die can't score
struct Scorer {
	std::vector<int> memo = std::vector<int>(7 * 7 * 7 * 7 * 7 * 7 * 7, -2);
	static int Key(const int* c, int j) { int k = j; for (int f = 0; f < 6; f++) k = k * 7 + c[f]; return k; }
	int Best(int* c, int j) {
		int tot = j; for (int f = 0; f < 6; f++) tot += c[f];
		if (tot == 0) return 0;
		int& m = memo[Key(c, j)];
		if (m != -2) return m;
		int res = NEG;
		auto take = [&](int pts) { if (pts > res) res = pts; };
		if (c[0]) { c[0]--; int r = Best(c, j); c[0]++; if (r >= 0) take(100 + r); }
		if (c[4]) { c[4]--; int r = Best(c, j); c[4]++; if (r >= 0) take(50 + r); }
		for (int v = 0; v < 6; v++)
			for (int k = 3; k <= std::min(6, c[v] + j); k++) {
				const int a = std::min(c[v], k), b = k - a;
				c[v] -= a; int r = Best(c, j - b); c[v] += a;
				if (r >= 0) take(((v == 0 ? 1000 : 100 * (v + 1)) << (k - 3)) + r);
			}
		static const int st[3][3] = { { 0, 5, 1500 }, { 0, 4, 500 }, { 1, 5, 750 } };
		for (auto& s : st) {
			int need = 0;
			for (int f = s[0]; f <= s[1]; f++) need += c[f] == 0;
			if (need > j) continue;
			int used[6] = {};
			for (int f = s[0]; f <= s[1]; f++) if (c[f]) { c[f]--; used[f] = 1; }
			int r = Best(c, j - need);
			for (int f = 0; f < 6; f++) c[f] += used[f];
			if (r >= 0) take(s[2] + r);
		}
		return m = res;
	}
	// faces[i] in 0..5 for die i of `dice`, sel = the dice set aside (bit i)
	int Score(const std::array<const Die*, 6>& dice, const int* faces, int sel) {
		int c[6] = {}, j = 0;
		for (int i = 0; i < 6; i++) if (sel >> i & 1) { if (dice[i]->joker && faces[i] == 0) j++; else c[faces[i]]++; }
		return Best(c, j);
	}
};

// ---- a dice set. Identical dice are interchangeable, so a hand is the count in hand of each kind of die (mixed radix:
// state = sum h[i] * mul[i]); 6 different dice give 64 states, 5 + 1 give 12, 6 alike give 7. State 0 (nothing left)
// stands for all six again. Per state: every distinct list of choices a roll can give, with its chance.
struct Opt { uint8_t rem; uint16_t sc; };   // the hand after setting aside (state), points in steps of 50
struct RollClass { double p; uint32_t o0, on; };
struct Model {
	std::array<const Die*, 6> dice{};
	int kinds = 0, cnt[6] = {}, mul[6] = {}, N = 0, full = 0;
	const Die* kind[6] = {};
	std::vector<RollClass> rolls[64];
	std::vector<Opt> opts;
	double bust[64] = {};
	int Count(int st, int i) const { return st / mul[i] % (cnt[i] + 1); }
};

inline void Build(const std::array<const Die*, 6>& dice, Scorer& sc, Model& m) {
	m = Model{}; m.dice = dice;
	for (auto* d : dice) {
		int k = 0;
		while (k < m.kinds && !(m.kind[k]->p == d->p && m.kind[k]->joker == d->joker)) k++;
		if (k == m.kinds) m.kind[m.kinds++] = d;
		m.cnt[k]++;
	}
	m.N = 1;
	for (int i = 0; i < m.kinds; i++) { m.mul[i] = m.N; m.N *= m.cnt[i] + 1; }
	m.full = m.N - 1;
	// the ways h alike dice of kind i can land: face counts and chance
	struct Way { int f[6]; double p; };
	auto ways = [&](int i, int h) {
		std::vector<Way> out;
		int f[6] = {};
		std::function<void(int, int)> rec = [&](int face, int left) {
			if (face == 5) {
				f[5] = left;
				double p = 1, coef = 1; int n = 0;
				for (int x = 0; x < 6; x++) for (int k = 1; k <= f[x]; k++) { n++; coef = coef * n / k; p *= m.kind[i]->p[x]; }
				if (p * coef > 0) { Way w; std::memcpy(w.f, f, sizeof f); w.p = p * coef; out.push_back(w); }
				return;
			}
			for (int k = 0; k <= left; k++) { f[face] = k; rec(face + 1, left - k); }
		};
		rec(0, h);
		return out;
	};
	for (int S = 1; S < m.N; S++) {
		int h[6] = {};
		for (int i = 0; i < m.kinds; i++) h[i] = m.Count(S, i);
		std::vector<Way> w[6];
		for (int i = 0; i < m.kinds; i++) w[i] = ways(i, h[i]);
		std::map<std::vector<uint32_t>, double> classes;
		int idx[6] = {};
		for (;;) {
			// one outcome: way idx[i] for each kind
			double p = 1; int f[6][6];
			for (int i = 0; i < m.kinds; i++) { p *= w[i][idx[i]].p; std::memcpy(f[i], w[i][idx[i]].f, sizeof f[i]); }
			// every sub-multiset to set aside: take[i][face] in 0..f[i][face]
			int best[64]; std::fill(best, best + 64, 0);
			int take[6][6] = {};
			for (;;) {
				int c[6] = {}, j = 0, rem = S, any = 0;
				for (int i = 0; i < m.kinds; i++) for (int x = 0; x < 6; x++) if (take[i][x]) {
					any = 1; rem -= take[i][x] * m.mul[i];
					if (m.kind[i]->joker && x == 0) j += take[i][x]; else c[x] += take[i][x];
				}
				if (any) {
					const int s = sc.Best(c, j);
					if (s > 0) { if (!rem) rem = m.full; best[rem] = std::max(best[rem], s); }
				}
				int i = 0, x = 0;   // next sub-multiset (odometer)
				for (; i < m.kinds; i++) {
					for (x = 0; x < 6; x++) { if (take[i][x] < f[i][x]) { take[i][x]++; break; } take[i][x] = 0; }
					if (x < 6) break;
				}
				if (i == m.kinds) break;
			}
			std::vector<uint32_t> key;   // no pruning: more dice in hand isn't always better (all set aside = six fresh)
			for (int r = 0; r < 64; r++) if (best[r]) key.push_back((uint32_t)r << 16 | (uint32_t)(best[r] / STEP));
			if (key.empty()) m.bust[S] += p; else classes[key] += p;
			int i = 0;   // next outcome
			for (; i < m.kinds; i++) { if (++idx[i] < (int)w[i].size()) break; idx[i] = 0; }
			if (i == m.kinds) break;
		}
		for (auto& kv : classes) {
			m.rolls[S].push_back({ kv.second, (uint32_t)m.opts.size(), (uint32_t)kv.first.size() });
			for (uint32_t k : kv.first) m.opts.push_back({ (uint8_t)(k >> 16), (uint16_t)(k & 0xffff) });
		}
	}
}

// ---- the two-player game to `K` steps (target / 50); player 0 uses model m[0], player 1 m[1]
struct Game {
	int K = 0;
	mutable std::atomic<long> turns{ 0 };
	const Model* m[2] = {};
	std::vector<double> W[2];   // W[p][a * K + b]
	double& At(int p, int a, int b) { return W[p][(size_t)a * K + b]; }
	double Get(int p, int a, int b) const { return W[p][(size_t)a * K + b]; }

	// value of p's position (own a, other b, turn points t, dice S in hand), V = scratch [K][64]; lose = the value when
	// the turn ends with nothing (1 - the other player's turn start)
	double Bank(int p, int a, int b, int t) const { return a + t >= K ? 1.0 : 1.0 - Get(1 - p, b, a + t); }
	// also gives *slope = d value / d lose under the chosen play (the chance the turn ends in a bust), for Newton's step
	double Turn(int p, int a, int b, double lose, std::vector<double>& V, std::vector<double>& D, double* slope = nullptr) const {
		const Model& md = *m[p];
		turns++;
		const int top = K - a;   // t >= top: banking wins
		for (int t = top - 1; t >= 0; t--) {
			const double bank = t > 0 ? Bank(p, a, b, t) : -1.0;
			for (int S = 1; S < md.N; S++) {
				double roll = md.bust[S] * lose, d = md.bust[S];
				for (auto& rc : md.rolls[S]) {
					double best = 0, bd = 0;
					for (uint32_t k = 0; k < rc.on; k++) {
						const Opt& o = md.opts[rc.o0 + k];
						const int t2 = t + o.sc;
						if (t2 >= top) { best = 1.0; bd = 0; break; }
						const size_t i = (size_t)t2 * 64 + o.rem;
						if (V[i] > best) { best = V[i]; bd = D[i]; }
					}
					roll += rc.p * best; d += rc.p * bd;
				}
				const size_t i = (size_t)t * 64 + S;
				if (bank >= roll) { V[i] = bank; D[i] = 0; } else { V[i] = roll; D[i] = d; }
			}
		}
		if (slope) *slope = D[md.full];
		return V[md.full];
	}

	void Solve(int threads = 4) {
		W[0].assign((size_t)K * K, 0); W[1].assign((size_t)K * K, 0);
		for (int s = 2 * K - 2; s >= 0; s--) {
			std::vector<std::pair<int, int>> pairs;   // (a, b) with a + b = s, a <= b; solves both orders for both players
			for (int a = std::max(0, s - K + 1); a <= s / 2; a++) pairs.push_back({ a, s - a });
			std::atomic<size_t> next{ 0 };
			auto work = [&]() {
				std::vector<double> V((size_t)K * 64), D((size_t)K * 64);
				for (size_t i; (i = next++) < pairs.size();) {
					const auto [a, b] = pairs[i];
					// p = 0 to move at (x, y) <-> p = 1 at (y, x): each needs the other's value for a lost turn
					for (int c = 0; c < (a == b ? 1 : 2); c++) {
						const int x = c ? b : a, y = c ? a : b;   // p0 has x, p1 has y
						// Newton on g(u) = H0(1 - H1(1 - u)) = u; g' = slope0 * slope1; start from a neighbour's answer
						double u = y + 1 < K ? Get(0, x, y + 1) : x + 1 < K ? Get(0, x + 1, y) : 0.5;
						for (int it = 0; it < 30; it++) {
							double s1, s0;
							const double v = Turn(1, y, x, 1.0 - u, V, D, &s1);
							const double gu = Turn(0, x, y, 1.0 - v, V, D, &s0);
							const double step = (gu - u) / (1.0 - s0 * s1);
							u = std::clamp(u + step, 0.0, 1.0);
							if (std::fabs(step) < 1e-9) break;
						}
						At(0, x, y) = u;
						At(1, y, x) = Turn(1, y, x, 1.0 - u, V, D);
					}
				}
			};
			std::vector<std::thread> th;
			for (int i = 0; i < threads; i++) th.emplace_back(work);
			for (auto& t : th) t.join();
		}
	}
};
}  // namespace farkle
