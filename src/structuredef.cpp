//
// Created by luna on 02/12/2021.
//

#include "structuredef.h"
#include "projectile.h"
#include "stats.h"

StructureStats::StructureStats() : pBaseIMD(nullptr), pECM(nullptr), pSensor(nullptr)
{
  memset(curCount, 0, sizeof(curCount));
  memset(upgrade, 0, sizeof(upgrade));
}

Vector2i StructureStats::size(uint16_t direction) const
{
  Vector2i size(baseWidth, baseBreadth);
  if ((snapDirection(direction) & 0x4000) != 0) // if building is rotated left or right by 90°, swap width and height
  {
    std::swap(size.x, size.y);
  }
  return size;
}

Vector2i Structure::size() const { return stats->size(m_rotation.direction); }

// see if a structure has the range to fire on a target
bool Structure::aiUnitHasRange(const GameObject& targetObj, int weapon_slot)
{
  if (numWeapons == 0 || m_weaponList[0].nStat == 0)
  {
    // Can't attack without a weapon
    return false;
  }

  WEAPON_STATS *psWStats = m_weaponList[weapon_slot].nStat + asWeaponStats;

  int longRange = proj_GetLongRange(psWStats, owningPlayer);
  return objPosDiffSq(targetObj) < longRange * longRange && lineOfFire(this, targetObj, weapon_slot, true);
}