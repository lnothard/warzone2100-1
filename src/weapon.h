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
 * @file weapon.h
 * Definitions for the weapons
 */

#ifndef __INCLUDED_WEAPON_H__
#define __INCLUDED_WEAPON_H__

#include "lib/framework/fixedpoint.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/ivisdef.h"

#include "basedef.h"
#include "stats.h"


static constexpr auto DEFAULT_RECOIL_TIME	= GAME_TICKS_PER_SEC / 4;

/// Maximum difference in direction for a fixed turret to fire
static constexpr auto FIXED_TURRET_DIR = DEG(1);

/// Percentage at which a unit is considered to be heavily damaged
static constexpr auto HEAVY_DAMAGE_LEVEL = 25;


/// Who specified the target?
enum class TARGET_ORIGIN
{
	UNKNOWN,
	PLAYER,
	VISUAL,
	ALLY,
	COMMANDER,
	SENSOR,
	CB_SENSOR,
	AIR_DEFENSE_SENSOR,
	RADAR_DETECTOR,
};

class Weapon : public BaseObject
{
public:
  Weapon() = default;
  Weapon(unsigned id, Player* player);

  [[nodiscard]] iIMDShape const* getMountGraphic() const;
  [[nodiscard]] iIMDShape const* getImdShape() const override;
  [[nodiscard]] unsigned getNumAttackRuns(unsigned player) const;
  [[nodiscard]] unsigned getMaxRange(unsigned player) const;
  [[nodiscard]] unsigned getMinRange(unsigned player) const;
  [[nodiscard]] unsigned getShortRange(unsigned player) const;
  [[nodiscard]] unsigned getHitChance(unsigned player) const;
  [[nodiscard]] unsigned getShortRangeHitChance(unsigned player) const;
  [[nodiscard]] unsigned getRecoil() const;
  [[nodiscard]] bool hasFullAmmo() const noexcept;
  [[nodiscard]] bool isArtillery() const noexcept;
  [[nodiscard]] bool isVtolWeapon() const;
  [[nodiscard]] bool isEmptyVtolWeapon(unsigned player) const;
  [[nodiscard]] unsigned calculateRateOfFire(unsigned player) const;
  void alignTurret();
public:
  std::shared_ptr<WeaponStats> stats;
  Rotation previousRotation {0, 0, 0};
  TARGET_ORIGIN origin = TARGET_ORIGIN::UNKNOWN;
  unsigned timeLastFired = 0;
  unsigned ammo = 0;
  unsigned ammoUsed = 0;
  unsigned shotsFired = 0;
};

class WeaponManager
{
public:
  std::array<Weapon, MAX_WEAPONS> weapons;
};

#endif // __INCLUDED_WEAPONDEF_H__
