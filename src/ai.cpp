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
const char* objInfo(const BaseObject *);
int scavengerPlayer();


unsigned aiDroidRange(Droid const* psDroid, int weapon_slot)
{
  ASSERT_OR_RETURN(0, numWeapons(*psDroid) > 0, "No attack range for unarmed unit");

	if (psDroid->getType() == DROID_TYPE::SENSOR) {
		return objSensorRange(psDroid);
	}

  auto psWStats = psDroid->weaponManager->weapons[weapon_slot].stats.get();
  return proj_GetLongRange(psWStats, psDroid->playerManager->getPlayer());
}

// see if a structure has the range to fire on a target
static bool aiStructHasRange(Structure const* psStruct, BaseObject const* psTarget, int weapon_slot)
{
  ASSERT_OR_RETURN(false, numWeapons(*psStruct) > 0, "No attack range for defenseless building");

	auto const psWStats = psStruct->weaponManager->weapons[weapon_slot].stats;
	auto const longRange = proj_GetLongRange(
          psWStats.get(), psStruct->playerManager->getPlayer());

  return objectPositionSquareDiff(
          psStruct->getPosition(),
          psTarget->getPosition()) < longRange * longRange &&
         lineOfFire(psStruct, psTarget, weapon_slot, true);
}

static bool aiDroidHasRange(Droid const* psDroid, BaseObject const* psTarget, int weapon_slot)
{
	auto const longRange = aiDroidRange(psDroid, weapon_slot);
  return objectPositionSquareDiff(psDroid->getPosition(), psTarget->getPosition())
         < longRange * longRange;
}

static bool aiObjHasRange(BaseObject const* psObj, BaseObject const* psTarget, int weapon_slot)
{
	if (auto psDroid = dynamic_cast<Droid const*>(psObj)) {
		return aiDroidHasRange(psDroid, psTarget, weapon_slot);
	}
	if (auto psStruct = dynamic_cast<Structure const*>(psObj)) {
    return aiStructHasRange(psStruct, psTarget, weapon_slot);
	}
	return false;
}

void aiInitialise()
{
  using enum ALLIANCE_TYPE;
	for (auto i = 0; i < MAX_PLAYER_SLOTS; i++)
	{
		alliancebits[i] = 0;
		for (auto j = 0; j < MAX_PLAYER_SLOTS; j++)
		{
			bool valid = i == j && i < MAX_PLAYERS;

			alliances[i][j] = valid ? ALLIANCE_FORMED : ALLIANCE_BROKEN;
			alliancebits[i] |= valid << j;
		}
	}
	satuplinkbits = 0;
}

