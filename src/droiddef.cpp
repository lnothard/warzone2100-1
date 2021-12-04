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
      if (droidStartBuild(psDroid))
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
    secondarySetState(psDroid, DSO_CIRCLE, DSS_NONE);
  }
  else if (secondaryGetState(psDroid, DSO_PATROL, ModeQueue) == DSS_PATROL_SET)  // ModeQueue here means to check whether we pressed the patrol button, whether or not it synched yet. The reason for this weirdness is that a patrol order makes no sense as a secondary state in the first place (the patrol button _should_ have been only in the UI, not in the game state..!), so anything dealing with patrol orders will necessarily be weird.
  {
    order = DORDER_PATROL;
    secondarySetState(psDroid, DSO_PATROL, DSS_NONE);
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

    orderDroidBase(psDroid, &sOrder);
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
    orderDroidList(psDroid);
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
  orderDroidBase(psDroid, &sOrder);
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
  orderCheckList(psDroid);

  if (isDead(psDroid))
  {
    return;
  }

  switch (order.type)
  {
  case DORDER_NONE:
  case DORDER_HOLD:
    // see if there are any orders queued up
    if (orderDroidList(psDroid))
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
      orderDroidLoc(psDroid, DORDER_GUARD, position.x, position.y, ModeImmediate);
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
          orderDroid(psDroid, DORDER_TRANSPORTIN, ModeImmediate);
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

      DeSelectDroid(psDroid);

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
          tryDoRepairlikeAction(psDroid);
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
          if (orderDroidList(psDroid))
          {
            // started a new order, quit
            break;
          }
          if (isVtolDroid(psDroid) && !vtolFull(psDroid) && (secondaryOrder & DSS_ALEV_MASK) != DSS_ALEV_NEVER)
          {
            moveToRearm(psDroid);
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
      moveToRearm(psDroid);  // Completely empty (and we're not set to hold fire), don't bother patrolling.
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
        if (orderDroidList(psDroid))
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

      if (isVtolDroid(psDroid) && vtolEmpty(psDroid) && (secondaryOrder & DSS_ALEV_MASK) != DSS_ALEV_NEVER)
      {
        moveToRearm(psDroid);  // Completely empty (and we're not set to hold fire), don't bother circling.
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
      moveToRearm(psDroid);
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
        if (!orderDroidList(psDroid))
        {
          order = DroidOrder(DORDER_NONE);
          moveToRearm(psDroid);
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