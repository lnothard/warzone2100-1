//
// Created by luna on 08/12/2021.
//

#ifndef WARZONE2100_UNIT_H
#define WARZONE2100_UNIT_H

#include <vector>

#include "lib/ivis_opengl/ivisdef.h"
#include "basedef.h"
#include "map.h"
#include "weapon.h"

static constexpr auto LINE_OF_FIRE_MINIMUM = 5;
static constexpr auto TURRET_ROTATION_RATE = 45;

class Unit : public virtual ::Simple_Object
{
public:
  Unit() = default;
  virtual ~Unit() = default;
  Unit(const Unit&) = delete;
  Unit(Unit&&) = delete;
  Unit& operator=(const Unit&) = delete;
  Unit& operator=(Unit&&) = delete;

  virtual bool is_alive() const = 0;
  virtual bool has_electronic_weapon() const = 0;
  virtual bool target_in_line_of_fire(const Unit& target, const int weapon_slot) const = 0;
  virtual bool is_radar_detector() const = 0;
  virtual uint8_t is_target_visible(const Simple_Object* target, bool walls_block) const = 0;
  virtual unsigned get_hp() const = 0;
  virtual int calculate_line_of_fire(const Simple_Object& target, const int weapon_slot, bool walls_block, bool is_direct) const = 0;
  virtual int calculate_sensor_range() const = 0;
  virtual unsigned get_max_weapon_range() const = 0;
  virtual Vector3i calculate_muzzle_base_location(const int weapon_slot) const = 0;
  virtual Vector3i calculate_muzzle_tip_location(const int weapon_slot) const = 0;
  virtual const std::vector<Weapon>& get_weapons() const = 0;
  virtual const iIMDShape& get_IMD_shape() const = 0;
  virtual void align_turret(int weapon_slot) = 0;
};

namespace Impl
{
  class Unit : public virtual ::Unit, public Impl::Simple_Object
  {
  public:
    Unit(uint32_t id, uint32_t player);

    [[nodiscard]] bool is_alive() const final;
    [[nodiscard]] bool has_electronic_weapon() const override;
    bool target_in_line_of_fire(const ::Unit& target, const int weapon_slot) const final;
    [[nodiscard]] bool has_full_ammo() const;
    [[nodiscard]] bool has_artillery() const;
    int calculate_line_of_fire(const ::Simple_Object& target, const int weapon_slot, bool walls_block, bool is_direct) const final;
    Vector3i calculate_muzzle_base_location(const int weapon_slot) const final;
    Vector3i calculate_muzzle_tip_location(const int weapon_slot) const final;
    [[nodiscard]] unsigned get_hp() const final;
    [[nodiscard]] unsigned get_max_weapon_range() const final;
    [[nodiscard]] const std::vector<Weapon>& get_weapons() const final;
    [[nodiscard]] unsigned num_weapons() const;
    void align_turret(int weapon_slot) final;
  private:
    unsigned hit_points { 0 };
    std::vector<Weapon> weapons { 0 };
  };
}

inline void check_angle(int64_t& angle_tan, int start_coord, int height, int square_distance, int target_height, bool is_direct)
{
  int64_t current_angle = 0;

  if (is_direct)
  {
    current_angle = (65536 * height) / iSqrt(start_coord);
  }
  else
  {
    const auto distance = iSqrt(square_distance);
    const auto position = iSqrt(start_coord);
    current_angle = (position * target_height) / distance;

    if (current_angle < height && position > TILE_UNITS / 2 && position < distance - TILE_UNITS / 2)
    {
      current_angle = (65536 * square_distance * height - start_coord * target_height)
                      / (square_distance * position - distance * start_coord);
    }
    else
    {
      current_angle = 0;
    }
  }
  angle_tan = std::max(angle_tan, current_angle);
}
#endif // WARZONE2100_UNIT_H