// Semantic decoder derived from cs2-skybox/source/entity_hooks.h at
// 249665151d770597fd96b81b95045ed82b38a2e1 (GPL-3.0). Binding patterns and
// virtual indices belong to shared signatures.ini, never binary fallbacks.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include "common_gamedata.h"

namespace SvarogHooks {

// Engine OnAdd/OnRemove receive the handle value in EDX. The SDK's
// CEntityHandle has a non-trivial copy constructor and therefore passes via an
// invisible reference under the C++ ABI. It MUST NOT appear in the hook ABI.
inline constexpr const char* kEntityAddSlotKey = "Plugins/SchemaEntity/EntityLifecycle/OnAddSlot/linux";
inline constexpr const char* kEntityRemoveSlotKey = "Plugins/SchemaEntity/EntityLifecycle/OnRemoveSlot/linux";
inline constexpr const char* kEntityAddPatternKey = "Plugins/SchemaEntity/EntityLifecycle/OnAddAbi/linux";
inline constexpr const char* kEntityRemovePatternKey = "Plugins/SchemaEntity/EntityLifecycle/OnRemoveAbi/linux";
template<class Entity>
using EntityLifecycleCallback = void (*)(Entity*, uint32_t);
template<class Hook, class Entity>
using EntityLifecycleMethod = void (Hook::*)(Entity*, uint32_t);

// Evidence for CEntitySystem::OnAddEntity / OnRemoveEntity, not permission to
// mutate a CUtlVector at a guessed SDK member offset. Runtime callers obtain
// the original functions from the validated vtable slots via KHook,
// establish that both byte windows belong to readable/executable libserver.so
// text, then validate before installing callbacks. Unknown code fails closed.
struct EntityHookAbiProof {
    uint32_t listenerOffset = 0;
};

inline constexpr std::size_t kEntityAddProofBytes = 0x75;
inline constexpr std::size_t kEntityRemoveProofBytes = 0xb5;

namespace entity_hook_detail {

// Operand positions describe the reviewed decoder format, NOT engine member
// offsets or hook indices. Only these four-byte displacements may be masked;
// every control-flow/opcode byte must be supplied and matched exactly.
inline constexpr std::array<std::size_t, 3> kAddDisplacements{{0x24, 0x31, 0x53}};
inline constexpr std::array<std::size_t, 4> kRemoveDisplacements{{0x24, 0x5a, 0x73, 0x9b}};

template<std::size_t N>
inline bool IsDisplacementByte(std::size_t i, const std::array<std::size_t, N>& offsets)
{
    for (const auto offset : offsets)
        if (i >= offset && i - offset < sizeof(uint32_t)) return true;
    return false;
}

template<std::size_t M>
inline bool Match(const uint8_t* bytes, const FleetGamedata::Pattern& pattern, std::size_t size,
                  const std::array<std::size_t, M>& displacements)
{
    if (pattern.bytes.size() != size || pattern.mask.size() != size) return false;
    for (std::size_t i = 0; i < size; ++i) {
        const bool displacement = IsDisplacementByte(i, displacements);
        if (pattern.mask[i] != 'x' && (!displacement || pattern.mask[i] != '?')) return false;
        if (pattern.mask[i] == 'x' && bytes[i] != static_cast<uint8_t>(pattern.bytes[i])) return false;
    }
    return true;
}

inline uint32_t ReadU32(const uint8_t* bytes)
{
    return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) |
        (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
}

} // namespace entity_hook_detail

inline bool ValidateEntityHookAbi(const uint8_t* onAdd, std::size_t addSize,
                                  const uint8_t* onRemove, std::size_t removeSize,
                                  const FleetGamedata::Pattern& addPattern,
                                  const FleetGamedata::Pattern& removePattern,
                                  EntityHookAbiProof* proof = nullptr)
{
    if (proof) *proof = {};
    if (!onAdd || !onRemove || addSize < kEntityAddProofBytes ||
        removeSize < kEntityRemoveProofBytes) return false;
    using namespace entity_hook_detail;
    if (!Match(onAdd, addPattern, kEntityAddProofBytes, kAddDisplacements) ||
        !Match(onRemove, removePattern, kEntityRemoveProofBytes, kRemoveDisplacements)) return false;

    const uint32_t count = ReadU32(onAdd + 0x31);
    const uint32_t networkCount = ReadU32(onAdd + 0x24);
    // Bounds reject negative/overflowing/unreasonable x86 displacements. No
    // supported member offset (including 0x2150) is hard-coded here.
    if (count < 0x1000 || count > 0x10000 || (count & 7) != 0 ||
        networkCount != count - 0x30 || ReadU32(onAdd + 0x53) != count + 8 ||
        ReadU32(onRemove + 0x24) != networkCount ||
        ReadU32(onRemove + 0x5a) != networkCount + 4 ||
        ReadU32(onRemove + 0x73) != count ||
        ReadU32(onRemove + 0x9b) != count + 8) return false;
    if (proof) proof->listenerOffset = count;
    return true;
}

} // namespace SvarogHooks
