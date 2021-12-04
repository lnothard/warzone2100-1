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
  if (vtolRearming(this))
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
            if ((order.type == DORDER_PATROL || order.type == DORDER_CIRCLE) && (!vtolEmpty(this) || (secondaryOrder & DSS_ALEV_MASK) == DSS_ALEV_NEVER))
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

  if (orderDroidList(this))
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
  initDroidMovement(this);
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
  if (numWeapons > 0 && m_weaponList[0].nStat > 0 && vtolEmpty(this))
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
  if (isVtolDroid(this))
  {
    if (numWeapons > 0)
    {
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
  if (!isVtolDroid(psDroid))
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
      secondarySetState(psDroid, DSO_ATTACK_LEVEL, DSS_ALEV_ALWAYS);
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
    droidBodyUpgrade(psDroid);
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
  aiUpdateDroid(psDroid);

  // Update the droids order.
  orderUpdateDroid(psDroid);

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
      droidDamage(psDroid, BURN_DAMAGE, WC_HEAT, WSC_FLAME, gameTime - deltaGameTime / 2 + 1, true, BURN_MIN_DAMAGE);
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
      cancelBuild(psDroid);
      objTrace(id, "DroidStartBuildFailed: not researched");
      return DroidStartBuildFailed;
    }

    //need to check structLimits have not been exceeded
    if (psStructStat->curCount[owningPlayer] >= psStructStat->upgrade[owningPlayer].limit)
    {
      cancelBuild(psDroid);
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
      cancelBuild(psDroid);
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