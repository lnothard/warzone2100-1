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
 * @file oprint.cpp
 * Object information printing routines
 */

#include "lib/framework/frame.h"

#include "console.h"
#include "droid.h"
#include "objects.h"
#include "oprint.h"
#include "projectile.h"
#include "visibility.h"
#include "baseobject.h"

/** print out information about a base object
 *  \param psObj the object to print the info for
 */
static void printBaseObjInfo(BaseObject const* psObj)
{
	const char* pType;
	switch (getObjectType(psObj)) {
    case OBJECT_TYPE::DROID:
		pType = "UNIT";
		break;
    case OBJECT_TYPE::STRUCTURE:
		pType = "STRUCT";
		break;
    case OBJECT_TYPE::FEATURE:
		pType = "FEAT";
		break;
	default:
		pType = "UNKNOWN TYPE";
		break;
	}

	CONPRINTF("%s id %d at (%d,%d,%d) dpr (%hu,%hu,%hu)\n", pType, psObj->getId(), 
            psObj->getPosition().x, psObj->getPosition().y, psObj->getPosition().z,
	          psObj->getRotation().direction, psObj->getRotation().pitch, psObj->getRotation().roll);
}

/** 
 * print out information about a general component
 * @param psStats the component to print the info for
 */
static void printComponentInfo(const ComponentStats* psStats)
{
	CONPRINTF("%s ref %d\n"
	          "   bPwr %d bPnts %d wt %d bdy %d imd %p\n",
	          getStatsName(psStats), psStats->ref, psStats->buildPower,
	          psStats->buildPoints, psStats->weight, psStats->base.hitPoints,
	          static_cast<void*>(psStats->pIMD.get()));
}

/** print out weapon information
 *  \param psStats the weapon to print the info for
 */
static void printWeaponInfo(const WeaponStats* psStats)
{
	const char *pWC, *pWSC, *pMM;

	switch (psStats->weaponClass) {
    case WEAPON_CLASS::KINETIC: //bullets etc
      pWC = "WC_KINETIC";
      break;
    case WEAPON_CLASS::HEAT: //laser etc
      pWC = "WC_HEAT";
      break;
    default:
      pWC = "UNKNOWN CLASS";
      break;
	}
	switch (psStats->weaponSubClass) {
    using enum WEAPON_SUBCLASS;
	case MACHINE_GUN:
		pWSC = "MGUN";
		break;
	case CANNON:
		pWSC = "CANNON";
		break;
	case MORTARS:
		pWSC = "MORTARS";
		break;
	case MISSILE:
		pWSC = "MISSILE";
		break;
	case ROCKET:
		pWSC = "ROCKET";
		break;
	case ENERGY:
		pWSC = "ENERGY";
		break;
	case GAUSS:
		pWSC = "GAUSS";
		break;
	case FLAME:
		pWSC = "FLAME";
		break;
	case HOWITZERS:
		pWSC = "HOWITZERS";
		break;
	case ELECTRONIC:
		pWSC = "ELECTRONIC";
		break;
	case AA_GUN:
		pWSC = "AAGUN";
		break;
	case SLOW_MISSILE:
		pWSC = "SLOWMISSILE";
		break;
	case SLOW_ROCKET:
		pWSC = "SLOWROCKET";
		break;
	case LAS_SAT:
		pWSC = "LAS_SAT";
		break;
	case BOMB:
		pWSC = "BOMB";
		break;
	case COMMAND:
		pWSC = "COMMAND";
		break;
	case EMP:
		pWSC = "EMP";
		break;
	default:
		pWSC = "UNKNOWN SUB CLASS";
		break;
	}
	switch (psStats->movementModel) {
    using enum MOVEMENT_MODEL;
	case DIRECT:
		pMM = "MM_DIRECT";
		break;
	case INDIRECT:
		pMM = "MM_INDIRECT";
		break;
	case HOMING_DIRECT:
		pMM = "MM_HOMINGDIRECT";
		break;
	case HOMING_INDIRECT:
		pMM = "MM_HOMINGINDIRECT";
		break;
	default:
		pMM = "UNKNOWN MOVE MODEL";
		break;
	}

	CONPRINTF("%s", "Weapon: ");
	printComponentInfo((const ComponentStats*)psStats);
	CONPRINTF("   sRng %d lRng %d mRng %d %s\n"
	          "   sHt %d lHt %d pause %d dam %d\n",
	          proj_GetShortRange(psStats, selectedPlayer), proj_GetLongRange(psStats, selectedPlayer),
	          proj_GetMinRange(psStats, selectedPlayer),
	          proj_Direct(psStats) ? "direct" : "indirect",
	          weaponShortHit(psStats, selectedPlayer), weaponLongHit(psStats, selectedPlayer),
	          weaponFirePause(psStats, selectedPlayer),
	          weaponDamage(psStats, selectedPlayer));
	if (selectedPlayer < MAX_PLAYERS) {
		CONPRINTF("   rad %d radDam %d\n"
		          "   inTime %d inDam %d inRad %d\n",
              psStats->upgraded[selectedPlayer].radius, psStats->upgraded[selectedPlayer].radiusDamage,
              psStats->upgraded[selectedPlayer].periodicalDamageTime,
              psStats->upgraded[selectedPlayer].periodicalDamage,
              psStats->upgraded[selectedPlayer].periodicalDamageRadius);
	}
	CONPRINTF("   flSpd %d %s\n",
	          psStats->flightSpeed, psStats->fireOnMove ? "fireOnMove" : "not fireOnMove");
	CONPRINTF("   %s %s %s\n", pWC, pWSC, pMM);
	CONPRINTF("   %srotate recoil %d\n"
	          "   radLife %d\n",
	          psStats->rotate ? "" : "not ",
	          psStats->recoilValue, psStats->radiusLife);
}

