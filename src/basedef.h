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
 * @file basedef.h
 * Definitions for the base object type
 */

#ifndef __INCLUDED_BASEDEF_H__
#define __INCLUDED_BASEDEF_H__

#include <array>
#include <bitset>

#include "weapon.h"

struct DisplayData;


static constexpr auto LINE_OF_FIRE_MINIMUM = 5;
static constexpr auto TURRET_ROTATION_RATE = 45;
static constexpr auto PROJ_MAX_PITCH = 45;

/// The maximum number of weapons attached to a single unit
static constexpr auto MAX_WEAPONS = 3;


enum class OBJECT_FLAG
{
  JAMMED_TILES,
  TARGETED,
  DIRTY,
  UNSELECTABLE,
  COUNT // MUST BE LAST
};

struct TILEPOS
{
  uint8_t x, y, type;
};

/// 4D spacetime coordinate and orientation
struct Spacetime
{
  Spacetime() = default;
  Spacetime(unsigned time, Position position, Rotation rotation);

  unsigned time = 0;
  Position position {0, 0, 0};
  Rotation rotation {0, 0,0};
};

class DamageManager {
public:
  ~DamageManager() = default;
  DamageManager();

  DamageManager(DamageManager const& rhs);
  DamageManager & operator=(DamageManager const& rhs);

  DamageManager(DamageManager && rhs) noexcept = default;
  DamageManager & operator=(DamageManager && rhs) noexcept = default;

  void setHp(unsigned hp);
  void setOriginalHp(unsigned hp);
  void setSelected(bool sel);
  void setResistance(unsigned res);
  void setExpectedDamageDirect(unsigned damage);
  void setExpectedDamageIndirect(unsigned damage);
  void setLastHitWeapon(WEAPON_SUBCLASS weap);
  void setPeriodicalDamage(unsigned damage);
  void setPeriodicalDamageStartTime(unsigned time);
  void setTimeOfDeath(unsigned t);
  [[nodiscard]] bool isSelected() const;
  [[nodiscard]] unsigned getHp() const;
  [[nodiscard]] unsigned getOriginalHp() const;
  [[nodiscard]] unsigned getResistance() const;
  [[nodiscard]] unsigned getExpectedDamageDirect() const;
  [[nodiscard]] unsigned getExpectedDamageIndirect() const;
  [[nodiscard]] WEAPON_SUBCLASS getLastHitWeapon() const;
  [[nodiscard]] unsigned getPeriodicalDamage() const;
  [[nodiscard]] unsigned getPeriodicalDamageStartTime() const;
  [[nodiscard]] bool isDead() const;
  [[nodiscard]] bool isProbablyDoomed(bool isDirectDamage) const;
private:
  struct Impl;
  std::unique_ptr<Impl> pimpl;
};

class PlayerManager {
public:
  ~PlayerManager() = default;
  explicit PlayerManager(unsigned player);

  PlayerManager(PlayerManager const& rhs);
  PlayerManager & operator=(PlayerManager const& rhs);

  PlayerManager(PlayerManager && rhs) noexcept = default;
  PlayerManager & operator=(PlayerManager && rhs) noexcept = default;

  void setPlayer(unsigned plr);
  [[nodiscard]] unsigned getPlayer() const;
  [[nodiscard]] bool isSelectedPlayer() const;
private:
  struct Impl;
  std::unique_ptr<Impl> pimpl;
};

/// The base type specification inherited by all game entities
class BaseObject
{
public:
  virtual ~BaseObject() = default;
  explicit BaseObject(unsigned id);
  BaseObject(unsigned id, std::unique_ptr<PlayerManager> playerManager);
  BaseObject(unsigned id, std::unique_ptr<DamageManager> damageManager);
  BaseObject(unsigned id,
             std::unique_ptr<PlayerManager> playerManager,
             std::unique_ptr<DamageManager> damageManager);

  BaseObject(BaseObject const& rhs);
  BaseObject& operator=(BaseObject const& rhs);

  BaseObject(BaseObject&& rhs) noexcept = default;
  BaseObject& operator=(BaseObject&& rhs) noexcept = default;

  [[nodiscard]] virtual int objRadius() const = 0;

  [[nodiscard]] unsigned getId() const noexcept;
  [[nodiscard]] Spacetime getSpacetime() const noexcept;
  [[nodiscard]] Position getPosition() const noexcept;
  [[nodiscard]] Rotation getRotation() const noexcept;
  [[nodiscard]] unsigned getTime() const noexcept;
  [[nodiscard]] const DisplayData* getDisplayData() const noexcept;
  [[nodiscard]] Spacetime getPreviousLocation() const noexcept;
  [[nodiscard]] uint8_t isVisibleToPlayer(unsigned player) const;
  [[nodiscard]] uint8_t isVisibleToSelectedPlayer() const;
  [[nodiscard]] bool testFlag(size_t pos) const;
  void setVisibleToPlayer(unsigned player, uint8_t vis);
  void setHidden();
  void setFlag(size_t pos, bool val);
  void setTime(unsigned t) noexcept;
  void setRotation(Rotation rot) noexcept;
  void setPosition(Position pos) noexcept;
  void setHeight(int height) noexcept;
  void setPreviousLocation(Spacetime prevLoc);
public:
  std::unique_ptr<DamageManager> damageManager;
  std::unique_ptr<PlayerManager> playerManager;
private:
  struct Impl;
  std::unique_ptr<Impl> pimpl;
};


int objectPositionSquareDiff(const Position& first, const Position& second);
int objectPositionSquareDiff(const BaseObject* first, const BaseObject* second);

Vector3i calculateMuzzleBaseLocation(const BaseObject& unit, int weapon_slot);

Vector3i calculateMuzzleTipLocation(const BaseObject& unit, int weapon_slot);

void checkAngle(int64_t& angle_tan, int start_coord, int height,
                int square_distance, int target_height, bool is_direct);

[[nodiscard]] bool hasFullAmmo(const BaseObject& unit) noexcept;

/// @return `true` if `unit` has an indirect weapon attached
[[nodiscard]] bool hasArtillery(const BaseObject& unit) noexcept;

/// @return `true` if `unit` has an electronic weapon attached
[[nodiscard]] bool hasElectronicWeapon(const BaseObject& unit) noexcept;

/**
 * @return `true` if `unit` may fire upon `target` with the weapon in
 *    `weapon_slot`
 */
[[nodiscard]] bool targetInLineOfFire(const BaseObject& unit,
                                      const BaseObject& target,
                                      int weapon_slot);

/**
 *
 * @param walls_block `true` if
 * @param is_direct `false` if this is an artillery weapon
 * @return
 */
[[nodiscard]] int calculateLineOfFire(const BaseObject& unit, const BaseObject& target,
                                      int weapon_slot, bool walls_block = true, bool is_direct = true);


[[nodiscard]] unsigned getMaxWeaponRange(const BaseObject& unit);

[[nodiscard]] size_t numWeapons(const BaseObject& unit);

#endif // __INCLUDED_BASEDEF_H__
