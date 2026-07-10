#pragma once

// Custom ehandle implementation that avoids undefined symbol GetEntityIdentity
// This must be included BEFORE any SDK headers that use ehandle.h

#include "entity2/entityidentity.h"
#include "entity2/entitysystem.h"

// Forward declare
extern CEntitySystem* g_pEntitySystem;

// Custom implementation of CEntityHandle::Get() that doesn't use GetEntityIdentity
// Uses direct chunk access instead
inline CEntityInstance* CEntityHandle::Get() const
{
    if (!IsValid() || !g_pEntitySystem)
        return nullptr;

    int index = GetEntryIndex();
    if (index < 0 || index >= MAX_TOTAL_ENTITIES)
        return nullptr;

    // Direct access via identity chunks
    int chunk = index / MAX_ENTITIES_IN_LIST;
    int offset = index % MAX_ENTITIES_IN_LIST;

    CEntityIdentity* pChunk = g_pEntitySystem->m_EntityList.m_pIdentityChunks[chunk];
    if (!pChunk)
        return nullptr;

    CEntityIdentity* pIdentity = &pChunk[offset];
    if (!pIdentity || !pIdentity->m_pInstance)
        return nullptr;

    // Verify serial number matches
    if (pIdentity->m_EHandle.GetSerialNumber() != GetSerialNumber())
        return nullptr;

    return pIdentity->m_pInstance;
}

// CHandle template - inherits from CEntityHandle
template< class T >
class CHandle : public CEntityHandle
{
public:
    CHandle() {}
    CHandle( int iEntry, int iSerialNumber ) { Init(iEntry, iSerialNumber); }
    CHandle( const CEntityHandle &handle ) : CEntityHandle( handle ) {}
    CHandle( T *pVal ) { Term(); Set( pVal ); }

    static CHandle<T> FromIndex( int index )
    {
        CHandle<T> ret;
        ret.m_Index = index;
        return ret;
    }

    T* Get() const { return (T*)CEntityHandle::Get(); }
    void Set( const T* pVal ) { CEntityHandle::Set(pVal); }

    operator T*() { return Get(); }
    operator T*() const { return Get(); }

    bool operator !() const { return !Get(); }
    bool operator==( T *val ) const { return Get() == val; }
    bool operator!=( T *val ) const { return Get() != val; }
    const CEntityHandle& operator=( const T *val ) { Set( val ); return *this; }

    T* operator->() const { return Get(); }
};
