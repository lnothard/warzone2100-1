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
 * @file action.c
 *
 * Functions for setting the action of a droid.
 *
 */

#include "lib/framework/frame.h"
#include "lib/framework/math_ext.h"
#include "lib/framework/fixedpoint.h"
#include "lib/sound/audio.h"
#include "lib/sound/audio_id.h"
#include "lib/netplay/netplay.h"

#include "action.h"
#include "combat.h"
#include "geometry.h"
#include "mission.h"
#include "projectile.h"
#include "qtscript.h"
#include "random.h"
#include "transporter.h"
#include "mapgrid.h"
#include "hci.h"
#include "order.h"
#include "objmem.h"
#include "move.h"
#include "cmddroid.h"

/* attack run distance */

#define VTOL_ATTACK_TARDIST		400

// turret rotation limit
#define VTOL_TURRET_LIMIT               DEG(45)
#define VTOL_TURRET_LIMIT_BOMB          DEG(60)

#define	VTOL_ATTACK_AUDIO_DELAY		(3*GAME_TICKS_PER_SEC)

/** Droids heavier than this rotate and pitch more slowly. */
#define HEAVY_WEAPON_WEIGHT     50000

#define ACTION_TURRET_ROTATION_RATE	45
#define REPAIR_PITCH_LOWER		30
#define	REPAIR_PITCH_UPPER		-15

/* How many tiles to pull back. */
#define PULL_BACK_DIST		10

// Check if a droid has stopped moving
#define DROID_STOPPED(psDroid) \
	(psDroid->sMove.Status == MOVEINACTIVE || psDroid->sMove.Status == MOVEHOVER || \
	 psDroid->sMove.Status == MOVESHUFFLE)

/** Radius for search when looking for VTOL landing getPosition */
static const int vtolLandingRadius = 23;

/**
 * @typedef tileMatchFunction
 *
 * @brief pointer to a 'tile search function', used by spiralSearch()
 *
 * @param x,y  are the coordinates that should be inspected.
 *
 * @param data a pointer to state data, allows the search function to retain
 *             state in between calls and can be used as a means of returning
 *             its result to the caller of spiralSearch().
 *
 * @return true when the search has finished, false when the search should
 *         continue.
 */
typedef bool (*tileMatchFunction)(int x, int y, void *matchState);

