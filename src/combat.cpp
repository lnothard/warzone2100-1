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
 * @file combat.cpp
 * Combat mechanics routines
 */

#include "lib/gamelib/gtime.h"
#include "lib/netplay/netplay.h"

#include "ai.h"
#include "combat.h"
#include "droid.h"
#include "move.h"
#include "objmem.h"
#include "projectile.h"
#include "random.h"
#include "structure.h"
#include "qtscript.h"


static constexpr auto EMP_DISABLE_TIME = 10000;
int mapHeight, mapWidth;
bool bMultiPlayer, bMultiMessages;
int map_Height(Vector2i);
int modifyForDifficultyLevel(int, bool);
const char* objInfo(const BaseObject *);


/* Fire a weapon at something */
bool combFire(Weapon* psWeap, BaseObject * psAttacker,
              BaseObject * psTarget, int weapon_slot)
{
	ASSERT(psWeap != nullptr, "Invalid weapon pointer");

  auto player = psAttacker->playerManager->getPlayer();
  auto psDroid = dynamic_cast<Droid*>(psAttacker);
	/* Don't shoot if the weapon_slot of a vtol is empty */
	if (psDroid && psDroid->isVtol() &&
      psWeap->ammoUsed >= getNumAttackRuns(psDroid, weapon_slot)) {
		objTrace(psAttacker->getId(), "VTOL slot %d is empty", weapon_slot);
		return false;
	}

	auto psStats = psWeap->stats;
	// check valid weapon/prop combination
	if (!validTarget(psAttacker, psTarget, weapon_slot)) {
		return false;
	}

	auto fireTime = gameTime - deltaGameTime + 1; // Can fire earliest at the start of the tick.

	// see if reloadable weapon.
	if (psStats->upgraded[player].reloadTime) {
		auto reloadTime = psWeap->timeLastFired + weaponReloadTime(psStats.get(), player);
		if (psWeap->ammo == 0) { // Out of ammo?
			fireTime = std::max(fireTime, reloadTime); // Have to wait for weapon to reload before firing.
			if (gameTime < fireTime) {
				return false;
			}
		}

		if (reloadTime <= fireTime) {
			//reset the ammo level
			psWeap->ammo = psStats->upgraded[player].numRounds;
		}
	}

	/* See when the weapon last fired to control it's rate of fire */
	unsigned firePause = weaponFirePause(psStats.get(), player);
	firePause = std::max(firePause, 1u); // Don't shoot infinitely many shots at once.
	fireTime = std::max(fireTime, psWeap->timeLastFired + firePause);

	if (gameTime < fireTime) {
		/* Too soon to fire again */
		return false;
	}

	ASSERT(player < MAX_PLAYERS,
         "psAttacker->player = %" PRIu8 "",
         player);

	if (psTarget->isVisibleToPlayer(player) != UBYTE_MAX) {
		// Can't see it - can't hit it
		objTrace(psAttacker->getId(),
             "combFire(%u[%s]->%u): Object has no indirect sight of target",
             psAttacker->getId(),
		         getStatsName(psStats),
             psTarget->getId());
		return false;
	}

	/* Check we can hit the target */
  auto droid = dynamic_cast<Droid*>(psAttacker);
  auto structure = dynamic_cast<Structure*>(psAttacker);
	bool tall = droid && droid->isVtol() ||
              structure && structure->getStats()->height > 1;

	if (proj_Direct(psStats.get()) && !
      lineOfFire(psAttacker, psTarget, weapon_slot, tall)) {
		// Can't see the target - can't hit it with direct fire
		objTrace(psAttacker->getId(), "combFire(%u[%s]->%u): No direct line of sight to target",
		         psAttacker->getId(), objInfo(psAttacker), psTarget->getId());
		return false;
	}

	Vector3i deltaPos = psTarget->getPosition() - psAttacker->getPosition();

	// if the turret doesn't turn, check if the attacker is in alignment with the target
	if (droid && !psStats->rotate) {
		auto targetDir = iAtan2(deltaPos.xy());
		auto dirDiff = abs(angleDelta(targetDir - psAttacker->getRotation().direction));
		if (dirDiff > FIXED_TURRET_DIR) {
			return false;
		}
	}

	/* Now see if the target is in range  - also check not too near */
	auto dist = iHypot(deltaPos.xy());
	auto longRange = proj_GetLongRange(psStats.get(), player);
	auto shortRange = proj_GetShortRange(psStats.get(), player);

	int min_angle = 0;
	// Calculate angle for indirect shots
	if (!proj_Direct(psStats.get()) && dist > 0) {
		min_angle = arcOfFire(psAttacker, psTarget, weapon_slot, true);

		// prevent extremely steep shots
		min_angle = std::min(min_angle, DEG(PROJ_ULTIMATE_PITCH));

		// adjust maximum range of unit if forced to shoot very steep
		if (min_angle > DEG(PROJ_MAX_PITCH)) {
			//do not allow increase of max range though
			if (iSin(2 * min_angle) < iSin(2 * DEG(PROJ_MAX_PITCH)))
			// If PROJ_MAX_PITCH == 45, then always iSin(2*min_angle) <= iSin(2*DEG(PROJ_MAX_PITCH)), and the test is redundant.
			{
				longRange = longRange * iSin(2 * min_angle) / iSin(2 * DEG(PROJ_MAX_PITCH));
			}
		}
	}

	int baseHitChance = 0;
	const auto min_range = proj_GetMinRange(psStats.get(), player);
	if (dist <= shortRange && dist >= min_range) {
		// get weapon chance to hit in the short range
		baseHitChance = weaponShortHit(psStats.get(), player);
	}
	else if (dist <= longRange && dist >= min_range) {
		// get weapon chance to hit in the long range
		baseHitChance = weaponLongHit(psStats.get(), player);
	}
	else {
		/* Out of range */
		objTrace(psAttacker->getId(), "combFire(%u[%s]->%u): Out of range",
             psAttacker->getId(), getStatsName(psStats), psTarget->getId());
		return false;
	}

	// adapt for height adjusted artillery shots
	if (min_angle > DEG(PROJ_MAX_PITCH)) {
		baseHitChance = baseHitChance * iCos(min_angle) / iCos(DEG(PROJ_MAX_PITCH));
	}

	// apply experience accuracy modifiers to the base
	//hit chance, not to the final hit chance
	int resultHitChance = baseHitChance;

	// add the attacker's experience
	if (droid) {
		auto level = getDroidEffectiveLevel(droid);

		// increase total accuracy by EXP_ACCURACY_BONUS % for each experience level
		resultHitChance += EXP_ACCURACY_BONUS * level * baseHitChance / 100;
	}

	// subtract the defender's experience
	if (auto asdroid = dynamic_cast<Droid*>(psTarget)) {
		auto level = getDroidEffectiveLevel(asdroid);

		// decrease weapon accuracy by EXP_ACCURACY_BONUS % for each experience level
		resultHitChance -= EXP_ACCURACY_BONUS * level * baseHitChance / 100;
	}

	if (droid &&
      droid->getMovementData()->status != MOVE_STATUS::INACTIVE &&
      !psStats->fireOnMove) {
		return false; // Can't fire while moving
	}

	/* -------!!! From that point we are sure that we are firing !!!------- */

	/* note when the weapon fired */
	psWeap->timeLastFired = fireTime;

	/* reduce ammo if salvo */
	if (psStats->upgraded[player].reloadTime) {
		psWeap->ammo--;
	}

	// increment the shots counter
	psWeap->shotsFired++;

	// predicted X,Y offset per sec
	Vector3i predict = psTarget->getPosition();

	// Target prediction
	if (dynamic_cast<Droid*>(psTarget) &&
      dynamic_cast<Droid*>(psTarget)->getMovementData()->bumpTime == 0) {
		auto psDroid = dynamic_cast<Droid*>(psTarget);

		int32_t flightTime;
		if (proj_Direct(psStats.get()) || dist <= proj_GetMinRange(psStats.get(), player)) {
			flightTime = dist * GAME_TICKS_PER_SEC / psStats->flightSpeed;
		}
		else {
			int32_t vXY, vZ; // Unused, we just want the flight time.
			flightTime = projCalcIndirectVelocities(
              dist, deltaPos.z, psStats->flightSpeed,
              &vXY, &vZ, min_angle);
		}

		if (psTarget->damageManager->getLastHitWeapon() == WEAPON_SUBCLASS::EMP) {
			int empTime = EMP_DISABLE_TIME - (gameTime - psTarget->damageManager->getTimeLastHit());
			CLIP(empTime, 0, EMP_DISABLE_TIME);
			if (empTime >= EMP_DISABLE_TIME * 9 / 10) {
				flightTime = 0; /* Just hit.  Assume they'll get hit again */
			}
			else {
				flightTime = MAX(0, flightTime - empTime);
			}
		}

		predict += Vector3i(iSinCosR(
            psDroid->getMovementData()->moveDir,
            psDroid->getMovementData()->speed * flightTime / GAME_TICKS_PER_SEC), 0);
		if (!isFlying(psDroid)) {
			predict.z = map_Height(predict.xy()); // Predict that the object will be on the ground.
		}
	}

	/* Fire off the bullet to the miss location. The miss is only visible if the player owns the target. (Why? - Per) */
	// What bVisible really does is to make the projectile audible even if it misses you. Since the target is NULL, proj_SendProjectile can't check if it was fired at you.
	bool bVisibleAnyway = psTarget->playerManager->getPlayer() == selectedPlayer;

	// see if we were lucky to hit the target
	bool isHit = gameRand(100) <= resultHitChance;
	if (isHit) {
		/* Kerrrbaaang !!!!! a hit */
		objTrace(psAttacker->getId(), "combFire: [%s]->%u: resultHitChance=%d, visibility=%d", getStatsName(psStats),
		         psTarget->getId(), resultHitChance, (int)psTarget->isVisibleToPlayer(player));
		syncDebug("hit=(%d,%d,%d)", predict.x, predict.y, predict.z);
	}
	else { /* Deal with a missed shot */
		// Get the shape of the target to avoid "missing" inside of it
		const auto targetShape = establishTargetShape(psTarget);

		// Worst possible shot based on distance and weapon accuracy
		auto deltaPosPredict = psAttacker->getPosition() - predict;
		int worstShot;
		if (resultHitChance > 0) {
			worstShot = (iHypot(deltaPosPredict.xy()) * 100 / resultHitChance) / 5;
		}
		else {
			worstShot = iHypot(deltaPosPredict.xy()) * 2;
		}

		// Use a random seed to determine how far the miss will land from the target
		// That (num/100)^3 allow the misses to fall much more frequently close to the target
		auto num = gameRand(100) + 1;
		auto minOffset = 2 * targetShape.radius();

		auto missDist = minOffset + (worstShot * num * num * num) / (100 * 100 * 100);

		// Determine the angle of the miss in the 270 degrees in "front" of the target.
		// The 90 degrees behind would most probably cause an unwanted hit when the projectile will be drawn through the hitbox.
		auto miss = Vector3i(iSinCosR(gameRand(
            DEG(270)) - DEG(135) + iAtan2(deltaPosPredict.xy()), missDist), 0);
		predict += miss;

		psTarget = nullptr; // Missed the target, so don't expect to hit it.

		objTrace(psAttacker->getId(), "combFire: Missed shot by (%4d,%4d)", miss.x, miss.y);
		syncDebug("miss=(%d,%d,%d)", predict.x, predict.y, predict.z);
	}

	// Make sure we don't pass any negative or out of bounds numbers to proj_SendProjectile
	CLIP(predict.x, 0, world_coord(mapWidth - 1));
	CLIP(predict.y, 0, world_coord(mapHeight - 1));

	proj_SendProjectileAngled(psWeap, psAttacker, player,
                            predict, psTarget, bVisibleAnyway, weapon_slot,
	                          min_angle, fireTime);
	return true;
}

