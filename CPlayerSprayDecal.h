#pragma once

#include "CBaseEntity.h"
#include "schemasystem.h"

// CPlayerSprayDecal - Graffiti spray decal entity
// Schema from CPlayerSprayDecal : CModelPointEntity

class CPlayerSprayDecal : public CBaseEntity
{
public:
    DECLARE_SCHEMA_CLASS(CPlayerSprayDecal);

    // m_nUniqueID - Unique identifier for this spray
    SCHEMA_FIELD(int32_t, m_nUniqueID);

    // m_unAccountID - Steam Account ID of the player who sprayed
    SCHEMA_FIELD(uint32_t, m_unAccountID);

    // m_unTraceID - Trace identifier
    SCHEMA_FIELD(uint32_t, m_unTraceID);

    // m_rtGcTime - Game client time
    SCHEMA_FIELD(uint32_t, m_rtGcTime);

    // m_vecEndPos - End position of the spray (on wall)
    SCHEMA_FIELD(Vector, m_vecEndPos);

    // m_vecStart - Start position (player eye position)
    SCHEMA_FIELD(Vector, m_vecStart);

    // m_vecLeft - Left vector for orientation
    SCHEMA_FIELD(Vector, m_vecLeft);

    // m_vecNormal - Surface normal
    SCHEMA_FIELD(Vector, m_vecNormal);

    // m_nPlayer - Player slot index
    SCHEMA_FIELD(int32_t, m_nPlayer);

    // m_nEntity - Entity index this decal is on
    SCHEMA_FIELD(int32_t, m_nEntity);

    // m_nHitbox - Hitbox index if on a model
    SCHEMA_FIELD(int32_t, m_nHitbox);

    // m_flCreationTime - When this spray was created
    SCHEMA_FIELD(float, m_flCreationTime);

    // m_nTintID - Graffiti tint/color ID
    SCHEMA_FIELD(int32_t, m_nTintID);

    // m_nVersion - Version number
    SCHEMA_FIELD(uint8_t, m_nVersion);

    // m_ubSignature[128] - Signature data
    // Note: Fixed array, access via SCHEMA_FIELD_POINTER if needed
};
