#pragma once
// The concept graph's find-by-name, the game's visit order without its deque (see conceptbfs.h). Header-only so that
// native/test/conceptbfs_test.cpp checks it against the game's algorithm outside the game.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cbfs {

// O supplies the node operations (the game's, in conceptbfs.cpp):
//   bool Named(void* n)          node vtable +0xb0 (only such nodes are compared by name)
//   bool Container(void** slot)  FUN_180695864: is the node a container (rttr is_derived_from)
//   void AddRef(void* n), Release(void* n)
//   const char* Name(void* n)    *(char**)(n+0x10)
//   void** Kids(void* n, void*** end)   the children vector n+0x40 .. n+0x48
// Returns the first node in breadth-first order (the root's children first; the root itself is never compared) whose
// name equals `want`, with one reference added for the caller, or nullptr. Every queued node holds one reference
// until it has been looked at, as in the game (it holds four), so nodes dropped from the graph meanwhile stay alive.
// *visited: nodes looked at. q: scratch, kept by the caller so it isn't allocated each call.
template <class O>
void* Find(void* root, const char* want, O& o, std::vector<void*>& q, size_t* visited) {
	q.clear();
	void** end = nullptr;
	for (void** p = o.Kids(root, &end); p < end; ++p)
		if (*p) { o.AddRef(*p); q.push_back(*p); }
	void* found = nullptr;
	size_t i = 0;
	for (; i < q.size(); ++i) {
		void* n = q[i];
		if (o.Named(n) && !strcmp(o.Name(n), want)) { found = n; ++i; break; }   // its queue reference becomes the caller's
		void* slot = n;
		if (o.Container(&slot)) {
			void** e = nullptr;
			for (void** p = o.Kids(n, &e); p < e; ++p)
				if (*p) { o.AddRef(*p); q.push_back(*p); }
		}
		o.Release(n);
	}
	for (size_t j = i; j < q.size(); ++j) o.Release(q[j]);
	if (visited) *visited = i;
	return found;
}

// The walk remembered (Snap): one full walk in the game's order records every node it looks at (its type, whether it is
// compared by name, its name, and for a container the children it queued) and, per name, the first node that has it.
// A find then checks only that the part of the graph the game's walk would cover is what was recorded: the root's
// children, and for every node up to the answer (all of them for a name that isn't there) the same type (vtable),
// the same Named() answer, the same name (pointer, length and first 8 bytes) and, for a container, the same children
// in the same order. Then the game's walk would visit the same nodes in the same order and stop at the same one.
// Any difference: walk again (Build). O adds: const void* Vt(n); int NameLen(n).
struct Snap {
	struct Ent { void* n; const void* vt; const char* name; int len; char first[8]; bool named, cont; uint32_t kid0, nkids; };
	void* root = nullptr;
	std::vector<void*> rootKids, kids;    // kids: every container's children, in walk order
	std::vector<Ent> ents;                // the walk, in order
	std::vector<std::string> names;       // owns the keys of `first`
	std::unordered_map<std::string_view, uint32_t> first;
	size_t builds = 0, checks = 0, stale = 0;
};

template <class O>
void Record(Snap::Ent& e, void* n, O& o) {
	e.n = n; e.vt = o.Vt(n); e.named = o.Named(n);
	e.name = o.Name(n); e.len = e.name ? o.NameLen(n) : 0;
	memset(e.first, 0, sizeof e.first);
	if (e.name) memcpy(e.first, e.name, e.len < 8 ? (size_t)e.len : 8);
}
template <class O>
bool Same(const Snap::Ent& e, O& o) {
	if (o.Vt(e.n) != e.vt || o.Name(e.n) != e.name || o.Named(e.n) != e.named) return false;
	if (!e.name) return true;
	if (o.NameLen(e.n) != e.len) return false;
	char f[8] = {};
	memcpy(f, e.name, e.len < 8 ? (size_t)e.len : 8);
	return !memcmp(f, e.first, 8);
}

// The full walk, references held as Find holds them (one per queued node until it has been looked at).
template <class O>
void Build(void* root, O& o, Snap& s) {
	s.root = root; s.rootKids.clear(); s.kids.clear(); s.ents.clear(); s.names.clear(); s.first.clear();
	s.builds++;
	std::vector<void*> q;
	void** end = nullptr;
	for (void** p = o.Kids(root, &end); p < end; ++p) {
		s.rootKids.push_back(*p);
		if (*p) { o.AddRef(*p); q.push_back(*p); }
	}
	s.ents.reserve(q.size() * 4);
	std::unordered_map<std::string, uint32_t> seen;
	for (size_t i = 0; i < q.size(); ++i) {
		void* n = q[i];
		Snap::Ent e;
		Record(e, n, o);
		void* slot = n;
		e.cont = o.Container(&slot);
		e.kid0 = (uint32_t)s.kids.size(); e.nkids = 0;
		if (e.named && e.name && !seen.count(e.name)) seen.emplace(e.name, (uint32_t)i);
		if (e.cont) {
			void** ke = nullptr;
			for (void** p = o.Kids(n, &ke); p < ke; ++p) {
				s.kids.push_back(*p); e.nkids++;
				if (*p) { o.AddRef(*p); q.push_back(*p); }
			}
		}
		s.ents.push_back(e);
		o.Release(n);
	}
	s.names.reserve(seen.size());
	for (auto& kv : seen) s.names.push_back(kv.first);
	for (const std::string& k : s.names) s.first.emplace(std::string_view(k), seen[k]);
}

// Is the walk up to and including position `upto` (ents.size() = all of it) still the recorded one?
template <class O>
bool Valid(const Snap& s, void* root, size_t upto, O& o) {
	void** end = nullptr;
	void** b = o.Kids(root, &end);
	if ((size_t)(end - b) != s.rootKids.size() || (end > b && memcmp(b, s.rootKids.data(), (end - b) * sizeof(void*))))
		return false;
	const size_t n = upto < s.ents.size() ? upto + 1 : s.ents.size();
	for (size_t i = 0; i < n; i++) {
		const Snap::Ent& e = s.ents[i];
		if (!Same(e, o)) return false;
		if (e.cont) {
			void** ke = nullptr;
			void** kb = o.Kids(e.n, &ke);
			if ((size_t)(ke - kb) != e.nkids || (e.nkids && memcmp(kb, &s.kids[e.kid0], e.nkids * sizeof(void*)))) return false;
		}
	}
	return true;
}

// Find with the remembered walk: the same node as Find (one reference added for the caller) or nullptr.
template <class O>
void* FindIndexed(void* root, const char* want, O& o, Snap& s) {
	for (int attempt = 0; attempt < 2; attempt++) {
		if (s.root != root || attempt) { if (attempt) s.stale++; Build(root, o, s); }
		auto it = s.first.find(std::string_view(want));
		const size_t upto = it == s.first.end() ? s.ents.size() : it->second;
		s.checks++;
		if (!Valid(s, root, upto, o)) continue;
		if (it == s.first.end()) return nullptr;
		void* n = s.ents[it->second].n;
		o.AddRef(n);
		return n;
	}
	std::vector<void*> q;   // changed while being walked (a Named() that edits the graph?): the plain walk
	return Find(root, want, o, q, nullptr);
}

}  // namespace cbfs
