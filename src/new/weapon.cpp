//
// Created by luna on 09/12/2021.
//

#include <stdlib.h>

#include "weapon.h"

bool Weapon::has_full_ammo() const
{
  return ammo_used == 0;
}

bool Weapon::is_artillery() const
{
  return stats.movement_type == MOVEMENT_TYPE::INDIRECT ||
         stats.movement_type == MOVEMENT_TYPE::HOMING_INDIRECT;
}

bool Weapon::is_VTOL_weapon() const
{
  return stats.max_VTOL_attack_runs;
}

bool Weapon::is_empty_VTOL_weapon(const unsigned player) const
{
  if (!is_VTOL_weapon()) return false;

  return ammo_used >= get_num_attack_runs(player);
}

const Weapon_Stats& Weapon::get_stats() const
{
  return stats;
}

unsigned Weapon::get_recoil() const
{
  if (graphicsTime >= time_last_fired && graphicsTime < time_last_fired + DEFAULT_RECOIL_TIME)
  {
    const auto recoil_time = static_cast<int>(graphicsTime - time_last_fired);
    const auto recoil_amount = DEFAULT_RECOIL_TIME / 2 - abs(recoil_time - static_cast<int>(DEFAULT_RECOIL_TIME) / 2);
    const auto max_recoil = stats.recoil_value;
    return max_recoil * recoil_amount / (DEFAULT_RECOIL_TIME / 2 * 10);
  }
  return 0;
}

unsigned Weapon::get_max_range(const unsigned player) const
{
  return stats.upgraded_stats[player].max_range;
}

unsigned Weapon::get_min_range(const unsigned player) const
{
  return stats.upgraded_stats[player].min_range;
}

WEAPON_SUBCLASS Weapon::get_subclass() const
{
  return stats.subclass;
}

unsigned Weapon::get_num_attack_runs(const unsigned player) const
{
  auto u_stats = stats.upgraded_stats[player];

  if (u_stats.reload_time > 0)
    return u_stats.rounds_per_salvo * stats.max_VTOL_attack_runs;

  return stats.max_VTOL_attack_runs;
}

unsigned Weapon::get_shots_fired() const
{
  return shots_fired;
}

const iIMDShape& Weapon::get_IMD_shape() const
{
  return *stats.weapon_graphic;
}

const iIMDShape& Weapon::get_mount_graphic() const
{
  return *stats.mount_graphic;
}