// conceptbfs_core.h against the game's find-by-name (FUN_181e32040, written out below the way the game does it: a
// deque of counted references, test the front, pop it, append its children if it is a container). Random graphs with
// shared children, unnamed nodes, leaves that aren't containers, duplicate names and missing names. Checks the same
// node comes back and every reference count ends where it started, plus one on the node returned.
#include "../src/conceptbfs_core.h"
#include <cstdio>
#include <deque>
#include <random>
#include <string>
#include <vector>

struct Node {
	std::string name;
	bool named = true, container = true;
	int type = 0;   // stands in for the vtable
	std::vector<void*> kids;
	int refs = 1;   // held by the graph
};

struct Ops {
	long addRefs = 0, releases = 0;
	bool Named(void* n) { return ((Node*)n)->named; }
	bool Container(void** slot) { return ((Node*)*slot)->container; }
	void AddRef(void* n) { ((Node*)n)->refs++; addRefs++; }
	void Release(void* n) { ((Node*)n)->refs--; releases++; }
	long badNameReads = 0;   // the game reads a name only where Named() is true (boot 66)
	const char* Name(void* n) { if (!((Node*)n)->named) badNameReads++; return ((Node*)n)->name.c_str(); }
	const void* Vt(void* n) { return (const void*)(uintptr_t)(((Node*)n)->type + 1); }
	void** Kids(void* n, void*** end) {
		auto& k = ((Node*)n)->kids;
		*end = k.data() + k.size();
		return k.data();
	}
};

// The game's version, step for step (refs per node: queue copy, front copy, name-test copy, cast copy).
void* GameFind(Node* root, const char* want, Ops& o) {
	std::deque<void*> q;
	for (void* k : root->kids) { o.AddRef(k); q.push_back(k); }
	void* out = nullptr;
	while (!q.empty()) {
		void* n = q.front();
		o.AddRef(n);                      // local_res8
		o.AddRef(n);                      // plVar3
		const bool hit = o.Named(n) && !strcmp(o.Name(n), want);
		o.Release(n);                     // plVar3
		if (hit) { out = n; break; }      // local_res8 moves into *out; the deque's destructor releases the rest
		q.pop_front(); o.Release(n);
		void* slot = n;
		if (o.Container(&slot)) {
			o.AddRef(n);                  // local_88
			for (void* k : ((Node*)n)->kids) { o.AddRef(k); q.push_back(k); }
			o.Release(n);
		}
		o.Release(n);                     // local_res8
	}
	for (void* n : q) o.Release(n);
	return out;
}

