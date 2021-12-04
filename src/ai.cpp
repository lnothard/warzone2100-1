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
 * @file ai.c
 *
 * AI update functions for the different object types.
 *
 */

#include "lib/framework/frame.h"

#include "action.h"
#include "cmddroid.h"
#include "combat.h"
#include "droid.h"
#include "group.h"
#include "mapgrid.h"
#include "map.h"
#include "projectile.h"
#include "objmem.h"
#include "order.h"

/* Weights used for target selection code,
 * target distance is used as 'common currency'
 */
#define	WEIGHT_DIST_TILE			13						//In points used in weaponmodifier.txt and structuremodifier.txt
#define	WEIGHT_DIST_TILE_DROID		WEIGHT_DIST_TILE		//How much weight a distance of 1 tile (128 world units) has when looking for the best nearest target
#define	WEIGHT_DIST_TILE_STRUCT		WEIGHT_DIST_TILE
#define	WEIGHT_HEALTH_DROID			(WEIGHT_DIST_TILE * 10)	//How much weight unit damage has (100% of damage is equally weighted as 10 tiles distance)
//~100% damage should be ~8 tiles (max sensor range)
#define	WEIGHT_HEALTH_STRUCT		(WEIGHT_DIST_TILE * 7)

#define	WEIGHT_NOT_VISIBLE_F		10						//We really don't like objects we can't see

#define	WEIGHT_SERVICE_DROIDS		(WEIGHT_DIST_TILE_DROID * 5)		//We don't want them to be repairing droids or structures while we are after them
#define	WEIGHT_WEAPON_DROIDS		(WEIGHT_DIST_TILE_DROID * 4)		//We prefer to go after anything that has a gun and can hurt us
#define	WEIGHT_COMMAND_DROIDS		(WEIGHT_DIST_TILE_DROID * 6)		//Commanders get a higher priority
#define	WEIGHT_MILITARY_STRUCT		WEIGHT_DIST_TILE_STRUCT				//Droid/cyborg factories, repair facility; shouldn't have too much weight
#define	WEIGHT_WEAPON_STRUCT		WEIGHT_WEAPON_DROIDS				//Same as weapon droids (?)
#define	WEIGHT_DERRICK_STRUCT		(WEIGHT_MILITARY_STRUCT + WEIGHT_DIST_TILE_STRUCT * 4)	//Even if it's 4 tiles further away than defenses we still choose it

#define	WEIGHT_STRUCT_NOTBUILT_F	8						//Humans won't fool us anymore!

#define OLD_TARGET_THRESHOLD		(WEIGHT_DIST_TILE * 4)	//it only makes sense to switch target if new one is 4+ tiles closer

#define	EMP_DISABLED_PENALTY_F		10								//EMP shouldn't attack emped targets again
#define	EMP_STRUCT_PENALTY_F		(EMP_DISABLED_PENALTY_F * 2)	//EMP don't attack structures, should be bigger than EMP_DISABLED_PENALTY_F

#define TOO_CLOSE_PENALTY_F             20

#define TARGET_DOOMED_PENALTY_F		10	// Targets that have a lot of damage incoming are less attractive
#define TARGET_DOOMED_SLOW_RELOAD_T	21	// Weapon ROF threshold for above penalty. per minute.

//Some weights for the units attached to a commander
#define	WEIGHT_CMD_RANK				(WEIGHT_DIST_TILE * 4)			//A single rank is as important as 4 tiles distance
#define	WEIGHT_CMD_SAME_TARGET		WEIGHT_DIST_TILE				//Don't want this to be too high, since a commander can have many units assigned

uint8_t alliances[MAX_PLAYER_SLOTS][MAX_PLAYER_SLOTS];

/// A bitfield of vision sharing in alliances, for quick manipulation of vision information
PlayerMask alliancebits[MAX_PLAYER_SLOTS];

/// A bitfield for the satellite uplink
PlayerMask satuplinkbits;

static int aiDroidRange(Droid *psDroid, int weapon_slot)
{
	int32_t longRange;
	if (psDroid->droidType == DROID_SENSOR)
	{
		longRange = objSensorRange(psDroid);
	}
	else if (psDroid->numWeapons == 0 || psDroid->m_weaponList[0].nStat == 0)
	{
		// Can't attack without a weapon
		return 0;
	}
	else
	{
		WEAPON_STATS *psWStats = psDroid->m_weaponList[weapon_slot].nStat + asWeaponStats;
		longRange = proj_GetLongRange(psWStats, psDroid->owningPlayer);
	}

	return longRange;
}

/* Initialise the AI system */
bool aiInitialise()
{
	SDWORD		i, j;

	for (i = 0; i < MAX_PLAYER_SLOTS; i++)
	{
		alliancebits[i] = 0;
		for (j = 0; j < MAX_PLAYER_SLOTS; j++)
		{
			bool valid = (i == j && i < MAX_PLAYERS);

			alliances[i][j] = valid ? ALLIANCE_FORMED : ALLIANCE_BROKEN;
			alliancebits[i] |= valid << j;
		}
	}
	satuplinkbits = 0;

	return true;
}

/* Shutdown the AI system */
bool aiShutdown()
{
	return true;
}

