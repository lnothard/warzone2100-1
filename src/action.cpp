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

// data required for any action
struct DROID_ACTION_DATA
{
	DROID_ACTION action;
	UDWORD x, y;
	//multiple action target info
        GameObject *targetObj;
        StatsObject *targetStats;
};

// Check if a droid has stopped moving
#define DROID_STOPPED(psDroid) \
	(psDroid->sMove.Status == MOVEINACTIVE || psDroid->sMove.Status == MOVEHOVER || \
	 psDroid->sMove.Status == MOVESHUFFLE)

/** Radius for search when looking for VTOL landing position */
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

	bool bRepair = psAttacker->type == OBJ_DROID && ((Droid *)psAttacker)->droidType == DROID_REPAIR;

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
	Vector3i attackerMuzzlePos = psAttacker->position;  // Using for calculating the pitch, but not the direction, in case using the exact direction causes bugs somewhere.
	if (psAttacker->type == OBJ_STRUCTURE)
	{
          Structure *psStructure = (Structure *)psAttacker;
		int weapon_slot = psWeapon - psStructure->m_weaponList;  // Should probably be passed weapon_slot instead of psWeapon.
		calcStructureMuzzleLocation(psStructure, &attackerMuzzlePos, weapon_slot);
		pitchLowerLimit = DEG(psWeapStats->minElevation);
		pitchUpperLimit = DEG(psWeapStats->maxElevation);
	}
	else if (psAttacker->type == OBJ_DROID)
	{
          Droid *psDroid = (Droid *)psAttacker;
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
	targetRotation = calcDirection(psAttacker->position.x, psAttacker->position.y, psTarget->position.x, psTarget->position.y);

	//restrict rotationerror to =/- 180 degrees
	int rotationError = angleDelta(targetRotation - (tRotation + psAttacker->rotation.direction));

	tRotation += clip(rotationError, -rotRate, rotRate);  // Addition wrapping intentional.
	if (psAttacker->type == OBJ_DROID && isVtolDroid((Droid *)psAttacker))
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
		Vector3i delta = psTarget->position - attackerMuzzlePos;
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


// return whether a droid can see a target to fire on it
bool actionVisibleTarget(Droid *psDroid, GameObject *psTarget, int weapon_slot)
{
	CHECK_DROID(psDroid);
	ASSERT_OR_RETURN(false, psTarget != nullptr, "Target is NULL");
	ASSERT_OR_RETURN(false, psDroid->owningPlayer < MAX_PLAYERS, "psDroid->player (%" PRIu8 ") must be < MAX_PLAYERS", psDroid->owningPlayer);
	if (!psTarget->visible[psDroid->owningPlayer])
	{
		return false;
	}
	if ((psDroid->numWeapons == 0 || isVtolDroid(psDroid)) && visibleObject(psDroid, psTarget, false))
	{
		return true;
	}
	return (orderState(psDroid, DORDER_FIRESUPPORT)	|| visibleObject(psDroid, psTarget, false) > UBYTE_MAX / 2)
	       && lineOfFire(psDroid, psTarget, weapon_slot, true);
}

// calculate a position for units to pull back to if they
// need to increase the range between them and a target
static void actionCalcPullBackPoint(GameObject *psObj, GameObject *psTarget, int *px, int *py)
{
	// get the vector from the target to the object
	int xdiff = psObj->position.x - psTarget->position.x;
	int ydiff = psObj->position.y - psTarget->position.y;
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
	*px = psObj->position.x + xdiff * PULL_BACK_DIST;
	*py = psObj->position.y + ydiff * PULL_BACK_DIST;

	// make sure coordinates stay inside of the map
	clip_world_offmap(px, py);
}

// check whether a droid is in the neighboring tile of another droid
bool actionReachedDroid(Droid const *psDroid, Droid const *psOther)
{
	ASSERT_OR_RETURN(false, psDroid != nullptr && psOther != nullptr, "Bad droids");
	CHECK_DROID(psDroid);
	Vector2i xy = map_coord(psDroid->position.xy());
	Vector2i otherxy = map_coord(psOther->position.xy());
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
	Vector2i delta = map_coord(psDroid->position.xy()) - b.map;
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

		Vector2i delta = map_coord(droid->position.xy()) - b.map;
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
				unsigned dist = iHypot(droid->position.xy() - dest);
				if (dist < bestDist && !fpathBlockingTile(map_coord(dest.x), map_coord(dest.y), getPropulsionStats(droid)->propulsionType))
				{
					bestDest = dest;
					bestDist = dist;
				}
			}
		if (bestDist != UINT32_MAX)
		{
			// Push the droid out of the way.
			Vector2i newPos = droid->position.xy() + iSinCosR(iAtan2(bestDest - droid->position.xy()), gameTimeAdjustedIncrement(TILE_UNITS));
			droidSetPosition(droid, newPos.x, newPos.y);
		}
	}

	return buildPosEmpty;
}

