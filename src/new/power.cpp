//
// Created by Luna Nothard on 25/12/2021.
//

#include <numeric>

#include "power.h"

bool add_power_request(unsigned player, unsigned requester_id, int amount)
{
  auto& player_power = power_list[player];
  auto required_power = amount;

  auto it = player_power.queue.begin();
  for (; it->requester_id != requester_id; ++it)
  {
    required_power += it->amount;
  }

  if (it == player_power.queue.end())
  {
    player_power.queue.emplace_back(requester_id, amount);
  }
  else
  {
    it->amount = amount;
  }

  return required_power <= power_list[player].current;
}

void remove_power_request(const Structure& structure)
{
  auto& player_power = power_list[structure.get_player()];

  std::erase_if(player_power.queue, [&structure](auto& request)
  {
    return request.requester_id == structure.get_id();
  });
}
