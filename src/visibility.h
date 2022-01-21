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

#ifndef __INCLUDED_SRC_VISIBILITY__
#define __INCLUDED_SRC_VISIBILITY__

static constexpr auto MIN_VIS_HEIGHT = 80;

/// Accuracy for the height gradient
static constexpr auto GRADIENT_MULTIPLIER = 10000;

/// Rate to change visibility level
static constexpr auto VIS_LEVEL_INC = 255 * 2;
static constexpr auto VIS_LEVEL_DEC = 50;

// Trivial upper bound to what a fully upgraded WSS can use
// (its number of angles). Should probably be some factor times
// the maximum possible radius. Is probably a lot more than needed.
// Tested to need at least 180.
static constexpr auto MAX_WAVECAST_LIST_SIZE = 1360;

enum class SENSOR_CLASS
{
    VISION,
    RADAR
};

struct Spotter
{
    ~Spotter();
    Spotter(int x, int y, unsigned plr, int radius,
            SENSOR_CLASS type, std::size_t expiry = 0);

    Position pos;
    unsigned player;
    int sensorRadius;
    SENSOR_CLASS sensorType;

    /// When to self-destruct, zero if never
    std::size_t expiryTime;

    int numWatchedTiles = 0;
    std::vector<TILEPOS> watchedTiles;
    unsigned id;
};

static std::vector<std::unique_ptr<Spotter>> apsInvisibleViewers;

struct VisibleObjectHelp_t
{
    bool rayStart; // Whether this is the first point on the ray
    bool wallsBlock; // Whether walls block line of sight
    int startHeight; // The height at the view point
    Vector2i final; // The final tile of the ray cast
    int lastHeight, lastDist; // The last height and distance
    int currGrad; // The current obscuring gradient
    int numWalls; // Whether the LOS has hit a wall
    Vector2i wall; // The position of a wall if it is on the LOS
};

// initialise the visibility stuff
bool visInitialise();

/* Check which tiles can be seen by an object */
void visTilesUpdate(PlayerOwnedObject * psObj);

void revealAll(uint8_t player);

/**
 * Check whether psViewer can see psTarget.
 * psViewer should be an object that has some form of sensor,
 * currently droids and structures. psTarget can be any
 * type of SimpleObject (e.g. a tree).
 */
int visibleObject(const PlayerOwnedObject * psViewer, const PlayerOwnedObject * psTarget, bool wallsBlock);

/** Can shooter hit target with direct fire weapon? */
bool lineOfFire(const PlayerOwnedObject * psViewer, const PlayerOwnedObject * psTarget, int weapon_slot, bool wallsBlock);

/** How much of target can the player hit with direct fire weapon? */
int areaOfFire(const PlayerOwnedObject * psViewer, const PlayerOwnedObject * psTarget, int weapon_slot, bool wallsBlock);

/** How much of target can the player hit with direct fire weapon? */
int arcOfFire(const PlayerOwnedObject * psViewer, const PlayerOwnedObject * psTarget, int weapon_slot, bool wallsBlock);

// Find the wall that is blocking LOS to a target (if any)
Structure* visGetBlockingWall(const PlayerOwnedObject * psViewer, const PlayerOwnedObject * psTarget);

bool hasSharedVision(unsigned viewer, unsigned ally);

void processVisibility(); ///< Calls processVisibilitySelf and processVisibilityVision on all objects.

// update the visibility reduction
void visUpdateLevel();

void setUnderTilesVis(PlayerOwnedObject * psObj, unsigned player);

void visRemoveVisibilityOffWorld(PlayerOwnedObject * psObj);
void visRemoveVisibility(PlayerOwnedObject * psObj);

// fast test for whether obj2 is in range of obj1
static bool visObjInRange(PlayerOwnedObject * psObj1, PlayerOwnedObject * psObj2, int range);

// If we have ECM, use this for range instead. Otherwise, the sensor's range will be used for
// jamming range, which we do not want. Rather limit ECM unit sensor range to jammer range.
static unsigned objSensorRange(const PlayerOwnedObject * psObj);

static unsigned objJammerPower(const PlayerOwnedObject * psObj);

void removeSpotters();

void removeSpotter(unsigned id);

unsigned addSpotter(int x, int y, unsigned player, int radius,
                    bool radar, std::size_t expiry = 0);

#endif // __INCLUDED_SRC_VISIBILITY__
