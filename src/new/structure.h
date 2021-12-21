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

    [[nodiscard]] bool is_blueprint() const noexcept;
    [[nodiscard]] bool is_wall() const noexcept;
    [[nodiscard]] bool is_radar_detector() const final;
    [[nodiscard]] bool is_probably_doomed() const;
    [[nodiscard]] bool is_pulled_to_terrain() const;
    [[nodiscard]] bool has_modules() const noexcept;
    [[nodiscard]] bool has_sensor() const final;
    [[nodiscard]] bool has_standard_sensor() const final;
    [[nodiscard]] bool has_CB_sensor() const final;
    [[nodiscard]] bool has_VTOL_intercept_sensor() const final;
    [[nodiscard]] bool has_VTOL_CB_sensor() const final;
    [[nodiscard]] bool smoke_when_damaged() const noexcept;
    [[nodiscard]] unsigned get_original_hp() const;
    [[nodiscard]] Vector2i get_size() const;
    [[nodiscard]] float get_foundation_depth() const noexcept;
    [[nodiscard]] const iIMDShape& get_IMD_shape() const final;
    void update_expected_damage(const int damage);
    [[nodiscard]] unsigned calculate_sensor_range() const final;
    [[nodiscard]] int calculate_gate_height(const std::size_t time, const int minimum) const;
    void set_foundation_depth(float depth) noexcept;
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

  [[nodiscard]] unsigned count_assigned_droids(const Structure& structure);
  [[nodiscard]] bool being_built(const Structure& structure);
  [[nodiscard]] bool is_damaged(const Structure& structure);
  [[nodiscard]] Structure_Bounds get_bounds(const Structure& structure) noexcept;
  void adjust_tile_height(const Structure& structure, int new_height);
  [[nodiscard]] int calculate_height(const Structure& structure);
  [[nodiscard]] int calculate_foundation_height(const Structure& structure);
  void align_structure(Structure& structure);
  [[nodiscard]] bool target_within_range(const Structure& structure, const Unit& unit, int weapon_slot);
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
private:
  Research_Item active_research_task;
  Research_Item pending_research_task;
};

class Power_Generator : public virtual Structure, public Impl::Structure
{
private:
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
private:
  Unit* repair_target;
  std::unique_ptr<Flag_Position> assembly_point;
};


#endif // WARZONE2100_STRUCTURE_H