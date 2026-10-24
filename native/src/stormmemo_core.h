#pragma once
// Storm's per-character rule pass with remembered condition sets (see stormmemo.h). Header-only so that
// native/test/stormmemo_test.cpp checks it against the game's algorithm outside the game.
#include <cstddef>
#include <cstdint>

namespace smemo {

// What one thread remembers while one character's rules are applied: named condition set -> result, valid only for
// that character and only until the next rule action runs (an action can change anything a condition reads).
struct Memo {
	struct Entry { const void* sel; const void* soul; uint64_t epoch; bool res; };
	static constexpr size_t kSize = 1024, kProbe = 8;   // the game has 298 named condition sets
	Entry e[kSize] = {};
	uint64_t epoch = 1;
	const void* soul = nullptr;   // the character whose rules are being applied, or null (nothing remembered)
	int inAction = 0;             // >0 while a rule action runs (or a check re-evaluates): no lookups, no stores

	bool Active(const void* s) const { return soul && soul == s && !inAction; }
	void Changed() { ++epoch; }   // everything remembered so far is stale
	static size_t Hash(const void* p) { return (size_t)(((uintptr_t)p >> 4) * 0x9E3779B97F4A7C15ull >> 54); }
	bool Get(const void* sel, bool* res) const {
		for (size_t i = 0, h = Hash(sel); i < kProbe; i++) {
			const Entry& x = e[(h + i) & (kSize - 1)];
			if (x.sel == sel && x.epoch == epoch && x.soul == soul) { *res = x.res; return true; }
		}
		return false;
	}
	void Put(const void* sel, bool res) {
		const size_t h = Hash(sel);
		size_t at = h & (kSize - 1);
		for (size_t i = 0; i < kProbe; i++) {
			const size_t k = (h + i) & (kSize - 1);
			if (e[k].sel == sel || e[k].epoch != epoch) { at = k; break; }   // its own slot, or a stale one
		}
		e[at] = { sel, soul, epoch, res };
	}
};

// One level of one rule group, the way FUN_180698e84 does it: every rule at this level whose conditions all pass
// (short-circuit, in order, null entries skipped) runs its actions in order. The only change: after each action the
// memo is stale, and while an action runs nothing is looked up or stored. A supplies the game's layout:
//   using Rule; int MaxLevel(g); Rule* Rules(g, Rule** end); Rule* Next(r); int Level(r);
//   void** Conds(r, void*** end); void** Actions(r, void*** end); void** NextEntry(p)   (16-byte counted pointers)
//   bool Eval(cond, soul); void Run(action, soul, r)
template <class A, class G>
void ApplyLevel(A& a, G* g, void* soul, int level, Memo& m) {
	if (level > a.MaxLevel(g)) return;
	typename A::Rule* end = nullptr;
	for (typename A::Rule* r = a.Rules(g, &end); r != end; r = a.Next(r)) {
		if (a.Level(r) != level) continue;
		void** ce = nullptr;
		void** c = a.Conds(r, &ce);
		for (; c != ce; c = a.NextEntry(c))
			if (*c && !a.Eval(*c, soul)) break;
		if (c != ce) continue;
		void** ae = nullptr;
		for (void** p = a.Actions(r, &ae); p != ae; p = a.NextEntry(p)) {
			if (!*p) continue;
			m.inAction++;
			a.Run(*p, soul, r);
			m.inAction--;
			m.Changed();
		}
	}
}

struct Stats { uint64_t looked = 0, hits = 0, checked = 0, bad = 0; };

// A named condition set's evaluate (C_CustomSelector, FUN_1806990f4) with the memo; orig() is the game's. Outside a
// character's rule pass, or while an action runs, it is just orig(). A miss evaluates (the sets inside it are
// remembered too) and stores when cacheable() allows it (asked on a miss only). When check() says so (asked on a hit
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
	if (cacheable()) m.Put(self, r);
	return r;
}

}  // namespace smemo
