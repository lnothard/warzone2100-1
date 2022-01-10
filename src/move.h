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
 * @file move.h
 * Interface for the unit movement system
 */

#ifndef __INCLUDED_SRC_MOVE_H__
#define __INCLUDED_SRC_MOVE_H__

#include <vector>

#include "lib/framework/vector.h"
#include "objectdef.h"
#include "fpath.h"

enum class MOVE_STATUS
{
    INACTIVE,
    NAVIGATE,
    TURN,
    PAUSE,
    POINT_TO_POINT,
    TURN_TO_TARGET,
    HOVER,
    WAIT_FOR_ROUTE,
    SHUFFLE
};

struct Movement
{
    Movement() = default;
    Movement(Vector2i src, Vector2i destination);

    [[nodiscard]] bool isStationary() const noexcept;
    void set_path_vars(int target_x, int target_y);

    using enum MOVE_STATUS;
    MOVE_STATUS status = INACTIVE;

    /// Position in path
    int pathIndex = 0;

    /// Pointer to list of block (x,y) map coordinates
    std::vector<Vector2i> path;

    /// World coordinates of movement destination
    Vector2i destination {0, 0};

    Vector2i src {0, 0};
    Vector2i target {0, 0};

    int speed = 0;
    uint16_t moveDir = 0; ///< Direction of motion (not the direction the droid is facing)
    uint16_t bumpDir = 0; ///< Direction at last bump
    unsigned bumpTime = 0; ///< Time of first bump with something
    uint16_t lastBump = 0; ///< Time of last bump with a droid - relative to bumpTime
    uint16_t pauseTime = 0; ///< When MOVEPAUSE started - relative to bumpTime
    Position bumpPos = Position(0, 0, 0); ///< Position of last bump
    unsigned shuffleStart = 0; ///< When a shuffle started

    /// For VTOL movement
    int vertical_speed = 0;
};

/* Set a target location for a droid to move to  - returns a bool based on if there is a path to the destination (true if there is a path)*/
bool moveDroidTo(Droid* psDroid, UDWORD x, UDWORD y, FPATH_MOVETYPE moveType = FMT_MOVE);

/* Set a target location for a droid to move to  - returns a bool based on if there is a path to the destination (true if there is a path)*/
// the droid will not join a formation when it gets to the location
bool moveDroidToNoFormation(Droid* psDroid, UDWORD x, UDWORD y, FPATH_MOVETYPE moveType = FMT_MOVE);

// move a droid directly to a location (used by vtols only)
void moveDroidToDirect(Droid* psDroid, UDWORD x, UDWORD y);

// Get a droid to turn towards a locaton
void moveTurnDroid(Droid* psDroid, UDWORD x, UDWORD y);

/* Stop a droid */
void moveStopDroid(Droid* psDroid);

/*Stops a droid dead in its tracks - doesn't allow for any little skidding bits*/
void moveReallyStopDroid(Droid* psDroid);

/* Get a droid to do a frame's worth of moving */
void moveUpdateDroid(Droid* psDroid);

SDWORD moveCalcDroidSpeed(Droid* psDroid);

/* update body and turret to local slope */
void updateDroidOrientation(Droid* psDroid);

/* audio callback used to kill movement sounds */
bool moveCheckDroidMovingAndVisible(void* psObj);

std::string moveDescription(MOVE_STATUS status);

#endif // __INCLUDED_SRC_MOVE_H__
