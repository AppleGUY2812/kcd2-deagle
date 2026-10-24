// questfast_sort.h's sorted insert against std::stable_sort, the way rttr's registry uses it: push_back one entry, then
// sort the whole list by a 64-bit key. Entries are plain bytes here (0x28 and 0x18 B with the key at +0x20 / +0x10), and
// each carries its insertion number so a stability slip shows up. Many equal keys on purpose.
#include "../src/questfast_sort.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

template <size_t SZ, size_t KEY> static int Run(unsigned seed, int types, uint64_t keyRange) {
	struct E { unsigned char b[SZ]; };
	std::mt19937_64 rng(seed);
	std::vector<E> game, ours;
	for (int i = 0; i < types; i++) {
		E e{};
		const uint64_t k = rng() % keyRange;
		memcpy(e.b + KEY, &k, 8);
		memcpy(e.b, &i, sizeof i);   // insertion number in the "string" bytes
		for (size_t j = sizeof i; j < KEY; j++) e.b[j] = (unsigned char)rng();
		game.push_back(e);
		ours.push_back(e);
		std::stable_sort(game.begin(), game.end(), [](const E& a, const E& c) {
			uint64_t x, y; memcpy(&x, a.b + KEY, 8); memcpy(&y, c.b + KEY, 8); return x < y; });
		if (!SortedInsertLast(ours.front().b, ours.front().b + ours.size() * SZ, SZ, KEY)) {
			printf("FAIL size %zu seed %u: refused a sorted list at %d\n", SZ, seed, i); return 1; }
		if (memcmp(game.data(), ours.data(), game.size() * SZ)) {
			printf("FAIL size %zu seed %u: differs from stable_sort after %d entries\n", SZ, seed, i + 1); return 1; }
	}
	return 0;
}

int main() {
	int fails = 0;
	for (unsigned s = 1; s <= 40; s++) {
		fails += Run<0x28, 0x20>(s, 600, s % 2 ? 50 : ~0ull);
		fails += Run<0x18, 0x10>(s, 600, s % 3 ? 20 : ~0ull);
	}
	// an unsorted rest: refused, nothing changed
	unsigned char b[3 * 0x18] = {};
	const uint64_t k[3] = { 5, 3, 4 };
	for (int i = 0; i < 3; i++) memcpy(b + i * 0x18 + 0x10, &k[i], 8);
	unsigned char before[sizeof b];
	memcpy(before, b, sizeof b);
	if (SortedInsertLast(b, b + sizeof b, 0x18, 0x10) || memcmp(b, before, sizeof b)) { puts("FAIL unsorted rest accepted"); fails++; }
	// a byte count that isn't whole entries: refused
	if (SortedInsertLast(b, b + sizeof b - 1, 0x18, 0x10)) { puts("FAIL ragged list accepted"); fails++; }
	if (fails) return 1;
	puts("questfast_sort_test: ok");
	return 0;
}
