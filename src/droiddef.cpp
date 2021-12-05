//
// Created by luna on 01/12/2021.
//

#include <iostream>

#include "droiddef.h"
#include "droid.h"
#include "projectile.h"
#include "stats.h"
#include "move.h"
#include "map.h"
#include "order.h"

constexpr uint16_t VTOL_ATTACK_LENGTH = 1000;

static inline DroidStats *castDroidTemplate(StatsObject *stats)
{
  return stats != nullptr && stats->hasType(STAT_TEMPLATE) ? dynamic_cast<DroidStats *>(stats) : nullptr;
}

static inline DroidStats const *castDroidTemplate(StatsObject const *stats)
{
  return stats != nullptr && stats->hasType(STAT_TEMPLATE) ? dynamic_cast<DroidStats const *>(stats) : nullptr;
}

/* Return the type of a droid from it's template */
DROID_TYPE DroidStats::droidTemplateType()
{
  DROID_TYPE type = DROID_DEFAULT;

  if (droidType == DROID_PERSON ||
      droidType == DROID_CYBORG ||
      droidType == DROID_CYBORG_SUPER ||
      droidType == DROID_CYBORG_CONSTRUCT ||
      droidType == DROID_CYBORG_REPAIR ||
      droidType == DROID_TRANSPORTER ||
      droidType == DROID_SUPERTRANSPORTER)
  {
    type = droidType;
  }
  else if (asParts[COMP_BRAIN] != 0)
  {
    type = DROID_COMMAND;
  }
  else if ((asSensorStats + asParts[COMP_SENSOR])->location == LOC_TURRET)
  {
    type = DROID_SENSOR;
  }
  else if ((asECMStats + asParts[COMP_ECM])->location == LOC_TURRET)
  {
    type = DROID_ECM;
  }
  else if (asParts[COMP_CONSTRUCT] != 0)
  {
    type = DROID_CONSTRUCT;
  }
  else if ((asRepairStats + asParts[COMP_REPAIRUNIT])->location == LOC_TURRET)
  {
    type = DROID_REPAIR;
  }
  else if (asWeaps[0] != 0)
  {
    type = DROID_WEAPON;
  }
  /* with more than weapon is still a DROID_WEAPON */
  else if (numWeaps > 1)
  {
    type = DROID_WEAPON;
  }

  return type;
}

/* Calculate the base speed of a droid from it's template */
UDWORD DroidStats::calcDroidBaseSpeed(UDWORD weight, UBYTE player)
{
  unsigned speed = asPropulsionTypes[asPropulsionStats[asParts[COMP_PROPULSION]].propulsionType].powerRatioMult *
                   bodyPower(&asBodyStats[asParts[COMP_BODY]], player) / MAX(1, weight);

  // reduce the speed of medium/heavy VTOLs
  if (asPropulsionStats[asParts[COMP_PROPULSION]].propulsionType == PROPULSION_TYPE_LIFT)
  {
    if (asBodyStats[asParts[COMP_BODY]].size == SIZE_HEAVY)
    {
      speed /= 4;
    }
    else if (asBodyStats[asParts[COMP_BODY]].size == SIZE_MEDIUM)
    {
      speed = speed * 3 / 4;
    }
  }

  // applies the engine output bonus if output > weight
  if (asBodyStats[asParts[COMP_BODY]].base.power > weight)
  {
    speed = speed * 3 / 2;
  }

  return speed;
}

bool Droid::aiUnitHasRange(const GameObject& targetObj, int weapon_slot)
{
  int32_t longRange = aiDroidRange(this, weapon_slot);

  return objPosDiffSq(targetObj) < longRange * longRange;
}

