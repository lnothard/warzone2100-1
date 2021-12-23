//
// Created by luna on 08/12/2021.
//

#include <algorithm>

#include "droid.h"
#include "projectile.h"

Droid::Droid(unsigned id, unsigned player)
    : Unit(id, player) { }

ACTION Droid::get_current_action() const noexcept
{
  return action;
}

const Order& Droid::get_current_order() const
{
  return *order;
}

bool Droid::is_probably_doomed(bool is_direct_damage) const
{
  auto is_doomed = [this] (unsigned damage) {
    const auto hit_points = get_hp();
    return damage > hit_points && damage - hit_points > hit_points / 5;
  };

  if (is_direct_damage)
  {
    return is_doomed(expected_damage_direct);
  }

  return is_doomed(expected_damage_indirect);
}

bool Droid::is_commander() const noexcept
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

  return (!movement->is_inactive() || is_transporter()) &&
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
  return (type != WEAPON || !is_cyborg()) && has_artillery(*this);
}

bool Droid::is_radar_detector() const
{
  if (!sensor) return false;

  return sensor->type == SENSOR_TYPE::RADAR_DETECTOR;
}

bool Droid::is_damaged() const
{
  return get_hp() < original_hp;
}


bool Droid::is_stationary() const
{
  return movement->is_stationary();
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
  if (Impl::has_electronic_weapon(*this)) return true;
  if (type != COMMAND) return false;

  return group->has_electronic_weapon();
}

bool Droid::has_CB_sensor() const
{
  if (type != SENSOR) return false;

}

void Droid::gain_experience(unsigned exp)
{
  experience += exp;
}

void Droid::commander_gain_experience(unsigned exp) const
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

bool Droid::is_attacking() const noexcept
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
  if (is_damaged() || !has_full_ammo(*this) || type == WEAPON)
    return false;

  return true;
}

bool Droid::is_VTOL_empty() const
{
  assert(is_VTOL());
  if (type != WEAPON) return false;

  return std::all_of(get_weapons().begin(), get_weapons().end(), [this] (const auto& weapon) {
    return weapon.is_VTOL_weapon() && weapon.is_empty_VTOL_weapon(get_player());
  });
}

bool Droid::is_VTOL_full() const
{
  assert(is_VTOL());
  if (type != WEAPON) return false;

  return std::all_of(get_weapons().begin(), get_weapons().end(), [] (const auto& weapon) {
    return weapon.is_VTOL_weapon() && weapon.has_full_ammo();
  });
}

bool Droid::is_valid_target(const ::Unit* attacker, int weapon_slot) const
{
  auto target_airborne = bool { false };
  auto valid_target = bool { false };

  if (propulsion->is_airborne)
  {
    if (movement->is_inactive())
      target_airborne = true;
  }

  if (const auto as_droid = dynamic_cast<const Droid*>(attacker))
  {
	  if (as_droid->get_type() == SENSOR)
      return !target_airborne;

    if (num_weapons(*as_droid) == 0)
      return false;

    auto& weapon_stats = attacker->get_weapons()[weapon_slot].get_stats();

	  if (auto surface_to_air = weapon_stats.surface_to_air; ((surface_to_air & SHOOT_IN_AIR) && target_airborne) ||
		  ((surface_to_air & SHOOT_ON_GROUND) && !target_airborne))
      return true;

    return false;
  }
  else // attacker is a structure
  {

  }
  return valid_target;
}

DROID_TYPE Droid::get_type() const noexcept
{
  return type;
}

unsigned Droid::get_level() const
{
  if (!brain) return 0;

  const auto& rank_thresholds = brain->upgraded[get_player()].rank_thresholds;
  for (int i = 1; i < rank_thresholds.size(); ++i)
  {
    if (kills < rank_thresholds.at(i))
    {
      return i - 1;
    }
  }
  return rank_thresholds.size() - 1;
}

unsigned Droid::get_commander_level() const
{
  if (!has_commander()) return 0;

  return group->get_commander_level();
}

unsigned Droid::commander_max_group_size() const
{
  assert (is_commander() && group->is_command_group());

  auto& cmd_stats = brain->upgraded[get_player()];
  return get_level() * cmd_stats.max_droids_multiplier + cmd_stats.max_droids_assigned;
}

