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

#include "weapon.h"

struct SimpleObject;
struct Droid;

static constexpr auto ALLIANCE_BROKEN	= 0;			// states of alliance between players
static constexpr auto ALLIANCE_REQUESTED = 1;
static constexpr auto ALLIANCE_INVITATION	= 2;
static constexpr auto ALLIANCE_FORMED	= 3;
static constexpr auto ALLIANCE_NULL	= 4;			// for setting values only

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

/// Amount of time to rage at the world when frustrated (10 seconds)
static constexpr auto FRUSTRATED_TIME = 1000 * 10;

extern uint8_t alliances[MAX_PLAYER_SLOTS][MAX_PLAYER_SLOTS];
extern PlayerMask alliancebits[MAX_PLAYER_SLOTS];
extern PlayerMask satuplinkbits;

/// Check no alliance has formed. This is a define to make sure we inline it
#define aiCheckAlliances(_s1, _s2) (alliances[_s1][_s2] == ALLIANCE_FORMED)

/* Initialise the AI system */
bool aiInitialise();

/* Shutdown the AI system */
bool aiShutdown();

/* Do the AI for a droid */
void aiUpdateDroid(Droid* psDroid);

/// Find the nearest best target for a droid
/// returns integer representing quality of choice, -1 if failed
int aiBestNearestTarget(Droid* psDroid, SimpleObject** ppsObj, int weapon_slot, int extraRange = 0);

/// Are there a lot of bullets heading towards the structure?
bool aiObjectIsProbablyDoomed(SimpleObject* psObject, bool isDirect);

/// Update the expected damage to the object.
void aiObjectAddExpectedDamage(SimpleObject* psObject, SDWORD damage, bool isDirect);

/* See if there is a target in range added int weapon_slot*/
bool aiChooseTarget(SimpleObject* psObj, SimpleObject** ppsTarget, int weapon_slot,
                    bool bUpdateTarget, TARGET_ORIGIN* targetOrigin);

/** See if there is a target in range for Sensor objects. */
bool aiChooseSensorTarget(SimpleObject* psObj, SimpleObject** ppsTarget);

/*set of rules which determine whether the weapon associated with the object
can fire on the propulsion type of the target*/
bool validTarget(SimpleObject* psObject, SimpleObject* psTarget, int weapon_slot);

// Check if any of the weapons can target the target
bool checkAnyWeaponsTarget(SimpleObject* psObject, SimpleObject* psTarget);

// Check properties of the AllianceType enum.
static inline bool alliancesFixed(int t)
{
	return t != ALLIANCES;
}

static inline bool alliancesSharedVision(int t)
{
	return t == ALLIANCES_TEAMS || t == ALLIANCES_UNSHARED;
}

static inline bool alliancesSharedResearch(int t)
{
	return t == ALLIANCES || t == ALLIANCES_TEAMS;
}

static inline bool alliancesSetTeamsBeforeGame(int t)
{
	return t == ALLIANCES_TEAMS || t == ALLIANCES_UNSHARED;
}

static inline bool alliancesCanGiveResearchAndRadar(int t)
{
	return t == ALLIANCES;
}

static inline bool alliancesCanGiveAnything(int t)
{
	return t != NO_ALLIANCES;
}

#endif // __INCLUDED_SRC_AI_H__
