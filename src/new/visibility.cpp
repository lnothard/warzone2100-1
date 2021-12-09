//
// Created by luna on 09/12/2021.
//

#include <stdlib.h>

#include "visibility.h"

static inline bool objects_in_vis_range(const Simple_Object& first, const Simple_Object& second, int32_t range)
{
  int32_t x_diff { first.get_position().x - second.get_position().x };
  int32_t y_diff { first.get_position().y - second.get_position().y };

  return abs(x_diff) <= range && x_diff * x_diff + y_diff * y_diff <= range;
}