// Update the action state for a droid
void actionUpdateDroid(Droid *psDroid)
{
	bool (*actionUpdateFunc)(Droid * psDroid) = nullptr;
	bool nonNullWeapon[MAX_WEAPONS] = { false };
        GameObject *psTargets[MAX_WEAPONS] = { nullptr };
	bool hasValidWeapon = false;
	bool hasVisibleTarget = false;
	bool targetVisibile[MAX_WEAPONS] = { false };
	bool bHasTarget = false;
	bool bDirect = false;
        Structure *blockingWall = nullptr;
	bool wallBlocked = false;

	CHECK_DROID(psDroid);

	PROPULSION_STATS *psPropStats = asPropulsionStats + psDroid->asBits[COMP_PROPULSION];
	ASSERT_OR_RETURN(, psPropStats != nullptr, "Invalid propulsion stats pointer");

	bool secHoldActive = secondaryGetState(psDroid, DSO_HALTTYPE) == DSS_HALT_HOLD;

	actionSanity(psDroid);

	//if the droid has been attacked by an EMP weapon, it is temporarily disabled
	if (psDroid->lastHitWeapon == WSC_EMP)
	{
		if (gameTime - psDroid->timeLastHit > EMP_DISABLE_TIME)
		{
			//the actionStarted time needs to be adjusted
			psDroid->actionStarted += (gameTime - psDroid->timeLastHit);
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

	for (unsigned i = 0; i < psDroid->numWeapons; ++i)
	{
		if (psDroid->m_weaponList[i].nStat > 0)
		{
			nonNullWeapon[i] = true;
		}
	}

	// HACK: Apparently we can't deal with a droid that only has NULL weapons ?
	// FIXME: Find out whether this is really necessary
	if (psDroid->numWeapons <= 1)
	{
		nonNullWeapon[0] = true;
	}

	DROID_ORDER_DATA *order = &psDroid->order;

	switch (psDroid->action)
	{
	case DACTION_NONE:
	case DACTION_WAITFORREPAIR:
		// doing nothing
		// see if there's anything to shoot.
		if (psDroid->numWeapons > 0 && !isVtolDroid(psDroid)
		    && (order->type == DORDER_NONE || order->type == DORDER_HOLD || order->type == DORDER_RTR || order->type == DORDER_GUARD))
		{
			for (unsigned i = 0; i < psDroid->numWeapons; ++i)
			{
				if (nonNullWeapon[i])
				{
                                  GameObject *psTemp = nullptr;

					WEAPON_STATS *const psWeapStats = &asWeaponStats[psDroid->m_weaponList[i].nStat];
					if (psDroid->m_weaponList[i].nStat > 0
					    && psWeapStats->rotate
					    && aiBestNearestTarget(psDroid, &psTemp, i) >= 0)
					{
						if (secondaryGetState(psDroid, DSO_ATTACK_LEVEL) == DSS_ALEV_ALWAYS)
						{
							psDroid->action = DACTION_ATTACK;
							setDroidActionTarget(psDroid, psTemp, i);
						}
					}
				}
			}
		}
		break;
	case DACTION_WAITDURINGREPAIR:
		// Check that repair facility still exists
		if (!order->psObj)
		{
			psDroid->action = DACTION_NONE;
			break;
		}
		if (order->type == DORDER_RTR && order->rtrType == RTR_TYPE_REPAIR_FACILITY)
		{
			// move back to the repair facility if necessary
			if (DROID_STOPPED(psDroid) &&
				!actionReachedBuildPos(psDroid,
									order->psObj->position.x, order->psObj->position.y, ((Structure *)order->psObj)->rotation.direction,
									((Structure *)order->psObj)->stats))
			{
				moveDroidToNoFormation(psDroid, order->psObj->position.x, order->psObj->position.y);
			}
		}
		else if (order->type == DORDER_RTR && order->rtrType == RTR_TYPE_DROID && DROID_STOPPED(psDroid)) {
			if (!actionReachedDroid(psDroid, static_cast<Droid *> (order->psObj)))
			{
				moveDroidToNoFormation(psDroid, order->psObj->position.x, order->psObj->position.y);
			} else {
				moveStopDroid(psDroid);
			}
		}
		break;
	case DACTION_TRANSPORTWAITTOFLYIN:
		//if we're moving droids to safety and currently waiting to fly back in, see if time is up
		if (psDroid->owningPlayer == selectedPlayer && getDroidsToSafetyFlag())
		{
			bool enoughTimeRemaining = (mission.time - (gameTime - mission.startTime)) >= (60 * GAME_TICKS_PER_SEC);
			if (((SDWORD)(mission.ETA - (gameTime - missionGetReinforcementTime())) <= 0) && enoughTimeRemaining)
			{
				UDWORD droidX, droidY;

				if (!droidRemove(psDroid, mission.apsDroidLists))
				{
					ASSERT_OR_RETURN(, false, "Unable to remove transporter from mission list");
				}
				addDroid(psDroid, allDroidLists);
				//set the x/y up since they were set to INVALID_XY when moved offWorld
				missionGetTransporterExit(selectedPlayer, &droidX, &droidY);
				psDroid->position.x = droidX;
				psDroid->position.y = droidY;
				//fly Transporter back to get some more droids
				orderDroidLoc(psDroid, DORDER_TRANSPORTIN,
				              getLandingX(selectedPlayer), getLandingY(selectedPlayer), ModeImmediate);
			}
		}
		break;

	case DACTION_MOVE:
	case DACTION_RETURNTOPOS:
	case DACTION_FIRESUPPORT_RETREAT:
		// moving to a location
		if (DROID_STOPPED(psDroid))
		{
			bool notify = psDroid->action == DACTION_MOVE;
			// Got to destination
			psDroid->action = DACTION_NONE;

			if (notify)
			{
				/* notify scripts we have reached the destination
				*  also triggers when patrolling and reached a waypoint
				*/

				triggerEventDroidIdle(psDroid);
			}
		}
		//added multiple weapon check
		else if (psDroid->numWeapons > 0)
		{
			for (unsigned i = 0; i < psDroid->numWeapons; ++i)
			{
				if (nonNullWeapon[i])
				{
                                  GameObject *psTemp = nullptr;

					//I moved psWeapStats flag update there
					WEAPON_STATS *const psWeapStats = &asWeaponStats[psDroid->m_weaponList[i].nStat];
					if (!isVtolDroid(psDroid)
					    && psDroid->m_weaponList[i].nStat > 0
					    && psWeapStats->rotate
					    && psWeapStats->fireOnMove
					    && aiBestNearestTarget(psDroid, &psTemp, i) >= 0)
					{
						if (secondaryGetState(psDroid, DSO_ATTACK_LEVEL) == DSS_ALEV_ALWAYS)
						{
							psDroid->action = DACTION_MOVEFIRE;
							setDroidActionTarget(psDroid, psTemp, i);
						}
					}
				}
			}
		}
		break;
	case DACTION_TRANSPORTIN:
	case DACTION_TRANSPORTOUT:
		actionUpdateTransporter(psDroid);
		break;
	case DACTION_MOVEFIRE:
		// check if vtol is armed
		if (vtolEmpty(psDroid))
		{
			moveToRearm(psDroid);
		}
		// If droid stopped, it can no longer be in DACTION_MOVEFIRE
		if (DROID_STOPPED(psDroid))
		{
			psDroid->action = DACTION_NONE;
			break;
		}
		// loop through weapons and look for target for each weapon
		bHasTarget = false;
		for (unsigned i = 0; i < psDroid->numWeapons; ++i)
		{
			bDirect = proj_Direct(asWeaponStats + psDroid->m_weaponList[i].nStat);
			blockingWall = nullptr;
			// Does this weapon have a target?
			if (psDroid->psActionTarget[i] != nullptr)
			{
				// Is target worth shooting yet?
				if (aiObjectIsProbablyDoomed(psDroid->psActionTarget[i], bDirect))
				{
					setDroidActionTarget(psDroid, nullptr, i);
				}
				// Is target from our team now? (Electronic Warfare)
				else if (electronicDroid(psDroid) && psDroid->owningPlayer == psDroid->psActionTarget[i]->owningPlayer)
				{
					setDroidActionTarget(psDroid, nullptr, i);
				}
				// Is target blocked by a wall?
				else if (bDirect && visGetBlockingWall(psDroid, psDroid->psActionTarget[i]))
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
                                GameObject *psTemp;
				if (aiBestNearestTarget(psDroid, &psTemp, i) >= 0) // assuming aiBestNearestTarget checks for electronic warfare
				{
					bHasTarget = true;
					setDroidActionTarget(psDroid, psTemp, i); // this updates psDroid->psActionTarget[i] to != NULL
				}
			}
			// If we have a target for the weapon: is it visible?
			if (psDroid->psActionTarget[i] != nullptr && visibleObject(psDroid, psDroid->psActionTarget[i], false) > UBYTE_MAX / 2)
			{
				hasVisibleTarget = true; // droid have a visible target to shoot
				targetVisibile[i] = true;// it is at least visible for this weapon
			}
		}
		// if there is at least one target
		if (bHasTarget)
		{
			// loop through weapons
			for (unsigned i = 0; i < psDroid->numWeapons; ++i)
			{
				const unsigned compIndex = psDroid->m_weaponList[i].nStat;
				const WEAPON_STATS *psStats = asWeaponStats + compIndex;
				wallBlocked = false;

				// has weapon a target? is target valid?
				if (psDroid->psActionTarget[i] != nullptr && validTarget(psDroid, psDroid->psActionTarget[i], i))
				{
					// is target visible and weapon is not a Nullweapon?
					if (targetVisibile[i] && nonNullWeapon[i]) //to fix a AA-weapon attack ground unit exploit
					{
                                          GameObject *psActionTarget = nullptr;
						blockingWall = visGetBlockingWall(psDroid, psDroid->psActionTarget[i]);

						if (proj_Direct(psStats) && blockingWall)
						{
							WEAPON_EFFECT weapEffect = psStats->weaponEffect;

							if (!aiCheckAlliances(psDroid->owningPlayer, blockingWall->owningPlayer)
								&& asStructStrengthModifier[weapEffect][blockingWall->stats
                                                                         ->strength] >= MIN_STRUCTURE_BLOCK_STRENGTH)
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
							psActionTarget = psDroid->psActionTarget[i];
						}

						// is the turret aligned with the target?
						if (!wallBlocked && actionTargetTurret(psDroid, psActionTarget, &psDroid->m_weaponList
                                                             [i]))
						{
							// In range - fire !!!
							combFire(&psDroid->m_weaponList
                                                                     [i], psDroid, psActionTarget, i);
						}
					}
				}
			}
			// Droid don't have a visible target and it is not in pursue mode
			if (!hasVisibleTarget && secondaryGetState(psDroid, DSO_ATTACK_LEVEL) != DSS_ALEV_ALWAYS)
			{
				// Target lost
				psDroid->action = DACTION_MOVE;
			}
		}
		// it don't have a target, change to DACTION_MOVE
		else
		{
			psDroid->action = DACTION_MOVE;
		}
		//check its a VTOL unit since adding Transporter's into multiPlayer
		/* check vtol attack runs */
		if (isVtolDroid(psDroid))
		{
			actionUpdateVtolAttack(psDroid);
		}
		break;
	case DACTION_ATTACK:
	case DACTION_ROTATETOATTACK:
		if (psDroid->psActionTarget[0] == nullptr &&  psDroid->psActionTarget[1] != nullptr)
		{
			break;
		}
		ASSERT_OR_RETURN(, psDroid->psActionTarget[0] != nullptr, "target is NULL while attacking");

		if (psDroid->action == DACTION_ROTATETOATTACK)
		{
			if (psDroid->sMove.Status == MOVETURNTOTARGET)
			{
				moveTurnDroid(psDroid, psDroid->psActionTarget[0]->position.x, psDroid->psActionTarget[0]->position.y);
				break;  // Still turning.
			}
			psDroid->action = DACTION_ATTACK;
		}

		//check the target hasn't become one the same player ID - Electronic Warfare
		if (electronicDroid(psDroid) && psDroid->owningPlayer == psDroid->psActionTarget[0]->owningPlayer)
		{
			for (unsigned i = 0; i < psDroid->numWeapons; ++i)
			{
				setDroidActionTarget(psDroid, nullptr, i);
			}
			psDroid->action = DACTION_NONE;
			break;
		}

		bHasTarget = false;
		wallBlocked = false;
		for (unsigned i = 0; i < psDroid->numWeapons; ++i)
		{
                  GameObject *psActionTarget;

			if (i > 0)
			{
				// If we're ordered to shoot something, and we can, shoot it
				if ((order->type == DORDER_ATTACK || order->type == DORDER_ATTACKTARGET) &&
				    psDroid->psActionTarget[i] != psDroid->psActionTarget[0] &&
				    validTarget(psDroid, psDroid->psActionTarget[0], i) &&
				    actionInRange(psDroid, psDroid->psActionTarget[0], i))
				{
					setDroidActionTarget(psDroid, psDroid->psActionTarget[0], i);
				}
				// If we still don't have a target, try to find one
				else
				{
					if (psDroid->psActionTarget[i] == nullptr &&
					    aiChooseTarget(psDroid, &psTargets[i], i, false, nullptr))  // Can probably just use psTarget instead of psTargets[i], and delete the psTargets variable.
					{
						setDroidActionTarget(psDroid, psTargets[i], i);
					}
				}
			}

			if (psDroid->psActionTarget[i])
			{
				psActionTarget = psDroid->psActionTarget[i];
			}
			else
			{
				psActionTarget = psDroid->psActionTarget[0];
			}

			if (nonNullWeapon[i]
			    && actionVisibleTarget(psDroid, psActionTarget, i)
			    && actionInRange(psDroid, psActionTarget, i))
			{
				WEAPON_STATS *const psWeapStats = &asWeaponStats[psDroid->m_weaponList[i].nStat];
				WEAPON_EFFECT weapEffect = psWeapStats->weaponEffect;
				blockingWall = visGetBlockingWall(psDroid, psActionTarget);

				// if a wall is inbetween us and the target, try firing at the wall if our
				// weapon is good enough
				if (proj_Direct(psWeapStats) && blockingWall)
				{
					if (!aiCheckAlliances(psDroid->owningPlayer, blockingWall->owningPlayer)
						&& asStructStrengthModifier[weapEffect][blockingWall->stats->strength] >= MIN_STRUCTURE_BLOCK_STRENGTH)
					{
						psActionTarget = (GameObject *)blockingWall;
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
						const uint16_t targetDir = calcDirection(psDroid->position.x, psDroid->position.y, psActionTarget->position
                                                            .x, psActionTarget->position
                                                            .y);
						dirDiff = abs(angleDelta(targetDir - psDroid->rotation
                                                                   .direction));
					}

					if (dirDiff > FIXED_TURRET_DIR)
					{
						if (i > 0)
						{
							if (psDroid->psActionTarget[i] != psDroid->psActionTarget[0])
							{
								// Nope, can't shoot this, try something else next time
								setDroidActionTarget(psDroid, nullptr, i);
							}
						}
						else if (psDroid->sMove.Status != MOVESHUFFLE)
						{
							psDroid->action = DACTION_ROTATETOATTACK;
							moveTurnDroid(psDroid, psActionTarget->position.x, psActionTarget->position.y);
						}
					}
					else if (!psWeapStats->rotate ||
					         actionTargetTurret(psDroid, psActionTarget, &psDroid->m_weaponList
                                                            [i]))
					{
						/* In range - fire !!! */
						combFire(&psDroid->m_weaponList[i], psDroid, psActionTarget, i);
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
                  GameObject *psTarget;
			bool supportsSensorTower = !isVtolDroid(psDroid) && (psTarget = orderStateObj(psDroid, DORDER_FIRESUPPORT)) && psTarget->type == OBJ_STRUCTURE;

			if (secHoldActive && (order->type == DORDER_ATTACKTARGET || order->type == DORDER_FIRESUPPORT))
			{
				psDroid->action = DACTION_NONE; // secondary holding, cancel the order.
			}
			else if (secondaryGetState(psDroid, DSO_HALTTYPE) == DSS_HALT_PURSUE &&
				!supportsSensorTower &&
				!(order->type == DORDER_HOLD ||
				order->type == DORDER_RTR))
			{
				//We need this so pursuing doesn't stop if a unit is ordered to move somewhere while
				//it is still in weapon range of the target when reaching the end destination.
				//Weird case, I know, but keeps the previous pursue order intact.
				psDroid->action = DACTION_MOVETOATTACK;	// out of range - chase it
			}
			else if (supportsSensorTower ||
				order->type == DORDER_NONE ||
				order->type == DORDER_HOLD ||
				order->type == DORDER_RTR)
			{
				// don't move if on hold or firesupport for a sensor tower
				// also don't move if we're holding position or waiting for repair
				psDroid->action = DACTION_NONE; // holding, cancel the order.
			}
			//Units attached to commanders are always guarding the commander
			else if (secHoldActive && order->type == DORDER_GUARD && hasCommander(psDroid))
			{
                          Droid *commander = psDroid->psGroup->psCommander;

				if (commander->order.type == DORDER_ATTACKTARGET ||
					commander->order.type == DORDER_FIRESUPPORT ||
					commander->order.type == DORDER_ATTACK)
				{
					psDroid->action = DACTION_MOVETOATTACK;
				}
				else
				{
					psDroid->action = DACTION_NONE;
				}
			}
			else if (secondaryGetState(psDroid, DSO_HALTTYPE) != DSS_HALT_HOLD)
			{
				psDroid->action = DACTION_MOVETOATTACK;	// out of range - chase it
			}
			else
			{
				psDroid->order.psObj = nullptr;
				psDroid->action = DACTION_NONE;
			}
		}

		break;

	case DACTION_VTOLATTACK:
		{
			WEAPON_STATS *psWeapStats = nullptr;
			const bool targetIsValid = validTarget(psDroid, psDroid->psActionTarget[0], 0);
			//uses vtResult
			if (psDroid->psActionTarget[0] != nullptr &&
			    targetIsValid)
			{
				//check if vtol that its armed
				if ((vtolEmpty(psDroid)) ||
				    (psDroid->psActionTarget[0] == nullptr) ||
				    //check the target hasn't become one the same player ID - Electronic Warfare
				    (electronicDroid(psDroid) && (psDroid->owningPlayer == psDroid->psActionTarget[0]->owningPlayer)) ||
					// Huh? !targetIsValid can't be true, we just checked for it
				    !targetIsValid)
				{
					moveToRearm(psDroid);
					break;
				}

				for (unsigned i = 0; i < psDroid->numWeapons; ++i)
				{
					if (nonNullWeapon[i]
					    && validTarget(psDroid, psDroid->psActionTarget[0], i))
					{
						//I moved psWeapStats flag update there
						psWeapStats = &asWeaponStats[psDroid->m_weaponList[i].nStat];
						if (actionVisibleTarget(psDroid, psDroid->psActionTarget[0], i))
						{
							if (actionInRange(psDroid, psDroid->psActionTarget[0], i))
							{
								if (psDroid->owningPlayer == selectedPlayer)
								{
									audio_QueueTrackMinDelay(ID_SOUND_COMMENCING_ATTACK_RUN2,
									                         VTOL_ATTACK_AUDIO_DELAY);
								}

								if (actionTargetTurret(psDroid, psDroid->psActionTarget[0], &psDroid->m_weaponList
                                                                             [i]))
								{
									// In range - fire !!!
									combFire(&psDroid->m_weaponList
                                                                                     [i], psDroid,
									         psDroid->psActionTarget[0], i);
								}
							}
							else
							{
								actionTargetTurret(psDroid, psDroid->psActionTarget[0], &psDroid->m_weaponList
                                                                       [i]);
							}
						}
					}
				}
			}

			/* circle around target if hovering and not cyborg */
			Vector2i attackRunDelta = psDroid->position.xy() - psDroid->sMove.destination;
			if (DROID_STOPPED(psDroid) || dot(attackRunDelta, attackRunDelta) < TILE_UNITS * TILE_UNITS)
			{
				actionAddVtolAttackRun(psDroid);
			}
			else if(psDroid->psActionTarget[0] != nullptr &&
			    targetIsValid)
			{
				// if the vtol is close to the target, go around again
				Vector2i diff = (psDroid->position - psDroid->psActionTarget[0]->position).xy();
				const unsigned rangeSq = dot(diff, diff);
				if (rangeSq < VTOL_ATTACK_TARDIST * VTOL_ATTACK_TARDIST)
				{
					// don't do another attack run if already moving away from the target
					diff = psDroid->sMove.destination - psDroid->psActionTarget[0]->position.xy();
					if (dot(diff, diff) < VTOL_ATTACK_TARDIST * VTOL_ATTACK_TARDIST)
					{
						actionAddVtolAttackRun(psDroid);
					}
				}
				// in case psWeapStats is still NULL
				else if (psWeapStats)
				{
					// if the vtol is far enough away head for the target again
					const int maxRange = proj_GetLongRange(psWeapStats, psDroid->owningPlayer);
					if (rangeSq > maxRange * maxRange)
					{
						// don't do another attack run if already heading for the target
						diff = psDroid->sMove.destination - psDroid->psActionTarget[0]->position.xy();
						if (dot(diff, diff) > VTOL_ATTACK_TARDIST * VTOL_ATTACK_TARDIST)
						{
							moveDroidToDirect(psDroid, psDroid->psActionTarget[0]->position.x, psDroid->psActionTarget[0]->position.y);
						}
					}
				}
			}
			break;
		}
	case DACTION_MOVETOATTACK:
		// send vtols back to rearm
		if (isVtolDroid(psDroid) && vtolEmpty(psDroid))
		{
			moveToRearm(psDroid);
			break;
		}

		ASSERT_OR_RETURN(, psDroid->psActionTarget[0] != nullptr, "action update move to attack target is NULL");
		for (unsigned i = 0; i < psDroid->numWeapons; ++i)
		{
			hasValidWeapon |= validTarget(psDroid, psDroid->psActionTarget[0], i);
		}
		//check the target hasn't become one the same player ID - Electronic Warfare, and that the target is still valid.
		if ((electronicDroid(psDroid) && psDroid->owningPlayer == psDroid->psActionTarget[0]->owningPlayer) || !hasValidWeapon)
		{
			for (unsigned i = 0; i < psDroid->numWeapons; ++i)
			{
				setDroidActionTarget(psDroid, nullptr, i);
			}
			psDroid->action = DACTION_NONE;
		}
		else
		{
			if (actionVisibleTarget(psDroid, psDroid->psActionTarget[0], 0))
			{
				for (unsigned i = 0; i < psDroid->numWeapons; ++i)
				{
					if (nonNullWeapon[i]
					    && validTarget(psDroid, psDroid->psActionTarget[0], i)
					    && actionVisibleTarget(psDroid, psDroid->psActionTarget[0], i))
					{
						bool chaseBloke = false;
						WEAPON_STATS *const psWeapStats = &asWeaponStats[psDroid->m_weaponList
                                                                     [i].nStat];

						if (psWeapStats->rotate)
						{
							actionTargetTurret(psDroid, psDroid->psActionTarget[0], &psDroid->m_weaponList[i]);
						}

						if (!isVtolDroid(psDroid) &&
						    psDroid->psActionTarget[0]->type == OBJ_DROID &&
						    ((Droid *)psDroid->psActionTarget[0])->droidType == DROID_PERSON &&
						    psWeapStats->fireOnMove)
						{
							chaseBloke = true;
						}

						if (actionInRange(psDroid, psDroid->psActionTarget[0], i) && !chaseBloke)
						{
							/* init vtol attack runs count if necessary */
							if (psPropStats->propulsionType == PROPULSION_TYPE_LIFT)
							{
								psDroid->action = DACTION_VTOLATTACK;
							}
							else
							{
								if (actionInRange(psDroid, psDroid->psActionTarget[0], i, false))
								{
									moveStopDroid(psDroid);
								}

								if (psWeapStats->rotate)
								{
									psDroid->action = DACTION_ATTACK;
								}
								else
								{
									psDroid->action = DACTION_ROTATETOATTACK;
									moveTurnDroid(psDroid, psDroid->psActionTarget[0]->position
                                                                                .x, psDroid->psActionTarget[0]->position
                                                                                .y);
								}
							}
						}
						else if (actionInRange(psDroid, psDroid->psActionTarget[0], i))
						{
							// fire while closing range
							if ((blockingWall = visGetBlockingWall(psDroid, psDroid->psActionTarget[0])) && proj_Direct(psWeapStats))
							{
								WEAPON_EFFECT weapEffect = psWeapStats->weaponEffect;

								if (!aiCheckAlliances(psDroid->owningPlayer, blockingWall->owningPlayer)
									&& asStructStrengthModifier[weapEffect][blockingWall->stats
                                                                                 ->strength] >= MIN_STRUCTURE_BLOCK_STRENGTH)
								{
									//Shoot at wall if the weapon is good enough against them
									combFire(&psDroid->m_weaponList
                                                                                     [i], psDroid, (GameObject
                                                                                 *)blockingWall, i);
								}
							}
							else
							{
								combFire(&psDroid->m_weaponList
                                                                       [i], psDroid, psDroid->psActionTarget[0], i);
							}
						}
					}
				}
			}
			else
			{
				for (unsigned i = 0; i < psDroid->numWeapons; ++i)
				{
					if ((psDroid->m_weaponList[i].rot.direction != 0) ||
					    (psDroid->m_weaponList[i].rot.pitch != 0))
					{
						actionAlignTurret(psDroid, i);
					}
				}
			}

			if (DROID_STOPPED(psDroid) && psDroid->action != DACTION_ATTACK)
			{
				/* Stopped moving but haven't reached the target - possibly move again */

				//'hack' to make the droid to check the primary turrent instead of all
				WEAPON_STATS *const psWeapStats = &asWeaponStats[psDroid->m_weaponList[0].nStat];

				if (order->type == DORDER_ATTACKTARGET && secHoldActive)
				{
					psDroid->action = DACTION_NONE; // on hold, give up.
				}
				else if (actionInsideMinRange(psDroid, psDroid->psActionTarget[0], psWeapStats))
				{
					if (proj_Direct(psWeapStats) && order->type != DORDER_HOLD)
					{
						SDWORD pbx, pby;

						// try and extend the range
						actionCalcPullBackPoint(psDroid, psDroid->psActionTarget[0], &pbx, &pby);
						moveDroidTo(psDroid, (UDWORD)pbx, (UDWORD)pby);
					}
					else
					{
						if (psWeapStats->rotate)
						{
							psDroid->action = DACTION_ATTACK;
						}
						else
						{
							psDroid->action = DACTION_ROTATETOATTACK;
							moveTurnDroid(psDroid, psDroid->psActionTarget[0]->position.x,
							              psDroid->psActionTarget[0]->position.y);
						}
					}
				}
				else if (order->type != DORDER_HOLD) // approach closer?
				{
					// try to close the range
					moveDroidTo(psDroid, psDroid->psActionTarget[0]->position.x, psDroid->psActionTarget[0]->position.y);
				}
			}
		}
		break;

	case DACTION_SULK:
		// unable to route to target ... don't do anything aggressive until time is up
		// we need to do something defensive at this point ???

		//hmmm, hope this doesn't cause any problems!
		if (gameTime > psDroid->actionStarted)
		{
			psDroid->action = DACTION_NONE;			// Sulking is over lets get back to the action ... is this all I need to do to get it back into the action?
		}
		break;

	case DACTION_MOVETOBUILD:
		if (!order->psStats)
		{
			psDroid->action = DACTION_NONE;
			break;
		}
		else
		{
			// Determine if the droid can still build or help to build the ordered structure at the specified location
			const StructureStats * const desiredStructure = order->psStats;
			const Structure * const structureAtBuildPosition = getTileStructure(map_coord(psDroid->actionPos.x), map_coord(psDroid->actionPos.y));

			if (nullptr != structureAtBuildPosition)
			{
				bool droidCannotBuild = false;

				if (!aiCheckAlliances(structureAtBuildPosition->owningPlayer, psDroid->owningPlayer))
				{
					// Not our structure
					droidCannotBuild = true;
				}
				else
				// There's an allied structure already there.  Is it a wall, and can the droid upgrade it to a defence or gate?
				if (isWall(structureAtBuildPosition->stats
                                                   ->type) &&
					(desiredStructure->type == REF_DEFENSE || desiredStructure->type == REF_GATE))
				{
					// It's always valid to upgrade a wall to a defence or gate
					droidCannotBuild = false; // Just to avoid an empty branch
				}
				else
				if ((structureAtBuildPosition->stats != desiredStructure) && // ... it's not the exact same type as the droid was ordered to build
					(structureAtBuildPosition->stats
                                                    ->type == REF_WALLCORNER && desiredStructure->type != REF_WALL)) // and not a wall corner when the droid wants to build a wall
				{
					// And so the droid can't build or help with building this structure
					droidCannotBuild = true;
				}
				else
				// So it's a structure that the droid could help to build, but is it already complete?
				if (structureAtBuildPosition->status == SS_BUILT &&
					(!IsStatExpansionModule(desiredStructure) || !canStructureHaveAModuleAdded(structureAtBuildPosition)))
				{
					// The building is complete and the droid hasn't been told to add a module, or can't add one, so can't help with that.
					droidCannotBuild = true;
				}

				if (droidCannotBuild)
				{
					if (order->type == DORDER_LINEBUILD && map_coord(psDroid->order.pos) != map_coord(psDroid->order.pos2))
					{
						// The droid is doing a line build, and there's more to build. This will force the droid to move to the next structure in the line build
						objTrace(psDroid->id, "DACTION_MOVETOBUILD: line target is already built, or can't be built - moving to next structure in line");
						psDroid->action = DACTION_NONE;
					}
					else
					{
						// Cancel the current build order. This will move the truck onto the next order, if it has one, or halt in place.
						objTrace(psDroid->id, "DACTION_MOVETOBUILD: target is already built, or can't be built - executing next order or halting");
						cancelBuild(psDroid);
					}

					break;
				}
			}
		} // End of check for whether the droid can still succesfully build the ordered structure

		// The droid can still build or help with a build, and is moving to a location to do so - are we there yet, are we there yet, are we there yet?
		if (actionReachedBuildPos(psDroid, psDroid->actionPos.x, psDroid->actionPos.y, order->direction, order->psStats))
		{
			// We're there, go ahead and build or help to build the structure
			bool buildPosEmpty = actionRemoveDroidsFromBuildPos(psDroid->owningPlayer, psDroid->actionPos, order->direction, order->psStats);
			if (!buildPosEmpty)
			{
				break;
			}

			bool helpBuild = false;
			// Got to destination - start building
                        StructureStats *const psStructStats = order->psStats;
			uint16_t dir = order->direction;
			moveStopDroid(psDroid);
			objTrace(psDroid->id, "Halted in our tracks - at construction site");
			if (order->type == DORDER_BUILD && order->psObj == nullptr)
			{
				// Starting a new structure
				const Vector2i pos = psDroid->actionPos;

				//need to check if something has already started building here?
				//unless its a module!
				if (IsStatExpansionModule(psStructStats))
				{
					syncDebug("Reached build target: module");
					debug(LOG_NEVER, "DACTION_MOVETOBUILD: setUpBuildModule");
					setUpBuildModule(psDroid);
				}
				else if (TileHasStructure(worldTile(pos)))
				{
					// structure on the build location - see if it is the same type
                                        Structure *const psStruct = getTileStructure(map_coord(pos.x), map_coord(pos.y));
					if (psStruct->stats == order->psStats ||
					    (order->psStats->type == REF_WALL && psStruct->stats->type == REF_WALLCORNER))
					{
						// same type - do a help build
						syncDebug("Reached build target: do-help");
						setDroidTarget(psDroid, psStruct);
						helpBuild = true;
					}
					else if ((psStruct->stats->type == REF_WALL ||
					          psStruct->stats->type == REF_WALLCORNER) &&
					         (order->psStats->type == REF_DEFENSE ||
					          order->psStats->type == REF_GATE))
					{
						// building a gun tower or gate over a wall - OK
						if (droidStartBuild(psDroid))
						{
							syncDebug("Reached build target: tower");
							psDroid->action = DACTION_BUILD;
						}
					}
					else
					{
						syncDebug("Reached build target: already-structure");
						objTrace(psDroid->id, "DACTION_MOVETOBUILD: tile has structure already");
						cancelBuild(psDroid);
					}
				}
				else if (!validLocation(order->psStats, pos, dir, psDroid->owningPlayer, false))
				{
					syncDebug("Reached build target: invalid");
					objTrace(psDroid->id, "DACTION_MOVETOBUILD: !validLocation");
					cancelBuild(psDroid);
				}
				else if (droidStartBuild(psDroid) == DroidStartBuildSuccess)  // If DroidStartBuildPending, then there's a burning oil well, and we don't want to change to DACTION_BUILD until it stops burning.
				{
					syncDebug("Reached build target: build");
					psDroid->action = DACTION_BUILD;
					psDroid->actionStarted = gameTime;
					psDroid->actionPoints = 0;
				}
			}
			else if (order->type == DORDER_LINEBUILD || order->type == DORDER_BUILD)
			{
				// building a wall.
				MAPTILE *const psTile = worldTile(psDroid->actionPos);
				syncDebug("Reached build target: wall");
				if (order->psObj == nullptr
				    && (TileHasStructure(psTile)
				        || TileHasFeature(psTile)))
				{
					if (TileHasStructure(psTile))
					{
						// structure on the build location - see if it is the same type
                                                Structure *const psStruct = getTileStructure(map_coord(psDroid->actionPos.x), map_coord(psDroid->actionPos.y));
						ASSERT(psStruct, "TileHasStructure, but getTileStructure returned nullptr");
#if defined(WZ_CC_GNU) && !defined(WZ_CC_INTEL) && !defined(WZ_CC_CLANG) && (7 <= __GNUC__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wnull-dereference"
#endif
						if (psStruct->stats == order->psStats)
						{
							// same type - do a help build
							setDroidTarget(psDroid, psStruct);
							helpBuild = true;
						}
						else if ((psStruct->stats->type == REF_WALL || psStruct->stats->type == REF_WALLCORNER) &&
						         (order->psStats->type == REF_DEFENSE || order->psStats->type == REF_GATE))
						{
							// building a gun tower over a wall - OK
							if (droidStartBuild(psDroid))
							{
								objTrace(psDroid->id, "DACTION_MOVETOBUILD: start building defense");
								psDroid->action = DACTION_BUILD;
							}
						}
						else if ((psStruct->stats->type == REF_FACTORY && order->psStats->type == REF_FACTORY_MODULE) ||
								(psStruct->stats->type == REF_RESEARCH && order->psStats->type == REF_RESEARCH_MODULE) ||
								(psStruct->stats->type == REF_POWER_GEN && order->psStats->type == REF_POWER_MODULE) ||
								(psStruct->stats->type == REF_VTOL_FACTORY && order->psStats->type == REF_FACTORY_MODULE))
							{
							// upgrade current structure in a row
							if (droidStartBuild(psDroid))
							{
								objTrace(psDroid->id, "DACTION_MOVETOBUILD: start building module");
								psDroid->action = DACTION_BUILD;
							}
						}
						else
						{
							objTrace(psDroid->id, "DACTION_MOVETOBUILD: line build hit building");
							cancelBuild(psDroid);
						}
#if defined(WZ_CC_GNU) && !defined(WZ_CC_INTEL) && !defined(WZ_CC_CLANG) && (7 <= __GNUC__)
# pragma GCC diagnostic pop
#endif
					}
					else if (TileHasFeature(psTile))
					{
                                          Feature *feature = getTileFeature(map_coord(psDroid->actionPos.x), map_coord(psDroid->actionPos.y));
						objTrace(psDroid->id, "DACTION_MOVETOBUILD: tile has feature %d", feature->psStats->subType);
						if (feature->psStats->subType == FEAT_OIL_RESOURCE && order->psStats->type == REF_RESOURCE_EXTRACTOR)
						{
							if (droidStartBuild(psDroid))
							{
								objTrace(psDroid->id, "DACTION_MOVETOBUILD: start building oil derrick");
								psDroid->action = DACTION_BUILD;
							}
						}
					}
					else
					{
						objTrace(psDroid->id, "DACTION_MOVETOBUILD: blocked line build");
						cancelBuild(psDroid);
					}
				}
				else if (droidStartBuild(psDroid))
				{
					psDroid->action = DACTION_BUILD;
				}
			}
			else
			{
				syncDebug("Reached build target: planned-help");
				objTrace(psDroid->id, "DACTION_MOVETOBUILD: planned-help");
				helpBuild = true;
			}

			if (helpBuild)
			{
				// continuing a partially built structure (order = helpBuild)
				if (droidStartBuild(psDroid))
				{
					objTrace(psDroid->id, "DACTION_MOVETOBUILD: starting help build");
					psDroid->action = DACTION_BUILD;
				}
			}
		}
		else if (DROID_STOPPED(psDroid))
		{
			objTrace(psDroid->id, "DACTION_MOVETOBUILD: Starting to drive toward construction site - move status was %d", (int)psDroid->sMove.Status);
			moveDroidToNoFormation(psDroid, psDroid->actionPos.x, psDroid->actionPos.y);
		}
		break;
	case DACTION_BUILD:
		if (!order->psStats)
		{
			objTrace(psDroid->id, "No target stats for build order - resetting");
			psDroid->action = DACTION_NONE;
			break;
		}
		if (DROID_STOPPED(psDroid) &&
		    !actionReachedBuildPos(psDroid, psDroid->actionPos.x, psDroid->actionPos.y, order->direction, order->psStats))
		{
			objTrace(psDroid->id, "DACTION_BUILD: Starting to drive toward construction site");
			moveDroidToNoFormation(psDroid, psDroid->actionPos.x, psDroid->actionPos.y);
		}
		else if (!DROID_STOPPED(psDroid) &&
		         psDroid->sMove.Status != MOVETURNTOTARGET &&
		         psDroid->sMove.Status != MOVESHUFFLE &&
		         actionReachedBuildPos(psDroid, psDroid->actionPos.x, psDroid->actionPos.y, order->direction, order->psStats))
		{
			objTrace(psDroid->id, "DACTION_BUILD: Stopped - at construction site");
			moveStopDroid(psDroid);
		}
		if (psDroid->action == DACTION_SULK)
		{
			objTrace(psDroid->id, "Failed to go to objective, aborting build action");
			psDroid->action = DACTION_NONE;
			break;
		}
		if (droidUpdateBuild(psDroid))
		{
			actionTargetTurret(psDroid, psDroid->psActionTarget[0], &psDroid->m_weaponList[0]);
		}
		break;
	case DACTION_MOVETODEMOLISH:
	case DACTION_MOVETOREPAIR:
	case DACTION_MOVETORESTORE:
		if (!order->psStats)
		{
			psDroid->action = DACTION_NONE;
			break;
		}
		else
		{
			const Structure * structureAtPos = getTileStructure(map_coord(psDroid->actionPos.x), map_coord(psDroid->actionPos.y));

			if (structureAtPos == nullptr)
			{
				//No structure located at desired position. Move on.
				psDroid->action = DACTION_NONE;
				break;
			}
			else if (order->type != DORDER_RESTORE)
			{
				bool cantDoRepairLikeAction = false;

				if (!aiCheckAlliances(structureAtPos->owningPlayer, psDroid->owningPlayer))
				{
					cantDoRepairLikeAction = true;
				}
				else if (order->type != DORDER_DEMOLISH && structureAtPos->hitPoints == structureBody(structureAtPos))
				{
					cantDoRepairLikeAction = true;
				}
				else if (order->type == DORDER_DEMOLISH && structureAtPos->owningPlayer != psDroid->owningPlayer)
				{
					cantDoRepairLikeAction = true;
				}

				if (cantDoRepairLikeAction)
				{
					psDroid->action = DACTION_NONE;
					moveStopDroid(psDroid);
					break;
				}
			}
		}
		// see if the droid is at the edge of what it is moving to
		if (actionReachedBuildPos(psDroid, psDroid->actionPos.x, psDroid->actionPos.y, ((Structure *)psDroid->psActionTarget[0])->rotation.direction, order->psStats))
		{
			moveStopDroid(psDroid);

			// got to the edge - start doing whatever it was meant to do
			droidStartAction(psDroid);
			switch (psDroid->action)
			{
			case DACTION_MOVETODEMOLISH:
				psDroid->action = DACTION_DEMOLISH;
				break;
			case DACTION_MOVETOREPAIR:
				psDroid->action = DACTION_REPAIR;
				break;
			case DACTION_MOVETORESTORE:
				psDroid->action = DACTION_RESTORE;
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

	case DACTION_DEMOLISH:
	case DACTION_REPAIR:
	case DACTION_RESTORE:
		if (!order->psStats)
		{
			psDroid->action = DACTION_NONE;
			break;
		}
		// set up for the specific action
		switch (psDroid->action)
		{
		case DACTION_DEMOLISH:
			// DACTION_MOVETODEMOLISH;
			actionUpdateFunc = droidUpdateDemolishing;
			break;
		case DACTION_REPAIR:
			// DACTION_MOVETOREPAIR;
			actionUpdateFunc = droidUpdateRepair;
			break;
		case DACTION_RESTORE:
			// DACTION_MOVETORESTORE;
			actionUpdateFunc = droidUpdateRestore;
			break;
		default:
			break;
		}

		// now do the action update
		if (DROID_STOPPED(psDroid) && !actionReachedBuildPos(psDroid, psDroid->actionPos.x, psDroid->actionPos.y, ((Structure *)psDroid->psActionTarget[0])->rotation.direction, order->psStats))
		{
			if (order->type != DORDER_HOLD && (!secHoldActive || (secHoldActive && order->type != DORDER_NONE)))
			{
				objTrace(psDroid->id, "Secondary order: Go to construction site");
				moveDroidToNoFormation(psDroid, psDroid->actionPos.x, psDroid->actionPos.y);
			}
			else
			{
				psDroid->action = DACTION_NONE;
			}
		}
		else if (!DROID_STOPPED(psDroid) &&
		         psDroid->sMove.Status != MOVETURNTOTARGET &&
		         psDroid->sMove.Status != MOVESHUFFLE &&
		         actionReachedBuildPos(psDroid, psDroid->actionPos.x, psDroid->actionPos.y, ((Structure *)psDroid->psActionTarget[0])->rotation.direction, order->psStats))
		{
			objTrace(psDroid->id, "Stopped - reached build position");
			moveStopDroid(psDroid);
		}
		else if (actionUpdateFunc(psDroid))
		{
			//use 0 for non-combat(only 1 'weapon')
			actionTargetTurret(psDroid, psDroid->psActionTarget[0], &psDroid->m_weaponList[0]);
		}
		else
		{
			psDroid->action = DACTION_NONE;
		}
		break;

	case DACTION_MOVETOREARMPOINT:
		if (DROID_STOPPED(psDroid))
		{
			objTrace(psDroid->id, "Finished moving onto the rearm pad");
			psDroid->action = DACTION_WAITDURINGREARM;
		}
		break;
	case DACTION_MOVETOREPAIRPOINT:
		if (psDroid->order.rtrType == RTR_TYPE_REPAIR_FACILITY)
		{
			/* moving from front to rear of repair facility or rearm pad */
			if (actionReachedBuildPos(psDroid, psDroid->psActionTarget[0]->position.x, psDroid->psActionTarget[0]->position.y, ((Structure *)psDroid->psActionTarget[0])->rotation.direction, ((Structure *)psDroid->psActionTarget[0])->stats))
			{
				objTrace(psDroid->id, "Arrived at repair point - waiting for our turn");
				moveStopDroid(psDroid);
				psDroid->action = DACTION_WAITDURINGREPAIR;
			}
			else if (DROID_STOPPED(psDroid))
			{
				moveDroidToNoFormation(psDroid, psDroid->psActionTarget[0]->position.x,
									psDroid->psActionTarget[0]->position.y);
			}
		} else if (psDroid->order.rtrType == RTR_TYPE_DROID)
		{
			bool reached = actionReachedDroid(psDroid, (Droid *) psDroid->order.psObj);
			if (reached)
			{
				if (psDroid->hitPoints >= psDroid->originalBody)
				{
					objTrace(psDroid->id, "Repair not needed of droid %d", (int)psDroid->id);
					/* set droid points to max */
					psDroid->hitPoints = psDroid->originalBody;
					// if completely repaired then reset order
					secondarySetState(psDroid, DSO_RETURN_TO_LOC, DSS_NONE);
					orderDroidObj(psDroid, DORDER_GUARD, psDroid->order.psObj, ModeImmediate);
				}
				else
				{
					objTrace(psDroid->id, "Stopping and waiting for repairs %d", (int)psDroid->id);
					moveStopDroid(psDroid);
					psDroid->action = DACTION_WAITDURINGREPAIR;
				}

			}
			else if (DROID_STOPPED(psDroid))
			{
				//objTrace(psDroid->id, "Droid was stopped, but havent reach the target, moving now");
				//moveDroidToNoFormation(psDroid, psDroid->order.psObj->pos.x, psDroid->order.psObj->pos.y);
			}
		}
		break;
	case DACTION_OBSERVE:
		// align the turret
		actionTargetTurret(psDroid, psDroid->psActionTarget[0], &psDroid->m_weaponList[0]);

		if (!cbSensorDroid(psDroid))
		{
			// make sure the target is within sensor range
			const int xdiff = (SDWORD)psDroid->position.x - (SDWORD)psDroid->psActionTarget[0]->position.x;
			const int ydiff = (SDWORD)psDroid->position.y - (SDWORD)psDroid->psActionTarget[0]->position.y;
			int rangeSq = droidSensorRange(psDroid);
			rangeSq = rangeSq * rangeSq;
			if (!visibleObject(psDroid, psDroid->psActionTarget[0], false)
			    || xdiff * xdiff + ydiff * ydiff >= rangeSq)
			{
				if (secondaryGetState(psDroid, DSO_HALTTYPE) != DSS_HALT_GUARD && (order->type == DORDER_NONE || order->type == DORDER_HOLD))
				{
					psDroid->action = DACTION_NONE;
				}
				else if ((!secHoldActive && order->type != DORDER_HOLD) || (secHoldActive && order->type == DORDER_OBSERVE))
				{
					psDroid->action = DACTION_MOVETOOBSERVE;
					moveDroidTo(psDroid, psDroid->psActionTarget[0]->position.x, psDroid->psActionTarget[0]->position.y);
				}
			}
		}
		break;
	case DACTION_MOVETOOBSERVE:
		// align the turret
		actionTargetTurret(psDroid, psDroid->psActionTarget[0], &psDroid->m_weaponList[0]);

		if (visibleObject(psDroid, psDroid->psActionTarget[0], false))
		{
			// make sure the target is within sensor range
			const int xdiff = (SDWORD)psDroid->position.x - (SDWORD)psDroid->psActionTarget[0]->position.x;
			const int ydiff = (SDWORD)psDroid->position.y - (SDWORD)psDroid->psActionTarget[0]->position.y;
			int rangeSq = droidSensorRange(psDroid);
			rangeSq = rangeSq * rangeSq;
			if ((xdiff * xdiff + ydiff * ydiff < rangeSq) &&
			    !DROID_STOPPED(psDroid))
			{
				psDroid->action = DACTION_OBSERVE;
				moveStopDroid(psDroid);
			}
		}
		if (DROID_STOPPED(psDroid) && psDroid->action == DACTION_MOVETOOBSERVE)
		{
			moveDroidTo(psDroid, psDroid->psActionTarget[0]->position.x, psDroid->psActionTarget[0]->position.y);
		}
		break;
	case DACTION_FIRESUPPORT:
		if (!order->psObj)
		{
			psDroid->action = DACTION_NONE;
			return;
		}
		//can be either a droid or a structure now - AB 7/10/98
		ASSERT_OR_RETURN(, (order->psObj->type == OBJ_DROID || order->psObj->type == OBJ_STRUCTURE)
		                 && aiCheckAlliances(order->psObj->owningPlayer, psDroid->owningPlayer), "DACTION_FIRESUPPORT: incorrect target type");

		//don't move VTOL's
		// also don't move closer to sensor towers
		if (!isVtolDroid(psDroid) && order->psObj->type != OBJ_STRUCTURE)
		{
			Vector2i diff = (psDroid->position - order->psObj->position).xy();
			//Consider .shortRange here
			int rangeSq = asWeaponStats[psDroid->m_weaponList[0].nStat].upgrade[psDroid->owningPlayer].maxRange / 2; // move close to sensor
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
					diff = order->psObj->position.xy() - psDroid->sMove.destination;
				}
				if (DROID_STOPPED(psDroid) || dot(diff, diff) > rangeSq)
				{
					if (secHoldActive)
					{
						// droid on hold, don't allow moves.
						psDroid->action = DACTION_NONE;
					}
					else
					{
						// move in range
						moveDroidTo(psDroid, order->psObj->position.x,order->psObj->position.y);
					}
				}
			}
		}
		break;
	case DACTION_MOVETODROIDREPAIR:
        {
          auto actionTarget = psDroid->psActionTarget[0];
          assert(actionTarget->type() == OBJ_DROID)

          if (actionTarget.hitPoints == actionTarget.originalBody)
          {
                  // target is healthy: nothing to do
                  psDroid->action = DACTION_NONE;
                  moveStopDroid(psDroid);
                  break;
          }
          Vector2i diff = (psDroid->position - psDroid->psActionTarget[0]->position).xy();
          // moving to repair a droid
          if (!psDroid->psActionTarget[0] ||  // Target missing.
                  (psDroid->order.type != DORDER_DROIDREPAIR && dot(diff, diff) > 2 * REPAIR_MAXDIST * REPAIR_MAXDIST))  // Target farther then 1.4142 * REPAIR_MAXDIST and we aren't ordered to follow.
          {
                  psDroid->action = DACTION_NONE;
                  return;
          }
          if (dot(diff, diff) < REPAIR_RANGE * REPAIR_RANGE)
          {
                  // Got to destination - start repair
                  //rotate turret to point at droid being repaired
                  //use 0 for repair droid
                  actionTargetTurret(psDroid, psDroid->psActionTarget[0], &psDroid->m_weaponList[0]);
                  droidStartAction(psDroid);
                  psDroid->action = DACTION_DROIDREPAIR;
          }
          if (DROID_STOPPED(psDroid))
          {
                  // Couldn't reach destination - try and find a new one
                  psDroid->actionPos = psDroid->psActionTarget[0]->position.xy();
                  moveDroidTo(psDroid, psDroid->actionPos.x, psDroid->actionPos.y);
          }
          break;
        }
	case DACTION_DROIDREPAIR:
		{
			int xdiff, ydiff;

			// If not doing self-repair (psActionTarget[0] is repair target)
			if (psDroid->psActionTarget[0] != psDroid)
			{
				actionTargetTurret(psDroid, psDroid->psActionTarget[0], &psDroid->m_weaponList[0]);
			}
			// Just self-repairing.
			// See if there's anything to shoot.
			else if (psDroid->numWeapons > 0 && !isVtolDroid(psDroid)
			         && (order->type == DORDER_NONE || order->type == DORDER_HOLD || order->type == DORDER_RTR))
			{
				for (unsigned i = 0; i < psDroid->numWeapons; ++i)
				{
					if (nonNullWeapon[i])
					{
                                          GameObject *psTemp = nullptr;

						WEAPON_STATS *const psWeapStats = &asWeaponStats[psDroid->m_weaponList[i].nStat];
						if (psDroid->m_weaponList[i].nStat > 0 && psWeapStats->rotate
						    && secondaryGetState(psDroid, DSO_ATTACK_LEVEL) == DSS_ALEV_ALWAYS
						    && aiBestNearestTarget(psDroid, &psTemp, i) >= 0 && psTemp)
						{
							psDroid->action = DACTION_ATTACK;
							setDroidActionTarget(psDroid, psTemp, 0);
							break;
						}
					}
				}
			}
			if (psDroid->action != DACTION_DROIDREPAIR)
			{
				break;	// action has changed
			}

			//check still next to the damaged droid
			xdiff = (SDWORD)psDroid->position.x - (SDWORD)psDroid->psActionTarget[0]->position.x;
			ydiff = (SDWORD)psDroid->position.y - (SDWORD)psDroid->psActionTarget[0]->position.y;
			if (xdiff * xdiff + ydiff * ydiff > REPAIR_RANGE * REPAIR_RANGE)
			{
				if (order->type == DORDER_DROIDREPAIR)
				{
					// damaged droid has moved off - follow if we're not holding position!
					psDroid->actionPos = psDroid->psActionTarget[0]->position.xy();
					psDroid->action = DACTION_MOVETODROIDREPAIR;
					moveDroidTo(psDroid, psDroid->actionPos.x, psDroid->actionPos.y);
				}
				else
				{
					psDroid->action = DACTION_NONE;
				}
			}
			else
			{
				if (!droidUpdateDroidRepair(psDroid))
				{
					psDroid->action = DACTION_NONE;
					moveStopDroid(psDroid);
					//if the order is RTR then resubmit order so that the unit will go to repair facility point
					if (orderState(psDroid, DORDER_RTR))
					{
						orderDroid(psDroid, DORDER_RTR, ModeImmediate);
					}
				}
				else
				{
					// don't let the target for a repair shuffle
					if (((Droid *)psDroid->psActionTarget[0])->sMove.Status == MOVESHUFFLE)
					{
						moveStopDroid((Droid *)psDroid->psActionTarget[0]);
					}
				}
			}
			break;
		}
	case DACTION_WAITFORREARM:
		// wait here for the rearm pad to instruct the vtol to move
		if (psDroid->psActionTarget[0] == nullptr)
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
			psDroid->action = DACTION_NONE;
		}
		break;
	case DACTION_CLEARREARMPAD:
		if (DROID_STOPPED(psDroid))
		{
			psDroid->action = DACTION_NONE;
			objTrace(psDroid->id, "clearing rearm pad");
			if (!vtolHappy(psDroid))  // Droid has cleared the rearm pad without getting rearmed. One way this can happen if a rearming pad was built under the VTOL while it was waiting for a pad.
			{
				moveToRearm(psDroid);  // Rearm somewhere else instead.
			}
		}
		break;
	case DACTION_WAITDURINGREARM:
		// this gets cleared by the rearm pad
		break;
	case DACTION_MOVETOREARM:
		if (psDroid->psActionTarget[0] == nullptr)
		{
			// base destroyed - find another
			objTrace(psDroid->id, "rearm gone - find another");
			moveToRearm(psDroid);
			break;
		}

		if (visibleObject(psDroid, psDroid->psActionTarget[0], false))
		{
                  Structure *const psStruct = findNearestReArmPad(psDroid, (Structure *)psDroid->psActionTarget[0], true);
			// got close to the rearm pad - now find a clear one
			objTrace(psDroid->id, "Seen rearm pad - searching for available one");

			if (psStruct != nullptr)
			{
				// found a clear landing pad - go for it
				objTrace(psDroid->id, "Found clear rearm pad");
				setDroidActionTarget(psDroid, psStruct, 0);
			}

			psDroid->action = DACTION_WAITFORREARM;
		}

		if (DROID_STOPPED(psDroid) || psDroid->action == DACTION_WAITFORREARM)
		{
			Vector2i pos = psDroid->psActionTarget[0]->position.xy();
			if (!actionVTOLLandingPos(psDroid, &pos))
			{
				// totally bunged up - give up
				objTrace(psDroid->id, "Couldn't find a clear tile near rearm pad - returning to base");
				orderDroid(psDroid, DORDER_RTB, ModeImmediate);
				break;
			}
			objTrace(psDroid->id, "moving to rearm pad at %d,%d (%d,%d)", (int)pos.x, (int)pos.y, (int)(pos.x/TILE_UNITS), (int)(pos.y/TILE_UNITS));
			moveDroidToDirect(psDroid, pos.x, pos.y);
		}
		break;
	default:
		ASSERT(!"unknown action", "unknown action");
		break;
	}

	if (psDroid->action != DACTION_MOVEFIRE &&
	    psDroid->action != DACTION_ATTACK &&
	    psDroid->action != DACTION_MOVETOATTACK &&
	    psDroid->action != DACTION_MOVETODROIDREPAIR &&
	    psDroid->action != DACTION_DROIDREPAIR &&
	    psDroid->action != DACTION_BUILD &&
	    psDroid->action != DACTION_OBSERVE &&
	    psDroid->action != DACTION_MOVETOOBSERVE)
	{
		//use 0 for all non-combat droid types
		if (psDroid->numWeapons == 0)
		{
			if (psDroid->m_weaponList[0].rot.direction != 0 || psDroid->m_weaponList[0].rot.pitch != 0)
			{
				actionAlignTurret(psDroid, 0);
			}
		}
		else
		{
			for (unsigned i = 0; i < psDroid->numWeapons; ++i)
			{
				if (psDroid->m_weaponList[i].rot.direction != 0 || psDroid->m_weaponList[i].rot.pitch != 0)
				{
					actionAlignTurret(psDroid, i);
				}
			}
		}
	}
	CHECK_DROID(psDroid);
}

/* Overall action function that is called by the specific action functions */
static void actionDroidBase(Droid *psDroid, DROID_ACTION_DATA *psAction)
{
	ASSERT_OR_RETURN(, psAction->targetObj == nullptr || !psAction->targetObj->deathTime, "Droid dead");

	WEAPON_STATS *psWeapStats = getWeaponStats(psDroid, 0);
	Vector2i pos(0, 0);

	CHECK_DROID(psDroid);

	bool secHoldActive = secondaryGetState(psDroid, DSO_HALTTYPE) == DSS_HALT_HOLD;
	psDroid->actionStarted = gameTime;
	syncDebugDroid(psDroid, '-');
	syncDebug("%d does %s", psDroid->id, getDroidActionName(psAction->action));
	objTrace(psDroid->id, "base set action to %s (was %s)", getDroidActionName(psAction->action), getDroidActionName(psDroid->action));

	DROID_ORDER_DATA *order = &psDroid->order;
	bool hasValidWeapon = false;
	for (int i = 0; i < MAX_WEAPONS; i++)
	{
		hasValidWeapon |= validTarget(psDroid, psAction->targetObj, i);
	}
	switch (psAction->action)
	{
	case DACTION_NONE:
		// Clear up what ever the droid was doing before if necessary
		if (!DROID_STOPPED(psDroid))
		{
			moveStopDroid(psDroid);
		}
		psDroid->action = DACTION_NONE;
		psDroid->actionPos = Vector2i(0, 0);
		psDroid->actionStarted = 0;
		psDroid->actionPoints = 0;
		if (psDroid->numWeapons > 0)
		{
			for (int i = 0; i < psDroid->numWeapons; i++)
			{
				setDroidActionTarget(psDroid, nullptr, i);
			}
		}
		else
		{
			setDroidActionTarget(psDroid, nullptr, 0);
		}
		break;

	case DACTION_TRANSPORTWAITTOFLYIN:
		psDroid->action = DACTION_TRANSPORTWAITTOFLYIN;
		break;

	case DACTION_ATTACK:
		if (psDroid->m_weaponList[0].nStat == 0 || isTransporter(psDroid) || psAction->targetObj == psDroid)
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
			if (psAction->targetObj->type == OBJ_STRUCTURE
			    && !validStructResistance((Structure *)psAction->targetObj))
			{
				//structure is low resistance already so don't attack
				psDroid->action = DACTION_NONE;
				break;
			}

			//in multiPlayer cannot electronically attack a transporter
			if (bMultiPlayer
			    && psAction->targetObj->type == OBJ_DROID
			    && isTransporter((Droid *)psAction->targetObj))
			{
				psDroid->action = DACTION_NONE;
				break;
			}
		}

		// note the droid's current pos so that scout & patrol orders know how far the
		// droid has gone during an attack
		// slightly strange place to store this I know, but I didn't want to add any more to the droid
		psDroid->actionPos = psDroid->position.xy();
		setDroidActionTarget(psDroid, psAction->targetObj, 0);

		if (((order->type == DORDER_ATTACKTARGET
		   || order->type == DORDER_NONE
		   || order->type == DORDER_HOLD
		   || (order->type == DORDER_GUARD && hasCommander(psDroid))
		   || order->type == DORDER_FIRESUPPORT)
		   && secHoldActive)
		   || (!isVtolDroid(psDroid) && (orderStateObj(psDroid, DORDER_FIRESUPPORT) != nullptr)))
		{
			psDroid->action = DACTION_ATTACK;		// holding, try attack straightaway
		}
		else if (actionInsideMinRange(psDroid, psAction->targetObj, psWeapStats)) // too close?
		{
			if (!proj_Direct(psWeapStats))
			{
				if (psWeapStats->rotate)
				{
					psDroid->action = DACTION_ATTACK;
				}
				else
				{
					psDroid->action = DACTION_ROTATETOATTACK;
					moveTurnDroid(psDroid, psDroid->psActionTarget[0]->position.x, psDroid->psActionTarget[0]->position.y);
				}
			}
			else if (order->type != DORDER_HOLD && secondaryGetState(psDroid, DSO_HALTTYPE) != DSS_HALT_HOLD)
			{
				int pbx = 0;
				int pby = 0;
				/* direct fire - try and extend the range */
				psDroid->action = DACTION_MOVETOATTACK;
				actionCalcPullBackPoint(psDroid, psAction->targetObj, &pbx, &pby);

				turnOffMultiMsg(true);
				moveDroidTo(psDroid, (UDWORD)pbx, (UDWORD)pby);
				turnOffMultiMsg(false);
			}
		}
		else if (order->type != DORDER_HOLD && secondaryGetState(psDroid, DSO_HALTTYPE) != DSS_HALT_HOLD) // approach closer?
		{
			psDroid->action = DACTION_MOVETOATTACK;
			turnOffMultiMsg(true);
			moveDroidTo(psDroid, psAction->targetObj->position.x, psAction->targetObj->position.y);
			turnOffMultiMsg(false);
		}
		else if (order->type != DORDER_HOLD && secondaryGetState(psDroid, DSO_HALTTYPE) == DSS_HALT_HOLD)
		{
			psDroid->action = DACTION_ATTACK;
		}
		break;

	case DACTION_MOVETOREARM:
		psDroid->action = DACTION_MOVETOREARM;
		psDroid->actionPos = psAction->targetObj->position.xy();
		psDroid->actionStarted = gameTime;
		setDroidActionTarget(psDroid, psAction->targetObj, 0);
		pos = psDroid->psActionTarget[0]->position.xy();
		if (!actionVTOLLandingPos(psDroid, &pos))
		{
			// totally bunged up - give up
			objTrace(psDroid->id, "move to rearm action failed!");
			orderDroid(psDroid, DORDER_RTB, ModeImmediate);
			break;
		}
		objTrace(psDroid->id, "move to rearm");
		moveDroidToDirect(psDroid, pos.x, pos.y);
		break;
	case DACTION_CLEARREARMPAD:
		debug(LOG_NEVER, "Unit %d clearing rearm pad", psDroid->id);
		psDroid->action = DACTION_CLEARREARMPAD;
		setDroidActionTarget(psDroid, psAction->targetObj, 0);
		pos = psDroid->psActionTarget[0]->position.xy();
		if (!actionVTOLLandingPos(psDroid, &pos))
		{
			// totally bunged up - give up
			objTrace(psDroid->id, "clear rearm pad action failed!");
			orderDroid(psDroid, DORDER_RTB, ModeImmediate);
			break;
		}
		objTrace(psDroid->id, "move to clear rearm pad");
		moveDroidToDirect(psDroid, pos.x, pos.y);
		break;
	case DACTION_MOVE:
	case DACTION_TRANSPORTIN:
	case DACTION_TRANSPORTOUT:
	case DACTION_RETURNTOPOS:
	case DACTION_FIRESUPPORT_RETREAT:
		psDroid->action = psAction->action;
		psDroid->actionPos.x = psAction->x;
		psDroid->actionPos.y = psAction->y;
		psDroid->actionStarted = gameTime;
		setDroidActionTarget(psDroid, psAction->targetObj, 0);
		moveDroidTo(psDroid, psAction->x, psAction->y);
		break;

	case DACTION_BUILD:
		if (!order->psStats)
		{
			psDroid->action = DACTION_NONE;
			break;
		}
		//ASSERT_OR_RETURN(, order->type == DORDER_BUILD || order->type == DORDER_HELPBUILD || order->type == DORDER_LINEBUILD, "cannot start build action without a build order");
		ASSERT_OR_RETURN(, psAction->x > 0 && psAction->y > 0, "Bad build order position");
		psDroid->action = DACTION_MOVETOBUILD;
		psDroid->actionPos.x = psAction->x;
		psDroid->actionPos.y = psAction->y;
		moveDroidToNoFormation(psDroid, psDroid->actionPos.x, psDroid->actionPos.y);
		break;
	case DACTION_DEMOLISH:
		ASSERT_OR_RETURN(, order->type == DORDER_DEMOLISH, "cannot start demolish action without a demolish order");
		psDroid->action = DACTION_MOVETODEMOLISH;
		psDroid->actionPos.x = psAction->x;
		psDroid->actionPos.y = psAction->y;
		ASSERT_OR_RETURN(, (order->psObj != nullptr) && (order->psObj->type == OBJ_STRUCTURE), "invalid target for demolish order");
		order->psStats = ((Structure *)order->psObj)->stats;
		setDroidActionTarget(psDroid, psAction->targetObj, 0);
		moveDroidTo(psDroid, psAction->x, psAction->y);
		break;
	case DACTION_REPAIR:
		psDroid->action = psAction->action;
		psDroid->actionPos.x = psAction->x;
		psDroid->actionPos.y = psAction->y;
		//this needs setting so that automatic repair works
		setDroidActionTarget(psDroid, psAction->targetObj, 0);
		ASSERT_OR_RETURN(, (psDroid->psActionTarget[0] != nullptr) && (psDroid->psActionTarget[0]->type == OBJ_STRUCTURE),
		       "invalid target for repair order");
		order->psStats = ((Structure *)psDroid->psActionTarget[0])->stats;
		if (secHoldActive && (order->type == DORDER_NONE || order->type == DORDER_HOLD))
		{
			psDroid->action = DACTION_REPAIR;
		}
		else if ((!secHoldActive && order->type != DORDER_HOLD) || (secHoldActive && order->type == DORDER_REPAIR))
		{
			psDroid->action = DACTION_MOVETOREPAIR;
			moveDroidTo(psDroid, psAction->x, psAction->y);
		}
		break;
	case DACTION_OBSERVE:
		psDroid->action = psAction->action;
		setDroidActionTarget(psDroid, psAction->targetObj, 0);
		psDroid->actionPos.x = psDroid->position.x;
		psDroid->actionPos.y = psDroid->position.y;
		if (secondaryGetState(psDroid, DSO_HALTTYPE) != DSS_HALT_GUARD && (order->type == DORDER_NONE || order->type == DORDER_HOLD))
		{
			psDroid->action = visibleObject(psDroid, psDroid->psActionTarget[0], false) ? DACTION_OBSERVE : DACTION_NONE;
		}
		else if (!cbSensorDroid(psDroid) && ((!secHoldActive && order->type != DORDER_HOLD) || (secHoldActive && order->type == DORDER_OBSERVE)))
		{
			psDroid->action = DACTION_MOVETOOBSERVE;
			moveDroidTo(psDroid, psDroid->psActionTarget[0]->position.x, psDroid->psActionTarget[0]->position.y);
		}
		break;
	case DACTION_FIRESUPPORT:
		psDroid->action = DACTION_FIRESUPPORT;
		if (!isVtolDroid(psDroid) && !secHoldActive && order->psObj->type != OBJ_STRUCTURE)
		{
			moveDroidTo(psDroid, order->psObj->position.x, order->psObj->position.y);		// movetotarget.
		}
		break;
	case DACTION_SULK:
		psDroid->action = DACTION_SULK;
		// hmmm, hope this doesn't cause any problems!
		psDroid->actionStarted = gameTime + MIN_SULK_TIME + (gameRand(MAX_SULK_TIME - MIN_SULK_TIME));
		break;
	case DACTION_WAITFORREPAIR:
		psDroid->action = DACTION_WAITFORREPAIR;
		// set the time so we can tell whether the start the self repair or not
		psDroid->actionStarted = gameTime;
		break;
	case DACTION_MOVETOREPAIRPOINT:
		psDroid->action = psAction->action;
		psDroid->actionPos.x = psAction->x;
		psDroid->actionPos.y = psAction->y;
		psDroid->actionStarted = gameTime;
		setDroidActionTarget(psDroid, psAction->targetObj, 0);
		moveDroidToNoFormation(psDroid, psAction->x, psAction->y);
		break;
	case DACTION_WAITDURINGREPAIR:
		psDroid->action = DACTION_WAITDURINGREPAIR;
		break;
	case DACTION_MOVETOREARMPOINT:
		objTrace(psDroid->id, "set to move to rearm pad");
		psDroid->action = psAction->action;
		psDroid->actionPos.x = psAction->x;
		psDroid->actionPos.y = psAction->y;
		psDroid->actionStarted = gameTime;
		setDroidActionTarget(psDroid, psAction->targetObj, 0);
		moveDroidToDirect(psDroid, psAction->x, psAction->y);

		// make sure there aren't any other VTOLs on the rearm pad
		ensureRearmPadClear((Structure *)psAction->targetObj, psDroid);
		break;
	case DACTION_DROIDREPAIR:
		{
			psDroid->action = psAction->action;
			psDroid->actionPos.x = psAction->x;
			psDroid->actionPos.y = psAction->y;
			setDroidActionTarget(psDroid, psAction->targetObj, 0);
			//initialise the action points
			psDroid->actionPoints  = 0;
			psDroid->actionStarted = gameTime;
			const auto xdiff = (SDWORD)psDroid->position.x - (SDWORD) psAction->x;
			const auto ydiff = (SDWORD)psDroid->position.y - (SDWORD) psAction->y;
			if (secHoldActive && (order->type == DORDER_NONE || order->type == DORDER_HOLD))
			{
				psDroid->action = DACTION_DROIDREPAIR;
			}
			else if (((!secHoldActive && order->type != DORDER_HOLD) || (secHoldActive && order->type == DORDER_DROIDREPAIR))
					// check that we actually need to move closer
					&& ((xdiff * xdiff + ydiff * ydiff) > REPAIR_RANGE * REPAIR_RANGE))
			{
				psDroid->action = DACTION_MOVETODROIDREPAIR;
				moveDroidTo(psDroid, psAction->x, psAction->y);
			}
			break;
		}
	case DACTION_RESTORE:
		ASSERT_OR_RETURN(, order->type == DORDER_RESTORE, "cannot start restore action without a restore order");
		psDroid->action = psAction->action;
		psDroid->actionPos.x = psAction->x;
		psDroid->actionPos.y = psAction->y;
		ASSERT_OR_RETURN(, (order->psObj != nullptr) && (order->psObj->type == OBJ_STRUCTURE), "invalid target for restore order");
		order->psStats = ((Structure *)order->psObj)->stats;
		setDroidActionTarget(psDroid, psAction->targetObj, 0);
		if (order->type != DORDER_HOLD)
		{
			psDroid->action = DACTION_MOVETORESTORE;
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
void actionDroid(Droid *psDroid, DROID_ACTION action)
{
	DROID_ACTION_DATA	sAction;

	memset(&sAction, 0, sizeof(DROID_ACTION_DATA));
	sAction.action = action;
	actionDroidBase(psDroid, &sAction);
}

/* Give a droid an action with a location target */
void actionDroid(Droid *psDroid, DROID_ACTION action, UDWORD x, UDWORD y)
{
	DROID_ACTION_DATA	sAction;

	memset(&sAction, 0, sizeof(DROID_ACTION_DATA));
	sAction.action = action;
	sAction.x = x;
	sAction.y = y;
	actionDroidBase(psDroid, &sAction);
}

/* Give a droid an action with an object target */
void actionDroid(Droid *psDroid, DROID_ACTION action, GameObject *psObj)
{
	DROID_ACTION_DATA	sAction;

	memset(&sAction, 0, sizeof(DROID_ACTION_DATA));
	sAction.action = action;
	sAction.targetObj = psObj;
	sAction.x = psObj->position.x;
	sAction.y = psObj->position.y;
	actionDroidBase(psDroid, &sAction);
}

/* Give a droid an action with an object target and a location */
void actionDroid(Droid *psDroid, DROID_ACTION action, GameObject *psObj, UDWORD x, UDWORD y)
{
	DROID_ACTION_DATA	sAction;

	memset(&sAction, 0, sizeof(DROID_ACTION_DATA));
	sAction.action = action;
	sAction.targetObj = psObj;
	sAction.x = x;
	sAction.y = y;
	actionDroidBase(psDroid, &sAction);
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