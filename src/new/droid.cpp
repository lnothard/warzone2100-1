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

bool Droid::is_stationary() const
{
  return movement.is_stationary();
}

bool Droid::has_commander() const
{
  if (type == COMMAND &&
      group != nullptr &&
      group->is_command_group())
    return true;

  return false;
}

void Droid::gain_experience(uint32_t exp)
{
  experience += exp;
}

void Droid::commander_gain_experience(uint32_t exp)
{
  assert(has_commander());
  group->commander_gain_experience(exp);
}