#pragma once
#include "CBaseEntity.h"
#include "CBaseModelEntity.h"
#include "services.h"
#include "vtable_resolve.h"

class CBasePlayerPawn : public CBaseModelEntity
{
public:
	DECLARE_SCHEMA_CLASS(CBasePlayerPawn);

	SCHEMA_FIELD(CCSPlayer_MovementServices*, m_pMovementServices)
	SCHEMA_FIELD(CCSPlayer_WeaponServices*, m_pWeaponServices)
	SCHEMA_FIELD(CCSPlayer_ItemServices*, m_pItemServices)
	SCHEMA_FIELD(CPlayer_ObserverServices*, m_pObserverServices)
	SCHEMA_FIELD(CPlayer_CameraServices*, m_pCameraServices)
	SCHEMA_FIELD(CHandle<CBasePlayerController>, m_hController)
	SCHEMA_FIELD(uint32, m_iHideHUD)
	SCHEMA_FIELD(bool, m_fInitHUD)
	SCHEMA_FIELD(QAngle, v_angle)  // The actual view angle being rendered (follows mouse movement)

    void CommitSuicide(bool bExplode, bool bForce)
	{
		// Слот сверен по таблице движка 04.08.2026: 384 → 0xad0010, начало функции
		// (пролог `push %rbp; lea …`). Прежнее значение 380 указывало на 0xb0ffa0 —
		// середину чужой команды, то есть вызов уходил в никуда.
		// Выведено якорем по консольным командам kill/explode: 0xc00/8 = 384.
		CALL_VIRTUAL_RESOLVED(void, "CBasePlayerPawn::CommitSuicide", 384, this, bExplode, bForce);
	}

	CBasePlayerController *GetController() { return m_hController.Get(); }
};