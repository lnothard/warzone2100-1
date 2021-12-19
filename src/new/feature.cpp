//
// Created by Luna Nothard on 17/12/2021.
//

#include "feature.h"

int Feature::calculate_height() const
{
  const auto& imd = get_display_data().imd_shape;
  return imd->max.y + imd->min.y;
}