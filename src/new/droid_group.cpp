//
// Created by luna on 08/12/2021.
//

#include <ranges>
using namespace std;

#include "droid_group.h"

bool Droid_Group::is_command_group() const
{
  return type == COMMAND;
}

bool Droid_Group::has_electronic_weapon() const
{
  return ranges::any_of(members, [] (const auto* droid) {
    return dynamic_cast<const Unit*>(droid)->has_electronic_weapon();
  });
}

uint32_t Droid_Group::get_commander_level() const
{
  return commander->get_level();
}

void Droid_Group::commander_gain_experience(uint32_t exp)
{
  commander->gain_experience(exp);
}