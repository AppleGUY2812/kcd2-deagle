#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
// rttr's type registry keeps two flat lists sorted by a 64-bit key (FUN_18092a154: {std::string, hash}, 0x28 B, key
// +0x20; FUN_1806a7b90: {ptr, len, hash}, 0x18 B, key +0x10). Each new type is push_back'ed and the whole list
// std::stable_sort'ed again (temp buffer, O(n log n) per type). When everything but the last entry is sorted, the same
// result is that entry moved to upper_bound(key) of the rest: stable, so it goes after equal keys. Entries move as raw
// bytes (MSVC release std::string is bitwise relocatable). Returns false and changes nothing when the rest isn't sorted
// or the size is odd: the caller then runs the original sort.
inline bool SortedInsertLast(unsigned char* first, unsigned char* last, size_t size, size_t keyOff) {
	if (!first || last < first || size == 0 || size > 64 || keyOff + 8 > size || (size_t)(last - first) % size) return false;
	const size_t n = (size_t)(last - first) / size;
	if (n < 2) return true;
	auto key = [&](size_t i) { uint64_t k; memcpy(&k, first + i * size + keyOff, 8); return k; };
	for (size_t i = 1; i + 1 < n; ++i)
		if (key(i) < key(i - 1)) return false;
	const uint64_t k = key(n - 1);
	size_t lo = 0, hi = n - 1;
	while (lo < hi) {
		const size_t mid = lo + (hi - lo) / 2;
		if (k < key(mid)) hi = mid; else lo = mid + 1;
	}
	if (lo == n - 1) return true;
	unsigned char tmp[64];
	memcpy(tmp, first + (n - 1) * size, size);
	memmove(first + (lo + 1) * size, first + lo * size, (n - 1 - lo) * size);
	memcpy(first + lo * size, tmp, size);
	return true;
}
