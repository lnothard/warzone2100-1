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
 * @file weapondef.h
 * Definitions for the weapons
 */

#ifndef __INCLUDED_WEAPONDEF_H__
#define __INCLUDED_WEAPONDEF_H__

enum class WEAPON_SUBCLASS;

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

class Weapon : public virtual SimpleObject, public Impl::SimpleObject
{
public:
  /* Accessors */
  [[nodiscard]] const WeaponStats& getStats() const;
  [[nodiscard]] unsigned getRecoil() const;
  [[nodiscard]] unsigned getMaxRange(unsigned player) const;
  [[nodiscard]] unsigned getMinRange(unsigned player) const;
  [[nodiscard]] unsigned getShortRange(unsigned player) const;
  [[nodiscard]] unsigned getHitChance(unsigned player) const;
  [[nodiscard]] unsigned getShortRangeHitChance(unsigned player) const;
  [[nodiscard]] unsigned getNumAttackRuns(unsigned player) const;
  [[nodiscard]] unsigned getShotsFired() const noexcept;
  [[nodiscard]] const iIMDShape& getImdShape() const;
  [[nodiscard]] const iIMDShape& getMountGraphic() const;
  [[nodiscard]] WEAPON_SUBCLASS getSubclass() const;
  [[nodiscard]] TARGET_ORIGIN getTargetOrigin() const;

  [[nodiscard]] bool hasAmmo() const;
  [[nodiscard]] bool hasFullAmmo() const noexcept;
  [[nodiscard]] bool isArtillery() const noexcept;
  [[nodiscard]] bool isVtolWeapon() const;
  [[nodiscard]] bool isEmptyVtolWeapon(unsigned player) const;
  [[nodiscard]] unsigned calculateRateOfFire(unsigned player) const;
private:
  using enum TARGET_ORIGIN;

	unsigned ammo = 0;
  std::shared_ptr<WeaponStats> stats;

  /// The game time when this weapon last fired
	std::size_t timeLastFired = 0;

	unsigned shotsFired = 0;
	Rotation previousRotation {0, 0, 0};
	unsigned ammoUsed = 0;
	TARGET_ORIGIN origin = UNKNOWN;
};

/// Returns how much the weapon assembly should currently
/// be rocked back due to firing.
int getRecoil(Weapon const& weapon);

#endif // __INCLUDED_WEAPONDEF_H__
