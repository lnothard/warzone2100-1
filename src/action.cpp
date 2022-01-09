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
#include "lib/sound/audio.h"
#include "lib/sound/audio_id.h"

#include "action.h"
#include "cmddroid.h"
#include "geometry.h"
#include "mapgrid.h"
#include "mission.h"
#include "move.h"
#include "projectile.h"
#include "qtscript.h"
#include "transporter.h"

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
typedef bool (*tileMatchFunction)(int x, int y, void* matchState);

std::string getDroidActionName(ACTION action)
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
//bool actionInRange(const DROID* psDroid, const SimpleObject* psObj, int weapon_slot, bool useLongWithOptimum)
//{
//	CHECK_DROID(psDroid);
//
//	if (psDroid->asWeaps[0].nStat == 0)
//	{
//		return false;
//	}
//
//	const unsigned compIndex = psDroid->asWeaps[weapon_slot].nStat;
//	ASSERT_OR_RETURN(false, compIndex < numWeaponStats, "Invalid range referenced for numWeaponStats, %d > %d",
//	                 compIndex, numWeaponStats);
//	const WEAPON_STATS* psStats = asWeaponStats + compIndex;
//
//	const int dx = (SDWORD)psDroid->pos.x - (SDWORD)psObj->pos.x;
//	const int dy = (SDWORD)psDroid->pos.y - (SDWORD)psObj->pos.y;
//
//	const int radSq = dx * dx + dy * dy;
//	const int longRange = proj_GetLongRange(psStats, psDroid->player);
//	const int shortRange = proj_GetShortRange(psStats, psDroid->player);
//
//	int rangeSq = 0;
//	switch (psDroid->secondaryOrder & DSS_ARANGE_MASK)
//	{
//	case DSS_ARANGE_OPTIMUM:
//		if (!useLongWithOptimum && weaponShortHit(psStats, psDroid->player) > weaponLongHit(psStats, psDroid->player))
//		{
//			rangeSq = shortRange * shortRange;
//		}
//		else
//		{
//			rangeSq = longRange * longRange;
//		}
//		break;
//	case DSS_ARANGE_SHORT:
//		rangeSq = shortRange * shortRange;
//		break;
//	case DSS_ARANGE_LONG:
//		rangeSq = longRange * longRange;
//		break;
//	default:
//		ASSERT(!"unknown attackrange order", "unknown attack range order");
//		rangeSq = longRange * longRange;
//		break;
//	}
//
//	/* check max range */
//	if (radSq <= rangeSq)
//	{
//		/* check min range */
//		const int minrange = proj_GetMinRange(psStats, psDroid->player);
//		if (radSq >= minrange * minrange || !proj_Direct(psStats))
//		{
//			return true;
//		}
//	}
//
//	return false;
//}

//// check if a target is inside minimum weapon range
//static bool actionInsideMinRange(DROID *psDroid, SimpleObject *psObj, WEAPON_STATS *psStats)
//{
//	CHECK_DROID(psDroid);
//	CHECK_OBJECT(psObj);
//
//	if (!psStats)
//	{
//		psStats = getWeaponStats(psDroid, 0);
//	}
//
//	/* if I am a multi-turret droid */
//	if (psDroid->asWeaps[0].nStat == 0)
//	{
//		return false;
//	}
//
//	const int dx = psDroid->pos.x - psObj->pos.x;
//	const int dy = psDroid->pos.y - psObj->pos.y;
//	const int radSq = dx * dx + dy * dy;
//	const int minRange = proj_GetMinRange(psStats, psDroid->player);
//	const int rangeSq = minRange * minRange;
//
//	// check min range
//	if (radSq <= rangeSq)
//	{
//		return true;
//	}
//
//	return false;
//}


//// Realign turret
//void actionAlignTurret(SimpleObject *psObj, int weapon_slot)
//{
//	uint16_t        nearest = 0;
//	uint16_t        tRot;
//	uint16_t        tPitch;
//
//	//get the maximum rotation this frame
//	const int rotation = gameTimeAdjustedIncrement(DEG(ACTION_TURRET_ROTATION_RATE));
//
//	switch (psObj->type)
//	{
//	case OBJ_DROID:
//		tRot = ((DROID *)psObj)->asWeaps[weapon_slot].rot.direction;
//		tPitch = ((DROID *)psObj)->asWeaps[weapon_slot].rot.pitch;
//		break;
//	case OBJ_STRUCTURE:
//		tRot = ((STRUCTURE *)psObj)->asWeaps[weapon_slot].rot.direction;
//		tPitch = ((STRUCTURE *)psObj)->asWeaps[weapon_slot].rot.pitch;
//
//		// now find the nearest 90 degree angle
//		nearest = (uint16_t)((tRot + DEG(45)) / DEG(90) * DEG(90));  // Cast wrapping intended.
//		break;
//	default:
//		ASSERT(!"invalid object type", "invalid object type");
//		return;
//	}
//
//	tRot += clip(angleDelta(nearest - tRot), -rotation, rotation);  // Addition wrapping intended.
//
//	// align the turret pitch
//	tPitch += clip(angleDelta(0 - tPitch), -rotation / 2, rotation / 2); // Addition wrapping intended.
//
//	switch (psObj->type)
//	{
//	case OBJ_DROID:
//		((DROID *)psObj)->asWeaps[weapon_slot].rot.direction = tRot;
//		((DROID *)psObj)->asWeaps[weapon_slot].rot.pitch = tPitch;
//		break;
//	case OBJ_STRUCTURE:
//		((STRUCTURE *)psObj)->asWeaps[weapon_slot].rot.direction = tRot;
//		((STRUCTURE *)psObj)->asWeaps[weapon_slot].rot.pitch = tPitch;
//		break;
//	default:
//		ASSERT(!"invalid object type", "invalid object type");
//		return;
//	}
//}

