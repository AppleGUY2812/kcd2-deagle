// stormmemo_core.h against the game's rule pass (FUN_180698e84 written out below without the memo). Random rule
// sets: named condition sets shared by many rules and nested in each other, plain and/or/not, actions that change the
// very fields the conditions read. Every character must fire the same rules in the same order and end up the same,
// and the memo must actually be used: once with every action marking everything stale, once with exact read/write
// groups (a field = a group), which must reuse more. Then a condition that is not read-only, and an action that writes
// a group it doesn't declare: the check must catch both.
#include "../src/stormmemo_core.h"
#include <cstdio>
#include <map>
#include <random>
#include <vector>

struct Soul { int f[6] = {}; std::vector<int> fired; };
enum Kind { Leaf, NamedAnd, NamedOr, And, Or, Not, Flaky };
struct Cond {
	Kind kind = Leaf;
	int a = 0, b = 0;
	std::vector<void*> kids;   // 16-byte entries like the game's counted pointers: {ptr, 0}
	int calls = 0;
};
struct Action { int idx, val; };
struct Rule {
	int id = 0, level = 0;
	std::vector<void*> conds, actions;
};
struct Group { int maxLevel = 0; std::vector<Rule> rules; };

smemo::Memo g_m;
smemo::Stats g_st;
bool g_useMemo = false, g_check = false, g_mismatch = false;
bool g_indexed = false;   // the walk over rules sorted by level (stormmemo.cpp's default)
std::map<Group*, smemo::Levels<Rule>> g_lv;
int g_masks = 0;   // 0: every action writes everything; 1: exact groups (field i = group i); 2: actions declare the wrong group
long g_leafEvals = 0;

bool Eval(Cond* c, Soul* s);
int64_t Reads(const Cond* c) {   // the fields a set reads
	if (!c) return 0;
	if (c->kind == Leaf) return (int64_t)1 << c->a;
	int64_t m = 0;
	for (size_t i = 0; i < c->kids.size(); i += 2) m |= Reads((const Cond*)c->kids[i]);
	return m;
}
bool Kids(Cond* c, Soul* s, bool all) {
	for (size_t i = 0; i < c->kids.size(); i += 2) {
		if (!c->kids[i]) continue;
		const bool v = Eval((Cond*)c->kids[i], s);
		if (all && !v) return false;
		if (!all && v) return true;
	}
	return all;
}
bool Eval(Cond* c, Soul* s) {
	switch (c->kind) {
	case Leaf: g_leafEvals++; return s->f[c->a] == c->b;
	case Flaky: return (++c->calls & 1) != 0;
	case And: return Kids(c, s, true);
	case Or: return Kids(c, s, false);
	case Not: return !Eval((Cond*)c->kids[0], s);
	case NamedAnd: case NamedOr: {
		auto orig = [&] { return Kids(c, s, c->kind == NamedAnd); };
		if (!g_useMemo) return orig();
		return smemo::EvalNamed(g_m, c, s, [&] { return Reads(c); }, [] { return g_check; }, orig, g_st, &g_mismatch);
	}
	}
	return false;
}

struct Acc {
	using Rule = ::Rule;
	int MaxLevel(Group* g) { return g->maxLevel; }
	Rule* Rules(Group* g, Rule** end) { *end = g->rules.data() + g->rules.size(); return g->rules.data(); }
	Rule* Next(Rule* r) { return r + 1; }
	int Level(Rule* r) { return r->level; }
	void** Conds(Rule* r, void*** end) { *end = r->conds.data() + r->conds.size(); return r->conds.data(); }
	void** Actions(Rule* r, void*** end) { *end = r->actions.data() + r->actions.size(); return r->actions.data(); }
	void** NextEntry(void** p) { return p + 2; }
	bool Eval(void* c, void* s) { return ::Eval((Cond*)c, (Soul*)s); }
	smemo::Memo::Mask Writes(void* a) {
		const int idx = ((Action*)a)->idx;
		return g_masks == 0 ? smemo::Memo::kAll : g_masks == 1 ? 1u << idx : 1u << ((idx + 1) % 6);
	}
	void Ended(Rule*, void*) {}
	void Ran(Rule*) {}
	void Run(void* a, void* s, Rule* r) {
		Soul* so = (Soul*)s;
		so->f[((Action*)a)->idx] = ((Action*)a)->val;
		so->fired.push_back(r->id);
	}
};

// The game's FUN_180698e84, as disassembled: no memo.
void GameLevel(Group* g, Soul* s, int level) {
	if (level > g->maxLevel) return;
	for (Rule& r : g->rules) {
		if (r.level != level) continue;
		size_t i = 0;
		for (; i < r.conds.size(); i += 2)
			if (r.conds[i] && !Eval((Cond*)r.conds[i], s)) break;
		if (i != r.conds.size()) continue;
		for (size_t k = 0; k < r.actions.size(); k += 2)
			if (r.actions[k]) {
				s->f[((Action*)r.actions[k])->idx] = ((Action*)r.actions[k])->val;
				s->fired.push_back(r.id);
			}
	}
}

// FUN_1806b6a08's level loop over every group, with the scope stormmemo.cpp sets around it.
void Apply(std::vector<Group>& gs, Soul* s, int maxLevel, bool memo) {
	Acc a;
	if (memo) { g_m.soul = s; g_m.Changed(); }
	for (int lv = 0; lv <= maxLevel; lv++)
		for (Group& g : gs) {
			if (memo && g_indexed) smemo::ApplyLevelIndexed(a, &g, s, lv, g_m, g_lv[&g]);
			else if (memo) smemo::ApplyLevel(a, &g, s, lv, g_m);
			else GameLevel(&g, s, lv);
		}
	if (memo) { g_m.Changed(); g_m.soul = nullptr; }
}

