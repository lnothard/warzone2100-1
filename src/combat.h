/*
	This file is part of Warzone 2100.
	Copyright (C) 1999-2004  Eidos Interactive
	Copyright (C) 2005-2020  Warzone 2100 Project

	Warzone 2100 is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Warzone 2100 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Warzone 2100; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/

/**
 * @file combat.h
 * Interface to the combat routines
 */

#ifndef __INCLUDED_SRC_COMBAT_H__
#define __INCLUDED_SRC_COMBAT_H__

#include "weapondef.h"

/* Fire a weapon at something added int weapon_slot*/
bool combFire(Weapon* psWeap, SimpleObject* psAttacker,
              SimpleObject* psTarget, int weapon_slot);

/*checks through the target players list of structures and droids to see
if any support a counter battery sensor*/
void counterBatteryFire(SimpleObject* psAttacker, SimpleObject* psTarget);

int objDamage(SimpleObject* psObj, unsigned damage, unsigned originalhp,
              WEAPON_CLASS weaponClass,WEAPON_SUBCLASS weaponSubClass,
              bool isDamagePerSecond, int minDamage);

unsigned objGuessFutureDamage(WeaponStats* psStats, unsigned player, SimpleObject* psTarget);

int objArmour(const SimpleObject* psObj, WEAPON_CLASS weaponClass);

#endif // __INCLUDED_SRC_COMBAT_H__