/*checks through the target players list of structures and droids to see
if any support a counter battery sensor*/
void counterBatteryFire(BaseObject* psAttacker, BaseObject* psTarget)
{
	/*if a null target is passed in ignore - this will be the case when a 'miss'
	projectile is sent - we may have to cater for these at some point*/
	// also ignore cases where you attack your own player
	// Also ignore cases where there are already 1000 missiles heading towards the attacker.
	if (psTarget == nullptr ||
      (psAttacker != nullptr &&
       psAttacker->playerManager->getPlayer() == psTarget->playerManager->getPlayer()) ||
      (psAttacker != nullptr && dynamic_cast<Health *>(psAttacker)->isProbablyDoomed(false))) {
		return;
	}

	for (auto psViewer : apsSensorList)
	{
    if (!aiCheckAlliances(psTarget->playerManager->getPlayer(), psViewer->playerManager->getPlayer())) {
      continue;
    }
    auto psStruct = dynamic_cast<Structure*>(psViewer);
    auto psDroid = dynamic_cast<Droid*>(psViewer);
    if ((psStruct && !structCBSensor(psStruct) &&
         psStruct->getStats()->sensor_stats->type != SENSOR_TYPE::VTOL_CB) ||
        (psDroid && !psDroid->hasCbSensor())) {
      continue;
    }
    const auto sensorRange = objSensorRange(psViewer);

    // Check sensor distance from target
    const auto xDiff = psViewer->getPosition().x -
                       psTarget->getPosition().x;
    const auto yDiff = psViewer->getPosition().y -
                       psTarget->getPosition().y;

    if (xDiff * xDiff + yDiff * yDiff >= sensorRange * sensorRange) {
      continue;
    }
    // Inform viewer of target
    if (psDroid) {
      orderDroidObj(psDroid, ORDER_TYPE::OBSERVE,
                    psAttacker, ModeImmediate);
    }
    else if (psStruct) {
      setStructureTarget(psStruct, psAttacker, 0, TARGET_ORIGIN::CB_SENSOR);
    }
  }
}