bool Droid::actionInRange(const GameObject& targetObj, int weapon_slot, bool useLongWithOptimum)
{
  if (weaponList[0].nStat == 0)
  {
    return false;
  }

  const unsigned compIndex = weaponList()[weapon_slot].nStat;
  ASSERT_OR_RETURN(false, compIndex < numWeaponStats, "Invalid range referenced for numWeaponStats, %d > %d", compIndex, numWeaponStats);
  const WEAPON_STATS *psStats = asWeaponStats + compIndex;

  const int dx = (SDWORD)m_position.x - (SDWORD)targetObj.position().x;
  const int dy = (SDWORD)m_position.y - (SDWORD)targetObj.position().y;

  const int radSq = dx * dx + dy * dy;
  const int longRange = proj_GetLongRange(psStats, owningPlayer);
  const int shortRange = proj_GetShortRange(psStats, owningPlayer);

  int rangeSq = 0;
  switch (secondaryOrder & DSS_ARANGE_MASK)
  {
  case DSS_ARANGE_OPTIMUM:
    if (!useLongWithOptimum && weaponShortHit(psStats, owningPlayer) > weaponLongHit(psStats, owningPlayer))
    {
      rangeSq = shortRange * shortRange;
    }
    else
    {
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
    ASSERT(!"unknown attackrange order", "unknown attack range order");
    rangeSq = longRange * longRange;
    break;
  }

  /* check max range */
  if (radSq <= rangeSq)
  {
    /* check min range */
    const int minrange = proj_GetMinRange(psStats, owningPlayer);
    if (radSq >= minrange * minrange || !proj_Direct(psStats))
    {
      return true;
    }
  }

  return false;
}

// check if a target is inside minimum weapon range
bool Droid::actionInsideMinRange(const GameObject& targetObj, const WEAPON_STATS* droidWeapStats) const
{
  if (!droidWeapStats)
  {
    droidWeapStats = getWeaponStats(this, 0);
  }

  /* if I am a multi-turret droid */
  if (m_weaponList[0].nStat == 0)
  {
    return false;
  }

  const int dx = m_position.x - targetObj.position().x;
  const int dy = m_position.y - targetObj.position().y;
  const int radSq = dx * dx + dy * dy;
  const int minRange = proj_GetMinRange(droidWeapStats, owningPlayer);
  const int rangeSq = minRange * minRange;

  // check min range
  if (radSq <= rangeSq)
  {
    return true;
  }

  return false;
}

void Droid::actionAddVtolAttackRun(const GameObject& targetDroid)
{
  /* get normal vector from droid to target */
  Vector2i delta = (targetDroid.position() - m_position).xy();

  /* get magnitude of normal vector (Pythagorean theorem) */
  int dist = std::max(iHypot(delta), 1);

  /* add waypoint behind target attack length away*/
  Vector2i dest =
      targetDroid.position().xy() + delta * VTOL_ATTACK_LENGTH / dist;

  if (!worldOnMap(dest))
  {
    debug(LOG_NEVER, "*** actionAddVtolAttackRun: run off map! ***");
  }
  else
  {
    moveDroidToDirect(this, dest.x, dest.y);
  }
}

// Choose a landing position for a VTOL when it goes to rearm that is close to rearm
// pad but not on it, since it may be busy by the time we get there.
bool Droid::actionVTOLLandingPos(Vector2i *p)
{
  /* Initial box dimensions and set iteration count to zero */
  int startX = map_coord(p->x);
  int startY = map_coord(p->y);

  // set blocking flags for all the other droids
  for (const Droid& droid : allDroidLists)
  {
    Vector2i t(0, 0);
    if (DROID_STOPPED(droid))
    {
      t = map_coord(droid.position().xy());
    }
    else
    {
      t = map_coord(droid.sMove.destination);
    }
    if (droid != this)
    {
      if (tileOnMap(t))
      {
        mapTile(t)->tileInfoBits |= BITS_FPATHBLOCK;
      }
    }
  }

  // search for landing tile; will stop when found or radius exceeded
  Vector2i xyCoords(0, 0);
  const bool foundTile = spiralSearch(startX, startY, vtolLandingRadius,
                                      vtolLandingTileSearchFunction, &xyCoords);
  if (foundTile)
  {
    objTrace(id, "Unit %d landing pos (%d,%d)", id, xyCoords.x, xyCoords.y);
    p->x = world_coord(xyCoords.x) + TILE_UNITS / 2;
    p->y = world_coord(xyCoords.y) + TILE_UNITS / 2;
  }

  // clear blocking flags for all the other droids
  for (Droid *psCurr = allDroidLists[owningPlayer]; psCurr; psCurr = psCurr->psNext)
  {
    Vector2i t(0, 0);
    if (DROID_STOPPED(psCurr))
    {
      t = map_coord(psCurr->position.xy());
    }
    else
    {
      t = map_coord(psCurr->sMove.destination);
    }
    if (tileOnMap(t))
    {
      mapTile(t)->tileInfoBits &= ~BITS_FPATHBLOCK;
    }
  }

  return foundTile;
}

/*send the vtol droid back to the nearest rearming pad - if one otherwise
return to base*/
void Droid::moveToRearm()
{
  if (!isVtolDroid(this))
  {
    return;
  }

  //if droid is already returning - ignore
  if (vtolRearming())
  {
    return;
  }

  //get the droid to fly back to a ReArming Pad
  // don't worry about finding a clear one for the minute
  Structure *psStruct = findNearestReArmPad(this, psBaseStruct, false);
  if (psStruct)
  {
    // note a base rearm pad if the vtol doesn't have one
    if (psBaseStruct == nullptr)
    {
      setDroidBase(this, psStruct);
    }

    //return to re-arming pad
    if (order.type == DORDER_NONE)
    {
      // no order set - use the rearm order to ensure the unit goes back
      // to the landing pad
      orderDroidObj(this, DORDER_REARM, psStruct, ModeImmediate);
    }
    else
    {
      actionDroid(this, DROID_ACTION::MOVETOREARM, psStruct);
    }
  }
  else
  {
    //return to base un-armed
    objTrace(id, "Did not find an available rearm pad - RTB instead");
    orderDroid(this, DORDER_RTB, ModeImmediate);
  }
}

void Droid::actionSanity()
{
  // Don't waste ammo unless given a direct attack order.
  bool avoidOverkill = order.type != DORDER_ATTACK &&
                       (action == DROID_ACTION::ATTACK || action == DROID_ACTION::MOVEFIRE || action == DROID_ACTION::MOVETOATTACK ||
                        action == DROID_ACTION::ROTATETOATTACK || action == DROID_ACTION::VTOLATTACK);
  bool bDirect = false;

  // clear the target if it has died
  for (int i = 0; i < MAX_WEAPONS; i++)
  {
    bDirect = proj_Direct(asWeaponStats + m_weaponList[i].nStat);
    if (psActionTarget[i] && (avoidOverkill ? aiObjectIsProbablyDoomed(psActionTarget[i], bDirect) : psActionTarget[i]->deathTime))
    {
      syncDebugObject(psActionTarget[i], '-');
      setDroidActionTarget(this, nullptr, i);
      if (i == 0)
      {
        if (action != DROID_ACTION::MOVEFIRE &&
            action != DROID_ACTION::TRANSPORTIN &&
            action != DROID_ACTION::TRANSPORTOUT)
        {
          action = DROID_ACTION::NONE;
          // if VTOL - return to rearm pad if not patrolling
          if (isVtolDroid(this))
          {
            if ((order.type == DORDER_PATROL || order.type == DORDER_CIRCLE) && (!vtolEmpty() || (secondaryOrder & DSS_ALEV_MASK) == DSS_ALEV_NEVER))
            {
              // Back to the patrol.
              actionDroid(this, DROID_ACTION::MOVE, order.pos.x, order.pos.y);
            }
            else
            {
              moveToRearm();
            }
          }
        }
      }
    }
  }
}

void Droid::cancelBuild()
{
  if (order.type == DORDER_NONE || order.type == DORDER_PATROL || order.type == DORDER_HOLD || order.type == DORDER_SCOUT || order.type == DORDER_GUARD)
  {
    objTrace(id, "Droid build action cancelled");
    order.psObj = nullptr;
    action = DROID_ACTION::NONE;
    setDroidActionTarget(this, nullptr, 0);
    return;  // Don't cancel orders.
  }

  if (orderDroidList())
  {
    objTrace(id, "Droid build order cancelled - changing to next order");
  }
  else
  {
    objTrace(id, "Droid build order cancelled");
    action = DROID_ACTION::NONE;
    order = DroidOrder(DORDER_NONE);
    setDroidActionTarget(this, nullptr, 0);

    // The droid has no more build orders, so halt in place rather than clumping around the build objective
    moveStopDroid(this);

    triggerEventDroidIdle(this);
  }
}

void Droid::droidBodyUpgrade()
{
  const int factor = 10000; // use big numbers to scare away rounding errors
  int prev = originalBody;
  originalBody = calcDroidBaseBody(this);
  int increase = originalBody * factor / prev;
  hitPoints = MIN(originalBody, (hitPoints * increase) / factor + 1);
  DroidStats sTemplate;
  templateSetParts(this, &sTemplate);
  // update engine too
  baseSpeed = calcDroidBaseSpeed(&sTemplate, weight, owningPlayer);
  if (isTransporter(this))
  {
    for (Droid *psCurr = psGroup->psList; psCurr != nullptr; psCurr = psCurr->psGrpNext)
    {
      if (psCurr != this)
      {
        droidBodyUpgrade(psCurr);
      }
    }
  }
}

/** Teleport a droid to a new position on the map */
void Droid::droidSetPosition(int x, int y)
{
  m_position.x = x;
  m_position.y = y;
  m_position.z = map_Height(m_position.x, m_position.y);
  initDroidMovement();
  visTilesUpdate((GameObject *)this);
}

void Droid::actionUpdateTransporter()
{
  //check if transporter has arrived
  if (updateTransporter(this))
  {
    // Got to destination
    action = DROID_ACTION::NONE;
  }
}

void Droid::actionUpdateVtolAttack()
{
  /* don't do attack runs whilst returning to base */
  if (order.type == DORDER_RTB)
  {
    return;
  }

  /* order back to base after fixed number of attack runs */
  if (numWeapons > 0 && m_weaponList[0].nStat > 0 && vtolEmpty())
  {
    moveToRearm();
    return;
  }

  /* circle around target if hovering and not cyborg */
  if (sMove.Status == MOVEHOVER && !cyborgDroid())
  {
    actionAddVtolAttackRun();
  }
}

bool Droid::droidOnMap()
{
  if (deathTime == NOT_CURRENT_LIST || isTransporter(this)
      || m_position.x == INVALID_XY || m_position.y == INVALID_XY || missionIsOffworld()
      || mapHeight == 0)
  {
    // Off world or on a transport or is a transport or in mission list, or on a mission, or no map - ignore
    return true;
  }
  return worldOnMap(m_position.x, m_position.y);
}

bool Droid::isConstructionDroid()
{
  return droidType == DROID_CONSTRUCT || droidType == DROID_CYBORG_CONSTRUCT;
}

/*returns true if droid type is one of the Cyborg types*/
bool Droid::cyborgDroid()
{
  return (droidType == DROID_CYBORG
          || droidType == DROID_CYBORG_CONSTRUCT
          || droidType == DROID_CYBORG_REPAIR
          || droidType == DROID_CYBORG_SUPER);
}

// Check if a droid can be selected.
bool Droid::isSelectable() const
{
  if (flags.test(UNSELECTABLE))
  {
    return false;
  }

  // we shouldn't ever control the transporter in SP games
  if (isTransporter(this) && !bMultiPlayer)
  {
    return false;
  }

  return true;
}

// Select a droid and do any necessary housekeeping.
//
void Droid::selectDroid()
{
  if (!isSelectable())
  {
    return;
  }

  selected = true;
  intRefreshScreen();
  triggerEventSelected();
  jsDebugSelected(psDroid);
}

// De-select a droid and do any necessary housekeeping.
//
void Droid::DeSelectDroid()
{
  selected = false;
  intRefreshScreen();
  triggerEventSelected();
}

/*calculates the electronic resistance of a droid based on its experience level*/
SWORD Droid::droidResistance() const
{
  const BODY_STATS *psStats = asBodyStats + asBits[COMP_BODY];
  int resistance = experience / (65536 / MAX(1, psStats->upgrade[owningPlayer].resistance));
  // ensure resistance is a base minimum
  resistance = MAX(resistance, psStats->upgrade[owningPlayer].resistance);
  return MIN(resistance, INT16_MAX);
}

// return whether a droid has a standard sensor on it (standard, VTOL strike, or wide spectrum)
bool Droid::standardSensorDroid() const
{
  if (droidType != DROID_SENSOR)
  {
    return false;
  }
  if (asSensorStats[asBits[COMP_SENSOR]].type == VTOL_INTERCEPT_SENSOR
      || asSensorStats[asBits[COMP_SENSOR]].type == STANDARD_SENSOR
      || asSensorStats[asBits[COMP_SENSOR]].type == SUPER_SENSOR)
  {
    return true;
  }

  return false;
}

// return whether a droid has a CB sensor on it
bool Droid::cbSensorDroid() const
{
  if (droidType != DROID_SENSOR)
  {
    return false;
  }
  if (asSensorStats[asBits[COMP_SENSOR]].type == VTOL_CB_SENSOR
      || asSensorStats[asBits[COMP_SENSOR]].type == INDIRECT_CB_SENSOR)
  {
    return true;
  }

  return false;
}

/*checks if the droid is a VTOL droid and updates the attack runs as required*/
void Droid::updateVtolAttackRun(int weapon_slot)
{
  if (!isVtolDroid(this)) return;
  if (numWeapons <= 0) return;
  if (asWeaponStats[m_weaponList[weapon_slot].nStat].vtolAttackRuns > 0)
  {
    ++m_weaponList[weapon_slot].usedAmmo;
    if (m_weaponList[weapon_slot].usedAmmo == getNumAttackRuns(this, weapon_slot))
    {
      m_weaponList[weapon_slot].ammo = 0;
    }
    //quick check doesn't go over limit
    ASSERT(m_weaponList[weapon_slot].usedAmmo < UWORD_MAX, "too many attack runs");
  }
}

/*Checks a vtol for being fully armed and fully repaired to see if ready to
leave reArm pad */
bool Droid::vtolHappy() const
{
  ASSERT_OR_RETURN(false, isVtolDroid(this), "not a VTOL droid");

  if (hitPoints < originalBody)
  {
    // VTOLs with less health than their original aren't happy
    return false;
  }

  if (droidType != DROID_WEAPON)
  {
    // Not an armed droid, so don't check the (non-existent) weapons
    return true;
  }

  /* NOTE: Previous code (r5410) returned false if a droid had no weapon,
	 *       which IMO isn't correct, but might be expected behaviour. I'm
	 *       also not sure if weapon droids (see the above droidType check)
	 *       can even have zero weapons. -- Giel
   */
  ASSERT_OR_RETURN(false, numWeapons > 0, "VTOL weapon droid without weapons found!");

  //check full complement of ammo
  for (int i = 0; i < numWeapons; ++i)
  {
    if (asWeaponStats[m_weaponList[i].nStat].vtolAttackRuns > 0
        && m_weaponList[i].usedAmmo != 0)
    {
      return false;
    }
  }

  return true;
}

/*returns a count of the base number of attack runs for the weapon attached to the droid*/
UWORD Droid::getNumAttackRuns(int weapon_slot)
{
  ASSERT_OR_RETURN(0, isVtolDroid(this), "not a VTOL Droid");
  // if weapon is a salvo weapon, then number of shots that can be fired = vtolAttackRuns * numRounds
  if (asWeaponStats[m_weaponList[weapon_slot].nStat].upgrade[owningPlayer].reloadTime)
  {
    return asWeaponStats[m_weaponList[weapon_slot].nStat].upgrade[owningPlayer].numRounds
           * asWeaponStats[m_weaponList[weapon_slot].nStat].vtolAttackRuns;
  }
  return asWeaponStats[m_weaponList[weapon_slot].nStat].vtolAttackRuns;
}

// see if there are any other vtols attacking the same target
// but still rearming
bool Droid::allVtolsRearmed() const
{
  // ignore all non vtols
  if (!isVtolDroid(this))
  {
    return true;
  }

  bool stillRearming = false;
  for (const Droid *psCurr = allDroidLists[owningPlayer]; psCurr; psCurr = psCurr->psNext)
  {
    if (vtolRearming(psCurr) &&
        psCurr->order.type == order.type &&
        psCurr->order.psObj == order.psObj)
    {
      stillRearming = true;
      break;
    }
  }

  return !stillRearming;
}

// true if a droid is currently attacking
bool Droid::droidAttacking() const
{
  //what about cyborgs?
  if (!(droidType == DROID_WEAPON || droidType == DROID_CYBORG ||
        droidType == DROID_CYBORG_SUPER))
  {
    return false;
  }

  if (action == DROID_ACTION::ATTACK ||
      action == DROID_ACTION::MOVETOATTACK ||
      action == DROID_ACTION::ROTATETOATTACK ||
      action == DROID_ACTION::VTOLATTACK ||
      action == DROID_ACTION::MOVEFIRE)
  {
    return true;
  }

  return false;
}

// true if a vtol droid currently returning to be rearmed
bool Droid::vtolRearming() const
{
  if (!isVtolDroid(this))
  {
    return false;
  }
  if (droidType != DROID_WEAPON)
  {
    return false;
  }

  if (action == DROID_ACTION::MOVETOREARM ||
      action == DROID_ACTION::WAITFORREARM ||
      action == DROID_ACTION::MOVETOREARMPOINT ||
      action == DROID_ACTION::WAITDURINGREARM)
  {
    return true;
  }

  return false;
}

unsigned Droid::getDroidLevel() const
{
  unsigned int numKills = experience / 65536;
  unsigned int i;

  // Search through the array of ranks until one is found
  // which requires more kills than the droid has.
  // Then fall back to the previous rank.
  const BRAIN_STATS *psStats = getBrainStats(psDroid);
  auto &vec = psStats->upgrade[owningPlayer].rankThresholds;
  for (i = 1; i < vec.size(); ++i)
  {
    if (numKills < vec.at(i))
    {
      return i - 1;
    }
  }

  // If the criteria of the last rank are met, then select the last one
  return vec.size() - 1;
}

// Set the asBits in a DROID structure given it's template.
void Droid::droidSetBits(const DroidStats *pTemplate)
{
  droidType = droidTemplateType(pTemplate);
  numWeapons = pTemplate->numWeaps;
  hitPoints = calcTemplateBody(pTemplate, owningPlayer);
  originalBody = hitPoints;
  expectedDamageDirect = 0;  // Begin life optimistically.
  expectedDamageIndirect = 0;  // Begin life optimistically.
  time = gameTime - deltaGameTime + 1;         // Start at beginning of tick.
  prevSpacetime.m_time = time - 1;  // -1 for interpolation.

  //create the droids weapons
  for (int inc = 0; inc < MAX_WEAPONS; inc++)
  {
    psActionTarget[inc] = nullptr;
    m_weaponList[inc].lastFired = 0;
    m_weaponList[inc].shotsFired = 0;
    // no weapon (could be a construction droid for example)
    // this is also used to check if a droid has a weapon, so zero it
    m_weaponList[inc].nStat = 0;
    m_weaponList[inc].ammo = 0;
    m_weaponList[inc].rot.direction = 0;
    m_weaponList[inc].rot.pitch = 0;
    m_weaponList[inc].rot.roll = 0;
    m_weaponList[inc].prevRot = m_weaponList[inc].rot;
    m_weaponList[inc].origin = ORIGIN_UNKNOWN;
    if (inc < pTemplate->numWeaps)
    {
      m_weaponList[inc].nStat = pTemplate->asWeaps[inc];
      m_weaponList[inc].ammo = (asWeaponStats + m_weaponList[inc].nStat)->upgrade[owningPlayer].numRounds;
    }
    m_weaponList[inc].usedAmmo = 0;
  }
  memcpy(asBits, pTemplate->asParts, sizeof(asBits));

  switch (getPropulsionStats(psDroid)->propulsionType)  // getPropulsionStats(psDroid) only defined after asBits[COMP_PROPULSION] is set.
  {
  case PROPULSION_TYPE_LIFT:
    blockedBits = AIR_BLOCKED;
    break;
  case PROPULSION_TYPE_HOVER:
    blockedBits = FEATURE_BLOCKED;
    break;
  case PROPULSION_TYPE_PROPELLOR:
    blockedBits = FEATURE_BLOCKED | LAND_BLOCKED;
    break;
  default:
    blockedBits = FEATURE_BLOCKED | WATER_BLOCKED;
    break;
  }
}

//initialises the droid movement model
void Droid::initDroidMovement()
{
  sMove.asPath.clear();
  sMove.pathIndex = 0;
}

// return whether a droid is IDF
bool Droid::idfDroid()
{
  //add Cyborgs
  //if (psDroid->droidType != DROID_WEAPON)
  if (!(droidType == DROID_WEAPON || droidType == DROID_CYBORG ||
        droidType == DROID_CYBORG_SUPER))
  {
    return false;
  }

  return !proj_Direct(m_weaponList[0].nStat + asWeaponStats);
}

bool Droid::droidUpdateRepair()
{
  ASSERT_OR_RETURN(false, action == DROID_ACTION::REPAIR, "unit does not have repair order");
  auto psStruct = std::move(psActionTarget[0]);

  ASSERT_OR_RETURN(false, psStruct->type == OBJ_STRUCTURE, "target is not a structure");
  int iRepairRate = constructorPoints(asConstructStats + asBits[COMP_CONSTRUCT], owningPlayer);

  /* add points to structure */
  structureRepair(psStruct, psDroid, iRepairRate);

  /* if not finished repair return true else complete repair and return false */
  if (psStruct->hitPoints < structureBody(psStruct))
  {
    return true;
  }
  else
  {
    objTrace(id, "Repaired of %s all done with %u", objInfo(psStruct), iRepairRate);
    return false;
  }
}

/*continue restoring a structure*/
bool Droid::droidUpdateRestore()
{
  ASSERT_OR_RETURN(false, action == DROID_ACTION::RESTORE, "Unit is not restoring");
  auto psStruct = order.psObj;
  ASSERT_OR_RETURN(false, psStruct->type == OBJECT_TYPE::STRUCTURE, "Target is not a structure");
  ASSERT_OR_RETURN(false, m_weaponList[0].nStat > 0, "Droid doesn't have any weapons");

  unsigned compIndex = m_weaponList[0].nStat;
  ASSERT_OR_RETURN(false, compIndex < numWeaponStats, "Invalid range referenced for numWeaponStats, %u > %u", compIndex, numWeaponStats);
  WEAPON_STATS *psStats = &asWeaponStats[compIndex];

  ASSERT_OR_RETURN(false, psStats->weaponSubClass == WSC_ELECTRONIC, "unit's weapon is not EW");

  unsigned restorePoints = calcDamage(weaponDamage(psStats, owningPlayer),
                                      psStats->weaponEffect, (GameObject *)psStruct);

  unsigned pointsToAdd = restorePoints * (gameTime - actionStarted) /
                         GAME_TICKS_PER_SEC;

  psStruct->resistance = (SWORD)(psStruct->resistance + (pointsToAdd - actionPoints));

  //store the amount just added
  actionPoints = pointsToAdd;

  CHECK_DROID(psDroid);

  /* check if structure is restored */
  if (psStruct->resistance < (SDWORD)structureResistance(psStruct->stats, psStruct->owningPlayer))
  {
    return true;
  }
  else
  {
    addConsoleMessage(_("Structure Restored") , DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
    psStruct->resistance = (UWORD)structureResistance(psStruct->stats,
                                                      psStruct->owningPlayer);
    return false;
  }
}

/* Update a construction droid while it is building
   returns true while building continues */
bool Droid::droidUpdateBuild()
{
  ASSERT_OR_RETURN(false, action == DROID_ACTION::BUILD, "%s (order %s) has wrong action for construction: %s",
                   droidGetName(psDroid), getDroidOrderName(order.type), getDroidActionName(action));

  auto psStruct = castStructure(order.psObj);
  if (psStruct == nullptr)
  {
    // Target missing, stop trying to build it.
    action = DROID_ACTION::NONE;
    return false;
  }

  ASSERT_OR_RETURN(false, psStruct->type == OBJECT_TYPE::STRUCTURE, "target is not a structure");
  ASSERT_OR_RETURN(false, asBits[COMP_CONSTRUCT] < numConstructStats, "Invalid construct pointer for unit");

  // First check the structure hasn't been completed by another droid
  if (psStruct->status == SS_BUILT)
  {
    // Check if line order build is completed, or we are not carrying out a line order build
    if (order.type != DORDER_LINEBUILD ||
        map_coord(order.pos) == map_coord(order.pos2))
    {
      cancelBuild(psDroid);
    }
    else
    {
      action = DROID_ACTION::NONE;	// make us continue line build
      setDroidTarget(psDroid, nullptr);
      setDroidActionTarget(psDroid, nullptr, 0);
    }
    return false;
  }

  // make sure we still 'own' the building in question
  if (!aiCheckAlliances(psStruct->owningPlayer, owningPlayer))
  {
    cancelBuild(psDroid);		// stop what you are doing fool it isn't ours anymore!
    return false;
  }

  unsigned constructPoints = constructorPoints(asConstructStats +
                                                                  asBits[COMP_CONSTRUCT], owningPlayer);

  unsigned pointsToAdd = constructPoints * (gameTime - actionStarted) /
                         GAME_TICKS_PER_SEC;

  structureBuild(psStruct, psDroid, pointsToAdd - actionPoints, constructPoints);

  //store the amount just added
  actionPoints = pointsToAdd;

  addConstructorEffect(psStruct);

  return true;
}

/* Deals damage to a droid
 * \param psDroid droid to deal damage to
 * \param damage amount of damage to deal
 * \param weaponClass the class of the weapon that deals the damage
 * \param weaponSubClass the subclass of the weapon that deals the damage
 * \param angle angle of impact (from the damage dealing projectile in relation to this droid)
 * \return > 0 when the dealt damage destroys the droid, < 0 when the droid survives
 *
 */
int32_t Droid::droidDamage(unsigned damage, WEAPON_CLASS weaponClass, WEAPON_SUBCLASS weaponSubClass, unsigned impactTime, bool isDamagePerSecond, int minDamage)
{
  int32_t relativeDamage;

  // VTOLs (and transporters in MP) on the ground take triple damage
  if ((isVtolDroid(psDroid) || (isTransporter(psDroid) && bMultiPlayer)) && (sMove.Status == MOVEINACTIVE))
  {
    damage *= 3;
  }

  relativeDamage = objDamage(psDroid, damage, originalBody, weaponClass, weaponSubClass, isDamagePerSecond, minDamage);

  if (relativeDamage > 0)
  {
    // reset the attack level
    if (secondaryGetState(psDroid, DSO_ATTACK_LEVEL) == DSS_ALEV_ATTACKED)
    {
      secondarySetState(DSO_ATTACK_LEVEL, DSS_ALEV_ALWAYS);
    }
    // Now check for auto return on droid's secondary orders (i.e. return on medium/heavy damage)
    secondaryCheckDamageLevel(psDroid);

    CHECK_DROID(psDroid);
  }
  else if (relativeDamage < 0)
  {
    // Droid destroyed
    debug(LOG_ATTACK, "droid (%d): DESTROYED", id);

    // Deal with score increase/decrease and messages to the player
    if (owningPlayer == selectedPlayer)
    {
      // TRANSLATORS:	Refers to the loss of a single unit, known by its name
      CONPRINTF(_("%s Lost!"), objInfo(psDroid));
      scoreUpdateVar(WD_UNITS_LOST);
      audio_QueueTrackMinDelayPos(ID_SOUND_UNIT_DESTROYED, UNIT_LOST_DELAY,
                                  m_position.x, m_position.y, m_position.z);
    }
    // only counts as a kill if it's not our ally
    else if (selectedPlayer < MAX_PLAYERS && !aiCheckAlliances(owningPlayer, selectedPlayer))
    {
      scoreUpdateVar(WD_UNITS_KILLED);
    }

    // Do we have a dying animation?
    if (displayData.imd->objanimpie[ANIM_EVENT_DYING] && animationEvent != ANIM_EVENT_DYING)
    {
      bool useDeathAnimation = true;
      //Babas should not burst into flames from non-heat weapons
      if (droidType == DROID_PERSON)
      {
        if (weaponClass == WC_HEAT)
        {
          // NOTE: 3 types of screams are available ID_SOUND_BARB_SCREAM - ID_SOUND_BARB_SCREAM3
          audio_PlayObjDynamicTrack(psDroid, ID_SOUND_BARB_SCREAM + (rand() % 3), nullptr);
        }
        else
        {
          useDeathAnimation = false;
        }
      }
      if (useDeathAnimation)
      {
        debug(LOG_DEATH, "%s droid %d (%p) is starting death animation", objInfo(psDroid), (int)id, static_cast<void *>(psDroid));
        timeAnimationStarted = gameTime;
        animationEvent = ANIM_EVENT_DYING;
      }
    }
    // Otherwise use the default destruction animation
    if (animationEvent != ANIM_EVENT_DYING)
    {
      debug(LOG_DEATH, "%s droid %d (%p) is toast", objInfo(psDroid), (int)id, static_cast<void *>(psDroid));
      // This should be sent even if multi messages are turned off, as the group message that was
      // sent won't contain the destroyed droid
      if (bMultiPlayer && !bMultiMessages)
      {
        bMultiMessages = true;
        destroyDroid(psDroid, impactTime);
        bMultiMessages = false;
      }
      else
      {
        destroyDroid(psDroid, impactTime);
      }
    }
  }

  return relativeDamage;
}

// recycle a droid (retain it's experience and some of it's cost)
void Droid::recycleDroid()
{
  CHECK_DROID(psDroid);

  // store the droids kills
  if (experience > 0)
  {
    recycled_experience[owningPlayer].push(experience);
  }

  // return part of the cost of the droid
  int cost = calcDroidPower(psDroid);
  cost = (cost / 2) * hitPoints / originalBody;
  addPower(owningPlayer, (UDWORD)cost);

  // hide the droid
  memset(visible, 0, sizeof(visible));

  if (psGroup)
  {
    psGroup->remove(psDroid);
  }

  triggerEvent(TRIGGER_OBJECT_RECYCLED, psDroid);
  vanishDroid(psDroid);

  Vector3i position = position.xzy();
  const auto mapCoord = map_coord({position.x, position.y});
  const auto psTile = mapTile(mapCoord);
  if (tileIsClearlyVisible(psTile))
  {
    addEffect(&position, EFFECT_EXPLOSION, EXPLOSION_TYPE_DISCOVERY, false, nullptr, false, gameTime - deltaGameTime + 1);
  }

  CHECK_DROID(psDroid);
}

bool Droid::removeDroidBase()
{
  CHECK_DROID(psDel);

  if (!alive())
  {
    // droid has already been killed, quit
    syncDebug("droid already dead");
    return true;
  }

  syncDebugDroid(psDel, '#');

  //kill all the droids inside the transporter
  if (isTransporter(psDel))
  {
    if (psGroup)
    {
      //free all droids associated with this Transporter
      Droid *psNext;
      for (auto psCurr = psGroup->psList; psCurr != nullptr && psCurr != psDel; psCurr = psNext)
      {
        psNext = psCurr->psGrpNext;

        /* add droid to droid list then vanish it - hope this works! - GJ */
        addDroid(psCurr, allDroidLists);
        vanishDroid(psCurr);
      }
    }
  }

  // leave the current group if any
  if (psGroup)
  {
    psGroup->remove(psDel);
    psGroup = nullptr;
  }

  /* Put Deliv. Pts back into world when a command droid dies */
  if (droidType == DROID_COMMAND)
  {
    for (auto psStruct = apsStructLists[owningPlayer]; psStruct; psStruct = psStruct->psNext)
    {
      // alexl's stab at a right answer.
      if (StructIsFactory(psStruct)
          && psStruct->pFunctionality->factory.psCommander == psDel)
      {
        assignFactoryCommandDroid(psStruct, nullptr);
      }
    }
  }

  // Check to see if constructor droid currently trying to find a location to build
  if (owningPlayer == selectedPlayer && selected && isConstructionDroid(psDel))
  {
    // If currently trying to build, kill off the placement
    if (tryingToGetLocation())
    {
      int numSelectedConstructors = 0;
      for (Droid *psDroid =
               allDroidLists[owningPlayer]; psDroid != nullptr; psDroid = psDroid->psNext)
      {
        numSelectedConstructors += psDroid->selected && isConstructionDroid(psDroid);
      }
      if (numSelectedConstructors <= 1)  // If we were the last selected construction droid.
      {
        kill3DBuilding();
      }
    }
  }

  if (owningPlayer == selectedPlayer)
  {
    intRefreshScreen();
  }

  killDroid(psDel);
  return true;
}

/* The main update routine for all droids */
void Droid::droidUpdate()
{
  Vector3i        dv;
  UDWORD          percentDamage, emissionInterval;
  GameObject *psBeingTargetted = nullptr;
  unsigned        i;

  CHECK_DROID(psDroid);

#ifdef DEBUG
  // Check that we are (still) in the sensor list
  if (droidType == DROID_SENSOR)
  {
    BASE_OBJECT	*psSensor;

    for (psSensor = apsSensorList[0]; psSensor; psSensor = psSensor->psNextFunc)
    {
      if (psSensor == (BASE_OBJECT *)psDroid)
      {
        break;
      }
    }
    ASSERT(psSensor == (BASE_OBJECT *)psDroid, "%s(%p) not in sensor list!",
           droidGetName(psDroid), static_cast<void *>(psDroid));
  }
#endif

  syncDebugDroid(psDroid, '<');

  if (flags.test(DIRTY))
  {
    visTilesUpdate(psDroid);
    droidBodyUpgrade();
    flags.set(DIRTY, false);
  }

  // Save old droid position, update time.
  prevSpacetime = getSpacetime(psDroid);
  time = gameTime;
  for (i = 0; i < MAX(1, numWeapons); ++i)
  {
    m_weaponList[i].prevRot = m_weaponList[i].rot;
  }

  if (animationEvent != ANIM_EVENT_NONE)
  {
    iIMDShape *imd = displayData.imd->objanimpie[animationEvent];
    if (imd && imd->objanimcycles > 0 && gameTime > timeAnimationStarted + imd->objanimtime * imd->objanimcycles)
    {
      // Done animating (animation is defined by body - other components should follow suit)
      if (animationEvent == ANIM_EVENT_DYING)
      {
        debug(LOG_DEATH, "%s (%d) died to burn anim (died=%d)", objInfo(psDroid), (int)id, (int)deathTime);
        destroyDroid(psDroid, gameTime);
        return;
      }
      animationEvent = ANIM_EVENT_NONE;
    }
  }
  else if (animationEvent == ANIM_EVENT_DYING)
  {
    return; // rest below is irrelevant if dead
  }

  // ai update droid
  aiUpdateDroid();

  // Update the droids order.
  orderUpdateDroid();

  // update the action of the droid
  actionUpdateDroid(psDroid);

  syncDebugDroid(psDroid, 'M');

  // update the move system
  moveUpdateDroid(psDroid);

  /* Only add smoke if they're visible */
  if (visibleForLocalDisplay() && droidType != DROID_PERSON)
  {
    // need to clip this value to prevent overflow condition
    percentDamage = 100 - clip<UDWORD>(PERCENT(hitPoints, originalBody), 0, 100);

    // Is there any damage?
    if (percentDamage >= 25)
    {
      if (percentDamage >= 100)
      {
        percentDamage = 99;
      }

      emissionInterval = CALC_DROID_SMOKE_INTERVAL(percentDamage);

      uint32_t effectTime = std::max(gameTime - deltaGameTime + 1, lastEmission + emissionInterval);
      if (gameTime >= effectTime)
      {
        dv.x = position.x + DROID_DAMAGE_SPREAD;
        dv.z = position.y + DROID_DAMAGE_SPREAD;
        dv.y = position.z;

        dv.y += (displayData.imd->max.y * 2);
        addEffect(&dv, EFFECT_SMOKE, SMOKE_TYPE_DRIFTING_SMALL, false, nullptr, 0, effectTime);
        lastEmission = effectTime;
      }
    }
  }

  // -----------------
  /* Are we a sensor droid or a command droid? Show where we target for selectedPlayer. */
  if (owningPlayer == selectedPlayer && (droidType == DROID_SENSOR || droidType == DROID_COMMAND))
  {
    /* If we're attacking or sensing (observing), then... */
    if ((psBeingTargetted = orderStateObj(psDroid, DORDER_ATTACK))
        || (psBeingTargetted = orderStateObj(psDroid, DORDER_OBSERVE)))
    {
      psBeingTargetted->flags.set(TARGETED, true);
    }
    else if (secondaryGetState(psDroid, DSO_HALTTYPE) != DSS_HALT_PURSUE &&
             psActionTarget[0] != nullptr &&
             validTarget(psDroid, psActionTarget[0], 0) &&
             (action == DROID_ACTION::ATTACK ||
              action == DROID_ACTION::OBSERVE ||
              orderState(psDroid, DORDER_HOLD)))
    {
      psBeingTargetted = psActionTarget[0];
      psBeingTargetted->flags.set(TARGETED, true);
    }
  }
  // ------------------------
  // if we are a repair turret, then manage incoming damaged droids, (just like repair facility)
  // unlike a repair facility
  // 	- we don't really need to move droids to us, we can come ourselves
  //	- we don't steal work from other repair turrets/ repair facilities
  Droid *psOther;
  if (droidType == DROID_REPAIR || droidType == DROID_CYBORG_REPAIR)
  {
    for (psOther = allDroidLists[owningPlayer]; psOther; psOther = psOther->psNext)
    {
      // unlike repair facility, no droid  can have DORDER_RTR_SPECIFIED with another droid as target, so skip that check
      if (psOther->order.type == DORDER_RTR &&
          psOther->order.rtrType == RTR_TYPE_DROID &&
          psOther->action != DROID_ACTION::WAITFORREPAIR &&
          psOther->action != DROID_ACTION::MOVETOREPAIRPOINT &&
          psOther->action != DROID_ACTION::WAITDURINGREPAIR)
      {
        if (psOther->hitPoints >= psOther->originalBody)
        {
          // set droid points to max
          psOther->hitPoints = psOther->originalBody;
          // if completely repaired reset order
          secondarySetState(psOther, DSO_RETURN_TO_LOC, DSS_NONE);

          if (hasCommander(psOther))
          {
            // return a droid to it's command group
            Droid *psCommander = psOther->psGroup->psCommander;
            orderDroidObj(psOther, DORDER_GUARD, psCommander, ModeImmediate);
          }
          continue;
        }
      }

      else if (psOther->order.rtrType == RTR_TYPE_DROID
               //is being, or waiting for repairs..
               && (psOther->action == DROID_ACTION::WAITFORREPAIR || psOther->action == DROID_ACTION::WAITDURINGREPAIR)
               // don't steal work from others
               && psOther->order.psObj == psDroid)
      {
        if (!actionReachedDroid(psDroid, psOther))
        {
          actionDroid(psOther, DROID_ACTION::MOVE, psDroid, position.x, position.y);
        }

      }
    }
  }
  // ------------------------
  // See if we can and need to self repair.
  if (!isVtolDroid(psDroid) && hitPoints < originalBody && asBits[COMP_REPAIRUNIT] != 0 && selfRepairEnabled(owningPlayer))
  {
    droidUpdateDroidSelfRepair(psDroid);
  }


  /* Update the fire damage data */
  if (periodicalDamageStart != 0 && periodicalDamageStart != gameTime - deltaGameTime)  // -deltaGameTime, since projectiles are updated after droids.
  {
    // The periodicalDamageStart has been set, but is not from the previous tick, so we must be out of the fire.
    periodicalDamage = 0;  // Reset periodical damage done this tick.
    if (periodicalDamageStart + BURN_TIME < gameTime)
    {
      // Finished periodical damaging.
      periodicalDamageStart = 0;
    }
    else
    {
      // do hardcoded burn damage (this damage automatically applied after periodical damage finished)
      droidDamage(BURN_DAMAGE, WC_HEAT, WSC_FLAME, gameTime - deltaGameTime / 2 + 1, true, BURN_MIN_DAMAGE);
    }
  }

  // At this point, the droid may be dead due to periodical damage or hardcoded burn damage.
  if (isDead(psDroid))
  {
    return;
  }

  calcDroidIllumination(psDroid);

  // Check the resistance level of the droid
  if ((id + gameTime) / 833 != (id + gameTime - deltaGameTime) / 833)
  {
    // Zero resistance means not currently been attacked - ignore these
    if (resistance && resistance < droidResistance(psDroid))
    {
      // Increase over time if low
      resistance++;
    }
  }

  syncDebugDroid(psDroid, '>');

  CHECK_DROID(psDroid);
}

/* Set up a droid to build a structure - returns true if successful */
DroidStartBuild Droid::droidStartBuild()
{
  Structure *psStruct = nullptr;
  ASSERT_OR_RETURN(DroidStartBuildFailed, psDroid != nullptr, "Bad Droid");
  CHECK_DROID(psDroid);

  /* See if we are starting a new structure */
  if (order.psObj == nullptr &&
      (order.type == DORDER_BUILD ||
       order.type == DORDER_LINEBUILD))
  {
    StructureStats *psStructStat = order.psStats;

    auto ia = (ItemAvailability)apStructTypeLists[owningPlayer][psStructStat - asStructureStats];
    if (ia != AVAILABLE && ia != REDUNDANT)
    {
      ASSERT(false, "Cannot build \"%s\" for player %d.", psStructStat->name.toUtf8().c_str(), owningPlayer);
      cancelBuild();
      objTrace(id, "DroidStartBuildFailed: not researched");
      return DroidStartBuildFailed;
    }

    //need to check structLimits have not been exceeded
    if (psStructStat->curCount[owningPlayer] >= psStructStat->upgrade[owningPlayer].limit)
    {
      cancelBuild();
      objTrace(id, "DroidStartBuildFailed: structure limits");
      return DroidStartBuildFailed;
    }
    // Can't build on burning oil derricks.
    if (psStructStat->type == REF_RESOURCE_EXTRACTOR && fireOnLocation(order.pos.x, order.pos.y))
    {
      // Don't cancel build, since we can wait for it to stop burning.
      objTrace(id, "DroidStartBuildPending: burning");
      return DroidStartBuildPending;
    }
    //ok to build
    psStruct = buildStructureDir(psStructStat, order.pos.x, order.pos.y, order.direction, owningPlayer, false);
    if (!psStruct)
    {
      cancelBuild();
      objTrace(id, "DroidStartBuildFailed: buildStructureDir failed");
      return DroidStartBuildFailed;
    }
    psStruct->hitPoints = (psStruct->hitPoints + 9) / 10;  // Structures start at 10% health. Round up.
  }
  else
  {
    /* Check the structure is still there to build (joining a partially built struct) */
    psStruct = castStructure(order.psObj);
    if (psStruct == nullptr)
    {
      psStruct = castStructure(worldTile(actionPos)->psObject);
    }
    if (psStruct && !droidNextToStruct(psDroid, psStruct))
    {
      /* Nope - stop building */
      debug(LOG_NEVER, "not next to structure");
      objTrace(id, "DroidStartBuildSuccess: not next to structure");
    }
  }

  //check structure not already built, and we still 'own' it
  if (psStruct)
  {
    if (psStruct->status != SS_BUILT && aiCheckAlliances(psStruct->owningPlayer, owningPlayer))
    {
      actionStarted = gameTime;
      actionPoints = 0;
      setDroidTarget(psDroid, psStruct);
      setDroidActionTarget(psDroid, psStruct, 0);
      objTrace(id, "DroidStartBuild: set target");
    }

    if (psStruct->visibleForLocalDisplay())
    {
      audio_PlayObjStaticTrackCallback(psDroid, ID_SOUND_CONSTRUCTION_START, droidBuildStartAudioCallback);
    }
  }
  CHECK_DROID(psDroid);

  objTrace(id, "DroidStartBuildSuccess");
  return DroidStartBuildSuccess;
}

bool Droid::droidUpdateDemolishing()
{
  CHECK_DROID(psDroid);

  ASSERT_OR_RETURN(false, action == DROID_ACTION::DEMOLISH, "unit is not demolishing");
  auto psStruct = (Structure*) order.psObj;
  ASSERT_OR_RETURN(false, psStruct->type == OBJECT_TYPE::STRUCTURE, "target is not a structure");

  int constructRate = 5 * constructorPoints(asConstructStats + asBits[COMP_CONSTRUCT], owningPlayer);
  int pointsToAdd = gameTimeAdjustedAverage(constructRate);

  structureDemolish(psStruct, psDroid, pointsToAdd);

  addConstructorEffect(psStruct);

  CHECK_DROID(psDroid);

  return true;
}

/*Deals with building a module - checking if any droid is currently doing this
 - if so, helping to build the current one*/
void Droid::setUpBuildModule()
{
  Vector2i tile = map_coord(order.pos);

  //check not another Truck started
  Structure *psStruct = getTileStructure(tile.x, tile.y);
  if (psStruct)
  {
    // if a droid is currently building, or building is in progress of being built/upgraded the droid's order should be DORDER_HELPBUILD
    if (checkDroidsBuilding(psStruct) || !psStruct->status)
    {
      //set up the help build scenario
      order.type = DORDER_HELPBUILD;
      setDroidTarget(psDroid, (GameObject *)psStruct);
      if (droidStartBuild())
      {
        action = DROID_ACTION::BUILD;
        return;
      }
    }
    else
    {
      if (nextModuleToBuild(psStruct, -1) > 0)
      {
        //no other droids building so just start it off
        if (droidStartBuild())
        {
          action = DROID_ACTION::BUILD;
          return;
        }
      }
    }
  }
  cancelBuild();
}

/*checks to see if an electronic warfare weapon is attached to the droid*/
bool Droid::electronicDroid() const
{
  CHECK_DROID(psDroid);

  //use slot 0 for now
  if (numWeapons > 0 && asWeaponStats[m_weaponList[0].nStat].weaponSubClass == WSC_ELECTRONIC)
  {
    return true;
  }

  if (droidType == DROID_COMMAND && psGroup && psGroup->psCommander == psDroid)
  {
    // if a commander has EW units attached it is electronic
    for (const Droid *psCurr = psGroup->psList; psCurr; psCurr = psCurr->psGrpNext)
    {
      if (psDroid != psCurr && electronicDroid(psCurr))
      {
        return true;
      }
    }
  }

  return false;
}

/*checks to see if the droid is currently being repaired by another*/
bool Droid::droidUnderRepair() const
{
  CHECK_DROID(psDroid);

  //droid must be damaged
  if (droidIsDamaged(psDroid))
  {
    //look thru the list of players droids to see if any are repairing this droid
    for (const Droid *psCurr = allDroidLists[owningPlayer]; psCurr != nullptr; psCurr = psCurr->psNext)
    {
      if ((psCurr->droidType == DROID_REPAIR || psCurr->droidType ==
                                                    DROID_CYBORG_REPAIR) && psCurr->action ==
              DROID_ACTION::DROIDREPAIR && psCurr->order.psObj == psDroid)
      {
        return true;
      }
    }
  }
  return false;
}

/* returns true if it's a VTOL weapon droid which has completed all runs */
bool Droid::vtolEmpty() const
{
  CHECK_DROID(psDroid);

  if (!isVtolDroid(psDroid))
  {
    return false;
  }
  if (droidType != DROID_WEAPON)
  {
    return false;
  }

  for (int i = 0; i < numWeapons; i++)
  {
    if (asWeaponStats[m_weaponList[i].nStat].vtolAttackRuns > 0 &&
        m_weaponList[i].usedAmmo < getNumAttackRuns(psDroid, i))
    {
      return false;
    }
  }

  return true;
}

/* returns true if it's a VTOL weapon droid which still has full ammo */
bool Droid::vtolFull() const
{
  CHECK_DROID(psDroid);

  if (!isVtolDroid(psDroid))
  {
    return false;
  }
  if (droidType != DROID_WEAPON)
  {
    return false;
  }

  for (int i = 0; i < numWeapons; i++)
  {
    if (asWeaponStats[m_weaponList[i].nStat].vtolAttackRuns > 0 &&
        m_weaponList[i].usedAmmo > 0)
    {
      return false;
    }
  }

  return true;
}

/* Do the AI for a droid */
void Droid::aiUpdateDroid()
{
  bool		lookForTarget, updateTarget;

  ASSERT(psDroid != nullptr, "Invalid droid pointer");
  if (!psDroid || isDead((GameObject *)psDroid))
  {
    return;
  }

  if (droidType != DROID_SENSOR && numWeapons == 0)
  {
    return;
  }

  lookForTarget = false;
  updateTarget = false;

  // look for a target if doing nothing
  if (orderState(psDroid, DORDER_NONE) ||
      orderState(psDroid, DORDER_GUARD) ||
      orderState(psDroid, DORDER_HOLD))
  {
    lookForTarget = true;
  }
  // but do not choose another target if doing anything while guarding
  // exception for sensors, to allow re-targetting when target is doomed
  if (orderState(psDroid, DORDER_GUARD) && action != DROID_ACTION::NONE && droidType != DROID_SENSOR)
  {
    lookForTarget = false;
  }
  // don't look for a target if sulking
  if (action == DROID_ACTION::SULK)
  {
    lookForTarget = false;
  }

  /* Only try to update target if already have some target */
  if (action == DROID_ACTION::ATTACK ||
      action == DROID_ACTION::MOVEFIRE ||
      action == DROID_ACTION::MOVETOATTACK ||
      action == DROID_ACTION::ROTATETOATTACK)
  {
    updateTarget = true;
  }
  if ((orderState(psDroid, DORDER_OBSERVE) || orderState(psDroid, DORDER_ATTACKTARGET)) &&
      order.psObj && order.psObj->deathTime)
  {
    lookForTarget = true;
    updateTarget = false;
  }

  /* Don't update target if we are sent to attack and reached attack destination (attacking our target) */
  if (orderState(psDroid, DORDER_ATTACK) && psActionTarget[0] == order.psObj)
  {
    updateTarget = false;
  }

  // don't look for a target if there are any queued orders
  if (listSize > 0)
  {
    lookForTarget = false;
    updateTarget = false;
  }

  // don't allow units to start attacking if they will switch to guarding the commander
  // except for sensors: they still look for targets themselves, because
  // they have wider view
  if (hasCommander(psDroid) && droidType != DROID_SENSOR)
  {
    lookForTarget = false;
    updateTarget = false;
  }

  if (bMultiPlayer && isVtolDroid(psDroid) && isHumanPlayer(owningPlayer))
  {
    lookForTarget = false;
    updateTarget = false;
  }

  // CB and VTOL CB droids can't autotarget.
  if (droidType == DROID_SENSOR && !standardSensorDroid(psDroid))
  {
    lookForTarget = false;
    updateTarget = false;
  }

  // do not attack if the attack level is wrong
  if (secondaryGetState(psDroid, DSO_ATTACK_LEVEL) != DSS_ALEV_ALWAYS)
  {
    lookForTarget = false;
  }

  /* For commanders and non-assigned non-commanders: look for a better target once in a while */
  if (!lookForTarget && updateTarget && numWeapons > 0 && !hasCommander(psDroid)
      && (id + gameTime) / TARGET_UPD_SKIP_FRAMES != (id + gameTime - deltaGameTime) / TARGET_UPD_SKIP_FRAMES)
  {
    for (unsigned i = 0; i < numWeapons; ++i)
    {
      updateAttackTarget((GameObject *)psDroid, i);
    }
  }

  /* Null target - see if there is an enemy to attack */

  if (lookForTarget && !updateTarget)
  {
    GameObject *psTarget;
    if (droidType == DROID_SENSOR)
    {
      if (aiChooseSensorTarget(psDroid, &psTarget))
      {
        if (!orderState(psDroid, DORDER_HOLD)
            && secondaryGetState(psDroid, DSO_HALTTYPE) == DSS_HALT_PURSUE)
        {
          order = DroidOrder(DORDER_OBSERVE, psTarget);
        }
        actionDroid(psDroid, DROID_ACTION::OBSERVE, psTarget);
      }
    }
    else
    {
      if (aiChooseTarget((GameObject *)psDroid, &psTarget, 0, true, nullptr))
      {
        if (!orderState(psDroid, DORDER_HOLD)
            && secondaryGetState(psDroid, DSO_HALTTYPE) == DSS_HALT_PURSUE)
        {
          order = DroidOrder(DORDER_ATTACK, psTarget);
        }
        actionDroid(psDroid, DROID_ACTION::ATTACK, psTarget);
      }
    }
  }
}

/** This function checks its order list: if a given order needs a target and the target has died, the order is removed from the list.*/
void Droid::orderCheckList()
{
  for (unsigned i = 0; i < asOrderList.size(); ++i)
  {
    auto psTarget = asOrderList[i].psObj;
    if (psTarget != nullptr && psTarget->deathTime)
    {
      if ((int)i < listSize)
      {
        syncDebugObject(psTarget, '-');
        syncDebug("droid%d list erase dead droid%d", id, psTarget->id);
      }
      orderDroidListEraseRange(psDroid, i, i + 1);
      --i;  // If this underflows, the ++i will overflow it back.
    }
  }
}

/** This function returns an order which is assigned according to the location and droid. Uses altOrder flag to choose between a direct order or an altOrder.*/
DROID_ORDER Droid::chooseOrderLoc(UDWORD x, UDWORD y, bool altOrder)
{
  DROID_ORDER order = DORDER_NONE;
  PROPULSION_TYPE propulsion = getPropulsionStats(psDroid)->propulsionType;

  if (isTransporter(psDroid) && game.type == LEVEL_TYPE::CAMPAIGN)
  {
    // transports can't be controlled in campaign
    return DORDER_NONE;
  }

  // default to move; however, we can only end up on a tile
  // where can stay, ie VTOLs must be able to land as well
  if (isVtolDroid(psDroid))
  {
    propulsion = PROPULSION_TYPE_WHEELED;
  }
  if (!fpathBlockingTile(map_coord(x), map_coord(y), propulsion))
  {
    order = DORDER_MOVE;
  }

  // scout if alt was pressed
  if (altOrder)
  {
    order = DORDER_SCOUT;
    if (isVtolDroid(psDroid))
    {
      // Patrol if in a VTOL
      order = DORDER_PATROL;
    }
  }

  // and now we want Transporters to fly! - in multiPlayer!!
  if (isTransporter(psDroid) && game.type == LEVEL_TYPE::SKIRMISH)
  {
    /* in MultiPlayer - if ALT-key is pressed then need to get the Transporter
		 * to fly to location and all units disembark */
    if (altOrder)
    {
      order = DORDER_DISEMBARK;
    }
  }
  else if (secondaryGetState(psDroid, DSO_CIRCLE, ModeQueue) == DSS_CIRCLE_SET)  // ModeQueue here means to check whether we pressed the circle button, whether or not it synched yet. The reason for this weirdness is that a circle order makes no sense as a secondary state in the first place (the circle button _should_ have been only in the UI, not in the game state..!), so anything dealing with circle orders will necessarily be weird.
  {
    order = DORDER_CIRCLE;
    secondarySetState(DSO_CIRCLE, DSS_NONE);
  }
  else if (secondaryGetState(psDroid, DSO_PATROL, ModeQueue) == DSS_PATROL_SET)  // ModeQueue here means to check whether we pressed the patrol button, whether or not it synched yet. The reason for this weirdness is that a patrol order makes no sense as a secondary state in the first place (the patrol button _should_ have been only in the UI, not in the game state..!), so anything dealing with patrol orders will necessarily be weird.
  {
    order = DORDER_PATROL;
    secondarySetState(DSO_PATROL, DSS_NONE);
  }

  return order;
}

/** This function goes to the droid's order list and sets a new order to it from its order list.*/
bool Droid::orderDroidList()
{
  if (listSize > 0)
  {
    // there are some orders to give
    DROID_ORDER_DATA sOrder = asOrderList[0];
    orderDroidListEraseRange(psDroid, 0, 1);

    switch (sOrder.type)
    {
    case DORDER_MOVE:
    case DORDER_SCOUT:
    case DORDER_DISEMBARK:
      ASSERT(sOrder.psObj == nullptr && sOrder.psStats == nullptr, "Extra %s parameters.", getDroidOrderName(sOrder.type));
      sOrder.psObj = nullptr;
      sOrder.psStats = nullptr;
      break;
    case DORDER_ATTACK:
    case DORDER_REPAIR:
    case DORDER_OBSERVE:
    case DORDER_DROIDREPAIR:
    case DORDER_FIRESUPPORT:
    case DORDER_DEMOLISH:
    case DORDER_HELPBUILD:
    case DORDER_BUILDMODULE:
    case DORDER_RECOVER:
      ASSERT(sOrder.psStats == nullptr, "Extra %s parameters.", getDroidOrderName(sOrder.type));
      sOrder.psStats = nullptr;
      break;
    case DORDER_BUILD:
    case DORDER_LINEBUILD:
      ASSERT(sOrder.psObj == nullptr, "Extra %s parameters.", getDroidOrderName(sOrder.type));
      sOrder.psObj = nullptr;
      break;
    default:
      ASSERT(false, "orderDroidList: Invalid order");
      return false;
    }

    orderDroidBase(&sOrder);
    syncDebugDroid(psDroid, 'o');

    return true;
  }

  return false;
}

/** Add an order to a droid's order list
 * @todo needs better documentation.
 */
void Droid::orderDroidAdd(DROID_ORDER_DATA *psOrder)
{
  if (listSize >= asOrderList.size())
  {
    // Make more room to store the order.
    asOrderList.resize(asOrderList.size() + 1);
  }

  asOrderList[listSize] = *psOrder;
  listSize += 1;

  // if not doing anything - do it immediately
  if (listSize <= 1 &&
      (order.type == DORDER_NONE ||
       order.type == DORDER_GUARD ||
       order.type == DORDER_PATROL ||
       order.type == DORDER_CIRCLE ||
       order.type == DORDER_HOLD))
  {
    orderDroidList();
  }
}

/** This function returns false if droid's order and order don't match or the order is not a location order. Else ppsStats = psDroid->psTarStats, (pX,pY) = psDroid.(orderX,orderY) and it returns true.
 * @todo seems closely related to orderStateLoc()
 */
bool Droid::orderStateStatsLoc(DROID_ORDER order, StructureStats **ppsStats)
{
  bool	match = false;

  switch (order)
  {
  case DORDER_BUILD:
  case DORDER_LINEBUILD:
    if (order.type == DORDER_BUILD ||
        order.type == DORDER_LINEBUILD)
    {
      match = true;
    }
    break;
  default:
    break;
  }
  if (!match)
  {
    return false;
  }

  // check the order is one with stats and a location
  switch (order.type)
  {
  default:
    // not a stats/location order - return false
    return false;
    break;
  case DORDER_BUILD:
  case DORDER_LINEBUILD:
    if (action == DROID_ACTION::MOVETOBUILD)
    {
      *ppsStats = order.psStats;
      return true;
    }
    break;
  }

  return false;
}

/** This function sends the droid an order with a location.
 * If the mode is ModeQueue, the order is added to the droid's order list using sendDroidInfo(), else, a DROID_ORDER_DATA is alloc, the old order list is erased, and the order is sent using orderDroidBase().
 */
void Droid::orderDroidLoc(DROID_ORDER order, UDWORD x, UDWORD y, QUEUE_MODE mode)
{
  ASSERT_OR_RETURN(, validOrderForLoc(order), "Invalid order for location");

  DROID_ORDER_DATA sOrder(order, Vector2i(x, y));
  if (mode == ModeQueue)
  {
    sendDroidInfo(psDroid, sOrder, false);
    return;  // Wait to receive our order before changing the droid.
  }

  orderClearDroidList(psDroid);
  orderDroidBase(&sOrder);
}

/** This function updates all the orders status, according with psdroid's current order and state.
 */
void Droid::orderUpdateDroid()
{
  GameObject *psObj = nullptr;
  Structure *psStruct, *psWall;
  SDWORD			xdiff, ydiff;
  bool			bAttack;
  SDWORD			xoffset, yoffset;

  // clear the target if it has died
  if (order.psObj && order.psObj->deathTime)
  {
    syncDebugObject(order.psObj, '-');
    setDroidTarget(psDroid, nullptr);
    objTrace(id, "Target dead");
  }

  //clear its base struct if its died
  if (psBaseStruct && psBaseStruct->deathTime)
  {
    syncDebugStructure(psBaseStruct, '-');
    setDroidBase(psDroid, nullptr);
    objTrace(id, "Base struct dead");
  }

  // check for died objects in the list
  orderCheckList();

  if (isDead(psDroid))
  {
    return;
  }

  switch (order.type)
  {
  case DORDER_NONE:
  case DORDER_HOLD:
    // see if there are any orders queued up
    if (orderDroidList())
    {
      // started a new order, quit
      break;
    }
    // if you are in a command group, default to guarding the commander
    else if (hasCommander(psDroid) && order.type != DORDER_HOLD
             && order.psStats != structGetDemolishStat())  // stop the constructor auto repairing when it is about to demolish
    {
      orderDroidObj(psDroid, DORDER_GUARD, psGroup->psCommander, ModeImmediate);
    }
    else if (isTransporter(psDroid) && !bMultiPlayer)
    {

    }
    // default to guarding
    else if (!tryDoRepairlikeAction(psDroid)
             && order.type != DORDER_HOLD
             && order.psStats != structGetDemolishStat()
             && secondaryGetState(psDroid, DSO_HALTTYPE) == DSS_HALT_GUARD
             && !isVtolDroid(psDroid))
    {
      orderDroidLoc(DORDER_GUARD, position.x, position.y, ModeImmediate);
    }
    break;
  case DORDER_TRANSPORTRETURN:
    if (action == DROID_ACTION::NONE)
    {
      missionMoveTransporterOffWorld(psDroid);

      /* clear order */
      order = DroidOrder(DORDER_NONE);
    }
    break;
  case DORDER_TRANSPORTOUT:
    if (action == DROID_ACTION::NONE)
    {
      if (owningPlayer == selectedPlayer)
      {
        if (getDroidsToSafetyFlag())
        {
          //move droids in Transporter into holding list
          moveDroidsToSafety(psDroid);
          //we need the transporter to just sit off world for a while...
          orderDroid(DORDER_TRANSPORTIN, ModeImmediate);
          /* set action transporter waits for timer */
          actionDroid(psDroid, DROID_ACTION::TRANSPORTWAITTOFLYIN);

          missionSetReinforcementTime(gameTime);
          //don't do this until waited for the required time
          //fly Transporter back to get some more droids
          //orderDroidLoc( psDroid, DORDER_TRANSPORTIN,
          //getLandingX(selectedPlayer), getLandingY(selectedPlayer));
        }
        else
        {
          //the script can call startMission for this callback for offworld missions
          triggerEvent(TRIGGER_TRANSPORTER_EXIT, psDroid);
          /* clear order */
          order = DroidOrder(DORDER_NONE);
        }

        sMove.speed = 0; // Prevent radical movement vector when adjusting from home to away map exit and entry coordinates.
      }
    }
    break;
  case DORDER_TRANSPORTIN:
    if ((action == DROID_ACTION::NONE) &&
        (sMove.Status == MOVEINACTIVE))
    {
      /* clear order */
      order = DroidOrder(DORDER_NONE);

      //FFS! You only wan't to do this if the droid being tracked IS the transporter! Not all the time!
      // What about if your happily playing the game and tracking a droid, and re-enforcements come in!
      // And suddenly BLAM!!!! It drops you out of camera mode for no apparent reason! TOTALY CONFUSING
      // THE PLAYER!
      //
      // Just had to get that off my chest....end of rant.....
      //
      if (psDroid == getTrackingDroid())    // Thats better...
      {
        /* deselect transporter if have been tracking */
        if (getWarCamStatus())
        {
          camToggleStatus();
        }
      }

      DeSelectDroid();

      /*don't try the unload if moving droids to safety and still got some
      droids left  - wait until full and then launch again*/
      if (owningPlayer == selectedPlayer && getDroidsToSafetyFlag() &&
          missionDroidsRemaining(selectedPlayer))
      {
        resetTransporter();
      }
      else
      {
        unloadTransporter(psDroid, position.x, position.y, false);
      }
    }
    break;
  case DORDER_MOVE:
    // Just wait for the action to finish then clear the order
    if (action == DROID_ACTION::NONE || action == DROID_ACTION::ATTACK)
    {
      order = DroidOrder(DORDER_NONE);
    }
    break;
  case DORDER_RECOVER:
    if (order.psObj == nullptr)
    {
      order = DroidOrder(DORDER_NONE);
    }
    else if (action == DROID_ACTION::NONE)
    {
      // stopped moving, but still haven't got the artifact
      actionDroid(psDroid, DROID_ACTION::MOVE, order.psObj->position.x, order.psObj->position.y);
    }
    break;
  case DORDER_SCOUT:
  case DORDER_PATROL:
    // if there is an enemy around, attack it
    if (action == DROID_ACTION::MOVE || action == DROID_ACTION::MOVEFIRE || (action == DROID_ACTION::NONE && isVtolDroid(psDroid)))
    {
      bool tooFarFromPath = false;
      if (isVtolDroid(psDroid) && order.type == DORDER_PATROL)
      {
        // Don't stray too far from the patrol path - only attack if we're near it
        // A fun algorithm to detect if we're near the path
        Vector2i delta = order.pos - order.pos2;
        if (delta == Vector2i(0, 0))
        {
          tooFarFromPath = false;
        }
        else if (abs(delta.x) >= abs(delta.y) &&
                 MIN(order.pos.x, order.pos2.x) - SCOUT_DIST <= position.x &&
                 position.x <= MAX(order.pos.x, order.pos2.x) + SCOUT_DIST)
        {
          tooFarFromPath = (abs((position.x - order.pos.x) * delta.y / delta.x +
                                order.pos.y - position.y) > SCOUT_DIST);
        }
        else if (abs(delta.x) <= abs(delta.y) &&
                 MIN(order.pos.y, order.pos2.y) - SCOUT_DIST <= position.y &&
                 position.y <= MAX(order.pos.y, order.pos2.y) + SCOUT_DIST)
        {
          tooFarFromPath = (abs((position.y - order.pos.y) * delta.x / delta.y +
                                order.pos.x - position.x) > SCOUT_DIST);
        }
        else
        {
          tooFarFromPath = true;
        }
      }
      if (!tooFarFromPath)
      {
        // true if in condition to set actionDroid to attack/observe
        bool attack = secondaryGetState(psDroid, DSO_ATTACK_LEVEL) == DSS_ALEV_ALWAYS &&
                      aiBestNearestTarget(psDroid, &psObj, 0, SCOUT_ATTACK_DIST) >= 0;
        switch (droidType)
        {
        case DROID_CONSTRUCT:
        case DROID_CYBORG_CONSTRUCT:
        case DROID_REPAIR:
        case DROID_CYBORG_REPAIR:
          tryDoRepairlikeAction();
          break;
        case DROID_WEAPON:
        case DROID_CYBORG:
        case DROID_CYBORG_SUPER:
        case DROID_PERSON:
        case DROID_COMMAND:
          if (attack)
          {
            actionDroid(psDroid, DROID_ACTION::ATTACK, psObj);
          }
          break;
        case DROID_SENSOR:
          if (attack)
          {
            actionDroid(psDroid, DROID_ACTION::OBSERVE, psObj);
          }
          break;
        default:
          actionDroid(psDroid, DROID_ACTION::NONE);
          break;
        }
      }
    }
    if (action == DROID_ACTION::NONE)
    {
      xdiff = position.x - order.pos.x;
      ydiff = position.y - order.pos.y;
      if (xdiff * xdiff + ydiff * ydiff < SCOUT_DIST * SCOUT_DIST)
      {
        if (order.type == DORDER_PATROL)
        {
          // see if we have anything queued up
          if (orderDroidList())
          {
            // started a new order, quit
            break;
          }
          if (isVtolDroid(psDroid) && !vtolFull() && (secondaryOrder & DSS_ALEV_MASK) != DSS_ALEV_NEVER)
          {
            moveToRearm();
            break;
          }
          // head back to the other point
          std::swap(order.pos, order.pos2);
          actionDroid(psDroid, DROID_ACTION::MOVE, order.pos.x, order.pos.y);
        }
        else
        {
          order = DroidOrder(DORDER_NONE);
        }
      }
      else
      {
        actionDroid(psDroid, DROID_ACTION::MOVE, order.pos.x, order.pos.y);
      }
    }
    else if (((action == DROID_ACTION::ATTACK) ||
              (action == DROID_ACTION::VTOLATTACK) ||
              (action == DROID_ACTION::MOVETOATTACK) ||
              (action == DROID_ACTION::ROTATETOATTACK) ||
              (action == DROID_ACTION::OBSERVE) ||
              (action == DROID_ACTION::MOVETOOBSERVE)) &&
             secondaryGetState(psDroid, DSO_HALTTYPE) != DSS_HALT_PURSUE)
    {
      // attacking something - see if the droid has gone too far, go up to twice the distance we want to go, so that we don't repeatedly turn back when the target is almost in range.
      if (objPosDiffSq(position, Vector3i(actionPos, 0)) > (SCOUT_ATTACK_DIST * 2 * SCOUT_ATTACK_DIST * 2))
      {
        actionDroid(psDroid, DROID_ACTION::RETURNTOPOS, actionPos.x, actionPos.y);
      }
    }
    if (order.type == DORDER_PATROL && isVtolDroid(psDroid) && vtolEmpty(psDroid) && (secondaryOrder & DSS_ALEV_MASK) != DSS_ALEV_NEVER)
    {
      moveToRearm();  // Completely empty (and we're not set to hold fire), don't bother patrolling.
      break;
    }
    break;
  case DORDER_CIRCLE:
    // if there is an enemy around, attack it
    if (action == DROID_ACTION::MOVE &&
        secondaryGetState(psDroid, DSO_ATTACK_LEVEL) == DSS_ALEV_ALWAYS &&
        aiBestNearestTarget(psDroid, &psObj, 0, SCOUT_ATTACK_DIST) >= 0)
    {
      switch (droidType)
      {
      case DROID_WEAPON:
      case DROID_CYBORG:
      case DROID_CYBORG_SUPER:
      case DROID_PERSON:
      case DROID_COMMAND:
        actionDroid(psDroid, DROID_ACTION::ATTACK, psObj);
        break;
      case DROID_SENSOR:
        actionDroid(psDroid, DROID_ACTION::OBSERVE, psObj);
        break;
      default:
        actionDroid(psDroid, DROID_ACTION::NONE);
        break;
      }
    }
    else if (action == DROID_ACTION::NONE || action == DROID_ACTION::MOVE)
    {
      if (action == DROID_ACTION::MOVE)
      {
        // see if we have anything queued up
        if (orderDroidList())
        {
          // started a new order, quit
          break;
        }
      }

      Vector2i edgeDiff = position.xy() - actionPos;
      if (action != DROID_ACTION::MOVE || dot(edgeDiff, edgeDiff) <= TILE_UNITS * 4 * TILE_UNITS * 4)
      {
        //Watermelon:use orderX,orderY as local space origin and calculate droid direction in local space
        Vector2i diff = position.xy() - order.pos;
        uint16_t angle = iAtan2(diff) - DEG(30);
        do
        {
          xoffset = iSinR(angle, 1500);
          yoffset = iCosR(angle, 1500);
          angle -= DEG(10);
        }
        while (!worldOnMap(order.pos.x + xoffset, order.pos.y + yoffset));    // Don't try to fly off map.
        actionDroid(psDroid, DROID_ACTION::MOVE, order.pos.x + xoffset, order.pos.y + yoffset);
      }

      if (isVtolDroid(psDroid) && vtolEmpty() && (secondaryOrder & DSS_ALEV_MASK) != DSS_ALEV_NEVER)
      {
        moveToRearm();  // Completely empty (and we're not set to hold fire), don't bother circling.
        break;
      }
    }
    else if (((action == DROID_ACTION::ATTACK) ||
              (action == DROID_ACTION::VTOLATTACK) ||
              (action == DROID_ACTION::MOVETOATTACK) ||
              (action == DROID_ACTION::ROTATETOATTACK) ||
              (action == DROID_ACTION::OBSERVE) ||
              (action == DROID_ACTION::MOVETOOBSERVE)) &&
             secondaryGetState(psDroid, DSO_HALTTYPE) != DSS_HALT_PURSUE)
    {
      // attacking something - see if the droid has gone too far
      xdiff = position.x - actionPos.x;
      ydiff = position.y - actionPos.y;
      if (xdiff * xdiff + ydiff * ydiff > 2000 * 2000)
      {
        // head back to the target location
        actionDroid(psDroid, DROID_ACTION::RETURNTOPOS, order.pos.x, order.pos.y);
      }
    }
    break;
  case DORDER_HELPBUILD:
  case DORDER_DEMOLISH:
  case DORDER_OBSERVE:
  case DORDER_REPAIR:
  case DORDER_DROIDREPAIR:
  case DORDER_RESTORE:
    if (action == DROID_ACTION::NONE || order.psObj == nullptr)
    {
      order = DroidOrder(DORDER_NONE);
      actionDroid(psDroid, DROID_ACTION::NONE);
      if (owningPlayer == selectedPlayer)
      {
        intRefreshScreen();
      }
    }
    break;
  case DORDER_REARM:
    if (order.psObj == nullptr || psActionTarget[0] == nullptr)
    {
      // arm pad destroyed find another
      order = DroidOrder(DORDER_NONE);
      moveToRearm();
    }
    else if (action == DROID_ACTION::NONE)
    {
      order = DroidOrder(DORDER_NONE);
    }
    break;
  case DORDER_ATTACK:
  case DORDER_ATTACKTARGET:
    if (order.psObj == nullptr || order.psObj->deathTime)
    {
      // if vtol then return to rearm pad as long as there are no other
      // orders queued up
      if (isVtolDroid(psDroid))
      {
        if (!orderDroidList())
        {
          order = DroidOrder(DORDER_NONE);
          moveToRearm();
        }
      }
      else
      {
        order = DroidOrder(DORDER_NONE);
        actionDroid(psDroid, DROID_ACTION::NONE);
      }
    }
    else if (((action == DROID_ACTION::MOVE) ||
              (action == DROID_ACTION::MOVEFIRE)) &&
             actionVisibleTarget(psDroid, order.psObj, 0) && !isVtolDroid(psDroid))
    {
      // moved near enough to attack change to attack action
      actionDroid(psDroid, DROID_ACTION::ATTACK, order.psObj);
    }
    else if ((action == DROID_ACTION::MOVETOATTACK) &&
             !isVtolDroid(psDroid) &&
             !actionVisibleTarget(psDroid, order.psObj, 0) &&
             secondaryGetState(psDroid, DSO_HALTTYPE) != DSS_HALT_HOLD)
    {
      // lost sight of the target while chasing it - change to a move action so
      // that the unit will fire on other things while moving
      actionDroid(psDroid, DROID_ACTION::MOVE, order.psObj->position.x, order.psObj->position.y);
    }
    else if (!isVtolDroid(psDroid)
             && order.psObj == psActionTarget[0]
             && actionInRange(psDroid, order.psObj, 0)
             && (psWall = visGetBlockingWall(psDroid, order.psObj))
             && !aiCheckAlliances(psWall->owningPlayer, owningPlayer))
    {
      // there is a wall in the way - attack that
      actionDroid(psDroid, DROID_ACTION::ATTACK, psWall);
    }
    else if ((action == DROID_ACTION::NONE) ||
             (action == DROID_ACTION::CLEARREARMPAD))
    {
      if ((order.type == DORDER_ATTACKTARGET || order.type == DORDER_ATTACK)
          && secondaryGetState(psDroid, DSO_HALTTYPE) == DSS_HALT_HOLD
          && !actionInRange(psDroid, order.psObj, 0))
      {
        // target is not in range and DSS_HALT_HOLD: give up, don't move
        order = DroidOrder(DORDER_NONE);
      }
      else if (!isVtolDroid(psDroid) || allVtolsRearmed(psDroid))
      {
        actionDroid(psDroid, DROID_ACTION::ATTACK, order.psObj);
      }
    }
    break;
  case DORDER_BUILD:
    if (action == DROID_ACTION::BUILD &&
        order.psObj == nullptr)
    {
      order = DroidOrder(DORDER_NONE);
      actionDroid(psDroid, DROID_ACTION::NONE);
      objTrace(id, "Clearing build order since build target is gone");
    }
    else if (action == DROID_ACTION::NONE)
    {
      order = DroidOrder(DORDER_NONE);
      objTrace(id, "Clearing build order since build action is reset");
    }
    break;
  case DORDER_EMBARK:
  {
    // only place it can be trapped - in multiPlayer can only put cyborgs onto a Cyborg Transporter
    Droid *temp = (Droid *)order.psObj;	// NOTE: It is possible to have a NULL here

    if (temp && temp->droidType == DROID_TRANSPORTER && !cyborgDroid(psDroid))
    {
      order = DroidOrder(DORDER_NONE);
      actionDroid(psDroid, DROID_ACTION::NONE);
      if (owningPlayer == selectedPlayer)
      {
        audio_PlayBuildFailedOnce();
        addConsoleMessage(_("We can't do that! We must be a Cyborg unit to use a Cyborg Transport!"), DEFAULT_JUSTIFY, selectedPlayer);
      }
    }
    else
    {
      // Wait for the action to finish then assign to Transporter (if not already flying)
      if (order.psObj == nullptr || transporterFlying((Droid *)order.psObj))
      {
        order = DroidOrder(DORDER_NONE);
        actionDroid(psDroid, DROID_ACTION::NONE);
      }
      else if (abs((SDWORD)position.x - (SDWORD)order.psObj->position.x) < TILE_UNITS
               && abs((SDWORD)position.y - (SDWORD)order.psObj->position.y) < TILE_UNITS)
      {
        // save the target of current droid (the transporter)
        Droid *transporter = (Droid *)order.psObj;

        // Make sure that it really is a valid droid
        CHECK_DROID(transporter);

        // order the droid to stop so moveUpdateDroid does not process this unit
        orderDroid(psDroid, DORDER_STOP, ModeImmediate);
        setDroidTarget(psDroid, nullptr);
        order.psObj = nullptr;
        secondarySetState(psDroid, DSO_RETURN_TO_LOC, DSS_NONE);

        /* We must add the droid to the transporter only *after*
					* processing changing its orders (see above).
         */
        transporterAddDroid(transporter, psDroid);
      }
      else if (action == DROID_ACTION::NONE)
      {
        actionDroid(psDroid, DROID_ACTION::MOVE, order.psObj->position.x, order.psObj->position.y);
      }
    }
  }
  // Do we need to clear the secondary order "DSO_EMBARK" here? (PD)
  break;
  case DORDER_DISEMBARK:
    //only valid in multiPlayer mode
    if (bMultiPlayer)
    {
      //this order can only be given to Transporter droids
      if (isTransporter(psDroid))
      {
        /*once the Transporter has reached its destination (and landed),
        get all the units to disembark*/
        if (action != DROID_ACTION::MOVE && action != DROID_ACTION::MOVEFIRE &&
            sMove.Status == MOVEINACTIVE && sMove.iVertSpeed == 0)
        {
          unloadTransporter(psDroid, position.x, position.y, false);
          //reset the transporter's order
          order = DroidOrder(DORDER_NONE);
        }
      }
    }
    break;
  case DORDER_RTB:
    // Just wait for the action to finish then clear the order
    if (action == DROID_ACTION::NONE)
    {
      order = DroidOrder(DORDER_NONE);
      secondarySetState(psDroid, DSO_RETURN_TO_LOC, DSS_NONE);
    }
    break;
  case DORDER_RTR:
  case DORDER_RTR_SPECIFIED:
    if (order.psObj == nullptr)
    {
      // Our target got lost. Let's try again.
      order = DroidOrder(DORDER_NONE);
      orderDroid(psDroid, DORDER_RTR, ModeImmediate);
    }
    else if (action == DROID_ACTION::NONE)
    {
      /* get repair facility pointer */
      psStruct = (Structure *)order.psObj;
      ASSERT(psStruct != nullptr,
             "orderUpdateUnit: invalid structure pointer");


      if (objPosDiffSq(position, order.psObj->position) < (TILE_UNITS * 8) * (TILE_UNITS * 8))
      {
        /* action droid to wait */
        actionDroid(psDroid, DROID_ACTION::WAITFORREPAIR);
      }
      else
      {
        // move the droid closer to the repair point
        // setting target to null will trigger search for nearest repair point: we might have a better option after all
        order.psObj = nullptr;
      }
    }
    break;
  case DORDER_LINEBUILD:
    if (action == DROID_ACTION::NONE ||
        (action == DROID_ACTION::BUILD && order.psObj == nullptr))
    {
      // finished building the current structure
      auto lb = calcLineBuild(order.psStats, order.direction, order.pos, order.pos2);
      if (lb.count <= 1)
      {
        // finished all the structures - done
        order = DroidOrder(DORDER_NONE);
        break;
      }

      // update the position for another structure
      order.pos = lb[1];

      // build another structure
      setDroidTarget(psDroid, nullptr);
      actionDroid(psDroid, DROID_ACTION::BUILD, order.pos.x, order.pos.y);
      //intRefreshScreen();
    }
    break;
  case DORDER_FIRESUPPORT:
    if (order.psObj == nullptr)
    {
      order = DroidOrder(DORDER_NONE);
      if (isVtolDroid(psDroid) && !vtolFull(psDroid))
      {
        moveToRearm(psDroid);
      }
      else
      {
        actionDroid(psDroid, DROID_ACTION::NONE);
      }
    }
    //before targetting - check VTOL's are fully armed
    else if (vtolEmpty(psDroid))
    {
      moveToRearm(psDroid);
    }
    //indirect weapon droid attached to (standard)sensor droid
    else
    {
      GameObject *psFireTarget = nullptr;

      if (order.psObj->type == OBJ_DROID)
      {
        Droid *psSpotter = (Droid *)order.psObj;

        if (psSpotter->action == DROID_ACTION::OBSERVE
            || (psSpotter->droidType == DROID_COMMAND && psSpotter->action == DROID_ACTION::ATTACK))
        {
          psFireTarget = psSpotter->psActionTarget[0];
        }
      }
      else if (order.psObj->type == OBJ_STRUCTURE)
      {
        Structure *psSpotter = (Structure *)order.psObj;

        psFireTarget = psSpotter->psTarget[0];
      }

      if (psFireTarget && !psFireTarget->deathTime && checkAnyWeaponsTarget(psDroid, psFireTarget))
      {
        bAttack = false;
        if (isVtolDroid(psDroid))
        {
          if (!vtolEmpty(psDroid) &&
              ((action == DROID_ACTION::MOVETOREARM) ||
               (action == DROID_ACTION::WAITFORREARM)) &&
              (sMove.Status != MOVEINACTIVE))
          {
            // catch vtols that were attacking another target which was destroyed
            // get them to attack the new target rather than returning to rearm
            bAttack = true;
          }
          else if (allVtolsRearmed(psDroid))
          {
            bAttack = true;
          }
        }
        else
        {
          bAttack = true;
        }

        //if not currently attacking or target has changed
        if (bAttack &&
            (!droidAttacking(psDroid) ||
             psFireTarget != psActionTarget[0]))
        {
          //get the droid to attack
          actionDroid(psDroid, DROID_ACTION::ATTACK, psFireTarget);
        }
      }
      else if (isVtolDroid(psDroid) &&
               !vtolFull(psDroid) &&
               (action != DROID_ACTION::NONE) &&
               (action != DROID_ACTION::FIRESUPPORT))
      {
        moveToRearm(psDroid);
      }
      else if ((action != DROID_ACTION::FIRESUPPORT) &&
               (action != DROID_ACTION::FIRESUPPORT_RETREAT))
      {
        actionDroid(psDroid, DROID_ACTION::FIRESUPPORT, order.psObj);
      }
    }
    break;
  case DORDER_RECYCLE:
    if (order.psObj == nullptr)
    {
      order = DroidOrder(DORDER_NONE);
      actionDroid(psDroid, DROID_ACTION::NONE);
    }
    else if (actionReachedBuildPos(psDroid, order.psObj->position.x, order.psObj->position.y, ((Structure *)order.psObj)->rotation.direction, ((Structure *)order.psObj)->stats))
    {
      recycleDroid(psDroid);
    }
    else if (action == DROID_ACTION::NONE)
    {
      actionDroid(psDroid, DROID_ACTION::MOVE, order.psObj->position.x, order.psObj->position.y);
    }
    break;
  case DORDER_GUARD:
    if (orderDroidList(psDroid))
    {
      // started a queued order - quit
      break;
    }
    else if ((action == DROID_ACTION::NONE) ||
             (action == DROID_ACTION::MOVE) ||
             (action == DROID_ACTION::MOVEFIRE))
    {
      // not doing anything, make sure the droid is close enough
      // to the thing it is defending
      if ((!(droidType == DROID_REPAIR || droidType == DROID_CYBORG_REPAIR))
          && order.psObj != nullptr && order.psObj->type == OBJ_DROID
          && ((Droid *)order.psObj)->droidType == DROID_COMMAND)
      {
        // guarding a commander, allow more space
        orderCheckGuardPosition(psDroid, DEFEND_CMD_BASEDIST);
      }
      else
      {
        orderCheckGuardPosition(psDroid, DEFEND_BASEDIST);
      }
    }
    else if (droidType == DROID_REPAIR || droidType == DROID_CYBORG_REPAIR)
    {
      // repairing something, make sure the droid doesn't go too far
      orderCheckGuardPosition(psDroid, REPAIR_MAXDIST);
    }
    else if (droidType == DROID_CONSTRUCT || droidType == DROID_CYBORG_CONSTRUCT)
    {
      // repairing something, make sure the droid doesn't go too far
      orderCheckGuardPosition(psDroid, CONSTRUCT_MAXDIST);
    }
    else if (isTransporter(psDroid))
    {

    }
    else
    {
      //let vtols return to rearm
      if (!vtolRearming(psDroid))
      {
        // attacking something, make sure the droid doesn't go too far
        if (order.psObj != nullptr && order.psObj->type == OBJ_DROID &&
            ((Droid *)order.psObj)->droidType == DROID_COMMAND)
        {
          // guarding a commander, allow more space
          orderCheckGuardPosition(psDroid, DEFEND_CMD_MAXDIST);
        }
        else
        {
          orderCheckGuardPosition(psDroid, DEFEND_MAXDIST);
        }
      }
    }

    // get combat units in a command group to attack the commanders target
    if (hasCommander(psDroid) && (numWeapons > 0))
    {
      if (psGroup->psCommander->action == DROID_ACTION::ATTACK &&
          psGroup->psCommander->psActionTarget[0] != nullptr &&
          !psGroup->psCommander->psActionTarget[0]->deathTime)
      {
        psObj = psGroup->psCommander->psActionTarget[0];
        if (action == DROID_ACTION::ATTACK ||
            action == DROID_ACTION::MOVETOATTACK)
        {
          if (psActionTarget[0] != psObj)
          {
            actionDroid(psDroid, DROID_ACTION::ATTACK, psObj);
          }
        }
        else if (action != DROID_ACTION::MOVE)
        {
          actionDroid(psDroid, DROID_ACTION::ATTACK, psObj);
        }
      }

      // make sure units in a command group are actually guarding the commander
      psObj = orderStateObj(psDroid, DORDER_GUARD);	// find out who is being guarded by the droid
      if (psObj == nullptr
          || psObj != psGroup->psCommander)
      {
        orderDroidObj(psDroid, DORDER_GUARD, psGroup->psCommander, ModeImmediate);
      }
    }

    tryDoRepairlikeAction(psDroid);
    break;
  default:
    ASSERT(false, "orderUpdateUnit: unknown order");
  }

  // catch any vtol that is rearming but has finished his order
  if (order.type == DORDER_NONE && vtolRearming(psDroid)
      && (psActionTarget[0] == nullptr || !psActionTarget[0]->deathTime))
  {
    order = DroidOrder(DORDER_REARM, psActionTarget[0]);
  }

  if (selected)
  {
    // Tell us what the droid is doing.
    snprintf(DROIDDOING, sizeof(DROIDDOING), "%.12s,id(%d) order(%d):%s action(%d):%s secondary:%x move:%s", droidGetName(psDroid), id,
             order.type, getDroidOrderName(order.type), action, getDroidActionName(action), secondaryOrder,
             moveDescription(sMove.Status));
  }
}

/** This function checks if the droid is off range. If yes, it uses actionDroid() to make the droid to move to its target if its target is on range, or to move to its order position if not.
 * @todo droid doesn't shoot while returning to the guard position.
 */
void Droid::orderCheckGuardPosition(SDWORD range)
{
  if (order.psObj != nullptr)
  {
    UDWORD x, y;

    // repair droids always follow behind - don't want them jumping into the line of fire got a moving droid - check against where the unit is going
    order.pos = Vector2i(x, y);
  }

  int xdiff = position.x - order.pos.x;
  int ydiff = position.y - order.pos.y;
  if (xdiff * xdiff + ydiff * ydiff > range * range)
  {
    if ((sMove.Status != MOVEINACTIVE) &&
        ((action == DROID_ACTION::MOVE) ||
         (action == DROID_ACTION::MOVEFIRE)))
    {
      xdiff = sMove.destination.x - order.pos.x;
      ydiff = sMove.destination.y - order.pos.y;
      if (xdiff * xdiff + ydiff * ydiff > range * range)
      {
        actionDroid(psDroid, DROID_ACTION::MOVE, order.pos.x, order.pos.y);
      }
    }
    else
    {
      actionDroid(psDroid, DROID_ACTION::MOVE, order.pos.x, order.pos.y);
    }
  }
}

/** This function checks if there are any damaged droids within a defined range.
 * It returns the damaged droid if there is any, or nullptr if none was found.
 */
Droid* Droid::checkForRepairRange()
{
  Droid *psFailedTarget = nullptr;
  if (action == DROID_ACTION::SULK)
  {
    psFailedTarget = (Droid *)psActionTarget[0];
  }

  ASSERT(droidType == DROID_REPAIR || droidType == DROID_CYBORG_REPAIR, "Invalid droid type");

  unsigned radius = ((order.type == DROID_ORDER_TYPE::HOLD) || (order.type == DROID_ORDER_TYPE::NONE && secondaryGetState(psDroid, DSO_HALTTYPE) == DSS_HALT_HOLD)) ? REPAIR_RANGE : REPAIR_MAXDIST;

  unsigned bestDistanceSq = radius * radius;
  Droid *best = nullptr;

  for (GameObject *object : gridStartIterate(position.x, position.y, radius))
  {
    unsigned distanceSq = droidSqDist(psDroid, object);  // droidSqDist returns -1 if unreachable, (unsigned)-1 is a big number.
    if (object == orderStateObj(psDroid, DROID_ORDER_TYPE::GUARD))
    {
      distanceSq = 0;  // If guarding a unit — always do that first.
    }

    Droid *droid = castDroid(object);
    if (droid != nullptr &&  // Must be a droid.
        droid != psFailedTarget &&   // Must not have just failed to reach it.
        distanceSq <= bestDistanceSq &&  // Must be as close as possible.
        aiCheckAlliances(owningPlayer, droid->owningPlayer) &&  // Must be a friendly droid.
        droidIsDamaged(droid) &&  // Must need repairing.
        visibleObject(psDroid, droid, false))  // Must be able to sense it.
    {
      bestDistanceSq = distanceSq;
      best = droid;
    }
  }

  return best;
}

bool Droid::tryDoRepairlikeAction()
{
  if (isRepairlikeAction(action))
  {
    return true;  // Already doing something.
  }

  switch (droidType)
  {
  case DROID_REPAIR:
  case DROID_CYBORG_REPAIR:
    //repair droids default to repairing droids within a given range
    if (Droid *repairTarget = checkForRepairRange(psDroid))
    {
      actionDroid(psDroid, DROID_ACTION::DROIDREPAIR, repairTarget);
    }
    break;
  case DROID_CONSTRUCT:
  case DROID_CYBORG_CONSTRUCT:
  {
    //construct droids default to repairing and helping structures within a given range
    auto damaged = checkForDamagedStruct(psDroid);
    if (damaged.second == DROID_ACTION::REPAIR)
    {
      actionDroid(psDroid, damaged.second, damaged.first);
    }
    else if (damaged.second == DROID_ACTION::BUILD)
    {
      order.psStats = damaged.first->stats;
      order.direction = damaged.first->rotation.direction;
      actionDroid(psDroid, damaged.second, damaged.first->position.x, damaged.first->position.y);
    }
    break;
  }
  default:
    return false;
  }
  return true;
}

/** This function actually tells the droid to perform the psOrder.
 * This function is called everytime to send a direct order to a droid.
 */
void Droid::orderDroidBase(DROID_ORDER_DATA *psOrder)
{
  UDWORD		iFactoryDistSq;
  Structure *psStruct, *psFactory;
  const PROPULSION_STATS *psPropStats = asPropulsionStats + asBits[COMP_PROPULSION];
  const Vector3i rPos(psOrder->pos, 0);
  syncDebugDroid(psDroid, '-');
  syncDebug("%d ordered %s", id, getDroidOrderName(psOrder->type));
  objTrace(id, "base set order to %s (was %s)", getDroidOrderName(psOrder->type), getDroidOrderName(order.type));
  if (psOrder->type != DROID_ORDER_TYPE::TRANSPORTIN         // transporters special
      && psOrder->psObj == nullptr			// location-type order
      && (validOrderForLoc(psOrder->type) || psOrder->type == DROID_ORDER_TYPE::BUILD)
      && !fpathCheck(position, rPos, psPropStats->propulsionType))
  {
    if (!isHumanPlayer(owningPlayer))
    {
      debug(LOG_SCRIPT, "Invalid order %s given to player %d's %s for position (%d, %d) - ignoring",
            getDroidOrderName(psOrder->type), owningPlayer, droidGetName(psDroid), psOrder->pos.x, psOrder->pos.y);
    }
    objTrace(id, "Invalid order %s for position (%d, %d) - ignoring", getDroidOrderName(psOrder->type), psOrder->pos.x, psOrder->pos.y);
    syncDebugDroid(psDroid, '?');
    return;
  }

  // deal with a droid receiving a primary order
  if (!isTransporter(psDroid) && psOrder->type != DROID_ORDER_TYPE::NONE && psOrder->type != DROID_ORDER_TYPE::STOP && psOrder->type != DROID_ORDER_TYPE::GUARD)
  {
    // reset secondary order
    const unsigned oldState = secondaryOrder;
    secondaryOrder &= ~(DSS_RTL_MASK | DSS_RECYCLE_MASK | DSS_PATROL_MASK);
    secondaryOrderPending &= ~(DSS_RTL_MASK | DSS_RECYCLE_MASK | DSS_PATROL_MASK);
    objTrace(id, "secondary order reset due to primary order set");
    if (oldState != secondaryOrder && owningPlayer == selectedPlayer)
    {
      intRefreshScreen();
    }
  }

  // if this is a command droid - all it's units do the same thing
  if ((droidType == DROID_COMMAND) &&
      (psGroup != nullptr) &&
      (psGroup->type == GT_COMMAND) &&
      (psOrder->type != DROID_ORDER_TYPE::GUARD) &&  //(psOrder->psObj == NULL)) &&
      (psOrder->type != DROID_ORDER_TYPE::RTR) &&
      (psOrder->type != DROID_ORDER_TYPE::RECYCLE))
  {
    if (psOrder->type == DROID_ORDER_TYPE::ATTACK)
    {
      // change to attacktarget so that the group members
      // guard order does not get canceled
      psOrder->type = DROID_ORDER_TYPE::ATTACKTARGET;
      orderCmdGroupBase(psGroup, psOrder);
      psOrder->type = DROID_ORDER_TYPE::ATTACK;
    }
    else
    {
      orderCmdGroupBase(psGroup, psOrder);
    }

    // the commander doesn't have to pick up artifacts, one
    // of his units will do it for him (if there are any in his group).
    if ((psOrder->type == DROID_ORDER_TYPE::RECOVER) &&
        (psGroup->psList != nullptr))
    {
      psOrder->type = DROID_ORDER_TYPE::NONE;
    }
  }

  // A selected campaign transporter shouldn't be given orders by the player.
  // Campaign transporter selection is required for it to be tracked by the camera, and
  // should be the only case when it does get selected.
  if (isTransporter(psDroid) &&
      !bMultiPlayer &&
      selected &&
      (psOrder->type != DROID_ORDER_TYPE::TRANSPORTOUT &&
       psOrder->type != DROID_ORDER_TYPE::TRANSPORTIN &&
       psOrder->type != DROID_ORDER_TYPE::TRANSPORTRETURN))
  {
    return;
  }

  switch (psOrder->type)
  {
  case DROID_ORDER_TYPE::NONE:
    // used when choose order cannot assign an order
    break;
  case DROID_ORDER_TYPE::STOP:
    // get the droid to stop doing whatever it is doing
    actionDroid(psDroid, DROID_ACTION::NONE);
    order = DroidOrder(DROID_ORDER_TYPE::NONE);
    break;
  case DROID_ORDER_TYPE::HOLD:
    // get the droid to stop doing whatever it is doing and temp hold
    actionDroid(psDroid, DROID_ACTION::NONE);
    order = *psOrder;
    break;
  case DROID_ORDER_TYPE::MOVE:
  case DROID_ORDER_TYPE::SCOUT:
    // can't move vtols to blocking tiles
    if (isVtolDroid(psDroid)
        && fpathBlockingTile(map_coord(psOrder->pos), getPropulsionStats(psDroid)->propulsionType))
    {
      break;
    }
    //in multiPlayer, cannot move Transporter to blocking tile either
    if (game.type == LEVEL_TYPE::SKIRMISH
        && isTransporter(psDroid)
        && fpathBlockingTile(map_coord(psOrder->pos), getPropulsionStats(psDroid)->propulsionType))
    {
      break;
    }
    // move a droid to a location
    order = *psOrder;
    actionDroid(psDroid, DROID_ACTION::MOVE, psOrder->pos.x, psOrder->pos.y);
    break;
  case DROID_ORDER_TYPE::PATROL:
    order = *psOrder;
    order.pos2 = position.xy();
    actionDroid(psDroid, DROID_ACTION::MOVE, psOrder->pos.x, psOrder->pos.y);
    break;
  case DROID_ORDER_TYPE::RECOVER:
    order = *psOrder;
    actionDroid(psDroid, DROID_ACTION::MOVE, psOrder->psObj->position.x, psOrder->psObj->position.y);
    break;
  case DROID_ORDER_TYPE::TRANSPORTOUT:
    // tell a (transporter) droid to leave home base for the offworld mission
    order = *psOrder;
    actionDroid(psDroid, DROID_ACTION::TRANSPORTOUT, psOrder->pos.x, psOrder->pos.y);
    break;
  case DROID_ORDER_TYPE::TRANSPORTRETURN:
    // tell a (transporter) droid to return after unloading
    order = *psOrder;
    actionDroid(psDroid, DROID_ACTION::TRANSPORTOUT, psOrder->pos.x, psOrder->pos.y);
    break;
  case DROID_ORDER_TYPE::TRANSPORTIN:
    // tell a (transporter) droid to fly onworld
    order = *psOrder;
    actionDroid(psDroid, DROID_ACTION::TRANSPORTIN, psOrder->pos.x, psOrder->pos.y);
    break;
  case DROID_ORDER_TYPE::ATTACK:
  case DROID_ORDER_TYPE::ATTACKTARGET:
    if (numWeapons == 0
        || m_weaponList[0].nStat == 0
        || isTransporter(psDroid))
    {
      break;
    }
    else if (order.type == DROID_ORDER_TYPE::GUARD && psOrder->type == DROID_ORDER_TYPE::ATTACKTARGET)
    {
      // attacking something while guarding, don't change the order
      actionDroid(psDroid, DROID_ACTION::ATTACK, psOrder->psObj);
    }
    else if (psOrder->psObj && !psOrder->psObj->deathTime)
    {
      //cannot attack a Transporter with EW in multiPlayer
      // FIXME: Why not ?
      if (game.type == LEVEL_TYPE::SKIRMISH && electronicDroid(psDroid)
          && psOrder->psObj->type == OBJ_DROID && isTransporter((Droid *)psOrder->psObj))
      {
        break;
      }
      order = *psOrder;

      if (isVtolDroid(psDroid)
          || actionInRange(psDroid, psOrder->psObj, 0)
          || ((psOrder->type == DROID_ORDER_TYPE::ATTACKTARGET || psOrder->type == DROID_ORDER_TYPE::ATTACK)  && secondaryGetState(psDroid, DSO_HALTTYPE) == DSS_HALT_HOLD))
      {
        // when DSS_HALT_HOLD, don't move to attack
        actionDroid(psDroid, DROID_ACTION::ATTACK, psOrder->psObj);
      }
      else
      {
        actionDroid(psDroid, DROID_ACTION::MOVE, psOrder->psObj->position.x, psOrder->psObj->position.y);
      }
    }
    break;
  case DROID_ORDER_TYPE::BUILD:
  case DROID_ORDER_TYPE::LINEBUILD:
    // build a new structure or line of structures
    ASSERT_OR_RETURN(, isConstructionDroid(psDroid), "%s cannot construct things!", objInfo(psDroid));
    ASSERT_OR_RETURN(, psOrder->psStats != nullptr, "invalid structure stats pointer");
    order = *psOrder;
    ASSERT_OR_RETURN(, !order.psStats || order.psStats->type != REF_DEMOLISH, "Cannot build demolition");
    actionDroid(psDroid, DROID_ACTION::BUILD, psOrder->pos.x, psOrder->pos.y);
    objTrace(id, "Starting new construction effort of %s", psOrder->psStats ? getStatsName(psOrder->psStats) : "NULL");
    break;
  case DROID_ORDER_TYPE::BUILDMODULE:
    //build a module onto the structure
    if (!isConstructionDroid(psDroid) || psOrder->index < nextModuleToBuild((Structure *)psOrder->psObj, -1))
    {
      break;
    }
    order = DroidOrder(DROID_ORDER_TYPE::BUILD, getModuleStat((Structure *)psOrder->psObj), psOrder->psObj->position.xy(), 0);
    ASSERT_OR_RETURN(, order.psStats != nullptr, "should have found a module stats");
    ASSERT_OR_RETURN(, !order.psStats || order.psStats->type != REF_DEMOLISH, "Cannot build demolition");
    actionDroid(psDroid, DROID_ACTION::BUILD, psOrder->psObj->position.x, psOrder->psObj->position.y);
    objTrace(id, "Starting new upgrade of %s", psOrder->psStats ? getStatsName(psOrder->psStats) : "NULL");
    break;
  case DROID_ORDER_TYPE::HELPBUILD:
    // help to build a structure that is starting to be built
    ASSERT_OR_RETURN(, isConstructionDroid(psDroid), "Not a constructor droid");
    ASSERT_OR_RETURN(, psOrder->psObj != nullptr, "Help to build a NULL pointer?");
    if (action == DROID_ACTION::BUILD && psOrder->psObj == psActionTarget[0]
        // skip DROID_ORDER_TYPE::LINEBUILD -> we still want to drop pending structure blueprints
        // this isn't a perfect solution, because ordering a LINEBUILD with negative energy, and then clicking
        // on first structure being built, will remove it, as we change order from DORDR_LINEBUILD to DROID_ORDER_TYPE::BUILD
        && (order.type != DROID_ORDER_TYPE::LINEBUILD))
    {
      // we are already building it, nothing to do
      objTrace(id, "Ignoring DROID_ORDER_TYPE::HELPBUILD because already buildig object %i", psOrder->psObj->id);
      break;
    }
    order = *psOrder;
    order.pos = psOrder->psObj->position.xy();
    order.psStats = ((Structure *)psOrder->psObj)->stats;
    ASSERT_OR_RETURN(,!order.psStats || order.psStats->type != REF_DEMOLISH, "Cannot build demolition");
    actionDroid(psDroid, DROID_ACTION::BUILD, order.pos.x, order.pos.y);
    objTrace(id, "Helping construction of %s", psOrder->psStats ? getStatsName(order.psStats) : "NULL");
    break;
  case DROID_ORDER_TYPE::DEMOLISH:
    if (!(droidType == DROID_CONSTRUCT || droidType == DROID_CYBORG_CONSTRUCT))
    {
      break;
    }
    order = *psOrder;
    order.pos = psOrder->psObj->position.xy();
    actionDroid(psDroid, DROID_ACTION::DEMOLISH, psOrder->psObj);
    break;
  case DROID_ORDER_TYPE::REPAIR:
    if (!(droidType == DROID_CONSTRUCT || droidType == DROID_CYBORG_CONSTRUCT))
    {
      break;
    }
    order = *psOrder;
    order.pos = psOrder->psObj->position.xy();
    actionDroid(psDroid, DROID_ACTION::REPAIR, psOrder->psObj);
    break;
  case DROID_ORDER_TYPE::DROIDREPAIR:
    if (!(droidType == DROID_REPAIR || droidType == DROID_CYBORG_REPAIR))
    {
      break;
    }
    order = *psOrder;
    actionDroid(psDroid, DROID_ACTION::DROIDREPAIR, psOrder->psObj);
    break;
  case DROID_ORDER_TYPE::OBSERVE:
    // keep an object within sensor view
    order = *psOrder;
    actionDroid(psDroid, DROID_ACTION::OBSERVE, psOrder->psObj);
    break;
  case DROID_ORDER_TYPE::FIRESUPPORT:
    if (isTransporter(psDroid))
    {
      debug(LOG_ERROR, "Sorry, transports cannot be assigned to commanders.");
      order = DroidOrder(DROID_ORDER_TYPE::NONE);
      break;
    }
    if (m_weaponList[0].nStat == 0)
    {
      break;
    }
    order = *psOrder;
    // let the order update deal with vtol droids
    if (!isVtolDroid(psDroid))
    {
      actionDroid(psDroid, DROID_ACTION::FIRESUPPORT, psOrder->psObj);
    }

    if (owningPlayer == selectedPlayer)
    {
      orderPlayFireSupportAudio(psOrder->psObj);
    }
    break;
  case DROID_ORDER_TYPE::COMMANDERSUPPORT:
    if (isTransporter(psDroid))
    {
      debug(LOG_ERROR, "Sorry, transports cannot be assigned to commanders.");
      order = DroidOrder(DROID_ORDER_TYPE::NONE);
      break;
    }
    ASSERT_OR_RETURN(, psOrder->psObj != nullptr, "Can't command a NULL");
    if (cmdDroidAddDroid((Droid *)psOrder->psObj, psDroid) && owningPlayer == selectedPlayer)
    {
      orderPlayFireSupportAudio(psOrder->psObj);
    }
    else if (owningPlayer == selectedPlayer)
    {
      audio_PlayBuildFailedOnce();
    }
    break;
  case DROID_ORDER_TYPE::RTB:
    for (psStruct = apsStructLists[owningPlayer]; psStruct; psStruct = psStruct->psNext)
    {
      if (psStruct->stats->type == REF_HQ)
      {
        Vector2i pos = psStruct->position.xy();

        order = *psOrder;
        // Find a place to land for vtols. And Transporters in a multiPlay game.
        if (isVtolDroid(psDroid) || (game.type == LEVEL_TYPE::SKIRMISH && isTransporter(psDroid)))
        {
          actionVTOLLandingPos(psDroid, &pos);
        }
        actionDroid(psDroid, DROID_ACTION::MOVE, pos.x, pos.y);
        break;
      }
    }
    // no HQ go to the landing zone
    if (order.type != DROID_ORDER_TYPE::RTB)
    {
      // see if the LZ has been set up
      int iDX = getLandingX(owningPlayer);
      int iDY = getLandingY(owningPlayer);

      if (iDX && iDY)
      {
        order = *psOrder;
        actionDroid(psDroid, DROID_ACTION::MOVE, iDX, iDY);
      }
      else
      {
        // haven't got an LZ set up so don't do anything
        actionDroid(psDroid, DROID_ACTION::NONE);
        order = DroidOrder(DROID_ORDER_TYPE::NONE);
      }
    }
    break;
  case DROID_ORDER_TYPE::RTR:
  case DROID_ORDER_TYPE::RTR_SPECIFIED:
  {
    if (isVtolDroid(psDroid))
    {
      moveToRearm(psDroid);
      break;
    }
    // if already has a target repair, don't override it: it might be different
    // and we don't want come back and forth between 2 repair points
    if (order.type == DROID_ORDER_TYPE::RTR && psOrder->psObj != nullptr && !psOrder->psObj->deathTime)
    {
      objTrace(id, "DONE FOR NOW");
      break;
    }
    RtrBestResult rtrData;
    if (psOrder->rtrType == RTR_TYPE_NO_RESULT || psOrder->psObj == nullptr)
    {
      rtrData = decideWhereToRepairAndBalance(psDroid);
    }
    else
    {
      rtrData = RtrBestResult(psOrder);
    }

    /* give repair order if repair facility found */
    if (rtrData.type == RTR_TYPE_REPAIR_FACILITY)
    {
      /* move to front of structure */
      order = DroidOrder(psOrder->type, rtrData.psObj, RTR_TYPE_REPAIR_FACILITY);
      order.pos = rtrData.psObj->position.xy();
      /* If in multiPlayer, and the Transporter has been sent to be
					* repaired, need to find a suitable location to drop down. */
      if (game.type == LEVEL_TYPE::SKIRMISH && isTransporter(psDroid))
      {
        Vector2i pos = order.pos;

        objTrace(id, "Repair transport");
        actionVTOLLandingPos(psDroid, &pos);
        actionDroid(psDroid, DROID_ACTION::MOVE, pos.x, pos.y);
      }
      else
      {
        objTrace(id, "Go to repair facility at (%d, %d) using (%d, %d)!", rtrData.psObj->position.x, rtrData.psObj->position.y, order.pos.x, order.pos.y);
        actionDroid(psDroid, DROID_ACTION::MOVE, rtrData.psObj, order.pos.x, order.pos.y);
      }
    }
    /* give repair order if repair droid found */
    else if (rtrData.type == RTR_TYPE_DROID && !isTransporter(psDroid))
    {
      order = DroidOrder(psOrder->type, Vector2i(rtrData.psObj->position.x, rtrData.psObj->position.y), RTR_TYPE_DROID);
      order.pos = rtrData.psObj->position.xy();
      order.psObj = rtrData.psObj;
      objTrace(id, "Go to repair at (%d, %d) using (%d, %d), time %i!", rtrData.psObj->position.x, rtrData.psObj->position.y, order.pos.x, order.pos.y, gameTime);
      actionDroid(psDroid, DROID_ACTION::MOVE, order.pos.x, order.pos.y);
    }
    else
    {
      // no repair facility or HQ go to the landing zone
      if (!bMultiPlayer && selectedPlayer == 0)
      {
        objTrace(id, "could not RTR, doing RTL instead");
        orderDroid(psDroid, DROID_ORDER_TYPE::RTB, ModeImmediate);
      }
    }
  }
  break;
  case DROID_ORDER_TYPE::EMBARK:
  {
    Droid *embarkee = castDroid(psOrder->psObj);
    if (isTransporter(psDroid)  // require a transporter for embarking.
        || embarkee == nullptr || !isTransporter(embarkee))  // nor can a transporter load another transporter
    {
      debug(LOG_ERROR, "Sorry, can only load things that aren't transporters into things that are.");
      order = DroidOrder(DROID_ORDER_TYPE::NONE);
      break;
    }
    // move the droid to the transporter location
    order = *psOrder;
    order.pos = psOrder->psObj->position.xy();
    actionDroid(psDroid, DROID_ACTION::MOVE, psOrder->psObj->position.x, psOrder->psObj->position.y);
    break;
  }
  case DROID_ORDER_TYPE::DISEMBARK:
    //only valid in multiPlayer mode
    if (bMultiPlayer)
    {
      //this order can only be given to Transporter droids
      if (isTransporter(psDroid))
      {
        order = *psOrder;
        //move the Transporter to the requested location
        actionDroid(psDroid, DROID_ACTION::MOVE, psOrder->pos.x, psOrder->pos.y);
        //close the Transporter interface - if up
        if (widgGetFromID(psWScreen, IDTRANS_FORM) != nullptr)
        {
          intRemoveTrans();
        }
      }
    }
    break;
  case DROID_ORDER_TYPE::RECYCLE:
    psFactory = nullptr;
    iFactoryDistSq = 0;
    for (psStruct = apsStructLists[owningPlayer]; psStruct; psStruct = psStruct->psNext)
    {
      // Look for nearest factory or repair facility
      if (psStruct->stats->type == REF_FACTORY || psStruct->stats->type == REF_CYBORG_FACTORY
          || psStruct->stats->type == REF_VTOL_FACTORY || psStruct->stats->type == REF_REPAIR_FACILITY)
      {
        /* get droid->facility distance squared */
        int iStructDistSq = droidSqDist(psDroid, psStruct);

        /* Choose current structure if first facility found or nearer than previously chosen facility */
        if (psStruct->status == SS_BUILT && iStructDistSq > 0 && (psFactory == nullptr || iFactoryDistSq > iStructDistSq))
        {
          psFactory = psStruct;
          iFactoryDistSq = iStructDistSq;
        }
      }
    }

    /* give recycle order if facility found */
    if (psFactory != nullptr)
    {
      /* move to front of structure */
      order = DroidOrder(psOrder->type, psFactory);
      order.pos = psFactory->position.xy();
      setDroidTarget(psDroid,  psFactory);
      actionDroid(psDroid, DROID_ACTION::MOVE, psFactory, order.pos.x, order.pos.y);
    }
    break;
  case DROID_ORDER_TYPE::GUARD:
    order = *psOrder;
    if (psOrder->psObj != nullptr)
    {
      order.pos = psOrder->psObj->position.xy();
    }
    actionDroid(psDroid, DROID_ACTION::NONE);
    break;
  case DROID_ORDER_TYPE::RESTORE:
    if (!electronicDroid(psDroid))
    {
      break;
    }
    if (psOrder->psObj->type != OBJ_STRUCTURE)
    {
      ASSERT(false, "orderDroidBase: invalid object type for Restore order");
      break;
    }
    order = *psOrder;
    order.pos = psOrder->psObj->position.xy();
    actionDroid(psDroid, DROID_ACTION::RESTORE, psOrder->psObj);
    break;
  case DROID_ORDER_TYPE::REARM:
    // didn't get executed before
    if (!vtolRearming())
    {
      order = *psOrder;
      actionDroid(psDroid, DROID_ACTION::MOVETOREARM, psOrder->psObj);
      assignVTOLPad(psDroid, (Structure *)psOrder->psObj);
    }
    break;
  case DROID_ORDER_TYPE::CIRCLE:
    if (!isVtolDroid(psDroid))
    {
      break;
    }
    order = *psOrder;
    actionDroid(psDroid, DROID_ACTION::MOVE, psOrder->pos.x, psOrder->pos.y);
    break;
  default:
    ASSERT(false, "orderUnitBase: unknown order");
    break;
  }

  syncDebugDroid(psDroid, '+');
}

/** This function sends the droid an order. It uses sendDroidInfo() if mode == ModeQueue and orderDroidBase() if not. */
void Droid::orderDroid(DROID_ORDER order, QUEUE_MODE mode)
{
  ASSERT(order == DROID_ORDER_TYPE::NONE ||
             order == DROID_ORDER_TYPE::RTR ||
             order == DROID_ORDER_TYPE::RTB ||
             order == DROID_ORDER_TYPE::RECYCLE ||
             order == DROID_ORDER_TYPE::TRANSPORTIN ||
             order == DROID_ORDER_TYPE::STOP ||		// Added this PD.
             order == DROID_ORDER_TYPE::HOLD,
         "orderUnit: Invalid order");

  DROID_ORDER_DATA sOrder(order);
  if (mode == ModeQueue && bMultiPlayer)
  {
    sendDroidInfo(psDroid, sOrder, false);
  }
  else
  {
    orderClearDroidList(psDroid);
    orderDroidBase(psDroid, &sOrder);
  }
}

/** This function assigns a state to a droid. It returns true if it assigned and false if it failed to assign.*/
bool Droid::secondarySetState(SECONDARY_ORDER sec, SECONDARY_STATE State, QUEUE_MODE mode)
{
  UDWORD		CurrState, factType, prodType;
  Structure *psStruct;
  SDWORD		factoryInc;
  bool		retVal;
  Droid *psTransport, *psCurr, *psNext;
  DROID_ORDER     order;

  CurrState = secondaryOrder;
  if (bMultiMessages && mode == ModeQueue)
  {
    CurrState = secondaryOrderPending;
  }

  // Figure out what the new secondary state will be (once the order is synchronised.
  // Why does this have to be so ridiculously complicated?
  uint32_t secondaryMask = 0;
  uint32_t secondarySet = 0;
  switch (sec)
  {
  case DSO_ATTACK_RANGE:
    secondaryMask = DSS_ARANGE_MASK;
    secondarySet = State;
    break;
  case DSO_REPAIR_LEVEL:
    secondaryMask = DSS_REPLEV_MASK;
    secondarySet = State;
    break;
  case DSO_ATTACK_LEVEL:
    secondaryMask = DSS_ALEV_MASK;
    secondarySet = State;
    break;
  case DSO_ASSIGN_PRODUCTION:
    if (droidType == DROID_COMMAND)
    {
      secondaryMask = DSS_ASSPROD_FACT_MASK;
      secondarySet = State & DSS_ASSPROD_MASK;
    }
    break;
  case DSO_ASSIGN_CYBORG_PRODUCTION:
    if (droidType == DROID_COMMAND)
    {
      secondaryMask = DSS_ASSPROD_CYB_MASK;
      secondarySet = State & DSS_ASSPROD_MASK;
    }
    break;
  case DSO_ASSIGN_VTOL_PRODUCTION:
    if (droidType == DROID_COMMAND)
    {
      secondaryMask = DSS_ASSPROD_VTOL_MASK;
      secondarySet = State & DSS_ASSPROD_MASK;
    }
    break;
  case DSO_CLEAR_PRODUCTION:
    if (droidType == DROID_COMMAND)
    {
      secondaryMask = State & DSS_ASSPROD_MASK;
    } break;
  case DSO_RECYCLE:
    if (State & DSS_RECYCLE_MASK)
    {
      secondaryMask = DSS_RTL_MASK | DSS_RECYCLE_MASK | DSS_HALT_MASK;
      secondarySet = DSS_RECYCLE_SET | DSS_HALT_GUARD;
    }
    else
    {
      secondaryMask = DSS_RECYCLE_MASK;
    }
    break;
  case DSO_CIRCLE:  // This doesn't even make any sense whatsoever as a secondary order...
    secondaryMask = DSS_CIRCLE_MASK;
    secondarySet = (State & DSS_CIRCLE_SET) ? DSS_CIRCLE_SET : 0;
    break;
  case DSO_PATROL:  // This doesn't even make any sense whatsoever as a secondary order...
    secondaryMask = DSS_PATROL_MASK;
    secondarySet = (State & DSS_PATROL_SET) ? DSS_PATROL_SET : 0;
    break;
  case DSO_HALTTYPE:
    switch (State & DSS_HALT_MASK)
    {
    case DSS_HALT_PURSUE:
    case DSS_HALT_GUARD:
    case DSS_HALT_HOLD:
      secondaryMask = DSS_HALT_MASK;
      secondarySet = State;
      break;
    }
    break;
  case DSO_RETURN_TO_LOC:
    secondaryMask = DSS_RTL_MASK;
    switch (State & DSS_RTL_MASK)
    {
    case DSS_RTL_REPAIR:
    case DSS_RTL_BASE:
      secondarySet = State;
      break;
    case DSS_RTL_TRANSPORT:
      psTransport = FindATransporter(psDroid);
      if (psTransport != nullptr)
      {
        secondarySet = State;
      }
      break;
    }
    if ((CurrState & DSS_HALT_MASK) == DSS_HALT_HOLD)
    {
      secondaryMask |= DSS_HALT_MASK;
      secondarySet |= DSS_HALT_GUARD;
    }
    break;
  case DSO_UNUSED:
  case DSO_FIRE_DESIGNATOR:
    // Do nothing.
    break;
  }
  uint32_t newSecondaryState = (CurrState & ~secondaryMask) | secondarySet;

  if (bMultiMessages && mode == ModeQueue)
  {
    if (sec == DSO_REPAIR_LEVEL)
    {
      secondaryCheckDamageLevelDeselect(psDroid, State);  // Deselect droid immediately, if applicable, so it isn't ordered around by mistake.
    }

    sendDroidSecondary(psDroid, sec, State);
    secondaryOrderPending = newSecondaryState;
    ++secondaryOrderPendingCount;
    return true;  // Wait for our order before changing the droid.
  }


  // set the state for any droids in the command group
  if ((sec != DSO_RECYCLE) &&
      droidType == DROID_COMMAND &&
      psGroup != nullptr &&
      psGroup->type == GT_COMMAND)
  {
    psGroup->setSecondary(sec, State);
  }

  retVal = true;
  switch (sec)
  {
  case DSO_ATTACK_RANGE:
    CurrState = (CurrState & ~DSS_ARANGE_MASK) | State;
    break;

  case DSO_REPAIR_LEVEL:
    CurrState = (CurrState & ~DSS_REPLEV_MASK) | State;
    secondaryOrder = CurrState;
    secondaryCheckDamageLevel(psDroid);
    break;

  case DSO_ATTACK_LEVEL:
    CurrState = (CurrState & ~DSS_ALEV_MASK) | State;
    if (State == DSS_ALEV_NEVER)
    {
      if (orderState(psDroid, DROID_ORDER_TYPE::ATTACK))
      {
        // just kill these orders
        orderDroid(psDroid, DROID_ORDER_TYPE::STOP, ModeImmediate);
        if (isVtolDroid(psDroid))
        {
          moveToRearm(psDroid);
        }
      }
      else if (droidAttacking(psDroid))
      {
        // send the unit back to the guard position
        actionDroid(psDroid, DROID_ACTION::NONE);
      }
      else if (orderState(psDroid, DROID_ORDER_TYPE::PATROL))
      {
        // send the unit back to the patrol
        actionDroid(psDroid, DROID_ACTION::RETURNTOPOS, actionPos.x, actionPos.y);
      }
    }
    break;


  case DSO_ASSIGN_PRODUCTION:
  case DSO_ASSIGN_CYBORG_PRODUCTION:
  case DSO_ASSIGN_VTOL_PRODUCTION:
#ifdef DEBUG
    debug(LOG_NEVER, "order factories %s\n", secondaryPrintFactories(State));
#endif
    if (sec == DSO_ASSIGN_PRODUCTION)
    {
      prodType = REF_FACTORY;
    }
    else if (sec == DSO_ASSIGN_CYBORG_PRODUCTION)
    {
      prodType = REF_CYBORG_FACTORY;
    }
    else
    {
      prodType = REF_VTOL_FACTORY;
    }

    if (droidType == DROID_COMMAND)
    {
      // look for the factories
      for (psStruct = apsStructLists[owningPlayer]; psStruct;
           psStruct = psStruct->psNext)
      {
        factType = psStruct->stats->type;
        if (factType == REF_FACTORY ||
            factType == REF_VTOL_FACTORY ||
            factType == REF_CYBORG_FACTORY)
        {
          factoryInc = ((Factory *)psStruct->pFunctionality)->psAssemblyPoint->factoryInc;
          if (factType == REF_FACTORY)
          {
            factoryInc += DSS_ASSPROD_SHIFT;
          }
          else if (factType == REF_CYBORG_FACTORY)
          {
            factoryInc += DSS_ASSPROD_CYBORG_SHIFT;
          }
          else
          {
            factoryInc += DSS_ASSPROD_VTOL_SHIFT;
          }
          if (!(CurrState & (1 << factoryInc)) &&
              (State & (1 << factoryInc)))
          {
            assignFactoryCommandDroid(psStruct, psDroid);// assign this factory to the command droid
          }
          else if ((prodType == factType) &&
                   (CurrState & (1 << factoryInc)) &&
                   !(State & (1 << factoryInc)))
          {
            // remove this factory from the command droid
            assignFactoryCommandDroid(psStruct, nullptr);
          }
        }
      }
      if (prodType == REF_FACTORY)
      {
        CurrState &= ~DSS_ASSPROD_FACT_MASK;
      }
      else if (prodType == REF_CYBORG_FACTORY)
      {
        CurrState &= ~DSS_ASSPROD_CYB_MASK;
      }
      else
      {
        CurrState &= ~DSS_ASSPROD_VTOL_MASK;
      }
      CurrState |= (State & DSS_ASSPROD_MASK);
#ifdef DEBUG
      debug(LOG_NEVER, "final factories %s\n", secondaryPrintFactories(CurrState));
#endif
    }
    break;

  case DSO_CLEAR_PRODUCTION:
    if (droidType == DROID_COMMAND)
    {
      // simply clear the flag - all the factory stuff is done in assignFactoryCommandDroid
      CurrState &= ~(State & DSS_ASSPROD_MASK);
    }
    break;


  case DSO_RECYCLE:
    if (State & DSS_RECYCLE_MASK)
    {
      if (!orderState(psDroid, DROID_ORDER_TYPE::RECYCLE))
      {
        orderDroid(psDroid, DROID_ORDER_TYPE::RECYCLE, ModeImmediate);
      }
      CurrState &= ~(DSS_RTL_MASK | DSS_RECYCLE_MASK | DSS_HALT_MASK);
      CurrState |= DSS_RECYCLE_SET | DSS_HALT_GUARD;
      group = UBYTE_MAX;
      if (psGroup != nullptr)
      {
        if (droidType == DROID_COMMAND)
        {
          // remove all the units from the commanders group
          for (psCurr = psGroup->psList; psCurr; psCurr = psNext)
          {
            psNext = psCurr->psGrpNext;
            psCurr->psGroup->remove(psCurr);
            orderDroid(psCurr, DROID_ORDER_TYPE::STOP, ModeImmediate);
          }
        }
        else if (psGroup->type == GT_COMMAND)
        {
          psGroup->remove(psDroid);
        }
      }
    }
    else
    {
      if (orderState(psDroid, DROID_ORDER_TYPE::RECYCLE))
      {
        orderDroid(psDroid, DROID_ORDER_TYPE::STOP, ModeImmediate);
      }
      CurrState &= ~DSS_RECYCLE_MASK;
    }
    break;
  case DSO_CIRCLE:
    if (State & DSS_CIRCLE_SET)
    {
      CurrState |= DSS_CIRCLE_SET;
    }
    else
    {
      CurrState &= ~DSS_CIRCLE_MASK;
    }
    break;
  case DSO_PATROL:
    if (State & DSS_PATROL_SET)
    {
      CurrState |= DSS_PATROL_SET;
    }
    else
    {
      CurrState &= ~DSS_PATROL_MASK;
    }
    break;
  case DSO_HALTTYPE:
    switch (State & DSS_HALT_MASK)
    {
    case DSS_HALT_PURSUE:
      CurrState &= ~ DSS_HALT_MASK;
      CurrState |= DSS_HALT_PURSUE;
      if (orderState(psDroid, DROID_ORDER_TYPE::GUARD))
      {
        orderDroid(psDroid, DROID_ORDER_TYPE::STOP, ModeImmediate);
      }
      break;
    case DSS_HALT_GUARD:
      CurrState &= ~ DSS_HALT_MASK;
      CurrState |= DSS_HALT_GUARD;
      orderDroidLoc(psDroid, DROID_ORDER_TYPE::GUARD, position.x, position.y, ModeImmediate);
      break;
    case DSS_HALT_HOLD:
      CurrState &= ~ DSS_HALT_MASK;
      CurrState |= DSS_HALT_HOLD;
      if (!orderState(psDroid, DROID_ORDER_TYPE::FIRESUPPORT))
      {
        orderDroid(psDroid, DROID_ORDER_TYPE::STOP, ModeImmediate);
      }
      break;
    }
    break;
  case DSO_RETURN_TO_LOC:
    if ((State & DSS_RTL_MASK) == 0)
    {
      if (orderState(psDroid, DROID_ORDER_TYPE::RTR) ||
          orderState(psDroid, DROID_ORDER_TYPE::RTB) ||
          orderState(psDroid, DROID_ORDER_TYPE::EMBARK))
      {
        orderDroid(psDroid, DROID_ORDER_TYPE::STOP, ModeImmediate);
      }
      CurrState &= ~DSS_RTL_MASK;
    }
    else
    {
      order = DROID_ORDER_TYPE::NONE;
      CurrState &= ~DSS_RTL_MASK;
      if ((CurrState & DSS_HALT_MASK) == DSS_HALT_HOLD)
      {
        CurrState &= ~DSS_HALT_MASK;
        CurrState |= DSS_HALT_GUARD;
      }
      switch (State & DSS_RTL_MASK)
      {
      case DSS_RTL_REPAIR:
        order = DROID_ORDER_TYPE::RTR;
        CurrState |= DSS_RTL_REPAIR;
        // can't clear the selection here cos it breaks the secondary order screen
        break;
      case DSS_RTL_BASE:
        order = DROID_ORDER_TYPE::RTB;
        CurrState |= DSS_RTL_BASE;
        break;
      case DSS_RTL_TRANSPORT:
        psTransport = FindATransporter(psDroid);
        if (psTransport != nullptr)
        {
          order = DROID_ORDER_TYPE::EMBARK;
          CurrState |= DSS_RTL_TRANSPORT;
          if (!orderState(psDroid, DROID_ORDER_TYPE::EMBARK))
          {
            orderDroidObj(psDroid, DROID_ORDER_TYPE::EMBARK, psTransport, ModeImmediate);
          }
        }
        else
        {
          retVal = false;
        }
        break;
      default:
        order = DROID_ORDER_TYPE::NONE;
        break;
      }
      if (!orderState(psDroid, order))
      {
        orderDroid(psDroid, order, ModeImmediate);
      }
    }
    break;

  case DSO_FIRE_DESIGNATOR:
    // don't actually set any secondary flags - the cmdDroid array is
    // always used to determine which commander is the designator
    if (State & DSS_FIREDES_SET)
    {
      cmdDroidSetDesignator(psDroid);
    }
    else if (cmdDroidGetDesignator(owningPlayer) == psDroid)
    {
      cmdDroidClearDesignator(owningPlayer);
    }
    break;

  default:
    break;
  }

  if (CurrState != newSecondaryState)
  {
    debug(LOG_WARNING, "Guessed the new secondary state incorrectly, expected 0x%08X, got 0x%08X, was 0x%08X, sec = %d, state = 0x%08X.", newSecondaryState, CurrState, secondaryOrder, sec, State);
  }
  secondaryOrder = CurrState;
  secondaryOrderPendingCount = std::max(secondaryOrderPendingCount - 1, 0);
  if (secondaryOrderPendingCount == 0)
  {
    secondaryOrderPending = secondaryOrder;  // If no orders are pending, make sure UI uses the actual state.
  }

  return retVal;
}

void Droid::_syncDebugDroid(const char *function, char ch)
{
  if (type != OBJ_DROID) {
    ASSERT(false, "%c Broken type %u!", ch, type);
    syncDebug("Broken type %u!", type);
  }
  int list[] =
      {
          ch,

          (int)id,

          owningPlayer,
          position.x, position.y, position.z,
          rotation.direction, rotation.pitch, rotation.roll,
          (int)order.type, order.pos.x, order.pos.y, listSize,
          (int)action,
          (int)secondaryOrder,
          (int)hitPoints,
          (int)sMove.Status,
          sMove.speed, sMove.moveDir,
          sMove.pathIndex, (int)sMove.asPath.size(),
          sMove.src.x, sMove.src.y, sMove.target.x, sMove.target.y, sMove.destination.x, sMove.destination.y,
          sMove.bumpDir, (int)sMove.bumpTime, (int)sMove.lastBump, (int)sMove.pauseTime, sMove.bumpPos.x, sMove.bumpPos.y, (int)sMove.shuffleStart,
          (int)experience,
      };
  _syncDebugIntList(function, "%c droid%d = p%d;pos(%d,%d,%d),rot(%d,%d,%d),order%d(%d,%d)^%d,action%d,secondaryOrder%X,body%d,sMove(status%d,speed%d,moveDir%d,path%d/%d,src(%d,%d),target(%d,%d),destination(%d,%d),bump(%d,%d,%d,%d,(%d,%d),%d)),exp%u", list, ARRAY_SIZE(list));
}

bool Droid::droidUpdateDroidRepair()
{
  ASSERT_OR_RETURN(false, action == DROID_ACTION::DROIDREPAIR, "Unit does not have unit repair order");
  ASSERT_OR_RETURN(false, asBits[COMP_REPAIRUNIT] != 0, "Unit does not have a repair turret");

  Droid *psDroidToRepair = (Droid *)psActionTarget[0];
  ASSERT_OR_RETURN(false, psDroidToRepair->type == OBJECT_TYPE::DROID, "Target is not a unit");
  bool needMoreRepair = droidUpdateDroidRepairBase(psRepairDroid, psDroidToRepair);
  if (needMoreRepair && psDroidToRepair->order.type == DORDER_RTR && psDroidToRepair->order.rtrType == RTR_TYPE_DROID && psDroidToRepair->action == DACTION_NONE)
  {
    psDroidToRepair->action = DROID_ACTION::WAITDURINGREPAIR;
  }
  if (!needMoreRepair && psDroidToRepair->order.type == DORDER_RTR && psDroidToRepair->order.rtrType == RTR_TYPE_DROID)
  {
    // if psDroidToRepair has a commander, commander will call him back anyway
    // if no commanders, just DORDER_GUARD the repair turret
    orderDroidObj(psDroidToRepair, DORDER_GUARD, psRepairDroid, ModeImmediate);
    secondarySetState(psDroidToRepair, DSO_RETURN_TO_LOC, DSS_NONE);
    psDroidToRepair->order.psObj = nullptr;
  }
  return needMoreRepair;
}

// balance the load at random
// always prefer faster repairs
RtrBestResult Droid::decideWhereToRepairAndBalance()
{
  int bestDistToRepairFac = INT32_MAX, bestDistToRepairDroid = INT32_MAX;
  int thisDistToRepair = 0;
  Structure *psHq = nullptr;
  Position bestDroidPos, bestFacPos;
  // static to save allocations
  static std::vector<Position> vFacilityPos;
  static std::vector<Structure *> vFacility;
  static std::vector<int> vFacilityCloseEnough;
  static std::vector<Position> vDroidPos;
  static std::vector<Droid *> vDroid;
  static std::vector<int> vDroidCloseEnough;
  // clear vectors from previous invocations
  vFacilityPos.clear();
  vFacility.clear();
  vFacilityCloseEnough.clear();
  vDroidCloseEnough.clear();
  vDroidPos.clear();
  vDroid.clear();

  for (Structure *psStruct = apsStructLists[owningPlayer]; psStruct; psStruct = psStruct->psNext)
  {
    if (psStruct->stats->type == REF_HQ)
    {
      psHq = psStruct;
      continue;
    }
    if (psStruct->stats->type == REF_REPAIR_FACILITY && psStruct->status == SS_BUILT)
    {
      thisDistToRepair = droidSqDist(psDroid, psStruct);
      if (thisDistToRepair <= 0)
      {
        continue;	// cannot reach position
      }
      vFacilityPos.push_back(psStruct->position);
      vFacility.push_back(psStruct);
      if (bestDistToRepairFac > thisDistToRepair)
      {
        bestDistToRepairFac = thisDistToRepair;
        bestFacPos = psStruct->position;
      }
    }
  }
  // if we are repair droid ourselves, don't consider other repairs droids
  // because that causes havoc on front line: RT repairing themselves,
  // blocking everyone else. And everyone else moving toward RT, also toward front line.s
  // Ideally, we should just avoid retreating toward "danger", but dangerMap is only for multiplayer
  if (droidType != DROID_REPAIR && droidType != DROID_CYBORG_REPAIR)
  {
    // one of these lists is empty when on mission
    Droid *psdroidList =
        allDroidLists[owningPlayer] != nullptr ? allDroidLists[owningPlayer] : mission.apsDroidLists[owningPlayer];
    for (Droid *psCurr = psdroidList; psCurr != nullptr; psCurr = psCurr->psNext)
    {
      if (psCurr->droidType == DROID_REPAIR || psCurr->droidType == DROID_CYBORG_REPAIR)
      {
        thisDistToRepair = droidSqDist(psDroid, psCurr);
        if (thisDistToRepair <= 0)
        {
          continue; // unreachable
        }
        vDroidPos.push_back(psCurr->position);
        vDroid.push_back(psCurr);
        if (bestDistToRepairDroid > thisDistToRepair)
        {
          bestDistToRepairDroid = thisDistToRepair;
          bestDroidPos = psCurr->position;
        }
      }
    }
  }

  ASSERT(bestDistToRepairFac > 0, "Bad distance to repair facility");
  ASSERT(bestDistToRepairDroid > 0, "Bad distance to repair droid");
// debug(LOG_INFO, "found a total of %lu RT, and %lu RF", vDroid.size(), vFacility.size());

// the center of this area starts at the closest repair droid/facility!
#define MAGIC_SUITABLE_REPAIR_AREA ((REPAIR_RANGE*3) * (REPAIR_RANGE*3))
  Position bestRepairPoint = bestDistToRepairFac < bestDistToRepairDroid ? bestFacPos: bestDroidPos;
  // find all close enough repairing candidates
  for (int i=0; i < vFacilityPos.size(); i++)
  {
    Vector2i diff = (bestRepairPoint - vFacilityPos[i]).xy();
    if (dot(diff, diff) < MAGIC_SUITABLE_REPAIR_AREA)
    {
      vFacilityCloseEnough.push_back(i);
    }
  }
  for (int i=0; i < vDroidPos.size(); i++)
  {
    Vector2i diff = (bestRepairPoint - vDroidPos[i]).xy();
    if (dot(diff, diff) < MAGIC_SUITABLE_REPAIR_AREA)
    {
      vDroidCloseEnough.push_back(i);
    }
  }

  // debug(LOG_INFO, "found  %lu RT, and %lu RF in suitable area", vDroidCloseEnough.size(), vFacilityCloseEnough.size());
  // prefer facilities, they re much more efficient than droids
  if (vFacilityCloseEnough.size() == 1)
  {
    return RtrBestResult(RTR_TYPE_REPAIR_FACILITY, vFacility[vFacilityCloseEnough[0]]);

  } else if (vFacilityCloseEnough.size() > 1)
  {
    int32_t which = gameRand(vFacilityCloseEnough.size());
    return RtrBestResult(RTR_TYPE_REPAIR_FACILITY, vFacility[vFacilityCloseEnough[which]]);
  }

  // no facilities :( fallback on droids
  if (vDroidCloseEnough.size() == 1)
  {
    return RtrBestResult(RTR_TYPE_DROID, vDroid[vDroidCloseEnough[0]]);
  } else if (vDroidCloseEnough.size() > 1)
  {
    int32_t which = gameRand(vDroidCloseEnough.size());
    return RtrBestResult(RTR_TYPE_DROID, vDroid[vDroidCloseEnough[which]]);
  }

  // go to headquarters, if any
  if (psHq != nullptr)
  {
    return RtrBestResult(RTR_TYPE_HQ, psHq);
  }
  // screw it
  return RtrBestResult(RTR_TYPE_NO_RESULT, nullptr);
}

// Update the action state for a droid
void Droid::actionUpdateDroid()
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

  PROPULSION_STATS *psPropStats = asPropulsionStats + asBits[COMP_PROPULSION];
  ASSERT_OR_RETURN(, psPropStats != nullptr, "Invalid propulsion stats pointer");

  bool secHoldActive = secondaryGetState(psDroid, DSO_HALTTYPE) == DSS_HALT_HOLD;

  actionSanity(psDroid);

  //if the droid has been attacked by an EMP weapon, it is temporarily disabled
  if (lastHitWeapon == WSC_EMP)
  {
    if (gameTime - timeLastHit > EMP_DISABLE_TIME)
    {
      //the actionStarted time needs to be adjusted
      actionStarted += (gameTime - timeLastHit);
      //reset the lastHit parameters
      timeLastHit = 0;
      lastHitWeapon = WSC_NUM_WEAPON_SUBCLASSES;
    }
    else
    {
      //get out without updating
      return;
    }
  }

  for (unsigned i = 0; i < numWeapons; ++i)
  {
    if (m_weaponList[i].nStat > 0)
    {
      nonNullWeapon[i] = true;
    }
  }

  // HACK: Apparently we can't deal with a droid that only has NULL weapons ?
  // FIXME: Find out whether this is really necessary
  if (numWeapons <= 1)
  {
    nonNullWeapon[0] = true;
  }

  DROID_ORDER_DATA *order = &order;

  switch (action)
  {
  case DROID_ACTION::NONE:
  case DROID_ACTION::WAITFORREPAIR:
    // doing nothing
    // see if there's anything to shoot.
    if (numWeapons > 0 && !isVtolDroid(psDroid)
        && (order->type == DORDER_NONE || order->type == DORDER_HOLD || order->type == DORDER_RTR || order->type == DORDER_GUARD))
    {
      for (unsigned i = 0; i < numWeapons; ++i)
      {
        if (nonNullWeapon[i])
        {
          GameObject *psTemp = nullptr;

          WEAPON_STATS *const psWeapStats = &asWeaponStats[m_weaponList[i].nStat];
          if (m_weaponList[i].nStat > 0
              && psWeapStats->rotate
              && aiBestNearestTarget(psDroid, &psTemp, i) >= 0)
          {
            if (secondaryGetState(psDroid, DSO_ATTACK_LEVEL) == DSS_ALEV_ALWAYS)
            {
              action = DROID_ACTION::ATTACK;
              setDroidActionTarget(psDroid, psTemp, i);
            }
          }
        }
      }
    }
    break;
  case DROID_ACTION::WAITDURINGREPAIR:
    // Check that repair facility still exists
    if (!order->psObj)
    {
      action = DROID_ACTION::NONE;
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
  case DROID_ACTION::TRANSPORTWAITTOFLYIN:
    //if we're moving droids to safety and currently waiting to fly back in, see if time is up
    if (owningPlayer == selectedPlayer && getDroidsToSafetyFlag())
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
        position.x = droidX;
        position.y = droidY;
        //fly Transporter back to get some more droids
        orderDroidLoc(psDroid, DORDER_TRANSPORTIN,
                      getLandingX(selectedPlayer), getLandingY(selectedPlayer), ModeImmediate);
      }
    }
    break;

  case DROID_ACTION::MOVE:
  case DROID_ACTION::RETURNTOPOS:
  case DROID_ACTION::FIRESUPPORT_RETREAT:
    // moving to a location
    if (DROID_STOPPED(psDroid))
    {
      bool notify = action == DROID_ACTION::MOVE;
      // Got to destination
      action = DROID_ACTION::NONE;

      if (notify)
      {
        /* notify scripts we have reached the destination
				*  also triggers when patrolling and reached a waypoint
         */

        triggerEventDroidIdle(psDroid);
      }
    }
    //added multiple weapon check
    else if (numWeapons > 0)
    {
      for (unsigned i = 0; i < numWeapons; ++i)
      {
        if (nonNullWeapon[i])
        {
          GameObject *psTemp = nullptr;

          //I moved psWeapStats flag update there
          WEAPON_STATS *const psWeapStats = &asWeaponStats[m_weaponList[i].nStat];
          if (!isVtolDroid(psDroid)
              && m_weaponList[i].nStat > 0
              && psWeapStats->rotate
              && psWeapStats->fireOnMove
              && aiBestNearestTarget(psDroid, &psTemp, i) >= 0)
          {
            if (secondaryGetState(psDroid, DSO_ATTACK_LEVEL) == DSS_ALEV_ALWAYS)
            {
              action = DROID_ACTION::MOVEFIRE;
              setDroidActionTarget(psDroid, psTemp, i);
            }
          }
        }
      }
    }
    break;
  case DROID_ACTION::TRANSPORTIN:
  case DROID_ACTION::TRANSPORTOUT:
    actionUpdateTransporter(psDroid);
    break;
  case DROID_ACTION::MOVEFIRE:
    // check if vtol is armed
    if (vtolEmpty(psDroid))
    {
      moveToRearm(psDroid);
    }
    // If droid stopped, it can no longer be in DROID_ACTION::MOVEFIRE
    if (DROID_STOPPED(psDroid))
    {
      action = DROID_ACTION::NONE;
      break;
    }
    // loop through weapons and look for target for each weapon
    bHasTarget = false;
    for (unsigned i = 0; i < numWeapons; ++i)
    {
      bDirect = proj_Direct(asWeaponStats + m_weaponList[i].nStat);
      blockingWall = nullptr;
      // Does this weapon have a target?
      if (psActionTarget[i] != nullptr)
      {
        // Is target worth shooting yet?
        if (aiObjectIsProbablyDoomed(psActionTarget[i], bDirect))
        {
          setDroidActionTarget(psDroid, nullptr, i);
        }
        // Is target from our team now? (Electronic Warfare)
        else if (electronicDroid(psDroid) && owningPlayer == psActionTarget[i]->owningPlayer)
        {
          setDroidActionTarget(psDroid, nullptr, i);
        }
        // Is target blocked by a wall?
        else if (bDirect && visGetBlockingWall(psDroid, psActionTarget[i]))
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
          setDroidActionTarget(psDroid, psTemp, i); // this updates psActionTarget[i] to != NULL
        }
      }
      // If we have a target for the weapon: is it visible?
      if (psActionTarget[i] != nullptr && visibleObject(psDroid, psActionTarget[i], false) > UBYTE_MAX / 2)
      {
        hasVisibleTarget = true; // droid have a visible target to shoot
        targetVisibile[i] = true;// it is at least visible for this weapon
      }
    }
    // if there is at least one target
    if (bHasTarget)
    {
      // loop through weapons
      for (unsigned i = 0; i < numWeapons; ++i)
      {
        const unsigned compIndex = m_weaponList[i].nStat;
        const WEAPON_STATS *psStats = asWeaponStats + compIndex;
        wallBlocked = false;

        // has weapon a target? is target valid?
        if (psActionTarget[i] != nullptr && validTarget(psDroid, psActionTarget[i], i))
        {
          // is target visible and weapon is not a Nullweapon?
          if (targetVisibile[i] && nonNullWeapon[i]) //to fix a AA-weapon attack ground unit exploit
          {
            GameObject *psActionTarget = nullptr;
            blockingWall = visGetBlockingWall(psDroid, psActionTarget[i]);

            if (proj_Direct(psStats) && blockingWall)
            {
              WEAPON_EFFECT weapEffect = psStats->weaponEffect;

              if (!aiCheckAlliances(owningPlayer, blockingWall->owningPlayer)
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
              psActionTarget = psActionTarget[i];
            }

            // is the turret aligned with the target?
            if (!wallBlocked && actionTargetTurret(psDroid, psActionTarget, &m_weaponList
                                                                                 [i]))
            {
              // In range - fire !!!
              combFire(&m_weaponList
                            [i], psDroid, psActionTarget, i);
            }
          }
        }
      }
      // Droid don't have a visible target and it is not in pursue mode
      if (!hasVisibleTarget && secondaryGetState(psDroid, DSO_ATTACK_LEVEL) != DSS_ALEV_ALWAYS)
      {
        // Target lost
        action = DROID_ACTION::MOVE;
      }
    }
    // it don't have a target, change to DROID_ACTION::MOVE
    else
    {
      action = DROID_ACTION::MOVE;
    }
    //check its a VTOL unit since adding Transporter's into multiPlayer
    /* check vtol attack runs */
    if (isVtolDroid(psDroid))
    {
      actionUpdateVtolAttack(psDroid);
    }
    break;
  case DROID_ACTION::ATTACK:
  case DROID_ACTION::ROTATETOATTACK:
    if (psActionTarget[0] == nullptr &&  psActionTarget[1] != nullptr)
    {
      break;
    }
    ASSERT_OR_RETURN(, psActionTarget[0] != nullptr, "target is NULL while attacking");

    if (action == DROID_ACTION::ROTATETOATTACK)
    {
      if (sMove.Status == MOVETURNTOTARGET)
      {
        moveTurnDroid(psDroid, psActionTarget[0]->position.x, psActionTarget[0]->position.y);
        break;  // Still turning.
      }
      action = DROID_ACTION::ATTACK;
    }

    //check the target hasn't become one the same player ID - Electronic Warfare
    if (electronicDroid(psDroid) && owningPlayer == psActionTarget[0]->owningPlayer)
    {
      for (unsigned i = 0; i < numWeapons; ++i)
      {
        setDroidActionTarget(psDroid, nullptr, i);
      }
      action = DROID_ACTION::NONE;
      break;
    }

    bHasTarget = false;
    wallBlocked = false;
    for (unsigned i = 0; i < numWeapons; ++i)
    {
      GameObject *psActionTarget;

      if (i > 0)
      {
        // If we're ordered to shoot something, and we can, shoot it
        if ((order->type == DORDER_ATTACK || order->type == DORDER_ATTACKTARGET) &&
            psActionTarget[i] != psActionTarget[0] &&
            validTarget(psDroid, psActionTarget[0], i) &&
            actionInRange(psDroid, psActionTarget[0], i))
        {
          setDroidActionTarget(psDroid, psActionTarget[0], i);
        }
        // If we still don't have a target, try to find one
        else
        {
          if (psActionTarget[i] == nullptr &&
              aiChooseTarget(psDroid, &psTargets[i], i, false, nullptr))  // Can probably just use psTarget instead of psTargets[i], and delete the psTargets variable.
          {
            setDroidActionTarget(psDroid, psTargets[i], i);
          }
        }
      }

      if (psActionTarget[i])
      {
        psActionTarget = psActionTarget[i];
      }
      else
      {
        psActionTarget = psActionTarget[0];
      }

      if (nonNullWeapon[i]
          && actionVisibleTarget(psDroid, psActionTarget, i)
          && actionInRange(psDroid, psActionTarget, i))
      {
        WEAPON_STATS *const psWeapStats = &asWeaponStats[m_weaponList[i].nStat];
        WEAPON_EFFECT weapEffect = psWeapStats->weaponEffect;
        blockingWall = visGetBlockingWall(psDroid, psActionTarget);

        // if a wall is inbetween us and the target, try firing at the wall if our
        // weapon is good enough
        if (proj_Direct(psWeapStats) && blockingWall)
        {
          if (!aiCheckAlliances(owningPlayer, blockingWall->owningPlayer)
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
            const uint16_t targetDir = calcDirection(position.x, position.y, psActionTarget->position
                                                                                                   .x, psActionTarget->position
                                                         .y);
            dirDiff = abs(angleDelta(targetDir - rotation
                                                     .direction));
          }

          if (dirDiff > FIXED_TURRET_DIR)
          {
            if (i > 0)
            {
              if (psActionTarget[i] != psActionTarget[0])
              {
                // Nope, can't shoot this, try something else next time
                setDroidActionTarget(psDroid, nullptr, i);
              }
            }
            else if (sMove.Status != MOVESHUFFLE)
            {
              action = DROID_ACTION::ROTATETOATTACK;
              moveTurnDroid(psDroid, psActionTarget->position.x, psActionTarget->position.y);
            }
          }
          else if (!psWeapStats->rotate ||
                   actionTargetTurret(psDroid, psActionTarget, &m_weaponList
                                                                    [i]))
          {
            /* In range - fire !!! */
            combFire(&m_weaponList[i], psDroid, psActionTarget, i);
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
        action = DROID_ACTION::NONE; // secondary holding, cancel the order.
      }
      else if (secondaryGetState(psDroid, DSO_HALTTYPE) == DSS_HALT_PURSUE &&
               !supportsSensorTower &&
               !(order->type == DORDER_HOLD ||
                 order->type == DORDER_RTR))
      {
        //We need this so pursuing doesn't stop if a unit is ordered to move somewhere while
        //it is still in weapon range of the target when reaching the end destination.
        //Weird case, I know, but keeps the previous pursue order intact.
        action = DROID_ACTION::MOVETOATTACK;	// out of range - chase it
      }
      else if (supportsSensorTower ||
               order->type == DORDER_NONE ||
               order->type == DORDER_HOLD ||
               order->type == DORDER_RTR)
      {
        // don't move if on hold or firesupport for a sensor tower
        // also don't move if we're holding position or waiting for repair
        action = DROID_ACTION::NONE; // holding, cancel the order.
      }
      //Units attached to commanders are always guarding the commander
      else if (secHoldActive && order->type == DORDER_GUARD && hasCommander(psDroid))
      {
        Droid *commander = psGroup->psCommander;

        if (commander->order.type == DORDER_ATTACKTARGET ||
            commander->order.type == DORDER_FIRESUPPORT ||
            commander->order.type == DORDER_ATTACK)
        {
          action = DROID_ACTION::MOVETOATTACK;
        }
        else
        {
          action = DROID_ACTION::NONE;
        }
      }
      else if (secondaryGetState(psDroid, DSO_HALTTYPE) != DSS_HALT_HOLD)
      {
        action = DROID_ACTION::MOVETOATTACK;	// out of range - chase it
      }
      else
      {
        order.psObj = nullptr;
        action = DROID_ACTION::NONE;
      }
    }

    break;

  case DROID_ACTION::VTOLATTACK:
  {
    WEAPON_STATS *psWeapStats = nullptr;
    const bool targetIsValid = validTarget(psDroid, psActionTarget[0], 0);
    //uses vtResult
    if (psActionTarget[0] != nullptr &&
        targetIsValid)
    {
      //check if vtol that its armed
      if ((vtolEmpty(psDroid)) ||
          (psActionTarget[0] == nullptr) ||
          //check the target hasn't become one the same player ID - Electronic Warfare
          (electronicDroid(psDroid) && (owningPlayer == psActionTarget[0]->owningPlayer)) ||
          // Huh? !targetIsValid can't be true, we just checked for it
          !targetIsValid)
      {
        moveToRearm(psDroid);
        break;
      }

      for (unsigned i = 0; i < numWeapons; ++i)
      {
        if (nonNullWeapon[i]
            && validTarget(psDroid, psActionTarget[0], i))
        {
          //I moved psWeapStats flag update there
          psWeapStats = &asWeaponStats[m_weaponList[i].nStat];
          if (actionVisibleTarget(psDroid, psActionTarget[0], i))
          {
            if (actionInRange(psDroid, psActionTarget[0], i))
            {
              if (owningPlayer == selectedPlayer)
              {
                audio_QueueTrackMinDelay(ID_SOUND_COMMENCING_ATTACK_RUN2,
                                         VTOL_ATTACK_AUDIO_DELAY);
              }

              if (actionTargetTurret(psDroid, psActionTarget[0], &m_weaponList
                                                                               [i]))
              {
                // In range - fire !!!
                combFire(&m_weaponList
                              [i], psDroid,
                         psActionTarget[0], i);
              }
            }
            else
            {
              actionTargetTurret(psDroid, psActionTarget[0], &m_weaponList
                                                                           [i]);
            }
          }
        }
      }
    }

    /* circle around target if hovering and not cyborg */
    Vector2i attackRunDelta = position.xy() - sMove.destination;
    if (DROID_STOPPED(psDroid) || dot(attackRunDelta, attackRunDelta) < TILE_UNITS * TILE_UNITS)
    {
      actionAddVtolAttackRun(psDroid);
    }
    else if(psActionTarget[0] != nullptr &&
             targetIsValid)
    {
      // if the vtol is close to the target, go around again
      Vector2i diff = (position - psActionTarget[0]->position).xy();
      const unsigned rangeSq = dot(diff, diff);
      if (rangeSq < VTOL_ATTACK_TARDIST * VTOL_ATTACK_TARDIST)
      {
        // don't do another attack run if already moving away from the target
        diff = sMove.destination - psActionTarget[0]->position.xy();
        if (dot(diff, diff) < VTOL_ATTACK_TARDIST * VTOL_ATTACK_TARDIST)
        {
          actionAddVtolAttackRun(psDroid);
        }
      }
      // in case psWeapStats is still NULL
      else if (psWeapStats)
      {
        // if the vtol is far enough away head for the target again
        const int maxRange = proj_GetLongRange(psWeapStats, owningPlayer);
        if (rangeSq > maxRange * maxRange)
        {
          // don't do another attack run if already heading for the target
          diff = sMove.destination - psActionTarget[0]->position.xy();
          if (dot(diff, diff) > VTOL_ATTACK_TARDIST * VTOL_ATTACK_TARDIST)
          {
            moveDroidToDirect(psDroid, psActionTarget[0]->position.x, psActionTarget[0]->position.y);
          }
        }
      }
    }
    break;
  }
  case DROID_ACTION::MOVETOATTACK:
    // send vtols back to rearm
    if (isVtolDroid(psDroid) && vtolEmpty(psDroid))
    {
      moveToRearm(psDroid);
      break;
    }

    ASSERT_OR_RETURN(, psActionTarget[0] != nullptr, "action update move to attack target is NULL");
    for (unsigned i = 0; i < numWeapons; ++i)
    {
      hasValidWeapon |= validTarget(psDroid, psActionTarget[0], i);
    }
    //check the target hasn't become one the same player ID - Electronic Warfare, and that the target is still valid.
    if ((electronicDroid(psDroid) && owningPlayer == psActionTarget[0]->owningPlayer) || !hasValidWeapon)
    {
      for (unsigned i = 0; i < numWeapons; ++i)
      {
        setDroidActionTarget(psDroid, nullptr, i);
      }
      action = DROID_ACTION::NONE;
    }
    else
    {
      if (actionVisibleTarget(psDroid, psActionTarget[0], 0))
      {
        for (unsigned i = 0; i < numWeapons; ++i)
        {
          if (nonNullWeapon[i]
              && validTarget(psDroid, psActionTarget[0], i)
              && actionVisibleTarget(psDroid, psActionTarget[0], i))
          {
            bool chaseBloke = false;
            WEAPON_STATS *const psWeapStats = &asWeaponStats[m_weaponList
                                                                 [i].nStat];

            if (psWeapStats->rotate)
            {
              actionTargetTurret(psDroid, psActionTarget[0], &m_weaponList[i]);
            }

            if (!isVtolDroid(psDroid) &&
                psActionTarget[0]->type == OBJ_DROID &&
                ((Droid *)psActionTarget[0])->droidType == DROID_PERSON &&
                psWeapStats->fireOnMove)
            {
              chaseBloke = true;
            }

            if (actionInRange(psDroid, psActionTarget[0], i) && !chaseBloke)
            {
              /* init vtol attack runs count if necessary */
              if (psPropStats->propulsionType == PROPULSION_TYPE_LIFT)
              {
                action = DROID_ACTION::VTOLATTACK;
              }
              else
              {
                if (actionInRange(psDroid, psActionTarget[0], i, false))
                {
                  moveStopDroid(psDroid);
                }

                if (psWeapStats->rotate)
                {
                  action = DROID_ACTION::ATTACK;
                }
                else
                {
                  action = DROID_ACTION::ROTATETOATTACK;
                  moveTurnDroid(psDroid, psActionTarget[0]->position
                                             .x, psActionTarget[0]->position
                                    .y);
                }
              }
            }
            else if (actionInRange(psDroid, psActionTarget[0], i))
            {
              // fire while closing range
              if ((blockingWall = visGetBlockingWall(psDroid, psActionTarget[0])) && proj_Direct(psWeapStats))
              {
                WEAPON_EFFECT weapEffect = psWeapStats->weaponEffect;

                if (!aiCheckAlliances(owningPlayer, blockingWall->owningPlayer)
                    && asStructStrengthModifier[weapEffect][blockingWall->stats
                                                                ->strength] >= MIN_STRUCTURE_BLOCK_STRENGTH)
                {
                  //Shoot at wall if the weapon is good enough against them
                  combFire(&m_weaponList
                                [i], psDroid, (GameObject
                                         *)blockingWall, i);
                }
              }
              else
              {
                combFire(&m_weaponList
                              [i], psDroid, psActionTarget[0], i);
              }
            }
          }
        }
      }
      else
      {
        for (unsigned i = 0; i < numWeapons; ++i)
        {
          if ((m_weaponList[i].rot.direction != 0) ||
              (m_weaponList[i].rot.pitch != 0))
          {
            actionAlignTurret(psDroid, i);
          }
        }
      }

      if (DROID_STOPPED(psDroid) && action != DROID_ACTION::ATTACK)
      {
        /* Stopped moving but haven't reached the target - possibly move again */

        //'hack' to make the droid to check the primary turrent instead of all
        WEAPON_STATS *const psWeapStats = &asWeaponStats[m_weaponList[0].nStat];

        if (order->type == DORDER_ATTACKTARGET && secHoldActive)
        {
          action = DROID_ACTION::NONE; // on hold, give up.
        }
        else if (actionInsideMinRange(psDroid, psActionTarget[0], psWeapStats))
        {
          if (proj_Direct(psWeapStats) && order->type != DORDER_HOLD)
          {
            SDWORD pbx, pby;

            // try and extend the range
            actionCalcPullBackPoint(psDroid, psActionTarget[0], &pbx, &pby);
            moveDroidTo(psDroid, (UDWORD)pbx, (UDWORD)pby);
          }
          else
          {
            if (psWeapStats->rotate)
            {
              action = DROID_ACTION::ATTACK;
            }
            else
            {
              action = DROID_ACTION::ROTATETOATTACK;
              moveTurnDroid(psDroid, psActionTarget[0]->position.x,
                            psActionTarget[0]->position.y);
            }
          }
        }
        else if (order->type != DORDER_HOLD) // approach closer?
        {
          // try to close the range
          moveDroidTo(psDroid, psActionTarget[0]->position.x, psActionTarget[0]->position.y);
        }
      }
    }
    break;

  case DROID_ACTION::SULK:
    // unable to route to target ... don't do anything aggressive until time is up
    // we need to do something defensive at this point ???

    //hmmm, hope this doesn't cause any problems!
    if (gameTime > actionStarted)
    {
      action = DROID_ACTION::NONE;			// Sulking is over lets get back to the action ... is this all I need to do to get it back into the action?
    }
    break;

  case DROID_ACTION::MOVETOBUILD:
    if (!order->psStats)
    {
      action = DROID_ACTION::NONE;
      break;
    }
    else
    {
      // Determine if the droid can still build or help to build the ordered structure at the specified location
      const StructureStats * const desiredStructure = order->psStats;
      const Structure * const structureAtBuildPosition = getTileStructure(map_coord(actionPos.x), map_coord(actionPos.y));

      if (nullptr != structureAtBuildPosition)
      {
        bool droidCannotBuild = false;

        if (!aiCheckAlliances(structureAtBuildPosition->owningPlayer, owningPlayer))
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
          if (order->type == DORDER_LINEBUILD && map_coord(order.pos) != map_coord(order.pos2))
          {
            // The droid is doing a line build, and there's more to build. This will force the droid to move to the next structure in the line build
            objTrace(id, "DROID_ACTION::MOVETOBUILD: line target is already built, or can't be built - moving to next structure in line");
            action = DROID_ACTION::NONE;
          }
          else
          {
            // Cancel the current build order. This will move the truck onto the next order, if it has one, or halt in place.
            objTrace(id, "DROID_ACTION::MOVETOBUILD: target is already built, or can't be built - executing next order or halting");
            cancelBuild(psDroid);
          }

          break;
        }
      }
    } // End of check for whether the droid can still succesfully build the ordered structure

    // The droid can still build or help with a build, and is moving to a location to do so - are we there yet, are we there yet, are we there yet?
    if (actionReachedBuildPos(psDroid, actionPos.x, actionPos.y, order->direction, order->psStats))
    {
      // We're there, go ahead and build or help to build the structure
      bool buildPosEmpty = actionRemoveDroidsFromBuildPos(owningPlayer, actionPos, order->direction, order->psStats);
      if (!buildPosEmpty)
      {
        break;
      }

      bool helpBuild = false;
      // Got to destination - start building
      StructureStats *const psStructStats = order->psStats;
      uint16_t dir = order->direction;
      moveStopDroid(psDroid);
      objTrace(id, "Halted in our tracks - at construction site");
      if (order->type == DORDER_BUILD && order->psObj == nullptr)
      {
        // Starting a new structure
        const Vector2i pos = actionPos;

        //need to check if something has already started building here?
        //unless its a module!
        if (IsStatExpansionModule(psStructStats))
        {
          syncDebug("Reached build target: module");
          debug(LOG_NEVER, "DROID_ACTION::MOVETOBUILD: setUpBuildModule");
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
              action = DROID_ACTION::BUILD;
            }
          }
          else
          {
            syncDebug("Reached build target: already-structure");
            objTrace(id, "DROID_ACTION::MOVETOBUILD: tile has structure already");
            cancelBuild(psDroid);
          }
        }
        else if (!validLocation(order->psStats, pos, dir, owningPlayer, false))
        {
          syncDebug("Reached build target: invalid");
          objTrace(id, "DROID_ACTION::MOVETOBUILD: !validLocation");
          cancelBuild(psDroid);
        }
        else if (droidStartBuild(psDroid) == DroidStartBuildSuccess)  // If DroidStartBuildPending, then there's a burning oil well, and we don't want to change to DROID_ACTION::BUILD until it stops burning.
        {
          syncDebug("Reached build target: build");
          action = DROID_ACTION::BUILD;
          actionStarted = gameTime;
          actionPoints = 0;
        }
      }
      else if (order->type == DORDER_LINEBUILD || order->type == DORDER_BUILD)
      {
        // building a wall.
        MAPTILE *const psTile = worldTile(actionPos);
        syncDebug("Reached build target: wall");
        if (order->psObj == nullptr
            && (TileHasStructure(psTile)
                || TileHasFeature(psTile)))
        {
          if (TileHasStructure(psTile))
          {
            // structure on the build location - see if it is the same type
            Structure *const psStruct = getTileStructure(map_coord(actionPos.x), map_coord(actionPos.y));
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
                objTrace(id, "DROID_ACTION::MOVETOBUILD: start building defense");
                action = DROID_ACTION::BUILD;
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
                objTrace(id, "DROID_ACTION::MOVETOBUILD: start building module");
                action = DROID_ACTION::BUILD;
              }
            }
            else
            {
              objTrace(id, "DROID_ACTION::MOVETOBUILD: line build hit building");
              cancelBuild(psDroid);
            }
#if defined(WZ_CC_GNU) && !defined(WZ_CC_INTEL) && !defined(WZ_CC_CLANG) && (7 <= __GNUC__)
# pragma GCC diagnostic pop
#endif
          }
          else if (TileHasFeature(psTile))
          {
            Feature *feature = getTileFeature(map_coord(actionPos.x), map_coord(actionPos.y));
            objTrace(id, "DROID_ACTION::MOVETOBUILD: tile has feature %d", feature->psStats->subType);
            if (feature->psStats->subType == FEAT_OIL_RESOURCE && order->psStats->type == REF_RESOURCE_EXTRACTOR)
            {
              if (droidStartBuild(psDroid))
              {
                objTrace(id, "DROID_ACTION::MOVETOBUILD: start building oil derrick");
                action = DROID_ACTION::BUILD;
              }
            }
          }
          else
          {
            objTrace(id, "DROID_ACTION::MOVETOBUILD: blocked line build");
            cancelBuild(psDroid);
          }
        }
        else if (droidStartBuild(psDroid))
        {
          action = DROID_ACTION::BUILD;
        }
      }
      else
      {
        syncDebug("Reached build target: planned-help");
        objTrace(id, "DROID_ACTION::MOVETOBUILD: planned-help");
        helpBuild = true;
      }

      if (helpBuild)
      {
        // continuing a partially built structure (order = helpBuild)
        if (droidStartBuild(psDroid))
        {
          objTrace(id, "DROID_ACTION::MOVETOBUILD: starting help build");
          action = DROID_ACTION::BUILD;
        }
      }
    }
    else if (DROID_STOPPED(psDroid))
    {
      objTrace(id, "DROID_ACTION::MOVETOBUILD: Starting to drive toward construction site - move status was %d", (int)sMove.Status);
      moveDroidToNoFormation(psDroid, actionPos.x, actionPos.y);
    }
    break;
  case DROID_ACTION::BUILD:
    if (!order->psStats)
    {
      objTrace(id, "No target stats for build order - resetting");
      action = DROID_ACTION::NONE;
      break;
    }
    if (DROID_STOPPED(psDroid) &&
        !actionReachedBuildPos(psDroid, actionPos.x, actionPos.y, order->direction, order->psStats))
    {
      objTrace(id, "DROID_ACTION::BUILD: Starting to drive toward construction site");
      moveDroidToNoFormation(psDroid, actionPos.x, actionPos.y);
    }
    else if (!DROID_STOPPED(psDroid) &&
             sMove.Status != MOVETURNTOTARGET &&
             sMove.Status != MOVESHUFFLE &&
             actionReachedBuildPos(psDroid, actionPos.x, actionPos.y, order->direction, order->psStats))
    {
      objTrace(id, "DROID_ACTION::BUILD: Stopped - at construction site");
      moveStopDroid(psDroid);
    }
    if (action == DROID_ACTION::SULK)
    {
      objTrace(id, "Failed to go to objective, aborting build action");
      action = DROID_ACTION::NONE;
      break;
    }
    if (droidUpdateBuild(psDroid))
    {
      actionTargetTurret(psDroid, psActionTarget[0], &m_weaponList[0]);
    }
    break;
  case DROID_ACTION::MOVETODEMOLISH:
  case DROID_ACTION::MOVETOREPAIR:
  case DROID_ACTION::MOVETORESTORE:
    if (!order->psStats)
    {
      action = DROID_ACTION::NONE;
      break;
    }
    else
    {
      const Structure * structureAtPos = getTileStructure(map_coord(actionPos.x), map_coord(actionPos.y));

      if (structureAtPos == nullptr)
      {
        //No structure located at desired position. Move on.
        action = DROID_ACTION::NONE;
        break;
      }
      else if (order->type != DORDER_RESTORE)
      {
        bool cantDoRepairLikeAction = false;

        if (!aiCheckAlliances(structureAtPos->owningPlayer, owningPlayer))
        {
          cantDoRepairLikeAction = true;
        }
        else if (order->type != DORDER_DEMOLISH && structureAtPos->hitPoints == structureBody(structureAtPos))
        {
          cantDoRepairLikeAction = true;
        }
        else if (order->type == DORDER_DEMOLISH && structureAtPos->owningPlayer != owningPlayer)
        {
          cantDoRepairLikeAction = true;
        }

        if (cantDoRepairLikeAction)
        {
          action = DROID_ACTION::NONE;
          moveStopDroid(psDroid);
          break;
        }
      }
    }
    // see if the droid is at the edge of what it is moving to
    if (actionReachedBuildPos(psDroid, actionPos.x, actionPos.y, ((Structure *)psActionTarget[0])->rotation.direction, order->psStats))
    {
      moveStopDroid(psDroid);

      // got to the edge - start doing whatever it was meant to do
      droidStartAction(psDroid);
      switch (action)
      {
      case DROID_ACTION::MOVETODEMOLISH:
        action = DROID_ACTION::DEMOLISH;
        break;
      case DROID_ACTION::MOVETOREPAIR:
        action = DROID_ACTION::REPAIR;
        break;
      case DROID_ACTION::MOVETORESTORE:
        action = DROID_ACTION::RESTORE;
        break;
      default:
        break;
      }
    }
    else if (DROID_STOPPED(psDroid))
    {
      moveDroidToNoFormation(psDroid, actionPos.x, actionPos.y);
    }
    break;

  case DROID_ACTION::DEMOLISH:
  case DROID_ACTION::REPAIR:
  case DROID_ACTION::RESTORE:
    if (!order->psStats)
    {
      action = DROID_ACTION::NONE;
      break;
    }
    // set up for the specific action
    switch (action)
    {
    case DROID_ACTION::DEMOLISH:
      // DROID_ACTION::MOVETODEMOLISH;
      actionUpdateFunc = droidUpdateDemolishing;
      break;
    case DROID_ACTION::REPAIR:
      // DROID_ACTION::MOVETOREPAIR;
      actionUpdateFunc = droidUpdateRepair;
      break;
    case DROID_ACTION::RESTORE:
      // DROID_ACTION::MOVETORESTORE;
      actionUpdateFunc = droidUpdateRestore;
      break;
    default:
      break;
    }

    // now do the action update
    if (DROID_STOPPED(psDroid) && !actionReachedBuildPos(psDroid, actionPos.x, actionPos.y, ((Structure *)psActionTarget[0])->rotation.direction, order->psStats))
    {
      if (order->type != DORDER_HOLD && (!secHoldActive || (secHoldActive && order->type != DORDER_NONE)))
      {
        objTrace(id, "Secondary order: Go to construction site");
        moveDroidToNoFormation(psDroid, actionPos.x, actionPos.y);
      }
      else
      {
        action = DROID_ACTION::NONE;
      }
    }
    else if (!DROID_STOPPED(psDroid) &&
             sMove.Status != MOVETURNTOTARGET &&
             sMove.Status != MOVESHUFFLE &&
             actionReachedBuildPos(psDroid, actionPos.x, actionPos.y, ((Structure *)psActionTarget[0])->rotation.direction, order->psStats))
    {
      objTrace(id, "Stopped - reached build position");
      moveStopDroid(psDroid);
    }
    else if (actionUpdateFunc(psDroid))
    {
      //use 0 for non-combat(only 1 'weapon')
      actionTargetTurret(psDroid, psActionTarget[0], &m_weaponList[0]);
    }
    else
    {
      action = DROID_ACTION::NONE;
    }
    break;

  case DROID_ACTION::MOVETOREARMPOINT:
    if (DROID_STOPPED(psDroid))
    {
      objTrace(id, "Finished moving onto the rearm pad");
      action = DROID_ACTION::WAITDURINGREARM;
    }
    break;
  case DROID_ACTION::MOVETOREPAIRPOINT:
    if (order.rtrType == RTR_TYPE_REPAIR_FACILITY)
    {
      /* moving from front to rear of repair facility or rearm pad */
      if (actionReachedBuildPos(psDroid, psActionTarget[0]->position.x, psActionTarget[0]->position.y, ((Structure *)psActionTarget[0])->rotation.direction, ((Structure *)psActionTarget[0])->stats))
      {
        objTrace(id, "Arrived at repair point - waiting for our turn");
        moveStopDroid(psDroid);
        action = DROID_ACTION::WAITDURINGREPAIR;
      }
      else if (DROID_STOPPED(psDroid))
      {
        moveDroidToNoFormation(psDroid, psActionTarget[0]->position.x,
                               psActionTarget[0]->position.y);
      }
    } else if (order.rtrType == RTR_TYPE_DROID)
    {
      bool reached = actionReachedDroid(psDroid, (Droid *) order.psObj);
      if (reached)
      {
        if (hitPoints >= originalBody)
        {
          objTrace(id, "Repair not needed of droid %d", (int)id);
          /* set droid points to max */
          hitPoints = originalBody;
          // if completely repaired then reset order
          secondarySetState(psDroid, DSO_RETURN_TO_LOC, DSS_NONE);
          orderDroidObj(psDroid, DORDER_GUARD, order.psObj, ModeImmediate);
        }
        else
        {
          objTrace(id, "Stopping and waiting for repairs %d", (int)id);
          moveStopDroid(psDroid);
          action = DROID_ACTION::WAITDURINGREPAIR;
        }

      }
      else if (DROID_STOPPED(psDroid))
      {
        //objTrace(id, "Droid was stopped, but havent reach the target, moving now");
        //moveDroidToNoFormation(psDroid, order.psObj->pos.x, order.psObj->pos.y);
      }
    }
    break;
  case DROID_ACTION::OBSERVE:
    // align the turret
    actionTargetTurret(psDroid, psActionTarget[0], &m_weaponList[0]);

    if (!cbSensorDroid(psDroid))
    {
      // make sure the target is within sensor range
      const int xdiff = (SDWORD)position.x - (SDWORD)psActionTarget[0]->position.x;
      const int ydiff = (SDWORD)position.y - (SDWORD)psActionTarget[0]->position.y;
      int rangeSq = droidSensorRange(psDroid);
      rangeSq = rangeSq * rangeSq;
      if (!visibleObject(psDroid, psActionTarget[0], false)
          || xdiff * xdiff + ydiff * ydiff >= rangeSq)
      {
        if (secondaryGetState(psDroid, DSO_HALTTYPE) != DSS_HALT_GUARD && (order->type == DORDER_NONE || order->type == DORDER_HOLD))
        {
          action = DROID_ACTION::NONE;
        }
        else if ((!secHoldActive && order->type != DORDER_HOLD) || (secHoldActive && order->type == DORDER_OBSERVE))
        {
          action = DROID_ACTION::MOVETOOBSERVE;
          moveDroidTo(psDroid, psActionTarget[0]->position.x, psActionTarget[0]->position.y);
        }
      }
    }
    break;
  case DROID_ACTION::MOVETOOBSERVE:
    // align the turret
    actionTargetTurret(psDroid, psActionTarget[0], &m_weaponList[0]);

    if (visibleObject(psDroid, psActionTarget[0], false))
    {
      // make sure the target is within sensor range
      const int xdiff = (SDWORD)position.x - (SDWORD)psActionTarget[0]->position.x;
      const int ydiff = (SDWORD)position.y - (SDWORD)psActionTarget[0]->position.y;
      int rangeSq = droidSensorRange(psDroid);
      rangeSq = rangeSq * rangeSq;
      if ((xdiff * xdiff + ydiff * ydiff < rangeSq) &&
          !DROID_STOPPED(psDroid))
      {
        action = DROID_ACTION::OBSERVE;
        moveStopDroid(psDroid);
      }
    }
    if (DROID_STOPPED(psDroid) && action == DROID_ACTION::MOVETOOBSERVE)
    {
      moveDroidTo(psDroid, psActionTarget[0]->position.x, psActionTarget[0]->position.y);
    }
    break;
  case DROID_ACTION::FIRESUPPORT:
    if (!order->psObj)
    {
      action = DROID_ACTION::NONE;
      return;
    }
    //can be either a droid or a structure now - AB 7/10/98
    ASSERT_OR_RETURN(, (order->psObj->type == OBJ_DROID || order->psObj->type == OBJ_STRUCTURE)
                           && aiCheckAlliances(order->psObj->owningPlayer, owningPlayer), "DROID_ACTION::FIRESUPPORT: incorrect target type");

    //don't move VTOL's
    // also don't move closer to sensor towers
    if (!isVtolDroid(psDroid) && order->psObj->type != OBJ_STRUCTURE)
    {
      Vector2i diff = (position - order->psObj->position).xy();
      //Consider .shortRange here
      int rangeSq = asWeaponStats[m_weaponList[0].nStat].upgrade[owningPlayer].maxRange / 2; // move close to sensor
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
          diff = order->psObj->position.xy() - sMove.destination;
        }
        if (DROID_STOPPED(psDroid) || dot(diff, diff) > rangeSq)
        {
          if (secHoldActive)
          {
            // droid on hold, don't allow moves.
            action = DROID_ACTION::NONE;
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
  case DROID_ACTION::MOVETODROIDREPAIR:
  {
    auto actionTarget = psActionTarget[0];
    assert(actionTarget->type() == OBJ_DROID)

        if (actionTarget.hitPoints == actionTarget.originalBody)
    {
      // target is healthy: nothing to do
      action = DROID_ACTION::NONE;
      moveStopDroid(psDroid);
      break;
    }
    Vector2i diff = (position - psActionTarget[0]->position).xy();
    // moving to repair a droid
    if (!psActionTarget[0] ||  // Target missing.
        (order.type != DORDER_DROIDREPAIR && dot(diff, diff) > 2 * REPAIR_MAXDIST * REPAIR_MAXDIST))  // Target farther then 1.4142 * REPAIR_MAXDIST and we aren't ordered to follow.
    {
      action = DROID_ACTION::NONE;
      return;
    }
    if (dot(diff, diff) < REPAIR_RANGE * REPAIR_RANGE)
    {
      // Got to destination - start repair
      //rotate turret to point at droid being repaired
      //use 0 for repair droid
      actionTargetTurret(psDroid, psActionTarget[0], &m_weaponList[0]);
      droidStartAction(psDroid);
      action = DROID_ACTION::DROIDREPAIR;
    }
    if (DROID_STOPPED(psDroid))
    {
      // Couldn't reach destination - try and find a new one
      actionPos = psActionTarget[0]->position.xy();
      moveDroidTo(psDroid, actionPos.x, actionPos.y);
    }
    break;
  }
  case DROID_ACTION::DROIDREPAIR:
  {
    int xdiff, ydiff;

    // If not doing self-repair (psActionTarget[0] is repair target)
    if (psActionTarget[0] != psDroid)
    {
      actionTargetTurret(psDroid, psActionTarget[0], &m_weaponList[0]);
    }
    // Just self-repairing.
    // See if there's anything to shoot.
    else if (numWeapons > 0 && !isVtolDroid(psDroid)
             && (order->type == DORDER_NONE || order->type == DORDER_HOLD || order->type == DORDER_RTR))
    {
      for (unsigned i = 0; i < numWeapons; ++i)
      {
        if (nonNullWeapon[i])
        {
          GameObject *psTemp = nullptr;

          WEAPON_STATS *const psWeapStats = &asWeaponStats[m_weaponList[i].nStat];
          if (m_weaponList[i].nStat > 0 && psWeapStats->rotate
              && secondaryGetState(psDroid, DSO_ATTACK_LEVEL) == DSS_ALEV_ALWAYS
              && aiBestNearestTarget(psDroid, &psTemp, i) >= 0 && psTemp)
          {
            action = DROID_ACTION::ATTACK;
            setDroidActionTarget(psDroid, psTemp, 0);
            break;
          }
        }
      }
    }
    if (action != DROID_ACTION::DROIDREPAIR)
    {
      break;	// action has changed
    }

    //check still next to the damaged droid
    xdiff = (SDWORD)position.x - (SDWORD)psActionTarget[0]->position.x;
    ydiff = (SDWORD)position.y - (SDWORD)psActionTarget[0]->position.y;
    if (xdiff * xdiff + ydiff * ydiff > REPAIR_RANGE * REPAIR_RANGE)
    {
      if (order->type == DORDER_DROIDREPAIR)
      {
        // damaged droid has moved off - follow if we're not holding position!
        actionPos = psActionTarget[0]->position.xy();
        action = DROID_ACTION::MOVETODROIDREPAIR;
        moveDroidTo(psDroid, actionPos.x, actionPos.y);
      }
      else
      {
        action = DROID_ACTION::NONE;
      }
    }
    else
    {
      if (!droidUpdateDroidRepair(psDroid))
      {
        action = DROID_ACTION::NONE;
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
        if (((Droid *)psActionTarget[0])->sMove.Status == MOVESHUFFLE)
        {
          moveStopDroid((Droid *)psActionTarget[0]);
        }
      }
    }
    break;
  }
  case DROID_ACTION::WAITFORREARM:
    // wait here for the rearm pad to instruct the vtol to move
    if (psActionTarget[0] == nullptr)
    {
      // rearm pad destroyed - move to another
      objTrace(id, "rearm pad gone - switch to new one");
      moveToRearm(psDroid);
      break;
    }
    if (DROID_STOPPED(psDroid) && vtolHappy(psDroid))
    {
      objTrace(id, "do not need to rearm after all");
      // don't actually need to rearm so just sit next to the rearm pad
      action = DROID_ACTION::NONE;
    }
    break;
  case DROID_ACTION::CLEARREARMPAD:
    if (DROID_STOPPED(psDroid))
    {
      action = DROID_ACTION::NONE;
      objTrace(id, "clearing rearm pad");
      if (!vtolHappy(psDroid))  // Droid has cleared the rearm pad without getting rearmed. One way this can happen if a rearming pad was built under the VTOL while it was waiting for a pad.
      {
        moveToRearm(psDroid);  // Rearm somewhere else instead.
      }
    }
    break;
  case DROID_ACTION::WAITDURINGREARM:
    // this gets cleared by the rearm pad
    break;
  case DROID_ACTION::MOVETOREARM:
    if (psActionTarget[0] == nullptr)
    {
      // base destroyed - find another
      objTrace(id, "rearm gone - find another");
      moveToRearm(psDroid);
      break;
    }

    if (visibleObject(psDroid, psActionTarget[0], false))
    {
      Structure *const psStruct = findNearestReArmPad(psDroid, (Structure *)psActionTarget[0], true);
      // got close to the rearm pad - now find a clear one
      objTrace(id, "Seen rearm pad - searching for available one");

      if (psStruct != nullptr)
      {
        // found a clear landing pad - go for it
        objTrace(id, "Found clear rearm pad");
        setDroidActionTarget(psDroid, psStruct, 0);
      }

      action = DROID_ACTION::WAITFORREARM;
    }

    if (DROID_STOPPED(psDroid) || action == DROID_ACTION::WAITFORREARM)
    {
      Vector2i pos = psActionTarget[0]->position.xy();
      if (!actionVTOLLandingPos(psDroid, &pos))
      {
        // totally bunged up - give up
        objTrace(id, "Couldn't find a clear tile near rearm pad - returning to base");
        orderDroid(psDroid, DORDER_RTB, ModeImmediate);
        break;
      }
      objTrace(id, "moving to rearm pad at %d,%d (%d,%d)", (int)pos.x, (int)pos.y, (int)(pos.x/TILE_UNITS), (int)(pos.y/TILE_UNITS));
      moveDroidToDirect(psDroid, pos.x, pos.y);
    }
    break;
  default:
    ASSERT(!"unknown action", "unknown action");
    break;
  }

  if (action != DROID_ACTION::MOVEFIRE &&
      action != DROID_ACTION::ATTACK &&
      action != DROID_ACTION::MOVETOATTACK &&
      action != DROID_ACTION::MOVETODROIDREPAIR &&
      action != DROID_ACTION::DROIDREPAIR &&
      action != DROID_ACTION::BUILD &&
      action != DROID_ACTION::OBSERVE &&
      action != DROID_ACTION::MOVETOOBSERVE)
  {
    //use 0 for all non-combat droid types
    if (numWeapons == 0)
    {
      if (m_weaponList[0].rot.direction != 0 || m_weaponList[0].rot.pitch != 0)
      {
        actionAlignTurret(psDroid, 0);
      }
    }
    else
    {
      for (unsigned i = 0; i < numWeapons; ++i)
      {
        if (m_weaponList[i].rot.direction != 0 || m_weaponList[i].rot.pitch != 0)
        {
          actionAlignTurret(psDroid, i);
        }
      }
    }
  }
  CHECK_DROID(psDroid);
}

