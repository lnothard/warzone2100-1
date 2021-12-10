//
// Created by luna on 08/12/2021.
//

#include <algorithm>

#include "droid.h"
#include "obj_lists.h"

Droid::Droid(uint32_t id, uint32_t player)
    : Unit(id, player)
{
}

ACTION Droid::get_current_action() const
{
  return action;
}

const Order& Droid::get_current_order() const
{
  return order;
}

bool Droid::is_probably_doomed(bool is_direct_damage) const
{
  auto is_doomed = [this] (uint32_t damage) {
    const auto hit_points = get_hp();
    return damage > hit_points && damage - hit_points > hit_points / 5;
  };

  if (is_direct_damage)
  {
    return is_doomed(expected_damage_direct);
  }

  return is_doomed(expected_damage_indirect);
}

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

bool Droid::is_repairer() const
{
  return type == REPAIRER || type == CYBORG_REPAIR;
}

bool Droid::is_damaged() const
{
  return get_hp() < original_hp;
}

bool Droid::is_being_repaired() const
{
  if (!is_damaged()) return false;

  auto droids = *droid_lists[get_player()];

  return std::any_of(droids.begin(), droids.end(), [this] (const auto& droid) {
    return droid.is_repairer() && droid.get_current_action() == DROID_REPAIR &&
           order.target_object == this;
  });
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

bool Droid::has_electronic_weapon() const
{
  if (Unit::has_electronic_weapon()) return true;
  if (type != COMMAND) return false;

  return group->has_electronic_weapon();
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

bool Droid::are_all_VTOLs_rearmed() const
{
  if (!is_VTOL()) return true;
  auto droids = *droid_lists[get_player()];

  std::none_of(droids.begin(), droids.end(), [this] (const auto& droid) {
    return droid.is_rearming() &&
           droid.get_current_order().type == order.type &&
           droid.get_current_order().target_object == order.target_object;
  });
}

bool Droid::target_within_range(const Unit &target, uint8_t weapon_slot) const
{
  if (num_weapons() == 0) return false;
}

void Droid::move_to_rearming_pad()
{
  if (!is_VTOL()) return;
  if (is_rearming()) return;


}

void Droid::cancel_build()
{

}

static inline bool is_droid_still_building(const Droid& droid)
{
  return droid.is_alive() && droid.get_current_action() == ACTION::BUILD;
}

void update_orientation(Droid& droid)
{
  if (droid.is_cyborg() || droid.is_flying() || droid.is_transporter())
    return;


}