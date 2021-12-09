//
// Created by luna on 09/12/2021.
//

#ifndef WARZONE2100_FEATURE_H
#define WARZONE2100_FEATURE_H

#include "basedef.h"

enum class FEATURE_TYPE
{
  TANK,
  OIL_RESOURCE,
  BOULDER,
  VEHICLE,
  BUILDING,
  OIL_DRUM,
  TREE,
  SKYSCRAPER
};

class Feature : public Simple_Object
{
private:
  using enum FEATURE_TYPE;

  FEATURE_TYPE type;
  bool         is_damageable;
  uint16_t     base_width;
  uint16_t     base_breadth;
  uint32_t     hit_points;
  uint32_t     armour_points;
};

#endif // WARZONE2100_FEATURE_H