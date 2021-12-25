//
// Created by Luna Nothard on 23/12/2021.
//

#ifndef WARZONE2100_POWER_H
#define WARZONE2100_POWER_H

#include <array>
#include <vector>
#include "lib/framework/frame.h"
#include "structure.h"

static constexpr auto MAX_POWER = 1'000'000;

struct Power_Request
{
    Power_Request(int amount, unsigned id) :
    amount{amount}, requester_id{id}
    {
    }

    int amount;
    unsigned requester_id;
};

struct Player_Power
{
    std::vector<Power_Request> queue;
    int current = 0;
    int modifier = 100;
    int max_store = MAX_POWER;
    int total_extracted = 0;
    int wasted = 0;
    int amount_generated_last_update = 0;
};
std::array<Player_Power, MAX_PLAYERS> power_list;

// return true iff requested power is available
bool add_power_request(unsigned player, unsigned requester_id, int amount);
void remove_power_request(const Structure& structure);

inline void use_power(unsigned player, int amount)
{
  power_list[player].current = MAX(0, power_list[player].current - amount);
}

inline void add_power(unsigned player, int amount)
{
  power_list[player].current += amount;
  if (power_list[player].current > power_list[player].max_store)
  {
    power_list[player].wasted += power_list[player].current - power_list[player].max_store;
    power_list[player].current = power_list[player].max_store;
  }
}

inline void set_power_modifier(unsigned player, int modifier)
{
  power_list[player].modifier = modifier;
}

#endif //WARZONE2100_POWER_H