/** Search the global list of sensors for a possible target for psObj. */
static GameObject *aiSearchSensorTargets(GameObject *psObj, int weapon_slot, WEAPON_STATS *psWStats, TARGET_ORIGIN *targetOrigin)
{
	int		longRange = proj_GetLongRange(psWStats, psObj->owningPlayer);
	int		tarDist = longRange * longRange;
	bool		foundCB = false;
	int		minDist = proj_GetMinRange(psWStats, psObj->owningPlayer) * proj_GetMinRange(psWStats, psObj->owningPlayer);
        GameObject *psTarget = nullptr;

	if (targetOrigin)
	{
		*targetOrigin = ORIGIN_UNKNOWN;
	}

	for (GameObject *psSensor = apsSensorList[0]; psSensor; psSensor = psSensor->psNextFunc)
	{
          GameObject *psTemp = nullptr;
		bool		isCB = false;
		//bool		isRD = false;

		if (!aiCheckAlliances(psSensor->owningPlayer, psObj->owningPlayer))
		{
			continue;
		}
		else if (psSensor->type == OBJ_DROID)
		{
                  Droid *psDroid = (Droid *)psSensor;

			ASSERT_OR_RETURN(nullptr, psDroid->droidType == DROID_SENSOR, "A non-sensor droid in a sensor list is non-sense");
			// Skip non-observing droids. This includes Radar Detectors at the moment since they never observe anything.
			if (psDroid->action != DACTION_OBSERVE)
			{
				continue;
			}
			// Artillery should not fire at objects observed by VTOL CB/Strike sensors.
			if (asSensorStats[psDroid->asBits[COMP_SENSOR]].type == VTOL_CB_SENSOR ||
				asSensorStats[psDroid->asBits[COMP_SENSOR]].type == VTOL_INTERCEPT_SENSOR ||
				objRadarDetector((GameObject *)psDroid))
			{
				continue;
			}
			psTemp = psDroid->psActionTarget[0];
			isCB = asSensorStats[psDroid->asBits[COMP_SENSOR]].type == INDIRECT_CB_SENSOR;
			//isRD = objRadarDetector((BASE_OBJECT *)psDroid);
		}
		else if (psSensor->type == OBJ_STRUCTURE)
		{
                  Structure *psCStruct = (Structure *)psSensor;

			// skip incomplete structures
			if (psCStruct->status != SS_BUILT)
			{
				continue;
			}
			// Artillery should not fire at objects observed by VTOL CB/Strike sensors.
			if (psCStruct->stats->pSensor->type == VTOL_CB_SENSOR ||
				psCStruct->stats->pSensor->type == VTOL_INTERCEPT_SENSOR ||
				objRadarDetector((GameObject *)psCStruct))
			{
				continue;
			}
			psTemp = psCStruct->psTarget[0];
			isCB = structCBSensor(psCStruct);
			//isRD = objRadarDetector((BASE_OBJECT *)psCStruct);
		}
		if (!psTemp || psTemp->deathTime || aiObjectIsProbablyDoomed(psTemp, false) || !validTarget(psObj, psTemp, 0) || aiCheckAlliances(psTemp->owningPlayer, psObj->owningPlayer))
		{
			continue;
		}
		int distSq = objPosDiffSq(psTemp->position, psObj->position);
		// Need to be in range, prefer closer targets or CB targets
		if ((isCB > foundCB || (isCB == foundCB && distSq < tarDist)) && distSq > minDist)
		{
			if (aiObjHasRange(psObj, psTemp, weapon_slot) && visibleObject(psSensor, psTemp, false))
			{
				tarDist = distSq;
				psTarget = psTemp;
				if (targetOrigin)
				{
					*targetOrigin = ORIGIN_SENSOR;
				}

				if (isCB)
				{
					if (targetOrigin)
					{
						*targetOrigin = ORIGIN_CB_SENSOR;
					}
					foundCB = true;  // got CB target, drop everything and shoot!
				}
				/*
				else if (isRD)
				{
					if (targetOrigin)
					{
						*targetOrigin = ORIGIN_RADAR_DETECTOR;
					}
				}
				*/
			}
		}
	}
	return psTarget;
}

