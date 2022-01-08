//
// Created by Luna Nothard on 25/12/2021.
//

#include <numeric>

#include "power.h"

PowerRequest::PowerRequest(int amount, unsigned id)
  : amount{amount}, requester_id{id}
{
}

//bool add_power_request(unsigned player, unsigned requester_id, int amount)
//{
//  auto& player_power = power_list[player];
//  auto required_power = amount;
//
//  auto it = player_power.queue.begin();
//  for (; it->requester_id != requester_id; ++it)
//  {
//    required_power += it->amount;
//  }
//
//  if (it == player_power.queue.end())
//  {
//    player_power.queue.emplace_back(requester_id, amount);
//  }
//  else
//  {
//    it->amount = amount;
//  }
//
//  return required_power <= power_list[player].current;
//}

//void remove_power_request(const Structure& structure)
//{
//  auto& player_power = power_list[structure.get_player()];
//
//  std::erase_if(player_power.queue, [&structure](auto& request)
//  {
//    return request.requester_id == structure.get_id();
//  });
//}

//void reset_power()
//{
//  std::for_each(power_list.begin(), power_list.end(), [](auto& player_power)
//  {
//    player_power.current = 0;
//    player_power.total_extracted = 0;
//    player_power.wasted = 0;
//    player_power.modifier = 100;
//    player_power.queue.clear();
//    player_power.max_store = MAX_POWER;
//    player_power.amount_generated_last_update = 0;
//  });
//}

//int get_queued_power(unsigned player)
//{
//  const auto& queue = power_list[player].queue;
//  return std::accumulate(queue.begin(), queue.end(), 0, [](int sum, const auto& request)
//  {
//    return sum + request.amount;
//  });
//}

void update_player_power(unsigned player, int ticks)
{
  auto current_power = power_list[player].current;
  auto& structures = structure_lists[player];
}

void use_power(unsigned player, int amount)
{
  power_list[player].current = MAX(0, power_list[player].current - amount);
}

void add_power(unsigned player, int amount)
{
  power_list[player].current += amount;
  if (power_list[player].current > power_list[player].max_store)
  {
    power_list[player].wasted += power_list[player].current - power_list[player].max_store;
    power_list[player].current = power_list[player].max_store;
  }
}

void set_power_modifier(unsigned player, int modifier)
{
  power_list[player].modifier = modifier;
}
