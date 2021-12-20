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

  bool Unit::target_in_line_of_fire(const ::Unit& target, const int weapon_slot) const
  {
    const auto distance = iHypot((target.get_position() - get_position()).xy());
    const auto range = weapons[weapon_slot].get_max_range(get_player());
    if (!has_artillery())
    {
      return range >= distance && LINE_OF_FIRE_MINIMUM <= calculate_line_of_fire(target, weapon_slot);
    }
    const auto min_angle = calculate_line_of_fire(target, weapon_slot);
    if (min_angle > DEG(PROJECTILE_MAX_PITCH)) {
      if (iSin(2 * min_angle) < iSin(2 * DEG(PROJECTILE_MAX_PITCH))) {
        range = range * iSin(2 * min_angle) / iSin(2 * DEG(PROJECTILE_MAX_PITCH));
      }
    }
    return range >= distance;
  }

  /**
   * Check fire line from psViewer to psTarget
   * psTarget can be any type of BASE_OBJECT (e.g. a tree).
   */
  int Unit::calculate_line_of_fire(const ::Simple_Object& target, int weapon_slot, bool walls_block, bool is_direct) const
  {
    Vector3i pos(0, 0, 0), dest(0, 0, 0);
    Vector2i start(0, 0), diff(0, 0), current(0, 0), halfway(0, 0), next(0, 0), part(0, 0);
    Vector3i muzzle(0, 0, 0);
    int distSq, partSq, oldPartSq;
    int64_t angletan;

    muzzle = calculate_muzzle_base_location(weapon_slot);

    pos = muzzle;
    dest = target.get_position();
    diff = (dest - pos).xy();

    distSq = dot(diff, diff);
    if (distSq == 0)
    {
      // Should never be on top of each other, but ...
      return 1000;
    }

    current = pos.xy();
    start = current;
    angletan = -1000 * 65536;
    partSq = 0;
    // run a manual trace along the line of fire until target is reached
    while (partSq < distSq)
    {
      bool hasSplitIntersection;

      oldPartSq = partSq;

      if (partSq > 0)
      {
        check_angle(angletan, partSq, calculate_map_height(current) - pos.z, distSq, dest.z - pos.z, is_direct);
      }

      // intersect current tile with line of fire
      next = diff;
      hasSplitIntersection = map_Intersect(&current.x, &current.y, &next.x, &next.y, &halfway.x, &halfway.y);

      if (hasSplitIntersection)
      {
        // check whether target was reached before tile split line:
        part = halfway - start;
        partSq = dot(part, part);

        if (partSq >= distSq)
        {
          break;
        }

        if (partSq > 0)
        {
          check_angle(angletan, partSq, calculate_map_height(halfway) - pos.z, distSq, dest.z - pos.z, is_direct);
        }
      }

      // check for walls and other structures
      // TODO: if there is a structure on the same tile as the shooter (and the shooter is not that structure) check if LOF is blocked by it.
      if (walls_block && oldPartSq > 0)
      {
        const Tile *psTile;
        halfway = current + (next - current) / 2;
        psTile = get_map_tile(map_coord(halfway.x), map_coord(halfway.y));
        if (tile_is_occupied_by_structure(*psTile) && psTile->occupying_object != target)
        {
          // check whether target was reached before tile's "half way" line
          part = halfway - start;
          partSq = dot(part, part);

          if (partSq >= distSq)
          {
            break;
          }

          // allowed to shoot over enemy structures if they are NOT the target
          if (partSq > 0)
          {
            check_angle(angletan, oldPartSq,
                        psTile->occupying_object->get_position().z + establishTargetHeight(psTile->occupying_object) - pos.z,
                        distSq, dest.z - pos.z, is_direct);
          }
          }
        }
      // next
      current = next;
      part = current - start;
      partSq = dot(part, part);
      ASSERT(partSq > oldPartSq, "areaOfFire(): no progress in tile-walk! From: %i,%i to %i,%i stuck in %i,%i", map_coord(pos.x), map_coord(pos.y), map_coord(dest.x), map_coord(dest.y), map_coord(current.x), map_coord(current.y));
    }
    if (is_direct)
    {
      return establishTargetHeight(target) - (pos.z + (angletan * iSqrt(distSq)) / 65536 - dest.z);
    }
    else
    {
      angletan = iAtan2(angletan, 65536);
      angletan = angleDelta(angletan);
      return DEG(1) + angletan;
    }
  }

  Vector3i Unit::calculate_muzzle_base_location(int weapon_slot) const
  {
    const auto& imd_shape = get_IMD_shape();
    const auto position = get_position();
    auto muzzle = Vector3i{0, 0, 0};

    if (imd_shape.nconnectors)
    {
      Affine3F af;
      const auto rotation = get_rotation();
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
      muzzle = position + Vector3i{0, 0, get_display_data().imd_shape->max.y};
    }
    return muzzle;
  }

  Vector3i Unit::calculate_muzzle_tip_location(int weapon_slot) const
  {
    const auto& imd_shape = get_IMD_shape();
    const auto& weapon = weapons[weapon_slot];
    const auto position = get_position();
    const auto rotation = get_rotation();
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
      muzzle = position + Vector3i{0, 0, 0 + get_display_data().imd_shape->max.y};
    }
    return muzzle;
  }

  unsigned Unit::num_weapons() const
  {
    return static_cast<unsigned>(weapons.size());
  }

  unsigned Unit::get_max_weapon_range() const
  {
    auto max = 0;
    for (const auto& weapon : weapons)
    {
      const auto max_weapon_range = weapon.get_max_range(get_player());
      if (max_weapon_range > max)
        max = max_weapon_range;
    }
    return max;
  }

  const std::vector<Weapon>& Unit::get_weapons() const
  {
    return weapons;
  }

  void Unit::align_turret(int weapon_slot)
  {
    if (num_weapons() == 0) return;

    const auto turret_rotation = gameTimeAdjustedIncrement(DEG(TURRET_ROTATION_RATE));
    auto unit_rotation = weapons[weapon_slot].get_rotation().direction;
    auto unit_pitch = weapons[weapon_slot].get_rotation().pitch;
    const auto nearest_right_angle = (unit_rotation + DEG(45)) / DEG(90) * DEG(90);

    unit_rotation += clip(angleDelta(nearest_right_angle - unit_rotation), -turret_rotation / 2, turret_rotation / 2);
    unit_pitch += clip(angleDelta(0 - unit_pitch), -turret_rotation / 2, turret_rotation / 2);

    set_rotation({unit_rotation, unit_pitch, get_rotation().roll});
  }
}
