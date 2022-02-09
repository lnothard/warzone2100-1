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

  if (vtolCanLandHere(coords.x, coords.y)) {
    xyCoords->x = coords.x;
    xyCoords->y = coords.y;
    return true;
  }
  return false;
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

bool actionInRange(Droid const* psDroid, BaseObject const* psObj, int weapon_slot, bool useLongWithOptimum)
{
	auto psStats = psDroid->weaponManager->weapons[0].stats.get();

	auto const dx = psDroid->getPosition().x - psObj->getPosition().x;
	auto const dy = psDroid->getPosition().y - psObj->getPosition().y;

	auto const radSq = dx * dx + dy * dy;
	auto const longRange = proj_GetLongRange(psStats, psDroid->playerManager->getPlayer());
	auto const shortRange = proj_GetShortRange(psStats, psDroid->playerManager->getPlayer());

	unsigned rangeSq;
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
		ASSERT(!"unknown attack range order", "unknown attack range order");
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

bool actionInsideMinRange(Droid const* psDroid, BaseObject const* psObj, WeaponStats const* psStats)
{
	if (!psStats) {
		psStats = psDroid->weaponManager->weapons[0].stats.get();
	}

	/* if I am a multi-turret droid */
	if (numWeapons(*psDroid) > 1) {
		return false;
	}

	auto const dx = psDroid->getPosition().x - psObj->getPosition().x;
	auto const dy = psDroid->getPosition().y - psObj->getPosition().y;
	auto const radSq = dx * dx + dy * dy;
	auto const minRange = proj_GetMinRange(psStats, psDroid->playerManager->getPlayer());
	auto const rangeSq = minRange * minRange;

	// check min range
	if (radSq <= rangeSq) {
		return true;
	}
	return false;
}

bool actionTargetTurret(BaseObject const* psAttacker, BaseObject const* psTarget, int weaponSlot)
{
  auto weapon = psAttacker->weaponManager->weapons[weaponSlot];
  auto rotRate = DEG(ACTION_TURRET_ROTATION_RATE) * 4;
  auto pitchRate = DEG(ACTION_TURRET_ROTATION_RATE) * 2;

	auto const psWeapStats = weapon.stats.get();
	auto rotationTolerance = 0;

	if (!psTarget) {
		return false;
	}

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
		calcStructureMuzzleLocation(as_struct, &attackerMuzzlePos, weaponSlot);
		pitchLowerLimit = DEG(psWeapStats->minElevation);
		pitchUpperLimit = DEG(psWeapStats->maxElevation);
	}
	else if (auto psDroid = dynamic_cast<Droid const*>(psAttacker)) {
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
	auto targetRotation = calcDirection(psAttacker->getPosition().x, psAttacker->getPosition().y,
                                 psTarget->getPosition().x, psTarget->getPosition().y);

	//restrict rotationError to =/- 180 degrees
	auto rotationError = angleDelta(targetRotation - (tRotation + psAttacker->getRotation().direction));

	tRotation += clip(rotationError, -rotRate, rotRate); // Addition wrapping intentional.
	if (dynamic_cast<Droid const*>(psAttacker) && dynamic_cast<Droid const*>(psAttacker)->isVtol()) {
		// limit the rotation for vtols
		auto limit = VTOL_TURRET_LIMIT;
		if (psWeapStats->weaponSubClass == WEAPON_SUBCLASS::BOMB ||
        psWeapStats->weaponSubClass == WEAPON_SUBCLASS::EMP) {
			limit = 0; // Don't turn bombs.
			rotationTolerance = VTOL_TURRET_LIMIT_BOMB;
		}
		tRotation = clip(angleDelta(tRotation), -limit, limit); // Cast wrapping intentional.
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

bool actionVisibleTarget(Droid const* psDroid, BaseObject const* psTarget,
                         int weapon_slot, bool useLongWithOptimum)
{
	ASSERT_OR_RETURN(false, psTarget != nullptr, "Target is NULL");
	ASSERT_OR_RETURN(false, psDroid->playerManager->getPlayer() < MAX_PLAYERS,
                   "psDroid->player (%d) must be < MAX_PLAYERS",
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

void actionAddVtolAttackRun(Droid* psDroid)
{
  BaseObject const* psTarget;

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
		moveDroidToDirect(psDroid, {dest.x, dest.y});
}

void actionUpdateVtolAttack(Droid* psDroid)
{
	/* don't do attack runs whilst returning to base */
	if (psDroid->getOrder()->type == ORDER_TYPE::RETURN_TO_BASE) {
		return;
	}

	/* order back to base after fixed number of attack runs */
	if (numWeapons(*psDroid) > 0 && vtolEmpty(*psDroid)) {
		psDroid->moveToRearm();
		return;
	}

	/* circle around target if hovering and not cyborg */
	if (psDroid->getMovementData()->status == MOVE_STATUS::HOVER && !isCyborg(psDroid)) {
		actionAddVtolAttackRun(psDroid);
	}
}

void actionCalcPullBackPoint(BaseObject const* psObj, BaseObject const* psTarget, int* px, int* py)
{
	// get the distance vector from the target to the object
	auto xdiff = psObj->getPosition().x - psTarget->getPosition().x;
	auto ydiff = psObj->getPosition().y - psTarget->getPosition().y;
	auto const len = iHypot(xdiff, ydiff);

	if (len == 0) {
		xdiff = TILE_UNITS;
		ydiff = TILE_UNITS;
	}
	else {
		xdiff = xdiff * TILE_UNITS / len;
		ydiff = ydiff * TILE_UNITS / len;
	}

	// create the fallback position
	*px = psObj->getPosition().x + xdiff * PULL_BACK_DIST;
	*py = psObj->getPosition().y + ydiff * PULL_BACK_DIST;

	// make sure the coordinates stay within the map bounds
	clip_world_offmap(px, py);
}

bool actionReachedDroid(Droid const* psDroid, Droid const* psOther)
{
	ASSERT_OR_RETURN(false, psDroid != nullptr && psOther != nullptr, "Bad droids");
  auto const delta = map_coord(psDroid->getPosition().xy())
                     - map_coord(psOther->getPosition().xy());

	return delta.x >= -1 && delta.x <= 1 && delta.y >= -1 && delta.y <= 1;
}

bool actionReachedBuildPos(Droid const* psDroid, Vector2i location, uint16_t dir, BaseStats const* psStats)
{
	ASSERT_OR_RETURN(false, psStats != nullptr &&
                          psDroid != nullptr, "Bad stat or droid");

	auto const b = getStructureBounds(
          dynamic_cast<StructureStats const*>(psStats), location, dir);

	auto const delta = map_coord(psDroid->getPosition().xy()) - b.map;
	return delta.x >= -1 && delta.x <= b.size.x &&
         delta.y >= -1 && delta.y <= b.size.y;
}

bool actionRemoveDroidsFromBuildPos(unsigned player, Vector2i pos, uint16_t dir, BaseStats const* psStats)
{
	ASSERT_OR_RETURN(false, psStats != nullptr, "Bad stat");

	bool buildPosEmpty = true;

	auto const b = getStructureBounds(
          dynamic_cast<StructureStats const*>(psStats),
          pos, dir);

	auto const structureCentre = world_coord(b.map) + world_coord(b.size) / 2;
	auto const structureMaxRadius = iHypot(world_coord(b.size) / 2) + 1; // +1 since iHypot rounds down.

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
        if (dist < bestDist &&
            !fpathBlockingTile(map_coord(dest),
                               dynamic_cast<PropulsionStats const *>(
                                       droid->getComponent(COMPONENT_TYPE::PROPULSION))->propulsionType)) {
          bestDest = dest;
          bestDist = dist;
        }
      }
    }
		if (bestDist != UINT32_MAX) {
			// Push the droid out of the way.
			auto const newPos = droid->getPosition().xy() + iSinCosR(
              iAtan2(bestDest - droid->getPosition().xy()),
              gameTimeAdjustedIncrement(TILE_UNITS));

			droidSetPosition(droid, newPos.x, newPos.y);
		}
	}
	return buildPosEmpty;
}

void actionDroid(Droid* psDroid, ACTION action)
{
	Action sAction{action};
	psDroid->actionDroidBase(&sAction);
}

void actionDroid(Droid* psDroid, ACTION action, Vector2i location)
{
	Action sAction{action, location};
	psDroid->actionDroidBase(&sAction);
}

void actionDroid(Droid* psDroid, ACTION action, BaseObject* targetObject)
{
	Action sAction{action, targetObject};
	psDroid->actionDroidBase(&sAction);
}

void actionDroid(Droid* psDroid, ACTION action, Vector2i location, BaseObject* targetObject)
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
		auto const minDistance = radius * radius;
		auto const maxDistance = minDistance + 2 * radius;

		// dx starts with 1, to visiting tiles on same row or col as start twice
		for (auto dx = 1; dx <= maxRadius; dx++)
		{
			for (auto dy = 0; dy <= maxRadius; dy++)
			{
				// Current distance, squared
				auto const distance = dx * dx + dy * dy;

				// Ignore tiles outside the current circle
				if (distance < minDistance || distance > maxDistance) {
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

bool actionVTOLLandingPos(Droid const* psDroid, Vector2i* p)
{
	// set blocking flags for all the other droids
	for (auto const& psCurr : playerList[psDroid->playerManager->getPlayer()].droids)
	{
		static Vector2i tileCoords{0, 0};
		if (psCurr.isStationary()) {
      tileCoords = map_coord(psCurr.getPosition().xy());
		}
		else {
      tileCoords = map_coord(psCurr.getMovementData()->destination);
		}
		if (&psCurr != psDroid && tileOnMap(tileCoords)) {
      mapTile(tileCoords)->tileInfoBits |= BITS_FPATHBLOCK;
		}
	}

	// search for landing tile; will stop when found or radius exceeded
	Vector2i xyCoords{0, 0};
	bool const foundTile = spiralSearch({map_coord(p->x), map_coord(p->y)},
                                      VTOL_LANDING_RADIUS, vtolLandingTileSearchFunction, &xyCoords);
	if (foundTile) {
		objTrace(psDroid->getId(), "Unit %d landing pos (%d,%d)", psDroid->getId(), xyCoords.x, xyCoords.y);
		p->x = world_coord(xyCoords.x) + TILE_UNITS / 2;
		p->y = world_coord(xyCoords.y) + TILE_UNITS / 2;
	}

	// clear blocking flags for all the other droids
	for (auto const& psCurr : playerList[psDroid->playerManager->getPlayer()].droids)
	{
		Vector2i t{0, 0};
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
