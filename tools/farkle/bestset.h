// Best 6 dice from a collection (shared by tools/farkle/farkle.cpp and the Dice Advisor plugin).
// See farkle.cpp for the method: a local search over multisets of 6 dice by E[min(turn points, target)] (one turn,
// best play for it), memoised per set. One search at a time (the scoring memo and templates are shared); the search
// itself scores up to `threads` candidate sets at once.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace bestset {
constexpr int STEP = 50;
constexpr int NEG = -1;

struct Die { std::string name; std::array<double, 6> p; bool devil; };

// ---- scoring: best score that uses every die of a multiset (faces 1..6 + jokers), or NEG if some die can't score
inline std::vector<int> g_memo;
inline void (*g_threadStart)() = nullptr;   // run first on every search thread (the plugin lowers their priority)
inline int Key(const int* c, int j) { int k = j; for (int f = 0; f < 6; f++) k = k * 7 + c[f]; return k; }
inline int Best(int* c, int j) {
	int tot = j; for (int f = 0; f < 6; f++) tot += c[f];
	if (tot == 0) return 0;
	const int key = Key(c, j);
	if (g_memo[key] != -2) return g_memo[key];
	int res = NEG;
	auto take = [&](int pts) { if (pts > res) res = pts; };
	// singles
	if (c[0]) { c[0]--; int r = Best(c, j); c[0]++; if (r >= 0) take(100 + r); }
	if (c[4]) { c[4]--; int r = Best(c, j); c[4]++; if (r >= 0) take(50 + r); }
	// a joker never scores alone (user, 2026-10-27): only inside a three-or-more of a kind or a straight
	// n of a kind, real dice first (a joker can always stand in later)
	for (int v = 0; v < 6; v++)
		for (int k = 3; k <= std::min(6, c[v] + j); k++) {
			const int a = std::min(c[v], k), b = k - a;
			c[v] -= a;
			int r = Best(c, j - b);
			c[v] += a;
			if (r >= 0) take(((v == 0 ? 1000 : 100 * (v + 1)) << (k - 3)) + r);
		}
	// straights
	struct S { int lo, hi, pts; } st[3] = { { 0, 5, 1500 }, { 0, 4, 500 }, { 1, 5, 750 } };
	for (auto& s : st) {
		int need = 0;
		for (int f = s.lo; f <= s.hi; f++) need += c[f] == 0;
		if (need > j) continue;
		int used[6] = {};
		for (int f = s.lo; f <= s.hi; f++) if (c[f]) { c[f]--; used[f] = 1; }
		int r = Best(c, j - need);
		for (int f = 0; f < 6; f++) c[f] += used[f];
		if (r >= 0) take(s.pts + r);
	}
	return g_memo[key] = res;
}

// ---- one dice set: every roll of every subset of the dice, with its probability and the set-aside choices
struct Opt { uint8_t rem; uint16_t sc; };   // sc in steps of 50
struct Roll { double p; uint32_t o0, on; };
struct Model {
	std::vector<Roll> rolls[64];   // per subset of the six dice still in hand
	std::vector<Opt> opts;
};

inline void Build(const std::array<const Die*, 6>& dice, Model& m) {
	m.opts.clear();
	for (int S = 1; S < 64; S++) {
		m.rolls[S].clear();
		int pos[6], n = 0;
		for (int i = 0; i < 6; i++) if (S >> i & 1) pos[n++] = i;
		int total = 1; for (int i = 0; i < n; i++) total *= 6;
		for (int code = 0; code < total; code++) {
			int face[6] = {}; double p = 1; int x = code;
			for (int i = 0; i < n; i++) { const int f = x % 6; x /= 6; face[pos[i]] = f; p *= dice[pos[i]]->p[f]; }
			if (p <= 0) continue;
			std::map<int, int> best;   // remaining dice -> best score for setting the rest aside
			for (int sel = S; sel; sel = (sel - 1) & S) {
				int c[6] = {}, j = 0;
				for (int i = 0; i < 6; i++) if (sel >> i & 1) {
					if (dice[i]->devil && face[i] == 0) j++; else c[face[i]]++;
				}
				const int sc = Best(c, j);
				if (sc <= 0) continue;
				int rem = S & ~sel; if (!rem) rem = 63;   // hot dice: all six again
				auto it = best.find(rem);
				if (it == best.end() || it->second < sc) best[rem] = sc;
			}
			Roll r{ p, (uint32_t)m.opts.size(), (uint32_t)best.size() };
			for (auto& kv : best) m.opts.push_back({ (uint8_t)kv.first, (uint16_t)(kv.second / STEP) });
			m.rolls[S].push_back(r);
		}
	}
}

