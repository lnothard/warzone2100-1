//
// Created by luna on 08/12/2021.
//

#include "map.h"
#include "structure.h"

namespace Impl
{
  Structure::Structure(unsigned id, unsigned player)
  : Unit(id, player) { }

  bool Structure::is_blueprint() const
  {
    return state == BLUEPRINT_VALID ||
           state == BLUEPRINT_INVALID ||
           state == BLUEPRINT_PLANNED ||
           state == BLUEPRINT_PLANNED_BY_ALLY;
  }

  bool Structure::is_wall() const
  {
    if (stats->type == WALL || stats->type == WALL_CORNER)
      return true;

    return false;
  }

  bool Structure::is_radar_detector() const
  {
    if (!has_sensor() || state != BUILT) return false;
    return stats->sensor_stats->type == RADAR_DETECTOR;
  }

  bool Structure::is_probably_doomed() const
  {
      const auto hit_points = get_hp();
      return expected_damage > hit_points && expected_damage - hit_points > hit_points / 15;
  }

  bool Structure::is_pulled_to_terrain() const
  {
    return is_wall() || stats->type == DEFENSE || stats->type == GATE || stats->type == REARM_PAD;
  }

  bool Structure::has_modules() const
  {
    return num_modules > 0;
  }

  bool Structure::has_sensor() const
  {
    return stats->sensor_stats != nullptr;
  }

  bool Structure::has_standard_sensor() const
  {
    if (!has_sensor()) return false;
    const auto sensor_type = stats->sensor_stats->type;

    return sensor_type == STANDARD || sensor_type == SUPER;
  }

  bool Structure::has_CB_sensor() const
  {
    if (!has_sensor()) return false;
    const auto sensor_type = stats->sensor_stats->type;

    return sensor_type == INDIRECT_CB || sensor_type == SUPER;
  }

  bool Structure::has_VTOL_intercept_sensor() const
  {
    if (!has_sensor()) return false;
    const auto sensor_type = stats->sensor_stats->type;

    return sensor_type == VTOL_INTERCEPT || sensor_type == SUPER;
  }

  bool Structure::has_VTOL_CB_sensor() const
  {
    if (!has_sensor()) return false;
    const auto sensor_type = stats->sensor_stats->type;

    return sensor_type == VTOL_CB || sensor_type == SUPER;
  }

  bool Structure::smoke_when_damaged() const
  {
    if (is_wall() || stats->type == GATE || state == BEING_BUILT)
      return false;

    return true;
  }

  unsigned Structure::get_original_hp() const
  {
    return stats->upgraded_stats[get_player()].hit_points;
  }

  Vector2i Structure::get_size() const
  {
    return stats->size(get_rotation().direction);
  }

  const iIMDShape& Structure::get_IMD_shape() const
  {
    return *stats->base_imd;
  }

  float Structure::get_foundation_depth() const
  {
    return foundation_depth;
  }

  void Structure::update_expected_damage(const int damage)
  {
    expected_damage += damage;
    assert(expected_damage >= 0);
  }

  unsigned Structure::calculate_sensor_range() const
  {

  }

  int Structure::calculate_gate_height(const std::size_t time, const int minimum) const
  {
    if (stats->type != GATE) return 0;

    auto height = get_display_data().imd_shape->max.y;
    int open_height;
    switch (animation_state)
    {
      case OPEN:
        open_height = height;
        break;
      case OPENING:
        open_height = (height * std::max<int>(time + GAME_TICKS_PER_UPDATE - last_state_time, 0)) / GAME_TICKS_PER_SEC;
        break;
      case CLOSING:
        open_height = height - (height * std::max<int>(time - last_state_time, 0)) / GAME_TICKS_PER_SEC;
        break;
      default:
        return 0;
    }
    return std::max(std::min(open_height, height - minimum), 0);
  }

  void Structure::set_foundation_depth(const float depth)
  {
    foundation_depth = depth;
  }

  long count_assigned_droids(const Structure& structure)
  {
    const auto& droids = droid_lists[selectedPlayer];
    return std::count_if(droids.begin(), droids.end(), [&structure] (const auto& droid) {
        if (droid.get_current_order().target_object->get_id() == structure.get_id() &&
            droid.get_player() == structure.get_player())
        {
          return droid.is_VTOL() || has_artillery(structure);
        }
    });
  }

  bool being_built(const Structure& structure)
  {
    const auto& droids = droid_lists[structure.get_player()];
    return std::any_of(droids.begin(), droids.end(), [&structure] (const auto& droid) {
        auto& order = droid.get_current_order();
        return order.type == ORDER_TYPE::BUILD &&
               order.target_object->get_id() == structure.get_id();
    });
  }
}

bool Rearm_Pad::is_clear() const
{
  return rearm_target == nullptr || rearm_target->is_VTOL_rearmed_and_repaired();
}