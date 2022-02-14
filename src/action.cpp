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


Action::Action(ACTION action)
  : action{action}
{
}

Action::Action(ACTION action, BaseObject* targetObject)
  : action{action}, targetObject{targetObject}
  , location{targetObject->getPosition().x, targetObject->getPosition().y}
{
}

Action::Action(ACTION action, Vector2i location)
  : action{action}, location{location}
{
}

Action::Action(ACTION action, Vector2i location, BaseObject* targetObject)
  : action{action}, location{location}, targetObject{targetObject}
{
}

/**
 * An internal tileMatchFunction that checks if \c (x, y) coords are
 * coordinates of a valid landing place
 *
 * @param matchState a pointer to a \c Vector2i where these coordinates should be stored
 * @return \c true if coordinates are a valid landing tile
 */
static bool vtolLandingTileSearchFunction(Vector2i coords, void* matchState)
{
  auto const xyCoords = static_cast<Vector2i*>(matchState);

  if (!vtolCanLandHere(coords.x, coords.y)) {
    return false;
  }
  xyCoords->x = coords.x;
  xyCoords->y = coords.y;
  return true;
}

std::string actionToString(ACTION action)
{
	static std::array<std::string, 
    static_cast<size_t>(ACTION::COUNT) + 1> name {
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

	return name[ static_cast<size_t>(action) ];
}

bool withinRange(Droid const* psDroid, BaseObject const* psObj, int weapon_slot, bool useLongWithOptimum)
{
	auto const psStats = psDroid->weaponManager->weapons[0].stats.get();
	auto const longRange = proj_GetLongRange(psStats, psDroid->playerManager->getPlayer());
	auto const shortRange = proj_GetShortRange(psStats, psDroid->playerManager->getPlayer());

	unsigned rangeSq;
	switch (psDroid->getSecondaryOrder() & DSS_ARANGE_MASK) {
	case DSS_ARANGE_OPTIMUM:
    if (!useLongWithOptimum && weaponShortHit(psStats, psDroid->playerManager->getPlayer())
                      > weaponLongHit(psStats, psDroid->playerManager->getPlayer())) {
			rangeSq = shortRange * shortRange;
      break;
		}
    rangeSq = longRange * longRange;
		break;
	case DSS_ARANGE_SHORT:
		rangeSq = shortRange * shortRange;
		break;
	case DSS_ARANGE_LONG:
		rangeSq = longRange * longRange;
		break;
	default:
		ASSERT(!"unknown attack range order", "unknown attack range order");
		rangeSq = longRange * longRange;
		break;
	}

  auto const radSq = objectPositionSquareDiff(psDroid, psObj);
	/* check max range */
  if (radSq > rangeSq)
    return false;

  /* check min range */
  auto const minrange = proj_GetMinRange(
          psStats, psDroid->playerManager->getPlayer());

  if (radSq >= minrange * minrange || !proj_Direct(psStats)) {
    return true;
  }
  return false;
}

bool targetInsideFiringDistance(Droid const* psDroid, BaseObject const* psObj, WeaponStats const* psStats)
{
  /* if I am a multi-turret droid */
  if (numWeapons(*psDroid) > 1) {
    return false;
  }

	if (!psStats) {
		psStats = psDroid->weaponManager->weapons[0].stats.get();
	}

  // check min range
  auto const minRange = proj_GetMinRange(
          psStats, psDroid->playerManager->getPlayer());

	if (objectPositionSquareDiff(psDroid, psObj) <= minRange * minRange) {
		return true;
	}
	return false;
}

bool rotateTurret(BaseObject const* psAttacker, BaseObject const* psTarget, int slot)
{
  auto weapon = psAttacker->weaponManager->weapons[slot];
  auto rotRate = DEG(ACTION_TURRET_ROTATION_RATE) * 4;
  auto pitchRate = DEG(ACTION_TURRET_ROTATION_RATE) * 2;

	auto const psWeapStats = weapon.stats.get();
	auto rotationTolerance = 0;

	if (!psTarget) return false;

  auto as_droid = dynamic_cast<Droid const*>(psAttacker);
	bool bRepair = as_droid && as_droid->getType() == DROID_TYPE::REPAIRER;

	// extra heavy weapons on some structures need to rotate and pitch more slowly
	if (psWeapStats->weight > HEAVY_WEAPON_WEIGHT && !bRepair) {
		auto excess = DEG(100) * (psWeapStats->weight - HEAVY_WEAPON_WEIGHT) / psWeapStats->weight;
		rotRate = DEG(ACTION_TURRET_ROTATION_RATE) * 2 - excess;
		pitchRate = rotRate / 2;
	}

	auto tRotation = weapon.getRotation().direction;
	auto tPitch = weapon.getRotation().pitch;

	// set the pitch limits based on the weapon stats of the attacker
	int pitchLowerLimit, pitchUpperLimit;
	auto attackerMuzzlePos = psAttacker->getPosition();

	// using for calculating the pitch, but not the direction, in case
  // using the exact direction causes bugs somewhere.
	if (auto as_struct = dynamic_cast<Structure const*>(psAttacker)) {
		calcStructureMuzzleLocation(as_struct, &attackerMuzzlePos, slot);
		pitchLowerLimit = DEG(psWeapStats->minElevation);
		pitchUpperLimit = DEG(psWeapStats->maxElevation);
	}

	if (auto psDroid = dynamic_cast<Droid const*>(psAttacker)) {
    calcDroidMuzzleLocation(psDroid, &attackerMuzzlePos, slot);
    if (psDroid->getType() == DROID_TYPE::WEAPON || isTransporter(*psDroid) ||
        psDroid->getType() == DROID_TYPE::COMMAND || psDroid->getType() == DROID_TYPE::CYBORG ||
        psDroid->getType() == DROID_TYPE::CYBORG_SUPER) {
      pitchLowerLimit = DEG(psWeapStats->minElevation);
			pitchUpperLimit = DEG(psWeapStats->maxElevation);
		}
		else if (psDroid->getType() == DROID_TYPE::REPAIRER) {
			pitchLowerLimit = DEG(REPAIR_PITCH_LOWER);
			pitchUpperLimit = DEG(REPAIR_PITCH_UPPER);
		}
	}

	// get the maximum rotation this frame
	rotRate = MAX(gameTimeAdjustedIncrement(rotRate), DEG(1));
	pitchRate = MAX(gameTimeAdjustedIncrement(pitchRate), DEG(1));

	// point the turret at target
  auto targetRotation = calcDirection(
          psAttacker->getPosition().x, psAttacker->getPosition().y,
          psTarget->getPosition().x, psTarget->getPosition().y);

	//restrict rotationError to =/- 180 degrees
	auto rotationError = angleDelta(targetRotation - (tRotation + psAttacker->getRotation().direction));

	tRotation += clip(rotationError, -rotRate, rotRate); // Addition wrapping intentional.
	if (dynamic_cast<Droid const*>(psAttacker) &&
      dynamic_cast<Droid const*>(psAttacker)->isVtol()) {
		// limit the rotation for vtols
		auto limit = VTOL_TURRET_LIMIT;
		if (psWeapStats->weaponSubClass == WEAPON_SUBCLASS::BOMB ||
        psWeapStats->weaponSubClass == WEAPON_SUBCLASS::EMP) {
			limit = 0; // Don't turn bombs.
			rotationTolerance = VTOL_TURRET_LIMIT_BOMB;
		}
		tRotation = clip(angleDelta(tRotation), -limit, limit); // Cast wrapping intentional.
	}

	bool onTarget = abs(angleDelta(targetRotation - tRotation + psAttacker->getRotation().direction)) <= rotationTolerance;

	/* Set muzzle pitch if not repairing or outside minimum range */
	auto const minRange = proj_GetMinRange(
          psWeapStats, psAttacker->playerManager->getPlayer());

	if (!bRepair && objectPositionSquareDiff(
          psAttacker->getPosition(), psTarget->getPosition()) > minRange * minRange) {
		// get target distance
		auto delta = psTarget->getPosition() - attackerMuzzlePos;

    auto targetPitch = iAtan2(delta.z, iHypot(delta.x, delta.y));
		targetPitch = clip(angleDelta(targetPitch), pitchLowerLimit, pitchUpperLimit);

		tPitch += clip(angleDelta(targetPitch - tPitch), -pitchRate, pitchRate);
		onTarget = onTarget && targetPitch == tPitch;
	}
  weapon.setRotation({static_cast<int>(tRotation),
                        static_cast<int>(tPitch),
                        weapon.getRotation().roll});
	return onTarget;
}

bool targetVisible(Droid const* psDroid, BaseObject const* psTarget,
                   int weapon_slot, bool useLongWithOptimum)
{
	ASSERT_OR_RETURN(false, psTarget != nullptr, "Target is NULL");
	ASSERT_OR_RETURN(false, psDroid->playerManager->getPlayer() < MAX_PLAYERS,
                   "psDroid->player (%d) must be < MAX_PLAYERS",
                   psDroid->playerManager->getPlayer());

  return (orderState(psDroid, ORDER_TYPE::FIRE_SUPPORT) ||
          visibleObject(psDroid, psTarget, false) > UBYTE_MAX / 2) &&
         lineOfFire(psDroid, psTarget, weapon_slot, true) &&
         psTarget->isVisibleToPlayer(psDroid->playerManager->getPlayer()) ||
         numWeapons(*psDroid) == 0 || psDroid->isVtol() &&
         visibleObject(psDroid, psTarget, false);
}

void addAttackRun(Droid* psDroid)
{
  if (psDroid->getOrder()->target == nullptr)
    return;

  auto const psTarget = psDroid->getOrder()->target;

  // get normal vector from droid to target
  auto delta = (psTarget->getPosition() - psDroid->getPosition()).xy();

  // get magnitude of normal vector (Pythagorean theorem) and
  // add waypoint behind target attack length away
  auto dest = psTarget->getPosition().xy() +
          delta * VTOL_ATTACK_LENGTH / std::max(iHypot(delta), 1);

  if (!worldOnMap(dest)) {
    debug(LOG_NEVER, "*** addAttackRun: run off map! ***");
    return;
  }
  psDroid->moveDroidToDirect(dest);
}

void updateAttackRuns(Droid* psDroid)
{
	// don't do attack runs whilst returning to base
  // order back to base after fixed number of attack runs
	if (psDroid->getOrder()->type == ORDER_TYPE::RETURN_TO_BASE ||
      numWeapons(*psDroid) > 0 && vtolEmpty(*psDroid) ||
      psDroid->getMovementData()->status != MOVE_STATUS::HOVER ||
      isCyborg(psDroid)) {
		return;
	}
  addAttackRun(psDroid);
}

Vector2i getFallbackPosition(BaseObject const* psObj, BaseObject const* psTarget)
{
	// get the distance vector from the target to the object
	auto xdiff = psObj->getPosition().x - psTarget->getPosition().x;
	auto ydiff = psObj->getPosition().y - psTarget->getPosition().y;
	auto const len = iHypot(xdiff, ydiff);

  xdiff = len == 0 ? TILE_UNITS : xdiff * TILE_UNITS / len;
  ydiff = len == 0 ? TILE_UNITS : ydiff * TILE_UNITS / len;

	// create the fallback position
  auto fallbackPos = Vector2i{psObj->getPosition().x + xdiff * PULL_BACK_DIST,
                              psObj->getPosition().y + ydiff * PULL_BACK_DIST};

	// make sure the coordinates stay within the map bounds
	clip_world_offmap(&fallbackPos.x, &fallbackPos.y);
  return fallbackPos;
}

bool adjacentToOtherDroid(Droid const* psDroid, Droid const* psOther)
{
	ASSERT_OR_RETURN(false, psDroid != nullptr && psOther != nullptr, "Bad droids");

  auto const delta = map_coord(psDroid->getPosition().xy())
                     - map_coord(psOther->getPosition().xy());

	return delta.x >= -1 && delta.x <= 1 && delta.y >= -1 && delta.y <= 1;
}

bool adjacentToBuildSite(Droid const* psDroid, BaseStats const* psStats,
                         Vector2i location, uint16_t direction)
{
	ASSERT_OR_RETURN(false, psStats != nullptr && psDroid != nullptr, "Bad stat or droid");
  auto const bounds = getStructureBounds(
          dynamic_cast<StructureStats const*>(psStats), location, direction);

	auto const delta = map_coord(
          psDroid->getPosition().xy()) - bounds.map;

	return delta.x >= -1 && delta.x <= bounds.size.x &&
         delta.y >= -1 && delta.y <= bounds.size.y;
}

bool pushDroidsAwayFromBuildSite(unsigned player, Vector2i pos, uint16_t dir, BaseStats const* psStats)
{
	ASSERT_OR_RETURN(false, psStats != nullptr, "Bad stat");

	bool buildPosEmpty = true;

	auto const b = getStructureBounds(
          dynamic_cast<StructureStats const*>(psStats), pos, dir);

	auto const structureCentre = world_coord(b.map) + world_coord(b.size) / 2;
  // +1 since iHypot rounds down
	auto const structureMaxRadius = iHypot(world_coord(b.size) / 2) + 1;

	static GridList gridList; // static to avoid allocations.
	gridList = gridStartIterate(structureCentre.x, structureCentre.y, structureMaxRadius);
	for (auto& gi : gridList)
	{
		auto droid = dynamic_cast<Droid*>(gi);
		if (droid == nullptr) continue; // Only looking for droids.

		auto const delta = map_coord(droid->getPosition().xy()) - b.map;
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
        auto const dest = world_coord(b.map + Vector2i(x, y)) + Vector2i(TILE_UNITS, TILE_UNITS) / 2;
        auto const dist = iHypot(droid->getPosition().xy() - dest);
        if (dist < bestDist && !fpathBlockingTile(
                map_coord(dest),
                dynamic_cast<PropulsionStats const*>(
                        droid->getComponent(COMPONENT_TYPE::PROPULSION))->propulsionType)) {
          bestDest = dest;
          bestDist = dist;
        }
      }
    }

    if (bestDist == UINT32_MAX) continue;
    // Push the droid out of the way.
    auto const newPos = droid->getPosition().xy() + iSinCosR(
            iAtan2(bestDest - droid->getPosition().xy()),
            gameTimeAdjustedIncrement(TILE_UNITS));

    droidSetPosition(droid, newPos);
  }
	return buildPosEmpty;
}