int objArmour(BaseObject const* psObj, WEAPON_CLASS weaponClass)
{
	auto armour = 0;
  auto player = psObj->playerManager->getPlayer();
	if (auto psDroid = dynamic_cast<const Droid*>(psObj)) {
    auto body = psDroid->getComponent(COMPONENT_TYPE::BODY);
    if (body) {
      armour = bodyArmour(
              dynamic_cast<const BodyStats*>(body),
              player, weaponClass);
    }
	}
	else if (auto psStruct = dynamic_cast<const Structure*>(psObj)) {
    if (psStruct->getState() != STRUCTURE_STATE::BEING_BUILT) {
      if (weaponClass == WEAPON_CLASS::KINETIC) {
        armour = psStruct->getStats()->upgraded_stats[player].armour;
      }
      else if (weaponClass == WEAPON_CLASS::HEAT) {
        armour = psStruct->getStats()->upgraded_stats[player].thermal;
      }
    }
	}
	else if (auto psFeature = dynamic_cast<const Feature*>(psObj))
    if (weaponClass == WEAPON_CLASS::KINETIC) {
		  armour = psFeature->getStats()->armourValue;
	}
	return armour;
}

/* Deals damage to an object
 * \param psObj object to deal damage to
 * \param damage amount of damage to deal
 * \param weaponClass the class of the weapon that deals the damage
 * \param weaponSubClass the subclass of the weapon that deals the damage
 * \return < 0 when the dealt damage destroys the object, > 0 when the object survives
 */
