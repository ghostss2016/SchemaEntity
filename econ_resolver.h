#pragma once
// Fleet-wide named econ setter. No fixed engine address and no vtable guess.
// Canonical engine-watch record: SchemaEntity/gamedata_econ.json.
#include "give_resolver.h" // shared executable /proc/self/maps scanner only
#include <cstdint>
#include <cstring>

class CAttributeList;
namespace CS2Econ {
using SetOrAddAttribute_t = void (*)(CAttributeList*, const char*, float);
inline constexpr const char* SIG_SetOrAddAttribute =
    "55 48 89 E5 41 57 41 56 49 89 FE 41 55 41 54 53 48 89 F3 48 83 EC ? F3 0F 11 85";

inline SetOrAddAttribute_t ResolveSetOrAddAttribute(int* matches = nullptr) {
    int count = 0;
    const auto address = CS2Give::ProcMapsScan("libserver.so", SIG_SetOrAddAttribute, &count);
    if (matches) *matches = count;
    return address && count == 1 ? reinterpret_cast<SetOrAddAttribute_t>(address) : nullptr;
}

// Valve attributes marked stored_as_integer use the IEEE storage bits, NOT a
// numeric float conversion. The setter's ABI is always (list, name, float).
inline float IntegerAttribute(uint32_t value) {
    float encoded;
    static_assert(sizeof(encoded) == sizeof(value), "econ integer ABI");
    std::memcpy(&encoded, &value, sizeof(encoded));
    return encoded;
}
} // namespace CS2Econ