const iIMDShape& Droid::get_IMD_shape() const
{
  return *body->imd_shape;
}

void Droid::move_to_rearming_pad()
{
  if (!is_VTOL() || is_rearming()) return;

}

void Droid::cancel_build()
{
  using enum ORDER_TYPE;

  if (order->type == NONE || order->type == PATROL || order->type == HOLD ||
      order->type == SCOUT || order->type == GUARD)
  {
    order->target_object = nullptr;
    action = ACTION::NONE;
    return;
  }
  else
  {
    action = ACTION::NONE;
    order->type = NONE;
    movement->stop_moving();
  }
}

void Droid::reset_action() noexcept
{
  time_action_started = gameTime;
  action_points_done = 0;
}

void Droid::update_expected_damage(unsigned damage, bool is_direct) noexcept
{
  if (is_direct)
    expected_damage_direct += damage;
  else
    expected_damage_indirect += damage;
}

unsigned Droid::calculate_sensor_range() const
{
  auto ecm_range = ecm->upgraded[get_player()].range;
  if (ecm_range > 0) return ecm_range;

  return sensor->upgraded[get_player()].range;
}

unsigned Droid::calculate_max_range() const
{
  if (type == SENSOR)
    return calculate_sensor_range();
  else if (num_weapons(*this) == 0)
    return 0;
  else
    return get_max_weapon_range(*this);
}

int Droid::calculate_height() const
{
  auto& imd = body->imd_shape;
  auto height = imd->max.y - imd->min.y;
  auto utility_height = 0, y_max = 0, y_min = 0;

  if (is_VTOL())
  {
    return height + VTOL_HITBOX_MODIFIER;
  }

  const auto& weapon_stats = get_weapons()[0].get_stats();
  switch (type)
  {
    case WEAPON:
      if (num_weapons(*this) == 0) break;

      y_max = weapon_stats.imd_shape->max.y;
      y_min = weapon_stats.imd_shape->min.y;
      break;
    case SENSOR:
      y_max = sensor->imd_shape->max.y;
      y_min = sensor->imd_shape->min.y;
      break;
    case ECM:
      y_max = ecm->imd_shape->max.y;
      y_min = ecm->imd_shape->min.y;
      break;
    case CONSTRUCT:
      break;
  }
}

bool still_building(const Droid& droid)
{
  return droid.is_alive() && droid.get_current_action() == ACTION::BUILD;
}

bool can_assign_fire_support(const Droid& droid, const Structure& structure)
{
  if (num_weapons(droid) == 0 || !structure.has_sensor()) return false;

  if (droid.is_VTOL())
  {
    return structure.has_VTOL_intercept_sensor() || structure.has_VTOL_CB_sensor();
  }
  else if (has_artillery(droid))
  {
    return structure.has_standard_sensor() || structure.has_CB_sensor();
  }
  return false;
}

bool all_VTOLs_rearmed(const Droid& droid)
{
  if (!droid.is_VTOL()) return true;

  const auto& droids = droid_lists[droid.get_player()];
  return std::none_of(droids.begin(), droids.end(), [&droid] (const auto& other_droid) {
      return other_droid.is_rearming() &&
             other_droid.get_current_order().type == droid.get_current_order().type &&
             other_droid.get_current_order().target_object == droid.get_current_order().target_object;
  });
}

bool VTOL_ready_to_rearm(const Droid& droid, const Rearm_Pad &rearm_pad)
{
  if (droid.is_VTOL() || droid.get_current_action() == ACTION::WAIT_FOR_REARM ||
      !droid.is_VTOL_rearmed_and_repaired() || rearm_pad.is_clear() || !droid.is_rearming())
    return true;

  return false;
}

bool being_repaired(const Droid& droid)
{
  using enum ACTION;
  if (!droid.is_damaged()) return false;

  const auto& droids = droid_lists[droid.get_player()];
  return std::any_of(droids.begin(), droids.end(), [&droid] (const auto& other_droid) {
      return other_droid.is_repairer() && other_droid.get_current_action() == DROID_REPAIR &&
             other_droid.get_current_order().target_object->get_id() == droid.get_id();
  });
}

unsigned get_effective_level(const Droid& droid)
{
  auto level = droid.get_level();
  if (!droid.has_commander()) return level;

  auto cmd_level = droid.get_commander_level();
  if (cmd_level > level + 1)
    return cmd_level;

  return level;
}

