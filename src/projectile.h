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

#include "lib/gamelib/gtime.h"
#include <glm/fwd.hpp>

#include "basedef.h"
#include "weapondef.h"

static constexpr auto PROJ_MAX_PITCH = 45;
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

class Projectile : public virtual SimpleObject, public Impl::SimpleObject
{
public:
  friend class Damage;

  Projectile(unsigned id, unsigned player);

  void debug_(const char* function, char ch) const;
  void checkProjectile(const char* location_description,
                       const char* function, int recurse) const;

  [[nodiscard]] PROJECTILE_STATE getState() const noexcept;

  void update();

  void setTarget(Unit* psObj);

  void proj_InFlightFunc();
  void proj_ImpactFunc();
  void proj_PostImpactFunc();
  void proj_checkPeriodicalDamage();

  bool proj_SendProjectileAngled(Weapon* psWeap, Unit* psAttacker, unsigned player,
                                 Vector3i dest, Unit* psTarget, bool bVisible,
                                 int weapon_slot, int min_angle, unsigned fireTime);

  /// Update the source experience after a target is damaged/destroyed
  void updateExperience(unsigned experienceInc);
  
  [[nodiscard]] bool gfxVisible() const;
private:
  using enum PROJECTILE_STATE;
  PROJECTILE_STATE state;

  /// Whether the selected player should see the projectile
  bool isVisible;

  /// Firing weapon stats
  std::shared_ptr<WeaponStats> weaponStats;

  /// What fired the projectile
  Unit* source;

  /// Target of this projectile (not a Unit because it can
  /// be a feature I guess)
  SimpleObject* target;

  /// Targets that have already been dealt damage to (don't damage the same target twice)
  std::vector<Unit*> damaged;

  /// Where projectile started
  Vector3i origin {0, 0, 0};

  /// The target coordinates
  Vector3i destination {0, 0, 0};

  /// Axis velocities
  int vXY, vZ;

  /// Location of projectile in previous tick
  Spacetime prevSpacetime;

  /// Expected damage that this projectile will cause to the target
  int expectedDamageCaused;

  /// How much of target was visible on shooting (important for homing)
  int partVisible;
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
    [[nodiscard]] bool isFriendlyFire() const;
    [[nodiscard]] bool shouldIncreaseExperience() const;
    [[nodiscard]] int objectDamage();
    void updateKills();
private:
    Projectile* projectile = nullptr;
    SimpleObject* target = nullptr;
    unsigned damage;
    WEAPON_CLASS weaponClass;
    WEAPON_SUBCLASS weaponSubClass;
    std::size_t impactTime;
    bool isDamagePerSecond;
    int minDamage;
};

extern SimpleObject* g_pProjLastAttacker; ///< The last unit that did damage - used by script functions

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
bool proj_SendProjectile(Weapon* psWeap, SimpleObject* psAttacker, unsigned player, Vector3i target, SimpleObject* psTarget,
                         bool bVisible, int weapon_slot);

/** Send a single projectile against the given target
 * with a minimum shot angle. */
bool proj_SendProjectileAngled(Weapon* psWeap, SimpleObject* psAttacker, unsigned player, Vector3i target,
                               SimpleObject* psTarget, bool bVisible, int weapon_slot, int min_angle, unsigned fireTime);

/** Return whether a weapon is direct or indirect. */
bool proj_Direct(const WeaponStats* psStats);

/** Return the maximum range for a weapon. */
unsigned proj_GetLongRange(const WeaponStats* psStats, unsigned player);

/** Return the minimum range for a weapon. */
unsigned proj_GetMinRange(const WeaponStats* psStats, unsigned player);

/** Return the short range for a weapon. */
unsigned proj_GetShortRange(const WeaponStats* psStats, unsigned player);

unsigned calcDamage(unsigned baseDamage, WEAPON_EFFECT weaponEffect, SimpleObject* psTarget);
bool gfxVisible(Projectile* psObj);

/***************************************************************************/

glm::mat4 objectShimmy(SimpleObject* psObj);

int establishTargetHeight(SimpleObject const* psTarget);

void checkProjectile(const Projectile* psProjectile, std::string location_description,
                     std::string function, int recurse);

/* Assert if projectile is bad */
#define CHECK_PROJECTILE(object) checkProjectile((object), AT_MACRO, __FUNCTION__, max_check_object_recursion)

struct ObjectShape
{
	ObjectShape() = default;
	explicit ObjectShape(int radius);
  explicit ObjectShape(Vector2i size);
	ObjectShape(int width, int breadth);

	[[nodiscard]] int radius() const;

  /// True if rectangular, false if circular
	bool isRectangular = false;

  /// x == y if circular
	Vector2i size {0, 0};
};

ObjectShape establishTargetShape(SimpleObject* psTarget);

#endif // __INCLUDED_SRC_PROJECTILE_H__