BaseObject* aiSearchSensorTargets(BaseObject const* psObj, int weapon_slot,
                                  WeaponStats const* psWStats, TARGET_ORIGIN* targetOrigin)
{
  auto const longRange = proj_GetLongRange(
          psWStats, psObj->playerManager->getPlayer());

  auto tarDist = longRange * longRange;
  auto foundCB = false;
  auto const minDist = proj_GetMinRange(
          psWStats, psObj->playerManager->getPlayer()) *
                 proj_GetMinRange(psWStats, psObj->playerManager->getPlayer());

  BaseObject* psTarget = nullptr;

	if (targetOrigin) {
		*targetOrigin = TARGET_ORIGIN::UNKNOWN;
	}

	for (auto const psSensor : apsSensorList)
	{
    BaseObject* psTemp = nullptr;
		bool isCB = false;

		if (!aiCheckAlliances(psSensor->playerManager->getPlayer(),
                          psObj->playerManager->getPlayer())) {
			continue;
		}

		if (auto psDroid = dynamic_cast<Droid const*>(psSensor)) {
			ASSERT_OR_RETURN(nullptr, psDroid->getType() == DROID_TYPE::SENSOR,
			                 "A non-sensor droid in a sensor list is non-sense");

      auto sensor = dynamic_cast<SensorStats const*>(psDroid->getComponent(COMPONENT_TYPE::SENSOR));

			// Skip non-observing droids. This includes Radar Detectors at
      // the moment since they never observe anything.
			// Artillery should not fire at objects observed by VTOL CB/Strike sensors.
			if (psDroid->getAction() != ACTION::OBSERVE ||
          sensor && sensor->type == SENSOR_TYPE::VTOL_CB ||
          sensor->type == SENSOR_TYPE::VTOL_INTERCEPT ||
			   	objRadarDetector(psDroid)) {
				continue;
			}
			psTemp = psDroid->getTarget(0);
			isCB = sensor->type == SENSOR_TYPE::INDIRECT_CB;
			//isRD = objRadarDetector((SimpleObject *)psDroid);
		}

		if (auto psCStruct = dynamic_cast<Structure*>(psSensor)) {
			// skip incomplete structures
			// Artillery should not fire at objects observed by VTOL CB/Strike sensors.
			if (psCStruct->getState() != STRUCTURE_STATE::BUILT ||
          psCStruct->getStats()->sensor_stats->type == SENSOR_TYPE::VTOL_CB ||
          psCStruct->getStats()->sensor_stats->type == SENSOR_TYPE::VTOL_INTERCEPT ||
          objRadarDetector((BaseObject*)psCStruct)) {
				continue;
			}
			psTemp = psCStruct->getTarget(0);
			isCB = structCBSensor(psCStruct);
		}

		if (!psTemp || psTemp->damageManager->isDead() ||
        psTemp->damageManager->isProbablyDoomed(false) ||
        !validTarget(psObj, psTemp, 0) ||
		  	aiCheckAlliances(psTemp->playerManager->getPlayer(), psObj->playerManager->getPlayer())) {
			continue;
		}
		auto const distSq = objectPositionSquareDiff(psTemp->getPosition(), psObj->getPosition());
    
		// Need to be in range, prefer closer targets or CB targets
    if (!(isCB > foundCB || isCB == foundCB && distSq < tarDist) ||
        distSq <= minDist || !aiObjHasRange(psObj, psTemp, weapon_slot) ||
        !visibleObject(psSensor, psTemp, false)) {
      continue;
    }

    tarDist = distSq;
    psTarget = psTemp;
    if (targetOrigin) {
      *targetOrigin = TARGET_ORIGIN::SENSOR;
    }
    if (!isCB) continue;

    if (targetOrigin) {
      *targetOrigin = TARGET_ORIGIN::CB_SENSOR;
    }
    foundCB = true;// got CB target, drop everything and shoot!
  }
	return psTarget;
}