int objDamage(BaseObject* psObj, unsigned damage, unsigned originalhp,
              WEAPON_CLASS weaponClass, WEAPON_SUBCLASS weaponSubClass,
              bool isDamagePerSecond, int minDamage)
{
	auto level = 1;
	auto armour = objArmour(psObj, weaponClass);
	const auto lastHit = psObj->damageManager->getTimeLastHit();

	// If the previous hit was by an EMP cannon and this one is not:
	// don't reset the weapon class and hit time
	// (Giel: I guess we need this to determine when the EMP-"shock" is over)
	if (psObj->damageManager->getLastHitWeapon() != WEAPON_SUBCLASS::EMP ||
      weaponSubClass == WEAPON_SUBCLASS::EMP) {
		psObj->damageManager->setTimeLastHit(gameTime);
		psObj->damageManager->setLastHitWeapon(weaponSubClass);
	}

	// EMP cannons do no damage, if we are one return now
	if (weaponSubClass == WEAPON_SUBCLASS::EMP) {
		return 0;
	}

	// apply game difficulty setting
	damage = modifyForDifficultyLevel(damage, !psObj->playerManager->isSelectedPlayer());

	if (dynamic_cast<Structure*>(psObj) || dynamic_cast<Droid*>(psObj)) {
		// Force sending messages, even if messages were turned off, since a non-synchronised script will execute here. (Aaargh!)
		bool bMultiMessagesBackup = bMultiMessages;
		bMultiMessages = bMultiPlayer;
		triggerEventAttacked(psObj, g_pProjLastAttacker, lastHit);
		bMultiMessages = bMultiMessagesBackup;
	}

	if (auto psDroid = dynamic_cast<Droid*>(psObj)) {
		// Retrieve highest, applicable, experience level
		level = getDroidEffectiveLevel(psDroid);
	}

	// Reduce damage taken by EXP_REDUCE_DAMAGE % for each experience level
	auto actualDamage = (damage * (100 - EXP_REDUCE_DAMAGE * level)) / 100;

	// Apply at least the minimum damage amount
	actualDamage = MAX(actualDamage - armour, actualDamage * minDamage / 100);

	// And at least MIN_WEAPON_DAMAGE points
	actualDamage = MAX(actualDamage, MIN_WEAPON_DAMAGE);

	debug(LOG_ATTACK, "objDamage(%d): body: %d, armour: %d,"
                    " basic damage: %d, actual damage: %d",
                    psObj->getId(), psObj->damageManager->getHp(), armour,
                    damage, actualDamage);

	if (isDamagePerSecond) {
		auto deltaDamageRate = actualDamage - psObj->damageManager->getPeriodicalDamage();
		if (deltaDamageRate <= 0) {
			return 0; // Did this much damage already, this tick, so don't do more.
		}
		actualDamage = gameTimeAdjustedAverage(deltaDamageRate);
		psObj->damageManager->setPeriodicalDamage(psObj->damageManager->getPeriodicalDamage() + deltaDamageRate);
	}

	objTrace(psObj->getId(), "objDamage: Penetrated %d", actualDamage);
	syncDebug("damage%u dam%u,o%u,wc%d.%d,ar%d,lev%d,aDam%d,isDps%d",
            psObj->getId(), damage, originalhp, weaponClass,
	          weaponSubClass, armour, level, actualDamage, isDamagePerSecond);

	// for some odd reason, we have 0 hitpoints.
	if (originalhp == 0) {
		ASSERT(originalhp, "original hitpoints are 0 ?");
		return -65536; // it is dead
	}

	// If the shell did sufficient damage to destroy the
  // object, deal with it and return
	if (actualDamage >= psObj->damageManager->getHp()) {
		return -(int64_t)65536 * psObj->damageManager->getHp() / originalhp;
	}

	// Subtract the dealt damage from the droid's remaining body points
	psObj->damageManager->setHp(psObj->damageManager->getHp() - actualDamage);

	return (int64_t)65536 * actualDamage / originalhp;
}

