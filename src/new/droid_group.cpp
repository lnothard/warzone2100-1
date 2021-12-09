//
// Created by luna on 08/12/2021.
//

#include "droid_group.h"

bool Droid_Group::is_command_group() const
{
  return type == COMMAND;
}

void Droid_Group::commander_gain_experience(uint32_t exp)
{
  commander->gain_experience(exp);
}