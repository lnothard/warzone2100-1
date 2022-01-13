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
 * @file ai.cpp
 * AI update functions for the different object types
 */

#include "cmddroid.h"
#include "combat.h"
#include "projectile.h"
#include "objmem.h"
#include "order.h"
#include "visibility.h"

/* Weights used for target selection code,
 * target distance is used as 'common currency'
 */
static constexpr auto	WEIGHT_DIST_TILE = 13;						//In points used in weaponmodifier.txt and structuremodifier.txt
static constexpr auto	WEIGHT_DIST_TILE_DROID = WEIGHT_DIST_TILE; //How much weight a distance of 1 tile (128 world units) has when looking for the best nearest target

static constexpr auto	WEIGHT_DIST_TILE_STRUCT =	WEIGHT_DIST_TILE;
static constexpr auto	WEIGHT_HEALTH_DROID	=	WEIGHT_DIST_TILE * 10; //How much weight unit damage has (100% of damage is equally weighted as 10 tiles distance)

//~100% damage should be ~8 tiles (max sensor range)
static constexpr auto	WEIGHT_HEALTH_STRUCT = WEIGHT_DIST_TILE * 7;

static constexpr auto	WEIGHT_NOT_VISIBLE_F = 10;						//We really don't like objects we can't see

static constexpr auto	WEIGHT_SERVICE_DROIDS	=	WEIGHT_DIST_TILE_DROID * 5;		//We don't want them to be repairing droids or structures while we are after them

static constexpr auto	WEIGHT_WEAPON_DROIDS =	WEIGHT_DIST_TILE_DROID * 4;		//We prefer to go after anything that has a gun and can hurt us

static constexpr auto	WEIGHT_COMMAND_DROIDS	=	WEIGHT_DIST_TILE_DROID * 6;		//Commanders get a higher priority
static constexpr auto	WEIGHT_MILITARY_STRUCT = WEIGHT_DIST_TILE_STRUCT;				//Droid/cyborg factories, repair facility; shouldn't have too much weight

static constexpr auto	WEIGHT_WEAPON_STRUCT = WEIGHT_WEAPON_DROIDS;	//Same as weapon droids (?)
static constexpr auto	WEIGHT_DERRICK_STRUCT	=	WEIGHT_MILITARY_STRUCT + WEIGHT_DIST_TILE_STRUCT * 4;	//Even if it's 4 tiles further away than defenses we still choose it


static constexpr auto	WEIGHT_STRUCT_NOTBUILT_F = 8;						//Humans won't fool us anymore!

static constexpr auto OLD_TARGET_THRESHOLD = WEIGHT_DIST_TILE * 4;	//it only makes sense to switch target if new one is 4+ tiles closer


static constexpr auto	EMP_DISABLED_PENALTY_F = 10;								//EMP shouldn't attack emped targets again
static constexpr auto	EMP_STRUCT_PENALTY_F = EMP_DISABLED_PENALTY_F * 2;	//EMP don't attack structures, should be bigger than EMP_DISABLED_PENALTY_F


static constexpr auto TOO_CLOSE_PENALTY_F = 20;

static constexpr auto TARGET_DOOMED_PENALTY_F	= 10;	// Targets that have a lot of damage incoming are less attractive
static constexpr auto TARGET_DOOMED_SLOW_RELOAD_T	= 21;	// Weapon ROF threshold for above penalty. per minute.

//Some weights for the units attached to a commander
static constexpr auto	WEIGHT_CMD_RANK	=	WEIGHT_DIST_TILE * 4;			//A single rank is as important as 4 tiles distance
static constexpr auto	WEIGHT_CMD_SAME_TARGET = WEIGHT_DIST_TILE;				//Don't want this to be too high, since a commander can have many units assigned

uint8_t alliances[MAX_PLAYER_SLOTS][MAX_PLAYER_SLOTS];

/// A bitfield of vision sharing in alliances, for quick manipulation of vision information
PlayerMask alliancebits[MAX_PLAYER_SLOTS];

/// A bitfield for the satellite uplink
PlayerMask satuplinkbits;

static int aiDroidRange(Droid *psDroid, int weapon_slot)
{
	int longRange;
	if (psDroid->getType() == DROID_TYPE::SENSOR) {
		longRange = objSensorRange(psDroid);
	}
	else if (numWeapons(*psDroid) == 0) {
		// can't attack without a weapon
		return 0;
	}
	else {
		auto& psWStats = psDroid->getWeapons()[weapon_slot].getStats();
		longRange = proj_GetLongRange(&psWStats, psDroid->getPlayer());
	}

	return longRange;
}

// see if a structure has the range to fire on a target
static bool aiStructHasRange(Structure *psStruct, SimpleObject *psTarget, int weapon_slot)
{
	if (numWeapons(*psStruct) == 0) {
		// Can't attack without a weapon
		return false;
	}

	auto& psWStats = psStruct->getWeapons()[weapon_slot].getStats();

	auto longRange = proj_GetLongRange(&psWStats, psStruct->getPlayer());
	return objectPositionSquareDiff(psStruct, psTarget) < longRange * longRange &&
         lineOfFire(psStruct, psTarget, weapon_slot, true);
}

