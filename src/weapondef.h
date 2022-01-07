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

#include "basedef.h"

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
  [[nodiscard]] bool has_ammo() const;
  [[nodiscard]] bool has_full_ammo() const noexcept;
  [[nodiscard]] bool is_artillery() const noexcept;
  [[nodiscard]] bool is_vtol_weapon() const;
  [[nodiscard]] bool is_empty_vtol_weapon(unsigned player) const;
  [[nodiscard]] const WeaponStats& get_stats() const;
  [[nodiscard]] unsigned get_recoil() const;
  [[nodiscard]] unsigned get_max_range(unsigned player) const;
  [[nodiscard]] unsigned get_min_range(unsigned player) const;
  [[nodiscard]] unsigned get_short_range(unsigned player) const;
  [[nodiscard]] unsigned get_hit_chance(unsigned player) const;
  [[nodiscard]] unsigned get_short_range_hit_chance(unsigned player) const;
  [[nodiscard]] unsigned get_num_attack_runs(unsigned player) const;
  [[nodiscard]] unsigned get_shots_fired() const noexcept;
  [[nodiscard]] const iIMDShape& get_IMD_shape() const;
  [[nodiscard]] const iIMDShape& get_mount_graphic() const;
  [[nodiscard]] WEAPON_SUBCLASS get_subclass() const;
  [[nodiscard]] unsigned calculate_rate_of_fire(unsigned player) const;
private:
  using enum TARGET_ORIGIN;

  /// Index into the asWeaponStats global array
	unsigned nStat = 0;

	unsigned ammo = 0;
  std::shared_ptr<WeaponStats> stats;

  /// The game time when this weapon last fired
	std::size_t time_last_fired = 0;

	unsigned shots_fired = 0;
	Rotation rotation {0, 0, 0};
	Rotation previous_rotation {0, 0, 0};
	unsigned ammo_used = 0;
	TARGET_ORIGIN origin = UNKNOWN;
};

/// Returns how much the weapon assembly should currently
/// be rocked back due to firing.
int getRecoil(Weapon const& weapon);

#endif // __INCLUDED_WEAPONDEF_H__
