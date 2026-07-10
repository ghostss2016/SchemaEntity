#pragma once
#include "schemasystem.h"

class CCSPlayer_AimPunchServices
{
public:
	DECLARE_SCHEMA_CLASS(CCSPlayer_AimPunchServices);

	SCHEMA_FIELD(int32_t, m_predictableBaseTick)
	SCHEMA_FIELD(float, m_predictableBaseTickInterpAmount)
	SCHEMA_FIELD(QAngle, m_predictableBaseAngle)
	SCHEMA_FIELD(QAngle, m_predictableBaseAngleVel)
	SCHEMA_FIELD(int32_t, m_unpredictableBaseTick)
	SCHEMA_FIELD(QAngle, m_unpredictableBaseAngle)
};
