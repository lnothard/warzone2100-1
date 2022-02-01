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
 * @file fpath.h
 * Interface to the routing functions
 */

#ifndef __INCLUDED_SRC_FPATH_H__
#define __INCLUDED_SRC_FPATH_H__

#include "astar.h"
#include "droid.h"
#include "move.h"
#include "structure.h"


enum class FPATH_MOVETYPE
{
	FMT_MOVE,
	///< Move around all obstacles
	FMT_ATTACK,
	///< Assume that we will destroy enemy obstacles
	FMT_BLOCK
	///< Don't go through obstacles, not even gates.
};

enum class FPATH_RESULT
{
    OK,
    ///< found a route
    FAILED,
    ///< failed to find a route
    WAIT
    ///< route is being calculated by the path-finding thread
};

struct PathBlockingMap;

struct PathJob
{
	PROPULSION_TYPE propulsion;
	DROID_TYPE droidType;
	PathCoord destination {0, 0};
	PathCoord origin {0, 0};
	StructureBounds dstStructure;
	unsigned droidID;
	FPATH_MOVETYPE moveType;
	unsigned owner; ///< Player owner
	std::shared_ptr<PathBlockingMap> blockingMap; ///< Map of blocking tiles.
	bool acceptNearest;
	bool deleted;
	///< Droid was deleted, so throw away result when complete. Must still process this PATHJOB, since processing order can affect resulting paths (but can't affect the path length).
};

struct PathResult
{
    unsigned droidID; ///< Unique droid ID.
    Movement sMove; ///< New movement values for the droid.
    FPATH_RESULT retval; ///< Result value from path-finding.
    Vector2i originalDest; ///< Used to check if the pathfinding job is to the right destination.
};

/** Initialise the path-finding module.
 */
bool fpathInitialise();

/** Shutdown the path-finding module.
 */
void fpathShutdown();

void fpathUpdate();

/** Find a route for a droid to a location.
 */
FPATH_RESULT fpathDroidRoute(Droid* psDroid, SDWORD targetX, SDWORD targetY, FPATH_MOVETYPE moveType);

/// Returns true iff the parameters have equivalent behaviour in fpathBaseBlockingTile.
bool fpathIsEquivalentBlocking(PROPULSION_TYPE propulsion1, unsigned player1, FPATH_MOVETYPE moveType1,
                               PROPULSION_TYPE propulsion2, unsigned player2, FPATH_MOVETYPE moveType2);

/** Function pointer to the currently in-use blocking tile check function.
 *
 *  This function will check if the map tile at the given location should be considered to block droids
 *  with the currently selected propulsion type. This is not identical to whether it will actually block,
 *  which can depend on hostilities and open/closed attributes.
 *
 * fpathBlockingTile -- when it is irrelevant who owns what buildings, they all block unless propulsion is right
 * fpathDroidBlockingTile -- when you may want to factor the above into account
 * fpathBaseBlockingTile -- set all parameters; the others are convenience functions for this one
 *
 *  @return true if the given tile is blocking for this droid
 */
bool fpathBlockingTile(SDWORD x, SDWORD y, PROPULSION_TYPE propulsion);
bool fpathDroidBlockingTile(Droid* psDroid, int x, int y, FPATH_MOVETYPE moveType);
bool fpathBaseBlockingTile(SDWORD x, SDWORD y, PROPULSION_TYPE propulsion, unsigned player, FPATH_MOVETYPE moveType);

static inline bool fpathBlockingTile(Vector2i tile, PROPULSION_TYPE propulsion)
{
	return fpathBlockingTile(tile.x, tile.y, propulsion);
}

/** Set a direct path to position.
 *
 *  Plan a path from @c psDroid's current position to given position without
 *  taking obstructions into consideration.
 *
 *  Used for instance by VTOLs. Function is thread-safe.
 */
void fpathSetDirectRoute(Droid* psDroid, SDWORD targetX, SDWORD targetY);

/** Clean up path jobs and results for a droid. Function is thread-safe. */
void fpathRemoveDroidData(int id);

/** Quick O(1) test of whether it is theoretically possible to go from origin to destination
 *  using the given propulsion type. orig and dest are in world coordinates. */
bool fpathCheck(Position orig, Position dest, PROPULSION_TYPE propulsion);

/** Unit testing. */
void fpathTest(int x, int y, int x2, int y2);
void fpathSetMove(Movement* psMoveCntl, int targetX, int targetY);

/** @} */

#endif // __INCLUDED_SRC_FPATH_H__
