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


enum class FPATH_RESULT
{
    OK, ///< Found a route
    FAILED, ///< Failed to find a route
    WAIT ///< route is being calculated by the path-finding thread
};

struct PathJob
{
	DROID_TYPE droidType = DROID_TYPE::ANY;
  PathBlockingType moveType;
	PathCoord destination {0, 0};
	PathCoord origin {0, 0};
	NonBlockingArea dstStructure{};
	unsigned droidID = 0;
	std::unique_ptr<PathBlockingMap> blockingMap;
	bool acceptNearest = false;
	bool deleted = false;
};

struct PathResult
{
  PathResult(unsigned id, FPATH_RESULT result, Vector2i originalDest);

  unsigned droidID; ///< Unique droid ID
  Movement* sMove = nullptr; ///< New movement values for the droid
  FPATH_RESULT retval; ///< Result value from path-finding
  Vector2i originalDest; ///< Used to check if the pathfinding job is to the right destination
};

/// Initialise the path-finding module
bool fpathInitialise();

/// Shutdown the path-finding module
void fpathShutdown();

void fpathUpdate();

/// Find a route for a droid to a location
FPATH_RESULT fpathDroidRoute(Droid* psDroid, Vector2i targetLocation, FPATH_MOVETYPE moveType);

/// @return \c true iff the parameters have equivalent behaviour in \c fpathBaseBlockingTile
[[nodiscard]] bool fpathIsEquivalentBlocking(PathBlockingType first, PathBlockingType second);

/**
 * Function pointer to the currently in-use blocking tile check function.
 *
 * This function will check if the map tile at the given location should be considered to block droids
 * with the currently selected propulsion type. This is not identical to whether it will actually block,
 * which can depend on hostilities and open/closed attributes.
 *
 * fpathBlockingTile -- when it is irrelevant who owns what buildings, they all block unless propulsion is right
 * fpathDroidBlockingTile -- when you may want to factor the above into account
 * fpathBaseBlockingTile -- set all parameters; the others are convenience functions for this one
 *
 *  @return true if the given tile is blocking for this droid
 */
bool fpathDroidBlockingTile(Droid* psDroid, Vector2i coords, FPATH_MOVETYPE moveType);
bool fpathBaseBlockingTile(Vector2i coords, PathBlockingType pathType);

static inline bool fpathBlockingTile(Vector2i tileCoords, PROPULSION_TYPE propulsion)
{
	return fpathBlockingTile(tile.x, tile.y, propulsion);
}

/** Clean up path jobs and results for a droid. Function is thread-safe. */
void fpathRemoveDroidData(unsigned id);

/** Quick O(1) test of whether it is theoretically possible to go from origin to destination
 *  using the given propulsion type. orig and dest are in world coordinates. */
bool fpathCheck(Position orig, Position dest, PROPULSION_TYPE propulsion);

/** Unit testing. */
void fpathTest(int x, int y, int x2, int y2);
void fpathSetMove(Movement* psMoveCntl, Vector2i targetLocation);

#endif // __INCLUDED_SRC_FPATH_H__
