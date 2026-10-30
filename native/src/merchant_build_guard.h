#pragma once
#include <array>
#include <cstdint>

namespace merchantresale {
// Build identity read from mapped PE metadata and its CodeView record. No Windows types.
struct BuildFingerprint {
	std::uint16_t machine = 0, sections = 0;
	std::uint32_t timestamp = 0, codeSize = 0, imageSize = 0, entryPoint = 0;
	std::array<unsigned char, 16> pdbGuid{};
	std::uint32_t pdbAge = 0;
};

template<class Install> bool ForKnownBuild(const BuildFingerprint& f, Install install) {
	// qk data: PE at 1800001c0..218; CodeView RSDS at 1847af510..520.
	// GUID db30851b-bffe-461c-97b6-3848a30e9325, age 3 (not an image address).
	constexpr std::array<unsigned char, 16> guid =
		{0x1b,0x85,0x30,0xdb,0xfe,0xbf,0x1c,0x46,0x97,0xb6,0x38,0x48,0xa3,0x0e,0x93,0x25};
	if (f.machine != 0x8664 || f.sections != 7 || f.timestamp != 0x6a350e20 ||
	    f.codeSize != 0x03a01e00 || f.imageSize != 0x05b2d000 || f.entryPoint != 0x01c1fe0c ||
	    f.pdbGuid != guid || f.pdbAge != 3) return false;
	install();
	return true;
}
} // namespace merchantresale
