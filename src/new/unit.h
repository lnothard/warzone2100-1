//
// Created by luna on 08/12/2021.
//

#ifndef WARZONE2100_UNIT_H
#define WARZONE2100_UNIT_H

#include <vector>

#include "lib/ivis_opengl/ivisdef.h"
#include "basedef.h"
#include "weapon.h"

constexpr auto LINE_OF_FIRE_MINIMUM { 5 };

class Unit : public virtual ::Simple_Object
{
public:
  virtual ~Unit() = default;

  virtual bool is_alive() const = 0;
  virtual bool has_electronic_weapon() const = 0;
  virtual bool target_in_line_of_fire(const Unit& target, const int weapon_slot) const = 0;
  virtual uint32_t get_hp() const = 0;
  virtual int calculate_line_of_fire(const Simple_Object& target, const int weapon_slot, bool walls_block, bool is_direct) const = 0;
  virtual int calculate_sensor_range() const = 0;
  virtual uint32_t get_max_weapon_range() const = 0;
  virtual Vector3i calculate_muzzle_base_location(const int weapon_slot) const = 0;
  virtual Vector3i calculate_muzzle_tip_location(const int weapon_slot) const = 0;
  virtual const std::vector<Weapon>& get_weapons() const = 0;
  virtual const iIMDShape& get_IMD_shape() const = 0;
};

namespace Impl
{
  class Unit : public virtual ::Unit, public Impl::Simple_Object
  {
  public:
    Unit(uint32_t id, uint32_t player);

    bool is_alive() const final;
    bool has_electronic_weapon() const final;
    bool target_in_line_of_fire(const ::Unit& target, const int weapon_slot) const final;
    bool has_full_ammo() const;
    bool has_artillery() const;
    int calculate_line_of_fire(const ::Simple_Object& target, const int weapon_slot, bool walls_block, bool is_direct) const final;
    Vector3i calculate_muzzle_base_location(const int weapon_slot) const final;
    Vector3i calculate_muzzle_tip_location(const int weapon_slot) const final;
    uint32_t get_hp() const final;
    uint32_t get_max_weapon_range() const final;
    const std::vector<Weapon>& get_weapons() const final;
    uint16_t num_weapons() const;
  private:
    uint32_t hit_points { 0 };
    std::vector<Weapon> weapons { 0 };
  };
}

static inline void check_angle(int64_t& angle_tan, int start_coord, int height, int square_distance, int target_height, bool is_direct);
#endif // WARZONE2100_UNIT_H