//
// Created by luna on 08/12/2021.
//

#include <algorithm>

#include "unit.h"

namespace Impl
{
  bool Unit::hp_below_x(uint32_t x) const
  {
    return hit_points < x;
  }

  bool Unit::has_full_ammo() const
  {
    return std::all_of(weapons.begin(), weapons.end(),
                       [](const auto& w){ return w.has_full_ammo(); });
  }
}