unsigned count_player_command_droids(unsigned player)
{
  const auto& droids = droid_lists[player];
  return std::count_if(droids.begin(), droids.end(), [] (const auto& droid) {
      return droid.is_commander();
  });
}

void update_orientation(Droid& droid)
{
  if (droid.is_cyborg() || droid.is_flying() || droid.is_transporter())
    return;


}

unsigned count_droids_for_level(unsigned player, unsigned level)
{
  const auto& droids = droid_lists[player];

  return std::count_if(droids.begin(), droids.end(),
                       [level] (const auto& droid) {
    return droid.get_level() == level;
  });
}

uint8_t is_target_visible(const Droid& droid, const Simple_Object* target, bool walls_block)
{
  constexpr static uint8_t VISIBLE = UBYTE_MAX;
  constexpr static uint8_t RADAR_BLIP = UBYTE_MAX / 2;
  constexpr static uint8_t NOT_VISIBLE = 0;

  auto& droid_position = droid.get_position();
  auto& target_position = target->get_position();

  if  (!is_coord_on_map(droid_position.x, droid_position.y) ||
       !is_coord_on_map(target_position.x, target_position.y))
    return 0;

  if (droid.get_current_order().target_object->get_id() == target->get_id() && droid.has_CB_sensor())
    return VISIBLE;

  auto range = droid.calculate_sensor_range();
  auto distance = iHypot((target_position - droid_position).xy());

  if (distance == 0) return VISIBLE;

  const auto target_tile = get_map_tile(map_coord(target_position.x), map_coord(target_position.y));
  bool is_jammed = target_tile->jammer_bits & ~alliance_bits[droid.get_player()];

  if (distance < range)
  {
    if (droid.is_VTOL()) return VISIBLE;

    else if (dynamic_cast<const Droid*>(target))
    {
      const auto* as_droid = dynamic_cast<const Droid*>(target);
      if (as_droid->is_VTOL()) return VISIBLE;
    }
  }

  bool is_tile_watched = target_tile->watchers[droid.get_player()] > 0;
  bool is_tile_watched_by_sensors = target_tile->watching_sensors[droid.get_player()] > 0;

  if (is_tile_watched || is_tile_watched_by_sensors)
  {
    if (is_jammed)
    {
      if (!is_tile_watched)
        return RADAR_BLIP;
    }
    else
    {
      return VISIBLE;
    }
  }

  if (droid.is_radar_detector() && )
    return RADAR_BLIP;

  return NOT_VISIBLE;
}

bool target_within_action_range(const Droid& droid, const Unit &target, int weapon_slot)
{
  if (num_weapons(droid) == 0) return false;

  auto& droid_position = droid.get_position();
  auto& target_position = target.get_position();

  auto x_diff = droid_position.x - target_position.x;
  auto y_diff = droid_position.y - target_position.y;

  auto square_diff = x_diff * x_diff + y_diff * y_diff;
  auto min_range = droid.get_weapons()[weapon_slot].get_min_range(droid.get_player());
  auto range_squared = min_range * min_range;

  if (square_diff <= range_squared) return true;
  return false;
}

bool target_within_weapon_range(const Droid& droid, const Unit& target, int weapon_slot)
{
  auto max_range = droid.get_weapons()[weapon_slot].get_max_range(droid.get_player());
  return object_position_square_diff(droid, target) < max_range * max_range;
}

void initialise_ai_bits()
{
  for (int i = 0; i < MAX_PLAYER_SLOTS; ++i)
  {
    alliance_bits[i] = 0;
    for (int j = 0; j < MAX_PLAYER_SLOTS; ++j)
    {
      bool valid = i == j && i < MAX_PLAYERS;
      alliances[i][j] = valid ? ALLIANCE_FORMED : ALLIANCE_BROKEN;
      alliance_bits[i] |= valid << j;
    }
  }
  satellite_uplink_bits = 0;
}

int get_commander_index(const Droid& commander)
{
  assert(commander.is_commander());

  auto& droids = droid_lists[commander.get_player()];
  return std::find_if(droids.begin(), droids.end(), [&commander] (const auto& droid) {
    return droid.is_commander() && droid.get_id() == commander.get_id();
  }) - droids.begin();
}
