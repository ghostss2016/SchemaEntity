// Central cs2-ci/.100 only. Production vmt code and pinned KHook interface;
// the fake boundary owns only hook registry entries and the engine fixture.
#define META_NO_HL2SDK
#include <ISmmPlugin.h>
#include "../virtual.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <sys/mman.h>
#include <unistd.h>

#if !defined(__linux__) || !defined(__x86_64__)
#error This fixture requires the pinned Linux x86-64 native target.
#endif
#if METAMOD_PLAPI_VERSION < 18
#error This regression must use the pinned API18 KHook headers.
#endif
namespace KHook { IKHook* __exported__khook = nullptr; }
// Logging is an I/O boundary, not a replacement for virtual dispatch logic.
void Warning(const char*, ...) {}

struct Engine { void** table; unsigned originals=0,hooks=0; int last=0; };
using Function=int(*)(Engine*,int);
static int Original(Engine* engine,int amount) { ++engine->originals;engine->last=amount;return amount+7; }
static int HookDispatch(Engine* engine,int amount) { ++engine->hooks;return Original(engine,amount); }

class Boundary final : public KHook::IKHook {
public:
    void** ownedTable=nullptr; int ownedSlot=0; void* original=nullptr;
    bool mutate=false;
    KHook::HookID_t SetupHook(void*,void*,void*,void*,void*,void*,void*,unsigned,bool) override { assert(false);return KHook::INVALID_HOOK; }
    KHook::HookID_t SetupVirtualHook(void**,int,void*,void*,void*,void*,void*,void*,unsigned,bool) override { assert(false);return KHook::INVALID_HOOK; }
    void RemoveHook(KHook::HookID_t,bool,void(*)(KHook::HookID_t,void*),void*) override { assert(false); }
    void* GetContextPtr() override { assert(false);return nullptr; }
    void* GetOriginalFunction() override { assert(false);return nullptr; }
    void* GetOriginalValuePtr() override { assert(false);return nullptr; }
    void* GetOverrideValuePtr() override { assert(false);return nullptr; }
    void* GetCurrentValuePtr(bool) override { assert(false);return nullptr; }
    void DestroyReturnValue() override { assert(false); }
    void* DoRecall(KHook::Action,void*,std::size_t,void*,void*) override { assert(false);return nullptr; }
    void SaveReturnValue(KHook::Action,void*,std::size_t,void*,void*,bool) override { assert(false); }
    void* FindOriginal(void*) override { assert(false);return nullptr; }
    void* FindOriginalVirtual(void** table,int index) override {
        if(table!=ownedTable || index!=ownedSlot)return table[index];
        if(mutate)table[index]=original;
        return original;
    }
    void* LookupSignature(void*,std::size_t,const char*) override { assert(false);return nullptr; }
    bool WasOriginalFunctionSkipped() override { assert(false);return false; }
};

class Stub {
    void* memory_=MAP_FAILED; std::size_t size_=0;
public:
    explicit Stub(Function target) {
        const auto page=::sysconf(_SC_PAGESIZE);assert(page>0);size_=std::size_t(page);
        memory_=::mmap(nullptr,size_,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(memory_!=MAP_FAILED);
        // Test-owned SysV jump, preserving every argument register. No engine
        // offset, patch or hook implementation is written into production.
        const unsigned char jump[]={0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};
        std::memcpy(memory_,jump,sizeof(jump));const auto address=reinterpret_cast<std::uintptr_t>(target);
        std::memcpy(static_cast<unsigned char*>(memory_)+2,&address,sizeof(address));
        assert(::mprotect(memory_,size_,PROT_READ|PROT_EXEC)==0);
    }
    ~Stub(){if(memory_!=MAP_FAILED)assert(::munmap(memory_,size_)==0);}
    void* Address()const{return memory_;}
    Stub(const Stub&)=delete;Stub& operator=(const Stub&)=delete;
};
int main() {
    void* table[1]={reinterpret_cast<void*>(&Original)};Engine engine{table};
    assert(vmt::IsExecutableAddress(table[0])); // Warm production cache BEFORE mmap.
    assert(vmt::GetVMethod<Function>(0,&engine)==&Original);
    assert(vmt::CallVirtual<int>(0,&engine,3)==10 && engine.originals==1 && engine.hooks==0);
    Stub stub(&HookDispatch);void* target=stub.Address();
    assert(!vmt::IsExecutableAddress(target)); // Reproduce late KHook RX mapping.
    table[0]=target;
    assert(vmt::GetVMethod<Function>(0,&engine)==nullptr); // Unknown RX, no registry.
    Boundary boundary;KHook::__exported__khook=&boundary;
    assert(vmt::GetVMethod<Function>(0,&engine)==nullptr); // Registry does not own it.
    boundary.ownedTable=table;boundary.original=reinterpret_cast<void*>(&Original);
    assert(vmt::GetVMethod<Function>(0,&engine)==reinterpret_cast<Function>(target));
    assert(vmt::CallVirtual<int>(0,&engine,11)==18);
    assert(engine.hooks==1 && engine.originals==2 && engine.last==11); // Never bypass hook.
    boundary.original=target;
    assert(vmt::GetVMethod<Function>(0,&engine)==nullptr); // Self-original is not ownership proof.
    int data=1;boundary.original=&data;
    assert(vmt::GetVMethod<Function>(0,&engine)==nullptr); // Nonexecutable original.
    boundary.original=reinterpret_cast<void*>(&Original);boundary.mutate=true;
    assert(vmt::GetVMethod<Function>(0,&engine)==nullptr); // Slot changed during lookup.
    boundary.mutate=false;boundary.ownedTable=nullptr;table[0]=&data;
    assert(vmt::GetVMethod<Function>(0,&engine)==nullptr); // Non-code unregistered target.
    table[0]=nullptr;assert(vmt::GetVMethod<Function>(0,&engine)==nullptr);
    Engine missing{nullptr};assert(vmt::GetVMethod<Function>(0,&missing)==nullptr);
    assert(vmt::GetVMethod<Function>(0,nullptr)==nullptr);
    table[0]=target;KHook::__exported__khook=nullptr;
    assert(vmt::GetVMethod<Function>(0,&engine)==nullptr); // Provider unavailable/revoked.
    table[0]=reinterpret_cast<void*>(&Original);
    assert(vmt::CallVirtual<int>(0,&engine,19)==26 && engine.originals==3 && engine.hooks==1);
    std::cout << "virtual_hook_target_test: all checks passed\n";
}
