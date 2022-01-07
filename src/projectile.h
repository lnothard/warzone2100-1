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
#include "projectiledef.h"
#include "weapondef.h"

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
  Projectile(unsigned id, unsigned player);

  void update();

private:
  uint8_t state; ///< current projectile state
  uint8_t bVisible; ///< whether the selected player should see the projectile
  WeaponStats* psWStats; ///< firing weapon stats
  Unit* psSource; ///< what fired the projectile
  Unit* psDest; ///< target of this projectile
  std::vector<SimpleObject*> psDamaged;
  ///< the targets that have already been dealt damage to (don't damage the same target twice)

  Vector3i src = Vector3i(0, 0, 0); ///< Where projectile started
  Vector3i dst = Vector3i(0, 0, 0); ///< The target coordinates
  int vXY, vZ; ///< axis velocities
  Spacetime prevSpacetime; ///< Location of projectile in previous tick.
  unsigned expectedDamageCaused; ///< Expected damage that this projectile will cause to the target.
  int partVisible; ///< how much of target was visible on shooting (important for homing)
};

struct INTERVAL
{
    int begin, end; // Time 1 = 0, time 2 = 1024. Or begin >= end if empty.
};

struct DAMAGE
{
    Projectile* psProjectile;
    SimpleObject* psDest;
    unsigned damage;
    WEAPON_CLASS weaponClass;
    WEAPON_SUBCLASS weaponSubClass;
    unsigned impactTime;
    bool isDamagePerSecond;
    int minDamage;
};

typedef std::vector<Projectile*>::const_iterator ProjectileIterator;

/// True iff object is a projectile.
static inline bool isProjectile(SIMPLE_OBJECT const* psObject)
{
  return psObject != nullptr && psObject->type == OBJ_PROJECTILE;
}

/// Returns PROJECTILE * if projectile or NULL if not.
static inline Projectile* castProjectile(SIMPLE_OBJECT* psObject)
{
  return isProjectile(psObject) ? (Projectile*)psObject : (Projectile*)nullptr;
}

/// Returns PROJECTILE const * if projectile or NULL if not.
static inline Projectile const* castProjectile(SIMPLE_OBJECT const* psObject)
{
  return isProjectile(psObject) ? (Projectile const*)psObject : (Projectile const*)nullptr;
}
extern SimpleObject* g_pProjLastAttacker; ///< The last unit that did damage - used by script functions

#define PROJ_MAX_PITCH  45
#define PROJ_ULTIMATE_PITCH  80

#define BURN_TIME	10000	///< How long an object burns for after leaving a fire.
#define BURN_DAMAGE	15	///< How much damage per second an object takes when it is burning.
#define BURN_MIN_DAMAGE	30	///< Least percentage of damage an object takes when burning.
#define ACC_GRAVITY	1000	///< Downward force against projectiles.

/** How long to display a single electronic warfare shimmmer. */
#define ELEC_DAMAGE_DURATION    (GAME_TICKS_PER_SEC/5)

bool proj_InitSystem(); ///< Initialize projectiles subsystem.
void proj_UpdateAll(); ///< Frame update for projectiles.
bool proj_Shutdown(); ///< Shut down projectile subsystem.

Projectile* proj_GetFirst(); ///< Get first projectile in the list.
Projectile* proj_GetNext(); ///< Get next projectile in the list.

void proj_FreeAllProjectiles(); ///< Free all projectiles in the list.

void setExpGain(int player, int gain);
int getExpGain(int player);

/// Calculate the initial velocities of an indirect projectile. Returns the flight time.
int32_t projCalcIndirectVelocities(const int32_t dx, const int32_t dz, int32_t v, int32_t* vx, int32_t* vz,
                                   int min_angle);

/** Send a single projectile against the given target. */
bool proj_SendProjectile(Weapon* psWeap, SIMPLE_OBJECT* psAttacker, int player, Vector3i target, SimpleObject* psTarget,
                         bool bVisible, int weapon_slot);

/** Send a single projectile against the given target
 * with a minimum shot angle. */
bool proj_SendProjectileAngled(Weapon* psWeap, SIMPLE_OBJECT* psAttacker, int player, Vector3i target,
                               SimpleObject* psTarget, bool bVisible, int weapon_slot, int min_angle, unsigned fireTime);

/** Return whether a weapon is direct or indirect. */
bool proj_Direct(const WeaponStats* psStats);

/** Return the maximum range for a weapon. */
int proj_GetLongRange(const WeaponStats* psStats, unsigned player);

/** Return the minimum range for a weapon. */
int proj_GetMinRange(const WeaponStats* psStats, unsigned player);

/** Return the short range for a weapon. */
int proj_GetShortRange(const WeaponStats* psStats, int player);

unsigned calcDamage(unsigned baseDamage, WEAPON_EFFECT weaponEffect, SimpleObject* psTarget);
bool gfxVisible(Projectile* psObj);

/***************************************************************************/

glm::mat4 objectShimmy(SimpleObject* psObj);

static inline void setProjectileSource(Projectile* psProj, SIMPLE_OBJECT* psObj)
{
	// use the source of the source of psProj if psAttacker is a projectile
	psProj->psSource = nullptr;
	if (psObj == nullptr)
	{
	}
	else if (isProjectile(psObj))
	{
		Projectile* psPrevProj = castProjectile(psObj);

		if (psPrevProj->psSource && !psPrevProj->psSource->died)
		{
			psProj->psSource = psPrevProj->psSource;
		}
	}
	else
	{
		psProj->psSource = castBaseObject(psObj);
	}
}

int establishTargetHeight(SimpleObject const* psTarget);

/* @} */

void checkProjectile(const Projectile* psProjectile, const char* const location_description, const char* function,
                     const int recurse);

/* assert if projectile is bad */
#define CHECK_PROJECTILE(object) checkProjectile((object), AT_MACRO, __FUNCTION__, max_check_object_recursion)

#define syncDebugProjectile(psProj, ch) _syncDebugProjectile(__FUNCTION__, psProj, ch)
void _syncDebugProjectile(const char* function, Projectile const* psProj, char ch);

struct ObjectShape
{
	ObjectShape() : isRectangular(false), size(0, 0)
	{
	}

	ObjectShape(int radius) : isRectangular(false), size(radius, radius)
	{
	}

	ObjectShape(int width, int breadth) : isRectangular(true), size(width, breadth)
	{
	}

	ObjectShape(Vector2i widthBreadth) : isRectangular(true), size(widthBreadth)
	{
	}

	int radius() const
	{
		return size.x;
	}

	bool isRectangular; ///< True if rectangular, false if circular.
	Vector2i size; ///< x == y if circular.
};

ObjectShape establishTargetShape(SimpleObject* psTarget);

#endif // __INCLUDED_SRC_PROJECTILE_H__
