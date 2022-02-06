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
 * @file action.cpp
 * Functions for setting the action of a droid
 */

#include "lib/framework/fixedpoint.h"
#include "lib/framework/math_ext.h"
#include "lib/framework/vector.h"

#include "action.h"
#include "droid.h"
#include "fpath.h"
#include "geometry.h"
#include "map.h"
#include "mapgrid.h"
#include "move.h"
#include "projectile.h"
#include "visibility.h"


std::string actionToString(ACTION action)
{
	static std::array<std::string, 
    static_cast<std::size_t>(ACTION::COUNT) + 1> name {
		"NONE", 
		"MOVE", 
		"BUILD", 
		"DEMOLISH",
		"REPAIR", 
		"ATTACK", 
		"OBSERVE", 
		"FIRE_SUPPORT", 
		"SULK", 
		"DESTRUCT", 
		"TRANSPORT_OUT", 
		"TRANSPORT_WAIT_TO_FLY_IN", 
		"TRANSPORT_IN", 
		"DROID_REPAIR", 
		"RESTORE", 
		"MOVE_FIRE", 
		"MOVE_TO_BUILD", 
		"MOVE_TO_DEMOLISH", 
		"MOVE_TO_REPAIR",
		"BUILD_WANDER", 
		"MOVE_TO_ATTACK",
		"ROTATE_TO_ATTACK",
		"MOVE_TO_OBSERVE",
		"WAIT_FOR_REPAIR",
		"MOVE_TO_REPAIR_POINT",
		"WAIT_DURING_REPAIR",
		"MOVE_TO_DROID_REPAIR",
		"MOVE_TO_RESTORE",
		"MOVE_TO_REARM",
		"WAIT_FOR_REARM",
		"MOVE_TO_REARM_POINT",
		"WAIT_DURING_REARM",
		"VTOL_ATTACK",
		"CLEAR_REARM_PAD",
		"RETURN_TO_POS",
		"FIRE_SUPPORT_RETREAT",
		"CIRCLE"
	};

	return name[ static_cast<std::size_t>(action) ];
}

// check if a target is within weapon range
bool actionInRange(Droid const* psDroid, BaseObject const* psObj, int weapon_slot, bool useLongWithOptimum)
{
	auto psStats = psDroid->weaponManager->weapons[0].stats.get();

	const auto dx = psDroid->getPosition().x - psObj->getPosition().x;
	const auto dy = psDroid->getPosition().y - psObj->getPosition().y;

	const auto radSq = dx * dx + dy * dy;
	const auto longRange = proj_GetLongRange(psStats, psDroid->playerManager->getPlayer());
	const auto shortRange = proj_GetShortRange(psStats, psDroid->playerManager->getPlayer());

	auto rangeSq = 0;
	switch (psDroid->getSecondaryOrder() & DSS_ARANGE_MASK) {
	case DSS_ARANGE_OPTIMUM:
		if (!useLongWithOptimum && weaponShortHit(
            psStats, psDroid->playerManager->getPlayer())
                               > weaponLongHit(psStats, psDroid->playerManager->getPlayer())) {
			rangeSq = shortRange * shortRange;
		}
		else {
			rangeSq = longRange * longRange;
		}
		break;
	case DSS_ARANGE_SHORT:
		rangeSq = shortRange * shortRange;
		break;
	case DSS_ARANGE_LONG:
		rangeSq = longRange * longRange;
		break;
	default:
		ASSERT(!"unknown attackrange order", "unknown attack range order");
		rangeSq = longRange * longRange;
		break;
	}

	/* check max range */
	if (radSq <= rangeSq) {
		/* check min range */
		const auto minrange = proj_GetMinRange(psStats, psDroid->playerManager->getPlayer());
		if (radSq >= minrange * minrange || !proj_Direct(psStats)) {
			return true;
		}
	}
	return false;
}

// check if a target is inside minimum weapon range
static bool actionInsideMinRange(Droid const* psDroid, BaseObject const* psObj, WeaponStats const* psStats)
{
	if (!psStats) {
		psStats = psDroid->weaponManager->weapons[0].stats.get();
	}

	/* if I am a multi-turret droid */
	if (numWeapons(*psDroid) > 1) {
		return false;
	}

	const auto dx = psDroid->getPosition().x - psObj->getPosition().x;
	const auto dy = psDroid->getPosition().y - psObj->getPosition().y;
	const auto radSq = dx * dx + dy * dy;
	const auto minRange = proj_GetMinRange(psStats, psDroid->playerManager->getPlayer());
	const auto rangeSq = minRange * minRange;

	// check min range
	if (radSq <= rangeSq) {
		return true;
	}
	return false;
}

