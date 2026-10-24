#pragma once
// One free-RAM rule for the read-ahead caches (pakahead, streamahead, xmlahead).
// Why: boot 14 (2026-10-24, 16 GB PC, WSL holding 3.7 GB) held 2.5 GB ahead while free RAM fell to ~1 GB. Windows paged
// our buffers out, the game's loading thread copied from them at page-fault speed while holding CryPak's file lock, and
// the main thread's file opens waited on that lock: the objects step went from 9.5 to 13.4 s.
// Rule: read ahead only while free RAM stays at or above kTakeMb. Below kShedMb, the caches free their farthest-ahead
// copies first (they go back to "waiting" and are read again once there is room). The game always comes first.
// Tests: env DEAGLE_RAM_FLOOR_MB sets the take level (shed = take - 300), or ramgov::SetForTest.
#include <windows.h>
#include <atomic>
#include <cstdlib>

namespace ramgov {

inline std::atomic<long long>& TakeLevel() {
	static std::atomic<long long> v{[] {
		char e[32] = {};
		return GetEnvironmentVariableA("DEAGLE_RAM_FLOOR_MB", e, sizeof e) ? atoll(e) : 1500ll;
	}()};
	return v;
}
inline std::atomic<long long>& ShedLevel() {
	static std::atomic<long long> v{TakeLevel().load() - 300};
	return v;
}
inline void SetForTest(long long takeMb, long long shedMb) { TakeLevel() = takeMb; ShedLevel() = shedMb; }

// free RAM in MB (Windows' "available": free + standby), measured at most every ~16 ms
inline long long AvailMb() {
	static std::atomic<ULONGLONG> at{0};
	static std::atomic<long long> mb{0};
	const ULONGLONG now = GetTickCount64();
	if (now != at.load(std::memory_order_relaxed)) {
		MEMORYSTATUSEX m{};
		m.dwLength = sizeof m;
		if (GlobalMemoryStatusEx(&m)) mb = (long long)(m.ullAvailPhys >> 20);
		at = now;
	}
	return mb.load(std::memory_order_relaxed);
}

// may a cache take `bytes` more now?
inline bool CanTake(long long bytes) { return AvailMb() - (bytes >> 20) >= TakeLevel().load(); }

// bytes a cache should free now (0 = none): enough to get back 256 MB above the shed level, which stays below the take
// level, so the caches do not refill straight away
inline long long ShedBytes() {
	const long long a = AvailMb(), s = ShedLevel().load();
	return a < s ? (s + 256 - a) << 20 : 0;
}

}  // namespace ramgov