/* Calculates attack priority for a certain target */
static SDWORD targetAttackWeight(GameObject *psTarget, GameObject *psAttacker, SDWORD weapon_slot)
{
	SDWORD			targetTypeBonus = 0, damageRatio = 0, attackWeight = 0, noTarget = -1;
	UDWORD			weaponSlot;
        Droid *targetDroid = nullptr, *psAttackerDroid = nullptr, *psGroupDroid, *psDroid;
        Structure *targetStructure = nullptr;
	WEAPON_EFFECT	weaponEffect;
	WEAPON_STATS	*attackerWeapon;
	bool			bEmpWeap = false, bCmdAttached = false, bTargetingCmd = false, bDirect = false;

	if (psTarget == nullptr || psAttacker == nullptr || psTarget->deathTime)
	{
		return noTarget;
	}
	ASSERT(psTarget != psAttacker, "targetAttackWeight: Wanted to evaluate the worth of attacking ourselves...");

	targetTypeBonus = 0;			//Sensors/ecm droids, non-military structures get lower priority

	/* Get attacker weapon effect */
	if (psAttacker->type == OBJ_DROID)
	{
		psAttackerDroid = (Droid *)psAttacker;

		attackerWeapon = (WEAPON_STATS *)(asWeaponStats + psAttackerDroid->m_weaponList[weapon_slot].nStat);

		//check if this droid is assigned to a commander
		bCmdAttached = hasCommander(psAttackerDroid);

		//find out if current target is targeting our commander
		if (bCmdAttached)
		{
			if (psTarget->type == OBJ_DROID)
			{
				psDroid = (Droid *)psTarget;

				//go through all enemy weapon slots
				for (weaponSlot = 0; !bTargetingCmd &&
				     weaponSlot < ((Droid *)psTarget)->numWeapons; weaponSlot++)
				{
					//see if this weapon is targeting our commander
					if (psDroid->psActionTarget[weaponSlot] == (GameObject *)psAttackerDroid->psGroup->psCommander)
					{
						bTargetingCmd = true;
					}
				}
			}
			else
			{
				if (psTarget->type == OBJ_STRUCTURE)
				{
					//go through all enemy weapons
					for (weaponSlot = 0; !bTargetingCmd && weaponSlot < ((Structure *)psTarget)->numWeapons; weaponSlot++)
					{
						if (((Structure *)psTarget)->psTarget[weaponSlot] ==
						    (GameObject *)psAttackerDroid->psGroup->psCommander)
						{
							bTargetingCmd = true;
						}
					}
				}
			}
		}
	}
	else if (psAttacker->type == OBJ_STRUCTURE)
	{
		attackerWeapon = ((WEAPON_STATS *)(asWeaponStats + ((Structure *)psAttacker)->m_weaponList[weapon_slot].nStat));
	}
	else	/* feature */
	{
		ASSERT(!"invalid attacker object type", "targetAttackWeight: Invalid attacker object type");
		return noTarget;
	}

	bDirect = proj_Direct(attackerWeapon);
	if (psAttacker->type == OBJ_DROID && psAttackerDroid->droidType == DROID_SENSOR)
	{
		// Sensors are considered a direct weapon,
		// but for computing expected damage it makes more sense to use indirect damage
		bDirect = false;
	}

	//Get weapon effect
	weaponEffect = attackerWeapon->weaponEffect;

	//See if attacker is using an EMP weapon
	bEmpWeap = (attackerWeapon->weaponSubClass == WSC_EMP);

	int dist = iHypot((psAttacker->position - psTarget->position).xy());
	bool tooClose = (unsigned)dist <= proj_GetMinRange(attackerWeapon, psAttacker->owningPlayer);
	if (tooClose)
	{
		dist = objSensorRange(psAttacker);  // If object is too close to fire at, consider it to be at maximum range.
	}

	/* Calculate attack weight */
	if (psTarget->type == OBJ_DROID)
	{
		targetDroid = (Droid *)psTarget;

		if (targetDroid->deathTime)
		{
			debug(LOG_NEVER, "Target droid is dead, skipping invalid droid.\n");
			return noTarget;
		}

		/* Calculate damage this target suffered */
		if (targetDroid->originalBody == 0) // FIXME Somewhere we get 0HP droids from
		{
			damageRatio = 0;
			debug(LOG_ERROR, "targetAttackWeight: 0HP droid detected!");
			debug(LOG_ERROR, "  Type: %i Name: \"%s\" Owner: %i \"%s\")",
			      targetDroid->droidType, targetDroid->name, targetDroid->owningPlayer, getPlayerName(targetDroid->owningPlayer));
		}
		else
		{
			damageRatio = 100 - 100 * targetDroid->hitPoints / targetDroid->originalBody;
		}
		assert(targetDroid->originalBody != 0); // Assert later so we get the info from above

		/* See if this type of a droid should be prioritized */
		switch (targetDroid->droidType)
		{
		case DROID_SENSOR:
		case DROID_ECM:
		case DROID_PERSON:
		case DROID_TRANSPORTER:
		case DROID_SUPERTRANSPORTER:
		case DROID_DEFAULT:
		case DROID_ANY:
			break;

		case DROID_CYBORG:
		case DROID_WEAPON:
		case DROID_CYBORG_SUPER:
			targetTypeBonus = WEIGHT_WEAPON_DROIDS;
			break;

		case DROID_COMMAND:
			targetTypeBonus = WEIGHT_COMMAND_DROIDS;
			break;

		case DROID_CONSTRUCT:
		case DROID_REPAIR:
		case DROID_CYBORG_CONSTRUCT:
		case DROID_CYBORG_REPAIR:
			targetTypeBonus = WEIGHT_SERVICE_DROIDS;
			break;
		}

		/* Now calculate the overall weight */
		attackWeight = asWeaponModifier[weaponEffect][(asPropulsionStats + targetDroid->asBits[COMP_PROPULSION])->propulsionType] // Our weapon's effect against target
		               + asWeaponModifierBody[weaponEffect][(asBodyStats + targetDroid->asBits[COMP_BODY])->size]
		               + WEIGHT_DIST_TILE_DROID * objSensorRange(psAttacker) / TILE_UNITS
		               - WEIGHT_DIST_TILE_DROID * dist / TILE_UNITS // farther droids are less attractive
		               + WEIGHT_HEALTH_DROID * damageRatio / 100 // we prefer damaged droids
		               + targetTypeBonus; // some droid types have higher priority

		/* If attacking with EMP try to avoid targets that were already "EMPed" */
		if (bEmpWeap &&
		    (targetDroid->lastHitWeapon == WSC_EMP) &&
		    ((gameTime - targetDroid->timeLastHit) < EMP_DISABLE_TIME))		//target still disabled
		{
			attackWeight /= EMP_DISABLED_PENALTY_F;
		}
	}
	else if (psTarget->type == OBJ_STRUCTURE)
	{
		targetStructure = (Structure *)psTarget;

		/* Calculate damage this target suffered */
		damageRatio = 100 - 100 * targetStructure->hitPoints / structureBody(targetStructure);

		/* See if this type of a structure should be prioritized */
		switch (targetStructure->stats->type)
		{
		case REF_DEFENSE:
			targetTypeBonus = WEIGHT_WEAPON_STRUCT;
			break;

		case REF_RESOURCE_EXTRACTOR:
			targetTypeBonus = WEIGHT_DERRICK_STRUCT;
			break;

		case REF_FACTORY:
		case REF_CYBORG_FACTORY:
		case REF_REPAIR_FACILITY:
			targetTypeBonus = WEIGHT_MILITARY_STRUCT;
			break;
		default:
			break;
		}

		/* Now calculate the overall weight */
		attackWeight = asStructStrengthModifier[weaponEffect][targetStructure->stats
                             ->strength] // Our weapon's effect against target
		               + WEIGHT_DIST_TILE_STRUCT * objSensorRange(psAttacker) / TILE_UNITS
		               - WEIGHT_DIST_TILE_STRUCT * dist / TILE_UNITS // farther structs are less attractive
		               + WEIGHT_HEALTH_STRUCT * damageRatio / 100 // we prefer damaged structures
		               + targetTypeBonus; // some structure types have higher priority

		/* Go for unfinished structures only if nothing else found (same for non-visible structures) */
		if (targetStructure->status != SS_BUILT)		//a decoy?
		{
			attackWeight /= WEIGHT_STRUCT_NOTBUILT_F;
		}

		/* EMP should only attack structures if no enemy droids are around */
		if (bEmpWeap)
		{
			attackWeight /= EMP_STRUCT_PENALTY_F;
		}
	}
	else	//a feature
	{
		return 1;
	}

	/* We prefer objects we can see and can attack immediately */
	if (!visibleObject((GameObject *)psAttacker, psTarget, true))
	{
		attackWeight /= WEIGHT_NOT_VISIBLE_F;
	}

	if (tooClose)
	{
		attackWeight /= TOO_CLOSE_PENALTY_F;
	}

	/* Penalty for units that are already considered doomed (but the missile might miss!) */
	if (aiObjectIsProbablyDoomed(psTarget, bDirect))
	{
		/* indirect firing units have slow reload times, so give the target a chance to die,
		 * and give a different unit a chance to get in range, too. */
		if (weaponROF(attackerWeapon, psAttacker->owningPlayer) < TARGET_DOOMED_SLOW_RELOAD_T)
		{
			debug(LOG_NEVER, "Not killing unit - doomed. My ROF: %i (%s)", weaponROF(attackerWeapon, psAttacker->owningPlayer), getStatsName(attackerWeapon));
			return noTarget;
		}
		attackWeight /= TARGET_DOOMED_PENALTY_F;
	}

	/* Commander-related criterias */
	if (bCmdAttached)	//attached to a commander and don't have a target assigned by some order
	{
		ASSERT(psAttackerDroid->psGroup->psCommander != nullptr, "Commander is NULL");

		//if commander is being targeted by our target, try to defend the commander
		if (bTargetingCmd)
		{
			attackWeight += WEIGHT_CMD_RANK * (1 + getDroidLevel(psAttackerDroid->psGroup->psCommander));
		}

		//fire support - go through all droids assigned to the commander
		for (psGroupDroid = psAttackerDroid->psGroup->psList; psGroupDroid; psGroupDroid = psGroupDroid->psGrpNext)
		{
			for (weaponSlot = 0; weaponSlot < psGroupDroid->numWeapons; weaponSlot++)
			{
				//see if this droid is currently targeting current target
				if (psGroupDroid->order.psObj == psTarget ||
				    psGroupDroid->psActionTarget[weaponSlot] == psTarget)
				{
					//we prefer targets that are already targeted and hence will be destroyed faster
					attackWeight += WEIGHT_CMD_SAME_TARGET;
				}
			}
		}
	}

	return std::max<int>(1, attackWeight);
}


