//
// Created by luna on 08/12/2021.
//

#ifndef WARZONE2100_STRUCTURE_H
#define WARZONE2100_STRUCTURE_H

#include "lib/ivis_opengl/ivisdef.h"
#include "droid.h"
#include "map.h"
#include "positiondef.h"
#include "unit.h"

enum class STRUCTURE_STATE
{
  BEING_BUILT,
  BUILT,
  BLUEPRINT_VALID,
  BLUEPRINT_INVALID,
  BLUEPRINT_PLANNED,
  BLUEPRINT_PLANNED_BY_ALLY,
};

enum class STRUCTURE_TYPE
{
  HQ,
  FACTORY,
  FACTORY_MODULE,
  POWER_GEN,
  POWER_MODULE,
  RESOURCE_EXTRACTOR,
  DEFENSE,
  WALL,
  WALL_CORNER,
  GENERIC,
  RESEARCH,
  RESEARCH_MODULE,
  REPAIR_FACILITY,
  COMMAND_CONTROL,
  BRIDGE,
  DEMOLISH,
  CYBORG_FACTORY,
  VTOL_FACTORY,
  LAB,
  REARM_PAD,
  MISSILE_SILO,
  SAT_UPLINK,
  GATE,
  LASSAT
};

enum class STRUCTURE_STRENGTH
{
  SOFT,
  MEDIUM,
  HARD,
  BUNKER
};

enum class STRUCTURE_ANIMATION_STATE
{
  NORMAL,
  OPEN,
  OPENING,
  CLOSING
};

enum class PENDING_STATUS
{
  NOTHING_PENDING,
  START_PENDING,
  HOLD_PENDING,
  CANCEL_PENDING
};

struct Structure_Bounds
{
  constexpr Structure_Bounds()
            : top_left_coords(0, 0), size_in_coords(0, 0) { }

  constexpr Structure_Bounds(const Vector2i& top_left_coords, const Vector2i& size_in_coords)
            : top_left_coords{top_left_coords}, size_in_coords{size_in_coords} { }

  Vector2i top_left_coords;
  Vector2i size_in_coords;
};

struct Flag_Position : public Object_Position
{
  Vector3i coords = Vector3i(0, 0, 0);
  uint8_t factory_inc;
  uint8_t factory_type;
};

struct Structure_Stats
{
  Vector2i size(unsigned direction) const;

  using enum STRUCTURE_TYPE;

  struct
  {
    unsigned hit_points;
    unsigned power;
    unsigned armour;
  } upgraded_stats[MAX_PLAYERS], base_stats;

  STRUCTURE_TYPE type;
  STRUCTURE_STRENGTH strength;
  std::vector< std::unique_ptr<iIMDShape> > IMDs;
  std::unique_ptr<Sensor_Stats> sensor_stats;
  std::unique_ptr<ECM_Stats> ecm_stats;
  std::unique_ptr<iIMDShape> base_imd;
  bool combines_with_wall;
  bool is_favourite;
  unsigned base_width;
  unsigned base_breadth;
  unsigned build_point_cost;
  unsigned height;
  unsigned power_to_build;
  unsigned weapon_slots;
  unsigned num_weapons_default;
};

class Structure : public virtual Unit
{
public:
  virtual ~Structure() = default;
  Structure(const Structure&) = delete;
  Structure(Structure&&) = delete;
  Structure& operator=(const Structure&) = delete;
  Structure& operator=(Structure&&) = delete;

  virtual void print_info() const = 0;
  virtual bool has_sensor() const = 0;
  virtual bool has_standard_sensor() const = 0;
  virtual bool has_CB_sensor() const = 0;
  virtual bool has_VTOL_intercept_sensor() const = 0;
  virtual bool has_VTOL_CB_sensor() const = 0;
};

namespace Impl
{
  class Structure : public virtual ::Structure, public Impl::Unit
  {
  public:
    Structure(unsigned id, unsigned player);

