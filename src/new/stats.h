//
// Created by luna on 08/12/2021.
//

#ifndef WARZONE2100_STATS_H
#define WARZONE2100_STATS_H

#include <cstdint>

enum class PROPULSION_TYPE
{
  WHEELED,
  TRACKED,
  LEGGED,
  HOVER,
  LIFT,
  PROPELLER,
  HALF_TRACKED
};

struct Component_Stats
{
  struct Upgradeable
  {
    uint32_t hit_points;
    uint32_t hit_point_percent;
  };

  uint32_t power_to_build;
  uint32_t weight;
  bool     is_designable;
};

struct Propulsion_Stats : public Component_Stats
{
  uint32_t max_speed;
};

struct Brain_Stats : public Component_Stats
{

};

#endif // WARZONE2100_STATS_H