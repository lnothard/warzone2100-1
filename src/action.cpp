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
#include "visibility.h"

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
bool actionTargetTurret(SimpleObject* psAttacker, SimpleObject* psTarget, Weapon* psWeapon)
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
			pitchLowerLimit = DEG(psWeapStats.minElevation);
			pitchUpperLimit = DEG(psWeapStats.maxElevation);
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
	int rotationError = angleDelta(targetRotation - (tRotation + psAttacker->getRotation().direction));

	tRotation += clip(rotationError, -rotRate, rotRate); // Addition wrapping intentional.
	if (dynamic_cast<Droid*>(psAttacker) && dynamic_cast<Droid*>(psAttacker)->isVtol())
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
	bool onTarget = abs(angleDelta(targetRotation - (tRotation + psAttacker->getRotation().direction))) <= rotationTolerance;

	/* Set muzzle pitch if not repairing or outside minimum range */
	const int minRange = proj_GetMinRange(psWeapStats, psAttacker->getPlayer());
	if (!bRepair && (unsigned)objectPositionSquareDiff(psAttacker->getPosition(),
                                                     psTarget->getPosition()) > minRange * minRange) {
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

  psWeapon->setRotation({static_cast<int>(tRotation),
                        static_cast<int>(tPitch),
                        psWeapon->getRotation().roll});

	return onTarget;
}

// return whether a droid can see a target to fire on it
bool actionVisibleTarget(Droid* psDroid, SimpleObject* psTarget, int weapon_slot)
{
	ASSERT_OR_RETURN(false, psTarget != nullptr, "Target is NULL");
	ASSERT_OR_RETURN(false, psDroid->getPlayer() < MAX_PLAYERS, "psDroid->player (%" PRIu8 ") must be < MAX_PLAYERS",
                   psDroid->getPlayer());
	if (!psTarget->visible[psDroid->getPlayer()]) {
		return false;
	}
	if ((num_weapons(*psDroid) == 0 || psDroid->isVtol() && visibleObject(psDroid, psTarget, false))
	{
		return true;
	}
	return (orderState(psDroid, ORDER_TYPE::FIRE_SUPPORT) ||
          visibleObject(psDroid, psTarget, false) > UBYTE_MAX / 2) &&
         lineOfFire(psDroid, psTarget, weapon_slot, true);
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
	Vector2i delta = map_coord(psDroid->getPosition().xy()) - b.map;
	return delta.x >= -1 && delta.x <= b.size.x &&
         delta.y >= -1 && delta.y <= b.size.y;
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
			Vector2i newPos = droid->getPosition().xy() + iSinCosR(iAtan2(bestDest - droid->getPosition().xy()),
			                                             gameTimeAdjustedIncrement(TILE_UNITS));
			droidSetPosition(droid, newPos.x, newPos.y);
		}
	}

	return buildPosEmpty;
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
