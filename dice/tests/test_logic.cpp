#include "dice_logic.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace diceadvisor;

void Check(bool value, const char* message) {
	if (!value) throw std::runtime_error(message);
}

Snapshot Fair() {
	Snapshot s;
	s.playerTurn = s.human = true;
	s.target = 4000;
	s.count[0] = s.count[1] = 6;
	for (int side = 0; side < 2; side++) for (int i = 0; i < 6; i++) {
		auto& d = s.dice[side][i];
		d.id = i;
		for (float& p : d.p) p = 1.0f / 6.0f;
		d.x = 10.0f * i;
		d.y = 20.0f * i;
	}
	return s;
}

int main(int argc, char** argv) {
	try {
		const std::filesystem::path cache = argc > 1 ? argv[1] : ".dice_logic_cache";
		std::filesystem::remove_all(cache);
		farkle::Die ordinary;
		ordinary.name = "Ordinary die";
		ordinary.p.fill(1.0 / 6.0);
		std::array<const farkle::Die*, 6> fair;
		fair.fill(&ordinary);
		auto solved = farkle::SolveCached(fair, fair, 4000, cache.string());
		const farkle::Game& game = solved->game;

		// A fresh throw: 1 1 1 2 3 4 -> the advice is the triple; the panel carries the numeric advice plus the game's
		// own selector ids for the three dice to set aside (100 + die index, as dice in the hand are 0..5).
		Snapshot keep = Fair();
		keep.banked[1] = 3950;
		keep.rolledMask = 0x3f;
		const int faces[6] = { 0, 0, 0, 1, 2, 3 };
		for (int i = 0; i < 6; i++) keep.dice[0][i].value = faces[i];
		farkle::Advice a = AdviseSnapshot(game, keep);
		Check(a.keep == 0x07 && !a.bank, "snapshot must select exactly the triple");
		farkle::Selection sel = SelectSnapshot(game, keep);
		Check(!sel.valid, "a fresh throw has no pick yet");
		Presentation p = Present(game, keep, a, sel);
		Check(p.keep == 0x07 && p.score == 1000 && !p.recommendBank, "advice keep and score");
		Check(p.selectorIds == std::vector<int>({ 100, 101, 102 }), "selector IDs must be 100 + die index");
		Check(p.throwPoints > 0 && p.hot > 0 && p.finish > 0 && p.bankPoints == 1000,
		      "advice finish / hot / average points / banked points");
		Check(!p.hasPick, "no pick before the player selects a die");
		std::cout << "[ok] fresh throw -> advice numbers and selector IDs\n";

		// The player's own held dice: three 1s kept, three ordinary dice left -> 1000 points, 27.8 % bust.
		Snapshot pick = Fair();
		pick.rolledMask = 0x3f;
		pick.heldMask = 0x07;
		for (int i = 0; i < 6; i++) {
			pick.dice[0][i].value = faces[i];
			pick.dice[0][i].state = i < 3 ? 1 : 0;
		}
		sel = SelectSnapshot(game, pick);
		Check(sel.valid, "the three held 1s must be a scoring selection");
		Check(sel.score == 1000 && sel.diceLeft == 3, "three held 1s must be a 1000 point pick with three dice left");
		Check(std::fabs(sel.bust - 60.0 / 216.0) < 1e-9, "three dice left must bust with 27.8 %");
		p = Present(game, pick, a, sel);
		Check(p.hasPick && p.pickScore == 1000 && p.pickDiceLeft == 3, "pick shown in the panel data");
		Check(std::fabs(p.pickBust - 60.0 / 216.0) < 1e-9, "pick bust chance");
		Check(p.pickWinBank >= 0 && p.pickWinBank <= 1 && p.pickWinRoll >= 0 && p.pickWinRoll <= 1,
		      "pick win chances must be probabilities");
		Check(p.pickFinish >= 0 && p.pickFinish <= 1 && p.pickHot >= 0 && p.pickHot <= 1,
		      "pick finish / hot chances must be probabilities");
		Check(p.selectorIds.empty(), "dice already held need no selector");
		std::cout << "[ok] player's own pick: score, dice left, bust, win and finish numbers\n";

		// Banking: 3950 + 100 in hand plus a scoring 1 banks for the game.
		Snapshot bank = Fair();
		bank.banked[0] = 3950;
		bank.turn[0] = 100;
		bank.rolledMask = 0x07;
		bank.dice[0][0].value = 0;
		bank.dice[0][1].value = 1;
		bank.dice[0][2].value = 2;
		a = AdviseSnapshot(game, bank);
		Check(a.keep == 1 && a.bank && a.winBank == 1.0, "winning bank");
		p = Present(game, bank, a, farkle::Selection{});
		Check(p.recommendBank && p.finish == 1.0, "a winning bank finishes the game now");
		std::cout << "[ok] bank recommendation finishes the turn\n";

		// Bust: no scoring dice at all -> no selectors, no score, and the opponent keeps their usual chance.
		farkle::Advice bust;
		bust.winBank = bust.winRoll = 0.375;
		p = Present(game, bank, bust, farkle::Selection{});
		Check(p.keep == 0 && p.score == 0 && p.selectorIds.empty(), "a bust has no keep and no selectors");
		Check(p.bankPoints == 100, "a bust banks the turn's points only");
		Check(p.opponentFinish >= 0 && p.opponentFinish <= 1, "opponent finish chance must be a probability");
		std::cout << "[ok] bust data and no selectors\n";

		// A live joker (value 6) maps to solver face 0; J, 2, 2 is a 200 point triple of 2s.
		Snapshot joker = Fair();
		joker.rolledMask = 0x07;
		joker.dice[0][0].joker = true;
		joker.dice[0][0].value = 6;
		joker.dice[0][1].value = joker.dice[0][2].value = 1;
		farkle::Die jokerDie = ordinary;
		jokerDie.name = "Joker die";
		jokerDie.joker = true;
		std::array<const farkle::Die*, 6> withJoker = fair;
		withJoker[0] = &jokerDie;
		auto jokerSolved = farkle::SolveCached(withJoker, fair, 4000, cache.string());
		a = AdviseSnapshot(jokerSolved->game, joker);
		Check(a.keep == 0x07, "live joker value 6 must map to solver face 0");
		p = Present(jokerSolved->game, joker, a, farkle::Selection{});
		Check(p.score == 200, "the joker completes the triple of 2s");
		std::cout << "[ok] live joker mapping\n";

		// Take fewer: whenever it is offered, the alternative must use more dice from the same roll, score with all of
		// them, and the advice's own value must not be lower.
		Check(!p.hasTakeFewer || (farkle::Popcount6(p.tfAllKeep) > farkle::Popcount6(p.tfKeep) &&
		                          p.tfAllScore > 0 && p.tfKeep == p.keep && p.tfScore == p.score),
		      "take-fewer fields must describe a larger keep of the same roll");
		std::cout << "[ok] take-fewer fields are consistent with the advice\n";

		// Two 1s, one on a weighted die (a 1 half the time): setting aside the plain 1 keeps the weighted die to throw,
		// and the panel's "why" names that die (swapDie) with the alternative's lower win chance.
		farkle::Die weighted = ordinary;
		weighted.name = "Weighted die";
		weighted.p = { 0.5, 0.1, 0.1, 0.1, 0.1, 0.1 };
		std::array<const farkle::Die*, 6> withWeighted = fair;
		withWeighted[0] = &weighted;
		auto weightedSolved = farkle::SolveCached(withWeighted, fair, 4000, cache.string());
		Snapshot two = Fair();
		two.rolledMask = 0x3f;
		for (int j = 0; j < 6; j++) two.dice[0][0].p[j] = j == 0 ? 0.5f : 0.1f;
		const int twoFaces[6] = { 0, 0, 1, 2, 3, 5 };
		for (int i = 0; i < 6; i++) two.dice[0][i].value = twoFaces[i];
		a = AdviseSnapshot(weightedSolved->game, two);
		p = Present(weightedSolved->game, two, a, farkle::Selection{});
		std::cout << "  two 1s: keep 0x" << std::hex << a.keep << std::dec << ", swap " << p.hasSwap << " die "
		          << p.swapDie << " win " << std::max(a.winBank, a.winRoll) << " vs " << p.swapWin << "\n";
		if (a.keep == 0x02) {
			Check(p.hasSwap && p.swapDie == 0 && p.swapWin <= std::max(a.winBank, a.winRoll),
			      "keeping the plain 1 must name the weighted die as the one kept in hand");
		} else {
			Check(a.keep != 0x01, "setting aside the weighted die's 1 instead of the plain 1 cannot be best");
		}
		std::cout << "[ok] which die stays in hand (two 1s, one weighted)\n";

		// Every die is named from its probabilities + joker, so different dice sets cannot share a disk cache.
		Snapshot other = Fair();
		other.dice[0][0].p[0] = 0.30f;
		for (int j = 1; j < 6; j++) other.dice[0][0].p[j] = 0.14f;
		std::array<farkle::Die, 6> n1, n2, n3;
		std::string error;
		Check(MakeDice(Fair(), 0, n1, error) && MakeDice(other, 0, n2, error), "both dice sets must build");
		Check(n1[0].name != n2[0].name, "different probabilities must name live dice differently");
		Check(MakeDice(Fair(), 0, n3, error) && n1[0].name == n3[0].name, "identical dice must keep the same name");
		std::cout << "[ok] live die names carry probabilities (and joker)\n";

		const std::string key = MatchupKey(joker);
		joker.dice[1][0].p[0] = 0.2f;
		Check(MatchupKey(joker) != key, "matchup cache key must include opponent weights");
		std::cout << "[ok] live matchup cache identity\n";
		std::filesystem::remove_all(cache);
		std::cout << "All dice plugin logic tests passed.\n";
		return 0;
	} catch (const std::exception& e) {
		std::cerr << "[fail] " << e.what() << '\n';
		return 1;
	}
}