// ---- the same model, fast: which set-aside choices a throw allows depends only on its faces and on which dice are
// jokers, never on the odds. So the choices are built once per joker count (jokers in the first positions) and throws
// with the same choices are grouped; a set then only needs its group odds (a sum of products). ~100x faster than Build.
struct Tmpl {
	std::vector<uint32_t> grp[64];                  // per throw (face code) -> group, or UINT32_MAX (farkle)
	std::vector<std::pair<uint32_t, uint32_t>> groups[64];   // per group: first option, option count
	std::vector<Opt> opts;
};
inline const Tmpl& Template(int jokers) {
	static Tmpl cache[7];
	static bool built[7] = {};
	Tmpl& T = cache[jokers];
	if (built[jokers]) return T;
	for (int S = 1; S < 64; S++) {
		int pos[6], n = 0;
		for (int i = 0; i < 6; i++) if (S >> i & 1) pos[n++] = i;
		int total = 1; for (int i = 0; i < n; i++) total *= 6;
		T.grp[S].assign(total, UINT32_MAX);
		std::map<std::vector<Opt>, uint32_t, bool (*)(const std::vector<Opt>&, const std::vector<Opt>&)> seen(
			[](const std::vector<Opt>& a, const std::vector<Opt>& b) {
				return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
					[](const Opt& x, const Opt& y) { return x.rem != y.rem ? x.rem < y.rem : x.sc < y.sc; });
			});
		for (int code = 0; code < total; code++) {
			int face[6] = {}; int x = code;
			for (int i = 0; i < n; i++) { face[pos[i]] = x % 6; x /= 6; }
			std::map<int, int> best;
			for (int sel = S; sel; sel = (sel - 1) & S) {
				int c[6] = {}, j = 0;
				for (int i = 0; i < 6; i++) if (sel >> i & 1) {
					if (i < jokers && face[i] == 0) j++; else c[face[i]]++;
				}
				const int sc = Best(c, j);
				if (sc <= 0) continue;
				int rem = S & ~sel; if (!rem) rem = 63;
				auto it = best.find(rem);
				if (it == best.end() || it->second < sc) best[rem] = sc;
			}
			if (best.empty()) continue;
			std::vector<Opt> o;
			for (auto& kv : best) o.push_back({ (uint8_t)kv.first, (uint16_t)(kv.second / STEP) });
			auto it = seen.find(o);
			if (it == seen.end()) {
				it = seen.emplace(o, (uint32_t)T.groups[S].size()).first;
				T.groups[S].push_back({ (uint32_t)T.opts.size(), (uint32_t)o.size() });
				T.opts.insert(T.opts.end(), o.begin(), o.end());
			}
			T.grp[S][code] = it->second;
		}
	}
	built[jokers] = true;
	return T;
}
// dice: the jokers first (as many as `jokers`), the order the template was built for
inline void BuildFast(const std::array<const Die*, 6>& dice, int jokers, Model& m) {
	const Tmpl& T = Template(jokers);
	m.opts = T.opts;
	std::vector<double> gp;
	for (int S = 1; S < 64; S++) {
		int pos[6], n = 0;
		for (int i = 0; i < 6; i++) if (S >> i & 1) pos[n++] = i;
		gp.assign(T.groups[S].size(), 0.0);
		const auto& g = T.grp[S];
		for (uint32_t code = 0; code < g.size(); code++) {
			if (g[code] == UINT32_MAX) continue;
			double p = 1; uint32_t x = code;
			for (int i = 0; i < n; i++) { p *= dice[pos[i]]->p[x % 6]; x /= 6; }
			gp[g[code]] += p;
		}
		m.rolls[S].clear();
		for (size_t k = 0; k < gp.size(); k++)
			if (gp[k] > 0) m.rolls[S].push_back({ gp[k], T.groups[S][k].first, T.groups[S][k].second });
	}
}