int main() {
	std::mt19937 rng(12345);
	int fails = 0;
	long visitedTotal = 0;
	for (int t = 0; t < 3000; t++) {
		const int count = 1 + rng() % 200;
		std::vector<Node> nodes(count + 1);
		Node& root = nodes[count];
		for (int i = 0; i < count; i++) {
			nodes[i].name = "n" + std::to_string(rng() % (count / 2 + 2));   // duplicates on purpose
			nodes[i].named = rng() % 5 != 0;
			nodes[i].container = rng() % 4 != 0;
		}
		// a DAG: children always have a higher index, some shared by several parents
		for (int i = 0; i < count; i++) {
			const int kids = rng() % 4;
			for (int k = 0; k < kids && i + 1 < count; k++) nodes[i].kids.push_back(&nodes[i + 1 + rng() % (count - i - 1)]);
		}
		for (int k = 0; k < 1 + (int)(rng() % 3); k++) root.kids.push_back(&nodes[rng() % count]);
		root.name = "root";
		std::vector<void*> q;
		for (int ask = 0; ask < 5; ask++) {
			const std::string want = ask == 4 ? "missing" : "n" + std::to_string(rng() % (count / 2 + 2));
			Ops a, b;
			std::vector<int> before;
			for (auto& n : nodes) before.push_back(n.refs);
			size_t visited = 0;
			void* fast = cbfs::Find(&root, want.c_str(), a, q, &visited);
			std::vector<int> afterFast;
			for (auto& n : nodes) afterFast.push_back(n.refs);
			if (fast) ((Node*)fast)->refs--;   // hand the caller's reference back
			bool refsOk = true;
			for (size_t i = 0; i < nodes.size(); i++) if (nodes[i].refs != before[i]) refsOk = false;
			void* game = GameFind(&root, want.c_str(), b);
			if (game) ((Node*)game)->refs--;
			for (size_t i = 0; i < nodes.size(); i++) if (nodes[i].refs != before[i]) refsOk = false;
			visitedTotal += (long)visited;
			if (fast != game || !refsOk) {
				if (fails < 5) printf("FAIL graph %d ask %s: fast %p game %p refs %s\n", t, want.c_str(), fast, game, refsOk ? "ok" : "off");
				fails++;
			}
			if (fast && afterFast[(Node*)fast - nodes.data()] != before[(Node*)fast - nodes.data()] + 1) {
				if (fails < 5) printf("FAIL graph %d: the found node didn't get its caller reference\n", t);
				fails++;
			}
		}
	}
	// The remembered walk: the same graphs, edited between finds (children added, removed or swapped, a node renamed in
	// place or given a new string, its type or Named() changed); every find must return the game's node with the
	// references balanced, and the walk must be rebuilt when (and mostly only when) something changed.
	long idxFinds = 0, builds = 0, stale = 0;
	for (int t = 0; t < 2000; t++) {
		const int count = 1 + rng() % 150;
		std::vector<Node> nodes(count + 1);
		Node& root = nodes[count];
		for (int i = 0; i < count; i++) {
			nodes[i].name = "n" + std::to_string(rng() % (count / 2 + 2));
			nodes[i].named = rng() % 5 != 0;
			nodes[i].container = rng() % 4 != 0;
		}
		for (int i = 0; i < count; i++)
			for (int k = 0, kids = rng() % 4; k < kids && i + 1 < count; k++)
				nodes[i].kids.push_back(&nodes[i + 1 + rng() % (count - i - 1)]);
		for (int k = 0; k < 1 + (int)(rng() % 3); k++) root.kids.push_back(&nodes[rng() % count]);
		cbfs::Snap snap;
		for (int ask = 0; ask < 12; ask++) {
			if (rng() % 3 == 0) {   // an edit
				Node& x = nodes[rng() % count];
				switch (rng() % 6) {
				case 0: { const int i = (int)(&x - nodes.data()); if (i + 1 < count) x.kids.push_back(&nodes[i + 1 + rng() % (count - i - 1)]); } break;
				case 1: if (!x.kids.empty()) x.kids.erase(x.kids.begin() + rng() % x.kids.size()); break;
				case 2: x.name[x.name.size() - 1] = (char)('0' + rng() % 10); break;   // in place, same length
				case 3: x.name = "n" + std::to_string(rng() % (count / 2 + 2)) + "x"; break;
				case 4: x.named = !x.named; break;
				case 5: x.type++; break;
				}
			}
			const std::string want = ask % 5 == 4 ? "missing" : "n" + std::to_string(rng() % (count / 2 + 2));
			Ops a, b;
			std::vector<int> before;
			for (auto& n : nodes) before.push_back(n.refs);
			void* fast = cbfs::FindIndexed(&root, want.c_str(), a, snap);
			if (fast) ((Node*)fast)->refs--;
			bool refsOk = true;
			for (size_t i = 0; i < nodes.size(); i++) if (nodes[i].refs != before[i]) refsOk = false;
			void* game = GameFind(&root, want.c_str(), b);
			if (game) ((Node*)game)->refs--;
			idxFinds++;
			if (a.badNameReads) {
				if (fails < 5) printf("FAIL indexed graph %d: %ld names read on nodes without a name\n", t, a.badNameReads);
				fails++;
			}
			if (fast != game || !refsOk) {
				if (fails < 5) printf("FAIL indexed graph %d ask %d %s: fast %p game %p refs %s\n", t, ask, want.c_str(), fast, game, refsOk ? "ok" : "off");
				fails++;
			}
		}
		builds += (long)snap.builds; stale += (long)snap.stale;
	}
	printf("conceptbfs_test: remembered walk: %ld finds, %ld walks built (%ld after an edit)\n", idxFinds, builds, stale);
	if (builds * 2 > idxFinds) { printf("conceptbfs_test: the walk is rebuilt too often\n"); fails++; }
	if (fails) { printf("conceptbfs_test: %d failures\n", fails); return 1; }
	printf("conceptbfs_test: 15000 finds match the game's order and references (%ld nodes looked at)\n", visitedTotal);
	return 0;
}
