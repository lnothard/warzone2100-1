//
// Created by luna on 08/12/2021.
//

#ifndef WARZONE2100_STRUCTURE_H
#define WARZONE2100_STRUCTURE_H

#include "lib/ivis_opengl/ivisdef.h"
#include "droid.h"
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

class Structure_Bounds
{
public:
  Structure_Bounds();
  Structure_Bounds(const Vector2i& top_left_coords, const Vector2i& size_in_coords);
private:
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
  Vector2i size(uint16_t direction) const;

  using enum STRUCTURE_TYPE;

  struct
  {
    uint32_t hit_points;
    uint32_t power;
    uint32_t armour;
  } upgraded_stats[MAX_PLAYERS], base_stats;

  STRUCTURE_TYPE type;
  STRUCTURE_STRENGTH strength;
  std::vector< std::unique_ptr<iIMDShape> > IMDs;
  std::unique_ptr<Sensor_Stats> sensor_stats;
  std::unique_ptr<ECM_Stats> ecm_stats;
  std::unique_ptr<iIMDShape> base_imd;
  bool combines_with_wall;
  bool is_favourite;
  uint32_t base_width;
  uint32_t base_breadth;
  uint32_t build_point_cost;
  uint32_t height;
  uint32_t power_to_build;
  uint32_t weapon_slots;
  uint32_t num_weapons_default;
};

class Structure : public virtual Unit
{
public:
  virtual ~Structure();

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
    Structure(uint32_t id, uint32_t player);

    bool is_blueprint() const;
    bool is_wall() const;
    bool is_probably_doomed() const;
    bool is_pulled_to_terrain() const;
    bool is_damaged() const;
    bool has_modules() const;
    bool has_sensor() const final;
    bool has_standard_sensor() const final;
    bool has_CB_sensor() const final;
    bool has_VTOL_intercept_sensor() const final;
    bool has_VTOL_CB_sensor() const final;
    bool smoke_when_damaged() const;
    uint16_t count_assigned_droids() const;
    uint32_t get_original_hp() const;
    Vector2i get_size() const;
    const iIMDShape& get_IMD_shape() const final;
    Structure_Bounds get_bounds() const;
    void update_expected_damage(const int32_t damage);
    int calculate_sensor_range() const final;
    bool target_within_range(const Unit& target) const;
    int calculate_gate_height(const uint32_t time, const int minimum) const;
    int calculate_height() const final;
  private:
    using enum STRUCTURE_STATE;
    using enum STRUCTURE_ANIMATION_STATE;
    using enum STRUCTURE_TYPE;
    using enum SENSOR_TYPE;

    STRUCTURE_STATE state;
    STRUCTURE_ANIMATION_STATE animation_state;
    Structure_Stats stats;
    uint32_t current_build_points;
    int build_rate;
    uint32_t expected_damage;
    uint8_t num_modules;
    float foundation_depth;
    uint32_t last_state_time;
  };

  static inline int calculate_foundation_height(const Structure& structure);
}

struct Production_Job
{
  Droid_Template droid_template;
  uint32_t time_started;
  int remaining_build_points;
};

struct Research_Item
{
  uint8_t tech_code;
  uint16_t research_point_cost;
  uint32_t power_cost;
};

class Factory : public virtual Structure, public Impl::Structure
{
public:
  void pause_production(QUEUE_MODE mode);
  void unpause_production(QUEUE_MODE mode);
  void cancel_production(QUEUE_MODE mode);
private:
  using enum PENDING_STATUS;

  Production_Job active_job;
  Flag_Position assembly_point;
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
  uint32_t time_started;
  uint32_t last_update_time;
};

class Repair_Facility : public virtual Structure, public Impl::Structure
{
  Unit* repair_target;
  Flag_Position assembly_point;
};


#endif // WARZONE2100_STRUCTURE_H