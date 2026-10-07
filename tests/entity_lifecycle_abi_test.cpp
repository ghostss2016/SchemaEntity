#include "../entity_lifecycle_abi.h"
#include "../entity_lifecycle_policy.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>

struct Handle {
    std::uint32_t raw;
    bool IsValid() const { return raw != UINT32_MAX; }
    std::uint32_t ToInt() const { return raw; }
};
struct Entity { void* m_pEntity; Handle h; const Handle& GetRefEHandle() const { return h; } };
struct System { Entity* current; Entity* GetEntityInstance(const Handle&) { return current; } };
struct Listener {
    unsigned creates = 0, parents = 0, deletes = 0;
    Entity* lastParent = nullptr;
    void OnEntityCreated(Entity*) { ++creates; }
    void OnEntityParentChanged(Entity*, Entity* parent) { ++parents; lastParent = parent; }
    void OnEntityDeleted(Entity*) { ++deletes; }
};
int main() {
    using namespace SvarogHooks;
    auto add = entity_hook_detail::kAddPattern;
    auto remove = entity_hook_detail::kRemovePattern;
    EntityHookAbiProof proof;
    assert(ValidateEntityHookAbi(add.data(),add.size(),remove.data(),remove.size(),&proof));
    assert(proof.listenerOffset == 0x2150);
    assert(!ValidateEntityHookAbi(nullptr,add.size(),remove.data(),remove.size(),&proof) && !proof.listenerOffset);
    assert(!ValidateEntityHookAbi(add.data(),add.size()-1,remove.data(),remove.size()));
    for (std::size_t i = 0; i < add.size(); ++i) {
        if (entity_hook_detail::IsDisplacementByte(i,entity_hook_detail::kAddDisplacements)) continue;
        add[i] ^= 1; assert(!ValidateEntityHookAbi(add.data(),add.size(),remove.data(),remove.size())); add[i] ^= 1;
    }
    for (std::size_t i = 0; i < remove.size(); ++i) {
        if (entity_hook_detail::IsDisplacementByte(i,entity_hook_detail::kRemoveDisplacements)) continue;
        remove[i] ^= 1; assert(!ValidateEntityHookAbi(add.data(),add.size(),remove.data(),remove.size())); remove[i] ^= 1;
    }
    const auto write = [](auto& data,std::size_t at,std::uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) data[at+i] = (value >> (i*8)) & 255;
    };
    for (auto count : {0x20d0u,0x2150u,0x3000u}) {
        write(add,0x24,count-0x30); write(add,0x31,count); write(add,0x53,count+8);
        write(remove,0x24,count-0x30); write(remove,0x5a,count-0x2c); write(remove,0x73,count); write(remove,0x9b,count+8);
        assert(ValidateEntityHookAbi(add.data(),add.size(),remove.data(),remove.size(),&proof) && proof.listenerOffset == count);
        write(remove,0x9b,count+16); assert(!ValidateEntityHookAbi(add.data(),add.size(),remove.data(),remove.size()));
    }
    Entity entity{&proof,{0x10042}}, reused{&proof,{0x20042}};
    System system{&entity}; Listener listener;
    const auto lookup = [&](std::uint32_t h) -> Entity* { return h == 0x10042 ? system.current : nullptr; };
    NotifyEntityCreated(&listener,&entity,0x10042,lookup); assert(listener.creates == 1);
    system.current = &reused;
    NotifyEntityCreated(&listener,&entity,0x10042,lookup);
    NotifyEntityCreated(&listener,&entity,0x20042,lookup);
    assert(listener.creates == 1); system.current = &entity;
    NotifyEntityParent(&system,&listener,&entity,&reused);
    assert(listener.parents == 1 && listener.lastParent == &reused);
    system.current = &reused; NotifyEntityParent(&system,&listener,&entity,static_cast<Entity*>(nullptr));
    assert(listener.parents == 1);
    system.current = &entity; NotifyEntityParent(&system,&listener,&entity,static_cast<Entity*>(nullptr));
    assert(listener.parents == 2 && !listener.lastParent);
    NotifyEntityRemoving(&listener,&entity,0x20042); assert(listener.deletes == 0);
    NotifyEntityRemoving(&listener,&entity,0x10042); assert(listener.deletes == 1);
    entity.m_pEntity = nullptr; NotifyEntityRemoving(&listener,&entity,0x10042);
    NotifyEntityParent(&system,&listener,&entity,&reused);
    assert(listener.parents == 2 && listener.deletes == 1);
    std::puts("entity_lifecycle_abi_test: all checks passed");
}
