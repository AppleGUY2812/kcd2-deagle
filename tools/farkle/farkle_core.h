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

	// ---- the row solver (2026-10-30; target 8000 took 122 s with Solve, the user's game). Inside a turn the start score
	// a only matters through c = a + t (what banking gives; c >= K wins) and the value of a bust, lose = 1 - the other
	// player's turn start. So per player and opponent score ("row") one table F[c][S] serves every start score, with lose
	// sampled at J points; the turn-start value at the real lose is a cubic Hermite interpolation of those samples (value
	// + slope = the bust chance under that sample's play). Cost ~ K^2 x J table rows instead of K^3 x ~3.4 turns.
	// Pass 1 samples lose over 0..1; pass 2 only over each row's band of lose values from pass 1 (+- pad).
	// Checked against Solve (scratchpad bench, 2026-10-30, Favourable x3 + Painted + Weighted + Ordinary vs Ci + Even x2 +
	// Ordinary x3): target 8000 in 8.1 s instead of 137.6 s on a 12-thread Xeon (passes of 5 and 17 samples). Table max
	// error 0.33 points of win %; playing every turn by it loses at most 0.0034 points of win chance against the exact
	// table. Six ordinary dice at 8000 (the widest bands): 0.98 points table error, at most 0.02 points lost per turn.
	// Balatro x3 + Pie + Weighted + Tengri vs ordinary at 5000: 3.5 s instead of 37 s, at most 0.0044 points per turn.
	int J = 9;
	double pad = 0.02;
	std::vector<float> lo[2], span[2];   // per (player, row): the band of lose values sampled (empty: 0..1)
	double Lam(int p, int row, int j) const { return lo[p].empty() ? (double)j / (J - 1) : lo[p][row] + span[p][row] * j / (J - 1); }
	// v, d at the J samples (d per band unit) -> the value at band position x in 0..1
	double Hermite(const float* v, const float* d, double x) const {
		const double f = std::clamp(x, 0.0, 1.0) * (J - 1);
		const int i = std::clamp((int)f, 0, J - 2);
		const double h = 1.0 / (J - 1), s = f - i, s2 = s * s, s3 = s2 * s;
		return (2 * s3 - 3 * s2 + 1) * v[i] + (s3 - 2 * s2 + s) * h * d[i] + (-2 * s3 + 3 * s2) * v[i + 1] + (s3 - s2) * h * d[i + 1];
	}
	double Secant(const float* v, double x) const {
		const int i = std::clamp((int)(std::clamp(x, 0.0, 1.0) * (J - 1)), 0, J - 2);
		return (v[i + 1] - v[i]) * (J - 1);
	}
	void SolveRows(int threads, int samples, bool banded) {
		J = samples;
		for (int p = 0; p < 2; p++) {
			lo[p].clear(); span[p].clear();
			if (!banded) continue;
			lo[p].resize(K); span[p].resize(K);
			for (int row = 0; row < K; row++) {   // row = the other player's score; lose = 1 - their turn start vs a
				double mn = 1, mx = 0;
				for (int a = 0; a < K; a++) { const double l = 1.0 - Get(1 - p, row, a); mn = std::min(mn, l); mx = std::max(mx, l); }
				mn = std::max(0.0, mn - pad); mx = std::min(1.0, mx + pad);
				lo[p][row] = (float)mn; span[p][row] = (float)std::max(1e-6, mx - mn);
			}
		}
		W[0].assign((size_t)K * K, 0); W[1].assign((size_t)K * K, 0);
		std::vector<float> Fv[2], Fd[2];   // [row][c][S][j]: value, d value / d lose
		for (int p = 0; p < 2; p++) { Fv[p].assign((size_t)K * K * m[p]->N * J, 0.f); Fd[p].assign(Fv[p].size(), 0.f); }
		auto idx = [&](int p, int row, int c, int S) { return (((size_t)row * K + c) * m[p]->N + S) * J; };
		// throw values (banking not allowed) at (row, c) for every S and sample -> rv, rd [S][j]
		auto rollRow = [&](int p, int row, int c, float* rv, float* rd, std::vector<double>& acc, std::vector<double>& accd,
		                   float* best, float* bd) {
			const Model& md = *m[p];
			turns++;
			for (int S = 1; S < md.N; S++) {
				for (int j = 0; j < J; j++) { acc[j] = md.bust[S] * Lam(p, row, j); accd[j] = md.bust[S]; }
				for (auto& rc : md.rolls[S]) {
					for (int j = 0; j < J; j++) { best[j] = 0.f; bd[j] = 0.f; }
					bool win = false;
					for (uint32_t k = 0; k < rc.on; k++) {
						const Opt& o = md.opts[rc.o0 + k];
						const int c2 = c + o.sc;
						if (c2 >= K) { win = true; break; }
						const size_t at = idx(p, row, c2, o.rem);
						const float* v = &Fv[p][at];
						const float* d = &Fd[p][at];
						for (int j = 0; j < J; j++) { const bool g = v[j] > best[j]; best[j] = g ? v[j] : best[j]; bd[j] = g ? d[j] : bd[j]; }
					}
					if (win) for (int j = 0; j < J; j++) acc[j] += rc.p;
					else for (int j = 0; j < J; j++) { acc[j] += rc.p * best[j]; accd[j] += rc.p * bd[j]; }
				}
				for (int j = 0; j < J; j++) { rv[(size_t)S * J + j] = (float)acc[j]; rd[(size_t)S * J + j] = (float)accd[j]; }
			}
		};
		for (int s = 2 * K - 2; s >= 0; s--) {
			std::vector<std::pair<int, int>> pairs;
			for (int a = std::max(0, s - K + 1); a <= s / 2; a++) pairs.push_back({ a, s - a });
			std::atomic<size_t> next{ 0 };
			auto work = [&]() {
				std::vector<float> r0((size_t)64 * J), d0((size_t)64 * J), r1((size_t)64 * J), d1((size_t)64 * J);
				std::vector<float> best(J), bd(J), e0(J), e1(J);
				std::vector<double> acc(J), accd(J);
				for (size_t i; (i = next++) < pairs.size();) {
					const auto [a, b] = pairs[i];
					for (int c = 0; c < (a == b ? 1 : 2); c++) {
						const int x = c ? b : a, y = c ? a : b;   // p0 has x (its row: y), p1 has y (its row: x)
						rollRow(0, y, x, r0.data(), d0.data(), acc, accd, best.data(), bd.data());
						rollRow(1, x, y, r1.data(), d1.data(), acc, accd, best.data(), bd.data());
						// the turn starts (six dice): value and slope per band unit
						const float* R0 = &r0[(size_t)m[0]->full * J]; const float* R1 = &r1[(size_t)m[1]->full * J];
						const double s0 = lo[0].empty() ? 1.0 : span[0][y], s1 = lo[1].empty() ? 1.0 : span[1][x];
						const double l0 = lo[0].empty() ? 0.0 : lo[0][y], l1 = lo[1].empty() ? 0.0 : lo[1][x];
						for (int j = 0; j < J; j++) { e0[j] = (float)(d0[(size_t)m[0]->full * J + j] * s0); e1[j] = (float)(d1[(size_t)m[1]->full * J + j] * s1); }
						auto T0 = [&](double lose) { return (lose - l0) / s0; };
						auto T1 = [&](double lose) { return (lose - l1) / s1; };
						// u = H0(1 - H1(1 - u)), slopes < 1: Newton on the interpolants
						double u = y + 1 < K ? Get(0, x, y + 1) : 0.5, v = 0;
						for (int it = 0; it < 60; it++) {
							v = Hermite(R1, e1.data(), T1(1.0 - u));
							const double gu = Hermite(R0, e0.data(), T0(1.0 - v));
							const double sl = Secant(R0, T0(1.0 - v)) / s0 * Secant(R1, T1(1.0 - u)) / s1;
							const double step = (gu - u) / std::max(1e-3, 1.0 - sl);
							u = std::clamp(u + step, 0.0, 1.0);
							if (std::fabs(step) < 1e-12) break;
						}
						v = Hermite(R1, e1.data(), T1(1.0 - u));
						At(0, x, y) = u; At(1, y, x) = v;
						// banking at this score is now known: F = max(bank, throw)
						auto store = [&](int p, int row, int cc, double bank, const std::vector<float>& rv, const std::vector<float>& rd) {
							for (int S = 1; S < m[p]->N; S++) for (int j = 0; j < J; j++) {
								const size_t k = idx(p, row, cc, S) + j; const float t = rv[(size_t)S * J + j];
								if (bank >= t) { Fv[p][k] = (float)bank; Fd[p][k] = 0; } else { Fv[p][k] = t; Fd[p][k] = rd[(size_t)S * J + j]; }
							}
						};
						store(0, y, x, 1.0 - v, r0, d0);   // p0 banking at x: 1 - p1's turn start (y vs x)
						store(1, x, y, 1.0 - u, r1, d1);
					}
				}
			};
			std::vector<std::thread> th;
			for (int i = 0; i < threads; i++) th.emplace_back(work);
			for (auto& t : th) t.join();
		}
	}
	// exact for small targets (2000: ~2 s), the two-pass row solver above that
	void SolveFast(int threads = 4) {
		if (K <= 40) { Solve(threads); return; }
		SolveRows(threads, 5, false);
		SolveRows(threads, 17, true);
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