/* returns true if on target */
bool actionTargetTurret(GameObject *psAttacker, GameObject *psTarget,
                        Weapon *psWeapon)
{
	WEAPON_STATS *psWeapStats = asWeaponStats + psWeapon->nStat;
	uint16_t tRotation, tPitch;
	uint16_t targetRotation;
	int32_t  rotationTolerance = 0;
	int32_t  pitchLowerLimit, pitchUpperLimit;

	if (!psTarget)
	{
		return false;
	}

	bool bRepair = psAttacker->getType == OBJ_DROID && ((Droid *)psAttacker)->droidType == DROID_REPAIR;

	// these are constants now and can be set up at the start of the function
	int rotRate = DEG(ACTION_TURRET_ROTATION_RATE) * 4;
	int pitchRate = DEG(ACTION_TURRET_ROTATION_RATE) * 2;

	// extra heavy weapons on some structures need to rotate and pitch more slowly
	if (psWeapStats->weight > HEAVY_WEAPON_WEIGHT && !bRepair)
	{
		UDWORD excess = DEG(100) * (psWeapStats->weight - HEAVY_WEAPON_WEIGHT) / psWeapStats->weight;

		rotRate = DEG(ACTION_TURRET_ROTATION_RATE) * 2 - excess;
		pitchRate = rotRate / 2;
	}

	tRotation = psWeapon->rot.direction;
	tPitch = psWeapon->rot.pitch;

	//set the pitch limits based on the weapon stats of the attacker
	pitchLowerLimit = pitchUpperLimit = 0;
	Vector3i attackerMuzzlePos = psAttacker->getPosition;  // Using for calculating the pitch, but not the direction, in case using the exact direction causes bugs somewhere.
	if (psAttacker->getType == OBJ_STRUCTURE)
	{
          auto *psStructure = (Structure *)psAttacker;
		int weapon_slot = psWeapon - psStructure->m_weaponList;  // Should probably be passed weapon_slot instead of psWeapon.
		calcStructureMuzzleLocation(psStructure, &attackerMuzzlePos, weapon_slot);
		pitchLowerLimit = DEG(psWeapStats->minElevation);
		pitchUpperLimit = DEG(psWeapStats->maxElevation);
	}
	else if (psAttacker->getType == OBJ_DROID)
	{
          auto *psDroid = (Droid *)psAttacker;
		int weapon_slot = psWeapon - psDroid->m_weaponList;  // Should probably be passed weapon_slot instead of psWeapon.
		calcDroidMuzzleLocation(psDroid, &attackerMuzzlePos, weapon_slot);

		if (psDroid->droidType == DROID_WEAPON || isTransporter(psDroid)
		    || psDroid->droidType == DROID_COMMAND || psDroid->droidType == DROID_CYBORG
		    || psDroid->droidType == DROID_CYBORG_SUPER)
		{
			pitchLowerLimit = DEG(psWeapStats->minElevation);
			pitchUpperLimit = DEG(psWeapStats->maxElevation);
		}
		else if (psDroid->droidType == DROID_REPAIR)
		{
			pitchLowerLimit = DEG(REPAIR_PITCH_LOWER);
			pitchUpperLimit = DEG(REPAIR_PITCH_UPPER);
		}
	}

	//get the maximum rotation this frame
	rotRate = gameTimeAdjustedIncrement(rotRate);
	rotRate = MAX(rotRate, DEG(1));
	pitchRate = gameTimeAdjustedIncrement(pitchRate);
	pitchRate = MAX(pitchRate, DEG(1));

	//and point the turret at target
	targetRotation =
            calcDirection(psAttacker->getPosition.x, psAttacker->getPosition.y,
                          psTarget->getPosition.x, psTarget->getPosition.y);

	//restrict rotationerror to =/- 180 degrees
	int rotationError = angleDelta(targetRotation - (tRotation + psAttacker->rotation.direction));

	tRotation += clip(rotationError, -rotRate, rotRate);  // Addition wrapping intentional.
	if (psAttacker->getType == OBJ_DROID && isVtolDroid((Droid *)psAttacker))
	{
		// limit the rotation for vtols
		int32_t limit = VTOL_TURRET_LIMIT;
		if (psWeapStats->weaponSubClass == WSC_BOMB || psWeapStats->weaponSubClass == WSC_EMP)
		{
			limit = 0;  // Don't turn bombs.
			rotationTolerance = VTOL_TURRET_LIMIT_BOMB;
		}
		tRotation = (uint16_t)clip(angleDelta(tRotation), -limit, limit);  // Cast wrapping intentional.
	}
	bool onTarget = abs(angleDelta(targetRotation - (tRotation + psAttacker->rotation.direction))) <= rotationTolerance;

	/* Set muzzle pitch if not repairing or outside minimum range */
	const int minRange = proj_GetMinRange(psWeapStats, psAttacker->owningPlayer);
	if (!bRepair && (unsigned)objPosDiffSq(psAttacker, psTarget) > minRange * minRange)
	{
		/* get target distance */
		Vector3i delta = psTarget->getPosition - attackerMuzzlePos;
		int32_t dxy = iHypot(delta.x, delta.y);

		uint16_t targetPitch = iAtan2(delta.z, dxy);
		targetPitch = (uint16_t)clip(angleDelta(targetPitch), pitchLowerLimit, pitchUpperLimit);  // Cast wrapping intended.
		int pitchError = angleDelta(targetPitch - tPitch);

		tPitch += clip(pitchError, -pitchRate, pitchRate);  // Addition wrapping intended.
		onTarget = onTarget && targetPitch == tPitch;
	}

	psWeapon->rot.direction = tRotation;
	psWeapon->rot.pitch = tPitch;

	return onTarget;
}

