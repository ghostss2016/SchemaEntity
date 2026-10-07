#pragma once

namespace SvarogHooks {
// Shared production iteration policy. The SDK supplies the serial-aware
// lookup; tests supply a lookup that deliberately simulates stale/reused IDs.
template<class System, class Listener, class Info>
void VisitSpawned(System* system, Listener* listener, int count, const Info* info, int maximum) {
    if (!system || !listener || !info || count <= 0 || count > maximum) return;
    for (int i = 0; i < count; ++i) {
        auto* identity = info[i].m_pEntity;
        if (!identity || !identity->m_pInstance) continue;
        const auto handle = identity->GetRefEHandle();
        if (handle.IsValid() && system->GetEntityInstance(handle) == identity->m_pInstance)
            listener->OnEntitySpawned(identity->m_pInstance);
    }
}
} // namespace SvarogHooks
