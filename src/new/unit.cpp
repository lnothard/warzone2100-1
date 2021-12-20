//
// Created by luna on 08/12/2021.
//

#include <algorithm>

#include "lib/framework/fixedpoint.h"
#include "lib/framework/geometry.h"
#include "lib/framework/math_ext.h"
#include "projectile.h"
#include "unit.h"

namespace Impl
{
  Unit::Unit(unsigned id, unsigned player)
  : Impl::Simple_Object(id, player)
  {
  }

  unsigned Unit::get_hp() const
  {
    return hit_points;
  }

  const std::vector<Weapon>& Unit::get_weapons() const
  {
    return weapons;
  }

  Vector3i calculate_muzzle_base_location(const Unit& unit, int weapon_slot)
  {
    auto& imd_shape = unit.get_IMD_shape();
    auto position = unit.get_position();
    auto muzzle = Vector3i{0, 0, 0};

    if (imd_shape.nconnectors)
    {
      Affine3F af;
      auto rotation = unit.get_rotation();
      af.Trans(position.x, -position.z, position.y);
      af.RotY(rotation.direction);
      af.RotX(rotation.pitch);
      af.RotZ(-rotation.roll);
      af.Trans(imd_shape.connectors[weapon_slot].x, -imd_shape.connectors[weapon_slot].z,
               -imd_shape.connectors[weapon_slot].y);

      const auto barrel = Vector3i{0, 0, 0};
      muzzle = (af * barrel).xzy();
      muzzle.z = -muzzle.z;
    }
    else
    {
      muzzle = position + Vector3i{0, 0, unit.get_display_data().imd_shape->max.y};
    }
    return muzzle;
  }

  Vector3i calculate_muzzle_tip_location(const Unit& unit, int weapon_slot)
  {
    const auto& imd_shape = unit.get_IMD_shape();
    const auto& weapon = unit.get_weapons()[weapon_slot];
    const auto& position = unit.get_position();
    const auto& rotation = unit.get_rotation();
    auto muzzle = Vector3i{0, 0, 0};

    if (imd_shape.nconnectors)
    {
      auto barrel = Vector3i{0, 0, 0};
      const auto weapon_imd = weapon.get_IMD_shape();
      const auto mount_imd = weapon.get_mount_graphic();

      Affine3F af;
      af.Trans(position.x, -position.z, position.y);
      af.RotY(rotation.direction);
      af.RotX(rotation.pitch);
      af.RotZ(-rotation.roll);
      af.Trans(imd_shape.connectors[weapon_slot].x, -imd_shape.connectors[weapon_slot].z,
               -imd_shape.connectors[weapon_slot].y);

      af.RotY(weapon.get_rotation().direction);

      if (mount_imd.nconnectors)
      {
        af.Trans(mount_imd.connectors->x, -mount_imd.connectors->z, -mount_imd.connectors->y);
      }
      af.RotX(weapon.get_rotation().pitch);

      if (weapon_imd.nconnectors)
      {
        auto connector_num = 0;
        if (weapon.get_shots_fired() && weapon_imd.nconnectors > 1)
        {
          connector_num = (weapon.get_shots_fired() - 1) % weapon_imd.nconnectors;
        }
        const auto connector = weapon_imd.connectors[connector_num];
        barrel = Vector3i{connector.x, -connector.z, -connector.y};
      }
      muzzle = (af * barrel).xzy();
      muzzle.z = -muzzle.z;
    }
    else
    {
      muzzle = position + Vector3i{0, 0, 0 + unit.get_display_data().imd_shape->max.y};
    }
    return muzzle;
  }
}