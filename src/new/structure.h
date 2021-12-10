//
// Created by luna on 08/12/2021.
//

#ifndef WARZONE2100_STRUCTURE_H
#define WARZONE2100_STRUCTURE_H

#include "unit.h"
#include "droid.h"

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

class Structure_Bounds
{
public:
  Structure_Bounds();
  Structure_Bounds(const Vector2i& top_left_coords, const Vector2i& size_in_coords);
private:
  Vector2i top_left_coords;
  Vector2i size_in_coords;
};

struct Structure_Stats
{
  Vector2i size(uint16_t direction) const;

  using enum STRUCTURE_TYPE;

  STRUCTURE_TYPE type;
  STRUCTURE_STRENGTH strength;
  bool combines_with_wall;
  bool is_favourite;
  uint32_t base_width;
  uint32_t base_breadth;
  uint32_t build_points_required;
  uint32_t height;
  uint32_t power_to_build;
  uint32_t weapon_slots;
  uint32_t num_weapons_default;
};

class Structure : public virtual Unit
{
public:
  virtual ~Structure();
};

namespace Impl
{
  class Structure : public virtual ::Structure, public Impl::Unit
  {
  public:
    bool is_blueprint() const;
    bool is_wall() const;
    bool is_probably_doomed() const;
    uint16_t count_assigned_droids() const;
  private:
    using enum STRUCTURE_STATE;
    using enum STRUCTURE_TYPE;

    STRUCTURE_STATE state;
    Structure_Stats stats;
    uint32_t        expected_damage;
  };
}

struct Production_Job
{
  Droid_Template droid_template;
  uint32_t       time_started;
  int            points_remaining;
};

class Factory : public virtual Structure, public Impl::Structure
{
private:
  Production_Job active_job;
};

#endif // WARZONE2100_STRUCTURE_H