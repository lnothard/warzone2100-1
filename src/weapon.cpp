//
// Created by Luna Nothard on 06/01/2022.
//

#include "lib/gamelib/gtime.h"

#include "weapondef.h"

bool Weapon::has_full_ammo() const noexcept
{
  return ammo_used == 0;
}

bool Weapon::is_artillery() const noexcept
{
  return stats->movementModel == MOVEMENT_MODEL::INDIRECT ||
         stats->movementModel == MOVEMENT_MODEL::HOMING_INDIRECT;
}

bool Weapon::is_vtol_weapon() const
{
  return stats->vtolAttackRuns;
}

bool Weapon::is_empty_vtol_weapon(unsigned player) const
{
  if (!is_vtol_weapon()) return false;

  return ammo_used >= get_num_attack_runs(player);
}

const WeaponStats& Weapon::get_stats() const
{
  return *stats;
}

unsigned Weapon::get_recoil() const
{
  if (graphicsTime >= time_last_fired && graphicsTime < time_last_fired + DEFAULT_RECOIL_TIME)
  {
    const auto recoil_time = static_cast<int>(graphicsTime - time_last_fired);
    const auto recoil_amount = DEFAULT_RECOIL_TIME / 2 - abs(
            recoil_time - static_cast<int>(DEFAULT_RECOIL_TIME) / 2);
    const auto max_recoil = stats->recoilValue;
    return max_recoil * recoil_amount / (DEFAULT_RECOIL_TIME / 2 * 10);
  }
  return 0;
}

unsigned Weapon::get_max_range(unsigned player) const
{
  return stats->upgraded[player].maxRange;
}

unsigned Weapon::get_min_range(unsigned player) const
{
  return stats->upgraded[player].minRange;
}

unsigned Weapon::get_short_range(unsigned player) const
{
  return stats->upgraded[player].shortRange;
}

unsigned Weapon::get_hit_chance(unsigned unsigned player) const
{
  return stats->upgraded[player].hitChance;
}

unsigned Weapon::get_short_range_hit_chance(unsigned unsigned player) const
{
  return stats->upgraded[player].shortHitChance;
}

WEAPON_SUBCLASS Weapon::get_subclass() const
{
  return stats->weaponSubClass;
}

unsigned Weapon::get_num_attack_runs(unsigned player) const
{
  const auto u_stats = stats->upgraded[player];

  if (u_stats.reloadTime > 0)
    return u_stats.numRounds * stats->vtolAttackRuns;

  return stats->vtolAttackRuns;
}

unsigned Weapon::get_shots_fired() const noexcept
{
  return shots_fired;
}

const iIMDShape& Weapon::get_IMD_shape() const
{
  return *stats->pIMD;
}

const iIMDShape& Weapon::get_mount_graphic() const
{
  return *stats->pMountGraphic;
}

unsigned Weapon::calculate_rate_of_fire(unsigned player) const
{
  const auto& w_stats = stats->upgraded[player];
  return w_stats.numRounds
         * 60 * GAME_TICKS_PER_SEC / w_stats.reloadTime;
}
