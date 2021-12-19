//
// Created by luna on 09/12/2021.
//

#ifndef WARZONE2100_OBJ_LISTS_H
#define WARZONE2100_OBJ_LISTS_H

#include <array>

#include "droid.h"
#include "structure.h"

extern std::array< std::vector<Droid>, MAX_PLAYERS > droid_lists;
// have to use pointers here because -Structure- is a base class
extern std::array< std::vector< std::unique_ptr<Impl::Structure> >, MAX_PLAYERS > structure_lists;
extern std::vector<Feature> feature_list;

void create_droid(uint32_t id, uint32_t player);
void create_structure(uint32_t id, uint32_t player);
void destroy_droid(Droid& droid);
void destroy_structure(Structure& structure);

#endif // WARZONE2100_OBJ_LISTS_H