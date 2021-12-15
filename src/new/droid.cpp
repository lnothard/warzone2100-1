//
// Created by luna on 08/12/2021.
//

#include <ranges>
using namespace std;

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

bool Droid::is_commander() const
{
  return type == COMMAND;
}

bool Droid::is_VTOL() const
{
  if (!propulsion) return false;
  using enum PROPULSION_TYPE;

  return !is_transporter() && propulsion->propulsion_type == LIFT;
}

bool Droid::is_flying() const
{
  if (!propulsion) return false;
  using enum PROPULSION_TYPE;

  return (!movement.is_inactive() || is_transporter()) &&
         propulsion->propulsion_type == LIFT;
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

bool Droid::is_IDF() const
{
  return (type != WEAPON || !is_cyborg()) && has_artillery();
}

bool Droid::is_damaged() const
{
  return get_hp() < original_hp;
}

bool Droid::is_being_repaired() const
{
  if (!is_damaged()) return false;

  auto droids = *droid_lists[get_player()];

  return ranges::any_of(droids, [this] (const auto& droid) {
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
  if (!is_VTOL() || type != WEAPON) return false;

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
  if (is_damaged() || !has_full_ammo() || type == WEAPON)
    return false;

  return true;
}

bool Droid::is_VTOL_empty() const
{
  assert(is_VTOL());
  if (type != WEAPON) return false;

  return ranges::all_of(get_weapons(), [this] (const auto& weapon) {
    return weapon.is_VTOL_weapon() && weapon.is_empty_VTOL_weapon(get_player());
  });
}

bool Droid::is_VTOL_full() const
{
  assert(is_VTOL());
  if (type != WEAPON) return false;

  return ranges::all_of(get_weapons(), [] (const auto& weapon) {
    return weapon.is_VTOL_weapon() && weapon.has_full_ammo();
  });
}

bool Droid::are_all_VTOLs_rearmed() const
{
  if (!is_VTOL()) return true;
  auto droids = *droid_lists[get_player()];

  ranges::none_of(droids, [this] (const auto& droid) {
    return droid.is_rearming() &&
           droid.get_current_order().type == order.type &&
           droid.get_current_order().target_object == order.target_object;
  });
}

bool Droid::target_within_range(const Unit &target, uint8_t weapon_slot) const
{
  return num_weapons() != 0;
}

uint32_t Droid::get_level() const
{
  if (!brain) return 0;

  auto& rank_thresholds = brain->upgraded[get_player()].rank_thresholds;
  for (int i = 1; i < rank_thresholds.size(); ++i)
  {
    if (kills < rank_thresholds.at(i))
    {
      return i - 1;
    }
  }
  return rank_thresholds.size() - 1;
}

uint32_t Droid::get_commander_level() const
{
  if (!has_commander()) return 0;

  return group->get_commander_level();
}

uint32_t Droid::get_effective_level() const
{
  uint32_t level = get_level();
  if (!has_commander()) return level;

  uint32_t cmd_level = get_commander_level();
  if (cmd_level > level + 1)
    return cmd_level;

  return level;
}

void Droid::move_to_rearming_pad()
{
  if (!is_VTOL() || is_rearming()) return;

}

void Droid::cancel_build()
{
  using enum ORDER_TYPE;

  if (order.type == NONE || order.type == PATROL || order.type == HOLD ||
      order.type == SCOUT || order.type == GUARD)
  {
    order.target_object = nullptr;
    action = ACTION::NONE;
    return;
  }
  else
  {
    action = ACTION::NONE;
    order.type = NONE;
    movement.stop_moving();
  }
}

void Droid::give_action(ACTION new_action, const Unit& target_unit, Position position)
{

};

void Droid::start_new_action()
{
  time_action_started = gameTime;
  action_points_done = 0;
}

bool can_assign_fire_support(const Droid& droid, const Structure& structure)
{
  if (droid.num_weapons() == 0 || !structure.has_sensor()) return false;

  if (droid.is_VTOL())
  {
    return structure.has_VTOL_intercept_sensor() || structure.has_VTOL_CB_sensor();
  }
  else if (droid.has_artillery())
  {
    return structure.has_standard_sensor() || structure.has_CB_sensor();
  }

  return false;
}

bool is_droid_still_building(const Droid& droid)
{
  return droid.is_alive() && droid.get_current_action() == ACTION::BUILD;
}

void update_orientation(Droid& droid)
{
  if (droid.is_cyborg() || droid.is_flying() || droid.is_transporter())
    return;


}

auto count_player_command_droids(uint32_t player)
{
  auto droids = *droid_lists[player];

  return ranges::count_if(droids, [] (const auto& droid) {
    return droid.is_commander();
  });
}