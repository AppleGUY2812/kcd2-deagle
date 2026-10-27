#pragma once
#include <cstdint>
#include <string>
#include <vector>
// Byte-pattern scanning in a module's executable sections. Pattern: "48 8B ?? 05" (?? / ? = any byte).
struct Module { uintptr_t base = 0; size_t size = 0; };
Module GetModule(const char* name);
std::vector<uintptr_t> FindAll(const Module& m, const char* pattern, size_t limit = 4);
uintptr_t FindUnique(const Module& m, const char* pattern);       // 0 unless exactly one match
// KCD2's LogAlways Lua handler (the Lua -> native channel), also when another plugin already hooked it: MinHook
// replaces the first 6 bytes with a jump (E9 rel32 + NOPs), so a match of the pattern without them counts.
uintptr_t FindLogAlwaysHandler(const Module& m);
uintptr_t RipTarget(uintptr_t insn, int dispOffset, int insnLen);  // target of a RIP-relative operand
std::string RttiName(const Module& m, uintptr_t vtable);           // MSVC RTTI type name of a vtable ("" if none)
