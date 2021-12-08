//
// Created by luna on 08/12/2021.
//

#ifndef WARZONE2100_STATS_H
#define WARZONE2100_STATS_H

#include <cstdint>

class Component_Stats
{
private:
  uint32_t power_to_build;
  uint32_t weight;
  bool     is_designable;
};

class Propulsion_Stats : public Component_Stats
{
private:
  uint32_t max_speed;
};

#endif // WARZONE2100_STATS_H