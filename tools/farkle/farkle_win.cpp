// Offline Farkle queries. Two dice sets come from dice_game.csv; every other flag is optional:
//   farkle_win dice_game.csv --me "A;...;F" --opp "A;...;F" [--target 4000] [--threads 4]
//   --faces "1,1,1,2,3,4" [--keep "1,2,3"] [--pos "my,opp,turn"]   advice + the player's own selection
//   --finish "my,opp"            chance to reach the target before this turn ends (from the turn start, six dice)
//   --opp-finish "my,opp"        same for the opponent's next turn, with their dice
//   --hot N                      chance that N dice left all score on the next throw (hot dice)
//   --expected N                 average points of the next throw with N dice left
#include "advisor.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
using namespace farkle;

std::vector<Die> Load(const char* path) {
	// dice_game.csv (from the game's item.xml): name, id, key, p1_pct..p6_pct, weights, sum, joker_face, ...
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
		d.joker = jcol >= 0 ? (jcol < (int)cells.size() && cells[jcol] == "1")
		                    : d.name.find("Devil") != std::string::npos || d.name.find("Tengri") != std::string::npos || d.name.find("Balatro") != std::string::npos;
		out.push_back(d);
	}
	return out;
}

bool Pick(const std::vector<Die>& all, const std::string& s, std::array<const Die*, 6>& d) {
	std::stringstream ss(s); std::string x; int n = 0;
	while (std::getline(ss, x, ';')) {
		const Die* f = nullptr; for (auto& a : all) if (a.name == x) f = &a;
		if (!f || n >= 6) { fprintf(stderr, "bad die: %s\n", x.c_str()); return false; }
		d[n++] = f;
	}
	return n == 6;
}

int ParseFaces(const std::string& s, int faces[6]) {
	std::stringstream ss(s); std::string x; int n = 0;
	while (std::getline(ss, x, ',')) {
		if (n >= 6) return -1;
		if (x == "J" || x == "j" || x == "joker" || x == "Joker") faces[n] = 0;
		else {
			char* e = nullptr; const long v = strtol(x.c_str(), &e, 10);
			if (!e || *e || v < 1 || v > 6) return -1;
			faces[n] = (int)v - 1;
		}
		n++;
	}
	return n == 6 ? 0 : -1;
}

int ParseKeep(const std::string& s) {
	std::stringstream ss(s); std::string x; int mask = 0;
	while (std::getline(ss, x, ',')) {
		char* e = nullptr; const long v = strtol(x.c_str(), &e, 10);
		if (!e || *e || v < 1 || v > 6) return -1;
		mask |= 1 << (v - 1);
	}
	return mask;
}

bool ParsePos(const std::string& s, int& my, int& opp, int& turn) {
	std::stringstream ss(s); std::string x; int v[3] = {}, n = 0;
	while (std::getline(ss, x, ',')) {
		if (n >= 3) return false;
		char* e = nullptr; const long got = strtol(x.c_str(), &e, 10);
		if (!e || *e) return false;
		v[n++] = (int)got;
	}
	if (!n) return false;
	my = v[0];
	opp = v[1];
	turn = v[2];
	return true;
}

// The die's face 0 is "1" on an ordinary die and the joker on a joker die (farkle_core.h's scorer), so the models are
// needed here to print the right symbol.
std::string FacesText(const std::array<const Die*, 6>& dice, const int* faces, int mask) {
	std::string out;
	for (int i = 0; i < 6; i++) if (mask >> i & 1) {
		if (!out.empty()) out += " ";
		out += dice[i]->joker && faces[i] == 0 ? "J" : std::to_string(faces[i] + 1);
	}
	return out;
}

int StateForFirst(const Model& md, const std::array<const Die*, 6>& dice, int n) {
	int state = 0;
	for (int i = 0; i < n; i++) state += md.mul[KindOf(md, i)];
	return state;
}

int StateAfterKeep(const Model& md, int rolledMask, int keep) {
	const int left = rolledMask & ~keep;
	if (!left) return md.full;
	int state = 0;
	for (int i = 0; i < 6; i++) if (left >> i & 1) state += md.mul[KindOf(md, i)];
	return state;
}

