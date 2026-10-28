#include "advisor.h"
#include <cmath>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace farkle;

namespace {
void Check(bool ok, const std::string& message) {
	if (!ok) throw std::runtime_error(message);
}

int Best(Scorer& scorer, std::initializer_list<int> faces, int jokers = 0) {
	int counts[6] = {};
	for (int face : faces) counts[face - 1]++;
	return scorer.Best(counts, jokers);
}

void TestScorer() {
	Scorer scorer;
	struct Case { const char* name; std::initializer_list<int> faces; int jokers; int expected; };
	const Case cases[] = {
		{ "single 1", { 1 }, 0, 100 },
		{ "single 5", { 5 }, 0, 50 },
		{ "lone 2 cannot score", { 2 }, 0, -1 },
		{ "joker alone cannot score", {}, 1, -1 },
		{ "joker cannot join a single 1", { 1 }, 1, -1 },
		{ "three 1s", { 1, 1, 1 }, 0, 1000 },
		{ "three 2s", { 2, 2, 2 }, 0, 200 },
		{ "three 6s", { 6, 6, 6 }, 0, 600 },
		{ "four 1s", { 1, 1, 1, 1 }, 0, 2000 },
		{ "four 2s", { 2, 2, 2, 2 }, 0, 400 },
		{ "five 2s", { 2, 2, 2, 2, 2 }, 0, 800 },
		{ "six 2s", { 2, 2, 2, 2, 2, 2 }, 0, 1600 },
		{ "six 1s", { 1, 1, 1, 1, 1, 1 }, 0, 8000 },
		{ "straight 1-5", { 1, 2, 3, 4, 5 }, 0, 500 },
		{ "straight 2-6", { 2, 3, 4, 5, 6 }, 0, 750 },
		{ "straight 1-6", { 1, 2, 3, 4, 5, 6 }, 0, 1500 },
		{ "joker completes three 2s", { 2, 2 }, 1, 200 },
		{ "joker completes four 2s", { 2, 2, 2 }, 1, 400 },
		{ "joker completes three 1s", { 1, 1 }, 1, 1000 },
		{ "joker completes straight 1-5", { 1, 2, 3, 4 }, 1, 500 },
		{ "joker completes straight 2-6", { 2, 3, 4, 6 }, 1, 750 },
		{ "joker completes straight 1-6", { 1, 2, 3, 4, 6 }, 1, 1500 },
		{ "three 2s plus single 1", { 2, 2, 2, 1 }, 0, 300 },
		{ "three 1s plus three 2s", { 1, 1, 1, 2, 2, 2 }, 0, 1200 },
	};
	for (const auto& c : cases) {
		const int actual = Best(scorer, c.faces, c.jokers);
		Check(actual == c.expected, std::string(c.name) + ": expected " + std::to_string(c.expected) +
		      ", got " + std::to_string(actual));
	}
	std::cout << "[ok] Scorer::Best: " << std::size(cases) << " hand-derived cases\n";
}

struct CacheCleanup {
	std::filesystem::path path;
	~CacheCleanup() {
		std::error_code error;
		std::filesystem::remove_all(path, error);
	}
};
}  // namespace

