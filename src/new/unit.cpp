//
// Created by luna on 08/12/2021.
//

#include "unit.h"

namespace Impl
{
  bool Unit::hp_below_x(uint32_t x) const
  {
    return hit_points < x;
  }
}