void newAction(Droid* psDroid, ACTION action)
{
	Action sAction{action};
	psDroid->actionDroidBase(&sAction);
}

void newAction(Droid* psDroid, ACTION action, Vector2i location)
{
	Action sAction{action, location};
	psDroid->actionDroidBase(&sAction);
}

void newAction(Droid* psDroid, ACTION action, BaseObject* targetObject)
{
	Action sAction{action, targetObject};
	psDroid->actionDroidBase(&sAction);
}

void newAction(Droid* psDroid, ACTION action, BaseObject* targetObject, Vector2i location)
{
	Action sAction{action, location, targetObject};
	psDroid->actionDroidBase(&sAction);
}

bool spiralSearch(Vector2i startCoords, int maxRadius, tileMatchFunction match, void* matchState)
{
	// test center tile
	if (match(startCoords, matchState)) {
		return true;
	}

	// test for each radius, from 1 to max_radius (inclusive)
	for (auto radius = 1; radius <= maxRadius; ++radius)
	{
		// choose tiles that are between radius and radius+1 away from center
		// distances are squared

    // dx starts with 1, to visiting tiles on same row or col as start twice
		for (auto dx = 1; dx <= maxRadius; dx++)
		{
			for (auto dy = 0; dy <= maxRadius; dy++)
			{
				// Current distance, squared
				auto const distance = dx * dx + dy * dy;

				// Ignore tiles outside the current circle
				if (distance < radius * radius || distance > radius * radius + 2 * radius) {
					continue;
				}

				// call search function for each of the 4 quadrants of the circle
				if (match({startCoords.x + dx, startCoords.y + dy}, matchState) ||
            match({startCoords.x - dx, startCoords.y - dy}, matchState) ||
            match({startCoords.x + dy, startCoords.y - dx}, matchState) ||
            match({startCoords.x - dy, startCoords.y + dx}, matchState)) {
					return true;
				}
			}
		}
	}
	return false;
}

bool findVtolLandingPosition(Droid const* psDroid, Vector2i* p)
{
  setBlockingFlags(*psDroid, BITS_FPATHBLOCK);

	// search for landing tile; will stop when found or radius exceeded
	Vector2i xyCoords{0, 0};
  bool const foundTile = spiralSearch(map_coord(*p), VTOL_LANDING_RADIUS,
                                      vtolLandingTileSearchFunction, &xyCoords);

	if (foundTile) {
		objTrace(psDroid->getId(), "Unit %d landing pos (%d,%d)", psDroid->getId(), xyCoords.x, xyCoords.y);
		p->x = world_coord(xyCoords.x) + TILE_UNITS / 2;
		p->y = world_coord(xyCoords.y) + TILE_UNITS / 2;
	}

	// clear blocking flags for all the other droids
  clearBlockingFlags(*psDroid, BITS_FPATHBLOCK);
	return foundTile;
}
