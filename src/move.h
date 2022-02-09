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
#include "wzmaplib/map.h"


#define	VTOL_VERTICAL_SPEED		(((pimpl->baseSpeed / 4) > 60) ? ((SDWORD)pimpl->baseSpeed / 4) : 60)

/* max and min vtol heights above terrain */
static constexpr auto VTOL_HEIGHT_MIN	= 250;
static constexpr auto VTOL_HEIGHT_LEVEL	=	300;
static constexpr auto VTOL_HEIGHT_MAX =	350;

// Maximum size of an object for collision
static constexpr auto OBJ_MAXRADIUS	= TILE_UNITS * 4;

// how long a shuffle can propagate before they all stop
static constexpr auto MOVE_SHUFFLETIME = 10000;

// Length of time a droid has to be stationery to be considered blocked
static constexpr auto BLOCK_TIME = 6000;
static constexpr auto SHUFFLE_BLOCK_TIME = 2000;
// How long a droid has to be stationary before stopping trying to move
static constexpr auto BLOCK_PAUSETIME = 1500;
static constexpr auto BLOCK_PAUSERELEASE = 500;
// How far a droid has to move before it is no longer 'stationary'
static constexpr auto BLOCK_DIST	=	64;
// How far a droid has to rotate before it is no longer 'stationary'
static constexpr auto BLOCK_DIR =	90;

// How far out from an obstruction to start avoiding it
static constexpr auto AVOID_DIST = TILE_UNITS * 2;

// Speed to approach a final way point, if possible.
static constexpr auto MIN_END_SPEED	= 60;

// distance from final way point to start slowing
static constexpr auto END_SPEED_RANGE	= 3 * TILE_UNITS;

// how long to pause after firing a FOM_NO weapon
static constexpr auto FOM_MOVEPAUSE	=	1500;

// distance to consider droids for a shuffle
static constexpr auto SHUFFLE_DIST = 3 * TILE_UNITS / 2;

// how far to move for a shuffle
static constexpr auto SHUFFLE_MOVE = 2 * TILE_UNITS / 2;

/// Extra precision added to movement calculations.
static constexpr auto EXTRA_BITS = 8;
static constexpr auto EXTRA_PRECISION = 1 << EXTRA_BITS;

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

struct BLOCKING_CALLBACK_DATA
{
    PROPULSION_TYPE propulsionType;
    bool blocking;
    Vector2i src;
    Vector2i dst;
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
    std::vector<Vector2i> path{0};

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
bool moveDroidTo(Droid* psDroid, Vector2i location, FPATH_MOVETYPE moveType = FPATH_MOVETYPE::FMT_MOVE);

/* Set a target location for a droid to move to  - returns a bool based on if there is a path to the destination (true if there is a path)*/
// the droid will not join a formation when it gets to the location
bool moveDroidToNoFormation(Droid* psDroid, Vector2i location, FPATH_MOVETYPE moveType = FPATH_MOVETYPE::FMT_MOVE);

// move a droid directly to a location (used by vtols only)
void moveDroidToDirect(Droid* psDroid, Vector2i location);

// Get a droid to turn towards a locaton
void moveTurnDroid(Droid* psDroid, Vector2i location);

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
static void moveOpenGates(Droid const* psDroid);
static void moveOpenGates(Droid const* psDroid, Vector2i tile);
static void moveCalcSlideVector(Droid* psDroid, int objX, int objY, int* pMx, int* pMy);
static bool moveDroidStopped(Droid* psDroid, SDWORD speed);
static void movePlayDroidMoveAudio(Droid* psDroid);
static void moveCheckFinalWaypoint(Droid const* psDroid, SDWORD* pSpeed);
static bool moveDroidStartCallback(void* psObj);
static void moveUpdateDroidDirection(Droid* psDroid, SDWORD* pSpeed, uint16_t direction,
                                     uint16_t iSpinAngle, int iSpinSpeed, int iTurnSpeed, uint16_t* pDroidDir);
static int moveCalcNormalSpeed(Droid const* psDroid, int fSpeed, uint16_t iDroidDir, int iAccel, int iDecel);
static int moveCalcPerpSpeed(Droid* psDroid, uint16_t iDroidDir, int iSkidDecel);
static void moveGetDroidPosDiffs(Droid const* psDroid, int32_t* pDX, int32_t* pDY);
static void moveCheckSquished(Droid const* psDroid, int emx, int emy);
static void moveUpdateDroidPos(Droid* psDroid, int32_t dx, int32_t dy);
static bool moveReachedWayPoint(Droid const* psDroid);
static uint16_t moveGetDirection(Droid* psDroid);
static void checkLocalFeatures(Droid* psDroid);
static bool moveBlockingTileCallback(Vector2i pos, int32_t dist, void* data_);

#endif // __INCLUDED_SRC_MOVE_H__
