#pragma once

#include "entity_spawn_observer.h"
#include "entity_lifecycle_abi.h"
#include "entity_lifecycle_policy.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

namespace SvarogHooks {
#if METAMOD_PLAPI_VERSION >= 18
// Snapshot process mappings only during Bind. Never scan entities, the engine
// image, the filesystem or network from a notification/frame callback.
class EntityObserverMappings final {
    struct Region { std::uintptr_t begin, end; bool executable, server; };
    std::vector<Region> regions_;
public:
    EntityObserverMappings() {
        FILE* file = std::fopen("/proc/self/maps", "r");
        if (!file) return;
        char line[2048];
        while (std::fgets(line, sizeof(line), file)) {
            unsigned long begin = 0, end = 0;
            char permissions[8]{};
            if (std::sscanf(line, "%lx-%lx %7s", &begin, &end, permissions) != 3 ||
                permissions[0] != 'r' || end <= begin) continue;
            const char* path = std::strchr(line, '/');
            const char* base = path ? std::strrchr(path, '/') : nullptr;
            const bool server = base && (!std::strcmp(base + 1, "libserver.so\n") ||
                !std::strcmp(base + 1, "libserver.so (deleted)\n"));
            regions_.push_back({begin, end, permissions[2] == 'x', server});
        }
        std::fclose(file);
    }
    bool Permit(const void* address, std::size_t size, bool executable = false, bool server = false) const {
        const auto p = reinterpret_cast<std::uintptr_t>(address);
        if (!p || !size) return false;
        for (const auto& region : regions_)
            if (p >= region.begin && p < region.end && size <= region.end - p &&
                (!executable || region.executable) && (!server || region.server)) return true;
        return false;
    }
};

// Preserve Create/Spawn/Parent/Delete without Add/RemoveListenerEntity.
// OnRemove's engine ABI takes a RAW uint32, not SDK CEntityHandle (non-trivial
// invisible-reference ABI). The proven native methods only notify observers;
// they are never invoked as a registration API and no private vector is edited.
class EntityLifecycleObserver final {
    using RemoveHook = Virtual<CEntitySystem, void, CEntityInstance*, std::uint32_t>;
    using ParentHook = Virtual<CEntitySystem, void, CEntityInstance*, CEntityInstance*>;
    EntitySpawnObserver spawn_;
    std::unique_ptr<RemoveHook> added_, removed_;
    std::unique_ptr<ParentHook> parent_;
    CEntitySystem* system_ = nullptr;
    IEntityListener* listener_ = nullptr;
    std::atomic<unsigned> callbacks_{0};
    struct Scope {
        std::atomic<unsigned>& count;
        explicit Scope(std::atomic<unsigned>& value) : count(value) { ++count; }
        ~Scope() { --count; }
    };
    KHook::Return<void> Created(CEntitySystem* system, CEntityInstance* entity, std::uint32_t raw) {
        Scope scope(callbacks_);
        if (system == system_) NotifyEntityCreated(listener_, entity, raw,
            [this](std::uint32_t handle) { return system_->GetEntityInstance(CEntityHandle(handle)); });
        return {KHook::Action::Ignore};
    }
    KHook::Return<void> Removing(CEntitySystem* system, CEntityInstance* entity, std::uint32_t raw) {
        Scope scope(callbacks_);
        // PRE: the entity identity still exists; don't query a registry which
        // can already be detaching it. A stale raw serial must never be used.
        if (system == system_) NotifyEntityRemoving(listener_, entity, raw);
        return {KHook::Action::Ignore};
    }
    KHook::Return<void> ParentChanged(CEntitySystem* system, CEntityInstance* entity, CEntityInstance* parent) {
        Scope scope(callbacks_);
        if (system == system_) NotifyEntityParent(system_, listener_, entity, parent);
        return {KHook::Action::Ignore};
    }
public:
    bool Busy() const { return callbacks_.load() != 0 || spawn_.Busy(); }
    bool Clear() {
        if (Busy() || ((added_ || removed_ || parent_) && !KHook::__exported__khook)) return false;
        if (!spawn_.Clear()) return false;
        parent_.reset(); removed_.reset(); added_.reset(); listener_ = nullptr; system_ = nullptr;
        return true;
    }
    bool Bind(CEntitySystem* system, IEntityListener* listener) {
        if (system && system == system_ && listener == listener_ && added_ && removed_ && parent_) return true;
        if (!Clear() || !system || !listener || !KHook::__exported__khook) return false;
        const auto& gamedata = FleetGamedata::current();
        const int addSlot = gamedata.integer(kEntityAddSlotKey);
        const int removeSlot = gamedata.integer(kEntityRemoveSlotKey);
        if (addSlot < 0 || removeSlot < 0 || addSlot == removeSlot || addSlot >= 256 || removeSlot >= 256) {
            FleetGamedata::reportUnavailable("Plugins/SchemaEntity/EntityLifecycle/slots"); return false;
        }
        const auto& addPattern = FleetGamedata::pattern(kEntityAddPatternKey);
        const auto& removePattern = FleetGamedata::pattern(kEntityRemovePatternKey);
        if (addPattern.bytes.empty() || removePattern.bytes.empty()) return false;
        const EntityObserverMappings mappings;
        if (!mappings.Permit(system, sizeof(void*))) return false;
        auto** table = *reinterpret_cast<void***>(system);
        const auto tableSize = static_cast<std::size_t>((addSlot > removeSlot ? addSlot : removeSlot) + 1) * sizeof(void*);
        if (!mappings.Permit(table, tableSize, false, true)) return false;
        const auto original = [table](int slot) {
            void* address = KHook::__exported__khook->FindOriginalVirtual(table, slot);
            return static_cast<const std::uint8_t*>(address ? address : table[slot]);
        };
        const auto* added = original(addSlot);
        const auto* removed = original(removeSlot);
        if (!mappings.Permit(added, kEntityAddProofBytes, true, true) ||
            !mappings.Permit(removed, kEntityRemoveProofBytes, true, true) ||
            !ValidateEntityHookAbi(added, kEntityAddProofBytes, removed, kEntityRemoveProofBytes, addPattern, removePattern)) return false;
        system_ = system; listener_ = listener;
        added_ = std::make_unique<RemoveHook>(addSlot, this, nullptr, &EntityLifecycleObserver::Created);
        removed_ = std::make_unique<RemoveHook>(removeSlot, this, &EntityLifecycleObserver::Removing, nullptr);
        // PRE matches the native listener notification and sees a live entity.
        // Another native/POST listener may delete it; don't dereference it then.
        parent_ = std::make_unique<ParentHook>(&CEntitySystem::OnEntityParentChanged, this, &EntityLifecycleObserver::ParentChanged, nullptr);
        if (added_->AddVtable(table) && removed_->AddVtable(table) && parent_->AddInstance(system) && spawn_.Bind(system, listener)) return true;
        Clear(); return false;
    }
};
#endif
} // namespace SvarogHooks