/* returns true if on target */
bool actionTargetTurret(BaseObject* psAttacker, BaseObject* psTarget, int weaponSlot)
{
  auto weapon = psAttacker->weaponManager->weapons[weaponSlot];
  auto rotRate = DEG(ACTION_TURRET_ROTATION_RATE) * 4;
  auto pitchRate = DEG(ACTION_TURRET_ROTATION_RATE) * 2;

	auto psWeapStats = weapon.stats.get();
	unsigned tRotation, tPitch;
	unsigned targetRotation;
	auto rotationTolerance = 0;
	int pitchLowerLimit, pitchUpperLimit;

	if (!psTarget) {
		return false;
	}

  auto as_droid = dynamic_cast<Droid*>(psAttacker);
	bool bRepair = as_droid && as_droid->getType() == DROID_TYPE::REPAIRER;

	// extra heavy weapons on some structures need to rotate and pitch more slowly
	if (psWeapStats->weight > HEAVY_WEAPON_WEIGHT && !bRepair) {
		auto excess = DEG(100) * (psWeapStats->weight - HEAVY_WEAPON_WEIGHT) / psWeapStats->weight;
		rotRate = DEG(ACTION_TURRET_ROTATION_RATE) * 2 - excess;
		pitchRate = rotRate / 2;
	}

	tRotation = weapon.getRotation().direction;
	tPitch = weapon.getRotation().pitch;

	// set the pitch limits based on the weapon stats of the attacker
	pitchLowerLimit = pitchUpperLimit = 0;
	auto attackerMuzzlePos = psAttacker->getPosition();

	// using for calculating the pitch, but not the direction, in case
  // using the exact direction causes bugs somewhere.
	if (auto as_struct = dynamic_cast<Structure*>(psAttacker)) {
		calcStructureMuzzleLocation(as_struct, &attackerMuzzlePos, weaponSlot);
		pitchLowerLimit = DEG(psWeapStats->minElevation);
		pitchUpperLimit = DEG(psWeapStats->maxElevation);
	}
	else if (auto psDroid = dynamic_cast<Droid*>(psAttacker)) {
		calcDroidMuzzleLocation(psDroid, &attackerMuzzlePos, weaponSlot);
		if (psDroid->getType() == DROID_TYPE::WEAPON ||
       isTransporter(*psDroid) ||
       psDroid->getType() == DROID_TYPE::COMMAND ||
       psDroid->getType() == DROID_TYPE::CYBORG ||
       psDroid->getType() == DROID_TYPE::CYBORG_SUPER) {
			pitchLowerLimit = DEG(psWeapStats->minElevation);
			pitchUpperLimit = DEG(psWeapStats->maxElevation);
		}
		else if (psDroid->getType() == DROID_TYPE::REPAIRER) {
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
	targetRotation = calcDirection(psAttacker->getPosition().x, psAttacker->getPosition().y,
                                 psTarget->getPosition().x, psTarget->getPosition().y);

	//restrict rotationerror to =/- 180 degrees
	auto rotationError = angleDelta(targetRotation - (tRotation + psAttacker->getRotation().direction));

	tRotation += clip(rotationError, -rotRate, rotRate); // Addition wrapping intentional.
	if (dynamic_cast<Droid*>(psAttacker) && dynamic_cast<Droid*>(psAttacker)->isVtol()) {
		// limit the rotation for vtols
		auto limit = VTOL_TURRET_LIMIT;
		if (psWeapStats->weaponSubClass == WEAPON_SUBCLASS::BOMB ||
        psWeapStats->weaponSubClass == WEAPON_SUBCLASS::EMP) {
			limit = 0; // Don't turn bombs.
			rotationTolerance = VTOL_TURRET_LIMIT_BOMB;
		}
		tRotation = (uint16_t)clip(angleDelta(tRotation), -limit, limit); // Cast wrapping intentional.
	}
	bool onTarget = abs(angleDelta(targetRotation - (tRotation + psAttacker->getRotation().direction))) <= rotationTolerance;

	/* Set muzzle pitch if not repairing or outside minimum range */
	const auto minRange = proj_GetMinRange(psWeapStats, psAttacker->playerManager->getPlayer());
	if (!bRepair && (unsigned)objectPositionSquareDiff(psAttacker->getPosition(),
                                                     psTarget->getPosition()) > minRange * minRange) {
		/* get target distance */
		auto delta = psTarget->getPosition() - attackerMuzzlePos;
		auto dxy = iHypot(delta.x, delta.y);

		auto targetPitch = iAtan2(delta.z, dxy);
		targetPitch = (uint16_t)clip(angleDelta(targetPitch), pitchLowerLimit, pitchUpperLimit);
		// Cast wrapping intended.
		auto pitchError = angleDelta(targetPitch - tPitch);

    // addition wrapping intended
		tPitch += clip(pitchError, -pitchRate, pitchRate);
		onTarget = onTarget && targetPitch == tPitch;
	}
  weapon.setRotation({static_cast<int>(tRotation),
                        static_cast<int>(tPitch),
                        weapon.getRotation().roll});

	return onTarget;
}

// return whether a droid can see a target to fire on it
bool actionVisibleTarget(Droid const* psDroid, BaseObject const* psTarget, int weapon_slot, bool useLongWithOptimum)
{
	ASSERT_OR_RETURN(false, psTarget != nullptr, "Target is NULL");
	ASSERT_OR_RETURN(false, psDroid->playerManager->getPlayer() < MAX_PLAYERS,
                   "psDroid->player (%" PRIu8 ") must be < MAX_PLAYERS",
                   psDroid->playerManager->getPlayer());

	if (!psTarget->isVisibleToPlayer(psDroid->playerManager->getPlayer())) {
		return false;
	}
	if ((numWeapons(*psDroid) == 0 || psDroid->isVtol() &&
      visibleObject(psDroid, psTarget, false))) {
		return true;
	}
	return (orderState(psDroid, ORDER_TYPE::FIRE_SUPPORT) ||
          visibleObject(psDroid, psTarget, false) > UBYTE_MAX / 2) &&
         lineOfFire(psDroid, psTarget, weapon_slot, true);
}

static void actionAddVtolAttackRun(Droid* psDroid)
{
  auto psTarget = psDroid->getTarget(0);

	if (psDroid->getOrder()->target != nullptr)
		psTarget = psDroid->getOrder()->target;
	else return;

	/* get normal vector from droid to target */
	auto delta = (psTarget->getPosition() - psDroid->getPosition()).xy();
	/* get magnitude of normal vector (Pythagorean theorem) */
	auto dist = std::max(iHypot(delta), 1);
	/* add waypoint behind target attack length away*/
	auto dest = psTarget->getPosition().xy() + delta * VTOL_ATTACK_LENGTH / dist;

	if (!worldOnMap(dest))
		debug(LOG_NEVER, "*** actionAddVtolAttackRun: run off map! ***");
	else
		moveDroidToDirect(psDroid, dest.x, dest.y);
}

static void actionUpdateVtolAttack(Droid* psDroid)
{
	/* don't do attack runs whilst returning to base */
	if (psDroid->getOrder()->type == ORDER_TYPE::RETURN_TO_BASE) {
		return;
	}

	/* order back to base after fixed number of attack runs */
	if (numWeapons(*psDroid) > 0 && vtolEmpty(*psDroid)) {
		moveToRearm(psDroid);
		return;
	}

	/* circle around target if hovering and not cyborg */
	if (psDroid->getMovementData()->status == MOVE_STATUS::HOVER &&
      !isCyborg(psDroid)) {
		actionAddVtolAttackRun(psDroid);
	}
}

// calculate a position for units to pull back to if they
// need to increase the range between them and a target
static void actionCalcPullBackPoint(BaseObject* psObj, BaseObject* psTarget, int* px, int* py)
{
	// get the vector from the target to the object
	auto xdiff = psObj->getPosition().x - psTarget->getPosition().x;
	auto ydiff = psObj->getPosition().y - psTarget->getPosition().y;
	const auto len = iHypot(xdiff, ydiff);

	if (len == 0) {
		xdiff = TILE_UNITS;
		ydiff = TILE_UNITS;
	}
	else {
		xdiff = (xdiff * TILE_UNITS) / len;
		ydiff = (ydiff * TILE_UNITS) / len;
	}

	// create the position
	*px = psObj->getPosition().x + xdiff * PULL_BACK_DIST;
	*py = psObj->getPosition().y + ydiff * PULL_BACK_DIST;

	// make sure coordinates stay inside of the map
	clip_world_offmap(px, py);
}

// check whether a droid is in the neighboring tile of another droid
bool actionReachedDroid(Droid const* psDroid, Droid const* psOther)
{
	ASSERT_OR_RETURN(false, psDroid != nullptr && psOther != nullptr, "Bad droids");
	auto xy = map_coord(psDroid->getPosition().xy());
	auto otherxy = map_coord(psOther->getPosition().xy());
	auto delta = xy - otherxy;
	return delta.x >= -1 && delta.x <= 1 && delta.y >= -1 && delta.y <= 1;
}

// check whether a droid is in the neighboring tile to a build position
bool actionReachedBuildPos(Droid const* psDroid, int x, int y, uint16_t dir, BaseStats const* psStats)
{
	ASSERT_OR_RETURN(false, psStats != nullptr &&
                          psDroid != nullptr, "Bad stat or droid");

	auto b = getStructureBounds(
          dynamic_cast<StructureStats const*>(psStats),
          Vector2i(x, y), dir);

	// do all calculations in half tile units so that
	// the droid moves to within half a tile of the target
	// NOT ANY MORE - JOHN
	Vector2i delta = map_coord(psDroid->getPosition().xy()) - b.map;
	return delta.x >= -1 && delta.x <= b.size.x &&
         delta.y >= -1 && delta.y <= b.size.y;
}

// check if a droid is on the foundations of a new building
static bool actionRemoveDroidsFromBuildPos(unsigned player, Vector2i pos, uint16_t dir, BaseStats* psStats)
{
	ASSERT_OR_RETURN(false, psStats != nullptr, "Bad stat");

	bool buildPosEmpty = true;

	auto b = getStructureBounds(
          dynamic_cast<StructureStats const*>(psStats),
          pos, dir);

	auto structureCentre = world_coord(b.map) + world_coord(b.size) / 2;
	auto structureMaxRadius = iHypot(world_coord(b.size) / 2) + 1; // +1 since iHypot rounds down.

	static GridList gridList; // static to avoid allocations.
	gridList = gridStartIterate(structureCentre.x, structureCentre.y, structureMaxRadius);
	for (auto& gi : gridList)
	{
		auto droid = dynamic_cast<Droid*>(gi);
		if (droid == nullptr) continue; // Only looking for droids.

		Vector2i delta = map_coord(droid->getPosition().xy()) - b.map;
		if (delta.x < 0 || delta.x >= b.size.x || delta.y < 0 ||
        delta.y >= b.size.y || isFlying(droid)) {
			continue; // Droid not under new structure (just near it).
		}

		buildPosEmpty = false; // Found a droid, have to move it away.

		if (!aiCheckAlliances(player, droid->playerManager->getPlayer())) {
			continue; // Enemy droids probably don't feel like moving.
		}

		// TODO If the action code was less convoluted, it would be possible for the droid should drive away instead of just getting moved away.
		Vector2i bestDest(0, 0); // Dummy initialisation.
		auto bestDist = UINT32_MAX;
		for (auto y = -1; y <= b.size.y; ++y)
    {
      for (auto x = -1; x <= b.size.x; x += y >= 0 && y < b.size.y ? b.size.x + 1 : 1) {
        Vector2i dest = world_coord(b.map + Vector2i(x, y)) + Vector2i(TILE_UNITS, TILE_UNITS) / 2;
        unsigned dist = iHypot(droid->getPosition().xy() - dest);
        if (dist < bestDist &&
            !fpathBlockingTile(map_coord(dest.x),
                               map_coord(dest.y),
                               dynamic_cast<PropulsionStats const *>(
                                       droid->getComponent(COMPONENT_TYPE::PROPULSION))->propulsionType)) {
          bestDest = dest;
          bestDist = dist;
        }
      }
    }
		if (bestDist != UINT32_MAX) {
			// Push the droid out of the way.
			auto newPos = droid->getPosition().xy() + iSinCosR(
              iAtan2(bestDest - droid->getPosition().xy()),
              gameTimeAdjustedIncrement(TILE_UNITS));

			droidSetPosition(droid, newPos.x, newPos.y);
		}
	}
	return buildPosEmpty;
}

/* Give a droid an action */
void actionDroid(Droid* psDroid, ACTION action)
{
	Action sAction;

	sAction.action = action;
	psDroid->actionDroidBase(&sAction);
}

/* Give a droid an action with a location target */
void actionDroid(Droid* psDroid, ACTION action, unsigned x, unsigned y)
{
	Action sAction;

	sAction.action = action;
	sAction.x = x;
	sAction.y = y;
	psDroid->actionDroidBase(&sAction);
}

/* Give a droid an action with an object target */
void actionDroid(Droid* psDroid, ACTION action, BaseObject* psObj)
{
	Action sAction;

	sAction.action = action;
	sAction.targetObject = psObj;
	sAction.x = psObj->getPosition().x;
	sAction.y = psObj->getPosition().y;
	psDroid->actionDroidBase(&sAction);
}

/* Give a droid an action with an object target and a location */
void actionDroid(Droid* psDroid, ACTION action,
                 BaseObject* psObj, unsigned x, unsigned y)
{
	Action sAction;

	sAction.action = action;
	sAction.targetObject = psObj;
	sAction.x = x;
	sAction.y = y;
	psDroid->actionDroidBase(&sAction);
}


static bool spiralSearch(int startX, int startY, int max_radius, tileMatchFunction match, void* matchState)
{
	// test center tile
	if (match(startX, startY, matchState)) {
		return true;
	}

	// test for each radius, from 1 to max_radius (inclusive)
	for (auto radius = 1; radius <= max_radius; ++radius)
	{
		// choose tiles that are between radius and radius+1 away from center
		// distances are squared
		const auto min_distance = radius * radius;
		const auto max_distance = min_distance + 2 * radius;

		// dx starts with 1, to visiting tiles on same row or col as start twice
		for (auto dx = 1; dx <= max_radius; dx++)
		{
			for (auto dy = 0; dy <= max_radius; dy++)
			{
				// Current distance, squared
				const auto distance = dx * dx + dy * dy;

				// Ignore tiles outside of the current circle
				if (distance < min_distance || distance > max_distance) {
					continue;
				}

				// call search function for each of the 4 quadrants of the circle
				if (match(startX + dx, startY + dy, matchState) ||
            match(startX - dx, startY - dy, matchState) ||
            match(startX + dy, startY - dx, matchState) ||
            match(startX - dy, startY + dx, matchState)) {
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
 * @param matchState a pointer to a Vector2i where these coordinates should be stored
 *
 * @return true if coordinates are a valid landing tile, false if not.
 */
static bool vtolLandingTileSearchFunction(int x, int y, void* matchState)
{
	auto const xyCoords = (Vector2i*)matchState;

	if (vtolCanLandHere(x, y)) {
		xyCoords->x = x;
		xyCoords->y = y;
		return true;
	}
	return false;
}

// Choose a landing position for a VTOL when it goes to rearm that is close to rearm
// pad but not on it, since it may be busy by the time we get there.
bool actionVTOLLandingPos(Droid const* psDroid, Vector2i* p)
{
	/* Initial box dimensions and set iteration count to zero */
	auto startX = map_coord(p->x);
	auto startY = map_coord(p->y);

	// set blocking flags for all the other droids
	for (auto const& psCurr : playerList[psDroid->playerManager->getPlayer()].droids)
	{
		static Vector2i t(0, 0);
		if (psCurr.isStationary()) {
			t = map_coord(psCurr.getPosition().xy());
		}
		else {
			t = map_coord(psCurr.getMovementData()->destination);
		}
		if (&psCurr != psDroid) {
			if (tileOnMap(t)) {
				mapTile(t)->tileInfoBits |= BITS_FPATHBLOCK;
			}
		}
	}

	// search for landing tile; will stop when found or radius exceeded
	Vector2i xyCoords(0, 0);
	const bool foundTile = spiralSearch(startX, startY, VTOL_LANDING_RADIUS,
	                                    vtolLandingTileSearchFunction, &xyCoords);
	if (foundTile) {
		objTrace(psDroid->getId(), "Unit %d landing pos (%d,%d)", psDroid->getId(), xyCoords.x, xyCoords.y);
		p->x = world_coord(xyCoords.x) + TILE_UNITS / 2;
		p->y = world_coord(xyCoords.y) + TILE_UNITS / 2;
	}

	// clear blocking flags for all the other droids
	for (auto const& psCurr : playerList[psDroid->playerManager->getPlayer()].droids)
	{
		Vector2i t(0, 0);
		if (psCurr.isStationary()) {
			t = map_coord(psCurr.getPosition().xy());
		}
		else {
			t = map_coord(psCurr.getMovementData()->destination);
		}
		if (tileOnMap(t)) {
			mapTile(t)->tileInfoBits &= ~BITS_FPATHBLOCK;
		}
	}
	return foundTile;
}