// Find the best nearest target for a droid.
// If extraRange is higher than zero, then this is the range it accepts for movement to target.
// Returns integer representing target priority, -1 if failed
int aiBestNearestTarget(Droid *psDroid, GameObject **ppsObj, int weapon_slot, int extraRange)
{
	int failure = -1;
	int bestMod = 0;
        GameObject *psTarget = nullptr, *bestTarget = nullptr, *tempTarget;
	bool				electronic = false;
        Structure *targetStructure;
	WEAPON_EFFECT			weaponEffect;
	TARGET_ORIGIN tmpOrigin = ORIGIN_UNKNOWN;

	//don't bother looking if empty vtol droid
	if (vtolEmpty(psDroid))
	{
		return failure;
	}

	/* Return if have no weapons */
	// The ai orders a non-combat droid to patrol = crash without it...
	if ((psDroid->m_weaponList[0].nStat == 0 || psDroid->numWeapons == 0) && psDroid->droidType != DROID_SENSOR)
	{
		return failure;
	}
	// Check if we have a CB target to begin with
	if (!proj_Direct(asWeaponStats + psDroid->m_weaponList[weapon_slot].nStat))
	{
		WEAPON_STATS *psWStats = psDroid->m_weaponList[weapon_slot].nStat + asWeaponStats;

		bestTarget = aiSearchSensorTargets((GameObject *)psDroid, weapon_slot, psWStats, &tmpOrigin);
		bestMod = targetAttackWeight(bestTarget, (GameObject *)psDroid, weapon_slot);
	}

	weaponEffect = (asWeaponStats + psDroid->m_weaponList[weapon_slot].nStat)->weaponEffect;

	electronic = electronicDroid(psDroid);

	// Range was previously 9*TILE_UNITS. Increasing this doesn't seem to help much, though. Not sure why.
	int droidRange = std::min(aiDroidRange(psDroid, weapon_slot) + extraRange, objSensorRange(psDroid) + 6 * TILE_UNITS);

	static GridList gridList;  // static to avoid allocations.
	gridList = gridStartIterate(psDroid->position.x, psDroid->position.y, droidRange);
	for (GridIterator gi = gridList.begin(); gi != gridList.end(); ++gi)
	{
          GameObject *friendlyObj = nullptr;
          GameObject *targetInQuestion = *gi;

		/* This is a friendly unit, check if we can reuse its target */
		if (aiCheckAlliances(targetInQuestion->owningPlayer, psDroid->owningPlayer))
		{
			friendlyObj = targetInQuestion;
			targetInQuestion = nullptr;

			/* Can we see what it is doing? */
			if (friendlyObj->visible[psDroid->owningPlayer] == UBYTE_MAX)
			{
				if (friendlyObj->type == OBJ_DROID)
				{
                                  Droid *friendlyDroid = (Droid *)friendlyObj;

					/* See if friendly droid has a target */
					tempTarget = friendlyDroid->psActionTarget[0];
					if (tempTarget && !tempTarget->deathTime)
					{
						//make sure a weapon droid is targeting it
						if (friendlyDroid->numWeapons > 0)
						{
							// make sure this target wasn't assigned explicitly to this droid
							if (friendlyDroid->order.type != DORDER_ATTACK)
							{
								targetInQuestion = tempTarget;  //consider this target
							}
						}
					}
				}
				else if (friendlyObj->type == OBJ_STRUCTURE)
				{
					tempTarget = ((Structure *)friendlyObj)->psTarget[0];
					if (tempTarget && !tempTarget->deathTime)
					{
						targetInQuestion = tempTarget;
					}
				}
			}
		}

		if (targetInQuestion != nullptr
		    && targetInQuestion != psDroid  // in case friendly unit had me as target
		    && (targetInQuestion->type == OBJ_DROID || targetInQuestion->type == OBJ_STRUCTURE || targetInQuestion->type == OBJ_FEATURE)
		    && targetInQuestion->visible[psDroid->owningPlayer] == UBYTE_MAX
		    && !aiCheckAlliances(targetInQuestion->owningPlayer, psDroid->owningPlayer)
		    && validTarget(psDroid, targetInQuestion, weapon_slot)
		    && objPosDiffSq(psDroid, targetInQuestion) < droidRange * droidRange)
		{
			if (targetInQuestion->type == OBJ_DROID)
			{
				// in multiPlayer - don't attack Transporters with EW
				if (bMultiPlayer)
				{
					// if not electronic then valid target
					if (!electronic
					    || (electronic
					        && !isTransporter((Droid *)targetInQuestion)))
					{
						//only a valid target if NOT a transporter
						psTarget = targetInQuestion;
					}
				}
				else
				{
					psTarget = targetInQuestion;
				}
			}
			else if (targetInQuestion->type == OBJ_STRUCTURE)
			{
                          Structure *psStruct = (Structure *)targetInQuestion;

				if (electronic)
				{
					/* don't want to target structures with resistance of zero if using electronic warfare */
					if (validStructResistance((
                                                Structure *)targetInQuestion))
					{
						psTarget = targetInQuestion;
					}
				}
				else if (psStruct->m_weaponList[0].nStat > 0)
				{
					// structure with weapons - go for this
					psTarget = targetInQuestion;
				}
				else if ((isHumanPlayer(psDroid->owningPlayer) && (psStruct->stats->type != REF_WALL && psStruct->stats->type != REF_WALLCORNER))
					|| !isHumanPlayer(psDroid->owningPlayer))
				{
					psTarget = targetInQuestion;
				}
			}
			else if (targetInQuestion->type == OBJ_FEATURE
			         && psDroid->lastFrustratedTime > 0
			         && gameTime - psDroid->lastFrustratedTime < FRUSTRATED_TIME
			         && ((Feature *)targetInQuestion)->psStats->damageable
			         && psDroid->owningPlayer != scavengerPlayer())  // hack to avoid scavs blowing up their nice feature walls
			{
				psTarget = targetInQuestion;
				objTrace(psDroid->id, "considering shooting at %s in frustration", objInfo(targetInQuestion));
			}

			/* Check if our weapon is most effective against this object */
			if (psTarget != nullptr && psTarget == targetInQuestion)		//was assigned?
			{
				int newMod = targetAttackWeight(psTarget, (GameObject *)psDroid, weapon_slot);

				/* Remember this one if it's our best target so far */
				if (newMod >= 0 && (newMod > bestMod || bestTarget == nullptr))
				{
					bestMod = newMod;
					tmpOrigin = ORIGIN_ALLY;
					bestTarget = psTarget;
				}
			}
		}
	}

	if (bestTarget)
	{
		ASSERT(!bestTarget->deathTime, "AI gave us a target that is already dead.");
		targetStructure = visGetBlockingWall(psDroid, bestTarget);

		// See if target is blocked by a wall; only affects direct weapons
		// Ignore friendly walls here
		if (proj_Direct(asWeaponStats + psDroid->m_weaponList[weapon_slot].nStat)
			&& targetStructure
			&& !aiCheckAlliances(psDroid->owningPlayer, targetStructure->owningPlayer))
		{
			//are we any good against walls?
			if (asStructStrengthModifier[weaponEffect][targetStructure->stats
                                                         ->strength] >= MIN_STRUCTURE_BLOCK_STRENGTH)
			{
				bestTarget = (GameObject *)targetStructure; //attack wall
			}
		}

		*ppsObj = bestTarget;
		return bestMod;
	}

	return failure;
}

