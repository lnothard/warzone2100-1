//
// Created by Luna Nothard on 19/12/2021.
//

#include "obj_lists.h"

void create_droid(uint32_t id, uint32_t player)
{
  droid_lists[player].emplace_back(id, player);
}

void create_structure(uint32_t id, uint32_t player)
{
  auto new_structure = std::make_unique<Impl::Structure>(id, player);
  structure_lists[player].push_back(new_structure);
}

void destroy_droid(Droid& droid)
{
  std::erase(droid_lists[droid.get_player()], droid);
}

void destroy_structure(Structure& structure)
{
  std::erase(structure_lists[structure.get_player()], structure);
}