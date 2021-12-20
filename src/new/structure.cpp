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
  Structure::Structure(unsigned id, unsigned player)
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

  bool Structure::is_radar_detector() const
  {
    if (!has_sensor() || state != BUILT) return false;
    return stats.sensor_stats->type == RADAR_DETECTOR;
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
    const auto sensor_type = stats.sensor_stats->type;

    return sensor_type == STANDARD || sensor_type == SUPER;
  }

  bool Structure::has_CB_sensor() const
  {
    if (!has_sensor()) return false;
    const auto sensor_type = stats.sensor_stats->type;

    return sensor_type == INDIRECT_CB || sensor_type == SUPER;
  }

  bool Structure::has_VTOL_intercept_sensor() const
  {
    if (!has_sensor()) return false;
    const auto sensor_type = stats.sensor_stats->type;

    return sensor_type == VTOL_INTERCEPT || sensor_type == SUPER;
  }

  bool Structure::has_VTOL_CB_sensor() const
  {
    if (!has_sensor()) return false;
    const auto sensor_type = stats.sensor_stats->type;

    return sensor_type == VTOL_CB || sensor_type == SUPER;
  }

  bool Structure::smoke_when_damaged() const
  {
    if (is_wall() || stats.type == GATE || state == BEING_BUILT)
      return false;

    return true;
  }


  unsigned Structure::get_original_hp() const
  {
    return stats.upgraded_stats[get_player()].hit_points;
  }

  Vector2i Structure::get_size() const
  {
    return stats.size(get_rotation().direction);
  }


  const iIMDShape& Structure::get_IMD_shape() const
  {
    return *stats.base_imd;
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

  bool Structure::target_within_range(const Unit &target) const
  {
    if (num_weapons() == 0) return false;

    auto max_range = get_max_weapon_range();
    return object_position_square_diff(get_position(), target.get_position()) < max_range * max_range &&
           target_in_line_of_fire(target);
  }

  int Structure::calculate_gate_height(const std::size_t time, const int minimum) const
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


  void Structure::set_foundation_depth(const float depth)
  {
    foundation_depth = depth;
  }

  void adjust_tile_height(const Structure& structure, int new_height)
  {
    const auto& bounds = structure.get_bounds();
    auto x_max = bounds.size_in_coords.x;
    auto y_max = bounds.size_in_coords.y;

    auto coords = bounds.top_left_coords;

    for (int breadth = 0; breadth <= y_max; ++breadth)
    {
      for (int width = 0; width <= x_max; ++width)
      {
        set_tile_height(coords.x + width, coords.y + breadth, new_height);

        if (tile_is_occupied_by_feature(*get_map_tile(coords.x + width, coords.y + breadth)))
        {
          get_feature_from_tile(coords.x + width, coords.y + breadth)->set_height(new_height);
        }
      }
    }
  }

  void align_structure(Structure& structure)
  {
    if (!structure.is_pulled_to_terrain())
    {
      const auto map_height = calculate_foundation_height(structure);
      adjust_tile_height(structure, map_height);
      structure.set_height(map_height);
      structure.set_foundation_depth(structure.get_position().z);

      const auto& bounds = structure.get_bounds();
      const auto x_max = bounds.size_in_coords.x;
      const auto y_max = bounds.size_in_coords.y;
      const auto coords = bounds.top_left_coords;

      for (int breadth = -1; breadth <= y_max; ++breadth)
      {
        for (int width = -1; width <= x_max; ++width)
        {
          auto neighbouring_structure = dynamic_cast<Structure*>(
                  get_map_tile(coords.x + width, coords.y + breadth)->occupying_object);
          if (neighbouring_structure != nullptr && neighbouring_structure->is_pulled_to_terrain())
          {
            align_structure(*neighbouring_structure);
          }
        }
      }
    }
    else
    {
      const auto& imd = structure.get_display_data().imd_shape;
      structure.set_height(TILE_MIN_HEIGHT);
      structure.set_foundation_depth(TILE_MAX_HEIGHT);

      auto dir = iSinCosR(structure.get_rotation().direction, 1);
      // Rotate s->max.{x, z} and s->min.{x, z} by angle rot.direction.
      Vector2i p1{imd->max.x * dir.y - imd->max.z * dir.x, imd->max.x * dir.x + imd->max.z * dir.y};
      Vector2i p2{imd->min.x * dir.y - imd->min.z * dir.x, imd->min.x * dir.x + imd->min.z * dir.y};

      auto h1 = calculate_map_height(structure.get_position().x + p1.x, structure.get_position().y + p2.y);
      auto h2 = calculate_map_height(structure.get_position().x + p1.x, structure.get_position().y + p1.y);
      auto h3 = calculate_map_height(structure.get_position().x + p2.x, structure.get_position().y + p1.y);
      auto h4 = calculate_map_height(structure.get_position().x + p2.x, structure.get_position().y + p2.y);
      auto minH = std::min({h1, h2, h3, h4});
      auto maxH = std::max({h1, h2, h3, h4});
      structure.set_height(std::max(structure.get_position().z, maxH));
      structure.set_foundation_depth(std::min<float>(structure.get_foundation_depth(), minH));
    }
  }

  bool is_a_droid_building_this_structure(const Structure& structure)
  {
    assert(structure != nullptr);
    const auto& droids = droid_lists[structure.get_player()];
    return std::any_of(droids.begin(), droids.end(),
                       [&structure] (const auto& droid) {
      const auto& order = droid.get_current_order();
      return order.type == ORDER_TYPE::BUILD &&
             order.target_object->get_id() == structure.get_id();
    });
  }

  int calculate_height(const Structure& structure)
  {
    const auto& imd = structure.get_IMD_shape();
    auto height = imd.max.y + imd.min.y;
    return height - structure.calculate_gate_height(gameTime, 2);  // Treat gate as at least 2 units tall, even if open, so that it's possible to hit.
  }

  Structure_Bounds get_bounds(const Structure& structure)
  {
    return Structure_Bounds{ map_coord(structure.get_position().xy()) - structure.get_size() / 2, structure.get_size() };
  }

  unsigned count_assigned_droids(const Structure& structure)
  {
    const auto& droids = droid_lists[selectedPlayer];

    return std::count_if(droids.begin(), droids.end(), [&structure] (const auto& droid) {
        if (droid.get_current_order().target_object->get_id() == structure.get_id() &&
            droid.get_player() == structure.get_player())
        {
          return droid.is_VTOL() || structure.has_artillery();
        }
    });
  }
}

bool Rearm_Pad::is_clear() const
{
  return rearm_target == nullptr || rearm_target->is_VTOL_rearmed_and_repaired();
}