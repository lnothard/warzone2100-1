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

#define	VTOL_ATTACK_LENGTH 1000

static inline DroidStats *castDroidTemplate(StatsObject *stats)
{
  return stats != nullptr && stats->hasType(STAT_TEMPLATE) ? dynamic_cast<DroidStats *>(stats) : nullptr;
}

static inline DroidStats const *castDroidTemplate(StatsObject const *stats)
{
  return stats != nullptr && stats->hasType(STAT_TEMPLATE) ? dynamic_cast<DroidStats const *>(stats) : nullptr;
}

bool Droid::aiObjHasRange(const GameObject& targetObj, int weapon_slot)
{
  int32_t longRange = aiDroidRange(psDroid, weapon_slot);

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