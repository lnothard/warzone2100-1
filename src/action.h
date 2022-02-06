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
 * @file action.h
 * Droid action interface
 */

#ifndef __INCLUDED_SRC_ACTION_H__
#define __INCLUDED_SRC_ACTION_H__

#include "lib/gamelib/gtime.h"
#include "wzmaplib/map.h"

struct BaseStats;
struct BaseObject;
struct Droid;
struct Weapon;
struct WeaponStats;


/* Attack run distances */
static constexpr auto	VTOL_ATTACK_LENGTH = 1000;
static constexpr auto VTOL_ATTACK_TARGET_DIST	=	400;

/* Turret rotation limits */
static constexpr auto VTOL_TURRET_LIMIT = DEG(45);
static constexpr auto VTOL_TURRET_LIMIT_BOMB = DEG(60);

static constexpr auto VTOL_ATTACK_AUDIO_DELAY	= 3 * GAME_TICKS_PER_SEC;

/// Droids heavier than this rotate and pitch more slowly
static constexpr auto HEAVY_WEAPON_WEIGHT = 50000;

static constexpr auto ACTION_TURRET_ROTATION_RATE	= 45;
static constexpr auto REPAIR_PITCH_LOWER =	30;
static constexpr auto REPAIR_PITCH_UPPER = -15;

/// How many tiles to retreat when falling back
static constexpr auto PULL_BACK_DIST	= 10;

/// Radius for search when looking for VTOL landing position
static constexpr int VTOL_LANDING_RADIUS = 23;

/// How many frames to skip before looking for a better target
static constexpr auto TARGET_UPD_SKIP_FRAMES = 1000;

/**
 * After failing a route ... this is the amount of time that
 * the droid goes all defensive until it can start going aggressive.
 */
static constexpr auto MIN_SULK_TIME = 1500;		// 1.5 sec
static constexpr auto MAX_SULK_TIME = 4000;		// 4 secs

/// This is how long a droid is disabled for when its been attacked by an EMP weapon
static constexpr auto EMP_DISABLE_TIME = 10000;     // 10 secs

/// How far away the repair droid can be from the damaged droid to function
static constexpr auto REPAIR_RANGE = 2 * TILE_UNITS;

/// The maximum distance a repair droid will automatically go in guard mode
static constexpr auto REPAIR_MAXDIST = 5 * TILE_UNITS;

// The minimum structure strength modifier needed to automatically target blocking walls
static constexpr auto MIN_STRUCTURE_BLOCK_STRENGTH = 33;

enum class ACTION
{
  NONE,
  MOVE,
  BUILD,
  DEMOLISH,
  REPAIR,
  ATTACK,
  OBSERVE,
  FIRE_SUPPORT,
  SULK,
  TRANSPORT_OUT,
  TRANSPORT_WAIT_TO_FLY_IN,
  TRANSPORT_IN,
  DROID_REPAIR,
  RESTORE,
  MOVE_FIRE,
  MOVE_TO_BUILD,
  MOVE_TO_DEMOLISH,
  MOVE_TO_REPAIR,
  BUILD_WANDER,
  MOVE_TO_ATTACK,
  ROTATE_TO_ATTACK,
  MOVE_TO_OBSERVE,
  WAIT_FOR_REPAIR,
  MOVE_TO_REPAIR_POINT,
  WAIT_DURING_REPAIR,
  MOVE_TO_DROID_REPAIR,
  MOVE_TO_RESTORE,
  MOVE_TO_REARM,
  WAIT_FOR_REARM,
  MOVE_TO_REARM_POINT,
  WAIT_DURING_REARM,
  VTOL_ATTACK,
  CLEAR_REARM_PAD,
  RETURN_TO_POS,
  FIRE_SUPPORT_RETREAT,
  CIRCLE,
  COUNT // MUST BE LAST
};

