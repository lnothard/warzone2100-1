//
// Created by luna on 08/12/2021.
//

#include "droid.h"

bool Droid::is_transporter() const
{
  return type == TRANSPORTER || type == SUPER_TRANSPORTER;
}

bool Droid::is_builder() const
{
  return type == CONSTRUCT || type == CYBORG_CONSTRUCT;
}

bool Droid::is_cyborg() const
{
  return (type == CYBORG || type == CYBORG_CONSTRUCT ||
          type == CYBORG_REPAIR || type == CYBORG_SUPER);
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

bool Droid::has_CB_sensor() const
{
  if (type != SENSOR) return false;

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

bool Droid::is_rearming() const
{
  if (!is_VTOL()) return false;
  if (type != WEAPON) return false;

  if (action == MOVE_TO_REARM ||
      action == WAIT_FOR_REARM ||
      action == MOVE_TO_REARM_POINT ||
      action == WAIT_DURING_REARM)
    return true;

  return false;
}

bool Droid::is_attacking() const
{
  if (!(type == WEAPON || type == CYBORG || type == CYBORG_SUPER))
    return false;

  if (action == ATTACK || action == MOVE_TO_ATTACK ||
      action == ROTATE_TO_ATTACK || action == VTOL_ATTACK ||
      action == MOVE_FIRE)
    return true;

  return false;
}

bool Droid::is_VTOL_rearmed_and_repaired() const
{
  assert(is_VTOL());
  if (is_damaged()) return false;
  if (type != WEAPON) return true;
  if (!has_full_ammo()) return false;
  return true;
}

void Droid::move_to_rearming_pad()
{
  if (!is_VTOL()) return;
  if (is_rearming()) return;


}