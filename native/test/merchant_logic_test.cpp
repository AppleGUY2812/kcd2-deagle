#include "merchant_logic.h"
#include <cassert>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>

using namespace merchantresale;

static State Kuttenberg() {
	State s;
	s.markets = ClusterMarkets({{1, "Kuttenberg armourer", {0, 0, 0}},
	                           {2, "Kuttenberg smith", {299, 0, 0}},
	                           {3, "Trosky", {1000, 0, 0}}});
	s.wealth[1].preset = 800;
	return s;
}

int main() {
	std::cout << std::fixed << std::setprecision(6);
	// Wrong distance boundary, missing transitive join, or input-order-dependent IDs.
	auto markets = ClusterMarkets({{4, "C", {600, 0, 0}}, {2, "A", {0, 0, 0}},
	                              {3, "B", {300, 0, 0}}, {9, "D", {901, 0, 0}}});
	assert(markets.size() == 2 && markets[0].id == 2 && markets[0].merchants.size() == 3);
	assert(markets[1].id == 9);
	std::cout << "markets=" << markets.size() << " connected_merchants=" << markets[0].merchants.size() << '\n';

	// First item must use fresh demand, not its own post-sale demand.
	State fresh = Kuttenberg();
	AddSale(fresh, 1, "plate-guid", "plate-1", Category::Armor, 1, 1200, 400, 0);
	const double first = fresh.ledger.front().eta;
	assert(first >= 2.1 && first <= 3.9);
	assert(Settle(fresh, 1, first - 0.00001).rows.empty());
	auto sold = Settle(fresh, 1, first);
	assert(sold.rows.size() == 1 && sold.coinEarned == 1200 && fresh.ledger.empty());
	assert(Settle(fresh, 1, 100).coinEarned == 0);
	std::cout << "fresh_plate_eta_days=" << first << " resale_coin=" << sold.coinEarned << '\n';

	// A stack must depress demand per unit; nearby merchants share it, distant ones do not.
	State flood = Kuttenberg();
	AddSale(flood, 1, "plate-guid", "thirty-plates", Category::Armor, 30, 1200, 400, 1);
	const double floor = DemandAt(flood, 1, Category::Armor, 1);
	const double last = flood.ledger.back().eta - 1;
	assert(std::abs(floor - 0.15) < 1e-12 && last >= 15);
	assert(DemandAt(flood, 2, Category::Armor, 1) == floor);
	assert(DemandAt(flood, 3, Category::Armor, 1) == 1);
	assert(DemandAt(flood, 1, Category::Food, 1) == 1);
	const double recovered = DemandAt(flood, 1, Category::Armor, 15);
	assert(recovered > 0.9 && recovered < 1);
	assert(std::abs(DemandAt(flood, 1, Category::Armor, 5) - 0.575) < 1e-12);
	std::cout << "flood_count=" << flood.ledger.size() << " armor_demand=" << floor
	          << " last_plate_eta_days=" << last << " demand_after_14_days=" << recovered << '\n';

	// Buyback cannot create resale coin; partial buybacks must leave the other units.
	State buyback = Kuttenberg();
	AddSale(buyback, 1, "plate-guid", "buyback", Category::Armor, 3, 1200, 400, 0);
	assert(BuyBack(buyback, 1, "buyback", 2) == 2 && buyback.ledger.size() == 1);
	assert(BuyBack(buyback, 2, "buyback", 1) == 0);
	assert(BuyBack(buyback, 1, "buyback", 2) == 1);
	const auto bought = Settle(buyback, 1, 100);
	assert(bought.coinEarned == 0 && bought.rows.empty());
	std::cout << "buyback_rows_left=" << buyback.ledger.size() << " buyback_coin=" << bought.coinEarned << '\n';

	// Reset restoration, repeated opens, intervening trades, spending, and the 10x ceiling.
	Wealth w;
	w.preset = 800;
	CloseShop(w, 2000);
	auto reset = OpenShop(w, 800);
	assert(reset.resetDetected && reset.coinAdded == 1200 && reset.purse == 2000);
	assert(OpenShop(w, 800).coinAdded == 0); // no second credit without a close
	CloseShop(w, 2000);
	NoteTrade(w);
	assert(OpenShop(w, 700).coinAdded == 0);
	CloseShop(w, 500);
	assert(w.bonus == 0);
	CloseShop(w, 20000);
	auto cap = OpenShop(w, 800);
	assert(cap.purse == 8000 && cap.coinAdded == 7200 && w.bonus == 7200);
	CloseShop(w, 8000);
	assert(OpenShop(w, 8000).coinAdded == 0);
	Wealth zero;
	CloseShop(zero, 100);
	assert(OpenShop(zero, 0).purse == 0);
	std::cout << "restock_bonus=" << reset.coinAdded << " restock_purse=" << reset.purse
	          << " capped_purse=" << cap.purse << " capped_bonus=" << w.bonus << '\n';

	// Settlement belongs to only the requested shop; wealth records the actual capped purse.
	State scoped = Kuttenberg();
	AddSale(scoped, 1, "food", "apple", Category::Food, 1, 20, 5, 0);
	AddSale(scoped, 2, "food", "pear", Category::Food, 1, 30, 7, 0);
	assert(Settle(scoped, 1, 10).coinEarned == 20 && scoped.ledger.size() == 1);
	assert(scoped.ledger[0].merchant == 2);
	auto credited = CreditResale(scoped.wealth[1], 7990, 100);
	assert(credited.coinAdded == 10 && credited.purse == 8000 && scoped.wealth[1].bonus == 7200);
	std::cout << "settlement_cap_added=" << credited.coinAdded << " settlement_cap_purse=" << credited.purse << '\n';

	// RNG cannot depend on std::hash, platform RNG state, visits, or deserialization.
	State same = Kuttenberg();
	AddSale(same, 1, "plate-guid", "thirty-plates", Category::Armor, 30, 1200, 400, 1);
	for (std::size_t i = 0; i < flood.ledger.size(); ++i) assert(flood.ledger[i].eta == same.ledger[i].eta);
	flood.markets[0].merchants[0].name = "Smith \"A\"\nshop";
	flood.wealth[1] = w;
	State loaded;
	std::string error;
	const std::string wire = Serialize(flood);
	assert(Deserialize(wire, loaded, error) && Serialize(loaded) == wire);
	assert(Settle(loaded, 1, 100).coinEarned == 36000);
	assert(loaded.ledger.empty());
	const std::string before = Serialize(same);
	assert(!Deserialize(wire.substr(0, wire.size() / 2), same, error));
	assert(Serialize(same) == before); // failure is atomic
	assert(!Deserialize(wire + " garbage", same, error));
	assert(!Deserialize("MRESALE 999", same, error));
	assert(!Deserialize("MRESALE 1 999999999999", same, error));
	std::cout << "roundtrip_equal=1 serialized_bytes=" << wire.size() << " deterministic_etas=" << flood.ledger.size()
	          << " restored_resale_coin=36000 malformed_rejected=4\n";

	// Reject nonsensical input before mutation; a reversed clock never recovers demand.
	const std::string untouched = Serialize(fresh);
	bool rejected = false;
	try { AddSale(fresh, 1, "x", "x", Category::Armor, 1, -1, 0, 0); }
	catch (const std::invalid_argument&) { rejected = true; }
	assert(rejected && Serialize(fresh) == untouched);
	rejected = false;
	try { AddSale(fresh, 1, "x", "x", Category::Armor, 1, 1, 0, std::numeric_limits<double>::infinity()); }
	catch (const std::invalid_argument&) { rejected = true; }
	assert(rejected && Serialize(fresh) == untouched);
	assert(DemandAt(flood, 1, Category::Armor, 0) == floor);
	std::cout << "invalid_sales_rejected=2\nALL MERCHANT LOGIC TESTS PASS\n";
}