    bool is_blueprint() const;
    bool is_wall() const;
    bool is_radar_detector() const final;
    bool is_probably_doomed() const;
    bool is_pulled_to_terrain() const;
    bool has_modules() const;
    bool has_sensor() const final;
    bool has_standard_sensor() const final;
    bool has_CB_sensor() const final;
    bool has_VTOL_intercept_sensor() const final;
    bool has_VTOL_CB_sensor() const final;
    bool smoke_when_damaged() const;
    unsigned get_original_hp() const;
    Vector2i get_size() const;
    float get_foundation_depth() const;
    const iIMDShape& get_IMD_shape() const final;
    void update_expected_damage(const int damage);
    unsigned calculate_sensor_range() const final;
    int calculate_gate_height(const std::size_t time, const int minimum) const;
    void set_foundation_depth(const float depth);
    void print_info() const override;
  private:
    using enum STRUCTURE_STATE;
    using enum STRUCTURE_ANIMATION_STATE;
    using enum STRUCTURE_TYPE;
    using enum SENSOR_TYPE;

    STRUCTURE_STATE state;
    STRUCTURE_ANIMATION_STATE animation_state;
    std::shared_ptr<Structure_Stats> stats;
    unsigned current_build_points;
    int build_rate;
    int previous_build_rate;
    unsigned expected_damage;
    uint8_t num_modules;
    float foundation_depth;
    std::size_t last_state_time;
  };

  long count_assigned_droids(const Structure& structure);
  [[nodiscard]] bool being_built(const Structure& structure);

  [[nodiscard]] constexpr bool is_damaged(const Structure& structure)
  {
    return structure.get_hp() < structure.get_original_hp();
  }

  [[nodiscard]] constexpr Structure_Bounds get_bounds(const Structure& structure)
  {
    return Structure_Bounds{ map_coord(structure.get_position().xy()) - structure.get_size() / 2, structure.get_size() };
  }

  constexpr void adjust_tile_height(const Structure& structure, int new_height)
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

  constexpr int calculate_height(const Structure& structure)
  {
    auto& imd = structure.get_IMD_shape();
    auto height = imd.max.y + imd.min.y;
    return height - structure.calculate_gate_height(gameTime, 2);  // Treat gate as at least 2 units tall, even if open, so that it's possible to hit.
  }

  constexpr int calculate_foundation_height(const Structure& structure)
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

  constexpr void align_structure(Structure& structure)
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

  [[nodiscard]] constexpr bool target_within_range(const Structure& structure, const Unit &target)
  {
    if (num_weapons(structure) == 0) return false;

    auto max_range = get_max_weapon_range(structure);
    return object_position_square_diff(structure.get_position(), target.get_position()) < max_range * max_range &&
           target_in_line_of_fire(structure, target, 0);
  }
}

struct Production_Job
{
  std::shared_ptr<Droid_Template> droid_template;
  std::size_t time_started;
  int remaining_build_points;
};

struct Research_Item
{
  uint8_t tech_code;
  unsigned research_point_cost;
  unsigned power_cost;
};

class Factory : public virtual Structure, public Impl::Structure
{
public:
  void pause_production(QUEUE_MODE mode);
  void resume_production(QUEUE_MODE mode);
  void cancel_production(QUEUE_MODE mode);
private:
  using enum PENDING_STATUS;

  std::unique_ptr<Production_Job> active_job;
  std::unique_ptr<Flag_Position> assembly_point;
  PENDING_STATUS pending_status;
  uint8_t production_loops;
  uint8_t loops_performed;
};

class Research_Facility : public virtual Structure, public Impl::Structure
{
  Research_Item active_research_task;
  Research_Item pending_research_task;
};

class Power_Generator : public virtual Structure, public Impl::Structure
{
  std::vector<Structure*> associated_resource_extractors;
};

class Resource_Extractor : public virtual Structure, public Impl::Structure
{
  Structure* owning_power_generator;
};

class Rearm_Pad : public virtual Structure, public Impl::Structure
{
public:
  bool is_clear() const;
private:
  Droid* rearm_target;
  std::size_t time_started;
  std::size_t last_update_time;
};

class Repair_Facility : public virtual Structure, public Impl::Structure
{
  Unit* repair_target;
  std::unique_ptr<Flag_Position> assembly_point;
};


#endif // WARZONE2100_STRUCTURE_H