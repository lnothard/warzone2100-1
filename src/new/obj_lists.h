//
// Created by luna on 09/12/2021.
//

#ifndef WARZONE2100_OBJ_LISTS_H
#define WARZONE2100_OBJ_LISTS_H

#include <array>

#include "droid.h"
#include "structure.h"

extern std::array<std::vector<Droid>, MAX_PLAYERS> droid_lists;

// Have to use pointers here because `Structure` is a base class
extern std::array< std::vector<std::unique_ptr<Impl::Structure> >, MAX_PLAYERS> structure_lists;
extern std::vector<Feature> feature_list;
extern std::vector<Unit*> sensor_list;

void create_droid(unsigned id, unsigned player);
void create_structure(unsigned id, unsigned player);
void destroy_droid(Droid& droid);
void destroy_structure(Structure& structure);

#endif // WARZONE2100_OBJ_LISTS_H
