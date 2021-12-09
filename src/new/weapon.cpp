//
// Created by luna on 09/12/2021.
//

#include <stdlib.h>

#include "weapon.h"

bool Weapon::has_full_ammo() const
{
  return ammo_used == 0;
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

uint32_t Weapon::get_max_range(uint8_t player) const
{
  return stats.upgraded_stats[player].max_range;
}

uint32_t Weapon::get_min_range(uint8_t player) const
{
  return stats.upgraded_stats[player].min_range;
}