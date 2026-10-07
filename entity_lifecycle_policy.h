#pragma once
#include <cstdint>

namespace SvarogHooks {
template<class Listener, class Entity, class Lookup>
void NotifyEntityCreated(Listener* listener, Entity* entity, std::uint32_t raw, Lookup&& lookup) {
    // The original/another POST callback may already have deleted the entity.
    // Resolve the full raw serial BEFORE dereferencing the borrowed pointer.
    if (listener && entity && raw != UINT32_MAX && lookup(raw) == entity)
        listener->OnEntityCreated(entity);
}
template<class Listener, class Entity>
void NotifyEntityRemoving(Listener* listener, Entity* entity, std::uint32_t raw) {
    if (listener && entity && entity->m_pEntity && raw != UINT32_MAX &&
        std::uint32_t(entity->GetRefEHandle().ToInt()) == raw)
        listener->OnEntityDeleted(entity);
}
template<class System, class Listener, class Entity>
void NotifyEntityParent(System* system, Listener* listener, Entity* entity, Entity* parent) {
    if (!system || !listener || !entity || !entity->m_pEntity) return;
    const auto& handle = entity->GetRefEHandle();
    if (handle.IsValid() && system->GetEntityInstance(handle) == entity)
        listener->OnEntityParentChanged(entity, parent);
}
} // namespace SvarogHooks
