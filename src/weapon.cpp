//
// Created by Luna Nothard on 06/01/2022.
//

#include "lib/framework/math_ext.h"
#include "weapon.h"


Weapon::Weapon(unsigned id, Player* player)
  : BaseObject(id, player)
{
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
  if (!isVtolWeapon()) return false;
  return ammoUsed >= getNumAttackRuns(player);
}

unsigned Weapon::getRecoil() const
{
  if (graphicsTime < timeLastFired ||
      graphicsTime >= timeLastFired + DEFAULT_RECOIL_TIME) {
    return 0;
  }
  const auto recoil_time = static_cast<int>(graphicsTime - timeLastFired);
  const auto recoil_amount = DEFAULT_RECOIL_TIME / 2 - abs(
          recoil_time - static_cast<int>(DEFAULT_RECOIL_TIME) / 2);

  const auto max_recoil = stats->recoilValue;
  return max_recoil * recoil_amount / (DEFAULT_RECOIL_TIME / 2 * 10);
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

unsigned Weapon::getNumAttackRuns(unsigned player) const
{
  const auto u_stats = stats->upgraded[player];
  if (u_stats.reloadTime > 0) {
    return u_stats.numRounds * stats->vtolAttackRuns;
  }
  return stats->vtolAttackRuns;
}

const iIMDShape* Weapon::getImdShape() const
{
  return stats->pIMD.get();
}

const iIMDShape* Weapon::getMountGraphic() const
{
  return stats->pMountGraphic.get();
}

unsigned Weapon::calculateRateOfFire(unsigned player) const
{
  const auto& w_stats = stats->upgraded[player];
  return w_stats.numRounds * 60 * GAME_TICKS_PER_SEC / w_stats.reloadTime;
}

void Weapon::alignTurret()
{
  auto const turret_rotation = gameTimeAdjustedIncrement(
          DEG(TURRET_ROTATION_RATE));

  auto weapon_rotation = getRotation().direction;
  auto weapon_pitch = getRotation().pitch;
  auto const nearest_right_angle = (weapon_rotation +
          DEG(45)) / DEG(90) * DEG(90);

  weapon_rotation += clip(angleDelta(nearest_right_angle - weapon_rotation),
                          -turret_rotation / 2, turret_rotation / 2);

  weapon_pitch += clip(angleDelta(0 - weapon_pitch),
                       -turret_rotation / 2, turret_rotation / 2);

  setRotation({weapon_rotation,
                    weapon_pitch,
                    getRotation().roll});
}
