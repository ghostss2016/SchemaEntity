#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "vtable_resolve.h"

namespace CS2Resources {
struct Mapping { uintptr_t begin, end; bool executable; };

// Resolve the existing engine singleton's vtable using Itanium RTTI. Source2
// mappings are not always listed by dl_iterate_phdr. No new GameSystem factory
// is registered, so no engine list can retain a pointer into our unloaded DSO.
inline void** ResourcePrecacherTable() {
    std::vector<Mapping> regions;
    FILE* maps = std::fopen("/proc/self/maps", "r");
    if (!maps) return nullptr;
    char line[1024];
    while (std::fgets(line, sizeof(line), maps)) {
        if (!std::strstr(line, "libserver.so")) continue;
        unsigned long start = 0, end = 0; char perms[8] = {};
        if (std::sscanf(line, "%lx-%lx %7s", &start, &end, perms) != 3 ||
            perms[0] != 'r' || end <= start) continue;
        regions.push_back({start, end, perms[2] == 'x'});
    }
    std::fclose(maps);
    const auto readable = [&regions](uintptr_t p, size_t n, bool executable = false) {
        for (const auto& region : regions)
            if (p >= region.begin && p < region.end && n <= region.end - p &&
                (!executable || region.executable)) return true;
        return false;
    };
    const char name[] = "28CResourcePrecacherGameSystem";
    uintptr_t nameAddress = 0; int names = 0;
    for (const auto& region : regions)
        for (uintptr_t p = region.begin; sizeof(name) <= region.end - p; ++p)
            if (!std::memcmp(reinterpret_cast<void*>(p), name, sizeof(name))) { nameAddress = p; ++names; }
    if (names != 1) return nullptr;
    uintptr_t typeInfo = 0; int types = 0;
    for (const auto& region : regions)
        for (uintptr_t p = region.begin; sizeof(uintptr_t) <= region.end - p; p += sizeof(uintptr_t)) {
            uintptr_t value; std::memcpy(&value, reinterpret_cast<void*>(p), sizeof(value));
            if (value == nameAddress && p >= region.begin + sizeof(uintptr_t)) {
                typeInfo = p - sizeof(uintptr_t); ++types;
            }
        }
    if (types != 1) return nullptr;
    void** result = nullptr; int tables = 0;
    for (const auto& region : regions)
        for (uintptr_t p = region.begin + sizeof(uintptr_t); 2 * sizeof(uintptr_t) <= region.end - p; p += sizeof(uintptr_t)) {
            uintptr_t value, top, function;
            std::memcpy(&value, reinterpret_cast<void*>(p), sizeof(value));
            if (value != typeInfo) continue;
            std::memcpy(&top, reinterpret_cast<void*>(p - sizeof(uintptr_t)), sizeof(top));
            std::memcpy(&function, reinterpret_cast<void*>(p + sizeof(uintptr_t)), sizeof(function));
            if (!top && readable(function, 1, true) && readable(p + sizeof(uintptr_t), 8 * sizeof(uintptr_t))) {
                result = reinterpret_cast<void**>(p + sizeof(uintptr_t)); ++tables;
            }
        }
    return tables == 1 ? result : nullptr;
}

inline int BuildManifestSlot(void** table) {
    // Named shared contract. SDK IGameSystem::OnBuildGameSessionManifest slot7.
    return table ? vmt::ResolveSlotInTable(table, 7,
        "CResourcePrecacherGameSystem::OnBuildGameSessionManifest") : -1;
}
} // namespace CS2Resources
