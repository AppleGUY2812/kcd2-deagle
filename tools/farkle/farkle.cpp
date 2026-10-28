// KCD2 dice (Farkle) solver: which 6 dice give the best play, solved exactly with the optimal strategy.
//
// The KCD2 game: roll your dice, set aside scoring dice (at least one combination), then bank the turn's points or roll
// the rest; a roll with nothing to set aside loses the turn's points; all six set aside = roll all six again.
// Scoring (user's table, 2026-10-27): single 1 = 100, single 5 = 50; three of a kind = 1000 for 1s, 100 x face otherwise,
// each extra die doubles; straights 1-5 = 500, 2-6 = 750, 1-6 = 1500. The devil's head is a joker for any face.
// Jokers: the "1" face of Tengri's die (user) and of the Devil's head die (assumed); a joker never scores alone (user).
//
// Why not "expected points per turn": with strong dice the best play never stops (the chance to lose six dice' worth of
// points is tiny), so that number has no limit. What wins is reaching the target score in the fewest turns:
//   exact:  N(r) = expected turns to score r more points, optimal bank / roll / set-aside choices, solved per r.
//   search: E[min(turn points, target)] (one turn, points past the target worth nothing), cheap enough for a local
//           search over all multisets of 6 dice; the best sets are then ranked by the exact N(target).
//
// usage: farkle <dice.csv> [--target 4000] [--top 8] [--own "Name;Name;..."] [--eval "Name;Name;Name;Name;Name;Name"]
//        --own limits the search to the dice you have (a name repeated = that many copies; without --own any die can
//        be used up to 6 times).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr int STEP = 50;
constexpr int NEG = -1;

struct Die { std::string name; std::array<double, 6> p; bool devil; };

