//
// Created by luna on 08/12/2021.
//

#include <algorithm>

#include "lib/framework/fixedpoint.h"
#include "lib/framework/geometry.h"
#include "projectile.h"
#include "unit.h"

namespace Impl
{
  Unit::Unit(uint32_t id, uint32_t player)
  : Impl::Simple_Object(id, player)
  {
  }

  uint32_t Unit::get_hp() const
  {
    return hit_points;
  }

  bool Unit::has_full_ammo() const
  {
    return std::all_of(weapons.begin(), weapons.end(),
                       [](const auto& w){ return w.has_full_ammo(); });
  }

  bool Unit::has_artillery() const
  {
    return std::any_of(weapons.begin(), weapons.end(), [] (const auto& weapon) {
      return weapon.is_artillery();
    });
  }

  bool Unit::has_electronic_weapon() const
  {
    if (weapons.size() == 0) return false;

    return std::any_of(weapons.begin(), weapons.end(), [] (const auto& weapon) {
      return weapon.get_subclass() == WEAPON_SUBCLASS::ELECTRONIC;
    });
  }

  bool Unit::target_in_line_of_fire(const ::Unit& target) const
  {
    auto distance = iHypot((target.get_position() - get_position()).xy());
    auto range = get_max_weapon_range();
    if (!has_artillery())
    {
      return range >= distance && LINE_OF_FIRE_MINIMUM <= calculate_line_of_fire(target);
    }
    auto min_angle = calculate_line_of_fire(target);
    if (min_angle > DEG(PROJECTILE_MAX_PITCH)) {
      if (iSin(2 * min_angle) < iSin(2 * DEG(PROJECTILE_MAX_PITCH))) {
        range = range * iSin(2 * min_angle) / iSin(2 * DEG(PROJECTILE_MAX_PITCH));
      }
    }
    return range >= distance;
  }

  int Unit::calculate_line_of_fire(const ::Simple_Object& target) const
  {

  }

  Vector3i Unit::calculate_muzzle_base_location(int weapon_slot) const
  {
    const auto& imd_shape = get_IMD_shape();
    auto position = get_position();
    auto muzzle = Vector3i{0, 0, 0};

    if (imd_shape.nconnectors)
    {
      Affine3F af;
      auto rotation = get_rotation();
      af.Trans(position.x, -position.z, position.y);
      af.RotY(rotation.direction);
      af.RotX(rotation.pitch);
      af.RotZ(-rotation.roll);
      af.Trans(imd_shape.connectors[weapon_slot].x, -imd_shape.connectors[weapon_slot].z,
               -imd_shape.connectors[weapon_slot].y);

      auto barrel = Vector3i{0, 0, 0};
      muzzle = (af * barrel).xzy();
      muzzle.z = -muzzle.z;
    }
    else
    {
       muzzle = position + Vector3i{0, 0, get_display_data().imd_shape->max.y};
    }
    return muzzle;
  }

  Vector3i Unit::calculate_muzzle_tip_location(int weapon_slot) const
  {
    const auto& imd_shape = get_IMD_shape();

    if (imd_shape.nconnectors)
    {
      auto weapon_stats = weapons[weapon_slot].stats;
    }
  }

  uint16_t Unit::num_weapons() const
  {
    return static_cast<uint16_t>(weapons.size());
  }

  uint32_t Unit::get_max_weapon_range() const
  {
    auto max = 0;
    for (const auto& weapon : weapons)
    {
      auto max_weapon_range = weapon.get_max_range(get_player());
      if (max_weapon_range > max)
        max = max_weapon_range;
    }
    return max;
  }

  const std::vector<Weapon>& Unit::get_weapons() const
  {
    return weapons;
  }
}