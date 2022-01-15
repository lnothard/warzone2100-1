//
// Created by Luna Nothard on 06/01/2022.
//

#include "lib/gamelib/gtime.h"

#include "weapon.h"

bool Weapon::hasAmmo() const
{
  return ammo > 0;
}

bool Weapon::hasFullAmmo() const noexcept
{
  return ammoUsed == 0;
}

bool Weapon::isArtillery() const noexcept
{
  return stats->movementModel == MOVEMENT_MODEL::INDIRECT ||
         stats->movementModel == MOVEMENT_MODEL::HOMING_INDIRECT;
}

bool Weapon::isVtolWeapon() const
{
  return stats->vtolAttackRuns;
}

bool Weapon::isEmptyVtolWeapon(unsigned player) const
{
  if (!isVtolWeapon())  {
    return false;
  }
  return ammoUsed >= getNumAttackRuns(player);
}

const WeaponStats& Weapon::getStats() const
{
  return *stats;
}

TARGET_ORIGIN Weapon::getTargetOrigin() const
{
  return origin;
}

unsigned Weapon::getRecoil() const
{
  if (graphicsTime >= timeLastFired &&
      graphicsTime < timeLastFired + DEFAULT_RECOIL_TIME) {

    const auto recoil_time = static_cast<int>(graphicsTime - timeLastFired);
    const auto recoil_amount = DEFAULT_RECOIL_TIME / 2 - abs(
            recoil_time - static_cast<int>(DEFAULT_RECOIL_TIME) / 2);
    const auto max_recoil = stats->recoilValue;
    return max_recoil * recoil_amount / (DEFAULT_RECOIL_TIME / 2 * 10);
  }
  return 0;
}

unsigned Weapon::getMaxRange(unsigned player) const
{
  return stats->upgraded[player].maxRange;
}

unsigned Weapon::getMinRange(unsigned player) const
{
  return stats->upgraded[player].minRange;
}

unsigned Weapon::getShortRange(unsigned player) const
{
  return stats->upgraded[player].shortRange;
}

unsigned Weapon::getHitChance(unsigned player) const
{
  return stats->upgraded[player].hitChance;
}

unsigned Weapon::getShortRangeHitChance(unsigned player) const
{
  return stats->upgraded[player].shortHitChance;
}

WEAPON_SUBCLASS Weapon::getSubclass() const
{
  return stats->weaponSubClass;
}

unsigned Weapon::getNumAttackRuns(unsigned player) const
{
  const auto u_stats = stats->upgraded[player];

  if (u_stats.reloadTime > 0)
    return u_stats.numRounds * stats->vtolAttackRuns;

  return stats->vtolAttackRuns;
}

unsigned Weapon::getShotsFired() const noexcept
{
  return shotsFired;
}

const iIMDShape& Weapon::getImdShape() const
{
  return *stats->pIMD;
}

const iIMDShape& Weapon::getMountGraphic() const
{
  return *stats->pMountGraphic;
}

unsigned Weapon::calculateRateOfFire(unsigned player) const
{
  const auto& w_stats = stats->upgraded[player];
  return w_stats.numRounds
         * 60 * GAME_TICKS_PER_SEC / w_stats.reloadTime;
}
