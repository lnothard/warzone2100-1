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

class Feature : public virtual ::Simple_Object, public Impl::Simple_Object
{
public:
  int calculate_height() const final;
private:
  using enum FEATURE_TYPE;

  FEATURE_TYPE type;
  bool is_damageable;
  unsigned base_width;
  unsigned base_breadth;
  unsigned hit_points;
  unsigned armour_points;
};

#endif // WARZONE2100_FEATURE_H