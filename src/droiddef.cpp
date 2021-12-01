//
// Created by luna on 01/12/2021.
//

#include "droiddef.h"
#include "projectile.h"
#include "stats.h"

static inline DroidStats *castDroidTemplate(StatsObject *stats)
{
  return stats != nullptr && stats->hasType(STAT_TEMPLATE) ? dynamic_cast<DroidStats *>(stats) : nullptr;
}

static inline DroidStats const *castDroidTemplate(StatsObject const *stats)
{
  return stats != nullptr && stats->hasType(STAT_TEMPLATE) ? dynamic_cast<DroidStats const *>(stats) : nullptr;
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

  const int dx = (SDWORD)spacetime()->position().x - (SDWORD)targetObj.spacetime()->position().x;
  const int dy = (SDWORD)spacetime()->position().y - (SDWORD)targetObj.spacetime()->position().y;

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