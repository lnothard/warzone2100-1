//
// Created by luna on 08/12/2021.
//

#ifndef WARZONE2100_STATS_H
#define WARZONE2100_STATS_H

#include <cstdint>
#include <memory>

#include "lib/framework/frame.h"
#include "weapon.h"

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

enum COMPONENT_TYPE
{
  BODY,
  BRAIN,
  PROPULSION,
  REPAIR_UNIT,
  ECM,
  SENSOR,
  CONSTRUCT,
  WEAPON,
  COUNT // MUST BE LAST
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

  COMPONENT_TYPE type;
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
  using enum PROPULSION_TYPE;

  PROPULSION_TYPE propulsion_type;
  uint32_t max_speed { 0 };
  uint32_t turn_speed { 0 };
  uint32_t spin_speed { 0 };
  uint32_t spin_angle { 0 };
  uint32_t skid_deceleration { 0 };
  uint32_t deceleration { 0 };
  uint32_t acceleration { 0 };

  struct : Upgradeable
  {
    int hitpoint_percent_increase { 0 };
  } upgraded[MAX_PLAYERS], base;
};

struct Commander_Stats : public Component_Stats
{
  std::unique_ptr<Weapon_Stats> weapon_stats;

  struct : Upgradeable
  {
    std::vector<int> rank_thresholds;
    int max_droids_assigned { 0 };
  } upgraded[MAX_PLAYERS], base;
};

struct Body_Stats : public Component_Stats
{
  struct : Upgradeable
  {
    unsigned power_output;
    unsigned armour;
    int thermal;
    int resistance;
  } upgraded[MAX_PLAYERS], base;
};

struct ECM_Stats : public Component_Stats
{
  struct : Upgradeable
  {
    uint32_t range { 0 };
  } upgraded[MAX_PLAYERS], base;
};

extern Propulsion_Stats* global_propulsion_stats;

#endif // WARZONE2100_STATS_H