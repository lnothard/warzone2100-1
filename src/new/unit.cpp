//
// Created by luna on 08/12/2021.
//

#include <algorithm>

#include "unit.h"

namespace Impl
{
  uint32_t Unit::get_hp() const
  {
    return hit_points;
  }

  bool Unit::has_full_ammo() const
  {
    return std::all_of(weapons.begin(), weapons.end(),
                       [](const auto& w){ return w.has_full_ammo(); });
  }
}