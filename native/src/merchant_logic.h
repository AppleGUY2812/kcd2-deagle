#pragma once
// Standalone model only: no game calls. Time is absolute GAME DAYS; money is Groschen.
// Phase 1 does not instantiate this model in the plugin.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace merchantresale {

enum class Category { Armor, Weapon, Food, Alchemy, Misc };
constexpr std::size_t kMaxRecords = 100000;
inline bool ValidCategory(Category c) { return static_cast<unsigned>(c) < 5; }
inline bool Nonnegative(double n) { return std::isfinite(n) && n >= 0; }
inline void Require(bool good) { if (!good) throw std::invalid_argument("invalid merchant resale input"); }
struct Position { double x = 0, y = 0, z = 0; };
struct Merchant { std::uint64_t id = 0; std::string name; Position position; };
struct Market { std::uint64_t id = 0; std::vector<Merchant> merchants; };

// Single-link connected components (distance <=300m, including bridges). Stable minimum
// merchant ID is the market ID, independent of enumeration order. Caller supplies a region's
// fixed merchant roster before recording sales; moving NPCs must not re-cluster live ledgers.
inline std::vector<Market> ClusterMarkets(std::vector<Merchant> merchants) {
	Require(merchants.size() <= kMaxRecords);
	std::sort(merchants.begin(), merchants.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
	std::vector<std::size_t> parent(merchants.size());
	for (std::size_t i = 0; i < merchants.size(); ++i) {
		const auto& m = merchants[i];
		Require(m.id && (!i || m.id != merchants[i - 1].id) && std::isfinite(m.position.x) &&
		        std::isfinite(m.position.y) && std::isfinite(m.position.z));
		parent[i] = i;
	}
	auto root = [&](std::size_t i) { while (parent[i] != i) { parent[i] = parent[parent[i]]; i = parent[i]; } return i; };
	for (std::size_t i = 0; i < merchants.size(); ++i) for (std::size_t j = 0; j < i; ++j) {
		const auto& a = merchants[i].position; const auto& b = merchants[j].position;
		if (std::hypot(a.x - b.x, a.y - b.y, a.z - b.z) <= 300) {
			const auto ri = root(i), rj = root(j);
			parent[std::max(ri, rj)] = std::min(ri, rj);
		}
	}
	std::map<std::size_t, Market> grouped;
	for (std::size_t i = 0; i < merchants.size(); ++i) {
		const auto r = root(i);
		auto& m = grouped[r]; m.id = merchants[r].id; m.merchants.push_back(merchants[i]);
	}
	std::vector<Market> result;
	for (auto& entry : grouped) result.push_back(std::move(entry.second));
	return result;
}

struct Demand { double value = 1, updated = 0; };
struct LedgerRow {
	std::uint64_t merchant = 0;
	std::string classGuid, itemId;
	Category category = Category::Misc;
	std::uint32_t count = 1, ordinal = 0;
	double baseValue = 0, pricePaid = 0, soldAt = 0, eta = 0;
};
struct Wealth {
	double preset = 0, bonus = 0, lastClose = 0;
	bool hasClose = false, tradeSinceClose = false;
};
struct PurseResult { double purse = 0, coinAdded = 0; bool resetDetected = false; };
struct State {
	std::vector<Market> markets;
	std::map<std::pair<std::uint64_t, Category>, Demand> demand;
	std::vector<LedgerRow> ledger;
	std::map<std::uint64_t, Wealth> wealth;
};
inline std::uint64_t MarketOf(const State& s, std::uint64_t merchant) {
	for (const auto& market : s.markets) for (const auto& m : market.merchants)
		if (m.id == merchant) return market.id;
	throw std::invalid_argument("merchant missing from fixed market roster");
}
inline double Recover(const Demand& d, double now) {
	Require(Nonnegative(now));
	return std::clamp(1 - (1 - d.value) * std::exp2(-std::max(0.0, now - d.updated) / 4), 0.15, 1.0);
}
inline double DemandAt(const State& s, std::uint64_t merchant, Category category, double now) {
	Require(ValidCategory(category) && Nonnegative(now));
	const auto it = s.demand.find({MarketOf(s, merchant), category});
	return it == s.demand.end() ? 1 : Recover(it->second, now);
}

// Fixed FNV-1a + SplitMix64, not implementation-defined std::hash / random distributions.
inline double ResaleEta(Category category, double demand, const std::string& itemId, std::uint32_t ordinal = 0) {
	Require(ValidCategory(category) && std::isfinite(demand) && demand >= 0.15 && demand <= 1);
	std::uint64_t seed = UINT64_C(14695981039346656037);
	for (unsigned char c : itemId) { seed ^= c; seed *= UINT64_C(1099511628211); }
	seed += UINT64_C(0x9e3779b97f4a7c15) * (static_cast<std::uint64_t>(ordinal) + 1);
	seed = (seed ^ (seed >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
	seed = (seed ^ (seed >> 27)) * UINT64_C(0x94d049bb133111eb);
	seed ^= seed >> 31;
	const double uniform = static_cast<double>(seed >> 11) / 9007199254740992.0;
	constexpr double days[] = {3, 3, 1, 2, 2};
	return days[static_cast<unsigned>(category)] / demand * (0.7 + 0.6 * uniform);
}
inline void AddSale(State& s, std::uint64_t merchant, const std::string& classGuid, const std::string& itemId,
                    Category category, std::uint32_t count, double baseValue, double pricePaid, double now) {
	Require(ValidCategory(category) && !classGuid.empty() && !itemId.empty() && count &&
	        count <= kMaxRecords && s.ledger.size() <= kMaxRecords - count && Nonnegative(baseValue) &&
	        Nonnegative(pricePaid) && Nonnegative(now));
	const auto key = std::make_pair(MarketOf(s, merchant), category);
	Demand d;
	const auto found = s.demand.find(key);
	if (found != s.demand.end()) { d = found->second; Require(now >= d.updated); }
	d.value = Recover(d, now); d.updated = now;
	constexpr double capacity[] = {6000, 4000, 600, 1500, 2000};
	// One row per unit: even a single checkout stack saturates the market progressively.
	// pricePaid is per unit; the caller must allocate a negotiated basket total externally.
	std::vector<LedgerRow> rows;
	rows.reserve(count);
	for (std::uint32_t i = 0; i < count; ++i) {
		const double eta = now + ResaleEta(category, d.value, itemId, i);
		Require(std::isfinite(eta));
		rows.push_back({merchant, classGuid, itemId, category, 1, i, baseValue, pricePaid, now, eta});
		d.value = std::max(0.15, d.value - baseValue / capacity[static_cast<unsigned>(category)]);
	}
	s.ledger.insert(s.ledger.end(), rows.begin(), rows.end());
	s.demand[key] = d;
}
inline std::uint32_t BuyBack(State& s, std::uint64_t merchant, const std::string& itemId, std::uint32_t count) {
	std::uint32_t removed = 0;
	for (auto it = s.ledger.begin(); it != s.ledger.end() && removed < count;) {
		if (it->merchant != merchant || it->itemId != itemId) { ++it; continue; }
		const auto n = std::min(it->count, count - removed);
		removed += n; it->count -= n;
		if (!it->count) it = s.ledger.erase(it); else ++it;
	}
	return removed;
}
struct Settlement { std::vector<LedgerRow> rows; double coinEarned = 0; };
inline Settlement Settle(State& s, std::uint64_t merchant, double now) {
	Require(Nonnegative(now));
	Settlement result;
	for (const auto& row : s.ledger) if (row.merchant == merchant && row.eta <= now) {
		result.rows.push_back(row);
		result.coinEarned += row.baseValue * row.count;
		Require(std::isfinite(result.coinEarned));
	}
	s.ledger.erase(std::remove_if(s.ledger.begin(), s.ledger.end(), [&](const auto& row) {
		return row.merchant == merchant && row.eta <= now;
	}), s.ledger.end());
	return result; // caller must reconcile actual stock before applying; never holds C_Item*.
}
inline double Cap(const Wealth& w) {
	Require(Nonnegative(w.preset) && w.preset <= std::numeric_limits<double>::max() / 10);
	return w.preset * 10;
}
inline void CloseShop(Wealth& w, double purse) {
	Require(Nonnegative(purse));
	w.bonus = std::clamp(purse - w.preset, 0.0, Cap(w) - w.preset);
	w.lastClose = purse; w.hasClose = true; w.tradeSinceClose = false;
}
inline void NoteTrade(Wealth& w) { w.tradeSinceClose = true; }
inline PurseResult OpenShop(Wealth& w, double purse) {
	Require(Nonnegative(purse) && Nonnegative(w.bonus));
	const double cap = Cap(w);
	const bool reset = w.hasClose && !w.tradeSinceClose && purse != w.lastClose;
	const double added = reset ? std::min(w.bonus, std::max(0.0, cap - purse)) : 0;
	// Only added coins are capped; never delete vanilla money from a read of an over-cap purse.
	const double total = purse + added;
	w.bonus = std::clamp(total - w.preset, 0.0, cap - w.preset);
	w.hasClose = false; w.tradeSinceClose = false;
	return {total, added, reset};
}
inline PurseResult CreditResale(Wealth& w, double purse, double earned) {
	Require(Nonnegative(purse) && Nonnegative(earned));
	const double cap = Cap(w), added = std::min(earned, std::max(0.0, cap - purse));
	w.bonus = std::clamp(purse + added - w.preset, 0.0, cap - w.preset);
	return {purse + added, added, false};
}

// Versioned, locale-independent, lossless text for a future save-string field. Deserialize
// builds a temporary and validates references/limits before replacing the live model.
inline std::string Serialize(const State& s) {
	std::ostringstream o;
	o.imbue(std::locale::classic()); o << std::setprecision(std::numeric_limits<double>::max_digits10);
	o << "MRESALE 1\n" << s.markets.size() << '\n';
	for (const auto& m : s.markets) {
		o << m.id << ' ' << m.merchants.size() << '\n';
		for (const auto& n : m.merchants) o << n.id << ' ' << std::quoted(n.name) << ' '
			<< n.position.x << ' ' << n.position.y << ' ' << n.position.z << '\n';
	}
	o << s.demand.size() << '\n';
	for (const auto& d : s.demand) o << d.first.first << ' ' << static_cast<int>(d.first.second) << ' '
		<< d.second.value << ' ' << d.second.updated << '\n';
	o << s.ledger.size() << '\n';
	for (const auto& r : s.ledger) o << r.merchant << ' ' << std::quoted(r.classGuid) << ' ' << std::quoted(r.itemId)
		<< ' ' << static_cast<int>(r.category) << ' ' << r.count << ' ' << r.ordinal << ' ' << r.baseValue
		<< ' ' << r.pricePaid << ' ' << r.soldAt << ' ' << r.eta << '\n';
	o << s.wealth.size() << '\n';
	for (const auto& e : s.wealth) o << e.first << ' ' << e.second.preset << ' ' << e.second.bonus << ' '
		<< e.second.lastClose << ' ' << e.second.hasClose << ' ' << e.second.tradeSinceClose << '\n';
	return o.str();
}
inline bool Deserialize(const std::string& text, State& out, std::string& error) {
	try {
		Require(text.size() <= 32 * 1024 * 1024);
		std::istringstream in(text); in.imbue(std::locale::classic());
		std::string magic; int version = 0;
		Require(static_cast<bool>(in >> magic >> version) && magic == "MRESALE" && version == 1);
		auto size = [&]() { std::size_t n = 0; Require(static_cast<bool>(in >> n) && n <= kMaxRecords); return n; };
		State s;
		std::map<std::uint64_t, bool> marketIds, merchantIds;
		std::size_t totalMerchants = 0;
		for (std::size_t n = size(); n; --n) {
			Market m; Require(static_cast<bool>(in >> m.id) && m.id && marketIds.emplace(m.id, true).second);
			const auto count = size(); Require(count && count <= kMaxRecords - totalMerchants); totalMerchants += count;
			for (std::size_t j = 0; j < count; ++j) {
				Merchant a;
				Require(static_cast<bool>(in >> a.id >> std::quoted(a.name) >> a.position.x >> a.position.y >> a.position.z));
				Require(a.id && merchantIds.emplace(a.id, true).second && std::isfinite(a.position.x) &&
				        std::isfinite(a.position.y) && std::isfinite(a.position.z));
				m.merchants.push_back(std::move(a));
			}
			Require(merchantIds.count(m.id) && std::any_of(m.merchants.begin(), m.merchants.end(),
			        [&](const auto& a) { return a.id == m.id; }));
			s.markets.push_back(std::move(m));
		}
		for (std::size_t n = size(); n; --n) {
			std::uint64_t market; int c; Demand d;
			Require(static_cast<bool>(in >> market >> c >> d.value >> d.updated));
			Require(marketIds.count(market) && c >= 0 && c < 5 && std::isfinite(d.value) && d.value >= 0.15 &&
			        d.value <= 1 && Nonnegative(d.updated));
			Require(s.demand.emplace(std::make_pair(market, static_cast<Category>(c)), d).second);
		}
		for (std::size_t n = size(); n; --n) {
			LedgerRow r; int c;
			Require(static_cast<bool>(in >> r.merchant >> std::quoted(r.classGuid) >> std::quoted(r.itemId) >> c
			        >> r.count >> r.ordinal >> r.baseValue >> r.pricePaid >> r.soldAt >> r.eta));
			Require(merchantIds.count(r.merchant) && !r.classGuid.empty() && !r.itemId.empty() && c >= 0 && c < 5 &&
			        r.count && r.count <= kMaxRecords && r.ordinal < kMaxRecords && Nonnegative(r.baseValue) &&
			        Nonnegative(r.pricePaid) && Nonnegative(r.soldAt) && Nonnegative(r.eta) && r.eta >= r.soldAt);
			r.category = static_cast<Category>(c); s.ledger.push_back(std::move(r));
		}
		for (std::size_t n = size(); n; --n) {
			std::uint64_t id; Wealth w;
			Require(static_cast<bool>(in >> id >> w.preset >> w.bonus >> w.lastClose >> w.hasClose >> w.tradeSinceClose));
			Require(merchantIds.count(id) && Nonnegative(w.bonus) && w.bonus <= Cap(w) - w.preset && Nonnegative(w.lastClose));
			Require(s.wealth.emplace(id, w).second);
		}
		in >> std::ws; Require(in.eof());
		out = std::move(s); error.clear(); return true;
	} catch (const std::exception& e) { error = e.what(); return false; }
}

} // namespace merchantresale
