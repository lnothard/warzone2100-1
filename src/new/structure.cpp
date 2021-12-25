//
// Created by luna on 08/12/2021.
//

#include "map.h"
#include "power.h"
#include "structure.h"

namespace Impl
{
	Structure::Structure(unsigned id, unsigned player)
		: Unit(id, player)
	{
	}

	bool Structure::is_blueprint() const noexcept
	{
		return state == BLUEPRINT_VALID ||
			state == BLUEPRINT_INVALID ||
			state == BLUEPRINT_PLANNED ||
			state == BLUEPRINT_PLANNED_BY_ALLY;
	}

	bool Structure::is_wall() const noexcept
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

	bool Structure::has_modules() const noexcept
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

	bool Structure::smoke_when_damaged() const noexcept
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

	float Structure::get_foundation_depth() const noexcept
	{
		return foundation_depth;
	}

	void Structure::update_expected_damage(const int damage)
	{
		expected_damage += damage;
		assert(expected_damage >= 0);
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
			open_height = (height * std::max<int>(time + GAME_TICKS_PER_UPDATE - last_state_time, 0)) /
				GAME_TICKS_PER_SEC;
			break;
		case CLOSING:
			open_height = height - (height * std::max<int>(time - last_state_time, 0)) / GAME_TICKS_PER_SEC;
			break;
		default:
			return 0;
		}
		return std::max(std::min(open_height, height - minimum), 0);
	}

	void Structure::set_foundation_depth(float depth) noexcept
	{
		foundation_depth = depth;
	}

  unsigned Structure::build_points_to_completion() const
  {
    return stats->build_point_cost - current_build_points;
  }

  unsigned Structure::calculate_refunded_power() const
  {
    auto power = stats->power_to_build;
    return power / 2;
  }

	unsigned count_assigned_droids(const Structure& structure)
	{
		const auto& droids = droid_lists[selectedPlayer];
		return std::count_if(droids.begin(), droids.end(), [&structure](const auto& droid)
		{
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
		return std::any_of(droids.begin(), droids.end(), [&structure](const auto& droid)
		{
			auto& order = droid.get_current_order();
			return order.type == ORDER_TYPE::BUILD &&
				order.target_object->get_id() == structure.get_id();
		});
	}

	bool being_demolished(const Structure& structure)
	{
		const auto& droids = droid_lists[structure.get_player()];
		return std::any_of(droids.begin(), droids.end(), [&structure](const auto& droid)
		{
			auto& order = droid.get_current_order();
			return order.type == ORDER_TYPE::DEMOLISH &&
				order.target_object->get_id() == structure.get_id();
		});
	}

	bool is_damaged(const Structure& structure)
	{
		return structure.get_hp() < structure.get_original_hp();
	}

	Structure_Bounds get_bounds(const Structure& structure) noexcept
	{
		return Structure_Bounds{
			map_coord(structure.get_position().xy()) - structure.get_size() / 2, structure.get_size()
		};
	}

	void adjust_tile_height(const Structure& structure, int new_height)
	{
		auto bounds = get_bounds(structure);
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

	int calculate_height(const Structure& structure)
	{
		auto& imd = structure.get_IMD_shape();
		auto height = imd.max.y + imd.min.y;
		return height - structure.calculate_gate_height(gameTime, 2);
		// Treat gate as at least 2 units tall, even if open, so that it's possible to hit.
	}

	int calculate_foundation_height(const Structure& structure)
	{
		auto bounds = get_bounds(structure);
		auto foundation_min = INT32_MIN;
		auto foundation_max = INT32_MAX;
		auto x_max = bounds.size_in_coords.x;
		auto y_max = bounds.size_in_coords.y;

		for (int breadth = 0; breadth <= y_max; ++breadth)
		{
			for (int width = 0; width <= x_max; ++width)
			{
				auto height = map_tile_height(bounds.top_left_coords.x, bounds.top_left_coords.y + breadth);
				foundation_min = std::min(foundation_min, height);
				foundation_max = std::min(foundation_max, height);
			}
		}
		return (foundation_min + foundation_max) / 2;
	}

	void align_structure(Structure& structure)
	{
		if (!structure.is_pulled_to_terrain())
		{
			auto map_height = calculate_foundation_height(structure);
			adjust_tile_height(structure, map_height);
			structure.set_height(map_height);
			structure.set_foundation_depth(structure.get_position().z);

			const auto& bounds = get_bounds(structure);
			auto x_max = bounds.size_in_coords.x;
			auto y_max = bounds.size_in_coords.y;
			auto coords = bounds.top_left_coords;

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

	bool target_within_range(const Structure& structure, const Unit& target, int weapon_slot)
	{
		if (num_weapons(structure) == 0) return false;

		auto& weapon = structure.get_weapons()[weapon_slot];
		auto max_range = weapon.get_max_range(structure.get_player());

		return object_position_square_diff(structure.get_position(), target.get_position()) < max_range * max_range &&
			target_in_line_of_fire(structure, target, weapon_slot);
	}
}

bool Rearm_Pad::is_clear() const
{
	return rearm_target == nullptr || rearm_target->is_VTOL_rearmed_and_repaired();
}

void Factory::increment_production_loops()
{
  assert(get_player() == selectedPlayer);

  if (production_loops == MAX_IN_RUN)
  {
    production_loops = 0;
  }
  else
  {
    ++production_loops;
    if (production_loops > MAX_IN_RUN)
    {
      production_loops = MAX_IN_RUN;
    }
  }
}

void Factory::decrement_production_loops()
{
  assert(get_player() == selectedPlayer);

  if (production_loops == 0)
  {
    production_loops = MAX_IN_RUN;
  }
  else
  {
    --production_loops;
  }
}

int Resource_Extractor::get_extracted_power() const
{
  if (!owning_power_generator)
    return 0;

  return power_list[get_player()].modifier * EXTRACT_POINTS /
         (100 * GAME_UPDATES_PER_SEC);
}

void Power_Generator::update_current_power()
{
  auto extracted_power = 0;
  for (auto extractor : associated_resource_extractors)
  {
    if (!extractor) return;

  }
}

const Structure* find_repair_facility(unsigned player)
{
  const auto& structures = structure_lists[player];

  auto it = std::find_if(structures.begin(), structures.end(), [](const auto& structure)
  {
    return dynamic_cast<const Repair_Facility*>(structure);
  });
  return (it != std::begin(structures)) ? *it : nullptr;
}