// search score: E[min(turn points, target)] with the best play for it
inline double OneTurn(const Model& m, int K) {
	std::vector<std::array<double, 64>> V(K + 1);
	for (int S = 0; S < 64; S++) V[K][S] = K;
	for (int t = K - 1; t >= 0; t--)
		for (int S = 1; S < 64; S++) {
			double roll = 0;
			for (auto& r : m.rolls[S]) {
				double b = 0;
				for (uint32_t k = 0; k < r.on; k++) {
					const Opt& o = m.opts[r.o0 + k];
					const int t2 = std::min(K, t + o.sc);
					b = std::max(b, V[t2][o.rem]);
				}
				roll += r.p * b;
			}
			V[t][S] = t == 0 ? roll : std::max((double)t, roll);
		}
	return V[0][63] * STEP;
}

// exact: expected turns to score K steps from nothing, best play throughout
inline double Turns(const Model& m, int K, std::vector<double>* curve = nullptr) {
	std::vector<double> N(K + 1, 0.0);
	std::vector<std::array<double, 64>> C(K + 1);
	for (int r = 1; r <= K; r++) {
		auto h = [&](double x) {   // 1 + expected further turns, with a farkle worth x more turns
			for (int t = r - 1; t >= 0; t--)
				for (int S = 1; S < 64; S++) {
					double roll = 0;
					for (auto& rl : m.rolls[S]) {
						double b = x;   // nothing to set aside: the turn is lost
						for (uint32_t k = 0; k < rl.on; k++) {
							const Opt& o = m.opts[rl.o0 + k];
							const int t2 = t + o.sc;
							b = std::min(b, t2 >= r ? 0.0 : C[t2][o.rem]);
						}
						roll += rl.p * b;
					}
					C[t][S] = t == 0 ? roll : std::min(N[r - t], roll);
				}
			return 1 + C[0][63];
		};
		// x = h(x): h rises with slope < 1 (the chance the turn fails), so the secant step converges in a few rounds
		double x0 = N[r - 1], f0 = h(x0) - x0;
		double x1 = x0 + std::max(1.0, f0), f1 = h(x1) - x1;
		for (int it = 0; it < 60 && std::fabs(f1) > 1e-10; it++) {
			const double x2 = x1 - f1 * (x1 - x0) / (f1 - f0);
			x0 = x1; f0 = f1; x1 = x2; f1 = h(x1) - x1;
		}
		N[r] = x1;
	}
	if (curve) *curve = N;
	return N[K];
}

