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

enum class WEAPON_CLASS;
enum class WEAPON_SUBCLASS;
struct BaseObject;
struct Weapon;
struct WeaponStats;


/* Fire a weapon at something added int weapon_slot*/
bool combFire(Weapon* psWeap, BaseObject* psAttacker,
              BaseObject* psTarget, int weapon_slot);

/*checks through the target players list of structures and droids to see
if any support a counter battery sensor*/
void counterBatteryFire(BaseObject* psAttacker, BaseObject* psTarget);

int objDamage(BaseObject* psObj, unsigned damage, unsigned originalhp,
              WEAPON_CLASS weaponClass, WEAPON_SUBCLASS weaponSubClass,
              bool isDamagePerSecond, int minDamage);

unsigned objGuessFutureDamage(WeaponStats const* psStats, unsigned player, BaseObject const* psTarget);

int objArmour(const BaseObject* psObj, WEAPON_CLASS weaponClass);

#endif // __INCLUDED_SRC_COMBAT_H__
