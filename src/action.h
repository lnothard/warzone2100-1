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

enum class ACTION;
struct BaseStats;
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

/** After failing a route ... this is the amount of time that
 * the droid goes all defensive until it can start going aggressive. */
static constexpr auto MIN_SULK_TIME = 1500;		// 1.5 sec
static constexpr auto MAX_SULK_TIME = 4000;		// 4 secs

/** This is how long a droid is disabled for when its
 * been attacked by an EMP weapon. */
static constexpr auto EMP_DISABLE_TIME = 10000;     // 10 secs

/** How far away the repair droid can be from the damaged droid to function. */
static constexpr auto REPAIR_RANGE = 2 * TILE_UNITS;

/// The maximum distance a repair droid will automatically go in guard mode.
static constexpr auto REPAIR_MAXDIST = 5 * TILE_UNITS;

// The minimum structure strength modifier needed to automatically target blocking walls.
static constexpr auto MIN_STRUCTURE_BLOCK_STRENGTH = 33;

/// Data required for any action
struct Action
{
    ACTION action;
    unsigned x, y;
    
    /// Multiple action target info
    BaseObject* psObj;
    std::unique_ptr<BaseStats> psStats;
};

std::string getDroidActionName(ACTION action);

/**
 * Update the action state for a droid.
 *
 * @todo FIXME: Bad design, "updating" the action state is too fuzzy a goal for
 *              a function. As a result this beast is <em>way</em> too large.
 *              The scope of this function should be significantly limited, or
 *              implemented only as a series of subroutine calls with almost no
 *              data manipulation in this function itself. In either case, this
 *              function requires a major refactoring...
 */
void actionUpdateDroid(Droid* psDroid);

/** Do sanity update only. Called from actionUpdateDroid() normally. */
void actionSanity(Droid* psDroid);

/** Give a droid an action. */
void actionDroid(Droid* psDroid, ACTION action);

static void actionAddVtolAttackRun(Droid* psDroid);
static void actionUpdateVtolAttack(Droid* psDroid);
static void actionCalcPullBackPoint(BaseObject * psObj, BaseObject * psTarget, int* px, int* py);

/** Give a droid an action with a location target. */
void actionDroid(Droid* psDroid, ACTION action,
                 unsigned x, unsigned y);

/** Give a droid an action with an object target. */
void actionDroid(Droid* psDroid, ACTION action, BaseObject * psObj);

/** Give a droid an action with an object target and a location. */
void actionDroid(Droid* psDroid, ACTION action, BaseObject * psObj,
                 unsigned x, unsigned y);

/** Rotate turret toward  target return True if locked on (Droid and Structure). */
bool actionTargetTurret(BaseObject * psAttacker, BaseObject * psTarget,
                        Weapon* psWeapon);

/** Realign turret. */
void actionAlignTurret(BaseObject * psObj, int weapon_slot);

/** Check if a target is within weapon range. */
bool actionInRange(const Droid* psDroid, const BaseObject * psObj,
                   int weapon_slot, bool useLongWithOptimum = true);

/** Return whether a droid can see a target to fire on it. */
bool actionVisibleTarget(Droid* psDroid, BaseObject * psTarget,
                         int weapon_slot);

/** Check whether a droid is in the neighboring tile to a build position. */
bool actionReachedBuildPos(Droid const* psDroid, int x, int y,
                           uint16_t direction, BaseStats const* psStats);

/** Check that  two droids are next to each other */
bool actionReachedDroid(Droid const* psDroid, Droid const* psOther);

/** Send the vtol droid back to the nearest rearming pad - if there is one, otherwise return to base. */
void moveToRearm(Droid* psDroid);

/** Choose a landing position for a VTOL when it goes to rearm. */
bool actionVTOLLandingPos(Droid const* psDroid, Vector2i* p);

static bool actionInsideMinRange(Droid const* psDroid, BaseObject const* psObj, WeaponStats const* psStats);

static bool actionRemoveDroidsFromBuildPos(unsigned player, Vector2i pos, uint16_t dir, BaseStats* psStats);

#endif // __INCLUDED_SRC_ACTION_H__
