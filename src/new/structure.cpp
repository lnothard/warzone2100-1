//
// Created by luna on 08/12/2021.
//

#include "map.h"
#include "structure.h"
#include "obj_lists.h"

Structure_Bounds::Structure_Bounds()
    : top_left_coords(0, 0), size_in_coords(0, 0)
{
}

Structure_Bounds::Structure_Bounds(const Vector2i& top_left_coords, const Vector2i& size_in_coords)
: top_left_coords{top_left_coords}, size_in_coords{size_in_coords}
{
}

namespace Impl
{
  Structure::Structure(uint32_t id, uint32_t player)
  : Unit(id, player)
  {

  }

  bool Structure::is_blueprint() const
  {
    return state == BLUEPRINT_VALID ||
           state == BLUEPRINT_INVALID ||
           state == BLUEPRINT_PLANNED ||
           state == BLUEPRINT_PLANNED_BY_ALLY;
  }

  bool Structure::is_wall() const
  {
    if (stats.type == WALL || stats.type == WALL_CORNER)
      return true;

    return false;
  }

  bool Structure::is_probably_doomed() const
  {
      const auto hit_points = get_hp();
      return expected_damage > hit_points && expected_damage - hit_points > hit_points / 15;
  }

  bool Structure::is_pulled_to_terrain() const
  {
    return is_wall() || stats.type == DEFENSE || stats.type == GATE || stats.type == REARM_PAD;
  }

  bool Structure::is_damaged() const
  {
    return get_hp() < get_original_hp();
  }

  bool Structure::has_modules() const
  {
    return num_modules > 0;
  }

  bool Structure::has_sensor() const
  {
    return stats.sensor_stats != nullptr;
  }

  bool Structure::has_standard_sensor() const
  {
    if (!has_sensor()) return false;
    auto sensor_type = stats.sensor_stats->type;

    return sensor_type == STANDARD || sensor_type == SUPER;
  }

  bool Structure::has_CB_sensor() const
  {
    if (!has_sensor()) return false;
    auto sensor_type = stats.sensor_stats->type;

    return sensor_type == INDIRECT_CB || sensor_type == SUPER;
  }

  bool Structure::has_VTOL_intercept_sensor() const
  {
    if (!has_sensor()) return false;
    auto sensor_type = stats.sensor_stats->type;

    return sensor_type == VTOL_INTERCEPT || sensor_type == SUPER;
  }

  bool Structure::has_VTOL_CB_sensor() const
  {
    if (!has_sensor()) return false;
    auto sensor_type = stats.sensor_stats->type;

    return sensor_type == VTOL_CB || sensor_type == SUPER;
  }

  bool Structure::smoke_when_damaged() const
  {
    if (is_wall() || stats.type == GATE || state == BEING_BUILT)
      return false;

    return true;
  }

  uint16_t Structure::count_assigned_droids() const
  {
    const auto& droids = *droid_lists[selectedPlayer];

    return std::count_if(droids.begin(), droids.end(), [this] (const auto& droid) {
      if (droid.get_current_order().target_object->get_id() == get_id() &&
          droid.get_player() == get_player())
      {
        return droid.is_VTOL() || has_artillery();
      }
    });
  }

  uint32_t Structure::get_original_hp() const
  {
    return stats.upgraded_stats[get_player()].hit_points;
  }

  Vector2i Structure::get_size() const
  {
    return stats.size(get_rotation().direction);
  }

  Structure_Bounds Structure::get_bounds() const
  {
    return Structure_Bounds{ map_coord(get_position().xy()) - get_size() / 2, get_size() };
  }

  const iIMDShape& Structure::get_IMD_shape() const
  {
    return *stats.base_imd;
  }

  void Structure::update_expected_damage(const int32_t damage)
  {
    expected_damage += damage;
    assert(expected_damage >= 0);
  }

  int Structure::calculate_sensor_range() const
  {

  }

  bool Structure::target_within_range(const Unit &target) const
  {
    if (num_weapons() == 0) return false;

    auto max_range = get_max_weapon_range();
    return object_position_square_diff(get_position(), target.get_position()) < max_range * max_range &&
           target_in_line_of_fire(target);
  }

  int Structure::calculate_gate_height(const uint32_t time, const int minimum) const
  {
    if (stats.type != GATE) return 0;

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

  int Structure::calculate_height() const
  {
    const auto& imd = get_IMD_shape();
    auto height = imd.max.y + imd.min.y;
    height -= calculate_gate_height(gameTime, 2);  // Treat gate as at least 2 units tall, even if open, so that it's possible to hit.
    return height;
  }

  static inline int calculate_foundation_height(const Structure& structure)
  {
    const Structure_Bounds& bounds = structure.get_bounds();
  }
}

bool Rearm_Pad::is_clear() const
{
  return rearm_target == nullptr || rearm_target->is_VTOL_rearmed_and_repaired();
}