/// Data required for any action
struct Action
{
  explicit Action(ACTION action);
  Action(ACTION action, BaseObject* targetObject);
  Action(ACTION action, Vector2i location);
  Action(ACTION action, Vector2i location, BaseObject* targetObject);

  ACTION action = ACTION::NONE;
  Vector2i location{0, 0};
  BaseObject* targetObject = nullptr;
  std::unique_ptr<BaseStats> stats = nullptr;
};

std::string actionToString(ACTION action);

/**
 * @typedef \c tileMatchFunction
 * @brief pointer to a 'tile search function', used by \c spiralSearch()
 * @param \c x,y are the coordinates that should be inspected.
 * @param data a pointer to state data, allows the search function to retain
 *             state in between calls and can be used as a means of returning
 *             its result to the caller of \c spiralSearch().
 * @return \c true when the search has finished, \c false when the search should
 *         continue.
 */
typedef bool (*tileMatchFunction)(Vector2i coords, void* matchState);

/// Give a droid an action
void actionDroid(Droid* psDroid, ACTION action);
/// Give a droid an action with a location target
void actionDroid(Droid* psDroid, ACTION action, Vector2i location);
/// Give a droid an action with an object target
void actionDroid(Droid* psDroid, ACTION action, BaseObject* targetObject);
/// Give a droid an action with an object target and a location
void actionDroid(Droid* psDroid, ACTION action, BaseObject* targetObject, Vector2i location);

void actionAddVtolAttackRun(Droid* psDroid);
void actionUpdateVtolAttack(Droid* psDroid);

/**
 * Calculate a position for units to pull back to if they
 * need to increase the range between them and a target
 */
void actionCalcPullBackPoint(BaseObject const* psObj, BaseObject const* psTarget, int* px, int* py);

/**
 * Rotate turret toward a target
 * @return \c true if locked on (to a droid or building)
 */
bool actionTargetTurret(BaseObject* psAttacker, BaseObject* psTarget, int slot);

/// @return \c true if a target is within weapon range
bool actionInRange(Droid const* psDroid, BaseObject const* psObj,
                   int weapon_slot, bool useLongWithOptimum = true);

/// @return \c true if a droid can see a target to fire on it
bool actionVisibleTarget(Droid const* psDroid, BaseObject const* psTarget,
                         int weapon_slot, bool useLongWithOptimum);

/// @return \c true if a droid is in the neighboring tile to a build position
bool actionReachedBuildPos(Droid const* psDroid, Vector2i location,
                           uint16_t direction, BaseStats const* psStats);

/// @return \c true if a droid is in the neighboring tile of another droid
bool actionReachedDroid(Droid const* psDroid, Droid const* psOther);

/// @return \c true if a target is inside minimum weapon range
bool actionInsideMinRange(Droid const* psDroid, BaseObject const* psObj, WeaponStats const* psStats);

/// @return \c true if a droid is on the foundations of a new building
bool actionRemoveDroidsFromBuildPos(unsigned player, Vector2i pos, uint16_t dir, BaseStats const* psStats);

/**
 * Choose a landing position for a VTOL when it goes to rearm that
 * is close to a rearm * pad but not on it, since it may be busy
 * by the time we get there
 */
bool actionVTOLLandingPos(Droid const* psDroid, Vector2i* p);

/**
 * Performs a space-filling spiral-like search from \c startCoords up to (and
 * including) \c maxRadius. For each tile, the search function is called - if it
 * returns \c true, the search will finish immediately
 *
 * @param startCoords starting \c (x, y) coordinates
 * @param maxRadius radius to examine. Search will finish when \c maxRadius is exceeded.
 * @param match searchFunction to use; described in typedef
 * \param matchState state for the search function
 * \return \c true if finished because the searchFunction requested termination,
 *   \c false if the radius limit was reached
 */
bool spiralSearch(Vector2i startCoords, int maxRadius, tileMatchFunction match, void* matchState);

#endif // __INCLUDED_SRC_ACTION_H__