/** print out information about a droid and it's components
 *  \param psDroid the droid to print the info for
 */
void printDroidInfo(const Droid* psDroid)
{
	printBaseObjInfo(psDroid);

	CONPRINTF("   wt %d bSpeed %d sRng %d ECM %d bdy %d\n",
						psDroid->getWeight(), psDroid->getBaseSpeed(), droidSensorRange(psDroid),
            objJammerPower(psDroid), psDroid->damageManager->getHp());

	if (psDroid->asWeaps[0].nStat > 0) {
		printWeaponInfo(psDroid->weaponManager->weapons[0].stats.get());
	}

	for (auto i = 0; i < (int)COMPONENT_TYPE::COUNT; ++i)
	{
		switch (i) {
      case (int)COMPONENT_TYPE::BODY:
			if (psDroid->asBits[i] > 0) {
				CONPRINTF("%s", "Body: ");
				auto psBdyStats = psDroid->getComponent((COMPONENT_TYPE)i);
				printComponentInfo(psBdyStats);
			}
			else
			{
				CONPRINTF("%s", "ZNULL BODY\n");
			}
			break;
      case (int)COMPONENT_TYPE::BRAIN:
			break;
      case (int)COMPONENT_TYPE::PROPULSION:
			if (psDroid->asBits[i] > 0) {
				CONPRINTF("%s", "Prop: ");
				auto psPropStats = psDroid->getComponent((COMPONENT_TYPE)i);
				printComponentInfo(psPropStats);
			}
			else
			{
				CONPRINTF("%s", "ZNULL PROPULSION\n");
			}
			break;
      case (int)COMPONENT_TYPE::ECM:
			if (psDroid->asBits[i] > 0) {
				CONPRINTF("%s", "ECM: ");
				auto psECMStats = dynamic_cast<EcmStats const*>(psDroid->getComponent((COMPONENT_TYPE)i));
				printComponentInfo(psECMStats);
				CONPRINTF("   range %d loc %d imd %p\n",
				          ecmRange(psECMStats, psDroid->playerManager->getPlayer()),
                  psECMStats->location, static_cast<void*>(psECMStats->pMountGraphic.get()));
			}
			else {
				CONPRINTF("%s", "ZNULL ECM\n");
			}
			break;
      case (int)COMPONENT_TYPE::SENSOR:
			if (psDroid->asBits[i] > 0) {
				CONPRINTF("%s", "Sensor: ");
				auto psSensStats = dynamic_cast<SensorStats const*>(psDroid->getComponent((COMPONENT_TYPE)i));
				printComponentInfo(psSensStats);
				CONPRINTF("   rng %d loc %d imd %p\n",
				          sensorRange(psSensStats, psDroid->playerManager->getPlayer()),
				          psSensStats->location, static_cast<void*>(psSensStats->pMountGraphic.get()));
			}
			else {
				CONPRINTF("%s", "ZNULL SENSOR\n");
			}
			break;
      case (int)COMPONENT_TYPE::CONSTRUCT:
			if (psDroid->asBits[i] > 0) {
				CONPRINTF("%s", "Construct: ");
				auto psConstStats = dynamic_cast<ConstructStats const*>(psDroid->getComponent((COMPONENT_TYPE)i));
				printComponentInfo(psConstStats);
				CONPRINTF("   cPnts %d imd %p\n",
				          constructorPoints(psConstStats, psDroid->playerManager->getPlayer()),
				          static_cast<void*>(psConstStats->pMountGraphic.get()));
			}
			break;
      case (int)COMPONENT_TYPE::REPAIR_UNIT:
			if (psDroid->asBits[i] > 0)
			{
				CONPRINTF("%s", "Repair: ");
				auto psRepairStats = dynamic_cast<RepairStats const*>(psDroid->getComponent((COMPONENT_TYPE)i));
				printComponentInfo((ComponentStats*)psRepairStats);
				CONPRINTF("   repPnts %d loc %d imd %p\n",
				          repairPoints(psRepairStats, psDroid->playerManager->getPlayer()),
				          psRepairStats->location, static_cast<void*>(psRepairStats->pMountGraphic.get()));
			}
			break;
		default:
			break;
		}
	}
}