// calculate a position for units to pull back to if they
// need to increase the range between them and a target
static void actionCalcPullBackPoint(GameObject *psObj, GameObject *psTarget, int *px, int *py)
{
	// get the vector from the target to the object
	int xdiff = psObj->getPosition.x - psTarget->getPosition.x;
	int ydiff = psObj->getPosition.y - psTarget->getPosition.y;
	const int len = iHypot(xdiff, ydiff);

	if (len == 0)
	{
		xdiff = TILE_UNITS;
		ydiff = TILE_UNITS;
	}
	else
	{
		xdiff = (xdiff * TILE_UNITS) / len;
		ydiff = (ydiff * TILE_UNITS) / len;
	}

	// create the position
	*px = psObj->getPosition.x + xdiff * PULL_BACK_DIST;
	*py = psObj->getPosition.y + ydiff * PULL_BACK_DIST;

	// make sure coordinates stay inside of the map
	clip_world_offmap(px, py);
}

// check whether a droid is in the neighboring tile of another droid
bool actionReachedDroid(Droid const *psDroid, Droid const *psOther)
{
	ASSERT_OR_RETURN(false, psDroid != nullptr && psOther != nullptr, "Bad droids");
	CHECK_DROID(psDroid);
	Vector2i xy = map_coord(psDroid->getPosition.xy());
	Vector2i otherxy = map_coord(psOther->getPosition.xy());
	Vector2i delta = xy - otherxy;
	return delta.x >=-1 && delta.x <=1 &&  delta.y >=-1 && delta.y <=1 ;
}


// check whether a droid is in the neighboring tile to a build position
bool actionReachedBuildPos(Droid const *psDroid, int x, int y, uint16_t dir,
                           StatsObject const *psStats)
{
	ASSERT_OR_RETURN(false, psStats != nullptr && psDroid != nullptr, "Bad stat or droid");
	CHECK_DROID(psDroid);

	StructureBounds b = getStructureBounds(psStats, Vector2i(x, y), dir);

	// do all calculations in half tile units so that
	// the droid moves to within half a tile of the target
	// NOT ANY MORE - JOHN
	Vector2i delta = map_coord(psDroid->getPosition.xy()) - b.map;
	return delta.x >= -1 && delta.x <= b.size.x && delta.y >= -1 && delta.y <= b.size.y;
}


// check if a droid is on the foundations of a new building
static bool actionRemoveDroidsFromBuildPos(unsigned player, Vector2i pos, uint16_t dir, StatsObject *psStats)
{
	ASSERT_OR_RETURN(false, psStats != nullptr, "Bad stat");

	bool buildPosEmpty = true;

	StructureBounds b = getStructureBounds(psStats, pos, dir);

	Vector2i structureCentre = world_coord(b.map) + world_coord(b.size) / 2;
	unsigned structureMaxRadius = iHypot(world_coord(b.size) / 2) + 1; // +1 since iHypot rounds down.

	static GridList gridList;  // static to avoid allocations.
	gridList = gridStartIterate(structureCentre.x, structureCentre.y, structureMaxRadius);
	for (auto gi : gridList)
	{
          Droid *droid = castDroid(gi);
		if (droid == nullptr)
		{
			continue;  // Only looking for droids.
		}

		Vector2i delta = map_coord(droid->getPosition.xy()) - b.map;
		if (delta.x < 0 || delta.x >= b.size.x || delta.y < 0 || delta.y >= b.size.y || isFlying(droid))
		{
			continue;  // Droid not under new structure (just near it).
		}

		buildPosEmpty = false;  // Found a droid, have to move it away.

		if (!aiCheckAlliances(player, droid->owningPlayer))
		{
			continue;  // Enemy droids probably don't feel like moving.
		}

		// TODO If the action code was less convoluted, it would be possible for the droid should drive away instead of just getting moved away.
		Vector2i bestDest(0, 0);  // Dummy initialisation.
		unsigned bestDist = UINT32_MAX;
		for (int y = -1; y <= b.size.y; ++y)
			for (int x = -1; x <= b.size.x; x += y >= 0 && y < b.size.y ? b.size.x + 1 : 1)
			{
				Vector2i dest = world_coord(b.map + Vector2i(x, y)) + Vector2i(TILE_UNITS, TILE_UNITS) / 2;
				unsigned dist = iHypot(droid->getPosition.xy() - dest);
				if (dist < bestDist && !fpathBlockingTile(map_coord(dest.x), map_coord(dest.y), getPropulsionStats(droid)->propulsionType))
				{
					bestDest = dest;
					bestDist = dist;
				}
			}
		if (bestDist != UINT32_MAX)
		{
			// Push the droid out of the way.
			Vector2i newPos =
                            droid->getPosition.xy() + iSinCosR(iAtan2(bestDest - droid->getPosition.xy()), gameTimeAdjustedIncrement(TILE_UNITS));
			droidSetPosition(droid, newPos.x, newPos.y);
		}
	}

	return buildPosEmpty;
}

