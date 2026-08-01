/**
 * =============================================================================
 * CS2Fixes
 * Copyright (C) 2023-2025 Source2ZE
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#include "schemasystem.h"

#include "platform.h"
#include "schemasystem/schemasystem.h"
#include "tier1/utlmap.h"
#include "tier0/memdbgon.h"
#include <edict.h>
#include <CBaseEntity.h>

#ifdef _WIN32
#define MODULE_PREFIX ""
#define MODULE_EXT ".dll"
#else
#define MODULE_PREFIX "lib"
#define MODULE_EXT ".so"
#endif

using SchemaKeyValueMap_t = std::map<uint32_t, SchemaKey>;
using SchemaTableMap_t = std::map<uint32_t, SchemaKeyValueMap_t>;

static constexpr uint32_t g_ChainKey = hash_32_fnv1a_const("__m_pChainEntity");

static bool IsFieldNetworked(SchemaClassFieldData_t& field)
{
	for (int i = 0; i < field.m_nStaticMetadataCount; i++)
	{
		static auto networkEnabled = hash_32_fnv1a_const("MNetworkEnable");
		if (networkEnabled == hash_32_fnv1a_const(field.m_pStaticMetadata[i].m_pszName))
			return true;
	}

	return false;
}

// Try to recursively find __m_pChainEntity in base classes 
// (e.g. CCSGameRules -> CTeamplayRules -> CMultiplayRules -> CGameRules, in this case it's in CGameRules)
static void InitChainOffset(SchemaClassInfoData_t *pClassInfo, SchemaKeyValueMap_t &keyValueMap)
{
	short fieldsSize = pClassInfo->m_nFieldCount;
	SchemaClassFieldData_t* pFields = pClassInfo->m_pFields;

	for (int i = 0; i < fieldsSize; ++i)
	{
		SchemaClassFieldData_t& field = pFields[i];
		
		if (hash_32_fnv1a_const(field.m_pszName) != g_ChainKey)
			continue;

		std::pair<uint32_t, SchemaKey> keyValuePair;
		keyValuePair.first = g_ChainKey;
		keyValuePair.second.offset = field.m_nSingleInheritanceOffset;
		keyValuePair.second.networked = IsFieldNetworked(field);

		keyValueMap.insert(keyValuePair);
		return;
	}

	// Not the base class yet, keep looking
	if (pClassInfo->m_nBaseClassCount)
		return InitChainOffset(pClassInfo->m_pBaseClasses[0].m_pClass, keyValueMap);
}

static void InitSchemaKeyValueMap(SchemaClassInfoData_t *pClassInfo, SchemaKeyValueMap_t& keyValueMap)
{
	short fieldsSize = pClassInfo->m_nFieldCount;
	SchemaClassFieldData_t* pFields = pClassInfo->m_pFields;

	for (int i = 0; i < fieldsSize; ++i)
	{
		SchemaClassFieldData_t& field = pFields[i];

		std::pair<uint32_t, SchemaKey> keyValuePair;
		keyValuePair.first = hash_32_fnv1a_const(field.m_pszName);
		keyValuePair.second.offset = field.m_nSingleInheritanceOffset;
		keyValuePair.second.networked = IsFieldNetworked(field);

		keyValueMap.insert(keyValuePair);
	}

	// If this is a child class there might be a parent class with __m_pChainEntity
	if (keyValueMap.find(g_ChainKey) == keyValueMap.end() && pClassInfo->m_nBaseClassCount)
		InitChainOffset(pClassInfo->m_pBaseClasses[0].m_pClass, keyValueMap);
}

static bool InitSchemaFieldsForClass(SchemaTableMap_t& tableMap, const char* className, uint32_t classKey)
{
	if (!g_pSchemaSystem)
		return false;

	CSchemaSystemTypeScope* pType = g_pSchemaSystem->FindTypeScopeForModule(MODULE_PREFIX "server" MODULE_EXT);

	if (!pType)
		return false;

	SchemaClassInfoData_t* pClassInfo = pType->FindDeclaredClass(className).Get();

	if (!pClassInfo)
	{
		SchemaKeyValueMap_t map;
		tableMap.insert(std::make_pair(classKey, map));

		Warning("InitSchemaFieldsForClass(): '%s' was not found!\n", className);
		return false;
	}

	SchemaKeyValueMap_t& keyValueMap = tableMap.insert(std::make_pair(classKey, SchemaKeyValueMap_t())).first->second;

	InitSchemaKeyValueMap(pClassInfo, keyValueMap);

	return true;
}

int16_t schema::FindChainOffset(const char* className, uint32_t classNameHash)
{
	return schema::GetOffset(className, classNameHash, "__m_pChainEntity", g_ChainKey).offset;
}

int16_t schema::FindChainOffset(const char* className)
{
    if (!g_pSchemaSystem)
        return 0;

    CSchemaSystemTypeScope* pType = g_pSchemaSystem->FindTypeScopeForModule(MODULE_PREFIX "server" MODULE_EXT);

    if (!pType)
        return 0;

    SchemaClassInfoData_t* pClassInfo = pType->FindDeclaredClass(className).Get();

    if (!pClassInfo)
        return 0;

    do
    {
        SchemaClassFieldData_t* pFields = pClassInfo->m_pFields;
        short fieldsSize = pClassInfo->m_nFieldCount;
        for (int i = 0; i < fieldsSize; ++i)
        {
            SchemaClassFieldData_t& field = pFields[i];

            if (V_strcmp(field.m_pszName, "__m_pChainEntity") == 0)
            {
                return field.m_nSingleInheritanceOffset;
            }
        }
    } while ((pClassInfo = pClassInfo->m_pBaseClasses ? pClassInfo->m_pBaseClasses->m_pClass : nullptr) != nullptr);

    return 0;
}

SchemaKey schema::GetOffset(const char* className, uint32_t classKey, const char* memberName, uint32_t memberKey)
{
	static SchemaTableMap_t schemaTableMap;

	if (schemaTableMap.find(classKey) == schemaTableMap.end())
	{
		if (InitSchemaFieldsForClass(schemaTableMap, className, classKey))
			return GetOffset(className, classKey, memberName, memberKey);

		return {0, 0};
	}

	SchemaKeyValueMap_t tableMap = schemaTableMap[classKey];

	if (tableMap.find(memberKey) == tableMap.end())
	{
		if (memberKey != g_ChainKey)
			Warning("schema::GetOffset(): '%s' was not found in '%s'!\n", memberName, className);

		return {0, 0};
	}

	return tableMap[memberKey];
}

int32_t schema::GetServerOffset(const char* pszClassName, const char* pszPropName)
{
    if (!g_pSchemaSystem)
        return -1;

    CSchemaSystemTypeScope* pType = g_pSchemaSystem->FindTypeScopeForModule(MODULE_PREFIX "server" MODULE_EXT);
    if (!pType)
        return -1;

    SchemaClassInfoData_t* pClassInfo = pType->FindDeclaredClass(pszClassName).Get();
    if (pClassInfo)
    {
        for (int i = 0; i < pClassInfo->m_nFieldCount; i++)
        {
            auto& pFieldData = pClassInfo->m_pFields[i];

            if (std::strcmp(pFieldData.m_pszName, pszPropName) == 0)
            {
                return pFieldData.m_nSingleInheritanceOffset;
            }
        }
    }

    return -1;
}

// ============================================================================
// [01.08.2026] Снимок всей схемы движка. Зачем — см. объявление в schemasystem.h.
// ============================================================================
int schema::DumpAllToFile(const char* pszPath)
{
    if (!g_pSchemaSystem || !pszPath || !pszPath[0])
        return 0;

    CSchemaSystemTypeScope* pType = g_pSchemaSystem->FindTypeScopeForModule(MODULE_PREFIX "server" MODULE_EXT);
    if (!pType)
        return 0;

    FILE* fp = fopen(pszPath, "w");
    if (!fp)
        return 0;

    auto& classes = pType->m_DeclaredClasses.m_Map;

    fprintf(fp, "# Снимок схемы движка. Строки: КЛАСС имя размер=N полей=M, затем «смещение имя».\n");
    fprintf(fp, "# Сравнивать снимки до и после обновления игры обычным diff.\n");
    fprintf(fp, "# Классов в схеме: %u\n", classes.Count());

    int nDumped = 0;
    for (auto i = classes.FirstInorder(); i != classes.InvalidIndex(); i = classes.NextInorder(i))
    {
        CSchemaType_DeclaredClass* pDecl = classes.Element(i);
        if (!pDecl || !pDecl->m_pClassInfo)
            continue;

        // m_pClassInfo объявлен непрозрачным типом, но лежит по нему именно этот блок данных:
        // на него же указывает обратная ссылка m_pDeclaredClass. Так к нему обращается и сам SDK.
        SchemaClassInfoData_t* pInfo = reinterpret_cast<SchemaClassInfoData_t*>(pDecl->m_pClassInfo);
        if (!pInfo || !pInfo->m_pszName)
            continue;

        // Защита от мусора: если счётчик полей неправдоподобен, класс пропускаем целиком,
        // а не идём по нему в цикле. Снимок не должен уронить сервер, ради которого он снят.
        const uint16 nFields = pInfo->m_nFieldCount;
        if (nFields > 0 && !pInfo->m_pFields)
            continue;

        fprintf(fp, "КЛАСС %s размер=%d полей=%u\n", pInfo->m_pszName, pInfo->m_nSize, (unsigned)nFields);

        for (uint16 f = 0; f < nFields; ++f)
        {
            const SchemaClassFieldData_t& fld = pInfo->m_pFields[f];
            if (!fld.m_pszName)
                continue;
            fprintf(fp, "  %d %s\n", fld.m_nSingleInheritanceOffset, fld.m_pszName);
        }

        ++nDumped;
    }

    fprintf(fp, "# Выгружено классов: %d\n", nDumped);
    fclose(fp);
    return nDumped;
}

void NetworkVarStateChanged(uintptr_t pNetworkVar, uint32_t nOffset, uint32 nNetworkStateChangedOffset)
{
	NetworkStateChangedData data(nOffset);
	CALL_VIRTUAL(void, nNetworkStateChangedOffset, (void*)pNetworkVar, &data);
}

void EntityNetworkStateChanged(uintptr_t pEntity, uint nOffset)
{
	NetworkStateChangedData data(nOffset);
	reinterpret_cast<CEntityInstance*>(pEntity)->NetworkStateChanged(data);
}

void ChainNetworkStateChanged(uintptr_t pNetworkVarChainer, uint nLocalOffset)
{
    CEntityInstance* pEntity = reinterpret_cast<CNetworkVarChainer2*>(pNetworkVarChainer)->m_pEntity;

    if (pEntity)
		// NetworkStateChanged_t WENDER SDK
		// NetworkStateChangedData HL2SDK-CS2
        pEntity->NetworkStateChanged(NetworkStateChangedData(nLocalOffset, -1, reinterpret_cast<CNetworkVarChainer2*>(pNetworkVarChainer)->m_PathIndex));
}