int main(int argc, char** argv) {
	try {
		TestScorer();
		Die ordinary;
		ordinary.name = "Ordinary die";
		ordinary.p.fill(1.0 / 6.0);
		std::array<const Die*, 6> fair;
		fair.fill(&ordinary);
		const std::string cacheDir = argc > 1 ? argv[1] : ".test_advisor_cache";
		std::filesystem::remove_all(cacheDir);
		CacheCleanup cleanup{ cacheDir };

		auto solved = SolveCached(fair, fair, 4000, cacheDir);
		const Game& g = solved->game;
		const int bankFaces[6] = { 0, 1, 2, 3, 4, 5 };
		const Advice bank = Advise(g, 0, 3950, 0, 100, bankFaces, 0x07);
		Check(bank.keep == 0x01 && bank.bank && bank.winBank == 1.0,
		      "3950 points plus 100 in hand and a scoring 1 must bank");
		std::cout << "[ok] obvious bank\n";

		const int keepFaces[6] = { 0, 0, 0, 1, 2, 3 };
		const Advice keep = Advise(g, 0, 0, 3950, 0, keepFaces, 0x3f);
		Check(keep.keep == 0x07 && !keep.bank,
		      "1,1,1,2,3,4 must keep exactly the three 1s and roll; got mask " +
		      std::to_string(keep.keep) + ", bank " + std::to_string(keep.bank) +
		      ", bank value " + std::to_string(keep.winBank) + ", roll value " + std::to_string(keep.winRoll));
		std::cout << "[ok] obvious keep\n";

		const int bustFaces[6] = { 1, 2, 3, 1, 2, 3 };
		const Advice bust = Advise(g, 0, 0, 0, 0, bustFaces, 0x07);
		const double bustValue = 1.0 - g.Get(1, 0, 0);
		Check(bust.keep == 0 && !bust.bank && bust.winBank == bustValue && bust.winRoll == bustValue,
		      "an unscorable roll must return the bust value");
		std::cout << "[ok] bust advice\n";

		// hot dice: all of the dice left score on the next throw. One fair die: 1 or 5, two: 1/5 on both.
		const Model& m0 = *g.m[0];
		Check(std::fabs(HotDice(m0, 1) - 1.0 / 3.0) < 1e-12, "one die left must all-score with 1/3");
		Check(std::fabs(HotDice(m0, 2) - 4.0 / 36.0) < 1e-12, "two dice left must all-score with 4/36");
		Check(std::fabs(HotDice(m0, 3) - 12.0 / 216.0) < 1e-12,
		      "three dice left must all-score with 1/3^3 + four triples = 12/216");
		std::cout << "[ok] hot dice chance (1 die 1/3, 2 dice 4/36, 3 dice 12/216)\n";

		// average points of one throw of one fair die: 100/6 + 50/6 = 25
		Check(std::fabs(ExpectedThrowPoints(m0, 1) - 25.0) < 1e-12, "one die must average 25 points");
		std::cout << "[ok] average points of the next throw (one die 25)\n";

		// needing 50 with six ordinary dice: the turn finishes unless the throw busts (1440/46656)
		auto shortGame = SolveCached(fair, fair, 50, cacheDir);
		const double finish = FinishFromTurnStart(shortGame->game, 0, 0, 0);
		Check(std::fabs(finish - (1.0 - 1440.0 / 46656.0)) < 1e-9,
		      "finish this turn needing 50 must be 96.91 %; got " + std::to_string(finish));
		const double opponentFinish = FinishFromTurnStart(shortGame->game, 1, 0, 0);
		Check(std::fabs(opponentFinish - finish) < 1e-9, "both players finish 50 the same way");
		std::cout << "[ok] finish this turn / opponent next turn (50 -> 96.91 %)\n";

		// committed-choice cost: zero when the choice is as good as the advice, never negative
		Advice choice;
		choice.winBank = 0.60;
		choice.winRoll = 0.65;
		Check(std::fabs(MistakeCost(choice, 0.60) - 0.05) < 1e-12, "banking instead of the better roll must cost 5 %");
		Check(MistakeCost(choice, 0.65) == 0.0, "the advised choice must cost nothing");
		Check(MistakeCost(choice, 0.90) == 0.0, "a cost is never negative");
		std::cout << "[ok] committed-choice cost\n";

		// the player's own held selection: three 1s kept out of six fair dice -> 1000 points, three dice left (27.8 % bust)
		const int selectFaces[6] = { 0, 0, 0, 1, 2, 3 };
		const Selection sel = EvaluateSelection(g, 0, 0, 0, 0, selectFaces, 0x3f, 0x07);
		Check(sel.valid && sel.score == 1000 && sel.diceLeft == 3,
		      "three held 1s must score 1000 with three dice left");
		Check(std::fabs(sel.bust - 60.0 / 216.0) < 1e-9,
		      "three ordinary dice left must bust with 60/216 = 27.8 %; got " + std::to_string(sel.bust));
		const Selection none = EvaluateSelection(g, 0, 0, 0, 0, selectFaces, 0x3f, 0);
		Check(!none.valid, "an empty selection has no value");
		const Selection outside = EvaluateSelection(g, 0, 0, 0, 0, selectFaces, 0x07, 0x38);
		Check(!outside.valid, "kept dice must be part of the rolled hand");
		std::cout << "[ok] player's own pick: score, dice left and bust chance\n";

		// cache identity: live dice are named by their probabilities + joker, so different dice sets never share a file
		std::array<double, 6> fairP, loadedP;
		fairP.fill(1.0 / 6.0);
		loadedP = { 0.3, 0.14, 0.14, 0.14, 0.14, 0.14 };
		Die fairDie, loadedDie, jokerDie;
		fairDie.p = fairP;
		fairDie.name = DieKeyName(fairP, false);
		loadedDie.p = loadedP;
		loadedDie.name = DieKeyName(loadedP, false);
		jokerDie.p = fairP;
		jokerDie.joker = true;
		jokerDie.name = DieKeyName(fairP, true);
		std::array<const Die*, 6> fairSet, loadedSet, jokerSet, fairCopy;
		fairSet.fill(&fairDie);
		fairCopy.fill(&fairDie);
		loadedSet.fill(&loadedDie);
		jokerSet.fill(&jokerDie);
		const auto fairPath = advisor_detail::CachePath(cacheDir, fairSet, fairSet, 4000);
		const auto loadedPath = advisor_detail::CachePath(cacheDir, loadedSet, fairSet, 4000);
		const auto jokerPath = advisor_detail::CachePath(cacheDir, jokerSet, fairSet, 4000);
		Check(fairPath != loadedPath, "different live dice shared one cache file");
		Check(fairPath != jokerPath, "joker and ordinary dice shared one cache file");
		Check(fairPath == advisor_detail::CachePath(cacheDir, fairCopy, fairCopy, 4000),
		      "identical live dice must share one cache file");
		Check(DieKeyName(fairP, false) != DieKeyName(fairP, true), "the joker flag must be part of a live die's name");
		std::cout << "[ok] live dice cache identity (probabilities + joker)\n";

		auto loaded = SolveCached(fair, fair, 4000, cacheDir);
		Check(loaded->game.turns.load() == 0, "second SolveCached call solved instead of loading");
		Check(loaded->game.W[0] == g.W[0] && loaded->game.W[1] == g.W[1],
		      "cached W differs from solved W");
		std::cout << "[ok] cache reload gives identical W without solving\n";

		auto symmetric = SolveCached(fair, fair, 2000, cacheDir);
		const double first = symmetric->game.Get(0, 0, 0);
		const double second = symmetric->game.Get(1, 0, 0);
		Check(std::fabs(first - second) <= 1e-6,
		      "fair-game symmetry failed: starting win chances are " + std::to_string(first) +
		      " and " + std::to_string(second));
		std::cout << "[ok] fair-game symmetry\n";
		std::cout << "All advisor tests passed.\n";
		return 0;
	} catch (const std::exception& e) {
		std::cerr << "[fail] " << e.what() << '\n';
		return 1;
	}
}