// whether a tile is suitable for a vtol to land on
static bool vtolLandingTile(SDWORD x, SDWORD y)
{
	if (x < 0 || x >= (SDWORD)mapWidth || y < 0 || y >= (SDWORD)mapHeight)
	{
		return false;
	}

	const MAPTILE *psTile = mapTile(x, y);
	if (psTile->tileInfoBits & BITS_FPATHBLOCK ||
	    TileIsOccupied(psTile) ||
	    terrainType(psTile) == TER_CLIFFFACE ||
	    terrainType(psTile) == TER_WATER)
	{
		return false;
	}
	return true;
}

/**
 * Performs a space-filling spiral-like search from startX,startY up to (and
 * including) radius. For each tile, the search function is called; if it
 * returns 'true', the search will finish immediately.
 *
 * @param startX,startY starting x and y coordinates
 *
 * @param max_radius radius to examine. Search will finish when @c max_radius is exceeded.
 *
 * @param match searchFunction to use; described in typedef
 * \param matchState state for the search function
 * \return true if finished because the searchFunction requested termination,
 *         false if the radius limit was reached
 */
static bool spiralSearch(int startX, int startY, int max_radius, tileMatchFunction match, void *matchState)
{
	// test center tile
	if (match(startX, startY, matchState))
	{
		return true;
	}

	// test for each radius, from 1 to max_radius (inclusive)
	for (int radius = 1; radius <= max_radius; ++radius)
	{
		// choose tiles that are between radius and radius+1 away from center
		// distances are squared
		const int min_distance = radius * radius;
		const int max_distance = min_distance + 2 * radius;

		// X offset from startX
		int dx;

		// dx starts with 1, to visiting tiles on same row or col as start twice
		for (dx = 1; dx <= max_radius; dx++)
		{
			// Y offset from startY
			int dy;

			for (dy = 0; dy <= max_radius; dy++)
			{
				// Current distance, squared
				const int distance = dx * dx + dy * dy;

				// Ignore tiles outside of the current circle
				if (distance < min_distance || distance > max_distance)
				{
					continue;
				}

				// call search function for each of the 4 quadrants of the circle
				if (match(startX + dx, startY + dy, matchState)
				    || match(startX - dx, startY - dy, matchState)
				    || match(startX + dy, startY - dx, matchState)
				    || match(startX - dy, startY + dx, matchState))
				{
					return true;
				}
			}
		}
	}

	return false;
}

/**
 * an internal tileMatchFunction that checks if x and y are coordinates of a
 * valid landing place.
 *
 * @param matchState a pointer to a Vector2i where these coordintates should be stored
 *
 * @return true if coordinates are a valid landing tile, false if not.
 */
static bool vtolLandingTileSearchFunction(int x, int y, void *matchState)
{
	auto *const xyCoords = (Vector2i *)matchState;

	if (vtolLandingTile(x, y))
	{
		xyCoords->x = x;
		xyCoords->y = y;
		return true;
	}

	return false;
}