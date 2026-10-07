#pragma once

#include "metamod_virtual_hook.h"
#include "entity_spawn_policy.h"
#include <entity2/entitysystem.h>
#include <entity2/entityidentity.h>
#include <atomic>
#include <memory>

namespace SvarogHooks {
#if METAMOD_PLAPI_VERSION >= 18
// Observe the SDK's public Spawn method, not the private listener CUtlVector.
// Each recipient owns its registration; map/dependency shutdown must Clear it
// before discarding borrowed pointers. No scan, timer, precache or frame I/O.
class EntitySpawnObserver final {
    using Hook = Virtual<CEntitySystem, void, int, const EntitySpawnInfo_t*>;
    std::unique_ptr<Hook> hook_;
    CEntitySystem* system_ = nullptr;
    IEntityListener* listener_ = nullptr;
    std::atomic<unsigned> callbacks_{0};

    KHook::Return<void> Spawned(CEntitySystem* system, int count, const EntitySpawnInfo_t* info) {
        struct Scope {
            std::atomic<unsigned>& count;
            explicit Scope(std::atomic<unsigned>& value) : count(value) { ++count; }
            ~Scope() { --count; }
        } scope(callbacks_);
        if (system != system_ || !listener_ || !info || count <= 0 || count > MAX_TOTAL_ENTITIES)
            return {KHook::Action::Ignore};
        // Another POST observer can delete/reuse an entity. The common
        // iteration checks its complete handle before notifying this owner.
        VisitSpawned(system_, listener_, count, info, MAX_TOTAL_ENTITIES);
        return {KHook::Action::Ignore};
    }

public:
    bool Busy() const { return callbacks_.load() != 0; }
    bool Clear() {
        if (Busy() || (hook_ && !KHook::__exported__khook)) return false;
        hook_.reset(); listener_ = nullptr; system_ = nullptr;
        return true;
    }
    bool Bind(CEntitySystem* system, IEntityListener* listener) {
        if (system && system == system_ && listener == listener_ && hook_) return true;
        if (!Clear() || !system || !listener || !KHook::__exported__khook) return false;
        system_ = system; listener_ = listener;
        hook_ = std::make_unique<Hook>(&CEntitySystem::Spawn, this, nullptr, &EntitySpawnObserver::Spawned);
        if (hook_->AddInstance(system)) return true;
        Clear(); return false;
    }
};
#endif
} // namespace SvarogHooks