int targetAttackWeight(BaseObject const* psTarget, BaseObject const* psAttacker, int weapon_slot)
{
  Droid const* psAttackerDroid;
	int damageRatio = 0, attackWeight = 0, noTarget = -1;
	WeaponStats const* attackerWeapon;
	auto bCmdAttached = false, bTargetingCmd = false, bDirect = false;

	if (psTarget == nullptr || psAttacker == nullptr ||
      psTarget->damageManager->isDead()) {
		return noTarget;
	}
	ASSERT(psTarget != psAttacker, "targetAttackWeight: Wanted to evaluate "
                                 "the worth of attacking ourselves...");

  // sensors/ecm droids, non-military structures get lower priority
	auto targetTypeBonus = 0;

	/* Get attacker weapon effect */
	if ((psAttackerDroid = dynamic_cast<Droid const*>(psAttacker))) {
		attackerWeapon = psAttackerDroid->weaponManager->weapons[0].stats.get();

		// check if this droid is assigned to a commander
		bCmdAttached = psAttackerDroid->hasCommander();

    auto psStruct = dynamic_cast<Structure const*>(psTarget);
    auto psDroid = dynamic_cast<Droid const*>(psTarget);

		// find out if current target is targeting our commander
    if (!bCmdAttached) { }
    else if (psDroid) {
      //go through all enemy weapon slots
      for (auto weaponSlot = 0; !bTargetingCmd && weaponSlot < numWeapons(*psDroid); weaponSlot++) {
        // see if this weapon is targeting our commander
        if (psDroid->getTarget(weaponSlot) ==
            psAttackerDroid->getCommander()) {
          bTargetingCmd = true;
        }
      }
    }
    else if (psStruct) {
      // go through all enemy weapons
      for (auto weaponSlot = 0; !bTargetingCmd && weaponSlot < numWeapons(*psStruct); weaponSlot++) {
        if (psStruct->getTarget(weaponSlot) == psAttackerDroid->getCommander()) {
          bTargetingCmd = true;
        }
      }
    }
  }
	else if (auto psStruct = dynamic_cast<Structure const*>(psAttacker)) {
		attackerWeapon = psStruct->weaponManager->weapons[weapon_slot].stats.get();
	}
	else {
    /* feature */
		ASSERT(!"invalid attacker object type", "targetAttackWeight: Invalid attacker object type");
		return noTarget;
	}
	bDirect = proj_Direct(attackerWeapon);

	if (dynamic_cast<Droid const*>(psAttacker) && psAttackerDroid->getType() == DROID_TYPE::SENSOR) {
		// sensors are considered a direct weapon, but for
    // computing expected damage it makes more sense
    // to use indirect damage
		bDirect = false;
	}

	// get weapon effect
	auto weaponEffect = attackerWeapon->weaponEffect;

	// see if attacker is using an EMP weapon
	auto bEmpWeap = (attackerWeapon->weaponSubClass == WEAPON_SUBCLASS::EMP);

	auto dist = iHypot((
          psAttacker->getPosition() - psTarget->getPosition()).xy());

	bool tooClose = (unsigned)dist <= proj_GetMinRange(
          attackerWeapon, psAttacker->playerManager->getPlayer());

	if (tooClose) {
    // if object is too close to fire at, consider it to be at maximum range
		dist = objSensorRange(psAttacker);
	}

	/* Calculate attack weight */
	if (auto targetDroid = dynamic_cast<Droid const*>(psTarget)) {
		if (targetDroid->damageManager->isDead()) {
			debug(LOG_NEVER, "Target droid is dead, skipping invalid droid.\n");
			return noTarget;
		}

		/* Calculate damage this target suffered */
    // FIXME Somewhere we get 0HP droids from
		if (targetDroid->damageManager->getOriginalHp() == 0) {
			damageRatio = 0;
			debug(LOG_ERROR, "targetAttackWeight: 0HP droid detected!");
			debug(LOG_ERROR, "  Type: %i Name: \"%s\" Owner: %i \"%s\")",
            targetDroid->getType(), targetDroid->getName()->c_str(),
            targetDroid->playerManager->getPlayer(),
            getPlayerName(targetDroid->playerManager->getPlayer()));
		}
		else {
			damageRatio = 100 - 100 * targetDroid->damageManager->getHp() / targetDroid->damageManager->getOriginalHp();
		}
		assert(targetDroid->damageManager->getOriginalHp() != 0); // Assert later so we get the info from above

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
    auto propulsion = dynamic_cast<PropulsionStats const*>(targetDroid->getComponent(COMPONENT_TYPE::PROPULSION));
    auto body = dynamic_cast<BodyStats const*>(targetDroid->getComponent(COMPONENT_TYPE::BODY));
		attackWeight = asWeaponModifier[(int)weaponEffect][(int)propulsion->propulsionType] // Our weapon's effect against target
			+ asWeaponModifierBody[(int)weaponEffect][(int)body->size]
			+ WEIGHT_DIST_TILE_DROID * objSensorRange(psAttacker) / TILE_UNITS
			- WEIGHT_DIST_TILE_DROID * dist / TILE_UNITS // farther droids are less attractive
			+ WEIGHT_HEALTH_DROID * damageRatio / 100 // we prefer damaged droids
			+ targetTypeBonus; // some droid types have higher priority

		/* If attacking with EMP try to avoid targets that were already "EMPed" */
		if (bEmpWeap && targetDroid->damageManager->getLastHitWeapon() == WEAPON_SUBCLASS::EMP &&
        gameTime - targetDroid->damageManager->getTimeLastHit() < EMP_DISABLE_TIME) { //target still disabled
			attackWeight /= EMP_DISABLED_PENALTY_F;
		}
	}
	else if (auto targetStructure = dynamic_cast<Structure const*>(psTarget)) {
		/* Calculate damage this target suffered */
		damageRatio = 100 - 100 * targetStructure->damageManager->getHp() / structureBody(targetStructure);

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
		attackWeight = asStructStrengthModifier[(int)weaponEffect][targetStructure->getStats()->strength]
			// Our weapon's effect against target
			+ WEIGHT_DIST_TILE_STRUCT * objSensorRange(psAttacker) / TILE_UNITS
			- WEIGHT_DIST_TILE_STRUCT * dist / TILE_UNITS // farther structs are less attractive
			+ WEIGHT_HEALTH_STRUCT * damageRatio / 100 // we prefer damaged structures
			+ targetTypeBonus; // some structure types have higher priority

		// go for unfinished structures only if nothing else found (same
    // for non-visible structures)
		if (targetStructure->getState() != STRUCTURE_STATE::BUILT) {
      //a decoy?
			attackWeight /= WEIGHT_STRUCT_NOT_BUILT_F;
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
	if (psTarget->damageManager->isProbablyDoomed(bDirect)) {
		/* indirect firing units have slow reload times, so give the target a chance to die,
		 * and give a different unit a chance to get in range, too. */
		if (weaponROF(attackerWeapon,
                  psAttacker->playerManager->getPlayer()) < TARGET_DOOMED_SLOW_RELOAD_T) {
			debug(LOG_NEVER, "Not killing unit - doomed. My ROF: %i (%s)",
            weaponROF(attackerWeapon, psAttacker->playerManager->getPlayer()),
            getStatsName(attackerWeapon));
			return noTarget;
		}
		attackWeight /= TARGET_DOOMED_PENALTY_F;
	}

  // attached to a commander and don't have a target assigned by some order
  if (!bCmdAttached) {
    return std::max<int>(1, attackWeight);
  }
  ASSERT(psAttackerDroid->getCommander() != nullptr, "Commander is NULL");

  // if commander is being targeted by our target, try to defend the commander
  if (bTargetingCmd) {
    attackWeight += WEIGHT_CMD_RANK * (1 + getDroidLevel(psAttackerDroid->getCommander()));
  }

  // fire support - go through all droids assigned to the commander
  for (auto const psGroupDroid : *psAttackerDroid->getGroup()->getMembers())
  {
    if (psGroupDroid->getOrder()->target == psTarget) {
      attackWeight = WEIGHT_CMD_SAME_TARGET;
      continue;
    }

    for (auto weaponSlot = 0; weaponSlot < numWeapons(*psGroupDroid); weaponSlot++)
    {
      // see if this droid is currently targeting current target
      if (psGroupDroid->getTarget(weaponSlot) == psTarget) {
        // we prefer targets that are already targeted and
        // hence will be destroyed faster
        attackWeight += WEIGHT_CMD_SAME_TARGET;
      }
    }
  }
  return std::max<int>(1, attackWeight);
}

bool aiChooseTarget(ConstructedObject const* psObj, BaseObject** ppsTarget, int weapon_slot,
										bool bUpdateTarget, TARGET_ORIGIN* targetOrigin)
{
  BaseObject * psTarget = nullptr;
	Droid* psCommander;
	int curTargetWeight = -1;
	TARGET_ORIGIN tmpOrigin = TARGET_ORIGIN::UNKNOWN;
  auto structure = dynamic_cast<Structure const*>(psObj);

	if (targetOrigin) {
		*targetOrigin = TARGET_ORIGIN::UNKNOWN;
	}

	ASSERT_OR_RETURN(false, (unsigned)weapon_slot < psObj->weaponManager->weapons.size(), "Invalid weapon selected");

	/* See if there is a something in range */
	if (auto droid = dynamic_cast<Droid const*>(psObj)) {
		auto psCurrTarget = droid->getTarget(0);

		/* find a new target */
		auto newTargetWeight = droid->aiBestNearestTarget(&psTarget, weapon_slot);

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
			ASSERT(!psTarget->damageManager->isDead(), "Droid found a dead target!");
			*ppsTarget = psTarget;
			return true;
		}
	}
	else if (structure == nullptr)
    return false;

  bool bCommanderBlock = false;
  auto psWStats = structure->weaponManager->weapons[weapon_slot].stats.get();
  auto longRange = proj_GetLongRange(psWStats, psObj->playerManager->getPlayer());

  // see if there is a target from the command droids
  psTarget = nullptr;
  psCommander = cmdDroidGetDesignator(psObj->playerManager->getPlayer());
  if (!proj_Direct(psWStats) && psCommander != nullptr &&
    aiStructHasRange(structure, (BaseObject *)psCommander, weapon_slot)) {
    // there is a commander that can fire designate for this structure
    // set bCommanderBlock so that the structure does not fire until the commander
    // has a target - (slow firing weapons will not be ready to fire otherwise).
    bCommanderBlock = true;

    // I do believe this will never happen, check for yourself :-)
    debug(LOG_NEVER, "Commander %d is good enough for fire designation", psCommander->getId());

    if (psCommander->getAction() == ACTION::ATTACK
        && psCommander->getTarget(0) != nullptr
        && !psCommander->getTarget(0)->damageManager->isDead()) {
      // the commander has a target to fire on
      if (aiStructHasRange(structure, psCommander->getTarget(0), weapon_slot)) {
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
      if (!dynamic_cast<Feature*>(psCurr) && !psCurr->damageManager->isDead() &&
          !aiCheckAlliances(psCurr->playerManager->getPlayer(), psObj->playerManager->getPlayer()) &&
          validTarget(psObj, psCurr, weapon_slot) &&
          psCurr->isVisibleToPlayer(psObj->playerManager->getPlayer()) == UBYTE_MAX &&
          aiStructHasRange(structure, psCurr, weapon_slot)) {

        auto newTargetValue = targetAttackWeight(psCurr, psObj, weapon_slot);
        // See if in sensor range and visible
        auto distSq = objectPositionSquareDiff(psCurr->getPosition(), psObj->getPosition());
        if (newTargetValue < targetValue || newTargetValue == targetValue && distSq >= tarDist) {
          continue;
        }

        tmpOrigin = TARGET_ORIGIN::VISUAL;
        psTarget = psCurr;
        tarDist = distSq;
        targetValue = newTargetValue;
      }
    }
  }

  if (!psTarget) return false;

  ASSERT(!psTarget->damageManager->isDead(), "Structure found a dead target!");
  if (targetOrigin)  {
    *targetOrigin = tmpOrigin;
  }
  *ppsTarget = psTarget;
  return true;
}

bool aiChooseSensorTarget(BaseObject const* psObj, BaseObject** ppsTarget)
{
  if (!objActiveRadar(psObj) && !objRadarDetector(psObj)) {
		ASSERT(false, "Only to be used for sensor turrets!");
		return false;
	}

	/* See if there is something in range */
  BaseObject* psTarget = nullptr;
  auto droid = dynamic_cast<Droid const*>(psObj);
  if (droid && droid->aiBestNearestTarget(&psTarget, 0) >= 0) {
    /* See if in sensor range */

    // I do believe this will never happen, check for yourself :-)
    debug(LOG_NEVER, "Sensor droid(%d) found possible target(%d)!!!", psObj->getId(), psTarget->getId());

    if (objectPositionSquareDiff(psTarget, psObj)
          < objSensorRange(psObj) * objSensorRange(psObj)) {
      *ppsTarget = psTarget;
      return true;
    }
  }

  // structure
  BaseObject* psTemp = nullptr;
  unsigned tarDist = UINT32_MAX;

  static GridList gridList; // static to save on allocations.
  gridList = gridStartIterate(psObj->getPosition().x, psObj->getPosition().y, objSensorRange(psObj));
  for (auto const psCurr : gridList)
  {
    // don't target features or doomed/dead objects
    if (dynamic_cast<Feature*>(psCurr) || psCurr->damageManager->isDead() ||
        psCurr->damageManager->isProbablyDoomed(false) ||
        aiCheckAlliances(psCurr->playerManager->getPlayer(), psObj->playerManager->getPlayer()) ||
        dynamic_cast<Structure*>(psCurr) && dynamic_cast<Structure*>(psCurr)->isWall()) {
      continue;
    }

    if (objectPositionSquareDiff(psCurr, psObj)
          < objSensorRange(psObj) * objSensorRange(psObj) &&
        psCurr->isVisibleToPlayer(psObj->playerManager->getPlayer()) == UBYTE_MAX &&
        objectPositionSquareDiff(psCurr, psObj) < tarDist) {
      psTemp = psCurr;
      tarDist = objectPositionSquareDiff(psCurr, psObj);
    }
  }

  if (!psTemp) return false;

  ASSERT(!psTemp->damageManager->isDead(), "aiChooseSensorTarget gave us a dead target");
  *ppsTarget = psTemp;
  return true;
}

/// Make droid/structure look for a better target
bool updateAttackTarget(BaseObject* psAttacker, int weapon_slot)
{
  BaseObject* psBetterTarget = nullptr;
	TARGET_ORIGIN tmpOrigin = TARGET_ORIGIN::UNKNOWN;

  if (!aiChooseTarget(psAttacker, &psBetterTarget, weapon_slot,
                      true, &tmpOrigin)) {
    return false;
  }

  // update target
  auto psDroid = dynamic_cast<Droid*>(psAttacker);
  auto psBuilding = dynamic_cast<Structure *>(psAttacker);
  if (psDroid == nullptr && psBuilding != nullptr) {
    psBuilding->setTarget(psBetterTarget, weapon_slot, tmpOrigin);
    return true;
  }

  if ((orderState(psDroid, ORDER_TYPE::NONE) ||
       orderState(psDroid, ORDER_TYPE::GUARD) ||
       orderState(psDroid, ORDER_TYPE::ATTACK_TARGET)) &&
      weapon_slot == 0) {
    newAction(psDroid, ACTION::ATTACK, psBetterTarget);
    return true;
  }
  // can't override current order
  psDroid->setActionTarget(psBetterTarget, weapon_slot);
  return true;
}

/// Check if any of our weapons can hit the target
bool checkAnyWeaponsTarget(Droid const* psDroid, BaseObject const* psTarget)
{
	for (auto i = 0; i < numWeapons(*psDroid); i++)
	{
		if (validTarget(psDroid, psTarget, i)) {
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
	bool bTargetInAir = false;
	uint8_t surfaceToAir = 0;

	if (!psTarget) return false;

	// need to check the propulsion type of target
	if (auto psDroid = dynamic_cast<Droid const*>(psTarget)) {
    auto propulsion = dynamic_cast<PropulsionStats const*>(
            psDroid->getComponent(COMPONENT_TYPE::PROPULSION));
    if (propulsion != nullptr &&
        asPropulsionTypes[static_cast<size_t>(propulsion->propulsionType)].travel == TRAVEL_MEDIUM::AIR &&
        psDroid->getMovementData()->status != MOVE_STATUS::INACTIVE) {
      bTargetInAir = true;
    }
    else {
        bTargetInAir = false;
    }
  }

	if (dynamic_cast<Structure const*>(psTarget)) {
		// let's hope so!
		bTargetInAir = false;
	}

  if (!dynamic_cast<Droid const*>(psObject) &&
      !dynamic_cast<Structure const*>(psObject)) {
    return false;
  }

	// need what can shoot at
	if (auto psDroid = dynamic_cast<Droid const*>(psObject)) {
    if (psDroid->getType() == DROID_TYPE::SENSOR) {
      // Sensor droids should not target anything in the air.
      return !bTargetInAir;
    }

    // can't attack without a weapon
    if (numWeapons(*psDroid) == 0)
      return false;

    surfaceToAir = psDroid->weaponManager->weapons[weapon_slot].stats->surfaceToAir;
    if (surfaceToAir & SHOOT_IN_AIR && bTargetInAir ||
        surfaceToAir & SHOOT_ON_GROUND && !bTargetInAir) {
      return true;
    }
    return false;
  }

	if (auto psStruct = dynamic_cast<Structure const*>(psObject)) {
    // can't attack without a weapon
    if (numWeapons(*psStruct) != 0) {
      surfaceToAir = psStruct->weaponManager->weapons[weapon_slot].stats->surfaceToAir;
    }
    else {
      surfaceToAir = 0;
    }

    if (surfaceToAir & SHOOT_IN_AIR && bTargetInAir ||
        surfaceToAir & SHOOT_ON_GROUND && !bTargetInAir) {
      return true;
    }
  }

	// if target is in the air, and you can shoot in the air - OK
  // if target is on the ground and can shoot at it - OK
	if (bTargetInAir && surfaceToAir & SHOOT_IN_AIR ||
      !bTargetInAir && surfaceToAir & SHOOT_ON_GROUND) {
    return true;
	}
  return false;
}

// Check properties of the AllianceType enum.
static bool alliancesFixed(ALLIANCE_TYPE t)
{
  return t != ALLIANCE_TYPE::ALLIANCES;
}

static bool alliancesSharedVision(ALLIANCE_TYPE t)
{
  return t == ALLIANCE_TYPE::ALLIANCES_TEAMS ||
         t == ALLIANCE_TYPE::ALLIANCES_UNSHARED;
}

static bool alliancesSharedResearch(ALLIANCE_TYPE t)
{
  return t == ALLIANCE_TYPE::ALLIANCES ||
         t == ALLIANCE_TYPE::ALLIANCES_TEAMS;
}

static bool alliancesSetTeamsBeforeGame(ALLIANCE_TYPE t)
{
  return t == ALLIANCE_TYPE::ALLIANCES_TEAMS ||
         t == ALLIANCE_TYPE::ALLIANCES_UNSHARED;
}

static bool alliancesCanGiveResearchAndRadar(ALLIANCE_TYPE t)
{
  return t == ALLIANCE_TYPE::ALLIANCES;
}

static bool alliancesCanGiveAnything(ALLIANCE_TYPE t)
{
  return t != ALLIANCE_TYPE::FFA;
}