// Are there a lot of bullets heading towards the droid?
static bool aiDroidIsProbablyDoomed(Droid *psDroid, bool isDirect)
{
	if (isDirect)
	{
		return psDroid->expectedDamageDirect > psDroid->hitPoints && psDroid->expectedDamageDirect - psDroid->hitPoints > psDroid->hitPoints / 5; // Doomed if projectiles will damage 120% of remaining body points.
	}
	else
	{
		return psDroid->expectedDamageIndirect > psDroid->hitPoints && psDroid->expectedDamageIndirect - psDroid->hitPoints > psDroid->hitPoints / 5; // Doomed if projectiles will damage 120% of remaining body points.
	}
}

// Are there a lot of bullets heading towards the structure?
static bool aiStructureIsProbablyDoomed(Structure *psStructure)
{
	return psStructure->expectedDamage > psStructure->hitPoints && psStructure->expectedDamage - psStructure->hitPoints > psStructure->hitPoints / 15; // Doomed if projectiles will damage 106.6666666667% of remaining body points.
}

// Are there a lot of bullets heading towards the object?
bool aiObjectIsProbablyDoomed(GameObject *psObject, bool isDirect)
{
	if (psObject->deathTime)
	{
		return true;    // Was definitely doomed.
	}

	switch (psObject->type)
	{
	case OBJ_DROID:
		return aiDroidIsProbablyDoomed((Droid *)psObject, isDirect);
	case OBJ_STRUCTURE:
		return aiStructureIsProbablyDoomed((Structure *)psObject);
	default:
		return false;
	}
}