/* Overall action function that is called by the specific action functions */
void Droid::actionDroidBase(DROID_ACTION_DATA *psAction)
{
  ASSERT_OR_RETURN(, psAction->targetObj == nullptr || !psAction->targetObj->deathTime, "Droid dead");

  WEAPON_STATS *psWeapStats = getWeaponStats(psDroid, 0);
  Vector2i pos(0, 0);

  CHECK_DROID(psDroid);

  bool secHoldActive = secondaryGetState(psDroid, DSO_HALTTYPE) == DSS_HALT_HOLD;
  actionStarted = gameTime;
  syncDebugDroid(psDroid, '-');
  syncDebug("%d does %s", id, getDroidActionName(psAction->action));
  objTrace(id, "base set action to %s (was %s)", getDroidActionName(psAction->action), getDroidActionName(action));

  DROID_ORDER_DATA *order = &order;
  bool hasValidWeapon = false;
  for (int i = 0; i < MAX_WEAPONS; i++)
  {
    hasValidWeapon |= validTarget(psDroid, psAction->targetObj, i);
  }
  switch (psAction->action)
  {
  case DROID_ACTION::NONE:
    // Clear up what ever the droid was doing before if necessary
    if (!DROID_STOPPED(psDroid))
    {
      moveStopDroid(psDroid);
    }
    action = DROID_ACTION::NONE;
    actionPos = Vector2i(0, 0);
    actionStarted = 0;
    actionPoints = 0;
    if (numWeapons > 0)
    {
      for (int i = 0; i < numWeapons; i++)
      {
        setDroidActionTarget(psDroid, nullptr, i);
      }
    }
    else
    {
      setDroidActionTarget(psDroid, nullptr, 0);
    }
    break;

  case DROID_ACTION::TRANSPORTWAITTOFLYIN:
    action = DROID_ACTION::TRANSPORTWAITTOFLYIN;
    break;

  case DROID_ACTION::ATTACK:
    if (m_weaponList[0].nStat == 0 || isTransporter(psDroid) || psAction->targetObj == psDroid)
    {
      break;
    }
    if (!hasValidWeapon)
    {
      // continuing is pointless, we were given an invalid target
      // for ex. AA gun can't attack ground unit
      break;
    }
    if (electronicDroid())
    {
      //check for low or zero resistance - just zero resistance!
      if (psAction->targetObj->type == OBJ_STRUCTURE
          && !validStructResistance((Structure *)psAction->targetObj))
      {
        //structure is low resistance already so don't attack
        action = DROID_ACTION::NONE;
        break;
      }

      //in multiPlayer cannot electronically attack a transporter
      if (bMultiPlayer
          && psAction->targetObj->type == OBJ_DROID
          && isTransporter((Droid *)psAction->targetObj))
      {
        action = DROID_ACTION::NONE;
        break;
      }
    }

    // note the droid's current pos so that scout & patrol orders know how far the
    // droid has gone during an attack
    // slightly strange place to store this I know, but I didn't want to add any more to the droid
    actionPos = position.xy();
    setDroidActionTarget(psDroid, psAction->targetObj, 0);

    if (((order->type == DORDER_ATTACKTARGET
          || order->type == DORDER_NONE
          || order->type == DORDER_HOLD
          || (order->type == DORDER_GUARD && hasCommander(psDroid))
          || order->type == DORDER_FIRESUPPORT)
         && secHoldActive)
        || (!isVtolDroid(psDroid) && (orderStateObj(psDroid, DORDER_FIRESUPPORT) != nullptr)))
    {
      action = DROID_ACTION::ATTACK;		// holding, try attack straightaway
    }
    else if (actionInsideMinRange(psAction->targetObj, psWeapStats)) // too close?
    {
      if (!proj_Direct(psWeapStats))
      {
        if (psWeapStats->rotate)
        {
          action = DROID_ACTION::ATTACK;
        }
        else
        {
          action = DROID_ACTION::ROTATETOATTACK;
          moveTurnDroid(psDroid, psActionTarget[0]->position.x, psActionTarget[0]->position.y);
        }
      }
      else if (order->type != DORDER_HOLD && secondaryGetState(psDroid, DSO_HALTTYPE) != DSS_HALT_HOLD)
      {
        int pbx = 0;
        int pby = 0;
        /* direct fire - try and extend the range */
        action = DROID_ACTION::MOVETOATTACK;
        actionCalcPullBackPoint(psDroid, psAction->targetObj, &pbx, &pby);

        turnOffMultiMsg(true);
        moveDroidTo(psDroid, (UDWORD)pbx, (UDWORD)pby);
        turnOffMultiMsg(false);
      }
    }
    else if (order->type != DORDER_HOLD && secondaryGetState(psDroid, DSO_HALTTYPE) != DSS_HALT_HOLD) // approach closer?
    {
      action = DROID_ACTION::MOVETOATTACK;
      turnOffMultiMsg(true);
      moveDroidTo(psDroid, psAction->targetObj->position.x, psAction->targetObj->position.y);
      turnOffMultiMsg(false);
    }
    else if (order->type != DORDER_HOLD && secondaryGetState(psDroid, DSO_HALTTYPE) == DSS_HALT_HOLD)
    {
      action = DROID_ACTION::ATTACK;
    }
    break;

  case DROID_ACTION::MOVETOREARM:
    action = DROID_ACTION::MOVETOREARM;
    actionPos = psAction->targetObj->position.xy();
    actionStarted = gameTime;
    setDroidActionTarget(psDroid, psAction->targetObj, 0);
    pos = psActionTarget[0]->position.xy();
    if (!actionVTOLLandingPos(psDroid, &pos))
    {
      // totally bunged up - give up
      objTrace(id, "move to rearm action failed!");
      orderDroid(psDroid, DORDER_RTB, ModeImmediate);
      break;
    }
    objTrace(id, "move to rearm");
    moveDroidToDirect(psDroid, pos.x, pos.y);
    break;
  case DROID_ACTION::CLEARREARMPAD:
    debug(LOG_NEVER, "Unit %d clearing rearm pad", id);
    action = DROID_ACTION::CLEARREARMPAD;
    setDroidActionTarget(psDroid, psAction->targetObj, 0);
    pos = psActionTarget[0]->position.xy();
    if (!actionVTOLLandingPos(&pos))
    {
      // totally bunged up - give up
      objTrace(id, "clear rearm pad action failed!");
      orderDroid(psDroid, DORDER_RTB, ModeImmediate);
      break;
    }
    objTrace(id, "move to clear rearm pad");
    moveDroidToDirect(psDroid, pos.x, pos.y);
    break;
  case DROID_ACTION::MOVE:
  case DROID_ACTION::TRANSPORTIN:
  case DROID_ACTION::TRANSPORTOUT:
  case DROID_ACTION::RETURNTOPOS:
  case DROID_ACTION::FIRESUPPORT_RETREAT:
    action = psAction->action;
    actionPos.x = psAction->x;
    actionPos.y = psAction->y;
    actionStarted = gameTime;
    setDroidActionTarget(psDroid, psAction->targetObj, 0);
    moveDroidTo(psDroid, psAction->x, psAction->y);
    break;

  case DROID_ACTION::BUILD:
    if (!order->psStats)
    {
      action = DROID_ACTION::NONE;
      break;
    }
    //ASSERT_OR_RETURN(, order->type == DORDER_BUILD || order->type == DORDER_HELPBUILD || order->type == DORDER_LINEBUILD, "cannot start build action without a build order");
    ASSERT_OR_RETURN(, psAction->x > 0 && psAction->y > 0, "Bad build order position");
    action = DROID_ACTION::MOVETOBUILD;
    actionPos.x = psAction->x;
    actionPos.y = psAction->y;
    moveDroidToNoFormation(psDroid, actionPos.x, actionPos.y);
    break;
  case DROID_ACTION::DEMOLISH:
    ASSERT_OR_RETURN(, order->type == DORDER_DEMOLISH, "cannot start demolish action without a demolish order");
    action = DROID_ACTION::MOVETODEMOLISH;
    actionPos.x = psAction->x;
    actionPos.y = psAction->y;
    ASSERT_OR_RETURN(, (order->psObj != nullptr) && (order->psObj->type == OBJ_STRUCTURE), "invalid target for demolish order");
    order->psStats = ((Structure *)order->psObj)->stats;
    setDroidActionTarget(psDroid, psAction->targetObj, 0);
    moveDroidTo(psDroid, psAction->x, psAction->y);
    break;
  case DROID_ACTION::REPAIR:
    action = psAction->action;
    actionPos.x = psAction->x;
    actionPos.y = psAction->y;
    //this needs setting so that automatic repair works
    setDroidActionTarget(psDroid, psAction->targetObj, 0);
    ASSERT_OR_RETURN(, (psActionTarget[0] != nullptr) && (psActionTarget[0]->type == OBJ_STRUCTURE),
                     "invalid target for repair order");
    order->psStats = ((Structure *)psActionTarget[0])->stats;
    if (secHoldActive && (order->type == DORDER_NONE || order->type == DORDER_HOLD))
    {
      action = DROID_ACTION::REPAIR;
    }
    else if ((!secHoldActive && order->type != DORDER_HOLD) || (secHoldActive && order->type == DORDER_REPAIR))
    {
      action = DROID_ACTION::MOVETOREPAIR;
      moveDroidTo(psDroid, psAction->x, psAction->y);
    }
    break;
  case DROID_ACTION::OBSERVE:
    action = psAction->action;
    setDroidActionTarget(psDroid, psAction->targetObj, 0);
    actionPos.x = position.x;
    actionPos.y = position.y;
    if (secondaryGetState(psDroid, DSO_HALTTYPE) != DSS_HALT_GUARD && (order->type == DORDER_NONE || order->type == DORDER_HOLD))
    {
      action = visibleObject(psDroid, psActionTarget[0], false) ? DROID_ACTION::OBSERVE : DROID_ACTION::NONE;
    }
    else if (!cbSensorDroid(psDroid) && ((!secHoldActive && order->type != DORDER_HOLD) || (secHoldActive && order->type == DORDER_OBSERVE)))
    {
      action = DROID_ACTION::MOVETOOBSERVE;
      moveDroidTo(psDroid, psActionTarget[0]->position.x, psActionTarget[0]->position.y);
    }
    break;
  case DROID_ACTION::FIRESUPPORT:
    action = DROID_ACTION::FIRESUPPORT;
    if (!isVtolDroid(psDroid) && !secHoldActive && order->psObj->type != OBJ_STRUCTURE)
    {
      moveDroidTo(psDroid, order->psObj->position.x, order->psObj->position.y);		// movetotarget.
    }
    break;
  case DROID_ACTION::SULK:
    action = DROID_ACTION::SULK;
    // hmmm, hope this doesn't cause any problems!
    actionStarted = gameTime + MIN_SULK_TIME + (gameRand(MAX_SULK_TIME - MIN_SULK_TIME));
    break;
  case DROID_ACTION::WAITFORREPAIR:
    action = DROID_ACTION::WAITFORREPAIR;
    // set the time so we can tell whether the start the self repair or not
    actionStarted = gameTime;
    break;
  case DROID_ACTION::MOVETOREPAIRPOINT:
    action = psAction->action;
    actionPos.x = psAction->x;
    actionPos.y = psAction->y;
    actionStarted = gameTime;
    setDroidActionTarget(psDroid, psAction->targetObj, 0);
    moveDroidToNoFormation(psDroid, psAction->x, psAction->y);
    break;
  case DROID_ACTION::WAITDURINGREPAIR:
    action = DROID_ACTION::WAITDURINGREPAIR;
    break;
  case DROID_ACTION::MOVETOREARMPOINT:
    objTrace(id, "set to move to rearm pad");
    action = psAction->action;
    actionPos.x = psAction->x;
    actionPos.y = psAction->y;
    actionStarted = gameTime;
    setDroidActionTarget(psDroid, psAction->targetObj, 0);
    moveDroidToDirect(psDroid, psAction->x, psAction->y);

    // make sure there aren't any other VTOLs on the rearm pad
    ensureRearmPadClear((Structure *)psAction->targetObj, psDroid);
    break;
  case DROID_ACTION::DROIDREPAIR:
  {
    action = psAction->action;
    actionPos.x = psAction->x;
    actionPos.y = psAction->y;
    setDroidActionTarget(psDroid, psAction->targetObj, 0);
    //initialise the action points
    actionPoints  = 0;
    actionStarted = gameTime;
    const auto xdiff = (SDWORD)position.x - (SDWORD) psAction->x;
    const auto ydiff = (SDWORD)position.y - (SDWORD) psAction->y;
    if (secHoldActive && (order->type == DORDER_NONE || order->type == DORDER_HOLD))
    {
      action = DROID_ACTION::DROIDREPAIR;
    }
    else if (((!secHoldActive && order->type != DORDER_HOLD) || (secHoldActive && order->type == DORDER_DROIDREPAIR))
             // check that we actually need to move closer
             && ((xdiff * xdiff + ydiff * ydiff) > REPAIR_RANGE * REPAIR_RANGE))
    {
      action = DROID_ACTION::MOVETODROIDREPAIR;
      moveDroidTo(psDroid, psAction->x, psAction->y);
    }
    break;
  }
  case DROID_ACTION::RESTORE:
    ASSERT_OR_RETURN(, order->type == DORDER_RESTORE, "cannot start restore action without a restore order");
    action = psAction->action;
    actionPos.x = psAction->x;
    actionPos.y = psAction->y;
    ASSERT_OR_RETURN(, (order->psObj != nullptr) && (order->psObj->type == OBJ_STRUCTURE), "invalid target for restore order");
    order->psStats = ((Structure *)order->psObj)->stats;
    setDroidActionTarget(psDroid, psAction->targetObj, 0);
    if (order->type != DORDER_HOLD)
    {
      action = DROID_ACTION::MOVETORESTORE;
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