inline std::vector<Die> Load(const char* path) {
	// dice_game.csv (from the game's item.xml): name, id, key, p1_pct..p6_pct, weights, sum, joker_face, ...
	// the wiki CSV: name, p1..p6, joker dice by name
	std::ifstream in(path); std::vector<Die> out; std::string line;
	int p0 = 1, jcol = -1;
	while (std::getline(in, line)) {
		std::vector<std::string> cells; std::string cur; bool q = false;
		for (char ch : line) { if (ch == '"') q = !q; else if (ch == ',' && !q) { cells.push_back(cur); cur.clear(); } else if (ch != '\r') cur += ch; }
		cells.push_back(cur);
		if (cells.size() > 3 && cells[3] == "p1_pct") {
			p0 = 3;
			for (size_t i = 0; i < cells.size(); i++) if (cells[i] == "joker_face") jcol = (int)i;
			continue;
		}
		if ((int)cells.size() < p0 + 6) continue;
		Die d; d.name = cells[0]; double sum = 0; bool ok = true;
		for (int f = 0; f < 6; f++) { char* e = nullptr; const double v = strtod(cells[p0 + f].c_str(), &e); if (e == cells[p0 + f].c_str()) { ok = false; break; } d.p[f] = v; sum += v; }
		if (!ok || sum <= 0) continue;
		bool dup = false; for (auto& o : out) dup |= o.name == d.name;   // "Lucky die" twice (quest copy)
		if (dup) continue;
		for (auto& v : d.p) v /= sum;
		d.devil = jcol >= 0 ? (jcol < (int)cells.size() && cells[jcol] == "1")
		                    : d.name.find("Devil") != std::string::npos || d.name.find("Tengri") != std::string::npos || d.name.find("Balatro") != std::string::npos;
		out.push_back(d);
	}
	return out;
}

inline std::vector<std::string> Split(const std::string& s) {
	std::vector<std::string> r; std::stringstream ss(s); std::string x;
	while (std::getline(ss, x, ';')) if (!x.empty()) r.push_back(x);
	return r;
}

// One entry per die name you own (count = copies). Identical tables merge (they are interchangeable).
struct Owned { std::string name; std::array<double, 6> p; bool devil = false; int count = 0; };
struct Pick { std::vector<int> count; double oneTurn = 0; };   // count per Owned index, sums to 6

