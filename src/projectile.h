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

#ifndef __INCLUDED_SRC_PROJECTILE_H__
#define __INCLUDED_SRC_PROJECTILE_H__

#include <vector>

#include "3rdparty/glm/glm/fwd.hpp"
#include "lib/framework/vector.h"
#include "lib/gamelib/gtime.h"
#include "wzmaplib/map.h"

#include "basedef.h"

enum class WEAPON_CLASS;
enum class WEAPON_EFFECT;
enum class WEAPON_SUBCLASS;
struct ConstructedObject;
struct Weapon;
struct WeaponStats;


static constexpr auto PROJ_ULTIMATE_PITCH = 80;

/// How long an object burns for after leaving a fire
static constexpr auto BURN_TIME	= 10000;

/// How much damage per second an object takes when it is burning
static constexpr auto BURN_DAMAGE	= 15;

/// Least percentage of damage an object takes when burning
static constexpr auto BURN_MIN_DAMAGE = 30;

/// Downward force against projectiles
static constexpr auto ACC_GRAVITY = 1000;

/// How long to display a single electronic warfare shimmer
static constexpr auto ELEC_DAMAGE_DURATION = GAME_TICKS_PER_SEC / 5;

static constexpr auto VTOL_HITBOX_MODIFIER = 100;
static constexpr auto HOMINGINDIRECT_HEIGHT_MIN = 200;
static constexpr auto HOMINGINDIRECT_HEIGHT_MAX = 450;

static std::array<int, MAX_PLAYERS> experienceGain;

/// The range for neighbouring objects
static constexpr auto PROJ_NEIGHBOUR_RANGE = TILE_UNITS * 4;


/// Represents the current stage of a projectile's trajectory
enum class PROJECTILE_STATE
{
  INFLIGHT,
  IMPACT,
  POST_IMPACT,
  INACTIVE
};

class Projectile : public BaseObject
                 , public PlayerOwned
{
public:
  ~Projectile() override = default;
  Projectile(unsigned id, unsigned  player);

  Projectile(Projectile const& rhs);
  Projectile& operator=(Projectile const& rhs);

  Projectile(Projectile&& rhs) noexcept = default;
  Projectile& operator=(Projectile&& rhs) noexcept = default;


  [[nodiscard]] PROJECTILE_STATE getState() const noexcept;
  [[nodiscard]] bool isVisible() const;
  void update();
  void setTarget(BaseObject* psObj);
  void setSource(BaseObject* psObj);
  void proj_InFlightFunc();
  void proj_ImpactFunc();
  void proj_PostImpactFunc();
  void proj_checkPeriodicalDamage();
  bool proj_SendProjectileAngled(Weapon* psWeap, BaseObject* psAttacker, unsigned plr,
                                 Vector3i dest, BaseObject* psTarget, bool bVisible,
                                 int weapon_slot, int min_angle, unsigned fireTime) const;
  /// Update the source experience after a target is damaged/destroyed
  void updateExperience(unsigned experienceInc);
private:
  struct Impl;
  std::unique_ptr<Impl> pimpl;
};

class ObjectShape
{
public:
  ~ObjectShape() = default;

  ObjectShape();
  ObjectShape(int width, int breadth);
  explicit ObjectShape(int radius);
  explicit ObjectShape(Vector2i size);

  ObjectShape(ObjectShape const& rhs);
  ObjectShape& operator=(ObjectShape const& rhs);

  ObjectShape(ObjectShape&& rhs) noexcept = default;
  ObjectShape& operator=(ObjectShape&& rhs) noexcept = default;

  [[nodiscard]] int radius() const;
private:
  struct Impl;
  std::unique_ptr<Impl> pimpl;
};

struct Interval
{
    [[nodiscard]] bool isEmpty() const;

    /// Time 1 = 0, time 2 = 1024. Or begin >= end if empty
    int begin, end;
};

class Damage
{
public:
   ~Damage() = default;
   Damage();

   Damage(Damage const& rhs);
   Damage& operator=(Damage const& rhs);

   Damage(Damage&& rhs) noexcept = default;
   Damage& operator=(Damage&& rhs) noexcept = default;

  [[nodiscard]] bool isFriendlyFire() const;
  [[nodiscard]] bool shouldIncreaseExperience() const;
  [[nodiscard]] int objectDamage();
  void updateKills();
private:
  friend class Projectile;
  struct Impl;
  std::unique_ptr<Impl> pimpl;
};

extern PlayerOwnedObject * g_pProjLastAttacker; ///< The last unit that did damage - used by script functions

void proj_InitSystem(); ///< Initialize projectiles subsystem.
void proj_UpdateAll(); ///< Frame update for projectiles.
bool proj_Shutdown(); ///< Shut down projectile subsystem.

Projectile* proj_GetFirst(); ///< Get first projectile in the list.
Projectile* proj_GetNext(); ///< Get next projectile in the list.

void proj_FreeAllProjectiles(); ///< Free all projectiles in the list.

void setExpGain(unsigned player, int gain);
int getExpGain(unsigned player);

/// Calculate the initial velocities of an indirect projectile. Returns the flight time.
int projCalcIndirectVelocities(int32_t dx, int32_t dz, int32_t v, int32_t* vx, int32_t* vz,
                                   int min_angle);

/** Send a single projectile against the given target. */
bool proj_SendProjectile(Weapon* psWeap, PlayerOwnedObject * psAttacker, unsigned player, Vector3i target, PlayerOwnedObject * psTarget,
                         bool bVisible, int weapon_slot);

/** Send a single projectile against the given target
 * with a minimum shot angle. */
bool proj_SendProjectileAngled(Weapon* psWeap, PlayerOwnedObject * psAttacker, unsigned player, Vector3i target,
                               PlayerOwnedObject * psTarget, bool bVisible, int weapon_slot, int min_angle, unsigned fireTime);

/** Return whether a weapon is direct or indirect. */
bool proj_Direct(const WeaponStats* psStats);

/** Return the maximum range for a weapon. */
unsigned proj_GetLongRange(const WeaponStats* psStats, unsigned player);

/** Return the minimum range for a weapon. */
unsigned proj_GetMinRange(const WeaponStats* psStats, unsigned player);

/** Return the short range for a weapon. */
unsigned proj_GetShortRange(const WeaponStats* psStats, unsigned player);

unsigned calcDamage(unsigned baseDamage, WEAPON_EFFECT weaponEffect, PlayerOwnedObject const* psTarget);
bool gfxVisible(Projectile* psObj);

glm::mat4 objectShimmy(PlayerOwnedObject * psObj);

int establishTargetHeight(PlayerOwnedObject const* psTarget);

void checkProjectile(const Projectile* psProjectile, std::string location_description,
                     std::string function, int recurse);

/* Assert if projectile is bad */
#define CHECK_PROJECTILE(object) checkProjectile((object), \
        AT_MACRO, __FUNCTION__, max_check_object_recursion)

static void setProjectileSource(Projectile *psProj, ConstructedObject *psObj);

ObjectShape establishTargetShape(PlayerOwnedObject* psTarget);

#endif // __INCLUDED_SRC_PROJECTILE_H__