static bool aiDroidHasRange(Droid *psDroid, SimpleObject *psTarget, int weapon_slot)
{
	auto longRange = aiDroidRange(psDroid, weapon_slot);
	return objectPositionSquareDiff(psDroid, psTarget) < longRange * longRange;
}

static bool aiObjHasRange(SimpleObject *psObj, SimpleObject *psTarget, int weapon_slot)
{
	if (auto psDroid = dynamic_cast<Droid*>(psObj)) {
		return aiDroidHasRange(psDroid, psTarget, weapon_slot);
	}
	else if (auto psStruct = dynamic_cast<Structure*>(psObj)) {
		return aiStructHasRange(dynamic_cast<Structure*>(psObj), psTarget, weapon_slot);
	}
	return false;
}

/* Initialise the AI system */
bool aiInitialise()
{
	int	i, j;

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

/** Search the global list of sensors for a possible target for psObj. */
static SimpleObject* aiSearchSensorTargets(SimpleObject* psObj, int weapon_slot, 
                                           WeaponStats* psWStats, TARGET_ORIGIN* targetOrigin)
{
	auto longRange = proj_GetLongRange(psWStats, psObj->getPlayer());
	auto tarDist = longRange * longRange;
	auto foundCB = false;
	auto minDist = proj_GetMinRange(psWStats, psObj->getPlayer()) *
                 proj_GetMinRange(psWStats, psObj->getPlayer());
	SimpleObject* psTarget = nullptr;

	if (targetOrigin) {
		*targetOrigin = TARGET_ORIGIN::UNKNOWN;
	}

	for (auto psSensor : apsSensorList)
	{
		SimpleObject* psTemp = nullptr;
		bool isCB = false;

		if (!aiCheckAlliances(psSensor->getPlayer(), psObj->getPlayer())) {
			continue;
		}
		else if (auto psDroid = dynamic_cast<Droid*>(psSensor)) {
			ASSERT_OR_RETURN(nullptr, psDroid->getType() == DROID_TYPE::SENSOR,
			                 "A non-sensor droid in a sensor list is non-sense");
      
			// Skip non-observing droids. This includes Radar Detectors at
      // the moment since they never observe anything.
			if (psDroid->getAction() != ACTION::OBSERVE) {
				continue;
			}
			// Artillery should not fire at objects observed by VTOL CB/Strike sensors.
			if (asSensorStats[psDroid->asBits[COMP_SENSOR]].type == SENSOR_TYPE::VTOL_CB ||
				asSensorStats[psDroid->asBits[COMP_SENSOR]].type == SENSOR_TYPE::VTOL_INTERCEPT ||
				objRadarDetector((SimpleObject*)psDroid))
			{
				continue;
			}
			psTemp = psDroid->getTarget(0);
			isCB = asSensorStats[psDroid->asBits[COMP_SENSOR]].type == SENSOR_TYPE::INDIRECT_CB;
			//isRD = objRadarDetector((SimpleObject *)psDroid);
		}
		else if (auto psCStruct = dynamic_cast<Structure*>(psSensor))
		{
			// skip incomplete structures
			if (psCStruct->getState() != STRUCTURE_STATE::BUILT) {
				continue;
			}
			// Artillery should not fire at objects observed by VTOL CB/Strike sensors.
			if (psCStruct->sensor_stats->type == SENSOR_TYPE::VTOL_CB ||
          psCStruct->sensor_stats->type == SENSOR_TYPE::VTOL_INTERCEPT ||
          objRadarDetector((SimpleObject*)psCStruct)) {
				continue;
			}
			psTemp = psCStruct->getTarget(0);
			isCB = structCBSensor(psCStruct);
			//isRD = objRadarDetector((SimpleObject *)psCStruct);
		}
		if (!psTemp || psTemp->died || aiObjectIsProbablyDoomed(psTemp, false) ||
        !validTarget(psObj, psTemp, 0) ||
		  	aiCheckAlliances(psTemp->getPlayer(), psObj->getPlayer())) {
			continue;
		}
		auto distSq = objectPositionSquareDiff(psTemp->getPosition(), psObj->getPosition());
    
		// Need to be in range, prefer closer targets or CB targets
    if (!(isCB > foundCB || (isCB == foundCB && distSq < tarDist)) || distSq <= minDist) {
      continue;
    }
    if (!aiObjHasRange(psObj, psTemp, weapon_slot) ||
        !visibleObject(psSensor, psTemp, false)) {
      continue;
    }
    tarDist = distSq;
    psTarget = psTemp;
    if (targetOrigin) {
      *targetOrigin = TARGET_ORIGIN::SENSOR;
    }
    if (isCB) {
      if (targetOrigin) {
        *targetOrigin = TARGET_ORIGIN::CB_SENSOR;
      }
      foundCB = true; // got CB target, drop everything and shoot!
    }
  }
	return psTarget;
}

/* Calculates attack priority for a certain target */
static int targetAttackWeight(SimpleObject* psTarget, SimpleObject* psAttacker, int weapon_slot)
{
	int targetTypeBonus = 0, damageRatio = 0, attackWeight = 0, noTarget = -1;
	unsigned weaponSlot;
	WEAPON_EFFECT weaponEffect;
	WeaponStats* attackerWeapon;
	auto bEmpWeap = false, bCmdAttached = false, bTargetingCmd = false, bDirect = false;

	if (psTarget == nullptr || psAttacker == nullptr || psTarget->died) {
		return noTarget;
	}
	ASSERT(psTarget != psAttacker, "targetAttackWeight: Wanted to evaluate the worth of attacking ourselves...");

	targetTypeBonus = 0; //Sensors/ecm droids, non-military structures get lower priority

	/* Get attacker weapon effect */
	if (auto psAttackerDroid = dynamic_cast<Droid*>(psAttacker)) {
		attackerWeapon = psAttackerDroid->getWeapons()[weapon_slot].getStats();

		//check if this droid is assigned to a commander
		bCmdAttached = hasCommander(psAttackerDroid);

		//find out if current target is targeting our commander
		if (bCmdAttached) {
			if (auto psDroid = dynamic_cast<Droid*>(psTarget)) {
				//go through all enemy weapon slots
				for (weaponSlot = 0; !bTargetingCmd &&
                             weaponSlot < numWeapons(*psDroid); weaponSlot++)
				{
					//see if this weapon is targeting our commander
					if (psDroid->getTarget(weaponSlot) == (SimpleObject&)psAttackerDroid->getGroup().getCommander())
					{
						bTargetingCmd = true;
					}
				}
			}
			else {
				if (auto psStruct = dynamic_cast<Structure*>(psTarget)) {
					//go through all enemy weapons
					for (weaponSlot = 0; !bTargetingCmd && weaponSlot < numWeapons(*psStruct); weaponSlot++)
					{
						if (psStruct->getTarget(weaponSlot) ==
                psAttackerDroid->getGroup().getCommander())
						{
							bTargetingCmd = true;
						}
					}
				}
			}
		}
	}
	else if (psAttacker->type == OBJ_STRUCTURE) {
		attackerWeapon = ((Structure*)psAttacker)->getWeapons()[weapon_slot].getStats();
	}
	else /* feature */
	{
		ASSERT(!"invalid attacker object type", "targetAttackWeight: Invalid attacker object type");
		return noTarget;
	}

	bDirect = proj_Direct(attackerWeapon);
	if (psAttacker->type == OBJ_DROID && psAttackerDroid->getType() == DROID_TYPE::SENSOR) {
		// Sensors are considered a direct weapon,
		// but for computing expected damage it makes more sense to use indirect damage
		bDirect = false;
	}

	//Get weapon effect
	weaponEffect = attackerWeapon->weaponEffect;

	//See if attacker is using an EMP weapon
	bEmpWeap = (attackerWeapon->weaponSubClass == WEAPON_SUBCLASS::EMP);

	int dist = iHypot((psAttacker->getPosition() - psTarget->getPosition()).xy());
	bool tooClose = (unsigned)dist <= proj_GetMinRange(attackerWeapon, psAttacker->getPlayer());
	if (tooClose)
	{
		dist = objSensorRange(psAttacker); // If object is too close to fire at, consider it to be at maximum range.
	}

	/* Calculate attack weight */
	if (auto targetDroid = dynamic_cast<Droid*>(psTarget))
	{
		if (targetDroid->died)
		{
			debug(LOG_NEVER, "Target droid is dead, skipping invalid droid.\n");
			return noTarget;
		}

		/* Calculate damage this target suffered */
		if (targetDroid->getOriginalHp() == 0) // FIXME Somewhere we get 0HP droids from
		{
			damageRatio = 0;
			debug(LOG_ERROR, "targetAttackWeight: 0HP droid detected!");
			debug(LOG_ERROR, "  Type: %i Name: \"%s\" Owner: %i \"%s\")",
            targetDroid->getType(), targetDroid->name, targetDroid->getPlayer(), getPlayerName(targetDroid->getPlayer()));
		}
		else
		{
			damageRatio = 100 - 100 * targetDroid->getHp() / targetDroid->getOriginalHp();
		}
		assert(targetDroid->getOriginalHp() != 0); // Assert later so we get the info from above

		/* See if this type of a droid should be prioritized */
		switch (targetDroid->getType())
		{
		case DROID_TYPE::SENSOR:
		case DROID_TYPE::ECM:
		case DROID_TYPE::PERSON:
		case DROID_TYPE::TRANSPORTER:
		case DROID_TYPE::SUPER_TRANSPORTER:
		case DROID_TYPE::DEFAULT:
		case DROID_TYPE::ANY:
			break;

		case DROID_TYPE::CYBORG:
		case DROID_TYPE::WEAPON:
		case DROID_TYPE::CYBORG_SUPER:
			targetTypeBonus = WEIGHT_WEAPON_DROIDS;
			break;

		case DROID_TYPE::COMMAND:
			targetTypeBonus = WEIGHT_COMMAND_DROIDS;
			break;

		case DROID_TYPE::CONSTRUCT:
		case DROID_TYPE::REPAIRER:
		case DROID_TYPE::CYBORG_CONSTRUCT:
		case DROID_TYPE::CYBORG_REPAIR:
			targetTypeBonus = WEIGHT_SERVICE_DROIDS;
			break;
		}

		/* Now calculate the overall weight */
		attackWeight = asWeaponModifier[weaponEffect][(asPropulsionStats + targetDroid->asBits[COMP_PROPULSION])->
				propulsionType] // Our weapon's effect against target
			+ asWeaponModifierBody[weaponEffect][(asBodyStats + targetDroid->asBits[COMP_BODY])->size]
			+ WEIGHT_DIST_TILE_DROID * objSensorRange(psAttacker) / TILE_UNITS
			- WEIGHT_DIST_TILE_DROID * dist / TILE_UNITS // farther droids are less attractive
			+ WEIGHT_HEALTH_DROID * damageRatio / 100 // we prefer damaged droids
			+ targetTypeBonus; // some droid types have higher priority

		/* If attacking with EMP try to avoid targets that were already "EMPed" */
		if (bEmpWeap &&
			(targetDroid->lastHitWeapon == WEAPON_SUBCLASS::EMP) &&
			((gameTime - targetDroid->timeLastHit) < EMP_DISABLE_TIME)) //target still disabled
		{
			attackWeight /= EMP_DISABLED_PENALTY_F;
		}
	}
	else if (auto targetStructure = dynamic_cast<Structure*>(psTarget))
	{
		/* Calculate damage this target suffered */
		damageRatio = 100 - 100 * targetStructure->getHp() / structureBody(targetStructure);

		/* See if this type of a structure should be prioritized */
		switch (targetStructure->getStats().type) {
      using enum STRUCTURE_TYPE;
		  case DEFENSE:
		  	targetTypeBonus = WEIGHT_WEAPON_STRUCT;
		  	break;
		  case RESOURCE_EXTRACTOR:
		  	targetTypeBonus = WEIGHT_DERRICK_STRUCT;
		  	break;
		  case FACTORY:
		  case CYBORG_FACTORY:
		  case REPAIR_FACILITY:
		  	targetTypeBonus = WEIGHT_MILITARY_STRUCT;
		  	break;
		  default:
		  	break;
		}

		/* Now calculate the overall weight */
		attackWeight = asStructStrengthModifier[weaponEffect][targetStructure->pStructureType->strength]
			// Our weapon's effect against target
			+ WEIGHT_DIST_TILE_STRUCT * objSensorRange(psAttacker) / TILE_UNITS
			- WEIGHT_DIST_TILE_STRUCT * dist / TILE_UNITS // farther structs are less attractive
			+ WEIGHT_HEALTH_STRUCT * damageRatio / 100 // we prefer damaged structures
			+ targetTypeBonus; // some structure types have higher priority

		/* Go for unfinished structures only if nothing else found (same for non-visible structures) */
		if (targetStructure->getState() != STRUCTURE_STATE::BUILT) //a decoy?
		{
			attackWeight /= WEIGHT_STRUCT_NOTBUILT_F;
		}

		/* EMP should only attack structures if no enemy droids are around */
		if (bEmpWeap)
		{
			attackWeight /= EMP_STRUCT_PENALTY_F;
		}
	}
	else //a feature
	{
		return 1;
	}

	/* We prefer objects we can see and can attack immediately */
	if (!visibleObject((SimpleObject*)psAttacker, psTarget, true))
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
		if (weaponROF(attackerWeapon, psAttacker->getPlayer()) < TARGET_DOOMED_SLOW_RELOAD_T)
		{
			debug(LOG_NEVER, "Not killing unit - doomed. My ROF: %i (%s)",
            weaponROF(attackerWeapon, psAttacker->getPlayer()), getStatsName(attackerWeapon));
			return noTarget;
		}
		attackWeight /= TARGET_DOOMED_PENALTY_F;
	}

	/* Commander-related criterias */
	if (bCmdAttached) //attached to a commander and don't have a target assigned by some order
	{
		ASSERT(psAttackerDroid->group->psCommander != nullptr, "Commander is NULL");

		//if commander is being targeted by our target, try to defend the commander
		if (bTargetingCmd)
		{
			attackWeight += WEIGHT_CMD_RANK * (1 + getDroidLevel(psAttackerDroid->group->psCommander));
		}

		//fire support - go through all droids assigned to the commander
		for (psGroupDroid = psAttackerDroid->group->members; psGroupDroid; psGroupDroid = psGroupDroid->psGrpNext)
		{
			for (weaponSlot = 0; weaponSlot < psGroupDroid->numWeaps; weaponSlot++)
			{
				//see if this droid is currently targeting current target
				if (psGroupDroid->order.psObj == psTarget ||
            psGroupDroid->actionTarget[weaponSlot] == psTarget)
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
int aiBestNearestTarget(Droid* psDroid, SimpleObject** ppsObj, int weapon_slot, int extraRange)
{
	int failure = -1;
	int bestMod = 0;
	SimpleObject *psTarget = nullptr, *bestTarget = nullptr, *tempTarget;
	bool electronic = false;
	Structure* targetStructure;
	WEAPON_EFFECT weaponEffect;
	TARGET_ORIGIN tmpOrigin = TARGET_ORIGIN::UNKNOWN;

	//don't bother looking if empty vtol droid
	if (vtolEmpty(*psDroid)) {
		return failure;
	}

	/* Return if have no weapons */
	// The ai orders a non-combat droid to patrol = crash without it...
	if ((psDroid->asWeaps[0].nStat == 0 || numWeapons(*psDroid) == 0) &&
      psDroid->getType() != DROID_TYPE::SENSOR) {
		return failure;
	}
	// Check if we have a CB target to begin with
	if (!proj_Direct(psDroid->getWeapons()[weapon_slot].getStats())) {
		WeaponStats* psWStats = psDroid->asWeaps[weapon_slot].nStat + asWeaponStats;

		bestTarget = aiSearchSensorTargets((SimpleObject*)psDroid, weapon_slot, psWStats, &tmpOrigin);
		bestMod = targetAttackWeight(bestTarget, (SimpleObject*)psDroid, weapon_slot);
	}

	weaponEffect = (asWeaponStats + psDroid->asWeaps[weapon_slot].nStat)->weaponEffect;

	electronic = electronicDroid(psDroid);

	// Range was previously 9*TILE_UNITS. Increasing this doesn't seem to help much, though. Not sure why.
	int droidRange = std::min(aiDroidRange(psDroid, weapon_slot) + extraRange,
	                          objSensorRange(psDroid) + 6 * TILE_UNITS);

	static GridList gridList; // static to avoid allocations.
	gridList = gridStartIterate(psDroid->getPosition().x, psDroid->getPosition().y, droidRange);
	for (auto targetInQuestion : gridList)
	{
		SimpleObject* friendlyObj = nullptr;
			/* This is a friendly unit, check if we can reuse its target */
		if (aiCheckAlliances(targetInQuestion->getPlayer(), psDroid->getPlayer()))
		{
			friendlyObj = targetInQuestion;
			targetInQuestion = nullptr;

			/* Can we see what it is doing? */
			if (friendlyObj->visible[psDroid->getPlayer()] == UBYTE_MAX) {
				if (auto friendlyDroid = dynamic_cast<Droid*>(friendlyObj)) {
					/* See if friendly droid has a target */
					tempTarget = friendlyDroid->action_target[0];
					if (tempTarget && !tempTarget->died) {
						//make sure a weapon droid is targeting it
						if (numWeapons(*friendlyDroid) > 0) {
							// make sure this target wasn't assigned explicitly to this droid
							if (friendlyDroid->getOrder().type != ORDER_TYPE::ATTACK) {
								targetInQuestion = tempTarget; //consider this target
							}
						}
					}
				}
				else if (auto friendlyStruct = dynamic_cast<Structure*>(friendlyObj))
				{
					tempTarget = friendlyStruct->psTarget[0];
					if (tempTarget && !tempTarget->died)
					{
						targetInQuestion = tempTarget;
					}
				}
			}
		}

		if (targetInQuestion != nullptr
			&& targetInQuestion != psDroid // in case friendly unit had me as target
			&& (targetInQuestion->type == OBJ_DROID || targetInQuestion->type == OBJ_STRUCTURE || targetInQuestion->type
				== OBJ_FEATURE)
			&& targetInQuestion->visible[psDroid->getPlayer()] == UBYTE_MAX
			&& !aiCheckAlliances(targetInQuestion->getPlayer(), psDroid->getPlayer())
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
							&& !isTransporter((Droid*)targetInQuestion)))
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
			else if (auto psStruct = dynamic_cast<Structure*>(targetInQuestion))
			{
				if (electronic) {
					/* don't want to target structures with resistance of zero if using electronic warfare */
					if (validStructResistance((Structure*)targetInQuestion)) {
						psTarget = targetInQuestion;
					}
				}
				else if (psStruct->asWeaps[0].nStat > 0) {
					// structure with weapons - go for this
					psTarget = targetInQuestion;
				}
				else if ((isHumanPlayer(psDroid->getPlayer()) &&
                 (psStruct->getStats().type != STRUCTURE_TYPE::WALL &&
                  psStruct->getStats().type != STRUCTURE_TYPE::WALL_CORNER))
					|| !isHumanPlayer(psDroid->getPlayer())) {
					psTarget = targetInQuestion;
				}
			}
			else if (targetInQuestion->type == OBJ_FEATURE
				&& psDroid->lastFrustratedTime > 0
				&& gameTime - psDroid->lastFrustratedTime < FRUSTRATED_TIME
				&& ((Feature*)targetInQuestion)->psStats->damageable
				&& psDroid->getPlayer() != scavengerPlayer()) // hack to avoid scavs blowing up their nice feature walls
			{
				psTarget = targetInQuestion;
				objTrace(psDroid->getId(), "considering shooting at %s in frustration", objInfo(targetInQuestion));
			}

			/* Check if our weapon is most effective against this object */
			if (psTarget != nullptr && psTarget == targetInQuestion) //was assigned?
			{
				int newMod = targetAttackWeight(psTarget, (SimpleObject*)psDroid, weapon_slot);

				/* Remember this one if it's our best target so far */
				if (newMod >= 0 &&
            (newMod > bestMod || bestTarget == nullptr)) {
					bestMod = newMod;
					tmpOrigin = TARGET_ORIGIN::ALLY;
					bestTarget = psTarget;
				}
			}
		}
	}

	if (bestTarget) {
		ASSERT(!bestTarget->died, "AI gave us a target that is already dead.");
		targetStructure = visGetBlockingWall(psDroid, bestTarget);

		// See if target is blocked by a wall; only affects direct weapons
		// Ignore friendly walls here
		if (proj_Direct(&psDroid->getWeapons()[weapon_slot].getStats()) && targetStructure &&
        !aiCheckAlliances(psDroid->getPlayer(), targetStructure->getPlayer())) {
			//are we any good against walls?
			if (asStructStrengthModifier[weaponEffect][targetStructure->getStats().strength] >=
				MIN_STRUCTURE_BLOCK_STRENGTH) {
				bestTarget = (SimpleObject*)targetStructure; //attack wall
			}
		}
		*ppsObj = bestTarget;
		return bestMod;
	}
	return failure;
}

//// Are there a lot of bullets heading towards the droid?
//static bool aiDroidIsProbablyDoomed(DROID *psDroid, bool isDirect)
//{
//	if (isDirect)
//	{
//		return psDroid->expectedDamageDirect > psDroid->body
//		      && psDroid->expectedDamageDirect - psDroid->body > psDroid->body / 5; // Doomed if projectiles will damage 120% of remaining body points.
//	}
//	else
//	{
//		return psDroid->expectedDamageIndirect > psDroid->body
//		      && psDroid->expectedDamageIndirect - psDroid->body > psDroid->body / 5; // Doomed if projectiles will damage 120% of remaining body points.
//	}
//}

//// Are there a lot of bullets heading towards the structure?
//static bool aiStructureIsProbablyDoomed(STRUCTURE *psStructure)
//{
//	return psStructure->expectedDamage > psStructure->body
//	       && psStructure->expectedDamage - psStructure->body > psStructure->body / 15; // Doomed if projectiles will damage 106.6666666667% of remaining body points.
//}
//
//// Are there a lot of bullets heading towards the object?
//bool aiObjectIsProbablyDoomed(SimpleObject *psObject, bool isDirect)
//{
//	if (psObject->died)
//	{
//		return true;    // Was definitely doomed.
//	}
//
//	switch (psObject->type)
//	{
//	case OBJ_DROID:
//		return aiDroidIsProbablyDoomed((DROID *)psObject, isDirect);
//	case OBJ_STRUCTURE:
//		return aiStructureIsProbablyDoomed((STRUCTURE *)psObject);
//	default:
//		return false;
//	}
//}

//// Update the expected damage of the object.
//void aiObjectAddExpectedDamage(SimpleObject *psObject, SDWORD damage, bool isDirect)
//{
//	if (psObject == nullptr)
//	{
//		return;    // Hard to destroy the ground.
//	}
//
//	switch (psObject->type)
//	{
//	case OBJ_DROID:
//		if (isDirect)
//		{
//			((DROID *)psObject)->expectedDamageDirect += damage;
//			ASSERT((SDWORD)((DROID *)psObject)->expectedDamageDirect >= 0, "aiObjectAddExpectedDamage: Negative amount of projectiles heading towards droid.");
//		}
//		else
//		{
//			((DROID *)psObject)->expectedDamageIndirect += damage;
//			ASSERT((SDWORD)((DROID *)psObject)->expectedDamageIndirect >= 0, "aiObjectAddExpectedDamage: Negative amount of projectiles heading towards droid.");
//		}
//		break;
//	case OBJ_STRUCTURE:
//		((STRUCTURE *)psObject)->expectedDamage += damage;
//		ASSERT((SDWORD)((STRUCTURE *)psObject)->expectedDamage >= 0, "aiObjectAddExpectedDamage: Negative amount of projectiles heading towards droid.");
//		break;
//	default:
//		break;
//	}
}

//// see if an object is a wall
//static bool aiObjIsWall(SimpleObject *psObj)
//{
//	if (psObj->type != OBJ_STRUCTURE)
//	{
//		return false;
//	}
//
//	if (((STRUCTURE *)psObj)->pStructureType->type != REF_WALL &&
//	    ((STRUCTURE *)psObj)->pStructureType->type != REF_WALLCORNER)
//	{
//		return false;
//	}
//
//	return true;
//}


/* See if there is a target in range */
bool aiChooseTarget(Unit* psObj, SimpleObject** ppsTarget, int weapon_slot,
                    bool bUpdateTarget, TARGET_ORIGIN* targetOrigin)
{
	SimpleObject* psTarget = nullptr;
	Droid* psCommander;
	SDWORD curTargetWeight = -1;
	TARGET_ORIGIN tmpOrigin = TARGET_ORIGIN::UNKNOWN;

	if (targetOrigin)
	{
		*targetOrigin = TARGET_ORIGIN::UNKNOWN;
	}

	ASSERT_OR_RETURN(false, (unsigned)weapon_slot < numWeapons(*psObj), "Invalid weapon selected");

	/* See if there is a something in range */
	if (psObj->type == OBJ_DROID) {
		auto psCurrTarget = ((Droid*)psObj)->action_target[0];

		/* find a new target */
		auto newTargetWeight = aiBestNearestTarget((Droid*)psObj, &psTarget, weapon_slot);

		/* Calculate weight of the current target if updating; but take care not to target
		 * ourselves... */
		if (bUpdateTarget && psCurrTarget != psObj)
		{
			curTargetWeight = targetAttackWeight(psCurrTarget, psObj, weapon_slot);
		}

		if (newTargetWeight >= 0 // found a new target
			&& (!bUpdateTarget // choosing a new target, don't care if current one is better
				|| curTargetWeight <= 0 // attacker had no valid target, use new one
				|| newTargetWeight > curTargetWeight + OLD_TARGET_THRESHOLD) // updating and new target is better
			&& validTarget(psObj, psTarget, weapon_slot)
			&& aiDroidHasRange((Droid*)psObj, psTarget, weapon_slot)) {
			ASSERT(!isDead(psTarget), "Droid found a dead target!");
			*ppsTarget = psTarget;
			return true;
		}
	}
	else if (psObj->type == OBJ_STRUCTURE) {
		bool bCommanderBlock = false;

		ASSERT_OR_RETURN(false, psObj->asWeaps[weapon_slot].nStat > 0, "Invalid weapon turret");

		WeaponStats* psWStats = psObj->asWeaps[weapon_slot].nStat + asWeaponStats;
		int longRange = proj_GetLongRange(psWStats, psObj->getPlayer());

		// see if there is a target from the command droids
		psTarget = nullptr;
		psCommander = cmdDroidGetDesignator(psObj->getPlayer());
		if (!proj_Direct(psWStats) && (psCommander != nullptr) &&
			aiStructHasRange((Structure*)psObj, (SimpleObject*)psCommander, weapon_slot))
		{
			// there is a commander that can fire designate for this structure
			// set bCommanderBlock so that the structure does not fire until the commander
			// has a target - (slow firing weapons will not be ready to fire otherwise).
			bCommanderBlock = true;

			// I do believe this will never happen, check for yourself :-)
			debug(LOG_NEVER, "Commander %d is good enough for fire designation", psCommander->getId());

			if (psCommander->getAction() == ACTION::ATTACK
				&& psCommander->action_target[0] != nullptr
				&& !psCommander->action_target[0]->died) {
				// the commander has a target to fire on
				if (aiStructHasRange((Structure*)psObj, psCommander->action_target[0], weapon_slot)) {
					// target in range - fire on it
					tmpOrigin = TARGET_ORIGIN::COMMANDER;
					psTarget = psCommander->action_target[0];
				}
				else {
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

			static GridList gridList; // static to avoid allocations.
			gridList = gridStartIterate(psObj->pos.x, psObj->pos.y, srange);
			for (auto psCurr : gridList)
			{
					/* Check that it is a valid target */
				if (psCurr->type != OBJ_FEATURE && !psCurr->died
					&& !aiCheckAlliances(psCurr->getPlayer(), psObj->getPlayer())
					&& validTarget(psObj, psCurr, weapon_slot) && psCurr->visible[psObj->getPlayer()] == UBYTE_MAX
					&& aiStructHasRange((Structure*)psObj, psCurr, weapon_slot))
				{
					int newTargetValue = targetAttackWeight(psCurr, psObj, weapon_slot);
					// See if in sensor range and visible
					int distSq = objPosDiffSq(psCurr->pos, psObj->pos);
					if (newTargetValue < targetValue || (newTargetValue == targetValue && distSq >= tarDist))
					{
						continue;
					}

					tmpOrigin = TARGET_ORIGIN::VISUAL;
					psTarget = psCurr;
					tarDist = distSq;
					targetValue = newTargetValue;
				}
			}
		}

		if (psTarget)
		{
			ASSERT(!psTarget->died, "Structure found a dead target!");
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
bool aiChooseSensorTarget(SimpleObject* psObj, SimpleObject** ppsTarget)
{
	int sensorRange = objSensorRange(psObj);
	unsigned int radSquared = sensorRange * sensorRange;
	bool radarDetector = objRadarDetector(psObj);

	if (!objActiveRadar(psObj) && !radarDetector)
	{
		ASSERT(false, "Only to be used for sensor turrets!");
		return false;
	}

	/* See if there is something in range */
	if (psObj->type == OBJ_DROID) {
		SimpleObject* psTarget = nullptr;

		if (aiBestNearestTarget((Droid*)psObj, &psTarget, 0) >= 0) {
			/* See if in sensor range */
			const auto xdiff = psTarget->getPosition().x - psObj->getPosition().x;
			const auto ydiff = psTarget->getPosition().y - psObj->getPosition().y;
			const auto distSq = xdiff * xdiff + ydiff * ydiff;

			// I do believe this will never happen, check for yourself :-)
			debug(LOG_NEVER, "Sensor droid(%d) found possible target(%d)!!!", psObj->getId(), psTarget->getId());

			if (distSq < radSquared) {
				*ppsTarget = psTarget;
				return true;
			}
		}
	}
	else // structure
	{
		SimpleObject* psTemp = nullptr;
		unsigned tarDist = UINT32_MAX;

		static GridList gridList; // static to avoid allocations.
		gridList = gridStartIterate(psObj->getPosition().x, psObj->getPosition().y, objSensorRange(psObj));
		for (auto psCurr : gridList)
		{
				// Don't target features or doomed/dead objects
			if (psCurr->type != OBJ_FEATURE && !psCurr->died && !aiObjectIsProbablyDoomed(psCurr, false))
			{
				if (!aiCheckAlliances(psCurr->getPlayer(), psObj->getPlayer()) && !aiObjIsWall(psCurr))
				{
					// See if in sensor range and visible
					const auto xdiff = psCurr->getPosition().x - psObj->getPosition().x;
					const auto ydiff = psCurr->getPosition().y - psObj->getPosition().y;
					const unsigned auto distSq = xdiff * xdiff + ydiff * ydiff;

					if (distSq < radSquared && psCurr->visible[psObj->getPlayer()] == UBYTE_MAX && distSq < tarDist)
					{
						psTemp = psCurr;
						tarDist = distSq;
					}
				}
			}
		}

		if (psTemp)
		{
			ASSERT(!psTemp->died, "aiChooseSensorTarget gave us a dead target");
			*ppsTarget = psTemp;
			return true;
		}
	}

	return false;
}

/* Make droid/structure look for a better target */
static bool updateAttackTarget(SimpleObject* psAttacker, int weapon_slot)
{
	SimpleObject* psBetterTarget = nullptr;
	TARGET_ORIGIN tmpOrigin = TARGET_ORIGIN::UNKNOWN;

	if (aiChooseTarget(psAttacker, &psBetterTarget, weapon_slot, true, &tmpOrigin)) //update target
	{
		if (auto psDroid = dynamic_cast<Droid*>(psAttacker)) {
			if ((orderState(psDroid, ORDER_TYPE::NONE) ||
					orderState(psDroid, ORDER_TYPE::GUARD) ||
					orderState(psDroid, ORDER_TYPE::ATTACK_TARGET)) &&
				weapon_slot == 0) {
				actionDroid((Droid*)psAttacker, ACTION::ATTACK, psBetterTarget);
			}
			else //can't override current order
			{
				setDroidActionTarget(psDroid, psBetterTarget, weapon_slot);
			}
		}
		else if (auto psBuilding = dynamic_cast<Structure*>(psAttacker)) {
			::setStructureTarget(psBuilding, psBetterTarget, weapon_slot, tmpOrigin);
		}

		return true;
	}
	return false;
}


/* Check if any of our weapons can hit the target... */
bool checkAnyWeaponsTarget(SimpleObject* psObject, SimpleObject* psTarget)
{
	auto psDroid = dynamic_cast<Droid*>(psObject);
	for (int i = 0; i < numWeapons(*psDroid); i++)
	{
		if (validTarget(psObject, psTarget, i)) {
			return true;
		}
	}
	return false;
}

/* Set of rules which determine whether the weapon associated with the object can fire on the propulsion type of the target. */
bool validTarget(SimpleObject* psObject, SimpleObject* psTarget, int weapon_slot)
{
	bool bTargetInAir = false, bValidTarget = false;
	uint8_t surfaceToAir = 0;

	if (!psTarget) {
		return false;
	}

	// Need to check propulsion type of target
	if (auto psDroid = dynamic_cast<Droid*>(psTarget)) {
    if (asPropulsionTypes[asPropulsionStats[((Droid *) psTarget)->asBits[COMP_PROPULSION]].propulsionType].travel ==
        TRAVEL_MEDIUM::AIR) {
      if (psDroid->getMovementData().status != MOVE_STATUS::INACTIVE) {
        bTargetInAir = true;
      } else {
        bTargetInAir = false;
      }
    } else {
      bTargetInAir = false;
    }
  }
	else if (dynamic_cast<Structure*>(psTarget)) {
		// let's hope so!
		bTargetInAir = false;
	}

	// need what can shoot at
	if (auto psDroid= dynamic_cast<Droid*>(psObject)) {
    if (psDroid->getType() == DROID_TYPE::SENSOR) {
      return !bTargetInAir; // Sensor droids should not target anything in the air.
    }

    // Can't attack without a weapon
    if (numWeapons(*psDroid) != 0) {
      surfaceToAir = asWeaponStats[((Droid *) psObject)->asWeaps[weapon_slot].nStat].surfaceToAir;
      if (((surfaceToAir & SHOOT_IN_AIR) && bTargetInAir) || ((surfaceToAir & SHOOT_ON_GROUND) && !bTargetInAir)) {
        return true;
      }
    } else {
      return false;
    }
  }
	else if (auto psStruct = dynamic_cast<Structure*>(psObject)) {
    // Can't attack without a weapon
    if (numWeapons(*psStruct) != 0 && ((Structure *) psObject)->asWeaps[weapon_slot].nStat != 0) {
      surfaceToAir = asWeaponStats[((Structure *) psObject)->asWeaps[weapon_slot].nStat].surfaceToAir;
    }
    else {
      surfaceToAir = 0;
    }

    if (((surfaceToAir & SHOOT_IN_AIR) && bTargetInAir) || ((surfaceToAir & SHOOT_ON_GROUND) && !bTargetInAir)) {
      return true;
    }
  }
  else {
    surfaceToAir = 0;
  }

	//if target is in the air and you can shoot in the air - OK
	if (bTargetInAir && (surfaceToAir & SHOOT_IN_AIR)) {
		bValidTarget = true;
	}

	//if target is on the ground and can shoot at it - OK
	if (!bTargetInAir && (surfaceToAir & SHOOT_ON_GROUND)) {
		bValidTarget = true;
	}

	return bValidTarget;
}
