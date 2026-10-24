#pragma once
// Storm's per-character rule pass with remembered condition sets (see stormmemo.h). Header-only so that
// native/test/stormmemo_test.cpp checks it against the game's algorithm outside the game.
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace smemo {

// What one thread remembers while one character's rules are applied: named condition set -> result, valid only for
// that character and only until a rule action changes something the set reads. The character's state is split into
// groups (roles, contexts, ...; stormmemo.cpp kG*); a set remembers which groups it reads (its read mask), an action
// says which it writes (its write mask, kAll = unknown: everything is stale). Each group has a counter, bumped by the
// actions that write it; an entry stores the sum of `epoch` and its groups' counters when it was stored, and is valid
// while that sum is unchanged (all counters only grow, so a changed sum never comes back).
struct Memo {
	using Mask = uint32_t;
	static constexpr int kGroups = 16;
	static constexpr Mask kAll = ~(Mask)0;
	struct Entry { const void* sel; const void* soul; uint64_t stamp; Mask mask; bool res; };
	static constexpr size_t kSize = 1024, kProbe = 8;   // the game has 298 named condition sets
	Entry e[kSize] = {};
	uint64_t epoch = 1;
	uint64_t gv[kGroups] = {};
	const void* soul = nullptr;   // the character whose rules are being applied, or null (nothing remembered)
	int inAction = 0;             // >0 while a rule action runs (or a check re-evaluates): no lookups, no stores

	bool Active(const void* s) const { return soul && soul == s && !inAction; }
	void Changed(Mask w = kAll) {   // w = the groups an action wrote; kAll: everything remembered so far is stale
		if (w == kAll) { ++epoch; return; }
		for (int i = 0; i < kGroups; i++) if (w >> i & 1) ++gv[i];
	}
	uint64_t Stamp(Mask m) const {
		uint64_t s = epoch;
		for (int i = 0; i < kGroups; i++) if (m >> i & 1) s += gv[i];
		return s;
	}
	bool Fresh(const Entry& x) const { return x.sel && x.stamp == Stamp(x.mask); }
	static size_t Hash(const void* p) { return (size_t)(((uintptr_t)p >> 4) * 0x9E3779B97F4A7C15ull >> 54); }
	bool Get(const void* sel, bool* res) const {
		for (size_t i = 0, h = Hash(sel); i < kProbe; i++) {
			const Entry& x = e[(h + i) & (kSize - 1)];
			if (x.sel == sel && x.soul == soul && Fresh(x)) { *res = x.res; return true; }
		}
		return false;
	}
	void Put(const void* sel, bool res, Mask reads) {
		const size_t h = Hash(sel);
		size_t at = h & (kSize - 1);
		for (size_t i = 0; i < kProbe; i++) {
			const size_t k = (h + i) & (kSize - 1);
			if (e[k].sel == sel || !Fresh(e[k]) || e[k].soul != soul) { at = k; break; }   // its own slot, or a stale one
		}
		e[at] = { sel, soul, Stamp(reads), reads, res };
	}
};

// One level of one rule group, the way FUN_180698e84 does it: every rule at this level whose conditions all pass
// (short-circuit, in order, null entries skipped) runs its actions in order. The only change: after each action the
// groups it writes are stale (a.Writes(action), kAll if unknown), and while an action runs nothing is looked up or
// stored. A supplies the game's layout:
//   using Rule; int MaxLevel(g); Rule* Rules(g, Rule** end); Rule* Next(r); int Level(r);
//   void** Conds(r, void*** end); void** Actions(r, void*** end); void** NextEntry(p)   (16-byte counted pointers)
//   bool Eval(cond, soul); void Run(action, soul, r); Memo::Mask Writes(action); void Ended(r, cond); void Ran(r)
//   (indexed walk) bool NameKeys(cond, std::vector<std::string>*); bool ReadOnly(cond); const char* SoulName(soul)
// One rule, as FUN_180698e84 runs it. a.Ended(r, cond) is told which condition stopped it (null: all passed) and
// a.Ran(r) when its actions are done (the census in stormmemo.cpp; no-ops in the test).
template <class A>
void ApplyRule(A& a, typename A::Rule* r, void* soul, Memo& m) {
	void** ce = nullptr;
	void** c = a.Conds(r, &ce);
	for (; c != ce; c = a.NextEntry(c))
		if (*c && !a.Eval(*c, soul)) break;
	if (c != ce) { a.Ended(r, *c); return; }
	a.Ended(r, nullptr);
	void** ae = nullptr;
	for (void** p = a.Actions(r, &ae); p != ae; p = a.NextEntry(p)) {
		if (!*p) continue;
		m.inAction++;
		a.Run(*p, soul, r);
		m.inAction--;
		m.Changed(a.Writes(*p));
	}
	a.Ran(r);
}

template <class A, class G>
void ApplyLevel(A& a, G* g, void* soul, int level, Memo& m) {
	if (level > a.MaxLevel(g)) return;
	typename A::Rule* end = nullptr;
	for (typename A::Rule* r = a.Rules(g, &end); r != end; r = a.Next(r))
		if (a.Level(r) == level) ApplyRule(a, r, soul, m);
}

// The same with the group's rules sorted by level once, and split by the character name they require: the game scans
// every rule of the group for each level (boot 63: ~6,500 rules looked at per character, 62 pass, 75 % stopped by a
// name check), this visits only the level's rules that need no name or need this character's. A rule needs a name when
// one of its conditions is a name gate (a.NameKeys: C_HasName with an exact name, or an or of those) and every
// condition before it is read-only (a.ReadOnly): for a character with another name that rule fails in the game too,
// and skipping it skips only read-only evaluations. Rules run in the group's order (a merge by index).
// Index() rebuilds when the group's rule array or max level is not what it was built from.
template <class Rule>
struct Levels {
	struct Entry { uint32_t idx; Rule* r; };
	struct Gated { Rule* r; void* gate; std::vector<std::string> keys; };   // the checked walk: every rule in order
	const void* begin = nullptr; const void* end = nullptr; int maxLevel = -1;
	std::vector<std::vector<Gated>> all;                                       // per level: gate null = no name
	std::vector<std::vector<Entry>> always;                                     // per level: rules with no name gate
	std::vector<std::unordered_map<std::string, std::vector<Entry>>> byName;    // per level: name -> its rules
	size_t gated = 0, rules = 0;
};
template <class A, class G>
const Levels<typename A::Rule>& Index(A& a, G* g, Levels<typename A::Rule>& lv) {
	using Rule = typename A::Rule;
	Rule* end = nullptr;
	Rule* b = a.Rules(g, &end);
	const int ml = a.MaxLevel(g);
	if (lv.begin == b && lv.end == end && lv.maxLevel == ml) return lv;
	lv.begin = b; lv.end = end; lv.maxLevel = ml;
	const size_t n = ml >= 0 ? (size_t)ml + 1 : 0;
	lv.always.assign(n, {});
	lv.byName.assign(n, {});
	lv.all.assign(n, {});
	lv.gated = lv.rules = 0;
	std::vector<std::string> keys;
	uint32_t idx = 0;
	for (Rule* r = b; r != end; r = a.Next(r), idx++) {
		const int l = a.Level(r);
		if (l < 0 || l > ml) continue;   // other levels never run (the game's level loop ends at max)
		lv.rules++;
		void* gate = nullptr;
		void** ce = nullptr;
		for (void** c = a.Conds(r, &ce); c != ce; c = a.NextEntry(c)) {
			if (!*c) continue;
			keys.clear();
			if (a.NameKeys(*c, &keys) && !keys.empty()) { gate = *c; break; }
			if (!a.ReadOnly(*c)) break;
		}
		if (!gate) keys.clear();
		lv.all[(size_t)l].push_back({ r, gate, keys });
		if (!gate) { lv.always[(size_t)l].push_back({ idx, r }); continue; }
		lv.gated++;
		for (size_t k = 0; k < keys.size(); k++) {
			bool dup = false;
			for (size_t j = 0; j < k; j++) dup = dup || keys[j] == keys[k];
			if (!dup) lv.byName[(size_t)l][keys[k]].push_back({ idx, r });
		}
	}
	return lv;
}
// check: walk every rule in the group's order instead; a rule the index would skip (a name gate for other names) has
// its gate evaluated the game's way at the point the game would reach it (the conditions before it are read-only, so
// the rule runs in the game only if the gate is true). A true gate is a mismatch: *mismatch is set and the rule runs
// as the game runs it, so the outcome stays the game's.
template <class A, class G>
void ApplyLevelIndexed(A& a, G* g, void* soul, int level, Memo& m, Levels<typename A::Rule>& lv, bool check = false,
                       bool* mismatch = nullptr) {
	using E = typename Levels<typename A::Rule>::Entry;
	if (level > a.MaxLevel(g) || level < 0) return;
	const Levels<typename A::Rule>& x = Index(a, g, lv);
	if ((size_t)level >= x.always.size()) return;
	if (check) {
		const char* name = a.SoulName(soul);
		for (const auto& e : x.all[(size_t)level]) {
			bool mine = !e.gate;
			for (size_t k = 0; !mine && k < e.keys.size(); k++) mine = name && e.keys[k] == name;
			if (mine) { ApplyRule(a, e.r, soul, m); continue; }
			if (a.Eval(e.gate, soul)) {
				if (mismatch) *mismatch = true;
				ApplyRule(a, e.r, soul, m);
			}
		}
		return;
	}
	const std::vector<E>& al = x.always[(size_t)level];
	const std::vector<E>* named = nullptr;
	const auto& bn = x.byName[(size_t)level];
	if (!bn.empty()) {
		const char* name = a.SoulName(soul);
		if (name) {
			thread_local std::string key;
			key.assign(name);
			auto it = bn.find(key);
			if (it != bn.end()) named = &it->second;
		}
	}
	if (!named) { for (const E& e : al) ApplyRule(a, e.r, soul, m); return; }
	size_t i = 0, j = 0;
	while (i < al.size() || j < named->size()) {
		if (j == named->size() || (i < al.size() && al[i].idx < (*named)[j].idx)) ApplyRule(a, al[i++].r, soul, m);
		else ApplyRule(a, (*named)[j++].r, soul, m);
	}
}

struct Stats { uint64_t looked = 0, hits = 0, checked = 0, bad = 0; };

// A named condition set's evaluate (C_CustomSelector, FUN_1806990f4) with the memo; orig() is the game's. Outside a
// character's rule pass, or while an action runs, it is just orig(). A miss evaluates (the sets inside it are
// remembered too) and stores when readMask() allows it (asked on a miss only: the groups the set reads, or -1 = never
// remember it). When check() says so (asked on a hit
// only), a hit is evaluated again with nothing remembered (inAction) and the game's answer wins: *mismatch tells the
// caller to stop using the memo.
template <class F, class C, class K>
bool EvalNamed(Memo& m, const void* self, const void* soul, C&& cacheable, K&& check, F&& orig, Stats& st,
               bool* mismatch) {
	if (!m.Active(soul)) return orig();
	st.looked++;
	bool r;
	if (m.Get(self, &r)) {
		st.hits++;
		if (check()) {
			st.checked++;
			m.inAction++;
			const bool g = orig();
			m.inAction--;
			if (g != r) { st.bad++; *mismatch = true; return g; }
		}
		return r;
	}
	r = orig();
	const int64_t reads = cacheable();
	if (reads >= 0) m.Put(self, r, (Memo::Mask)reads);
	return r;
}

}  // namespace smemo