// The best sets, best first (at most top). Empty when fewer than 6 dice are owned.
inline std::vector<Pick> BestSets(const std::vector<Owned>& own, int target, int top, int threads = 1) {
	const int K = target / STEP;
	if (g_memo.empty()) g_memo.assign(7 * 7 * 7 * 7 * 7 * 7 * 7, -2);
	std::vector<Die> rep; std::vector<std::vector<int>> members; std::vector<int> cap;
	int total = 0;
	for (size_t i = 0; i < own.size(); i++) {
		if (own[i].count <= 0) continue;
		total += own[i].count;
		int dup = -1;
		for (size_t k = 0; k < rep.size() && dup < 0; k++) {
			bool same = rep[k].devil == own[i].devil;
			for (int f = 0; f < 6 && same; f++) same = std::fabs(rep[k].p[f] - own[i].p[f]) < 1e-9;
			if (same) dup = (int)k;
		}
		if (dup >= 0) { members[dup].push_back((int)i); cap[dup] = std::min(6, cap[dup] + own[i].count); continue; }
		rep.push_back(Die{ own[i].name, own[i].p, own[i].devil });
		members.push_back({ (int)i });
		cap.push_back(std::min(6, own[i].count));
	}
	if (total < 6 || rep.empty()) return {};
	const int T = (int)rep.size();
	std::map<std::vector<int>, double> seen;
	// the templates every set can need, built before any thread reads them
	int jokerCopies = 0;
	for (int k = 0; k < T; k++) if (rep[k].devil) jokerCopies += cap[k];
	for (int j = 0; j <= std::min(6, jokerCopies); j++) Template(j);
	auto eval = [&](const std::vector<int>& set) {   // thread-safe: reads only the templates and rep
		std::array<const Die*, 6> d{}; int jokers = 0;
		for (int i = 0; i < 6; i++) if (rep[set[i]].devil) d[jokers++] = &rep[set[i]];
		for (int i = 0, k = jokers; i < 6; i++) if (!rep[set[i]].devil) d[k++] = &rep[set[i]];
		Model m; BuildFast(d, jokers, m);
		return OneTurn(m, K);
	};
	auto score = [&](const std::vector<int>& set) {
		auto it = seen.find(set);
		if (it != seen.end()) return it->second;
		return seen[set] = eval(set);
	};
	// scores the unseen sets of a batch, `threads` at a time
	const int nt = std::max(1, threads);
	auto scoreBatch = [&](const std::vector<std::vector<int>>& sets) {
		std::vector<const std::vector<int>*> todo;
		for (auto& s : sets) if (!seen.count(s)) todo.push_back(&s);
		std::vector<double> val(todo.size());
		if (nt == 1 || todo.size() < 2) { for (size_t i = 0; i < todo.size(); i++) val[i] = eval(*todo[i]); }
		else {
			std::vector<std::thread> pool;
			for (int w = 0; w < nt; w++) pool.emplace_back([&, w] {
				if (g_threadStart) g_threadStart();
				for (size_t i = w; i < todo.size(); i += nt) val[i] = eval(*todo[i]);
			});
			for (auto& th : pool) th.join();
		}
		for (size_t i = 0; i < todo.size(); i++) seen[*todo[i]] = val[i];
	};
	auto valid = [&](const std::vector<int>& set) {
		std::map<int, int> c; for (int x : set) if (++c[x] > cap[x]) return false;
		return true;
	};
	// starts: as many copies as allowed of the dice that score most often alone (a 1 or a 5), then two random sets
	std::vector<int> order(T);
	for (int k = 0; k < T; k++) order[k] = k;
	auto single = [&](int k) { return rep[k].p[0] * 100 + rep[k].p[4] * 50; };
	std::sort(order.begin(), order.end(), [&](int x, int y) { return single(x) > single(y); });
	std::vector<std::vector<int>> starts;
	for (int first = 0; first < std::min(T, 2); first++) {
		std::vector<int> st;
		for (int c = 0; c < cap[order[first]] && st.size() < 6; c++) st.push_back(order[first]);
		for (int k : order) for (int c = 0; c < cap[k] && st.size() < 6; c++) if (k != order[first]) st.push_back(k);
		if (st.size() == 6) starts.push_back(st);
	}
	std::mt19937 rng(12345);
	for (int s = 0; s < 2; s++) {
		std::vector<int> st;
		for (int guard = 0; st.size() < 6 && guard < 10000; guard++) {
			std::vector<int> t = st; t.push_back((int)(rng() % T));
			if (valid(t)) st = t;
		}
		if (st.size() == 6) starts.push_back(st);
	}
	for (auto st : starts) {
		std::sort(st.begin(), st.end());
		double cur = score(st);
		// first improvement, as before, but the neighbours are scored in batches of `threads`
		for (bool improved = true; improved;) {
			improved = false;
			std::vector<std::vector<int>> nb;
			for (int i = 0; i < 6; i++)
				for (int k = 0; k < T; k++) {
					std::vector<int> t = st; t[i] = k; std::sort(t.begin(), t.end());
					if (t == st || !valid(t)) continue;
					nb.push_back(t);
				}
			for (size_t b = 0; b < nb.size() && !improved; b += nt) {
				std::vector<std::vector<int>> batch(nb.begin() + b, nb.begin() + std::min(nb.size(), b + nt));
				scoreBatch(batch);
				for (auto& t : batch) {
					const double v = seen[t];
					if (v > cur + 1e-9) { st = t; cur = v; improved = true; break; }
				}
			}
		}
	}
	std::vector<std::pair<double, std::vector<int>>> ranked;
	for (auto& kv : seen) ranked.push_back({ kv.second, kv.first });
	std::sort(ranked.rbegin(), ranked.rend());
	std::vector<Pick> out;
	for (int r = 0; r < top && r < (int)ranked.size(); r++) {
		Pick p; p.count.assign(own.size(), 0); p.oneTurn = ranked[r].first;
		std::map<int, int> per; for (int x : ranked[r].second) per[x]++;
		for (auto& kv : per) {   // spread a merged table's copies over the names that have it, in order
			int left = kv.second;
			for (int i : members[kv.first]) { const int n = std::min(left, own[i].count); p.count[i] += n; left -= n; }
		}
		out.push_back(p);
	}
	return out;
}
}  // namespace bestset
