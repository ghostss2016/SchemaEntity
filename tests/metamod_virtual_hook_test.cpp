// Native regression for the production adapter and the actual pinned KHook
// header. Only the engine/detour boundary is fake; registration, filtering,
// callbacks and removal use the production classes. Run via central cs2-ci.
#define META_NO_HL2SDK
#include "metamod_virtual_hook.h"
#include <cassert>
#include <map>
#include <memory>
#include <iostream>

#if METAMOD_PLAPI_VERSION >= 18
namespace KHook { IKHook* __exported__khook = nullptr; }

struct Engine {
    int originals = 0;
    virtual void Frame(bool) { ++originals; }
};

class Boundary final : public KHook::IKHook {
public:
    struct Registration { void* context; void* removed; void* pre; void* post; };
    std::map<KHook::HookID_t, Registration> hooks;
    KHook::HookID_t serial = 0;
    void* current = nullptr;
    KHook::Action action = KHook::Action::Ignore;
    bool reject = false;
    unsigned setups = 0, removals = 0;

    KHook::HookID_t SetupHook(void*,void*,void*,void*,void*,void*,void*,unsigned,bool) override {
        assert(false); return KHook::INVALID_HOOK;
    }
    KHook::HookID_t SetupVirtualHook(void** table,int index,void* context,void* removed,
        void* pre,void* post,void*,void*,unsigned,bool) override {
        assert(table && index >= 0); ++setups;
        if (reject) return KHook::INVALID_HOOK;
        hooks.emplace(++serial,Registration{context,removed,pre,post}); return serial;
    }
    void RemoveHook(KHook::HookID_t id,bool,void(*done)(KHook::HookID_t,void*),void* context) override {
        const auto found=hooks.find(id); assert(found!=hooks.end());
        const auto entry=found->second; hooks.erase(found); ++removals;
        current=entry.context;
        reinterpret_cast<void(*)(KHook::HookID_t)>(entry.removed)(id);
        current=nullptr; if(done)done(id,context);
    }
    void* GetContextPtr() override { return current; }
    void* GetOriginalFunction() override { assert(false); return nullptr; }
    void* GetOriginalValuePtr() override { assert(false); return nullptr; }
    void* GetOverrideValuePtr() override { assert(false); return nullptr; }
    void* GetCurrentValuePtr(bool) override { assert(false); return nullptr; }
    void DestroyReturnValue() override {}
    void* DoRecall(KHook::Action,void*,std::size_t,void*,void*) override { assert(false); return nullptr; }
    void SaveReturnValue(KHook::Action next,void*,std::size_t,void*,void*,bool) override {
        if(next>action)action=next;
    }
    void* FindOriginal(void*) override { assert(false); return nullptr; }
    void* FindOriginalVirtual(void**,int) override { assert(false); return nullptr; }
    void* LookupSignature(void*,std::size_t,const char*) override { assert(false); return nullptr; }
    bool WasOriginalFunctionSkipped() override { return action==KHook::Action::Supersede; }

    void Invoke(Engine& engine) {
        assert(hooks.size()==1); const auto entry=hooks.begin()->second;
        current=entry.context; action=KHook::Action::Ignore;
        reinterpret_cast<void(*)(Engine*,bool)>(entry.pre)(&engine,true);
        if(action!=KHook::Action::Supersede)engine.Frame(true);
        reinterpret_cast<void(*)(Engine*,bool)>(entry.post)(&engine,true);
        current=nullptr;
    }
};

struct Consumer {
    int pre=0,post=0; bool block=false;
    KHook::Return<void> Pre(Engine*,bool) { ++pre; return {block?KHook::Action::Supersede:KHook::Action::Ignore}; }
    KHook::Return<void> Post(Engine*,bool) { ++post; return {KHook::Action::Ignore}; }
};

int main() {
    Boundary boundary; KHook::__exported__khook=&boundary;
    Engine first,second; Consumer consumer;
    using Hook=SvarogHooks::Virtual<Engine,void,bool>;
    auto make=[&]{return std::make_unique<Hook>(&Engine::Frame,&consumer,&Consumer::Pre,&Consumer::Post);};
    {
        auto hook=make(); assert(!hook->AddInstance(nullptr)); assert(!hook->AddVtable(nullptr));
        assert(boundary.setups==0); assert(hook->AddInstance(&first));
        assert(hook->AddInstance(&first)); assert(boundary.setups==1);
        boundary.Invoke(first); assert(consumer.pre==1 && consumer.post==1 && first.originals==1);
        boundary.Invoke(second); assert(consumer.pre==1 && consumer.post==1 && second.originals==1);
        consumer.block=true; boundary.Invoke(first);
        assert(consumer.pre==2 && consumer.post==2 && first.originals==1);
        hook->Remove(&first); boundary.Invoke(first);
        assert(consumer.pre==2 && consumer.post==2 && first.originals==2);
    }
    assert(boundary.hooks.empty() && boundary.removals==1);
    {
        consumer.block=false; auto hook=make(); auto table=*reinterpret_cast<void***>(&first);
        assert(hook->AddVtable(table)); assert(hook->AddVtable(table)); assert(boundary.setups==2);
        boundary.Invoke(first); boundary.Invoke(second); assert(consumer.pre==4 && consumer.post==4);
        hook->RemoveVtable(table); boundary.Invoke(second); assert(consumer.pre==4);
    }
    assert(boundary.hooks.empty() && boundary.removals==2);
    {
        boundary.reject=true; auto hook=make();
        assert(!hook->AddInstance(&first)); assert(!hook->IsActive());
        assert(!hook->AddVtable(*reinterpret_cast<void***>(&first))); assert(!hook->IsActive());
    }
    assert(boundary.hooks.empty() && boundary.removals==2);
    KHook::__exported__khook=nullptr;
    std::cout<<"MetaMod API18 typed hooks: all checks passed\n";
}
#else
int main() { std::cout<<"MetaMod API17 build: API18 hook checks require sdk-staging\n"; }
#endif