// Update the expected damage of the object.
void aiObjectAddExpectedDamage(GameObject *psObject, SDWORD damage, bool isDirect)
{
	if (psObject == nullptr)
	{
		return;    // Hard to destroy the ground.
	}

	switch (psObject->type)
	{
	case OBJ_DROID:
		if (isDirect)
		{
			((Droid *)psObject)->expectedDamageDirect += damage;
			ASSERT((SDWORD)((Droid *)psObject)->expectedDamageDirect >= 0, "aiObjectAddExpectedDamage: Negative amount of projectiles heading towards droid.");
		}
		else
		{
			((Droid *)psObject)->expectedDamageIndirect += damage;
			ASSERT((SDWORD)((Droid *)psObject)->expectedDamageIndirect >= 0, "aiObjectAddExpectedDamage: Negative amount of projectiles heading towards droid.");
		}
		break;
	case OBJ_STRUCTURE:
		((Structure *)psObject)->expectedDamage += damage;
		ASSERT((SDWORD)((Structure *)psObject)->expectedDamage >= 0, "aiObjectAddExpectedDamage: Negative amount of projectiles heading towards droid.");
		break;
	default:
		break;
	}
}

// see if an object is a wall
static bool aiObjIsWall(GameObject *psObj)
{
	if (psObj->type != OBJ_STRUCTURE)
	{
		return false;
	}

	if (((Structure *)psObj)->stats->type != REF_WALL &&
	    ((Structure *)psObj)->stats->type != REF_WALLCORNER)
	{
		return false;
	}

	return true;
}


