//
// Created by luna on 09/12/2021.
//

#ifndef WARZONE2100_VISIBILITY_H
#define WARZONE2100_VISIBILITY_H

#include "basedef.h"

inline bool objects_in_vis_range(const Simple_Object& first, const Simple_Object& second, int range)
{
  auto x_diff = first.get_position().x - second.get_position().x;
  auto y_diff = first.get_position().y - second.get_position().y;

  return abs(x_diff) <= range && x_diff * x_diff + y_diff * y_diff <= range;
}

#endif // WARZONE2100_VISIBILITY_H