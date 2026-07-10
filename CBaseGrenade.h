#pragma once
#include "CBaseModelEntity.h"
#include "CBasePlayerPawn.h"
#include "globaltypes.h"

class CBaseGrenade : public CBaseModelEntity
{
public:
	DECLARE_SCHEMA_CLASS(CBaseGrenade);
	SCHEMA_FIELD(CHandle<CCSPlayerPawn>, m_hThrower);
	SCHEMA_FIELD(float, m_flDamage);
	SCHEMA_FIELD(float, m_DmgRadius);
	SCHEMA_FIELD(GameTime_t, m_flDetonateTime);
};

class CBaseCSGrenadeProjectile : public CBaseGrenade
{
public:
	DECLARE_SCHEMA_CLASS(CBaseCSGrenadeProjectile);
	SCHEMA_FIELD(int32, m_nTicksAtZeroVelocity);
	SCHEMA_FIELD(int32, m_nBounces);
};

class CDecoyProjectile : public CBaseCSGrenadeProjectile
{
public:
	DECLARE_SCHEMA_CLASS(CDecoyProjectile);
};

class CSmokeGrenadeProjectile : public CBaseCSGrenadeProjectile
{
public:
	DECLARE_SCHEMA_CLASS(CSmokeGrenadeProjectile);
	SCHEMA_FIELD(Vector, m_vSmokeColor);
	SCHEMA_FIELD(bool, m_bDidSmokeEffect);
	SCHEMA_FIELD(int, m_nSmokeEffectTickBegin);
};

class CHEGrenadeProjectile : public CBaseCSGrenadeProjectile
{
public:
	DECLARE_SCHEMA_CLASS(CHEGrenadeProjectile);
};

class CFlashbangProjectile : public CBaseCSGrenadeProjectile
{
public:
	DECLARE_SCHEMA_CLASS(CFlashbangProjectile);
};

class CMolotovProjectile : public CBaseCSGrenadeProjectile
{
public:
	DECLARE_SCHEMA_CLASS(CMolotovProjectile);
	SCHEMA_FIELD(bool, m_bIsIncGrenade);
};