/* See if there is a target in range */
bool aiChooseTarget(GameObject *psObj, GameObject **ppsTarget, int weapon_slot, bool bUpdateTarget, TARGET_ORIGIN *targetOrigin)
{
  GameObject *psTarget = nullptr;
  Droid *psCommander;
	SDWORD			curTargetWeight = -1;
	TARGET_ORIGIN		tmpOrigin = ORIGIN_UNKNOWN;

	if (targetOrigin)
	{
		*targetOrigin = ORIGIN_UNKNOWN;
	}

	ASSERT_OR_RETURN(false, (unsigned)weapon_slot < psObj->numWeapons, "Invalid weapon selected");

	/* See if there is a something in range */
	if (psObj->type == OBJ_DROID)
	{
          GameObject *psCurrTarget = ((Droid *)psObj)->psActionTarget[0];

		/* find a new target */
		int newTargetWeight = aiBestNearestTarget((Droid *)psObj, &psTarget, weapon_slot);

		/* Calculate weight of the current target if updating; but take care not to target
		 * ourselves... */
		if (bUpdateTarget && psCurrTarget != psObj)
		{
			curTargetWeight = targetAttackWeight(psCurrTarget, psObj, weapon_slot);
		}

		if (newTargetWeight >= 0		// found a new target
		    && (!bUpdateTarget			// choosing a new target, don't care if current one is better
		        || curTargetWeight <= 0		// attacker had no valid target, use new one
		        || newTargetWeight > curTargetWeight + OLD_TARGET_THRESHOLD)	// updating and new target is better
		    && validTarget(psObj, psTarget, weapon_slot)
		    && aiDroidHasRange((Droid *)psObj, psTarget, weapon_slot))
		{
			ASSERT(!isDead(psTarget), "Droid found a dead target!");
			*ppsTarget = psTarget;
			return true;
		}
	}
	else if (psObj->type == OBJ_STRUCTURE)
	{
		bool	bCommanderBlock = false;

		ASSERT_OR_RETURN(false, psObj->weaponList[weapon_slot].nStat > 0, "Invalid weapon turret");

		WEAPON_STATS *psWStats = psObj->weaponList[weapon_slot].nStat + asWeaponStats;
		int longRange = proj_GetLongRange(psWStats, psObj->owningPlayer);

		// see if there is a target from the command droids
		psTarget = nullptr;
		psCommander = cmdDroidGetDesignator(psObj->owningPlayer);
		if (!proj_Direct(psWStats) && (psCommander != nullptr) &&
		    aiStructHasRange((Structure *)psObj, (GameObject *)psCommander, weapon_slot))
		{
			// there is a commander that can fire designate for this structure
			// set bCommanderBlock so that the structure does not fire until the commander
			// has a target - (slow firing weapons will not be ready to fire otherwise).
			bCommanderBlock = true;

			// I do believe this will never happen, check for yourself :-)
			debug(LOG_NEVER, "Commander %d is good enough for fire designation", psCommander->id);

			if (psCommander->action == DACTION_ATTACK
			    && psCommander->psActionTarget[0] != nullptr
			    && !psCommander->psActionTarget[0]->deathTime)
			{
				// the commander has a target to fire on
				if (aiStructHasRange((Structure *)psObj, psCommander->psActionTarget[0], weapon_slot))
				{
					// target in range - fire on it
					tmpOrigin = ORIGIN_COMMANDER;
					psTarget = psCommander->psActionTarget[0];
				}
				else
				{
					// target out of range - release the commander block
					bCommanderBlock = false;
				}
			}
		}

		// indirect fire structures use sensor towers first
		if (psTarget == nullptr && !bCommanderBlock && !proj_Direct(psWStats))
		{
			psTarget = aiSearchSensorTargets(psObj, weapon_slot, psWStats, &tmpOrigin);
		}

		if (psTarget == nullptr && !bCommanderBlock)
		{
			int targetValue = -1;
			int tarDist = INT32_MAX;
			int srange = longRange;

			if (!proj_Direct(psWStats) && srange > objSensorRange(psObj))
			{
				// search radius of indirect weapons limited by their sight, unless they use
				// external sensors to provide fire designation
				srange = objSensorRange(psObj);
			}

			static GridList gridList;  // static to avoid allocations.
			gridList = gridStartIterate(psObj->position.x, psObj->position.y, srange);
			for (GridIterator gi = gridList.begin(); gi != gridList.end(); ++gi)
			{
                          GameObject *psCurr = *gi;
				/* Check that it is a valid target */
				if (psCurr->type != OBJ_FEATURE && !psCurr->deathTime && !aiCheckAlliances(psCurr->owningPlayer, psObj->owningPlayer)
				    && validTarget(psObj, psCurr, weapon_slot) && psCurr->visible[psObj->owningPlayer] == UBYTE_MAX
				    && aiStructHasRange((Structure *)psObj, psCurr, weapon_slot))
				{
					int newTargetValue = targetAttackWeight(psCurr, psObj, weapon_slot);
					// See if in sensor range and visible
					int distSq = objPosDiffSq(psCurr->position, psObj->position);
					if (newTargetValue < targetValue || (newTargetValue == targetValue && distSq >= tarDist))
					{
						continue;
					}

					tmpOrigin = ORIGIN_VISUAL;
					psTarget = psCurr;
					tarDist = distSq;
					targetValue = newTargetValue;
				}
			}
		}

		if (psTarget)
		{
			ASSERT(!psTarget->deathTime, "Structure found a dead target!");
			if (targetOrigin)
			{
				*targetOrigin = tmpOrigin;
			}
			*ppsTarget = psTarget;
			return true;
		}
	}

	return false;
}


/* See if there is a target in range for Sensor objects*/
bool aiChooseSensorTarget(GameObject *psObj, GameObject **ppsTarget)
{
	int		sensorRange = objSensorRange(psObj);
	unsigned int	radSquared = sensorRange * sensorRange;
	bool		radarDetector = objRadarDetector(psObj);

	if (!objActiveRadar(psObj) && !radarDetector)
	{
		ASSERT(false, "Only to be used for sensor turrets!");
		return false;
	}

	/* See if there is something in range */
	if (psObj->type == OBJ_DROID)
	{
          GameObject *psTarget = nullptr;

		if (aiBestNearestTarget((Droid *)psObj, &psTarget, 0) >= 0)
		{
			/* See if in sensor range */
			const int xdiff = psTarget->position.x - psObj->position.x;
			const int ydiff = psTarget->position.y - psObj->position.y;
			const unsigned int distSq = xdiff * xdiff + ydiff * ydiff;

			// I do believe this will never happen, check for yourself :-)
			debug(LOG_NEVER, "Sensor droid(%d) found possible target(%d)!!!", psObj->id, psTarget->id);

			if (distSq < radSquared)
			{
				*ppsTarget = psTarget;
				return true;
			}
		}
	}
	else	// structure
	{
          GameObject *psTemp = nullptr;
		unsigned tarDist = UINT32_MAX;

		static GridList gridList;  // static to avoid allocations.
		gridList = gridStartIterate(psObj->position.x, psObj->position.y, objSensorRange(psObj));
		for (GridIterator gi = gridList.begin(); gi != gridList.end(); ++gi)
		{
                  GameObject *psCurr = *gi;
			// Don't target features or doomed/dead objects
			if (psCurr->type != OBJ_FEATURE && !psCurr->deathTime && !aiObjectIsProbablyDoomed(psCurr, false))
			{
				if (!aiCheckAlliances(psCurr->owningPlayer, psObj->owningPlayer) && !aiObjIsWall(psCurr))
				{
					// See if in sensor range and visible
					const int xdiff = psCurr->position.x - psObj->position.x;
					const int ydiff = psCurr->position.y - psObj->position.y;
					const unsigned int distSq = xdiff * xdiff + ydiff * ydiff;

					if (distSq < radSquared && psCurr->visible[psObj->owningPlayer] == UBYTE_MAX && distSq < tarDist)
					{
						psTemp = psCurr;
						tarDist = distSq;
					}
				}
			}
		}

		if (psTemp)
		{
			ASSERT(!psTemp->deathTime, "aiChooseSensorTarget gave us a dead target");
			*ppsTarget = psTemp;
			return true;
		}
	}

	return false;
}

