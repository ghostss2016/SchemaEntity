#include "../entity_lifecycle_abi.h"
#include "../entity_lifecycle_policy.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <filesystem>
#include <vector>

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
    FleetGamedata::Document document; std::string error;
    assert(document.load((std::filesystem::path(__FILE__).parent_path().parent_path()/"config/entity_lifecycle.gamedata.fragment.ini").string(),error));
    assert(document.integer(kEntityAddSlotKey)==16 && document.integer(kEntityRemoveSlotKey)==17);
    const auto fixturePattern = [&document](const char* key) {
        FleetGamedata::Pattern result;std::istringstream input(document.get(key));std::string token;
        while(input>>token){result.bytes+=token=="??"?char(0):char(std::strtoul(token.c_str(),nullptr,16));result.mask+=token=="??"?'?':'x';}
        return result;
    };
    const auto addPattern=fixturePattern(kEntityAddPatternKey), removePattern=fixturePattern(kEntityRemovePatternKey);
    std::vector<uint8_t> add(addPattern.bytes.begin(),addPattern.bytes.end()), remove(removePattern.bytes.begin(),removePattern.bytes.end());
    const auto write = [](auto& data,std::size_t at,std::uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) data[at+i] = (value >> (i*8)) & 255;
    };
    const auto setOffsets = [&](std::uint32_t count){
        write(add,0x24,count-0x30);write(add,0x31,count);write(add,0x53,count+8);
        write(remove,0x24,count-0x30);write(remove,0x5a,count-0x2c);write(remove,0x73,count);write(remove,0x9b,count+8);
    };
    setOffsets(0x2150);
    const auto validate = [&](const uint8_t* a,std::size_t aSize,const uint8_t* r,std::size_t rSize,EntityHookAbiProof* proof=nullptr){
        return ValidateEntityHookAbi(a,aSize,r,rSize,addPattern,removePattern,proof);
    };
    EntityHookAbiProof proof;
    assert(validate(add.data(),add.size(),remove.data(),remove.size(),&proof));
    assert(proof.listenerOffset == 0x2150);
    assert(!validate(nullptr,add.size(),remove.data(),remove.size(),&proof) && !proof.listenerOffset);
    assert(!validate(add.data(),add.size()-1,remove.data(),remove.size()));
    assert(!ValidateEntityHookAbi(add.data(),add.size(),remove.data(),remove.size(),{},removePattern));
    auto unsafePattern=addPattern;unsafePattern.mask[0]='?';
    assert(!ValidateEntityHookAbi(add.data(),add.size(),remove.data(),remove.size(),unsafePattern,removePattern));
    for (std::size_t i = 0; i < add.size(); ++i) {
        if (entity_hook_detail::IsDisplacementByte(i,entity_hook_detail::kAddDisplacements)) continue;
        add[i] ^= 1; assert(!validate(add.data(),add.size(),remove.data(),remove.size())); add[i] ^= 1;
    }
    for (std::size_t i = 0; i < remove.size(); ++i) {
        if (entity_hook_detail::IsDisplacementByte(i,entity_hook_detail::kRemoveDisplacements)) continue;
        remove[i] ^= 1; assert(!validate(add.data(),add.size(),remove.data(),remove.size())); remove[i] ^= 1;
    }
    for (auto count : {0x20d0u,0x2150u,0x3000u}) {
        write(add,0x24,count-0x30); write(add,0x31,count); write(add,0x53,count+8);
        write(remove,0x24,count-0x30); write(remove,0x5a,count-0x2c); write(remove,0x73,count); write(remove,0x9b,count+8);
        assert(validate(add.data(),add.size(),remove.data(),remove.size(),&proof) && proof.listenerOffset == count);
        write(remove,0x9b,count+16); assert(!validate(add.data(),add.size(),remove.data(),remove.size()));
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
