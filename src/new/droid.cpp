//
// Created by luna on 08/12/2021.
//

#include "droid.h"

bool Droid::is_transporter() const
{
  return type == TRANSPORTER || type == SUPER_TRANSPORTER;
}

bool Droid::is_damaged() const
{
  return hp_below_x(original_hp);
}

bool Droid::has_commander() const
{
  if (type == COMMAND &&
      group != nullptr &&
      group->is_command_group())
    return true;

  return false;
}