/* Make droid/structure look for a better target */
static bool updateAttackTarget(GameObject *psAttacker, SDWORD weapon_slot)
{
  GameObject *psBetterTarget = nullptr;
	TARGET_ORIGIN tmpOrigin = ORIGIN_UNKNOWN;

	if (aiChooseTarget(psAttacker, &psBetterTarget, weapon_slot, true, &tmpOrigin))	//update target
	{
		if (psAttacker->type == OBJ_DROID)
		{
                  Droid *psDroid = (Droid *)psAttacker;

			if ((orderState(psDroid, DORDER_NONE) ||
			     orderState(psDroid, DORDER_GUARD) ||
			     orderState(psDroid, DORDER_ATTACKTARGET)) &&
			    weapon_slot == 0)
			{
				actionDroid((Droid *)psAttacker, DACTION_ATTACK, psBetterTarget);
			}
			else	//can't override current order
			{
				setDroidActionTarget(psDroid, psBetterTarget, weapon_slot);
			}
		}
		else if (psAttacker->type == OBJ_STRUCTURE)
		{
                  Structure *psBuilding = (Structure *)psAttacker;

			setStructureTarget(psBuilding, psBetterTarget, weapon_slot, tmpOrigin);
		}

		return true;
	}

	return false;
}


/* Check if any of our weapons can hit the target... */
bool checkAnyWeaponsTarget(GameObject *psObject, GameObject *psTarget)
{
  Droid *psDroid = (Droid *) psObject;
	for (int i = 0; i < psDroid->numWeapons; i++)
	{
		if (validTarget(psObject, psTarget, i))
		{
			return true;
		}
	}
	return false;
}

/* Set of rules which determine whether the weapon associated with the object can fire on the propulsion type of the target. */
bool validTarget(GameObject *psObject, GameObject *psTarget, int weapon_slot)
{
	bool	bTargetInAir = false, bValidTarget = false;
	UBYTE	surfaceToAir = 0;

	if (!psTarget)
	{
		return false;
	}

	// Need to check propulsion type of target
	switch (psTarget->type)
	{
	case OBJ_DROID:
		if (asPropulsionTypes[asPropulsionStats[((Droid *)psTarget)->asBits[COMP_PROPULSION]].propulsionType].travel == AIR)
		{
			if (((Droid *)psTarget)->sMove.Status != MOVEINACTIVE)
			{
				bTargetInAir = true;
			}
			else
			{
				bTargetInAir = false;
			}
		}
		else
		{
			bTargetInAir = false;
		}
		break;
	case OBJ_STRUCTURE:
	default:
		//lets hope so!
		bTargetInAir = false;
		break;
	}

	//need what can shoot at
	switch (psObject->type)
	{
	case OBJ_DROID:
		if (((Droid *)psObject)->droidType == DROID_SENSOR)
		{
			return !bTargetInAir;  // Sensor droids should not target anything in the air.
		}

		// Can't attack without a weapon
		if (((Droid *)psObject)->numWeapons != 0 && ((Droid *)psObject)->m_weaponList[weapon_slot].nStat != 0)
		{
			surfaceToAir = asWeaponStats[((Droid *)psObject)->m_weaponList[weapon_slot].nStat].surfaceToAir;
			if (((surfaceToAir & SHOOT_IN_AIR) && bTargetInAir) || ((surfaceToAir & SHOOT_ON_GROUND) && !bTargetInAir))
			{
				return true;
			}
		}
		else
		{
			return false;
		}
		break;
	case OBJ_STRUCTURE:
		// Can't attack without a weapon
		if (((Structure *)psObject)->numWeapons != 0 && ((Structure *)psObject)->m_weaponList[weapon_slot].nStat != 0)
		{
			surfaceToAir = asWeaponStats[((Structure *)psObject)->m_weaponList[weapon_slot].nStat].surfaceToAir;
		}
		else
		{
			surfaceToAir = 0;
		}

		if (((surfaceToAir & SHOOT_IN_AIR) && bTargetInAir) || ((surfaceToAir & SHOOT_ON_GROUND) && !bTargetInAir))
		{
			return true;
		}
		break;
	default:
		surfaceToAir = 0;
		break;
	}

	//if target is in the air and you can shoot in the air - OK
	if (bTargetInAir && (surfaceToAir & SHOOT_IN_AIR))
	{
		bValidTarget = true;
	}

	//if target is on the ground and can shoot at it - OK
	if (!bTargetInAir && (surfaceToAir & SHOOT_ON_GROUND))
	{
		bValidTarget = true;
	}

	return bValidTarget;
}