void Usage() {
	fprintf(stderr, "usage: farkle_win <dice_game.csv> --me \"A;..;F\" [--opp \"A;..;F\"] [--target 4000] [--threads 4]\n"
	                "       [--faces \"1,1,1,2,3,4\" [--keep \"1,2,3\"] [--pos \"my,opp,turn\"]]\n"
	                "       [--finish \"my,opp\"] [--opp-finish \"my,opp\"] [--hot N] [--expected N]\n");
}

int main(int argc, char** argv) {
	if (argc < 2) { Usage(); return 2; }
	std::string me, opp, facesArg, keepArg, posArg, finishArg, oppFinishArg;
	int target = 4000, threads = 4, hotN = -1, expectedN = -1;
	for (int i = 2; i < argc; i++) {
		const std::string key = argv[i];
		auto value = [&]() -> std::string {
			if (i + 1 >= argc) { fprintf(stderr, "missing value for %s\n", key.c_str()); exit(2); }
			return argv[++i];
		};
		if (key == "--me") me = value();
		else if (key == "--opp") opp = value();
		else if (key == "--target") target = atoi(value().c_str());
		else if (key == "--threads") threads = atoi(value().c_str());
		else if (key == "--faces") facesArg = value();
		else if (key == "--keep") keepArg = value();
		else if (key == "--pos") posArg = value();
		else if (key == "--finish") finishArg = value();
		else if (key == "--opp-finish") oppFinishArg = value();
		else if (key == "--hot") hotN = atoi(value().c_str());
		else if (key == "--expected") expectedN = atoi(value().c_str());
		else if (key == "--help" || key == "-h") { Usage(); return 0; }
		else { fprintf(stderr, "unknown option %s\n", key.c_str()); Usage(); return 2; }
	}
	if (me.empty()) { Usage(); return 2; }
	if (opp.empty()) opp = me;
	if (target <= 0 || target % STEP) { fprintf(stderr, "target must be a positive multiple of %d\n", STEP); return 2; }

	auto all = Load(argv[1]);
	if (all.empty()) { fprintf(stderr, "no dice in %s\n", argv[1]); return 2; }
	std::array<const Die*, 6> dm{}, dop{};
	if (!Pick(all, me, dm) || !Pick(all, opp, dop)) return 1;

	int faces[6] = {};
	if (!facesArg.empty() && ParseFaces(facesArg, faces)) { fprintf(stderr, "bad --faces (six values 1..6 or J)\n"); return 2; }
	int keep = 0;
	if (!keepArg.empty()) {
		keep = ParseKeep(keepArg);
		if (keep <= 0) { fprintf(stderr, "bad --keep (positions 1..6)\n"); return 2; }
		if (facesArg.empty()) { fprintf(stderr, "--keep needs --faces\n"); return 2; }
	}
	int my = 0, oppScore = 0, turn = 0;
	if (!posArg.empty() && !ParsePos(posArg, my, oppScore, turn)) { fprintf(stderr, "bad --pos (my,opp,turn)\n"); return 2; }
	if (hotN > 6 || expectedN > 6) { fprintf(stderr, "N must be 1..6\n"); return 2; }

	Scorer sc;
	Model m0, m1;
	Build(dm, sc, m0);
	Build(dop, sc, m1);
	const bool defaultQuery = facesArg.empty() && finishArg.empty() && oppFinishArg.empty() &&
	                          hotN < 0 && expectedN < 0;
	const bool needSolve = !facesArg.empty() || !finishArg.empty() || !oppFinishArg.empty() || defaultQuery;
	Game g;
	if (needSolve) {
		auto t0 = std::chrono::steady_clock::now();
		g.K = target / STEP;
		g.m[0] = &m0;
		g.m[1] = &m1;
		g.Solve(threads);
		auto t1 = std::chrono::steady_clock::now();
		printf("solve %.1f s\n", std::chrono::duration<double>(t1 - t0).count());
	}

	if (!facesArg.empty()) {
		Advice a = Advise(g, 0, my, oppScore, turn, faces, 0x3f);
		const int score = a.keep ? Scorer().Score(m0.dice, faces, a.keep) : 0;
		if (!a.keep) printf("advice: no scoring dice (bust), win after bust %.2f %%\n", a.winRoll * 100.0);
		else printf("advice: keep %s = %d, throw %d dice\n", FacesText(m0.dice, faces, a.keep).c_str(), score,
		            6 - Popcount6(a.keep));
		printf("win: throw again %.2f %%   bank %.2f %%\n", a.winRoll * 100.0, a.winBank * 100.0);
		if (a.keep) {
			const int left = StateAfterKeep(m0, 0x3f, a.keep);
			const TurnTables tt = TurnTablesFor(g, 0, my / STEP, oppScore / STEP);
			printf("finish this turn: %.2f %%\n", FinishFromSelection(tt, g, 0, turn, score, left, a.bank) * 100.0);
			printf("hot dice: %.2f %%\n", HotDice(m0, left) * 100.0);
			printf("average points of the next throw: %.2f\n", ExpectedThrowPoints(m0, left));
			int allKeep = 0, allCount = 0, allScore = 0;
			Scorer counter;
			for (int candidate = 0x3f; candidate; candidate = (candidate - 1) & 0x3f) {
				const int s = counter.Score(m0.dice, faces, candidate);
				if (s <= 0) continue;
				const int n = Popcount6(candidate);
				if (n > allCount || (n == allCount && s > allScore)) { allCount = n; allKeep = candidate; allScore = s; }
			}
			if (allKeep && allCount > Popcount6(a.keep)) {
				const Selection alt = EvaluateSelection(g, 0, my, oppScore, turn, faces, 0x3f, allKeep);
				if (alt.valid)
					printf("take fewer: keep %s, throw %d (%.2f %%) beats %s, throw %d (%.2f %%)\n",
					       FacesText(m0.dice, faces, a.keep).c_str(), 6 - Popcount6(a.keep), std::max(a.winBank, a.winRoll) * 100.0,
					       FacesText(m0.dice, faces, allKeep).c_str(), 6 - allCount, std::max(alt.winBank, alt.winRoll) * 100.0);
			}
		}
		printf("opponent finishes next turn: %.2f %%\n", FinishFromTurnStart(g, 1, oppScore, my) * 100.0);
		if (keep) {
			const Selection sel = EvaluateSelection(g, 0, my, oppScore, turn, faces, 0x3f, keep);
			if (!sel.valid) printf("your pick: not a scoring selection\n");
			else {
				printf("your pick: %s = %d, %d dice left, bust %.2f %%, throw again %.2f %%, bank %.2f %%\n",
				       FacesText(m0.dice, faces, keep).c_str(), sel.score, sel.diceLeft, sel.bust * 100.0,
				       sel.winRoll * 100.0, sel.winBank * 100.0);
				printf("mistake cost: %.2f %%\n",
				       MistakeCost(a, std::max(sel.winBank, sel.winRoll)) * 100.0);
			}
		}
	}

	if (!finishArg.empty()) {
		int fmy = 0, fopp = 0, fturn = 0;
		if (!ParsePos(finishArg, fmy, fopp, fturn)) { fprintf(stderr, "bad --finish (my,opp)\n"); return 2; }
		printf("finish this turn from the turn start: %.2f %%\n", FinishFromTurnStart(g, 0, fmy, fopp) * 100.0);
	}
	if (!oppFinishArg.empty()) {
		int fmy = 0, fopp = 0, fturn = 0;
		if (!ParsePos(oppFinishArg, fmy, fopp, fturn)) { fprintf(stderr, "bad --opp-finish (my,opp)\n"); return 2; }
		printf("opponent finishes next turn: %.2f %%\n", FinishFromTurnStart(g, 1, fopp, fmy) * 100.0);
	}
	if (hotN >= 1) {
		const int state = StateForFirst(m0, dm, hotN);
		printf("hot dice (%d dice): %.2f %%\n", hotN, HotDice(m0, state) * 100.0);
	}
	if (expectedN >= 1) {
		const int state = StateForFirst(m0, dm, expectedN);
		printf("expected points (%d dice): %.2f\n", expectedN, ExpectedThrowPoints(m0, state));
	}
	if (defaultQuery) printf("I start: win %.4f   opponent starts: win %.4f\n", g.Get(0, 0, 0), 1.0 - g.Get(1, 0, 0));
	return 0;
}