/* returns true if on target */
bool actionTargetTurret(SimpleObject* psAttacker, SimpleObject* psTarget, const Weapon* psWeapon)
{
  int rotRate = DEG(ACTION_TURRET_ROTATION_RATE) * 4;
  int pitchRate = DEG(ACTION_TURRET_ROTATION_RATE) * 2;

	auto& psWeapStats = psWeapon->get_stats();
	unsigned tRotation, tPitch;
	unsigned targetRotation;
	int rotationTolerance = 0;
	int pitchLowerLimit, pitchUpperLimit;

	if (!psTarget) {
		return false;
	}

  auto as_droid = dynamic_cast<Droid*>(psAttacker);
	bool bRepair = as_droid && as_droid->getType() == DROID_TYPE::REPAIRER;

	// extra heavy weapons on some structures need to rotate and pitch more slowly
	if (psWeapStats.weight > HEAVY_WEAPON_WEIGHT && !bRepair) {
		unsigned excess = DEG(100) * (psWeapStats.weight - HEAVY_WEAPON_WEIGHT) / psWeapStats.weight;
		rotRate = DEG(ACTION_TURRET_ROTATION_RATE) * 2 - excess;
		pitchRate = rotRate / 2;
	}

	tRotation = psWeapon->getRotation().direction;
	tPitch = psWeapon->getRotation().pitch;

	// set the pitch limits based on the weapon stats of the attacker
	pitchLowerLimit = pitchUpperLimit = 0;
	auto attackerMuzzlePos = psAttacker->getPosition();

	// using for calculating the pitch, but not the direction, in case
  // using the exact direction causes bugs somewhere.
	if (auto as_struct = dynamic_cast<Structure*>(psAttacker)) {
		int weapon_slot = psWeapon - as_struct->asWeaps; // Should probably be passed weapon_slot instead of psWeapon.
		calcStructureMuzzleLocation(as_struct, &attackerMuzzlePos, weapon_slot);
		pitchLowerLimit = DEG(psWeapStats.minElevation);
		pitchUpperLimit = DEG(psWeapStats.maxElevation);
	}
	else if (auto psDroid = dynamic_cast<Droid*>(psAttacker))
	{
		int weapon_slot = psWeapon - psDroid->asWeaps; // Should probably be passed weapon_slot instead of psWeapon.
		calcDroidMuzzleLocation(psDroid, &attackerMuzzlePos, weapon_slot);

		if (psDroid->getType() == DROID_TYPE::WEAPON ||
       isTransporter(*psDroid) ||
       psDroid->getType() == DROID_TYPE::COMMAND ||
       psDroid->getType() == DROID_TYPE::CYBORG ||
       psDroid->getType() == DROID_TYPE::CYBORG_SUPER) {
			pitchLowerLimit = DEG(psWeapStats->minElevation);
			pitchUpperLimit = DEG(psWeapStats->maxElevation);
		}
		else if (psDroid->getType() == DROID_TYPE::REPAIRER)
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
	targetRotation = calcDirection(psAttacker->getPosition().x, psAttacker->getPosition().y,
                                 psTarget->getPosition().x, psTarget->getPosition().y);

	//restrict rotationerror to =/- 180 degrees
	int rotationError = angleDelta(targetRotation - (tRotation + psAttacker->rot.direction));

	tRotation += clip(rotationError, -rotRate, rotRate); // Addition wrapping intentional.
	if (psAttacker->type == OBJ_DROID && isVtolDroid((Droid*)psAttacker))
	{
		// limit the rotation for vtols
		int32_t limit = VTOL_TURRET_LIMIT;
		if (psWeapStats.weaponSubClass == WEAPON_SUBCLASS::BOMB ||
        psWeapStats.weaponSubClass == WEAPON_SUBCLASS::EMP)
		{
			limit = 0; // Don't turn bombs.
			rotationTolerance = VTOL_TURRET_LIMIT_BOMB;
		}
		tRotation = (uint16_t)clip(angleDelta(tRotation), -limit, limit); // Cast wrapping intentional.
	}
	bool onTarget = abs(angleDelta(targetRotation - (tRotation + psAttacker->rot.direction))) <= rotationTolerance;

	/* Set muzzle pitch if not repairing or outside minimum range */
	const int minRange = proj_GetMinRange(psWeapStats, psAttacker->player);
	if (!bRepair && (unsigned)objectPositionSquareDiff(psAttacker, psTarget) > minRange * minRange) {
		/* get target distance */
		Vector3i delta = psTarget->getPosition() - attackerMuzzlePos;
		int32_t dxy = iHypot(delta.x, delta.y);

		uint16_t targetPitch = iAtan2(delta.z, dxy);
		targetPitch = (uint16_t)clip(angleDelta(targetPitch), pitchLowerLimit, pitchUpperLimit);
		// Cast wrapping intended.
		int pitchError = angleDelta(targetPitch - tPitch);

    // addition wrapping intended
		tPitch += clip(pitchError, -pitchRate, pitchRate);
		onTarget = onTarget && targetPitch == tPitch;
	}

	psWeapon->rotation.direction = tRotation;
	psWeapon->rotation.pitch = tPitch;

	return onTarget;
}

// return whether a droid can see a target to fire on it
bool actionVisibleTarget(Droid* psDroid, SimpleObject* psTarget, int weapon_slot)
{
	CHECK_DROID(psDroid);
	ASSERT_OR_RETURN(false, psTarget != nullptr, "Target is NULL");
	ASSERT_OR_RETURN(false, psDroid->getPlayer() < MAX_PLAYERS, "psDroid->player (%" PRIu8 ") must be < MAX_PLAYERS",
                   psDroid->getPlayer());
	if (!psTarget->visible[psDroid->player]) {
		return false;
	}
	if ((psDroid->numWeaps == 0 || isVtolDroid(psDroid)) && visibleObject(psDroid, psTarget, false))
	{
		return true;
	}
	return (orderState(psDroid, ORDER_TYPE::FIRE_SUPPORT) || visibleObject(psDroid, psTarget, false) > UBYTE_MAX / 2)
		&& lineOfFire(psDroid, psTarget, weapon_slot, true);
}

//static void actionAddVtolAttackRun(DROID* psDroid)
//{
//	SimpleObject* psTarget;
//
//	CHECK_DROID(psDroid);
//
//	if (psDroid->psActionTarget[0] != nullptr)
//	{
//		psTarget = psDroid->psActionTarget[0];
//	}
//	else if (psDroid->order.psObj != nullptr)
//	{
//		psTarget = psDroid->order.psObj;
//	}
//	else
//	{
//		return;
//	}
//
//	/* get normal vector from droid to target */
//	Vector2i delta = (psTarget->pos - psDroid->pos).xy();
//
//	/* get magnitude of normal vector (Pythagorean theorem) */
//	int dist = std::max(iHypot(delta), 1);
//
//	/* add waypoint behind target attack length away*/
//	Vector2i dest = psTarget->pos.xy() + delta * VTOL_ATTACK_LENGTH / dist;
//
//	if (!worldOnMap(dest))
//	{
//		debug(LOG_NEVER, "*** actionAddVtolAttackRun: run off map! ***");
//	}
//	else
//	{
//		moveDroidToDirect(psDroid, dest.x, dest.y);
//	}
//}

//static void actionUpdateVtolAttack(DROID* psDroid)
//{
//	CHECK_DROID(psDroid);
//
//	/* don't do attack runs whilst returning to base */
//	if (psDroid->order.type == ORDER_TYPE::RTB)
//	{
//		return;
//	}
//
//	/* order back to base after fixed number of attack runs */
//	if (psDroid->numWeaps > 0 && psDroid->asWeaps[0].nStat > 0 && vtolEmpty(psDroid))
//	{
//		moveToRearm(psDroid);
//		return;
//	}
//
//	/* circle around target if hovering and not cyborg */
//	if (psDroid->sMove.Status == MOVEHOVER && !cyborgDroid(psDroid))
//	{
//		actionAddVtolAttackRun(psDroid);
//	}
//}

//// calculate a position for units to pull back to if they
//// need to increase the range between them and a target
//static void actionCalcPullBackPoint(SimpleObject* psObj, SimpleObject* psTarget, int* px, int* py)
//{
//	// get the vector from the target to the object
//	int xdiff = psObj->pos.x - psTarget->pos.x;
//	int ydiff = psObj->pos.y - psTarget->pos.y;
//	const int len = iHypot(xdiff, ydiff);
//
//	if (len == 0)
//	{
//		xdiff = TILE_UNITS;
//		ydiff = TILE_UNITS;
//	}
//	else
//	{
//		xdiff = (xdiff * TILE_UNITS) / len;
//		ydiff = (ydiff * TILE_UNITS) / len;
//	}
//
//	// create the position
//	*px = psObj->pos.x + xdiff * PULL_BACK_DIST;
//	*py = psObj->pos.y + ydiff * PULL_BACK_DIST;
//
//	// make sure coordinates stay inside of the map
//	clip_world_offmap(px, py);
//}

//// check whether a droid is in the neighboring tile of another droid
//bool actionReachedDroid(DROID const* psDroid, DROID const* psOther)
//{
//	ASSERT_OR_RETURN(false, psDroid != nullptr && psOther != nullptr, "Bad droids");
//	CHECK_DROID(psDroid);
//	Vector2i xy = map_coord(psDroid->pos.xy());
//	Vector2i otherxy = map_coord(psOther->pos.xy());
//	Vector2i delta = xy - otherxy;
//	return delta.x >= -1 && delta.x <= 1 && delta.y >= -1 && delta.y <= 1;
//}


// check whether a droid is in the neighboring tile to a build position
bool actionReachedBuildPos(Droid const* psDroid, int x, int y, uint16_t dir, BaseStats const* psStats)
{
	ASSERT_OR_RETURN(false, psStats != nullptr && psDroid != nullptr, "Bad stat or droid");
	CHECK_DROID(psDroid);

	auto b = getStructureBounds(psStats, Vector2i(x, y), dir);

	// do all calculations in half tile units so that
	// the droid moves to within half a tile of the target
	// NOT ANY MORE - JOHN
	Vector2i delta = map_coord(psDroid->getPosition().xy()) - b.top_left_coords;
	return delta.x >= -1 && delta.x <= b.size_in_coords.x &&
         delta.y >= -1 && delta.y <= b.size_in_coords.y;
}


// check if a droid is on the foundations of a new building
static bool actionRemoveDroidsFromBuildPos(unsigned player, Vector2i pos, uint16_t dir, BaseStats* psStats)
{
	ASSERT_OR_RETURN(false, psStats != nullptr, "Bad stat");

	bool buildPosEmpty = true;

	StructureBounds b = getStructureBounds(psStats, pos, dir);

	Vector2i structureCentre = world_coord(b.map) + world_coord(b.size) / 2;
	unsigned structureMaxRadius = iHypot(world_coord(b.size) / 2) + 1; // +1 since iHypot rounds down.

	static GridList gridList; // static to avoid allocations.
	gridList = gridStartIterate(structureCentre.x, structureCentre.y, structureMaxRadius);
	for (auto& gi : gridList)
	{
		auto droid = dynamic_cast<Droid*>(gi);
		if (droid == nullptr)
		{
			continue; // Only looking for droids.
		}

		Vector2i delta = map_coord(droid->getPosition().xy()) - b.map;
		if (delta.x < 0 || delta.x >= b.size.x || delta.y < 0 || delta.y >= b.size.y || isFlying(droid))
		{
			continue; // Droid not under new structure (just near it).
		}

		buildPosEmpty = false; // Found a droid, have to move it away.

		if (!aiCheckAlliances(player, droid->getPlayer()))
		{
			continue; // Enemy droids probably don't feel like moving.
		}

		// TODO If the action code was less convoluted, it would be possible for the droid should drive away instead of just getting moved away.
		Vector2i bestDest(0, 0); // Dummy initialisation.
		unsigned bestDist = UINT32_MAX;
		for (int y = -1; y <= b.size.y; ++y)
			for (int x = -1; x <= b.size.x; x += y >= 0 && y < b.size.y ? b.size.x + 1 : 1)
			{
				Vector2i dest = world_coord(b.map + Vector2i(x, y)) + Vector2i(TILE_UNITS, TILE_UNITS) / 2;
				unsigned dist = iHypot(droid->pos.xy() - dest);
				if (dist < bestDist && !fpathBlockingTile(map_coord(dest.x), map_coord(dest.y),
				                                          getPropulsionStats(droid)->propulsionType))
				{
					bestDest = dest;
					bestDist = dist;
				}
			}
		if (bestDist != UINT32_MAX)
		{
			// Push the droid out of the way.
			Vector2i newPos = droid->pos.xy() + iSinCosR(iAtan2(bestDest - droid->pos.xy()),
			                                             gameTimeAdjustedIncrement(TILE_UNITS));
			droidSetPosition(droid, newPos.x, newPos.y);
		}
	}

	return buildPosEmpty;
}


// Update the action state for a droid
void actionUpdateDroid(Droid* psDroid)
{
	bool (*actionUpdateFunc)(Droid* psDroid) = nullptr;
	bool nonNullWeapon[MAX_WEAPONS] = {false};
	SimpleObject* psTargets[MAX_WEAPONS] = {nullptr};
	bool hasValidWeapon = false;
	bool hasVisibleTarget = false;
	bool targetVisibile[MAX_WEAPONS] = {false};
	bool bHasTarget = false;
	bool bDirect = false;
	Structure* blockingWall = nullptr;
	bool wallBlocked = false;

	CHECK_DROID(psDroid);

	auto& psPropStats = psDroid->getPropulsion();
	ASSERT_OR_RETURN(, psPropStats != nullptr, "Invalid propulsion stats pointer");

	bool secHoldActive = secondaryGetState(psDroid, DSO_HALTTYPE) == DSS_HALT_HOLD;

	actionSanity(psDroid);

	//if the droid has been attacked by an EMP weapon, it is temporarily disabled
	if (psDroid->lastHitWeapon == WSC_EMP)
	{
		if (gameTime - psDroid->timeLastHit > EMP_DISABLE_TIME)
		{
			//the actionStarted time needs to be adjusted
			psDroid->time_action_started += (gameTime - psDroid->timeLastHit);
			//reset the lastHit parameters
			psDroid->timeLastHit = 0;
			psDroid->lastHitWeapon = WSC_NUM_WEAPON_SUBCLASSES;
		}
		else
		{
			//get out without updating
			return;
		}
	}

	for (unsigned i = 0; i < psDroid->numWeaps; ++i)
	{
		if (psDroid->asWeaps[i].nStat > 0)
		{
			nonNullWeapon[i] = true;
		}
	}

	// HACK: Apparently we can't deal with a droid that only has NULL weapons ?
	// FIXME: Find out whether this is really necessary
	if (psDroid->numWeaps <= 1)
	{
		nonNullWeapon[0] = true;
	}

	Order* order = &psDroid->order;

	switch (psDroid->getAction())
	{
	case ACTION::NONE:
	case ACTION::WAIT_FOR_REPAIR:
		// doing nothing
		// see if there's anything to shoot.
		if (psDroid->numWeaps > 0 && !isVtolDroid(psDroid)
			&& (order->type == ORDER_TYPE::NONE || order->type == ORDER_TYPE::HOLD || order->type == ORDER_TYPE::RTR || order->type ==
				ORDER_TYPE::GUARD))
		{
			for (unsigned i = 0; i < psDroid->numWeaps; ++i)
			{
				if (nonNullWeapon[i])
				{
					SimpleObject* psTemp = nullptr;

					WeaponStats* const psWeapStats = &asWeaponStats[psDroid->asWeaps[i].nStat];
					if (psDroid->asWeaps[i].nStat > 0
						&& psWeapStats->rotate
						&& aiBestNearestTarget(psDroid, &psTemp, i) >= 0)
					{
						if (secondaryGetState(psDroid, DSO_ATTACK_LEVEL) == DSS_ALEV_ALWAYS)
						{
							psDroid->action = ACTION::ATTACK;
							setDroidActionTarget(psDroid, psTemp, i);
						}
					}
				}
			}
		}
		break;
	case ACTION::WAITDURINGREPAIR:
		// Check that repair facility still exists
		if (!order->psObj)
		{
			psDroid->action = ACTION::NONE;
			break;
		}
		if (order->type == ORDER_TYPE::RTR && order->rtrType == RTR_TYPE_REPAIR_FACILITY)
		{
			// move back to the repair facility if necessary
			if (DROID_STOPPED(psDroid) &&
				!actionReachedBuildPos(psDroid,
				                       order->psObj->pos.x, order->psObj->pos.y,
				                       ((Structure*)order->psObj)->rot.direction,
				                       ((Structure*)order->psObj)->pStructureType))
			{
				moveDroidToNoFormation(psDroid, order->psObj->pos.x, order->psObj->pos.y);
			}
		}
		else if (order->type == ORDER_TYPE::RTR && order->rtrType == RTR_TYPE_DROID && DROID_STOPPED(psDroid))
		{
			if (!actionReachedDroid(psDroid, static_cast<Droid*>(order->psObj)))
			{
				moveDroidToNoFormation(psDroid, order->psObj->pos.x, order->psObj->pos.y);
			}
			else
			{
				moveStopDroid(psDroid);
			}
		}
		break;
	case ACTION::TRANSPORTWAITTOFLYIN:
		//if we're moving droids to safety and currently waiting to fly back in, see if time is up
		if (psDroid->getPlayer() == selectedPlayer && getDroidsToSafetyFlag())
		{
			bool enoughTimeRemaining = (mission.time - (gameTime - mission.startTime)) >= (60 * GAME_TICKS_PER_SEC);
			if (((SDWORD)(mission.ETA - (gameTime - missionGetReinforcementTime())) <= 0) && enoughTimeRemaining)
			{
				UDWORD droidX, droidY;

				if (!droidRemove(psDroid, mission.apsDroidLists))
				{
					ASSERT_OR_RETURN(, false, "Unable to remove transporter from mission list");
				}
				addDroid(psDroid, apsDroidLists);
				//set the x/y up since they were set to INVALID_XY when moved offWorld
				missionGetTransporterExit(selectedPlayer, &droidX, &droidY);
				psDroid->pos.x = droidX;
				psDroid->pos.y = droidY;
				//fly Transporter back to get some more droids
				orderDroidLoc(psDroid, ORDER_TYPE::TRANSPORT_IN,
				              getLandingX(selectedPlayer), getLandingY(selectedPlayer), ModeImmediate);
			}
		}
		break;

	case ACTION::MOVE:
	case ACTION::RETURN_TO_POS:
	case ACTION::FIRE_SUPPORT_RETREAT:
		// moving to a location
		if (DROID_STOPPED(psDroid))
		{
			bool notify = psDroid->action == ACTION::MOVE;
			// Got to destination
			psDroid->action = ACTION::NONE;

			if (notify)
			{
				/* notify scripts we have reached the destination
				*  also triggers when patrolling and reached a waypoint
				*/

				triggerEventDroidIdle(psDroid);
			}
		}
		//added multiple weapon check
		else if (psDroid->numWeaps > 0)
		{
			for (unsigned i = 0; i < psDroid->numWeaps; ++i)
			{
				if (nonNullWeapon[i])
				{
					SimpleObject* psTemp = nullptr;

					//I moved psWeapStats flag update there
					WeaponStats* const psWeapStats = &asWeaponStats[psDroid->asWeaps[i].nStat];
					if (!isVtolDroid(psDroid)
						&& psDroid->asWeaps[i].nStat > 0
						&& psWeapStats->rotate
						&& psWeapStats->fireOnMove
						&& aiBestNearestTarget(psDroid, &psTemp, i) >= 0)
					{
						if (secondaryGetState(psDroid, SECONDARY_ORDER::ATTACK_LEVEL) == DSS_ALEV_ALWAYS)
						{
							psDroid->action = ACTION::MOVE_FIRE;
							setDroidActionTarget(psDroid, psTemp, i);
						}
					}
				}
			}
		}
		break;
	case ACTION::TRANSPORTIN:
	case ACTION::TRANSPORTOUT:
		actionUpdateTransporter(psDroid);
		break;
	case ACTION::MOVEFIRE:
		// check if vtol is armed
		if (vtolEmpty(psDroid))
		{
			moveToRearm(psDroid);
		}
	// If droid stopped, it can no longer be in ACTION::MOVEFIRE
		if (DROID_STOPPED(psDroid))
		{
			psDroid->action = ACTION::NONE;
			break;
		}
	// loop through weapons and look for target for each weapon
		bHasTarget = false;
		for (unsigned i = 0; i < psDroid->numWeaps; ++i)
		{
			bDirect = proj_Direct(asWeaponStats + psDroid->asWeaps[i].nStat);
			blockingWall = nullptr;
			// Does this weapon have a target?
			if (psDroid->action_target[i] != nullptr)
			{
				// Is target worth shooting yet?
				if (aiObjectIsProbablyDoomed(psDroid->action_target[i], bDirect))
				{
					setDroidActionTarget(psDroid, nullptr, i);
				}
				// Is target from our team now? (Electronic Warfare)
				else if (electronicDroid(psDroid) && psDroid->player == psDroid->action_target[i]->player)
				{
					setDroidActionTarget(psDroid, nullptr, i);
				}
				// Is target blocked by a wall?
				else if (bDirect && visGetBlockingWall(psDroid, psDroid->action_target[i]))
				{
					setDroidActionTarget(psDroid, nullptr, i);
				}
				// I have a target!
				else
				{
					bHasTarget = true;
				}
			}
			// This weapon doesn't have a target
			else
			{
				// Can we find a good target for the weapon?
				SimpleObject* psTemp;
				if (aiBestNearestTarget(psDroid, &psTemp, i) >= 0)
				// assuming aiBestNearestTarget checks for electronic warfare
				{
					bHasTarget = true;
					setDroidActionTarget(psDroid, psTemp, i); // this updates psDroid->psActionTarget[i] to != NULL
				}
			}
			// If we have a target for the weapon: is it visible?
			if (psDroid->action_target[i] != nullptr && visibleObject(psDroid, psDroid->action_target[i], false) >
				UBYTE_MAX / 2)
			{
				hasVisibleTarget = true; // droid have a visible target to shoot
				targetVisibile[i] = true; // it is at least visible for this weapon
			}
		}
	// if there is at least one target
		if (bHasTarget)
		{
			// loop through weapons
			for (unsigned i = 0; i < psDroid->numWeaps; ++i)
			{
				const unsigned compIndex = psDroid->asWeaps[i].nStat;
				const WeaponStats* psStats = asWeaponStats + compIndex;
				wallBlocked = false;

				// has weapon a target? is target valid?
				if (psDroid->action_target[i] != nullptr && validTarget(psDroid, psDroid->action_target[i], i))
				{
					// is target visible and weapon is not a Nullweapon?
					if (targetVisibile[i] && nonNullWeapon[i]) //to fix a AA-weapon attack ground unit exploit
					{
						SimpleObject* psActionTarget = nullptr;
						blockingWall = visGetBlockingWall(psDroid, psDroid->action_target[i]);

						if (proj_Direct(psStats) && blockingWall)
						{
							WEAPON_EFFECT weapEffect = psStats->weaponEffect;

							if (!aiCheckAlliances(psDroid->player, blockingWall->player)
								&& asStructStrengthModifier[weapEffect][blockingWall->pStructureType->strength] >=
								MIN_STRUCTURE_BLOCK_STRENGTH)
							{
								psActionTarget = blockingWall;
								setDroidActionTarget(psDroid, psActionTarget, i); // attack enemy wall
							}
							else
							{
								wallBlocked = true;
							}
						}
						else
						{
							psActionTarget = psDroid->action_target[i];
						}

						// is the turret aligned with the target?
						if (!wallBlocked && actionTargetTurret(psDroid, psActionTarget, &psDroid->asWeaps[i]))
						{
							// In range - fire !!!
							combFire(&psDroid->asWeaps[i], psDroid, psActionTarget, i);
						}
					}
				}
			}
			// Droid don't have a visible target and it is not in pursue mode
			if (!hasVisibleTarget && secondaryGetState(psDroid, DSO_ATTACK_LEVEL) != DSS_ALEV_ALWAYS)
			{
				// Target lost
				psDroid->action = ACTION::MOVE;
			}
		}
		// it don't have a target, change to ACTION::MOVE
		else
		{
			psDroid->action = ACTION::MOVE;
		}
	//check its a VTOL unit since adding Transporter's into multiPlayer
	/* check vtol attack runs */
		if (isVtolDroid(psDroid))
		{
			actionUpdateVtolAttack(psDroid);
		}
		break;
	case ACTION::ATTACK:
	case ACTION::ROTATETOATTACK:
		if (psDroid->action_target[0] == nullptr && psDroid->action_target[1] != nullptr)
		{
			break;
		}
		ASSERT_OR_RETURN(, psDroid->action_target[0] != nullptr, "target is NULL while attacking");

		if (psDroid->action == ACTION::ROTATETOATTACK)
		{
			if (psDroid->movement.status == MOVETURNTOTARGET)
			{
				moveTurnDroid(psDroid, psDroid->action_target[0]->pos.x, psDroid->action_target[0]->pos.y);
				break; // Still turning.
			}
			psDroid->action = ACTION::ATTACK;
		}

	//check the target hasn't become one the same player ID - Electronic Warfare
		if (electronicDroid(psDroid) && psDroid->player == psDroid->action_target[0]->player)
		{
			for (unsigned i = 0; i < psDroid->numWeaps; ++i)
			{
				setDroidActionTarget(psDroid, nullptr, i);
			}
			psDroid->action = ACTION::NONE;
			break;
		}

		bHasTarget = false;
		wallBlocked = false;
		for (unsigned i = 0; i < psDroid->numWeaps; ++i)
		{
			SimpleObject* psActionTarget;

			if (i > 0)
			{
				// If we're ordered to shoot something, and we can, shoot it
				if ((order->type == ORDER_TYPE::ATTACK || order->type == ORDER_TYPE::ATTACKTARGET) &&
            psDroid->action_target[i] != psDroid->action_target[0] &&
            validTarget(psDroid, psDroid->action_target[0], i) &&
            actionInRange(psDroid, psDroid->action_target[0], i))
				{
					setDroidActionTarget(psDroid, psDroid->action_target[0], i);
				}
				// If we still don't have a target, try to find one
				else
				{
					if (psDroid->action_target[i] == nullptr &&
              aiChooseTarget(psDroid, &psTargets[i], i, false, nullptr))
					// Can probably just use psTarget instead of psTargets[i], and delete the psTargets variable.
					{
						setDroidActionTarget(psDroid, psTargets[i], i);
					}
				}
			}

			if (psDroid->action_target[i])
			{
				psActionTarget = psDroid->action_target[i];
			}
			else
			{
				psActionTarget = psDroid->action_target[0];
			}

			if (nonNullWeapon[i]
				&& actionVisibleTarget(psDroid, psActionTarget, i)
				&& actionInRange(psDroid, psActionTarget, i))
			{
				WeaponStats* const psWeapStats = &asWeaponStats[psDroid->asWeaps[i].nStat];
				WEAPON_EFFECT weapEffect = psWeapStats->weaponEffect;
				blockingWall = visGetBlockingWall(psDroid, psActionTarget);

				// if a wall is inbetween us and the target, try firing at the wall if our
				// weapon is good enough
				if (proj_Direct(psWeapStats) && blockingWall)
				{
					if (!aiCheckAlliances(psDroid->player, blockingWall->player)
						&& asStructStrengthModifier[weapEffect][blockingWall->pStructureType->strength] >=
						MIN_STRUCTURE_BLOCK_STRENGTH)
					{
						psActionTarget = (SimpleObject*)blockingWall;
						setDroidActionTarget(psDroid, psActionTarget, i);
					}
					else
					{
						wallBlocked = true;
					}
				}

				if (!bHasTarget)
				{
					bHasTarget = actionInRange(psDroid, psActionTarget, i, false);
				}

				if (validTarget(psDroid, psActionTarget, i) && !wallBlocked)
				{
					int dirDiff = 0;

					if (!psWeapStats->rotate)
					{
						// no rotating turret - need to check aligned with target
						const uint16_t targetDir = calcDirection(psDroid->pos.x, psDroid->pos.y, psActionTarget->pos.x,
						                                         psActionTarget->pos.y);
						dirDiff = abs(angleDelta(targetDir - psDroid->rot.direction));
					}

					if (dirDiff > FIXED_TURRET_DIR)
					{
						if (i > 0)
						{
							if (psDroid->action_target[i] != psDroid->action_target[0])
							{
								// Nope, can't shoot this, try something else next time
								setDroidActionTarget(psDroid, nullptr, i);
							}
						}
						else if (psDroid->movement.status != MOVESHUFFLE)
						{
							psDroid->action = ACTION::ROTATETOATTACK;
							moveTurnDroid(psDroid, psActionTarget->pos.x, psActionTarget->pos.y);
						}
					}
					else if (!psWeapStats->rotate ||
						actionTargetTurret(psDroid, psActionTarget, &psDroid->asWeaps[i]))
					{
						/* In range - fire !!! */
						combFire(&psDroid->asWeaps[i], psDroid, psActionTarget, i);
					}
				}
				else if (i > 0)
				{
					// Nope, can't shoot this, try something else next time
					setDroidActionTarget(psDroid, nullptr, i);
				}
			}
			else if (i > 0)
			{
				// Nope, can't shoot this, try something else next time
				setDroidActionTarget(psDroid, nullptr, i);
			}
		}

		if (!bHasTarget || wallBlocked)
		{
			SimpleObject* psTarget;
			bool supportsSensorTower = !isVtolDroid(psDroid) && (psTarget = orderStateObj(psDroid, ORDER_TYPE::FIRESUPPORT))
				&& psTarget->type == OBJ_STRUCTURE;

			if (secHoldActive && (order->type == ORDER_TYPE::ATTACKTARGET || order->type == ORDER_TYPE::FIRESUPPORT))
			{
				psDroid->action = ACTION::NONE; // secondary holding, cancel the order.
			}
			else if (secondaryGetState(psDroid, DSO_HALTTYPE) == DSS_HALT_PURSUE &&
				!supportsSensorTower &&
				!(order->type == ORDER_TYPE::HOLD ||
					order->type == ORDER_TYPE::RTR))
			{
				//We need this so pursuing doesn't stop if a unit is ordered to move somewhere while
				//it is still in weapon range of the target when reaching the end destination.
				//Weird case, I know, but keeps the previous pursue order intact.
				psDroid->action = ACTION::MOVETOATTACK; // out of range - chase it
			}
			else if (supportsSensorTower ||
				order->type == ORDER_TYPE::NONE ||
				order->type == ORDER_TYPE::HOLD ||
				order->type == ORDER_TYPE::RTR)
			{
				// don't move if on hold or firesupport for a sensor tower
				// also don't move if we're holding position or waiting for repair
				psDroid->action = ACTION::NONE; // holding, cancel the order.
			}
			//Units attached to commanders are always guarding the commander
			else if (secHoldActive && order->type == ORDER_TYPE::GUARD && hasCommander(psDroid))
			{
				Droid* commander = psDroid->group->psCommander;

				if (commander->order.type == ORDER_TYPE::ATTACKTARGET ||
					commander->order.type == ORDER_TYPE::FIRESUPPORT ||
					commander->order.type == ORDER_TYPE::ATTACK)
				{
					psDroid->action = ACTION::MOVETOATTACK;
				}
				else
				{
					psDroid->action = ACTION::NONE;
				}
			}
			else if (secondaryGetState(psDroid, DSO_HALTTYPE) != DSS_HALT_HOLD)
			{
				psDroid->action = ACTION::MOVETOATTACK; // out of range - chase it
			}
			else
			{
				psDroid->order.psObj = nullptr;
				psDroid->action = ACTION::NONE;
			}
		}

		break;

	case ACTION::VTOLATTACK:
		{
			WeaponStats* psWeapStats = nullptr;
			const bool targetIsValid = validTarget(psDroid, psDroid->action_target[0], 0);
			//uses vtResult
			if (psDroid->action_target[0] != nullptr &&
          targetIsValid)
			{
				//check if vtol that its armed
				if ((vtolEmpty(psDroid)) ||
            (psDroid->action_target[0] == nullptr) ||
					//check the target hasn't become one the same player ID - Electronic Warfare
					(electronicDroid(psDroid) && (psDroid->player == psDroid->action_target[0]->player)) ||
					// Huh? !targetIsValid can't be true, we just checked for it
					!targetIsValid)
				{
					moveToRearm(psDroid);
					break;
				}

				for (unsigned i = 0; i < psDroid->numWeaps; ++i)
				{
					if (nonNullWeapon[i]
						&& validTarget(psDroid, psDroid->action_target[0], i))
					{
						//I moved psWeapStats flag update there
						psWeapStats = &asWeaponStats[psDroid->asWeaps[i].nStat];
						if (actionVisibleTarget(psDroid, psDroid->action_target[0], i))
						{
							if (actionInRange(psDroid, psDroid->action_target[0], i))
							{
								if (psDroid->player == selectedPlayer)
								{
									audio_QueueTrackMinDelay(ID_SOUND_COMMENCING_ATTACK_RUN2,
									                         VTOL_ATTACK_AUDIO_DELAY);
								}

								if (actionTargetTurret(psDroid, psDroid->action_target[0], &psDroid->asWeaps[i]))
								{
									// In range - fire !!!
									combFire(&psDroid->asWeaps[i], psDroid,
                           psDroid->action_target[0], i);
								}
							}
							else
							{
								actionTargetTurret(psDroid, psDroid->action_target[0], &psDroid->asWeaps[i]);
							}
						}
					}
				}
			}

			/* circle around target if hovering and not cyborg */
			Vector2i attackRunDelta = psDroid->pos.xy() - psDroid->movement.destination;
			if (DROID_STOPPED(psDroid) || dot(attackRunDelta, attackRunDelta) < TILE_UNITS * TILE_UNITS)
			{
				actionAddVtolAttackRun(psDroid);
			}
			else if (psDroid->action_target[0] != nullptr &&
               targetIsValid)
			{
				// if the vtol is close to the target, go around again
				Vector2i diff = (psDroid->pos - psDroid->action_target[0]->pos).xy();
				const unsigned rangeSq = dot(diff, diff);
				if (rangeSq < VTOL_ATTACK_TARGET_DIST * VTOL_ATTACK_TARGET_DIST)
				{
					// don't do another attack run if already moving away from the target
					diff = psDroid->movement.destination - psDroid->action_target[0]->pos.xy();
					if (dot(diff, diff) < VTOL_ATTACK_TARGET_DIST * VTOL_ATTACK_TARGET_DIST)
					{
						actionAddVtolAttackRun(psDroid);
					}
				}
				// in case psWeapStats is still NULL
				else if (psWeapStats)
				{
					// if the vtol is far enough away head for the target again
					const int maxRange = proj_GetLongRange(psWeapStats, psDroid->player);
					if (rangeSq > maxRange * maxRange)
					{
						// don't do another attack run if already heading for the target
						diff = psDroid->movement.destination - psDroid->action_target[0]->pos.xy();
						if (dot(diff, diff) > VTOL_ATTACK_TARGET_DIST * VTOL_ATTACK_TARGET_DIST)
						{
							moveDroidToDirect(psDroid, psDroid->action_target[0]->pos.x,
							                  psDroid->action_target[0]->pos.y);
						}
					}
				}
			}
			break;
		}
	case ACTION::MOVETOATTACK:
		// send vtols back to rearm
		if (isVtolDroid(psDroid) && vtolEmpty(psDroid))
		{
			moveToRearm(psDroid);
			break;
		}

		ASSERT_OR_RETURN(, psDroid->action_target[0] != nullptr, "action update move to attack target is NULL");
		for (unsigned i = 0; i < psDroid->numWeaps; ++i)
		{
			hasValidWeapon |= validTarget(psDroid, psDroid->action_target[0], i);
		}
	//check the target hasn't become one the same player ID - Electronic Warfare, and that the target is still valid.
		if ((electronicDroid(psDroid) && psDroid->player == psDroid->action_target[0]->player) || !hasValidWeapon)
		{
			for (unsigned i = 0; i < psDroid->numWeaps; ++i)
			{
				setDroidActionTarget(psDroid, nullptr, i);
			}
			psDroid->action = ACTION::NONE;
		}
		else
		{
			if (actionVisibleTarget(psDroid, psDroid->action_target[0], 0))
			{
				for (unsigned i = 0; i < psDroid->numWeaps; ++i)
				{
					if (nonNullWeapon[i]
						&& validTarget(psDroid, psDroid->action_target[0], i)
						&& actionVisibleTarget(psDroid, psDroid->action_target[0], i))
					{
						bool chaseBloke = false;
						WeaponStats* const psWeapStats = &asWeaponStats[psDroid->asWeaps[i].nStat];

						if (psWeapStats->rotate)
						{
							actionTargetTurret(psDroid, psDroid->action_target[0], &psDroid->asWeaps[i]);
						}

						if (!isVtolDroid(psDroid) &&
                psDroid->action_target[0]->type == OBJ_DROID &&
                ((Droid*)psDroid->action_target[0])->type == DROID_PERSON &&
                psWeapStats->fireOnMove)
						{
							chaseBloke = true;
						}

						if (actionInRange(psDroid, psDroid->action_target[0], i) && !chaseBloke)
						{
							/* init vtol attack runs count if necessary */
							if (psPropStats->propulsionType == PROPULSION_TYPE_LIFT)
							{
								psDroid->action = ACTION::VTOLATTACK;
							}
							else
							{
								if (actionInRange(psDroid, psDroid->action_target[0], i, false))
								{
									moveStopDroid(psDroid);
								}

								if (psWeapStats->rotate)
								{
									psDroid->action = ACTION::ATTACK;
								}
								else
								{
									psDroid->action = ACTION::ROTATETOATTACK;
									moveTurnDroid(psDroid, psDroid->action_target[0]->pos.x,
									              psDroid->action_target[0]->pos.y);
								}
							}
						}
						else if (actionInRange(psDroid, psDroid->action_target[0], i))
						{
							// fire while closing range
							if ((blockingWall = visGetBlockingWall(psDroid, psDroid->action_target[0])) && proj_Direct(
								psWeapStats))
							{
								WEAPON_EFFECT weapEffect = psWeapStats->weaponEffect;

								if (!aiCheckAlliances(psDroid->player, blockingWall->player)
									&& asStructStrengthModifier[weapEffect][blockingWall->pStructureType->strength] >=
									MIN_STRUCTURE_BLOCK_STRENGTH)
								{
									//Shoot at wall if the weapon is good enough against them
									combFire(&psDroid->asWeaps[i], psDroid, (SimpleObject*)blockingWall, i);
								}
							}
							else
							{
								combFire(&psDroid->asWeaps[i], psDroid, psDroid->action_target[0], i);
							}
						}
					}
				}
			}
			else
			{
				for (unsigned i = 0; i < psDroid->numWeaps; ++i)
				{
					if ((psDroid->asWeaps[i].rotation.direction != 0) ||
							(psDroid->asWeaps[i].rotation.pitch != 0))
					{
						actionAlignTurret(psDroid, i);
					}
				}
			}

			if (DROID_STOPPED(psDroid) && psDroid->action != ACTION::ATTACK)
			{
				/* Stopped moving but haven't reached the target - possibly move again */

				//'hack' to make the droid to check the primary turrent instead of all
				WeaponStats* const psWeapStats = &asWeaponStats[psDroid->asWeaps[0].nStat];

				if (order->type == ORDER_TYPE::ATTACKTARGET && secHoldActive)
				{
					psDroid->action = ACTION::NONE; // on hold, give up.
				}
				else if (actionInsideMinRange(psDroid, psDroid->action_target[0], psWeapStats))
				{
					if (proj_Direct(psWeapStats) && order->type != ORDER_TYPE::HOLD)
					{
						SDWORD pbx, pby;

						// try and extend the range
						actionCalcPullBackPoint(psDroid, psDroid->action_target[0], &pbx, &pby);
						moveDroidTo(psDroid, (UDWORD)pbx, (UDWORD)pby);
					}
					else
					{
						if (psWeapStats->rotate)
						{
							psDroid->action = ACTION::ATTACK;
						}
						else
						{
							psDroid->action = ACTION::ROTATETOATTACK;
							moveTurnDroid(psDroid, psDroid->action_target[0]->pos.x,
							              psDroid->action_target[0]->pos.y);
						}
					}
				}
				else if (order->type != ORDER_TYPE::HOLD) // approach closer?
				{
					// try to close the range
					moveDroidTo(psDroid, psDroid->action_target[0]->pos.x, psDroid->action_target[0]->pos.y);
				}
			}
		}
		break;

	case ACTION::SULK:
		// unable to route to target ... don't do anything aggressive until time is up
		// we need to do something defensive at this point ???

		//hmmm, hope this doesn't cause any problems!
		if (gameTime > psDroid->time_action_started)
		{
			psDroid->action = ACTION::NONE;
			// Sulking is over lets get back to the action ... is this all I need to do to get it back into the action?
		}
		break;

	case ACTION::MOVETOBUILD:
		if (!order->psStats)
		{
			psDroid->action = ACTION::NONE;
			break;
		}
		else
		{
			// Determine if the droid can still build or help to build the ordered structure at the specified location
			const StructureStats* const desiredStructure = order->psStats;
			const Structure* const structureAtBuildPosition = getTileStructure(
				map_coord(psDroid->actionPos.x), map_coord(psDroid->actionPos.y));

			if (nullptr != structureAtBuildPosition)
			{
				bool droidCannotBuild = false;

				if (!aiCheckAlliances(structureAtBuildPosition->player, psDroid->player))
				{
					// Not our structure
					droidCannotBuild = true;
				}
				else
				// There's an allied structure already there.  Is it a wall, and can the droid upgrade it to a defence or gate?
					if (isWall(structureAtBuildPosition->pStructureType->type) &&
						(desiredStructure->type == REF_DEFENSE || desiredStructure->type == REF_GATE))
					{
						// It's always valid to upgrade a wall to a defence or gate
						droidCannotBuild = false; // Just to avoid an empty branch
					}
					else if ((structureAtBuildPosition->pStructureType != desiredStructure) &&
						// ... it's not the exact same type as the droid was ordered to build
						(structureAtBuildPosition->pStructureType->type == REF_WALLCORNER && desiredStructure->type !=
							REF_WALL)) // and not a wall corner when the droid wants to build a wall
					{
						// And so the droid can't build or help with building this structure
						droidCannotBuild = true;
					}
					else
					// So it's a structure that the droid could help to build, but is it already complete?
						if (structureAtBuildPosition->status == SS_BUILT &&
							(!IsStatExpansionModule(desiredStructure) || !canStructureHaveAModuleAdded(
								structureAtBuildPosition)))
						{
							// The building is complete and the droid hasn't been told to add a module, or can't add one, so can't help with that.
							droidCannotBuild = true;
						}

				if (droidCannotBuild)
				{
					if (order->type == ORDER_TYPE::LINEBUILD && map_coord(psDroid->order.pos) != map_coord(
						psDroid->order.pos2))
					{
						// The droid is doing a line build, and there's more to build. This will force the droid to move to the next structure in the line build
						objTrace(psDroid->id,
						         "ACTION::MOVETOBUILD: line target is already built, or can't be built - moving to next structure in line")
						;
						psDroid->action = ACTION::NONE;
					}
					else
					{
						// Cancel the current build order. This will move the truck onto the next order, if it has one, or halt in place.
						objTrace(psDroid->id,
						         "ACTION::MOVETOBUILD: target is already built, or can't be built - executing next order or halting")
						;
						cancelBuild(psDroid);
					}

					break;
				}
			}
		} // End of check for whether the droid can still succesfully build the ordered structure

	// The droid can still build or help with a build, and is moving to a location to do so - are we there yet, are we there yet, are we there yet?
		if (actionReachedBuildPos(psDroid, psDroid->actionPos.x, psDroid->actionPos.y, order->direction,
		                          order->psStats))
		{
			// We're there, go ahead and build or help to build the structure
			bool buildPosEmpty = actionRemoveDroidsFromBuildPos(psDroid->player, psDroid->actionPos, order->direction,
			                                                    order->psStats);
			if (!buildPosEmpty)
			{
				break;
			}

			bool helpBuild = false;
			// Got to destination - start building
			StructureStats* const psStructStats = order->psStats;
			uint16_t dir = order->direction;
			moveStopDroid(psDroid);
			objTrace(psDroid->id, "Halted in our tracks - at construction site");
			if (order->type == ORDER_TYPE::BUILD && order->psObj == nullptr)
			{
				// Starting a new structure
				const Vector2i pos = psDroid->actionPos;

				//need to check if something has already started building here?
				//unless its a module!
				if (IsStatExpansionModule(psStructStats))
				{
					syncDebug("Reached build target: module");
					debug(LOG_NEVER, "ACTION::MOVETOBUILD: setUpBuildModule");
					setUpBuildModule(psDroid);
				}
				else if (TileHasStructure(worldTile(pos)))
				{
					// structure on the build location - see if it is the same type
					Structure* const psStruct = getTileStructure(map_coord(pos.x), map_coord(pos.y));
					if (psStruct->pStructureType == order->psStats ||
						(order->psStats->type == REF_WALL && psStruct->pStructureType->type == REF_WALLCORNER))
					{
						// same type - do a help build
						syncDebug("Reached build target: do-help");
						setDroidTarget(psDroid, psStruct);
						helpBuild = true;
					}
					else if ((psStruct->pStructureType->type == REF_WALL ||
							psStruct->pStructureType->type == REF_WALLCORNER) &&
						(order->psStats->type == REF_DEFENSE ||
							order->psStats->type == REF_GATE))
					{
						// building a gun tower or gate over a wall - OK
						if (droidStartBuild(psDroid))
						{
							syncDebug("Reached build target: tower");
							psDroid->action = ACTION::BUILD;
						}
					}
					else
					{
						syncDebug("Reached build target: already-structure");
						objTrace(psDroid->id, "ACTION::MOVETOBUILD: tile has structure already");
						cancelBuild(psDroid);
					}
				}
				else if (!validLocation(order->psStats, pos, dir, psDroid->player, false))
				{
					syncDebug("Reached build target: invalid");
					objTrace(psDroid->id, "ACTION::MOVETOBUILD: !validLocation");
					cancelBuild(psDroid);
				}
				else if (droidStartBuild(psDroid) == DroidStartBuildSuccess)
				// If DroidStartBuildPending, then there's a burning oil well, and we don't want to change to ACTION::BUILD until it stops burning.
				{
					syncDebug("Reached build target: build");
					psDroid->action = ACTION::BUILD;
					psDroid->time_action_started = gameTime;
					psDroid->action_points_done = 0;
				}
			}
			else if (order->type == ORDER_TYPE::LINEBUILD || order->type == ORDER_TYPE::BUILD)
			{
				// building a wall.
				Tile* const psTile = worldTile(psDroid->actionPos);
				syncDebug("Reached build target: wall");
				if (order->psObj == nullptr
					&& (TileHasStructure(psTile)
						|| TileHasFeature(psTile)))
				{
					if (TileHasStructure(psTile))
					{
						// structure on the build location - see if it is the same type
						Structure* const psStruct = getTileStructure(map_coord(psDroid->actionPos.x),
                                                         map_coord(psDroid->actionPos.y));
						ASSERT(psStruct, "TileHasStructure, but getTileStructure returned nullptr");
#if defined(WZ_CC_GNU) && !defined(WZ_CC_INTEL) && !defined(WZ_CC_CLANG) && (7 <= __GNUC__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wnull-dereference"
#endif
						if (psStruct->pStructureType == order->psStats)
						{
							// same type - do a help build
							setDroidTarget(psDroid, psStruct);
							helpBuild = true;
						}
						else if ((psStruct->pStructureType->type == REF_WALL || psStruct->pStructureType->type ==
								REF_WALLCORNER) &&
							(order->psStats->type == REF_DEFENSE || order->psStats->type == REF_GATE))
						{
							// building a gun tower over a wall - OK
							if (droidStartBuild(psDroid))
							{
								objTrace(psDroid->id, "ACTION::MOVETOBUILD: start building defense");
								psDroid->action = ACTION::BUILD;
							}
						}
						else if ((psStruct->pStructureType->type == REF_FACTORY && order->psStats->type ==
								REF_FACTORY_MODULE) ||
							(psStruct->pStructureType->type == REF_RESEARCH && order->psStats->type ==
								REF_RESEARCH_MODULE) ||
							(psStruct->pStructureType->type == REF_POWER_GEN && order->psStats->type ==
								REF_POWER_MODULE) ||
							(psStruct->pStructureType->type == REF_VTOL_FACTORY && order->psStats->type ==
								REF_FACTORY_MODULE))
						{
							// upgrade current structure in a row
							if (droidStartBuild(psDroid))
							{
								objTrace(psDroid->id, "ACTION::MOVETOBUILD: start building module");
								psDroid->action = ACTION::BUILD;
							}
						}
						else
						{
							objTrace(psDroid->id, "ACTION::MOVETOBUILD: line build hit building");
							cancelBuild(psDroid);
						}
#if defined(WZ_CC_GNU) && !defined(WZ_CC_INTEL) && !defined(WZ_CC_CLANG) && (7 <= __GNUC__)
# pragma GCC diagnostic pop
#endif
					}
					else if (TileHasFeature(psTile))
					{
						Feature* feature = getTileFeature(map_coord(psDroid->actionPos.x),
                                              map_coord(psDroid->actionPos.y));
						objTrace(psDroid->id, "ACTION::MOVETOBUILD: tile has feature %d", feature->psStats->subType);
						if (feature->psStats->subType == FEAT_OIL_RESOURCE && order->psStats->type ==
							REF_RESOURCE_EXTRACTOR)
						{
							if (droidStartBuild(psDroid))
							{
								objTrace(psDroid->id, "ACTION::MOVETOBUILD: start building oil derrick");
								psDroid->action = ACTION::BUILD;
							}
						}
					}
					else
					{
						objTrace(psDroid->id, "ACTION::MOVETOBUILD: blocked line build");
						cancelBuild(psDroid);
					}
				}
				else if (droidStartBuild(psDroid))
				{
					psDroid->action = ACTION::BUILD;
				}
			}
			else
			{
				syncDebug("Reached build target: planned-help");
				objTrace(psDroid->id, "ACTION::MOVETOBUILD: planned-help");
				helpBuild = true;
			}

			if (helpBuild)
			{
				// continuing a partially built structure (order = helpBuild)
				if (droidStartBuild(psDroid))
				{
					objTrace(psDroid->id, "ACTION::MOVETOBUILD: starting help build");
					psDroid->action = ACTION::BUILD;
				}
			}
		}
		else if (DROID_STOPPED(psDroid))
		{
			objTrace(psDroid->id,
			         "ACTION::MOVETOBUILD: Starting to drive toward construction site - move status was %d",
			         (int)psDroid->movement.status);
			moveDroidToNoFormation(psDroid, psDroid->actionPos.x, psDroid->actionPos.y);
		}
		break;
	case ACTION::BUILD:
		if (!order->psStats)
		{
			objTrace(psDroid->id, "No target stats for build order - resetting");
			psDroid->action = ACTION::NONE;
			break;
		}
		if (DROID_STOPPED(psDroid) &&
			!actionReachedBuildPos(psDroid, psDroid->actionPos.x, psDroid->actionPos.y, order->direction,
			                       order->psStats))
		{
			objTrace(psDroid->id, "ACTION::BUILD: Starting to drive toward construction site");
			moveDroidToNoFormation(psDroid, psDroid->actionPos.x, psDroid->actionPos.y);
		}
		else if (!DROID_STOPPED(psDroid) &&
             psDroid->movement.status != MOVETURNTOTARGET &&
             psDroid->movement.status != MOVESHUFFLE &&
             actionReachedBuildPos(psDroid, psDroid->actionPos.x, psDroid->actionPos.y, order->direction,
			                      order->psStats))
		{
			objTrace(psDroid->id, "ACTION::BUILD: Stopped - at construction site");
			moveStopDroid(psDroid);
		}
		if (psDroid->action == ACTION::SULK)
		{
			objTrace(psDroid->id, "Failed to go to objective, aborting build action");
			psDroid->action = ACTION::NONE;
			break;
		}
		if (droidUpdateBuild(psDroid))
		{
			actionTargetTurret(psDroid, psDroid->action_target[0], &psDroid->asWeaps[0]);
		}
		break;
	case ACTION::MOVETODEMOLISH:
	case ACTION::MOVETOREPAIR:
	case ACTION::MOVETORESTORE:
		if (!order->psStats)
		{
			psDroid->action = ACTION::NONE;
			break;
		}
		else
		{
			const Structure* structureAtPos = getTileStructure(map_coord(psDroid->actionPos.x),
                                                         map_coord(psDroid->actionPos.y));

			if (structureAtPos == nullptr)
			{
				//No structure located at desired position. Move on.
				psDroid->action = ACTION::NONE;
				break;
			}
			else if (order->type != ORDER_TYPE::RESTORE)
			{
				bool cantDoRepairLikeAction = false;

				if (!aiCheckAlliances(structureAtPos->player, psDroid->player))
				{
					cantDoRepairLikeAction = true;
				}
				else if (order->type != ORDER_TYPE::DEMOLISH && structureAtPos->body == structureBody(structureAtPos))
				{
					cantDoRepairLikeAction = true;
				}
				else if (order->type == ORDER_TYPE::DEMOLISH && structureAtPos->player != psDroid->player)
				{
					cantDoRepairLikeAction = true;
				}

				if (cantDoRepairLikeAction)
				{
					psDroid->action = ACTION::NONE;
					moveStopDroid(psDroid);
					break;
				}
			}
		}
	// see if the droid is at the edge of what it is moving to
		if (actionReachedBuildPos(psDroid, psDroid->actionPos.x, psDroid->actionPos.y,
                              ((Structure*)psDroid->action_target[0])->rot.direction, order->psStats))
		{
			moveStopDroid(psDroid);

			// got to the edge - start doing whatever it was meant to do
			droidStartAction(psDroid);
			switch (psDroid->action)
			{
			case ACTION::MOVETODEMOLISH:
				psDroid->action = ACTION::DEMOLISH;
				break;
			case ACTION::MOVETOREPAIR:
				psDroid->action = ACTION::REPAIR;
				break;
			case ACTION::MOVETORESTORE:
				psDroid->action = ACTION::RESTORE;
				break;
			default:
				break;
			}
		}
		else if (DROID_STOPPED(psDroid))
		{
			moveDroidToNoFormation(psDroid, psDroid->actionPos.x, psDroid->actionPos.y);
		}
		break;

	case ACTION::DEMOLISH:
	case ACTION::REPAIR:
	case ACTION::RESTORE:
		if (!order->psStats)
		{
			psDroid->action = ACTION::NONE;
			break;
		}
	// set up for the specific action
		switch (psDroid->action)
		{
		case ACTION::DEMOLISH:
			// ACTION::MOVETODEMOLISH;
			actionUpdateFunc = droidUpdateDemolishing;
			break;
		case ACTION::REPAIR:
			// ACTION::MOVETOREPAIR;
			actionUpdateFunc = droidUpdateRepair;
			break;
		case ACTION::RESTORE:
			// ACTION::MOVETORESTORE;
			actionUpdateFunc = droidUpdateRestore;
			break;
		default:
			break;
		}

	// now do the action update
		if (DROID_STOPPED(psDroid) && !actionReachedBuildPos(psDroid, psDroid->actionPos.x, psDroid->actionPos.y,
		                                                     ((Structure*)psDroid->action_target[0])->rot.direction,
		                                                     order->psStats))
		{
			if (order->type != ORDER_TYPE::HOLD && (!secHoldActive || (secHoldActive && order->type != ORDER_TYPE::NONE)))
			{
				objTrace(psDroid->id, "Secondary order: Go to construction site");
				moveDroidToNoFormation(psDroid, psDroid->actionPos.x, psDroid->actionPos.y);
			}
			else
			{
				psDroid->action = ACTION::NONE;
			}
		}
		else if (!DROID_STOPPED(psDroid) &&
             psDroid->movement.status != MOVETURNTOTARGET &&
             psDroid->movement.status != MOVESHUFFLE &&
             actionReachedBuildPos(psDroid, psDroid->actionPos.x, psDroid->actionPos.y,
                                   ((Structure*)psDroid->action_target[0])->rot.direction, order->psStats))
		{
			objTrace(psDroid->id, "Stopped - reached build position");
			moveStopDroid(psDroid);
		}
		else if (actionUpdateFunc(psDroid))
		{
			//use 0 for non-combat(only 1 'weapon')
			actionTargetTurret(psDroid, psDroid->action_target[0], &psDroid->asWeaps[0]);
		}
		else
		{
			psDroid->action = ACTION::NONE;
		}
		break;

	case ACTION::MOVETOREARMPOINT:
		if (DROID_STOPPED(psDroid))
		{
			objTrace(psDroid->id, "Finished moving onto the rearm pad");
			psDroid->action = ACTION::WAITDURINGREARM;
		}
		break;
	case ACTION::MOVETOREPAIRPOINT:
		if (psDroid->order.rtrType == RTR_TYPE_REPAIR_FACILITY)
		{
			/* moving from front to rear of repair facility or rearm pad */
			if (actionReachedBuildPos(psDroid, psDroid->action_target[0]->pos.x, psDroid->action_target[0]->pos.y,
                                ((Structure*)psDroid->action_target[0])->rot.direction,
                                ((Structure*)psDroid->action_target[0])->pStructureType))
			{
				objTrace(psDroid->id, "Arrived at repair point - waiting for our turn");
				moveStopDroid(psDroid);
				psDroid->action = ACTION::WAITDURINGREPAIR;
			}
			else if (DROID_STOPPED(psDroid))
			{
				moveDroidToNoFormation(psDroid, psDroid->action_target[0]->pos.x,
				                       psDroid->action_target[0]->pos.y);
			}
		}
		else if (psDroid->order.rtrType == RTR_TYPE_DROID)
		{
			bool reached = actionReachedDroid(psDroid, (Droid*)psDroid->order.psObj);
			if (reached)
			{
				if (psDroid->body >= psDroid->original_hp)
				{
					objTrace(psDroid->id, "Repair not needed of droid %d", (int)psDroid->id);
					/* set droid points to max */
					psDroid->body = psDroid->original_hp;
					// if completely repaired then reset order
					secondarySetState(psDroid, DSO_RETURN_TO_LOC, DSS_NONE);
					orderDroidObj(psDroid, ORDER_TYPE::GUARD, psDroid->order.psObj, ModeImmediate);
				}
				else
				{
					objTrace(psDroid->id, "Stopping and waiting for repairs %d", (int)psDroid->id);
					moveStopDroid(psDroid);
					psDroid->action = ACTION::WAITDURINGREPAIR;
				}
			}
			else if (DROID_STOPPED(psDroid))
			{
				//objTrace(psDroid->id, "Droid was stopped, but havent reach the target, moving now");
				//moveDroidToNoFormation(psDroid, psDroid->order.psObj->pos.x, psDroid->order.psObj->pos.y);
			}
		}
		break;
	case ACTION::OBSERVE:
		// align the turret
		actionTargetTurret(psDroid, psDroid->action_target[0], &psDroid->asWeaps[0]);

		if (!cbSensorDroid(psDroid))
		{
			// make sure the target is within sensor range
			const int xdiff = (SDWORD)psDroid->pos.x - (SDWORD)psDroid->action_target[0]->pos.x;
			const int ydiff = (SDWORD)psDroid->pos.y - (SDWORD)psDroid->action_target[0]->pos.y;
			int rangeSq = droidSensorRange(psDroid);
			rangeSq = rangeSq * rangeSq;
			if (!visibleObject(psDroid, psDroid->action_target[0], false)
				|| xdiff * xdiff + ydiff * ydiff >= rangeSq)
			{
				if (secondaryGetState(psDroid, DSO_HALTTYPE) != DSS_HALT_GUARD && (order->type == ORDER_TYPE::NONE || order->
					type == ORDER_TYPE::HOLD))
				{
					psDroid->action = ACTION::NONE;
				}
				else if ((!secHoldActive && order->type != ORDER_TYPE::HOLD) || (secHoldActive && order->type ==
					ORDER_TYPE::OBSERVE))
				{
					psDroid->action = ACTION::MOVETOOBSERVE;
					moveDroidTo(psDroid, psDroid->action_target[0]->pos.x, psDroid->action_target[0]->pos.y);
				}
			}
		}
		break;
	case ACTION::MOVE_TO_OBSERVE:
		// align the turret
		actionTargetTurret(psDroid, psDroid->action_target[0], &psDroid->asWeaps[0]);

		if (visibleObject(psDroid, psDroid->action_target[0], false))
		{
			// make sure the target is within sensor range
			const int xdiff = (SDWORD)psDroid->pos.x - (SDWORD)psDroid->action_target[0]->pos.x;
			const int ydiff = (SDWORD)psDroid->pos.y - (SDWORD)psDroid->action_target[0]->pos.y;
			int rangeSq = droidSensorRange(psDroid);
			rangeSq = rangeSq * rangeSq;
			if ((xdiff * xdiff + ydiff * ydiff < rangeSq) &&
				!DROID_STOPPED(psDroid))
			{
				psDroid->action = ACTION::OBSERVE;
				moveStopDroid(psDroid);
			}
		}
		if (DROID_STOPPED(psDroid) && psDroid->action == ACTION::MOVETOOBSERVE)
		{
			moveDroidTo(psDroid, psDroid->action_target[0]->pos.x, psDroid->action_target[0]->pos.y);
		}
		break;
	case ACTION::FIRE_SUPPORT:
		if (!order->psObj)
		{
			psDroid->action = ACTION::NONE;
			return;
		}
	//can be either a droid or a structure now - AB 7/10/98
		ASSERT_OR_RETURN(, (order->psObj->type == OBJ_DROID || order->psObj->type == OBJ_STRUCTURE)
		                   && aiCheckAlliances(order->psObj->player, psDroid->player),
		                   "ACTION::FIRESUPPORT: incorrect target type");

	//don't move VTOL's
	// also don't move closer to sensor towers
		if (!isVtolDroid(psDroid) && order->psObj->type != OBJ_STRUCTURE)
		{
			Vector2i diff = (psDroid->pos - order->psObj->pos).xy();
			//Consider .shortRange here
			int rangeSq = asWeaponStats[psDroid->asWeaps[0].nStat].upgraded[psDroid->player].maxRange / 2;
			// move close to sensor
			rangeSq = rangeSq * rangeSq;
			if (dot(diff, diff) < rangeSq)
			{
				if (!DROID_STOPPED(psDroid))
				{
					moveStopDroid(psDroid);
				}
			}
			else
			{
				if (!DROID_STOPPED(psDroid))
				{
					diff = order->psObj->pos.xy() - psDroid->movement.destination;
				}
				if (DROID_STOPPED(psDroid) || dot(diff, diff) > rangeSq)
				{
					if (secHoldActive)
					{
						// droid on hold, don't allow moves.
						psDroid->action = ACTION::NONE;
					}
					else
					{
						// move in range
						moveDroidTo(psDroid, order->psObj->pos.x, order->psObj->pos.y);
					}
				}
			}
		}
		break;
	case ACTION::MOVETODROIDREPAIR:
		{
			SimpleObject* actionTargetObj = psDroid->action_target[0];
			ASSERT_OR_RETURN(, actionTargetObj != nullptr && actionTargetObj->type == OBJ_DROID,
			                   "unexpected repair target");
			const Droid* actionTarget = (const Droid*)actionTargetObj;
			if (actionTarget->body == actionTarget->original_hp)
			{
				// target is healthy: nothing to do
				psDroid->action = ACTION::NONE;
				moveStopDroid(psDroid);
				break;
			}
			Vector2i diff = (psDroid->pos - psDroid->action_target[0]->pos).xy();
			// moving to repair a droid
			if (!psDroid->action_target[0] || // Target missing.
				(psDroid->order.type != ORDER_TYPE::DROIDREPAIR && dot(diff, diff) > 2 * REPAIR_MAXDIST * REPAIR_MAXDIST))
			// Target farther then 1.4142 * REPAIR_MAXDIST and we aren't ordered to follow.
			{
				psDroid->action = ACTION::NONE;
				return;
			}
			if (dot(diff, diff) < REPAIR_RANGE * REPAIR_RANGE)
			{
				// Got to destination - start repair
				//rotate turret to point at droid being repaired
				//use 0 for repair droid
				actionTargetTurret(psDroid, psDroid->action_target[0], &psDroid->asWeaps[0]);
				droidStartAction(psDroid);
				psDroid->action = ACTION::DROIDREPAIR;
			}
			if (DROID_STOPPED(psDroid))
			{
				// Couldn't reach destination - try and find a new one
				psDroid->actionPos = psDroid->action_target[0]->pos.xy();
				moveDroidTo(psDroid, psDroid->actionPos.x, psDroid->actionPos.y);
			}
			break;
		}
	case ACTION::DROIDREPAIR:
		{
			int xdiff, ydiff;

			// If not doing self-repair (psActionTarget[0] is repair target)
			if (psDroid->action_target[0] != psDroid)
			{
				actionTargetTurret(psDroid, psDroid->action_target[0], &psDroid->asWeaps[0]);
			}
			// Just self-repairing.
			// See if there's anything to shoot.
			else if (psDroid->numWeaps > 0 && !isVtolDroid(psDroid)
				&& (order->type == ORDER_TYPE::NONE || order->type == ORDER_TYPE::HOLD || order->type == ORDER_TYPE::RTR))
			{
				for (unsigned i = 0; i < psDroid->numWeaps; ++i)
				{
					if (nonNullWeapon[i])
					{
						SimpleObject* psTemp = nullptr;

						WeaponStats* const psWeapStats = &asWeaponStats[psDroid->asWeaps[i].nStat];
						if (psDroid->asWeaps[i].nStat > 0 && psWeapStats->rotate
							&& secondaryGetState(psDroid, DSO_ATTACK_LEVEL) == DSS_ALEV_ALWAYS
							&& aiBestNearestTarget(psDroid, &psTemp, i) >= 0 && psTemp)
						{
							psDroid->action = ACTION::ATTACK;
							setDroidActionTarget(psDroid, psTemp, 0);
							break;
						}
					}
				}
			}
			if (psDroid->action != ACTION::DROIDREPAIR)
			{
				break; // action has changed
			}

			//check still next to the damaged droid
			xdiff = (SDWORD)psDroid->pos.x - (SDWORD)psDroid->action_target[0]->pos.x;
			ydiff = (SDWORD)psDroid->pos.y - (SDWORD)psDroid->action_target[0]->pos.y;
			if (xdiff * xdiff + ydiff * ydiff > REPAIR_RANGE * REPAIR_RANGE)
			{
				if (order->type == ORDER_TYPE::DROIDREPAIR)
				{
					// damaged droid has moved off - follow if we're not holding position!
					psDroid->actionPos = psDroid->action_target[0]->pos.xy();
					psDroid->action = ACTION::MOVETODROIDREPAIR;
					moveDroidTo(psDroid, psDroid->actionPos.x, psDroid->actionPos.y);
				}
				else
				{
					psDroid->action = ACTION::NONE;
				}
			}
			else
			{
				if (!droidUpdateDroidRepair(psDroid))
				{
					psDroid->action = ACTION::NONE;
					moveStopDroid(psDroid);
					//if the order is RTR then resubmit order so that the unit will go to repair facility point
					if (orderState(psDroid, ORDER_TYPE::RTR))
					{
						orderDroid(psDroid, ORDER_TYPE::RTR, ModeImmediate);
					}
				}
				else
				{
					// don't let the target for a repair shuffle
					if (((Droid*)psDroid->action_target[0])->movement.status == MOVESHUFFLE)
					{
						moveStopDroid((Droid*)psDroid->action_target[0]);
					}
				}
			}
			break;
		}
	case ACTION::WAITFORREARM:
		// wait here for the rearm pad to instruct the vtol to move
		if (psDroid->action_target[0] == nullptr)
		{
			// rearm pad destroyed - move to another
			objTrace(psDroid->id, "rearm pad gone - switch to new one");
			moveToRearm(psDroid);
			break;
		}
		if (DROID_STOPPED(psDroid) && vtolHappy(psDroid))
		{
			objTrace(psDroid->id, "do not need to rearm after all");
			// don't actually need to rearm so just sit next to the rearm pad
			psDroid->action = ACTION::NONE;
		}
		break;
	case ACTION::CLEARREARMPAD:
		if (DROID_STOPPED(psDroid))
		{
			psDroid->action = ACTION::NONE;
			objTrace(psDroid->id, "clearing rearm pad");
			if (!vtolHappy(*psDroid))
			// Droid has cleared the rearm pad without getting rearmed. One way this can happen if a rearming pad was built under the VTOL while it was waiting for a pad.
			{
				moveToRearm(psDroid); // Rearm somewhere else instead.
			}
		}
		break;
	case ACTION::WAITDURINGREARM:
		// this gets cleared by the rearm pad
		break;
	case ACTION::MOVETOREARM:
		if (psDroid->action_target[0] == nullptr)
		{
			// base destroyed - find another
			objTrace(psDroid->id, "rearm gone - find another");
			moveToRearm(psDroid);
			break;
		}

		if (visibleObject(psDroid, psDroid->action_target[0], false))
		{
			Structure* const psStruct = findNearestReArmPad(psDroid, (Structure*)psDroid->action_target[0], true);
			// got close to the rearm pad - now find a clear one
			objTrace(psDroid->id, "Seen rearm pad - searching for available one");

			if (psStruct != nullptr)
			{
				// found a clear landing pad - go for it
				objTrace(psDroid->id, "Found clear rearm pad");
				setDroidActionTarget(psDroid, psStruct, 0);
			}

			psDroid->action = ACTION::WAITFORREARM;
		}

		if (DROID_STOPPED(psDroid) || psDroid->action == ACTION::WAITFORREARM)
		{
			Vector2i pos = psDroid->action_target[0]->pos.xy();
			if (!actionVTOLLandingPos(psDroid, &pos))
			{
				// totally bunged up - give up
				objTrace(psDroid->id, "Couldn't find a clear tile near rearm pad - returning to base");
				orderDroid(psDroid, ORDER_TYPE::RTB, ModeImmediate);
				break;
			}
			objTrace(psDroid->id, "moving to rearm pad at %d,%d (%d,%d)", (int)pos.x, (int)pos.y,
			         (int)(pos.x/TILE_UNITS), (int)(pos.y/TILE_UNITS));
			moveDroidToDirect(psDroid, pos.x, pos.y);
		}
		break;
	default:
		ASSERT(!"unknown action", "unknown action");
		break;
	}

	if (psDroid->action != ACTION::MOVEFIRE &&
		psDroid->action != ACTION::ATTACK &&
		psDroid->action != ACTION::MOVETOATTACK &&
		psDroid->action != ACTION::MOVETODROIDREPAIR &&
		psDroid->action != ACTION::DROIDREPAIR &&
		psDroid->action != ACTION::BUILD &&
		psDroid->action != ACTION::OBSERVE &&
		psDroid->action != ACTION::MOVETOOBSERVE)
	{
		//use 0 for all non-combat droid types
		if (psDroid->numWeaps == 0)
		{
			if (psDroid->asWeaps[0].rotation.direction != 0 || psDroid->asWeaps[0].rotation.pitch != 0)
			{
				actionAlignTurret(psDroid, 0);
			}
		}
		else
		{
			for (unsigned i = 0; i < psDroid->numWeaps; ++i)
			{
				if (psDroid->asWeaps[i].rotation.direction != 0 || psDroid->asWeaps[i].rotation.pitch != 0)
				{
					actionAlignTurret(psDroid, i);
				}
			}
		}
	}
	CHECK_DROID(psDroid);
}

/* Overall action function that is called by the specific action functions */
static void actionDroidBase(Droid* psDroid, Action* psAction)
{
	ASSERT_OR_RETURN(, psAction->psObj == nullptr || !psAction->psObj->died, "Droid dead");

	WeaponStats* psWeapStats = getWeaponStats(psDroid, 0);
	Vector2i pos(0, 0);

	CHECK_DROID(psDroid);

	bool secHoldActive = secondaryGetState(psDroid, DSO_HALTTYPE) == DSS_HALT_HOLD;
	psDroid->time_action_started = gameTime;
	syncDebugDroid(psDroid, '-');
	syncDebug("%d does %s", psDroid->id, getDroidActionName(psAction->action));
	objTrace(psDroid->id, "base set action to %s (was %s)", getDroidActionName(psAction->action),
	         getDroidActionName(psDroid->action));

	auto order = &psDroid->order;
	bool hasValidWeapon = false;
	for (int i = 0; i < MAX_WEAPONS; i++)
	{
		hasValidWeapon |= validTarget(psDroid, psAction->psObj, i);
	}
	switch (psAction->action)
	{
	case ACTION::NONE:
		// Clear up what ever the droid was doing before if necessary
		if (!DROID_STOPPED(psDroid))
		{
			moveStopDroid(psDroid);
		}
		psDroid->action = ACTION::NONE;
		psDroid->actionPos = Vector2i(0, 0);
		psDroid->time_action_started = 0;
		psDroid->action_points_done = 0;
		if (psDroid->numWeaps > 0)
		{
			for (int i = 0; i < psDroid->numWeaps; i++)
			{
				setDroidActionTarget(psDroid, nullptr, i);
			}
		}
		else
		{
			setDroidActionTarget(psDroid, nullptr, 0);
		}
		break;

	case ACTION::TRANSPORT_WAIT_TO_FLY_IN:
		psDroid->action = ACTION::TRANSPORTWAITTOFLYIN;
		break;

	case ACTION::ATTACK:
		if (num_weapons(*psDroid) == 0 || isTransporter(*psDroid) || psAction->psObj == psDroid)
		{
			break;
		}
		if (!hasValidWeapon)
		{
			// continuing is pointless, we were given an invalid target
			// for ex. AA gun can't attack ground unit
			break;
		}
		if (electronicDroid(psDroid))
		{
			//check for low or zero resistance - just zero resistance!
			if (psAction->psObj->type == OBJ_STRUCTURE
				&& !validStructResistance((Structure*)psAction->psObj))
			{
				//structure is low resistance already so don't attack
				psDroid->action = ACTION::NONE;
				break;
			}

			//in multiPlayer cannot electronically attack a transporter
			if (bMultiPlayer
				&& psAction->psObj->type == OBJ_DROID
				&& isTransporter((Droid*)psAction->psObj))
			{
				psDroid->action = ACTION::NONE;
				break;
			}
		}

	// note the droid's current pos so that scout & patrol orders know how far the
	// droid has gone during an attack
	// slightly strange place to store this I know, but I didn't want to add any more to the droid
		psDroid->actionPos = psDroid->pos.xy();
		setDroidActionTarget(psDroid, psAction->psObj, 0);

		if (((order->type == ORDER_TYPE::ATTACKTARGET
					|| order->type == ORDER_TYPE::NONE
					|| order->type == ORDER_TYPE::HOLD
					|| (order->type == ORDER_TYPE::GUARD && hasCommander(psDroid))
					|| order->type == ORDER_TYPE::FIRESUPPORT)
				&& secHoldActive)
			|| (!isVtolDroid(psDroid) && (orderStateObj(psDroid, ORDER_TYPE::FIRESUPPORT) != nullptr)))
		{
			psDroid->action = ACTION::ATTACK; // holding, try attack straightaway
		}
		else if (actionInsideMinRange(psDroid, psAction->psObj, psWeapStats)) // too close?
		{
			if (!proj_Direct(psWeapStats))
			{
				if (psWeapStats->rotate)
				{
					psDroid->action = ACTION::ATTACK;
				}
				else
				{
					psDroid->action = ACTION::ROTATETOATTACK;
					moveTurnDroid(psDroid, psDroid->action_target[0]->pos.x, psDroid->action_target[0]->pos.y);
				}
			}
			else if (order->type != ORDER_TYPE::HOLD && secondaryGetState(psDroid, DSO_HALTTYPE) != DSS_HALT_HOLD)
			{
				int pbx = 0;
				int pby = 0;
				/* direct fire - try and extend the range */
				psDroid->action = ACTION::MOVETOATTACK;
				actionCalcPullBackPoint(psDroid, psAction->psObj, &pbx, &pby);

				turnOffMultiMsg(true);
				moveDroidTo(psDroid, (UDWORD)pbx, (UDWORD)pby);
				turnOffMultiMsg(false);
			}
		}
		else if (order->type != ORDER_TYPE::HOLD && secondaryGetState(psDroid, DSO_HALTTYPE) != DSS_HALT_HOLD)
		// approach closer?
		{
			psDroid->action = ACTION::MOVETOATTACK;
			turnOffMultiMsg(true);
			moveDroidTo(psDroid, psAction->psObj->pos.x, psAction->psObj->pos.y);
			turnOffMultiMsg(false);
		}
		else if (order->type != ORDER_TYPE::HOLD && secondaryGetState(psDroid, DSO_HALTTYPE) == DSS_HALT_HOLD)
		{
			psDroid->action = ACTION::ATTACK;
		}
		break;

	case ACTION::MOVETOREARM:
		psDroid->action = ACTION::MOVETOREARM;
		psDroid->actionPos = psAction->psObj->pos.xy();
		psDroid->time_action_started = gameTime;
		setDroidActionTarget(psDroid, psAction->psObj, 0);
		pos = psDroid->action_target[0]->pos.xy();
		if (!actionVTOLLandingPos(psDroid, &pos))
		{
			// totally bunged up - give up
			objTrace(psDroid->id, "move to rearm action failed!");
			orderDroid(psDroid, ORDER_TYPE::RTB, ModeImmediate);
			break;
		}
		objTrace(psDroid->id, "move to rearm");
		moveDroidToDirect(psDroid, pos.x, pos.y);
		break;
	case ACTION::CLEARREARMPAD:
		debug(LOG_NEVER, "Unit %d clearing rearm pad", psDroid->id);
		psDroid->action = ACTION::CLEARREARMPAD;
		setDroidActionTarget(psDroid, psAction->psObj, 0);
		pos = psDroid->action_target[0]->pos.xy();
		if (!actionVTOLLandingPos(psDroid, &pos))
		{
			// totally bunged up - give up
			objTrace(psDroid->id, "clear rearm pad action failed!");
			orderDroid(psDroid, ORDER_TYPE::RTB, ModeImmediate);
			break;
		}
		objTrace(psDroid->id, "move to clear rearm pad");
		moveDroidToDirect(psDroid, pos.x, pos.y);
		break;
	case ACTION::MOVE:
	case ACTION::TRANSPORT_IN:
	case ACTION::TRANSPORT_OUT:
	case ACTION::RETURN_TO_POS:
	case ACTION::FIRE_SUPPORT_RETREAT:
		psDroid->action = psAction->action;
		psDroid->actionPos.x = psAction->x;
		psDroid->actionPos.y = psAction->y;
		psDroid->time_action_started = gameTime;
		setDroidActionTarget(psDroid, psAction->psObj, 0);
		moveDroidTo(psDroid, psAction->x, psAction->y);
		break;

	case ACTION::BUILD:
		if (!order->psStats)
		{
			psDroid->action = ACTION::NONE;
			break;
		}
	//ASSERT_OR_RETURN(, order->type == ORDER_TYPE::BUILD || order->type == ORDER_TYPE::HELPBUILD || order->type == ORDER_TYPE::LINEBUILD, "cannot start build action without a build order");
		ASSERT_OR_RETURN(, psAction->x > 0 && psAction->y > 0, "Bad build order position");
		psDroid->action = ACTION::MOVE_TO_BUILD;
		psDroid->actionPos.x = psAction->x;
		psDroid->actionPos.y = psAction->y;
		moveDroidToNoFormation(psDroid, psDroid->actionPos.x, psDroid->actionPos.y);
		break;
	case ACTION::DEMOLISH:
		ASSERT_OR_RETURN(, order->type == ORDER_TYPE::DEMOLISH, "cannot start demolish action without a demolish order");
		psDroid->action = ACTION::MOVETODEMOLISH;
		psDroid->actionPos.x = psAction->x;
		psDroid->actionPos.y = psAction->y;
		ASSERT_OR_RETURN(, (order->psObj != nullptr) && (order->psObj->type == OBJ_STRUCTURE),
		                   "invalid target for demolish order");
		order->psStats = ((Structure*)order->psObj)->pStructureType;
		setDroidActionTarget(psDroid, psAction->psObj, 0);
		moveDroidTo(psDroid, psAction->x, psAction->y);
		break;
	case ACTION::REPAIR:
		psDroid->action = psAction->action;
		psDroid->actionPos.x = psAction->x;
		psDroid->actionPos.y = psAction->y;
	//this needs setting so that automatic repair works
		setDroidActionTarget(psDroid, psAction->psObj, 0);
		ASSERT_OR_RETURN(,
            (psDroid->action_target[0] != nullptr) && (psDroid->action_target[0]->type == OBJ_STRUCTURE),
			"invalid target for repair order");
		order->psStats = ((Structure*)psDroid->action_target[0])->pStructureType;
		if (secHoldActive && (order->type == ORDER_TYPE::NONE || order->type == ORDER_TYPE::HOLD))
		{
			psDroid->action = ACTION::REPAIR;
		}
		else if ((!secHoldActive && order->type != ORDER_TYPE::HOLD) || (secHoldActive && order->type == ORDER_TYPE::REPAIR))
		{
			psDroid->action = ACTION::MOVETOREPAIR;
			moveDroidTo(psDroid, psAction->x, psAction->y);
		}
		break;
	case ACTION::OBSERVE:
		psDroid->action = psAction->action;
		setDroidActionTarget(psDroid, psAction->psObj, 0);
		psDroid->actionPos.x = psDroid->pos.x;
		psDroid->actionPos.y = psDroid->pos.y;
		if (secondaryGetState(psDroid, DSO_HALTTYPE) != DSS_HALT_GUARD && (order->type == ORDER_TYPE::NONE || order->type ==
			ORDER_TYPE::HOLD))
		{
			psDroid->action = visibleObject(psDroid, psDroid->action_target[0], false)
				                  ? ACTION::OBSERVE
				                  : ACTION::NONE;
		}
		else if (!cbSensorDroid(psDroid) && ((!secHoldActive && order->type != ORDER_TYPE::HOLD) || (secHoldActive && order->
			type == ORDER_TYPE::OBSERVE)))
		{
			psDroid->action = ACTION::MOVETOOBSERVE;
			moveDroidTo(psDroid, psDroid->action_target[0]->pos.x, psDroid->action_target[0]->pos.y);
		}
		break;
	case ACTION::FIRESUPPORT:
		psDroid->action = ACTION::FIRESUPPORT;
		if (!isVtolDroid(psDroid) && !secHoldActive && order->psObj->type != OBJ_STRUCTURE)
		{
			moveDroidTo(psDroid, order->psObj->pos.x, order->psObj->pos.y); // movetotarget.
		}
		break;
	case ACTION::SULK:
		psDroid->action = ACTION::SULK;
	// hmmm, hope this doesn't cause any problems!
		psDroid->time_action_started = gameTime + MIN_SULK_TIME + (gameRand(MAX_SULK_TIME - MIN_SULK_TIME));
		break;
	case ACTION::WAITFORREPAIR:
		psDroid->action = ACTION::WAITFORREPAIR;
	// set the time so we can tell whether the start the self repair or not
		psDroid->time_action_started = gameTime;
		break;
	case ACTION::MOVETOREPAIRPOINT:
		psDroid->action = psAction->action;
		psDroid->actionPos.x = psAction->x;
		psDroid->actionPos.y = psAction->y;
		psDroid->time_action_started = gameTime;
		setDroidActionTarget(psDroid, psAction->psObj, 0);
		moveDroidToNoFormation(psDroid, psAction->x, psAction->y);
		break;
	case ACTION::WAITDURINGREPAIR:
		psDroid->action = ACTION::WAITDURINGREPAIR;
		break;
	case ACTION::MOVETOREARMPOINT:
		objTrace(psDroid->id, "set to move to rearm pad");
		psDroid->action = psAction->action;
		psDroid->actionPos.x = psAction->x;
		psDroid->actionPos.y = psAction->y;
		psDroid->time_action_started = gameTime;
		setDroidActionTarget(psDroid, psAction->psObj, 0);
		moveDroidToDirect(psDroid, psAction->x, psAction->y);

	// make sure there aren't any other VTOLs on the rearm pad
		ensureRearmPadClear((Structure*)psAction->psObj, psDroid);
		break;
	case ACTION::DROIDREPAIR:
		{
			psDroid->action = psAction->action;
			psDroid->actionPos.x = psAction->x;
			psDroid->actionPos.y = psAction->y;
			setDroidActionTarget(psDroid, psAction->psObj, 0);
			//initialise the action points
			psDroid->action_points_done = 0;
			psDroid->time_action_started = gameTime;
			const auto xdiff = (SDWORD)psDroid->pos.x - (SDWORD)psAction->x;
			const auto ydiff = (SDWORD)psDroid->pos.y - (SDWORD)psAction->y;
			if (secHoldActive && (order->type == ORDER_TYPE::NONE || order->type == ORDER_TYPE::HOLD))
			{
				psDroid->action = ACTION::DROID_REPAIR;
			}
			else if (((!secHoldActive && order->type != ORDER_TYPE::HOLD) || (secHoldActive && order->type ==
					ORDER_TYPE::DROIDREPAIR))
				// check that we actually need to move closer
				&& ((xdiff * xdiff + ydiff * ydiff) > REPAIR_RANGE * REPAIR_RANGE))
			{
				psDroid->action = ACTION::MOVETODROIDREPAIR;
				moveDroidTo(psDroid, psAction->x, psAction->y);
			}
			break;
		}
	case ACTION::RESTORE:
		ASSERT_OR_RETURN(, order->type == ORDER_TYPE::RESTORE, "cannot start restore action without a restore order");
		psDroid->action = psAction->action;
		psDroid->actionPos.x = psAction->x;
		psDroid->actionPos.y = psAction->y;
		ASSERT_OR_RETURN(, (order->psObj != nullptr) && (order->psObj->type == OBJ_STRUCTURE),
		                   "invalid target for restore order");
		order->psStats = ((Structure*)order->psObj)->pStructureType;
		setDroidActionTarget(psDroid, psAction->psObj, 0);
		if (order->type != ORDER_TYPE::HOLD)
		{
			psDroid->action = ACTION::MOVETORESTORE;
			moveDroidTo(psDroid, psAction->x, psAction->y);
		}
		break;
	default:
		ASSERT(!"unknown action", "actionUnitBase: unknown action");
		break;
	}
	syncDebugDroid(psDroid, '+');
	CHECK_DROID(psDroid);
}


/* Give a droid an action */
void actionDroid(Droid* psDroid, ACTION action)
{
	Action sAction;

	memset(&sAction, 0, sizeof(Action));
	sAction.action = action;
	actionDroidBase(psDroid, &sAction);
}

/* Give a droid an action with a location target */
void actionDroid(Droid* psDroid, ACTION action, UDWORD x, UDWORD y)
{
	Action sAction;

	memset(&sAction, 0, sizeof(Action));
	sAction.action = action;
	sAction.x = x;
	sAction.y = y;
	actionDroidBase(psDroid, &sAction);
}

/* Give a droid an action with an object target */
void actionDroid(Droid* psDroid, ACTION action, SimpleObject* psObj)
{
	Action sAction;

	memset(&sAction, 0, sizeof(Action));
	sAction.action = action;
	sAction.psObj = psObj;
	sAction.x = psObj->pos.x;
	sAction.y = psObj->pos.y;
	actionDroidBase(psDroid, &sAction);
}

/* Give a droid an action with an object target and a location */
void actionDroid(Droid* psDroid, ACTION action,
                 SimpleObject* psObj, UDWORD x, UDWORD y)
{
	Action sAction;

	memset(&sAction, 0, sizeof(Action));
	sAction.action = action;
	sAction.psObj = psObj;
	sAction.x = x;
	sAction.y = y;
	actionDroidBase(psDroid, &sAction);
}


/*send the vtol droid back to the nearest rearming pad - if one otherwise
return to base*/
void moveToRearm(Droid* psDroid)
{
	CHECK_DROID(psDroid);

	if (!isVtolDroid(psDroid))
	{
		return;
	}

	//if droid is already returning - ignore
	if (vtolRearming(psDroid))
	{
		return;
	}

	//get the droid to fly back to a ReArming Pad
	// don't worry about finding a clear one for the minute
	Structure* psStruct = findNearestReArmPad(psDroid, psDroid->associated_structure, false);
	if (psStruct)
	{
		// note a base rearm pad if the vtol doesn't have one
		if (psDroid->associated_structure == nullptr)
		{
			setDroidBase(psDroid, psStruct);
		}

		//return to re-arming pad
		if (psDroid->order.type == ORDER_TYPE::NONE)
		{
			// no order set - use the rearm order to ensure the unit goes back
			// to the landing pad
			orderDroidObj(psDroid, ORDER_TYPE::REARM, psStruct, ModeImmediate);
		}
		else
		{
			actionDroid(psDroid, ACTION::MOVETOREARM, psStruct);
		}
	}
	else
	{
		//return to base un-armed
		objTrace(psDroid->id, "Did not find an available rearm pad - RTB instead");
		orderDroid(psDroid, ORDER_TYPE::RTB, ModeImmediate);
	}
}


//// whether a tile is suitable for a vtol to land on
//static bool vtolLandingTile(SDWORD x, SDWORD y)
//{
//	if (x < 0 || x >= (SDWORD)mapWidth || y < 0 || y >= (SDWORD)mapHeight)
//	{
//		return false;
//	}
//
//	const MAPTILE* psTile = mapTile(x, y);
//	if (psTile->tileInfoBits & BITS_FPATHBLOCK ||
//		TileIsOccupied(psTile) ||
//		terrainType(psTile) == TER_CLIFFFACE ||
//		terrainType(psTile) == TER_WATER)
//	{
//		return false;
//	}
//	return true;
//}

///**
// * Performs a space-filling spiral-like search from startX,startY up to (and
// * including) radius. For each tile, the search function is called; if it
// * returns 'true', the search will finish immediately.
// *
// * @param startX,startY starting x and y coordinates
// *
// * @param max_radius radius to examine. Search will finish when @c max_radius is exceeded.
// *
// * @param match searchFunction to use; described in typedef
// * \param matchState state for the search function
// * \return true if finished because the searchFunction requested termination,
// *         false if the radius limit was reached
// */
//static bool spiralSearch(int startX, int startY, int max_radius, tileMatchFunction match, void* matchState)
//{
//	// test center tile
//	if (match(startX, startY, matchState))
//	{
//		return true;
//	}
//
//	// test for each radius, from 1 to max_radius (inclusive)
//	for (int radius = 1; radius <= max_radius; ++radius)
//	{
//		// choose tiles that are between radius and radius+1 away from center
//		// distances are squared
//		const int min_distance = radius * radius;
//		const int max_distance = min_distance + 2 * radius;
//
//		// X offset from startX
//		int dx;
//
//		// dx starts with 1, to visiting tiles on same row or col as start twice
//		for (dx = 1; dx <= max_radius; dx++)
//		{
//			// Y offset from startY
//			int dy;
//
//			for (dy = 0; dy <= max_radius; dy++)
//			{
//				// Current distance, squared
//				const int distance = dx * dx + dy * dy;
//
//				// Ignore tiles outside of the current circle
//				if (distance < min_distance || distance > max_distance)
//				{
//					continue;
//				}
//
//				// call search function for each of the 4 quadrants of the circle
//				if (match(startX + dx, startY + dy, matchState)
//					|| match(startX - dx, startY - dy, matchState)
//					|| match(startX + dy, startY - dx, matchState)
//					|| match(startX - dy, startY + dx, matchState))
//				{
//					return true;
//				}
//			}
//		}
//	}
//
//	return false;
//}
//
///**
// * an internal tileMatchFunction that checks if x and y are coordinates of a
// * valid landing place.
// *
// * @param matchState a pointer to a Vector2i where these coordintates should be stored
// *
// * @return true if coordinates are a valid landing tile, false if not.
// */
//static bool vtolLandingTileSearchFunction(int x, int y, void* matchState)
//{
//	Vector2i* const xyCoords = (Vector2i*)matchState;
//
//	if (vtolLandingTile(x, y))
//	{
//		xyCoords->x = x;
//		xyCoords->y = y;
//		return true;
//	}
//
//	return false;
//}

//// Choose a landing position for a VTOL when it goes to rearm that is close to rearm
//// pad but not on it, since it may be busy by the time we get there.
//bool actionVTOLLandingPos(DROID const* psDroid, Vector2i* p)
//{
//	CHECK_DROID(psDroid);
//
//	/* Initial box dimensions and set iteration count to zero */
//	int startX = map_coord(p->x);
//	int startY = map_coord(p->y);
//
//	// set blocking flags for all the other droids
//	for (const DROID* psCurr = apsDroidLists[psDroid->player]; psCurr; psCurr = psCurr->psNext)
//	{
//		Vector2i t(0, 0);
//		if (DROID_STOPPED(psCurr))
//		{
//			t = map_coord(psCurr->pos.xy());
//		}
//		else
//		{
//			t = map_coord(psCurr->sMove.destination);
//		}
//		if (psCurr != psDroid)
//		{
//			if (tileOnMap(t))
//			{
//				mapTile(t)->tileInfoBits |= BITS_FPATHBLOCK;
//			}
//		}
//	}
//
//	// search for landing tile; will stop when found or radius exceeded
//	Vector2i xyCoords(0, 0);
//	const bool foundTile = spiralSearch(startX, startY, vtolLandingRadius,
//	                                    vtolLandingTileSearchFunction, &xyCoords);
//	if (foundTile)
//	{
//		objTrace(psDroid->id, "Unit %d landing pos (%d,%d)", psDroid->id, xyCoords.x, xyCoords.y);
//		p->x = world_coord(xyCoords.x) + TILE_UNITS / 2;
//		p->y = world_coord(xyCoords.y) + TILE_UNITS / 2;
//	}
//
//	// clear blocking flags for all the other droids
//	for (DROID* psCurr = apsDroidLists[psDroid->player]; psCurr; psCurr = psCurr->psNext)
//	{
//		Vector2i t(0, 0);
//		if (DROID_STOPPED(psCurr))
//		{
//			t = map_coord(psCurr->pos.xy());
//		}
//		else
//		{
//			t = map_coord(psCurr->sMove.destination);
//		}
//		if (tileOnMap(t))
//		{
//			mapTile(t)->tileInfoBits &= ~BITS_FPATHBLOCK;
//		}
//	}
//
//	return foundTile;
//}
