// Win chance for two dice sets: farkle_win <dice.csv> --me "A;B;C;D;E;F" --opp "A;...;F" [--target 4000]
#include "farkle_core.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
using namespace farkle;

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
int main(int argc, char** argv) {
	std::string me, opp; int target = 4000, threads = 4;
	for (int i = 2; i + 1 < argc; i += 2) {
		if (!strcmp(argv[i], "--me")) me = argv[i + 1];
		else if (!strcmp(argv[i], "--opp")) opp = argv[i + 1];
		else if (!strcmp(argv[i], "--target")) target = atoi(argv[i + 1]);
		else if (!strcmp(argv[i], "--threads")) threads = atoi(argv[i + 1]);
	}
	auto all = Load(argv[1]);
	std::array<const Die*, 6> dm{}, dop{};
	if (!Pick(all, me, dm) || !Pick(all, opp, dop)) return 1;
	Scorer sc; Model m0, m1;
	auto t0 = std::chrono::steady_clock::now();
	Build(dm, sc, m0); Build(dop, sc, m1);
	size_t cls = 0; for (int S = 1; S < m0.N; S++) cls += m0.rolls[S].size();
	auto t1 = std::chrono::steady_clock::now();
	Game g; g.K = target / STEP; g.m[0] = &m0; g.m[1] = &m1;
	g.Solve(threads);
	auto t2 = std::chrono::steady_clock::now();
	printf("build %.1f s (%zu roll classes), solve %.1f s, %ld turn passes\n", std::chrono::duration<double>(t1 - t0).count(), cls, std::chrono::duration<double>(t2 - t1).count(), (long)g.turns);
	printf("I start: win %.4f   opponent starts: win %.4f\n", g.Get(0, 0, 0), 1.0 - g.Get(1, 0, 0));
}
