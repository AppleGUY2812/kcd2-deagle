#pragma once
// The concept graph's find-by-name, the game's visit order without its deque (see conceptbfs.h). Header-only so that
// native/test/conceptbfs_test.cpp checks it against the game's algorithm outside the game.
#include <cstddef>
#include <cstring>
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

}  // namespace cbfs
