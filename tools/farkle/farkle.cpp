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
#include "bestset.h"
using namespace bestset;

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

	std::map<std::string, int> ownCount;
	for (auto& n : Split(own)) ownCount[n]++;
	std::vector<Owned> mine;
	for (auto& d : all) { const int have = own.empty() ? 6 : ownCount[d.name]; if (have > 0) mine.push_back(Owned{ d.name, d.p, d.devil, have }); }
	auto picks = BestSets(mine, target, top);
	printf("best sets for reaching %d (exact expected turns; lower is better):\n", target);
	for (auto& pk : picks) {
		std::array<const Die*, 6> d{}; int n6 = 0;
		std::vector<Die> keep;
		for (size_t i = 0; i < mine.size(); i++) for (int c = 0; c < pk.count[i]; c++) keep.push_back(Die{ mine[i].name, mine[i].p, mine[i].devil });
		for (auto& x : keep) d[n6++] = &x;
		Model m; Build(d, m);
		const double n = Turns(m, K);
		printf("  %.3f turns  (one turn %.0f)  ", n, pk.oneTurn);
		for (size_t i = 0; i < mine.size(); i++) if (pk.count[i]) printf("%dx %s  ", pk.count[i], mine[i].name.c_str());
		printf("\n");
	}
	return 0;
}
