#pragma once

#include <ISmmPlugin.h>

#if METAMOD_PLAPI_VERSION >= 18
#include <mutex>
#include <type_traits>
#include <utility>

namespace SvarogHooks {

// The engine ABI and detour implementation remain owned by the pinned MetaMod
// KHook. This extension only adds typed, checked registration for objects and
// raw vtables (the equivalent of the former global DVP hook). No fake entity,
// copied hook implementation, vtable offset, or independent detour library.
template <class Class, class Result, class... Args>
class Virtual final : public KHook::Virtual<Class, Result, Args...> {
    using Base = KHook::Virtual<Class, Result, Args...>;

    bool RegistrationAccepted(void** table) {
        if (!table || this->_vtbl_index < 0) return false;
        std::lock_guard<std::mutex> guard(this->_hooks_stored);
        const auto entry = this->_addr_hook_ids.find(table + this->_vtbl_index);
        return entry != this->_addr_hook_ids.end() && entry->second != KHook::INVALID_HOOK;
    }

public:
    template<class... Constructor>
    explicit Virtual(Constructor&&... constructor) : Base(std::forward<Constructor>(constructor)...) {
        // Pinned KHook returns this dummy from PRE/POST trampolines even for an
        // Ignore action. Its `new Result` leaves scalar storage indeterminate;
        // initialize it without changing the real original/override value.
        if constexpr (std::is_scalar_v<Result> && !std::is_const_v<Result>)
            *this->_fake_return = Result{};
    }

    // A returned true confirms KHook accepted a registration, not that a
    // deferred detour has run or that gameplay has been tested.
    bool AddInstance(Class* instance) {
        if (!instance || !KHook::__exported__khook) return false;
        Base::Add(instance);
        if (RegistrationAccepted(*reinterpret_cast<void***>(instance))) return true;
        Base::Remove(instance);
        return false;
    }

    bool AddVtable(void** table) {
        if (!table || !KHook::__exported__khook || this->_vtbl_index < 0) return false;
        {
            std::lock_guard<std::mutex> guard(this->_m_hooked_this);
            this->_hooked_global.insert(table);
        }
        this->_Setup(table);
        if (RegistrationAccepted(table)) return true;
        RemoveVtable(table);
        return false;
    }

    void RemoveVtable(void** table) {
        std::lock_guard<std::mutex> guard(this->_m_hooked_this);
        this->_hooked_global.erase(table);
    }
};

} // namespace SvarogHooks
#endif
