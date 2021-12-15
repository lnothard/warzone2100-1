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

bool Weapon::is_empty_VTOL_weapon(uint32_t player) const
{
  if (!is_VTOL_weapon()) return false;

  return ammo_used >= get_num_attack_runs(player);
}

uint32_t Weapon::get_recoil() const
{
  if (graphicsTime >= time_last_fired && graphicsTime < time_last_fired + DEFAULT_RECOIL_TIME)
  {
    int recoil_time { static_cast<int>(graphicsTime - time_last_fired) };
    uint32_t recoil_amount { DEFAULT_RECOIL_TIME / 2 - abs(recoil_time - static_cast<int>(DEFAULT_RECOIL_TIME) / 2) };
    uint32_t max_recoil { stats.recoil_value };
    return max_recoil * recoil_amount / (DEFAULT_RECOIL_TIME / 2 * 10);
  }
  return 0;
}

uint32_t Weapon::get_max_range(uint32_t player) const
{
  return stats.upgraded_stats[player].max_range;
}

uint32_t Weapon::get_min_range(uint32_t player) const
{
  return stats.upgraded_stats[player].min_range;
}

WEAPON_SUBCLASS Weapon::get_subclass() const
{
  return stats.subclass;
}

uint32_t Weapon::get_num_attack_runs(uint32_t player) const
{
  auto u_stats = stats.upgraded_stats[player];

  if (u_stats.reload_time > 0)
    return u_stats.rounds_per_salvo * stats.max_VTOL_attack_runs;

  return stats.max_VTOL_attack_runs;
}