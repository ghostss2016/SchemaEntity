#include "../entity_spawn_policy.h"
#include <cassert>
#include <cstdint>
#include <cstdio>

struct Entity { unsigned value; };
struct Handle { std::uint32_t raw; bool IsValid() const { return raw != UINT32_MAX; } };
struct Identity {
    Entity* m_pInstance;
    Handle handle;
    const Handle& GetRefEHandle() const { return handle; }
};
struct Info { Identity* m_pEntity; };
struct System {
    Entity* entity;
    Handle current;
    unsigned reads = 0;
    Entity* GetEntityInstance(const Handle& handle) { ++reads; return handle.raw == current.raw ? entity : nullptr; }
};
struct Listener {
    unsigned calls = 0;
    Entity* last = nullptr;
    void OnEntitySpawned(Entity* entity) { ++calls; last = entity; }
};
int main() {
    Entity entity{1}, reused{2};
    System system{&entity,{0x10001}};
    Identity identity{&entity,{0x10001}};
    const Info info[]{ {nullptr}, {&identity} };
    Listener listener;
    for (int count : {-1,0,3}) SvarogHooks::VisitSpawned(&system,&listener,count,info,2);
    SvarogHooks::VisitSpawned(&system,&listener,1,static_cast<const Info*>(nullptr),2);
    SvarogHooks::VisitSpawned(static_cast<System*>(nullptr),&listener,2,info,2);
    SvarogHooks::VisitSpawned(&system,static_cast<Listener*>(nullptr),2,info,2);
    assert(system.reads == 0 && listener.calls == 0);
    SvarogHooks::VisitSpawned(&system,&listener,2,info,2);
    assert(system.reads == 1 && listener.calls == 1 && listener.last == &entity);
    // Reuse the index with a new serial, then with a different entity pointer.
    system.current.raw = 0x20001;
    SvarogHooks::VisitSpawned(&system,&listener,2,info,2);
    assert(listener.calls == 1);
    system.current.raw = 0x10001; system.entity = &reused;
    SvarogHooks::VisitSpawned(&system,&listener,2,info,2);
    assert(listener.calls == 1);
    identity.handle.raw = UINT32_MAX;
    const unsigned reads = system.reads;
    SvarogHooks::VisitSpawned(&system,&listener,2,info,2);
    identity.m_pInstance = nullptr;
    SvarogHooks::VisitSpawned(&system,&listener,2,info,2);
    assert(listener.calls == 1 && system.reads == reads);
    std::puts("entity_spawn_policy_test: all checks passed");
}
