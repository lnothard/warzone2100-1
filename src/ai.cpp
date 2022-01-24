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

#include "action.h"
#include "ai.h"
#include "move.h"
#include "objmem.h"
#include "projectile.h"
#include "structure.h"

bool bMultiPlayer;
bool isHumanPlayer(unsigned);
typedef std::vector<BaseObject *> GridList;
Droid* cmdDroidGetDesignator(unsigned);
const char* getPlayerName(unsigned);
const GridList& gridStartIterate(int, int, unsigned);
bool lineOfFire(const BaseObject *, const BaseObject *, int, bool);
const char* objInfo(const BaseObject *);
unsigned objSensorRange(BaseObject *);
int scavengerPlayer();
Structure* visGetBlockingWall(const BaseObject *, const BaseObject *);
int visibleObject(const BaseObject *, const BaseObject *, bool);


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

static unsigned aiDroidRange(Droid *psDroid, int weapon_slot)
{
	unsigned longRange;
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
static bool aiStructHasRange(Structure *psStruct, BaseObject *psTarget, int weapon_slot)
{
	if (numWeapons(*psStruct) == 0) {
		// can't attack without a weapon
		return false;
	}

	auto& psWStats = psStruct->getWeapons()[weapon_slot].getStats();

	auto longRange = proj_GetLongRange(&psWStats, psStruct->getPlayer());
	return objectPositionSquareDiff(
          psStruct->getPosition(),
          psTarget->getPosition()) < longRange * longRange &&
          lineOfFire(psStruct, psTarget, weapon_slot, true);
}

static bool aiDroidHasRange(Droid *psDroid, BaseObject *psTarget, int weapon_slot)
{
	auto longRange = aiDroidRange(psDroid, weapon_slot);
	return objectPositionSquareDiff(psDroid->getPosition(),
                                  psTarget->getPosition()) < longRange * longRange;
}

static bool aiObjHasRange(BaseObject *psObj, BaseObject *psTarget, int weapon_slot)
{
	if (auto psDroid = dynamic_cast<Droid*>(psObj)) {
		return aiDroidHasRange(psDroid, psTarget, weapon_slot);
	}
	else if (auto psStruct = dynamic_cast<Structure*>(psObj)) {
		return aiStructHasRange(
            dynamic_cast<Structure*>(psObj),
            psTarget, weapon_slot);
	}
	return false;
}

/* Initialise the AI system */
bool aiInitialise()
{
  using enum ALLIANCE_TYPE;
	for (auto i = 0; i < MAX_PLAYER_SLOTS; i++)
	{
		alliancebits[i] = 0;
		for (auto j = 0; j < MAX_PLAYER_SLOTS; j++)
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
static BaseObject * aiSearchSensorTargets(BaseObject * psObj, int weapon_slot,
																							 WeaponStats* psWStats, TARGET_ORIGIN* targetOrigin)
{
	auto longRange = proj_GetLongRange(psWStats, psObj->getPlayer());
	auto tarDist = longRange * longRange;
	auto foundCB = false;
	auto minDist = proj_GetMinRange(psWStats, psObj->getPlayer()) *
                 proj_GetMinRange(psWStats, psObj->getPlayer());
  BaseObject * psTarget = nullptr;

	if (targetOrigin) {
		*targetOrigin = TARGET_ORIGIN::UNKNOWN;
	}

	for (auto psSensor : apsSensorList)
	{
    BaseObject * psTemp = nullptr;
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
				objRadarDetector((BaseObject *)psDroid))
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
			if (psCStruct->getStats().sensor_stats->type == SENSOR_TYPE::VTOL_CB ||
          psCStruct->getStats().sensor_stats->type == SENSOR_TYPE::VTOL_INTERCEPT ||
          objRadarDetector((BaseObject *)psCStruct)) {
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
    if (!(isCB > foundCB ||
        (isCB == foundCB && distSq < tarDist)) || 
        distSq <= minDist) {
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
static int targetAttackWeight(BaseObject * psTarget, BaseObject * psAttacker, int weapon_slot)
{
  Droid* psAttackerDroid;
	int targetTypeBonus = 0, damageRatio = 0, attackWeight = 0, noTarget = -1;
	unsigned weaponSlot;
	WEAPON_EFFECT weaponEffect;
	WeaponStats* attackerWeapon;
	auto bEmpWeap = false, bCmdAttached = false, bTargetingCmd = false, bDirect = false;

	if (psTarget == nullptr || psAttacker == nullptr ||
      psTarget->died) {
		return noTarget;
	}
	ASSERT(psTarget != psAttacker, "targetAttackWeight: Wanted to evaluate "
                                 "the worth of attacking ourselves...");

  // sensors/ecm droids, non-military structures get lower priority
	targetTypeBonus = 0;

	/* Get attacker weapon effect */
	if ((psAttackerDroid = dynamic_cast<Droid*>(psAttacker))) {
		attackerWeapon = psAttackerDroid->getWeapons()[weapon_slot].getStats();

		// check if this droid is assigned to a commander
		bCmdAttached = psAttackerDroid->hasCommander();

		// find out if current target is targeting our commander
		if (bCmdAttached) {
			if (auto psDroid = dynamic_cast<Droid*>(psTarget)) {
				//go through all enemy weapon slots
				for (weaponSlot = 0; !bTargetingCmd &&
                             weaponSlot < numWeapons(*psDroid); weaponSlot++)
				{
					// see if this weapon is targeting our commander
					if (&psDroid->getTarget(weaponSlot) == 
              psAttackerDroid->getGroup()->getCommander()) {
						bTargetingCmd = true;
					}
				}
			}
			else {
				if (auto psStruct = dynamic_cast<Structure*>(psTarget)) {
					// go through all enemy weapons
					for (weaponSlot = 0; !bTargetingCmd &&
                  weaponSlot < numWeapons(*psStruct); weaponSlot++)
					{
						if (psStruct->getTarget(weaponSlot) ==
                    psAttackerDroid->getGroup()->getCommander()) {
							bTargetingCmd = true;
						}
					}
				}
			}
		}
	}
	else if (auto psStruct = dynamic_cast<Structure*>(psAttacker)) {
		attackerWeapon = psStruct->getWeapons()[weapon_slot].getStats();
	}
	else {
  /* feature */
		ASSERT(!"invalid attacker object type", "targetAttackWeight: Invalid attacker object type");
		return noTarget;
	}
	bDirect = proj_Direct(attackerWeapon);

	if (dynamic_cast<Droid*>(psAttacker) &&
      psAttackerDroid->getType() == DROID_TYPE::SENSOR) {
		// sensors are considered a direct weapon, but for
    // computing expected damage it makes more sense
    // to use indirect damage
		bDirect = false;
	}

	// get weapon effect
	weaponEffect = attackerWeapon->weaponEffect;

	// see if attacker is using an EMP weapon
	bEmpWeap = (attackerWeapon->weaponSubClass == WEAPON_SUBCLASS::EMP);

	auto dist = iHypot((
          psAttacker->getPosition() - psTarget->getPosition()).xy());

	bool tooClose = (unsigned)dist <= proj_GetMinRange(
          attackerWeapon, psAttacker->getPlayer());

	if (tooClose) {
    // if object is too close to fire at, consider it to be at maximum range
		dist = objSensorRange(psAttacker);
	}

	/* Calculate attack weight */
	if (auto targetDroid = dynamic_cast<Droid*>(psTarget)) {
		if (targetDroid->isDead()) {
			debug(LOG_NEVER, "Target droid is dead, skipping invalid droid.\n");
			return noTarget;
		}

		/* Calculate damage this target suffered */
    // FIXME Somewhere we get 0HP droids from
		if (targetDroid->getOriginalHp() == 0) {
			damageRatio = 0;
			debug(LOG_ERROR, "targetAttackWeight: 0HP droid detected!");
			debug(LOG_ERROR, "  Type: %i Name: \"%s\" Owner: %i \"%s\")",
            targetDroid->getType(), targetDroid->getName().c_str(), 
            targetDroid->getPlayer(), getPlayerName(targetDroid->getPlayer()));
		}
		else {
			damageRatio = 100 - 100 * targetDroid->getHp() / targetDroid->getOriginalHp();
		}
		assert(targetDroid->getOriginalHp() != 0); // Assert later so we get the info from above

		/* See if this type of droid should be prioritized */
		switch (targetDroid->getType()) {
      using enum DROID_TYPE;
	  	case SENSOR:
	  	case ECM:
	  	case PERSON:
	  	case TRANSPORTER:
	  	case SUPER_TRANSPORTER:
	  	case DEFAULT:
	  	case ANY:
	  		break;
	  	case CYBORG:
	  	case WEAPON:
	  	case CYBORG_SUPER:
	  		targetTypeBonus = WEIGHT_WEAPON_DROIDS;
	  		break;
	  	case COMMAND:
	  		targetTypeBonus = WEIGHT_COMMAND_DROIDS;
	  		break;
	  	case CONSTRUCT:
	  	case REPAIRER:
	  	case CYBORG_CONSTRUCT:
	  	case CYBORG_REPAIR:
	  		targetTypeBonus = WEIGHT_SERVICE_DROIDS;
	  		break;
		}

		/* Now calculate the overall weight */
		attackWeight = asWeaponModifier[(int)weaponEffect][targetDroid->getPropulsion()->propulsionType] // Our weapon's effect against target
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
	else if (auto targetStructure = dynamic_cast<Structure*>(psTarget)) {
		/* Calculate damage this target suffered */
		damageRatio = 100 - 100 * targetStructure->getHp() / structureBody(targetStructure);

		/* See if this type of a structure should be prioritized */
		switch (targetStructure->getStats()->type) {
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
		attackWeight = asStructStrengthModifier[weaponEffect][targetStructure->getStats()->strength]
			// Our weapon's effect against target
			+ WEIGHT_DIST_TILE_STRUCT * objSensorRange(psAttacker) / TILE_UNITS
			- WEIGHT_DIST_TILE_STRUCT * dist / TILE_UNITS // farther structs are less attractive
			+ WEIGHT_HEALTH_STRUCT * damageRatio / 100 // we prefer damaged structures
			+ targetTypeBonus; // some structure types have higher priority

		// go for unfinished structures only if nothing else found (same
    // for non-visible structures)
		if (targetStructure->getState() != STRUCTURE_STATE::BUILT) {
      //a decoy?
			attackWeight /= WEIGHT_STRUCT_NOTBUILT_F;
		}

		// EMP should only attack structures if no enemy droids are around
		if (bEmpWeap) {
			attackWeight /= EMP_STRUCT_PENALTY_F;
		}
	}
	else {
    // a feature
		return 1;
	}

	/* We prefer objects we can see and can attack immediately */
	if (!visibleObject(psAttacker, psTarget, true)) {
		attackWeight /= WEIGHT_NOT_VISIBLE_F;
	}

	if (tooClose) {
		attackWeight /= TOO_CLOSE_PENALTY_F;
	}

	/* Penalty for units that are already considered doomed (but the missile might miss!) */
	if (aiObjectIsProbablyDoomed(psTarget, bDirect)) {
		/* indirect firing units have slow reload times, so give the target a chance to die,
		 * and give a different unit a chance to get in range, too. */
		if (weaponROF(attackerWeapon,
                  psAttacker->getPlayer()) < TARGET_DOOMED_SLOW_RELOAD_T) {
			debug(LOG_NEVER, "Not killing unit - doomed. My ROF: %i (%s)",
            weaponROF(attackerWeapon, psAttacker->getPlayer()), getStatsName(attackerWeapon));
			return noTarget;
		}
		attackWeight /= TARGET_DOOMED_PENALTY_F;
	}

  // attached to a commander and don't have a target assigned by some order
  if (!bCmdAttached) {
    return std::max<int>(1, attackWeight);
  }
  ASSERT(psAttackerDroid->group->psCommander != nullptr, "Commander is NULL");

  // if commander is being targeted by our target, try to defend the commander
  if (bTargetingCmd) {
    attackWeight += WEIGHT_CMD_RANK * (1 + getDroidLevel(
            psAttackerDroid->group->psCommander));
  }

  // fire support - go through all droids assigned to the commander
  for (auto psGroupDroid = psAttackerDroid->group->members;
       psGroupDroid; psGroupDroid = psGroupDroid->psGrpNext)
  {
    for (weaponSlot = 0; weaponSlot < psGroupDroid->numWeaps; weaponSlot++)
    {
      // see if this droid is currently targeting current target
      if (psGroupDroid->getOrder().target == psTarget ||
              psGroupDroid->getTarget(weaponSlot) == psTarget) {
        // we prefer targets that are already targeted and
        // hence will be destroyed faster
        attackWeight += WEIGHT_CMD_SAME_TARGET;
      }
    }
  }
  return std::max<int>(1, attackWeight);
}


// Find the best nearest target for a droid.
// If extraRange is higher than zero, then this is the range it accepts for movement to target.
// Returns integer representing target priority, -1 if failed
int aiBestNearestTarget(Droid* psDroid, BaseObject ** ppsObj, int weapon_slot, int extraRange)
{
	int failure = -1;
	int bestMod = 0;
  BaseObject *psTarget = nullptr, *bestTarget = nullptr, *tempTarget;
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
	if (numWeapons(*psDroid) == 0 &&
      psDroid->getType() != DROID_TYPE::SENSOR) {
		return failure;
	}
	// Check if we have a CB target to begin with
	if (!proj_Direct(&psDroid->getWeapons()[weapon_slot].getStats())) {
		auto& psWStats = psDroid->getWeapons()[weapon_slot].getStats();

		bestTarget = aiSearchSensorTargets(psDroid, weapon_slot, psWStats, &tmpOrigin);
		bestMod = targetAttackWeight(bestTarget, (BaseObject *)psDroid, weapon_slot);
	}

	weaponEffect = psDroid->getWeapons()[weapon_slot].getStats().weaponEffect;

	electronic = electronicDroid(psDroid);

	// Range was previously 9*TILE_UNITS. Increasing this doesn't seem to help much, though. Not sure why.
	auto droidRange = std::min(aiDroidRange(psDroid, weapon_slot) + extraRange,
                             objSensorRange(psDroid) + 6 * TILE_UNITS);

	static GridList gridList; // static to avoid allocations.
	gridList = gridStartIterate(psDroid->getPosition().x, psDroid->getPosition().y, droidRange);
	for (auto targetInQuestion : gridList)
	{
    BaseObject * friendlyObj = nullptr;
			/* This is a friendly unit, check if we can reuse its target */
		if (aiCheckAlliances(targetInQuestion->getPlayer(), psDroid->getPlayer())) {
			friendlyObj = targetInQuestion;
			targetInQuestion = nullptr;

			/* Can we see what it is doing? */
			if (friendlyObj->visibilityState[psDroid->getPlayer()] == UBYTE_MAX) {
				if (auto friendlyDroid = dynamic_cast<Droid*>(friendlyObj)) {
					/* See if friendly droid has a target */
					tempTarget = friendlyDroid->actionTarget[0];
					if (tempTarget && !tempTarget->died) {
						//make sure a weapon droid is targeting it
						if (numWeapons(*friendlyDroid) > 0) {
							// make sure this target wasn't assigned explicitly to this droid
							if (friendlyDroid->getOrder()->type != ORDER_TYPE::ATTACK) {
								targetInQuestion = tempTarget; //consider this target
							}
						}
					}
				}
				else if (auto friendlyStruct = dynamic_cast<Structure*>(friendlyObj)) {
					tempTarget = friendlyStruct->psTarget[0];
					if (tempTarget && !tempTarget->died) {
						targetInQuestion = tempTarget;
					}
				}
			}
		}

		if (targetInQuestion &&
        targetInQuestion != psDroid && //< in case friendly unit had me as target
        (dynamic_cast<Droid*>(targetInQuestion) ||
         dynamic_cast<Structure*>(targetInQuestion) ||
         dynamic_cast<Feature*>(targetInQuestion) &&
        targetInQuestion->visible[psDroid->getPlayer()] == UBYTE_MAX &&
        !aiCheckAlliances(targetInQuestion->getPlayer(), psDroid->getPlayer()) &&
        validTarget(psDroid, targetInQuestion, weapon_slot) &&
        objectPositionSquareDiff(psDroid, targetInQuestion) < droidRange * droidRange)) {

			if (auto droid = dynamic_cast<Droid*>(targetInQuestion)) {
				// don't attack transporters with EW in multiplayer
				if (bMultiPlayer) {
					// if not electronic then valid target
					if (!electronic ||
              !isTransporter(*droid)) {
						// only a valid target if NOT a transporter
						psTarget = targetInQuestion;
					}
				}
				else {
					psTarget = targetInQuestion;
				}
			}
			else if (auto psStruct = dynamic_cast<Structure*>(targetInQuestion)) {
				if (electronic) {
					// don't want to target structures with resistance of zero
          // if using electronic warfare
					if (validStructResistance(psStruct)) {
						psTarget = targetInQuestion;
					}
				}
				else if (numWeapons(*psStruct) > 0) {
					// structure with weapons - go for this
					psTarget = targetInQuestion;
				}
				else if ((isHumanPlayer(psDroid->getPlayer()) &&
                  (psStruct->getStats()->type != STRUCTURE_TYPE::WALL &&
                   psStruct->getStats()->type != STRUCTURE_TYPE::WALL_CORNER)) ||
                 !isHumanPlayer(psDroid->getPlayer())) {
					psTarget = targetInQuestion;
				}
			}
			else if (dynamic_cast<Feature*>(targetInQuestion) &&
               psDroid->lastFrustratedTime > 0 &&
               gameTime - psDroid->lastFrustratedTime < FRUSTRATED_TIME &&
               dynamic_cast<Feature*>(targetInQuestion)->getStats()->damageable &&
               psDroid->getPlayer() != scavengerPlayer()) { //< hack to avoid scavs blowing up their nice feature walls

				psTarget = targetInQuestion;
				objTrace(psDroid->getId(),
                 "considering shooting at %s in frustration",
                 objInfo(targetInQuestion));
			}

			// check if our weapon is most effective against this object
			if (psTarget != nullptr && psTarget == targetInQuestion) { //< was assigned?
				auto newMod = targetAttackWeight(psTarget, (BaseObject *)psDroid, weapon_slot);

				// remember this one if it's our best target so far
				if (newMod >= 0 &&
            (newMod > bestMod || !bestTarget)) {
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
			if (asStructStrengthModifier[weaponEffect][targetStructure->getStats()->strength] >=
				MIN_STRUCTURE_BLOCK_STRENGTH) {
				bestTarget = (BaseObject *)targetStructure; //attack wall
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

/* See if there is a target in range */
bool aiChooseTarget(BaseObject* psObj, BaseObject** ppsTarget, int weapon_slot,
										bool bUpdateTarget, TARGET_ORIGIN* targetOrigin)
{
  BaseObject * psTarget = nullptr;
	Droid* psCommander;
	SDWORD curTargetWeight = -1;
	TARGET_ORIGIN tmpOrigin = TARGET_ORIGIN::UNKNOWN;

	if (targetOrigin) {
		*targetOrigin = TARGET_ORIGIN::UNKNOWN;
	}

	ASSERT_OR_RETURN(false, (unsigned)weapon_slot < numWeapons(*psObj), "Invalid weapon selected");

	/* See if there is a something in range */
	if (auto droid = dynamic_cast<Droid*>(psObj)) {
		auto psCurrTarget = droid->action_target[0];

		/* find a new target */
		auto newTargetWeight = aiBestNearestTarget(droid, &psTarget, weapon_slot);

		/* Calculate weight of the current target if updating; but take care not to target
		 * ourselves... */
		if (bUpdateTarget && psCurrTarget != psObj) {
			curTargetWeight = targetAttackWeight(psCurrTarget, psObj, weapon_slot);
		}

		if (newTargetWeight >= 0 // found a new target
			&& (!bUpdateTarget // choosing a new target, don't care if current one is better
				|| curTargetWeight <= 0 // attacker had no valid target, use new one
				|| newTargetWeight > curTargetWeight + OLD_TARGET_THRESHOLD) // updating and new target is better
			&& validTarget(psObj, psTarget, weapon_slot)
			&& aiDroidHasRange(droid, psTarget, weapon_slot)) {
			ASSERT(!isDead(psTarget), "Droid found a dead target!");
			*ppsTarget = psTarget;
			return true;
		}
	}
	else if (auto structure = dynamic_cast<Structure*>(psObj)) {
		bool bCommanderBlock = false;

		auto& psWStats = psObj->getWeapons()[weapon_slot].getStats();
		auto longRange = proj_GetLongRange(psWStats, psObj->getPlayer());

		// see if there is a target from the command droids
		psTarget = nullptr;
		psCommander = cmdDroidGetDesignator(psObj->getPlayer());
		if (!proj_Direct(psWStats) && (psCommander != nullptr) &&
			aiStructHasRange(structure, (BaseObject *)psCommander, weapon_slot)) {
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
				if (aiStructHasRange(structure, psCommander->action_target[0], weapon_slot)) {
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
		if (!psTarget && !bCommanderBlock && !proj_Direct(psWStats)) {
			psTarget = aiSearchSensorTargets(psObj, weapon_slot, psWStats, &tmpOrigin);
		}

		if (!psTarget && !bCommanderBlock) {
			auto targetValue = -1;
			auto tarDist = INT32_MAX;
			auto srange = longRange;

			if (!proj_Direct(psWStats) && srange > objSensorRange(psObj)) {
				// search radius of indirect weapons limited by their sight, unless they use
				// external sensors to provide fire designation
				srange = objSensorRange(psObj);
			}

			static GridList gridList; // static to avoid allocations.
			gridList = gridStartIterate(psObj->getPosition().x, psObj->getPosition().y, srange);
			for (auto psCurr : gridList)
			{
			  /* Check that it is a valid target */
				if (!dynamic_cast<Feature*>(psCurr) && !psCurr->died &&
            !aiCheckAlliances(psCurr->getPlayer(), psObj->getPlayer()) &&
            validTarget(psObj, psCurr, weapon_slot) &&
            psCurr->visible[psObj->getPlayer()] == UBYTE_MAX &&
            aiStructHasRange(structure, psCurr, weapon_slot)) {

					auto newTargetValue = targetAttackWeight(psCurr, psObj, weapon_slot);
					// See if in sensor range and visible
					auto distSq = objectPositionSquareDiff(psCurr->getPosition(), psObj->getPosition());
					if (newTargetValue < targetValue ||
              (newTargetValue == targetValue && distSq >= tarDist)) {
						continue;
					}

					tmpOrigin = TARGET_ORIGIN::VISUAL;
					psTarget = psCurr;
					tarDist = distSq;
					targetValue = newTargetValue;
				}
			}
		}

		if (psTarget) {
			ASSERT(!psTarget->died, "Structure found a dead target!");
			if (targetOrigin)  {
				*targetOrigin = tmpOrigin;
			}
			*ppsTarget = psTarget;
			return true;
		}
	}
	return false;
}

/// See if there is a target in range for sensor objects
bool aiChooseSensorTarget(BaseObject * psObj, BaseObject ** ppsTarget)
{
	int sensorRange = objSensorRange(psObj);
	unsigned int radSquared = sensorRange * sensorRange;
	bool radarDetector = objRadarDetector(psObj);

	if (!objActiveRadar(psObj) && !radarDetector) {
		ASSERT(false, "Only to be used for sensor turrets!");
		return false;
	}

	/* See if there is something in range */
	if (auto droid = dynamic_cast<Droid*>(psObj)) {
    BaseObject * psTarget = nullptr;

		if (aiBestNearestTarget(droid, &psTarget, 0) >= 0) {
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
	else {
  // structure
  BaseObject * psTemp = nullptr;
		unsigned tarDist = UINT32_MAX;

		static GridList gridList; // static to save on allocations.
		gridList = gridStartIterate(psObj->getPosition().x, psObj->getPosition().y, objSensorRange(psObj));
		for (auto psCurr : gridList)
		{
			// don't target features or doomed/dead objects
      if (dynamic_cast<Feature*>(psCurr) || psCurr->died || aiObjectIsProbablyDoomed(psCurr, false)) {
        continue;
      }

      if (aiCheckAlliances(psCurr->getPlayer(), psObj->getPlayer()) ||
          (dynamic_cast<Structure*>(psCurr) &&
           dynamic_cast<Structure*>(psCurr)->isWall())) {
        continue;
      }

      // see if in sensor range and visible
      const auto xdiff1 = psCurr->getPosition().x - psObj->getPosition().x;
      const auto ydiff1 = psCurr->getPosition().y - psObj->getPosition().y;
      const auto distSq1 = xdiff1 * xdiff1 + ydiff1 * ydiff1;

      if (distSq1 < radSquared &&
          psCurr->visible[psObj->getPlayer()] == UBYTE_MAX &&
          distSq1 < tarDist) {
        psTemp = psCurr;
        tarDist = distSq1;
      }
    }

		if (psTemp) {
			ASSERT(!psTemp->died, "aiChooseSensorTarget gave us a dead target");
			*ppsTarget = psTemp;
			return true;
		}
	}
	return false;
}

/// Make droid/structure look for a better target
static bool updateAttackTarget(BaseObject * psAttacker, int weapon_slot)
{
  BaseObject * psBetterTarget = nullptr;
	TARGET_ORIGIN tmpOrigin = TARGET_ORIGIN::UNKNOWN;

	if (aiChooseTarget(psAttacker, &psBetterTarget, weapon_slot,
                     true, &tmpOrigin)) {
    // update target
		if (auto psDroid = dynamic_cast<Droid*>(psAttacker)) {
			if ((orderState(psDroid, ORDER_TYPE::NONE) ||
					 orderState(psDroid, ORDER_TYPE::GUARD) ||
					 orderState(psDroid, ORDER_TYPE::ATTACK_TARGET)) &&
				  weapon_slot == 0) {
				actionDroid(psDroid, ACTION::ATTACK, psBetterTarget);
			}
      // can't override current order
			else {
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

/// Check if any of our weapons can hit the target
bool checkAnyWeaponsTarget(BaseObject const* psObject, BaseObject const* psTarget)
{
	auto psDroid = dynamic_cast<Droid const*>(psObject);
	for (int i = 0; i < numWeapons(*psDroid); i++)
	{
		if (validTarget(psObject, psTarget, i)) {
			return true;
		}
	}
	return false;
}

/**
 * Set of rules which determine whether the weapon associated with
 * the object can fire on the propulsion type of the target
 */
bool validTarget(BaseObject const* psObject, BaseObject const* psTarget, int weapon_slot)
{
	bool bTargetInAir = false, bValidTarget = false;
	uint8_t surfaceToAir = 0;

	if (!psTarget) {
		return false;
	}

	// need to check the propulsion type of target
	if (auto psDroid = dynamic_cast<Droid const*>(psTarget)) {
    auto propulsion = dynamic_cast<PropulsionStats const*>(psDroid->getComponent("propulsion"));
    if (propulsion != nullptr &&
        asPropulsionTypes[static_cast<size_t>(propulsion->propulsionType)].travel == TRAVEL_MEDIUM::AIR) {
      if (psDroid->getMovementData()->status != MOVE_STATUS::INACTIVE) {
        bTargetInAir = true;
      }
      else {
        bTargetInAir = false;
      }
    }
    else {
      bTargetInAir = false;
    }
  }
	else if (dynamic_cast<Structure const*>(psTarget)) {
		// let's hope so!
		bTargetInAir = false;
	}

	// need what can shoot at
	if (auto psDroid= dynamic_cast<Droid const*>(psObject)) {
    if (psDroid->getType() == DROID_TYPE::SENSOR) {
      return !bTargetInAir; // Sensor droids should not target anything in the air.
    }

    // can't attack without a weapon
    if (numWeapons(*psDroid) != 0) {
      surfaceToAir = psDroid->getWeapons()[weapon_slot].getStats().surfaceToAir;
      if (((surfaceToAir & SHOOT_IN_AIR) && bTargetInAir) ||
          ((surfaceToAir & SHOOT_ON_GROUND) && !bTargetInAir)) {
        return true;
      }
    }
    else {
      return false;
    }
  }
	else if (auto psStruct = dynamic_cast<Structure const*>(psObject)) {
    // can't attack without a weapon
    if (numWeapons(*psStruct) != 0) {
      surfaceToAir = psStruct->getWeapons()[weapon_slot].getStats().surfaceToAir;
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

	// if target is in the air, and you can shoot in the air - OK
	if (bTargetInAir && (surfaceToAir & SHOOT_IN_AIR)) {
		bValidTarget = true;
	}

	// if target is on the ground and can shoot at it - OK
	if (!bTargetInAir && (surfaceToAir & SHOOT_ON_GROUND)) {
		bValidTarget = true;
	}

	return bValidTarget;
}