int main() {
	std::mt19937 rng(12345);
	auto R = [&](int n) { return (int)(rng() % (unsigned)n); };
	std::vector<Cond*> all, named;
	auto leaf = [&] { Cond* c = new Cond; c->a = R(6); c->b = R(3); all.push_back(c); return c; };
	auto pick = [&]() -> Cond* {   // a condition to put in a set or a rule: mostly named sets once there are some
		const int k = R(10);
		if (k < 5 && !named.empty()) return named[R((int)named.size())];
		if (k == 5) { Cond* n = new Cond; n->kind = Not; n->kids = { leaf(), nullptr }; all.push_back(n); return n; }
		return leaf();
	};
	for (int i = 0; i < 60; i++) {   // named sets, each can use the ones before it
		Cond* c = new Cond;
		c->kind = R(2) ? NamedAnd : NamedOr;
		for (int k = 0, n = 2 + R(5); k < n; k++) {
			c->kids.push_back(R(12) ? (void*)pick() : nullptr);
			c->kids.push_back(nullptr);
		}
		all.push_back(c);
		named.push_back(c);
	}
	std::vector<Group> gs(3);
	int id = 0;
	for (Group& g : gs) {
		g.maxLevel = &g == &gs[1] ? 2 : 3;   // the pass loops to 3: group 1's level 3 must not run
		for (int i = 0; i < 300; i++) {
			Rule r;
			r.id = id++;
			r.level = R(6) - 1;   // -1 and 4 never run (the game's loop is 0..3)
			for (int k = 0, n = 1 + R(3); k < n; k++) {
				Cond* c = pick();
				if (R(4) == 0) { Cond* o = new Cond; o->kind = R(2) ? And : Or; o->kids = { pick(), nullptr, pick(), nullptr }; all.push_back(o); c = o; }
				r.conds.push_back(c);
				r.conds.push_back(nullptr);
			}
			for (int k = 0, n = R(3); k < n; k++) {
				r.actions.push_back(new Action{ R(6), R(3) });
				r.actions.push_back(nullptr);
			}
			g.rules.push_back(r);
		}
	}
	unsigned long long hits[3] = {};
	for (int pass = 0; pass < 3; pass++) {
		g_masks = pass == 0 ? 0 : 1;
		g_indexed = pass == 2;
		g_st = {};
		int bad = 0;
		long gameLeaves = 0, memoLeaves = 0;
		for (int i = 0; i < 3000; i++) {
				Soul a;
				for (int& v : a.f) v = R(3);
				Soul b = a;
				g_leafEvals = 0;
				Apply(gs, &a, 3, false);
				gameLeaves += g_leafEvals;
				g_leafEvals = 0;
				g_useMemo = true;
				g_check = (i % 7) == 0;   // the check path must not change the outcome either
				Apply(gs, &b, 3, true);
				g_useMemo = false;
				memoLeaves += g_leafEvals;
				bool same = a.fired == b.fired;
				for (int k = 0; k < 6; k++) same = same && a.f[k] == b.f[k];
				if (!same) bad++;
		}
		printf("masks %d%s, souls 3000: %d differ; leaf evaluations %ld (game) vs %ld (memo, %.0f %% fewer); named looked up %llu, "
		       "from the memo %llu; checked %llu, mismatches %llu\n", g_masks, g_indexed ? " indexed" : "", bad, gameLeaves, memoLeaves,
		       100.0 * (gameLeaves - memoLeaves) / gameLeaves, (unsigned long long)g_st.looked,
		       (unsigned long long)g_st.hits, (unsigned long long)g_st.checked, (unsigned long long)g_st.bad);
		if (bad || g_st.bad || g_mismatch || g_st.hits == 0 || memoLeaves * 10 > gameLeaves * 9) { puts("FAIL"); return 1; }
		hits[pass] = g_st.hits;

	}
	if (hits[1] <= hits[0]) { puts("FAIL: exact groups reused no more than marking everything stale"); return 1; }
	g_indexed = false;

	// An action that writes a field it doesn't declare: some remembered answers go wrong, the check must see it.
	g_masks = 2; g_st = {}; g_mismatch = false;
	for (int i = 0; i < 300 && !g_mismatch; i++) {
		Soul b;
		for (int& v : b.f) v = R(3);
		g_useMemo = true; g_check = true;
		Apply(gs, &b, 3, true);
		g_useMemo = false;
	}
	if (!g_mismatch) { puts("FAIL: the check missed an action writing an undeclared group"); return 1; }
	g_masks = 0; g_check = false;

	// A condition that isn't read-only (its answer changes from call to call): the check must catch it.
	Cond* fl = new Cond; fl->kind = Flaky;
	Cond* set = new Cond; set->kind = NamedOr; set->kids = { fl, nullptr };
	Group g1; g1.maxLevel = 0;
	for (int i = 0; i < 4; i++) { Rule r; r.id = 9000 + i; r.conds = { set, nullptr }; g1.rules.push_back(r); }
	std::vector<Group> one{ g1 };
	Soul s;
	g_useMemo = true; g_check = true; g_mismatch = false;
	Apply(one, &s, 0, true);
	if (!g_mismatch) { puts("FAIL: the check missed a condition that changes"); return 1; }

	// Outside a character's pass nothing is remembered.
	g_mismatch = false;
	const auto before = g_st.looked;
	Soul t;
	Eval(named[0], &t);
	Eval(named[0], &t);
	if (g_st.looked != before) { puts("FAIL: memo used outside a pass"); return 1; }
	puts("ok");
	return 0;
}
