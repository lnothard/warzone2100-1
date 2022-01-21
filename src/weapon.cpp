//
// Created by Luna Nothard on 06/01/2022.
//

#include "lib/framework/math_ext.h"
#include "lib/gamelib/gtime.h"

#include "constructedobject.h"
#include "stats.h"
#include "weapon.h"

struct Weapon::Impl
{
  ~Impl() = default;
  Impl() = default;

  Impl(Impl const& rhs) = default;
  Impl& operator=(Impl const& rhs) = default;

  Impl(Impl&& rhs) = default;
  Impl& operator=(Impl&& rhs) = default;


  using enum TARGET_ORIGIN;
  std::shared_ptr<WeaponStats> stats;
  Rotation previousRotation {0, 0, 0};
  TARGET_ORIGIN origin = UNKNOWN;
  unsigned timeLastFired = 0;
  unsigned ammo = 0;
  unsigned ammoUsed = 0;
  unsigned shotsFired = 0;
};

Weapon::Weapon(unsigned id, unsigned player)
  : PlayerOwnedObject(id, player),
    pimpl{std::make_unique<Impl>()}
{
}

Weapon::Weapon(Weapon const& rhs)
  : PlayerOwnedObject(rhs),
    pimpl{std::make_unique<Impl>(*rhs.pimpl)}
{
}

Weapon& Weapon::operator=(Weapon const& rhs)
{
  if (this == &rhs) return *this;
  *pimpl = *rhs.pimpl;
  return *this;
}

bool Weapon::hasAmmo() const
{
  return pimpl && pimpl->ammo > 0;
}

bool Weapon::hasFullAmmo() const noexcept
{
  return !pimpl || pimpl->ammoUsed == 0;
}

bool Weapon::isArtillery() const noexcept
{
  using enum MOVEMENT_MODEL;

  return pimpl &&
         (pimpl->stats->movementModel == INDIRECT ||
          pimpl->stats->movementModel == HOMING_INDIRECT);
}

bool Weapon::isVtolWeapon() const
{
  return pimpl && pimpl->stats->vtolAttackRuns;
}

bool Weapon::isEmptyVtolWeapon(unsigned player) const
{
  if (!isVtolWeapon())  {
    return false;
  }
  return pimpl && pimpl->ammoUsed >= getNumAttackRuns(player);
}

const WeaponStats* Weapon::getStats() const
{
  return pimpl
         ? pimpl->stats.get()
         : nullptr;
}

TARGET_ORIGIN Weapon::getTargetOrigin() const noexcept
{
  return pimpl
         ? pimpl->origin
         : TARGET_ORIGIN::UNKNOWN;
}

unsigned Weapon::getRecoil() const
{
  if (!pimpl) return 0;

  if (graphicsTime >= pimpl->timeLastFired &&
      graphicsTime < pimpl->timeLastFired + DEFAULT_RECOIL_TIME) {

    const auto recoil_time = static_cast<int>(graphicsTime - pimpl->timeLastFired);
    const auto recoil_amount = DEFAULT_RECOIL_TIME / 2 - abs(
            recoil_time - static_cast<int>(DEFAULT_RECOIL_TIME) / 2);
    const auto max_recoil = pimpl->stats->recoilValue;
    return max_recoil * recoil_amount / (DEFAULT_RECOIL_TIME / 2 * 10);
  }
  return 0;
}

unsigned Weapon::getMaxRange(unsigned player) const
{
  return pimpl
         ? pimpl->stats->upgraded[player].maxRange
         : 0;
}

unsigned Weapon::getMinRange(unsigned player) const
{
  return pimpl
         ? pimpl->stats->upgraded[player].minRange
         : 0;
}

unsigned Weapon::getShortRange(unsigned player) const
{
  return pimpl
         ? pimpl->stats->upgraded[player].shortRange
         : 0;
}

unsigned Weapon::getHitChance(unsigned player) const
{
  return pimpl
         ? pimpl->stats->upgraded[player].hitChance
         : 0;
}

unsigned Weapon::getShortRangeHitChance(unsigned player) const
{
  return pimpl
         ? pimpl->stats->upgraded[player].shortHitChance
         : 0;
}

WEAPON_SUBCLASS Weapon::getSubclass() const
{
  return pimpl
         ? pimpl->stats->weaponSubClass
         : WEAPON_SUBCLASS::COUNT;
}

unsigned Weapon::getNumAttackRuns(unsigned player) const
{
  if (!pimpl) return 0;

  const auto u_stats = pimpl->stats->upgraded[player];

  if (u_stats.reloadTime > 0) {
    return u_stats.numRounds * pimpl->stats->vtolAttackRuns;
  }

  return pimpl->stats->vtolAttackRuns;
}

unsigned Weapon::getShotsFired() const noexcept
{
  return pimpl
         ? pimpl->shotsFired
         : 0;
}

const iIMDShape* Weapon::getImdShape() const
{
  return pimpl
         ? pimpl->stats->pIMD.get()
         : nullptr;
}

const iIMDShape* Weapon::getMountGraphic() const
{
  return pimpl
         ? pimpl->stats->pMountGraphic.get()
         : nullptr;
}

unsigned Weapon::calculateRateOfFire(unsigned player) const
{
  if (!pimpl) return 0;
  const auto& w_stats = pimpl->stats->upgraded[player];

  return w_stats.numRounds
         * 60 * GAME_TICKS_PER_SEC / w_stats.reloadTime;
}

Rotation Weapon::getPreviousRotation() const {
  return pimpl
         ? pimpl->previousRotation
         : Rotation();
}

void Weapon::useAmmo() {
  if (!pimpl) return;
  ++pimpl->ammoUsed;
}

void Weapon::alignTurret()
{
  const auto turret_rotation = gameTimeAdjustedIncrement(
          DEG(TURRET_ROTATION_RATE));

  auto weapon_rotation = getRotation().direction;
  auto weapon_pitch = getRotation().pitch;

  const auto nearest_right_angle = (weapon_rotation +
          DEG(45)) / DEG(90) * DEG(90);

  weapon_rotation += clip(angleDelta(nearest_right_angle - weapon_rotation),
                          -turret_rotation / 2, turret_rotation / 2);

  weapon_pitch += clip(angleDelta(0 - weapon_pitch),
                       -turret_rotation / 2, turret_rotation / 2);

  setRotation({weapon_rotation,
                           weapon_pitch,
                           getRotation().roll});
}
