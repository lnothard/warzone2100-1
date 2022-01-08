//
// Created by Luna Nothard on 23/12/2021.
//

#ifndef WARZONE2100_POWER_H
#define WARZONE2100_POWER_H

#include <array>
#include <vector>
#include "lib/framework/frame.h"
#include "structure.h"

/// The limit on a player's stored power
static constexpr auto MAX_POWER = 1'000'000;

static constexpr auto EXTRACT_POINTS = 1;

struct PowerRequest
{
    PowerRequest() = default;
    PowerRequest(int amount, unsigned id);

    int amount = 0;
    unsigned requester_id = 0;
};

struct PlayerPower
{
    std::vector<PowerRequest> queue;
    int current = 0;
    int modifier = 100;
    int max_store = MAX_POWER;
    int total_extracted = 0;
    int wasted = 0;
    int amount_generated_last_update = 0;
};

std::array<PlayerPower, MAX_PLAYERS> power_list;

/// @return true if the requested power is available
bool add_power_request(unsigned player, unsigned requester_id, int amount);

void remove_power_request(const Structure& structure);

void reset_power();

/// @return the total power waiting to be transferred
int get_queued_power(unsigned player);

//inline void use_power(unsigned player, int amount)
//{
//  power_list[player].current = MAX(0, power_list[player].current - amount);
//}
//
//inline void add_power(unsigned player, int amount)
//{
//  power_list[player].current += amount;
//  if (power_list[player].current > power_list[player].max_store)
//  {
//    power_list[player].wasted += power_list[player].current - power_list[player].max_store;
//    power_list[player].current = power_list[player].max_store;
//  }
//}

inline void set_power_modifier(unsigned player, int modifier)
{
  power_list[player].modifier = modifier;
}

#endif //WARZONE2100_POWER_H
