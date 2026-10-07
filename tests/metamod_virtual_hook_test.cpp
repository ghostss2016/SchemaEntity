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

// SDK intentionally only forward-declares the configuration. A reference is
// pointer-sized on the target ABI: never fabricate its object layout just to
// make the upstream stack-size calculation compile.
class OpaqueConfiguration;
static_assert(KHook::Hook<void>::_copy_stack_size<void*,const OpaqueConfiguration&>() == 2*sizeof(void*));
struct LargeConfiguration { char data[4096]; };
static_assert(KHook::Hook<void>::_copy_stack_size<void*,LargeConfiguration&>() == 2*sizeof(void*));

struct Engine {
    int originals = 0;
    virtual void Frame(bool) { ++originals; }
};

struct BoolEngine {
    int originals=0;
    virtual bool Ready(int value) { ++originals; return value>0; }
};

struct Config { int identity=7; };
struct RefEngine {
    const Config* originalConfig=nullptr;
    virtual void Start(const Config& config) { originalConfig=&config; }
};

struct MutableRefEngine {
    Config* originalFirst=nullptr;
    Config* originalSecond=nullptr;
    virtual void Transmit(int amount, Config& first, Config& second) {
        originalFirst=&first;originalSecond=&second;
        first.identity+=amount;second.identity+=amount;
    }
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
    bool overridden=false, boolValue=false;

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
    void SaveReturnValue(KHook::Action next,void* value,std::size_t size,void*,void*,bool) override {
        if(next>action) {
            action=next;
            if(value) { assert(size==sizeof(bool)); overridden=true; boolValue=*static_cast<bool*>(value); }
        }
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
    bool InvokeBool(BoolEngine& engine,int value) {
        assert(hooks.size()==1); const auto entry=hooks.begin()->second;
        current=entry.context; action=KHook::Action::Ignore; overridden=false;
        // The dummy returned by the real KHook trampoline is not the game's
        // return value; it must nevertheless be initialized scalar storage.
        assert(!reinterpret_cast<bool(*)(BoolEngine*,int)>(entry.pre)(&engine,value));
        bool result=false;
        if(action!=KHook::Action::Supersede)result=engine.Ready(value);
        assert(!reinterpret_cast<bool(*)(BoolEngine*,int)>(entry.post)(&engine,value));
        current=nullptr;
        return overridden ? boolValue : result;
    }
    void InvokeRef(RefEngine& engine,const Config& config) {
        assert(hooks.size()==1);const auto entry=hooks.begin()->second;
        current=entry.context;action=KHook::Action::Ignore;
        reinterpret_cast<void(*)(RefEngine*,const Config&)>(entry.pre)(&engine,config);
        if(action!=KHook::Action::Supersede)engine.Start(config);
        reinterpret_cast<void(*)(RefEngine*,const Config&)>(entry.post)(&engine,config);
        current=nullptr;
    }
    void InvokeMutableRefs(MutableRefEngine& engine,int amount,Config& first,Config& second) {
        assert(hooks.size()==1);const auto entry=hooks.begin()->second;
        current=entry.context;action=KHook::Action::Ignore;
        reinterpret_cast<void(*)(MutableRefEngine*,int,Config&,Config&)>(entry.pre)(&engine,amount,first,second);
        if(action!=KHook::Action::Supersede)engine.Transmit(amount,first,second);
        reinterpret_cast<void(*)(MutableRefEngine*,int,Config&,Config&)>(entry.post)(&engine,amount,first,second);
        current=nullptr;
    }
};

struct Consumer {
    int pre=0,post=0; bool block=false;
    KHook::Return<void> Pre(Engine*,bool) { ++pre; return {block?KHook::Action::Supersede:KHook::Action::Ignore}; }
    KHook::Return<void> Post(Engine*,bool) { ++post; return {KHook::Action::Ignore}; }
};

struct BoolConsumer {
    int pre=0,post=0;
    KHook::Action action=KHook::Action::Ignore;
    bool result=false;
    KHook::Return<bool> Pre(BoolEngine*,int) { ++pre; return {action,result}; }
    KHook::Return<bool> Post(BoolEngine*,int) { ++post; return {KHook::Action::Ignore,false}; }
};

struct RefConsumer {
    const Config* before=nullptr;
    const Config* after=nullptr;
    KHook::Return<void> Pre(RefEngine*,const Config& config) { before=&config;return {KHook::Action::Ignore}; }
    KHook::Return<void> Post(RefEngine*,const Config& config) { after=&config;return {KHook::Action::Ignore}; }
};

struct MutableRefConsumer {
    Config* beforeFirst=nullptr;Config* beforeSecond=nullptr;
    Config* afterFirst=nullptr;Config* afterSecond=nullptr;
    KHook::Return<void> Pre(MutableRefEngine*,int,Config& first,Config& second) {
        beforeFirst=&first;beforeSecond=&second;first.identity=10;second.identity=20;
        return {KHook::Action::Ignore};
    }
    KHook::Return<void> Post(MutableRefEngine*,int amount,Config& first,Config& second) {
        afterFirst=&first;afterSecond=&second;
        assert(first.identity==10+amount && second.identity==20+amount);
        first.identity+=100;second.identity+=200;
        return {KHook::Action::Ignore};
    }
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
    {
        boundary.reject=false; BoolEngine engine,other; BoolConsumer receiver;
        auto hook=std::make_unique<SvarogHooks::Virtual<BoolEngine,bool,int>>(
            &BoolEngine::Ready,&receiver,&BoolConsumer::Pre,&BoolConsumer::Post);
        assert(hook->AddInstance(&engine));
        assert(boundary.InvokeBool(engine,1));assert(receiver.pre==1 && receiver.post==1 && engine.originals==1);
        receiver.action=KHook::Action::Override;
        assert(!boundary.InvokeBool(engine,1));assert(engine.originals==2);
        receiver.action=KHook::Action::Supersede;receiver.result=true;
        assert(boundary.InvokeBool(engine,-1));assert(engine.originals==2 && receiver.post==3);
        assert(boundary.InvokeBool(other,1));assert(other.originals==1 && receiver.pre==3);
    }
    assert(boundary.hooks.empty() && boundary.removals==3);
    {
        RefEngine engine;RefConsumer receiver;Config config;
        auto hook=std::make_unique<SvarogHooks::Virtual<RefEngine,void,const Config&>>(
            &RefEngine::Start,&receiver,&RefConsumer::Pre,&RefConsumer::Post);
        assert(hook->AddInstance(&engine));
        boundary.InvokeRef(engine,config);
        assert(receiver.before==&config && receiver.after==&config && engine.originalConfig==&config);
    }
    assert(boundary.hooks.empty() && boundary.removals==4);
    {
        MutableRefEngine engine;MutableRefConsumer receiver;Config first,second;
        auto hook=std::make_unique<SvarogHooks::Virtual<MutableRefEngine,void,int,Config&,Config&>>(
            &MutableRefEngine::Transmit,&receiver,&MutableRefConsumer::Pre,&MutableRefConsumer::Post);
        assert(hook->AddInstance(&engine));
        boundary.InvokeMutableRefs(engine,3,first,second);
        assert(receiver.beforeFirst==&first && receiver.beforeSecond==&second);
        assert(receiver.afterFirst==&first && receiver.afterSecond==&second);
        assert(engine.originalFirst==&first && engine.originalSecond==&second);
        assert(first.identity==113 && second.identity==223);
    }
    assert(boundary.hooks.empty() && boundary.removals==5);
    KHook::__exported__khook=nullptr;
    std::cout<<"MetaMod API18 typed hooks: all checks passed\n";
}
#else
int main() { std::cout<<"MetaMod API17 build: API18 hook checks require sdk-staging\n"; }
#endif
