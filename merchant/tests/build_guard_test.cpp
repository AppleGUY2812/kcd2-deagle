#include "../native/build_guard.h"
#include <cassert>
#include <iostream>

int main() {
	// Independent fixture transcribed from qk PE/RSDS data, not from the production comparator.
	merchantresale::BuildFingerprint good;
	good.machine = 0x8664; good.sections = 7; good.timestamp = 0x6a350e20;
	good.codeSize = 0x03a01e00; good.imageSize = 0x05b2d000; good.entryPoint = 0x01c1fe0c;
	good.pdbGuid = {0x1b,0x85,0x30,0xdb,0xfe,0xbf,0x1c,0x46,0x97,0xb6,0x38,0x48,0xa3,0x0e,0x93,0x25};
	good.pdbAge = 3;
	int installed = 0;
	auto installMatchingPrologues = [&] { installed += 4; }; // three observations + Lua channel
	auto wrong = good;
	wrong.timestamp ^= 1;
	assert(!merchantresale::ForKnownBuild(wrong, installMatchingPrologues));
	assert(installed == 0);
	std::cout << "matching_prologues_wrong_build_hooks=" << installed << '\n';
	wrong = good; wrong.pdbGuid[15] ^= 1;
	assert(!merchantresale::ForKnownBuild(wrong, installMatchingPrologues) && installed == 0);
	wrong = good; ++wrong.pdbAge;
	assert(!merchantresale::ForKnownBuild(wrong, installMatchingPrologues) && installed == 0);
	wrong = good; ++wrong.imageSize;
	assert(!merchantresale::ForKnownBuild(wrong, installMatchingPrologues) && installed == 0);
	wrong = good; ++wrong.codeSize;
	assert(!merchantresale::ForKnownBuild(wrong, installMatchingPrologues) && installed == 0);
	wrong = good; ++wrong.entryPoint;
	assert(!merchantresale::ForKnownBuild(wrong, installMatchingPrologues) && installed == 0);
	wrong = good; ++wrong.sections;
	assert(!merchantresale::ForKnownBuild(wrong, installMatchingPrologues) && installed == 0);
	wrong = good; wrong.machine = 0x14c;
	assert(!merchantresale::ForKnownBuild(wrong, installMatchingPrologues) && installed == 0);
	assert(!merchantresale::ForKnownBuild({}, installMatchingPrologues) && installed == 0);
	assert(merchantresale::ForKnownBuild(good, installMatchingPrologues));
	assert(installed == 4);
	std::cout << "fingerprint_mismatches_rejected=9 known_build_hooks=" << installed << '\n';
}
