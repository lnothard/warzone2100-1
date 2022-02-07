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
 * @file ai.h
 * Definitions for the AI system structures
 */

#ifndef __INCLUDED_SRC_AI_H__
#define __INCLUDED_SRC_AI_H__

#include "lib/framework/frame.h"

#include "basedef.h"
#include "droid.h"
#include "weapon.h"


static constexpr auto ALLIANCE_BROKEN	= 0;
static constexpr auto ALLIANCE_REQUESTED = 1;
static constexpr auto ALLIANCE_INVITATION	= 2;
static constexpr auto ALLIANCE_FORMED	= 3;
static constexpr auto ALLIANCE_NULL	= 4;

/// Amount of time to rage at the world when frustrated (10 seconds)
static constexpr auto FRUSTRATED_TIME = 1000 * 10;

/* Weights used for target selection code, target distance is used as 'common currency' */
/// In points used in weaponmodifier.txt and structuremodifier.txt
static constexpr auto	WEIGHT_DIST_TILE = 13;
/// How much weight a distance of 1 tile (128 world units) has when looking for the best nearest target
static constexpr auto	WEIGHT_DIST_TILE_DROID = WEIGHT_DIST_TILE;

static constexpr auto	WEIGHT_DIST_TILE_STRUCT =	WEIGHT_DIST_TILE;
/// How much weight unit damage has (100% of damage is equally weighted as 10 tiles distance)
static constexpr auto	WEIGHT_HEALTH_DROID	=	WEIGHT_DIST_TILE * 10;
/// ~100% damage should be ~8 tiles (max sensor range)
static constexpr auto	WEIGHT_HEALTH_STRUCT = WEIGHT_DIST_TILE * 7;

/// We really don't like objects we can't see
static constexpr auto	WEIGHT_NOT_VISIBLE_F = 10;
/// We don't want them to be repairing droids or structures while we are after them
static constexpr auto	WEIGHT_SERVICE_DROIDS	=	WEIGHT_DIST_TILE_DROID * 5;
/// We prefer to go after anything that has a gun and can hurt us
static constexpr auto	WEIGHT_WEAPON_DROIDS =	WEIGHT_DIST_TILE_DROID * 4;
/// Commanders get a higher priority
static constexpr auto	WEIGHT_COMMAND_DROIDS	=	WEIGHT_DIST_TILE_DROID * 6;
/// Droid/cyborg factories, repair facility; shouldn't have too much weight
static constexpr auto	WEIGHT_MILITARY_STRUCT = WEIGHT_DIST_TILE_STRUCT;
/// Same as weapon droids (?)
static constexpr auto	WEIGHT_WEAPON_STRUCT = WEIGHT_WEAPON_DROIDS;
/// Even if it's 4 tiles further away than defenses we still choose it
static constexpr auto	WEIGHT_DERRICK_STRUCT	=	WEIGHT_MILITARY_STRUCT + WEIGHT_DIST_TILE_STRUCT * 4;

/// Humans won't fool us anymore!
static constexpr auto WEIGHT_STRUCT_NOT_BUILT_F = 8;
/// It only makes sense to switch target if new one is 4+ tiles closer
static constexpr auto OLD_TARGET_THRESHOLD = WEIGHT_DIST_TILE * 4;
/// EMP shouldn't attack EMP'd targets again
static constexpr auto	EMP_DISABLED_PENALTY_F = 10;
/// EMP don't attack structures, should be bigger than EMP_DISABLED_PENALTY_F
static constexpr auto	EMP_STRUCT_PENALTY_F = EMP_DISABLED_PENALTY_F * 2;

static constexpr auto TOO_CLOSE_PENALTY_F = 20;
/// Targets that have a lot of damage incoming are less attractive
static constexpr auto TARGET_DOOMED_PENALTY_F	= 10;
/// Weapon ROF threshold for above penalty. per minute
static constexpr auto TARGET_DOOMED_SLOW_RELOAD_T	= 21;

/// A single rank is as important as 4 tiles distance
static constexpr auto	WEIGHT_CMD_RANK	=	WEIGHT_DIST_TILE * 4;
/// Don't want this to be too high, since a commander can have many units assigned
static constexpr auto	WEIGHT_CMD_SAME_TARGET = WEIGHT_DIST_TILE;

/// Check no alliance has formed. This is a define to make sure we inline it
#define aiCheckAlliances(_s1, _s2) (alliances[_s1][_s2] == ALLIANCE_FORMED)

enum class ALLIANCE_TYPE
{
	FFA,
  /// Players can make and break alliances during the game
	ALLIANCES,
  /// Alliances are set before the game
	ALLIANCES_TEAMS,
  /// Alliances are set before the game. No shared research
	ALLIANCES_UNSHARED
};

extern uint8_t alliances[MAX_PLAYER_SLOTS][MAX_PLAYER_SLOTS];
/// A bitfield of vision sharing in alliances, for quick manipulation of vision information
extern PlayerMask alliancebits[MAX_PLAYER_SLOTS];
/// A bitfield for the satellite uplink
extern PlayerMask satuplinkbits;

bool aiInitialise();
bool aiShutdown();

/// Update the expected damage to the object.
void aiObjectAddExpectedDamage(BaseObject* psObject, int damage, bool isDirect);

unsigned aiDroidRange(Droid const* psDroid, int weapon_slot);

/** Search the global list of sensors for a possible target for psObj. */
BaseObject* aiSearchSensorTargets(BaseObject const* psObj, int weapon_slot,
                                        WeaponStats const* psWStats, TARGET_ORIGIN* targetOrigin);

/* Calculates attack priority for a certain target */
int targetAttackWeight(BaseObject const* psTarget, BaseObject const* psAttacker, int weapon_slot);

/// See if there is a target in range
bool aiChooseTarget(BaseObject const* psObj, BaseObject** ppsTarget, int weapon_slot,
                    bool bUpdateTarget, TARGET_ORIGIN* targetOrigin);

/** See if there is a target in range for Sensor objects. */
bool aiChooseSensorTarget(BaseObject* psObj, BaseObject** ppsTarget);

/*set of rules which determine whether the weapon associated with the object
can fire on the propulsion type of the target*/
bool validTarget(BaseObject const* psObject, BaseObject const* psTarget, int weapon_slot);

// Check if any of the weapons can target the target
bool checkAnyWeaponsTarget(BaseObject const* psObject, BaseObject const* psTarget);

// Check properties of the AllianceType enum.
static bool alliancesFixed(ALLIANCE_TYPE t);
static bool alliancesSharedVision(ALLIANCE_TYPE t);
static bool alliancesSharedResearch(ALLIANCE_TYPE t);
static bool alliancesSetTeamsBeforeGame(ALLIANCE_TYPE t);
static bool alliancesCanGiveResearchAndRadar(ALLIANCE_TYPE t);
static bool alliancesCanGiveAnything(ALLIANCE_TYPE t);

bool updateAttackTarget(BaseObject* psAttacker, int weapon_slot);

#endif // __INCLUDED_SRC_AI_H__
