//
// Created by luna on 08/12/2021.
//

#include <algorithm>

#include "droid_group.h"

bool Droid_Group::is_command_group() const
{
  return type == COMMAND;
}

bool Droid_Group::has_electronic_weapon() const
{
  return std::any_of(members.begin(), members.end(), [] (const auto* droid) {
    return dynamic_cast<const Unit*>(droid)->has_electronic_weapon();
  });
}

unsigned Droid_Group::get_commander_level() const
{
  return commander->get_level();
}

void Droid_Group::commander_gain_experience(unsigned exp)
{
  commander->gain_experience(exp);
}