// ---- scoring: best score that uses every die of a multiset (faces 1..6 + jokers), or NEG if some die can't score
std::vector<int> g_memo;
int Key(const int* c, int j) { int k = j; for (int f = 0; f < 6; f++) k = k * 7 + c[f]; return k; }
int Best(int* c, int j) {
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

void Build(const std::array<const Die*, 6>& dice, Model& m) {
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

// search score: E[min(turn points, target)] with the best play for it
double OneTurn(const Model& m, int K) {
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
double Turns(const Model& m, int K, std::vector<double>* curve = nullptr) {
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

std::vector<Die> Load(const char* path) {
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

std::vector<std::string> Split(const std::string& s) {
	std::vector<std::string> r; std::stringstream ss(s); std::string x;
	while (std::getline(ss, x, ';')) if (!x.empty()) r.push_back(x);
	return r;
}
}  // namespace

int main(int argc, char** argv) {
	if (argc < 2) { fprintf(stderr, "usage: farkle <dice.csv> [--target 4000] [--top 8] [--own \"A;B;...\"] [--eval \"A;B;C;D;E;F\"]\n"); return 1; }
	int target = 4000, top = 8; std::string own, eval;
	for (int i = 2; i + 1 < argc; i += 2) {
		if (!strcmp(argv[i], "--target")) target = atoi(argv[i + 1]);
		else if (!strcmp(argv[i], "--top")) top = atoi(argv[i + 1]);
		else if (!strcmp(argv[i], "--own")) own = argv[i + 1];
		else if (!strcmp(argv[i], "--eval")) eval = argv[i + 1];
	}
	const int K = target / STEP;
	g_memo.assign(7 * 7 * 7 * 7 * 7 * 7 * 7, -2);
	std::vector<Die> all = Load(argv[1]);
	auto find = [&](const std::string& n) -> int { for (size_t i = 0; i < all.size(); i++) if (all[i].name == n) return (int)i; return -1; };

	if (!eval.empty()) {
		auto names = Split(eval);
		if (names.size() != 6) { fprintf(stderr, "--eval needs 6 names\n"); return 1; }
		std::array<const Die*, 6> d{};
		for (int i = 0; i < 6; i++) { int k = find(names[i]); if (k < 0) { fprintf(stderr, "unknown die: %s\n", names[i].c_str()); return 1; } d[i] = &all[k]; }
		Model m; Build(d, m);
		std::vector<double> curve;
		const double n = Turns(m, K, &curve);
		printf("%s\n  one turn E[min(points, %d)] = %.1f\n  expected turns to %d = %.3f\n", eval.c_str(), target, OneTurn(m, K), target, n);
		for (int r : { 1000, 2000, 3000, 4000, 6000, 8000 }) if (r <= target) printf("  turns to %d: %.3f\n", r, curve[r / STEP]);
		return 0;
	}

	// candidates: one die per distinct probability table (identical dice are interchangeable), with how many you may use
	std::vector<int> types; std::vector<int> cap;
	std::map<std::string, int> ownCount;
	for (auto& n : Split(own)) ownCount[n]++;
	for (size_t i = 0; i < all.size(); i++) {
		int dup = -1;
		for (size_t k = 0; k < types.size(); k++) {
			const Die& a = all[types[k]]; bool same = a.devil == all[i].devil;
			for (int f = 0; f < 6 && same; f++) same = std::fabs(a.p[f] - all[i].p[f]) < 1e-9;
			if (same) { dup = (int)k; break; }
		}
		const int have = own.empty() ? 6 : ownCount[all[i].name];
		if (dup >= 0) { cap[dup] = std::min(6, cap[dup] + have); continue; }
		types.push_back((int)i); cap.push_back(std::min(6, have));
	}
	{   // drop types you don't have
		std::vector<int> t2, c2;
		for (size_t k = 0; k < types.size(); k++) if (cap[k] > 0) { t2.push_back(types[k]); c2.push_back(cap[k]); }
		types = t2; cap = c2;
	}
	fprintf(stderr, "%zu dice, %zu different tables usable, target %d\n", all.size(), types.size(), target);

	// local search over multisets (sorted type lists), memoised, from several starts, one die swapped at a time
	std::map<std::vector<int>, double> seen;
	auto score = [&](const std::vector<int>& set) {
		auto it = seen.find(set);
		if (it != seen.end()) return it->second;
		std::array<const Die*, 6> d{}; for (int i = 0; i < 6; i++) d[i] = &all[types[set[i]]];
		Model m; Build(d, m);
		const double v = OneTurn(m, K);
		seen[set] = v;
		return v;
	};
	auto valid = [&](const std::vector<int>& set) {
		std::map<int, int> c; for (int x : set) if (++c[x] > cap[x]) return false;
		return true;
	};
	std::mt19937 rng(12345);
	std::vector<std::vector<int>> starts;
	{   // greedy start: the dice with the best one-die value (chance to score), plus random starts
		std::vector<int> st; for (size_t k = 0; k < types.size() && st.size() < 6; k++) for (int c = 0; c < cap[k] && st.size() < 6; c++) st.push_back((int)k);
		starts.push_back(st);
	}
	for (int s = 0; s < 2; s++) {
		std::vector<int> st;
		while (st.size() < 6) { int k = rng() % types.size(); std::vector<int> t = st; t.push_back(k); if (valid(t)) st = t; }
		starts.push_back(st);
	}
	const int wd = find("Weighted die");
	for (size_t k = 0; k < types.size(); k++) if (types[k] == wd) { std::vector<int> st(6, (int)std::min<size_t>(k, types.size())); std::vector<int> st2; for (int i = 0; i < std::min(6, cap[k]); i++) st2.push_back((int)k); while (st2.size() < 6) st2.push_back(starts[0][st2.size()]); if (valid(st2)) starts.push_back(st2); }
	for (auto st : starts) {
		std::sort(st.begin(), st.end());
		double cur = score(st);
		for (bool improved = true; improved;) {
			improved = false;
			for (int i = 0; i < 6 && !improved; i++)
				for (size_t k = 0; k < types.size(); k++) {
					std::vector<int> t = st; t[i] = (int)k; std::sort(t.begin(), t.end());
					if (t == st || !valid(t)) continue;
					const double v = score(t);
					if (v > cur + 1e-9) { st = t; cur = v; improved = true; break; }
				}
		}
		fprintf(stderr, "  start done: %.1f\n", cur);
	}
	std::vector<std::pair<double, std::vector<int>>> ranked;
	for (auto& kv : seen) ranked.push_back({ kv.second, kv.first });
	std::sort(ranked.rbegin(), ranked.rend());
	printf("best sets for reaching %d (exact expected turns; lower is better), searched %zu sets:\n", target, seen.size());
	for (int i = 0; i < top && i < (int)ranked.size(); i++) {
		std::array<const Die*, 6> d{}; for (int k = 0; k < 6; k++) d[k] = &all[types[ranked[i].second[k]]];
		Model m; Build(d, m);
		const double n = Turns(m, K);
		std::map<std::string, int> c; for (auto* x : d) c[x->name]++;
		printf("  %.3f turns  (one turn %.0f)  ", n, ranked[i].first);
		for (auto& kv : c) printf("%dx %s  ", kv.second, kv.first.c_str());
		printf("\n");
	}
	return 0;
}
