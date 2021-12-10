//
// Created by luna on 08/12/2021.
//

#ifndef WARZONE2100_STATS_H
#define WARZONE2100_STATS_H

#include <cstdint>

#include "lib/framework/frame.h"

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

enum class SENSOR_TYPE
{
  STANDARD,
  INDIRECT_CB,
  VTOL_CB,
  VTOL_INTERCEPT,
  SUPER,
  RADAR_DETECTOR
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

struct Sensor_Stats : public Component_Stats
{
  using enum SENSOR_TYPE;

  SENSOR_TYPE type = STANDARD;

  struct : Upgradeable
  {
    uint32_t range;
  } upgraded[MAX_PLAYERS], base;
};

struct Propulsion_Stats : public Component_Stats
{
  uint32_t max_speed;
};

struct Brain_Stats : public Component_Stats
{

};

#endif // WARZONE2100_STATS_H