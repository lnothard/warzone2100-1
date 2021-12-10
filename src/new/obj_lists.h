//
// Created by luna on 09/12/2021.
//

#ifndef WARZONE2100_OBJ_LISTS_H
#define WARZONE2100_OBJ_LISTS_H

#include "droid.h"
#include "structure.h"

extern std::unique_ptr< std::vector<Droid> > droid_lists[MAX_PLAYERS];
extern std::unique_ptr< std::vector<Structure> > structure_lists[MAX_PLAYERS];

void create_droid(uint32_t id, uint32_t player)
{
  droid_lists[player]->emplace_back(id, player);
}

#endif // WARZONE2100_OBJ_LISTS_H