/* Guesses how damage a shot might do.
 * \param psObj object that might be hit
 * \param damage amount of damage to deal
 * \param weaponClass the class of the weapon that deals the damage
 * \param weaponSubClass the subclass of the weapon that deals the damage
 * \return guess at amount of damage
 */
unsigned objGuessFutureDamage(WeaponStats const* psStats, unsigned player, BaseObject const* psTarget)
{
	int level = 1;

	if (psTarget == nullptr) {
		return 0; // Hard to destroy the ground. The armour on the mud is very strong and blocks all damage.
	}

	auto damage = calcDamage(weaponDamage(psStats, player),
                      psStats->weaponEffect, psTarget);

	// EMP cannons do no damage, if we are one return now
	if (psStats->weaponSubClass == WEAPON_SUBCLASS::EMP) {
		return 0;
	}

	// apply game difficulty setting
	damage = modifyForDifficultyLevel(damage, psTarget->playerManager->getPlayer() != selectedPlayer);
	auto armour = objArmour(psTarget, psStats->weaponClass);

	if (auto psDroid = dynamic_cast<Droid const*>(psTarget)) {
		// Retrieve highest, applicable, experience level
		level = getDroidEffectiveLevel(psDroid);
	}
	//debug(LOG_ATTACK, "objGuessFutureDamage(%d): body %d armour %d damage: %d", psObj->id, psObj->body, armour, damage);

	// Reduce damage taken by EXP_REDUCE_DAMAGE % for each experience level
	auto actualDamage = (damage * (100 - EXP_REDUCE_DAMAGE * level)) / 100;

	// You always do at least a third of the experience modified damage
	actualDamage = MAX(actualDamage - armour, actualDamage * psStats->upgraded[player].minimumDamage / 100);

	// And at least MIN_WEAPON_DAMAGE points
	actualDamage = MAX(actualDamage, MIN_WEAPON_DAMAGE);

	//objTrace(psObj->id, "objGuessFutureDamage: Would penetrate %d", actualDamage);

	return actualDamage;
}
