#pragma once

// Central resolver for the inventory-only weapon detach primitive used by
// CCSPlayer_WeaponServices.  The public RemoveWeapon virtual first dispatches
// the weapon's drop handler and only then calls this helper.  Custom model
// refresh must not run that drop lifecycle: it can expose a tossed entity to
// hooks/clients before the replacement exists and was the source of duplicate
// weapons after engine updates.
//
// The helper itself removes the existing handle from m_hMyWeapons, clears the
// weapon owner and fixes the active-weapon state.  It does not create or give an
// item.  Its address is found independently in the live libserver.so; when the
// body changes the resolver returns nullptr and the feature fails closed.

#include "give_resolver.h"

namespace CS2Weapon
{
	typedef bool (*DetachWeapon_t)(void* weaponServices, CBasePlayerWeapon* weapon);

	// CS2 build 2000873.  The complete entry sequence is long enough to be
	// unique while allowing relocations/branch distances to move.

	inline DetachWeapon_t DetachWeapon()
	{
		const char* SIG_DetachWeapon = FleetGamedata::current().get("Plugins/SchemaEntity/DetachWeapon");
		if (!*SIG_DetachWeapon) {
			FleetGamedata::reportUnavailable("SchemaEntity/DetachWeapon");
			return nullptr;
		}
		static DetachWeapon_t s_fn = nullptr;
		static bool s_done = false;
		if (s_done) return s_fn;
		s_done = true;

		int count = 0;
		const uintptr_t address = CS2Give::ProcMapsScan(
			"libserver.so", SIG_DetachWeapon, &count);
		if (count == 1 && address)
			s_fn = reinterpret_cast<DetachWeapon_t>(address);
		return s_fn;
	}
}
