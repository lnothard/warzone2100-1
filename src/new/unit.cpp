//
// Created by luna on 08/12/2021.
//

#include <algorithm>

#include "lib/framework/fixedpoint.h"
#include "lib/framework/geometry.h"
#include "lib/framework/math_ext.h"
#include "map.h"
#include "projectile.h"
#include "unit.h"

namespace Impl
{
  Unit::Unit(unsigned id, unsigned player)
  : Impl::Simple_Object(id, player) { }

  unsigned Unit::get_hp() const noexcept
  {
    return hit_points;
  }

  const std::vector<Weapon>& Unit::get_weapons() const
  {
    return weapons;
  }

  void Unit::align_turret(int weapon_slot)
  {
    if (num_weapons(*this) == 0) return;
    auto& weapon = weapons[weapon_slot];

    auto turret_rotation = gameTimeAdjustedIncrement(DEG(TURRET_ROTATION_RATE));
    auto weapon_rotation = weapon.get_rotation().direction;
    auto weapon_pitch = weapon.get_rotation().pitch;
    auto nearest_right_angle = (weapon_rotation + DEG(45)) / DEG(90) * DEG(90);

    weapon_rotation += clip(angleDelta(nearest_right_angle - weapon_rotation), -turret_rotation / 2, turret_rotation / 2);
    weapon_pitch += clip(angleDelta(0 - weapon_pitch), -turret_rotation / 2, turret_rotation / 2);

    weapon.set_rotation({weapon_rotation, weapon_pitch, weapon.get_rotation().roll});
  }
}

bool has_full_ammo(const Unit& unit) noexcept
{
  auto& weapons = unit.get_weapons();
  return std::all_of(weapons.begin(), weapons.end(),
                     [](const auto& weapon){ return weapon.has_full_ammo(); });
}

bool has_artillery(const Unit& unit) noexcept
{
  auto& weapons = unit.get_weapons();
  return std::any_of(weapons.begin(), weapons.end(), [] (const auto& weapon) {
    return weapon.is_artillery();
  });
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

void check_angle(int64_t& angle_tan, int start_coord, int height, int square_distance, int target_height, bool is_direct)
{
  int64_t current_angle = 0;

  if (is_direct)
  {
    current_angle = (65536 * height) / iSqrt(start_coord);
  }
  else
  {
    auto distance = iSqrt(square_distance);
    auto position = iSqrt(start_coord);
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
/**
 * Check fire line from psViewer to psTarget
 * psTarget can be any type of BASE_OBJECT (e.g. a tree).
 */
int calculate_line_of_fire(const Unit& unit, const ::Simple_Object& target, int weapon_slot, bool walls_block, bool is_direct)
{
  Vector3i pos(0, 0, 0), dest(0, 0, 0);
  Vector2i start(0, 0), diff(0, 0), current(0, 0), halfway(0, 0), next(0, 0), part(0, 0);
  Vector3i muzzle(0, 0, 0);
  int distSq, partSq, oldPartSq;
  int64_t angletan;

  muzzle = calculate_muzzle_base_location(unit, weapon_slot);

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

bool has_electronic_weapon(const Unit& unit) noexcept
{
  auto& weapons = unit.get_weapons();
  if (weapons.size() == 0) return false;

  return std::any_of(weapons.begin(), weapons.end(), [] (const auto& weapon) {
      return weapon.get_subclass() == WEAPON_SUBCLASS::ELECTRONIC;
  });
}

bool target_in_line_of_fire(const Unit& unit, const ::Unit& target, int weapon_slot)
{
  auto distance = iHypot((target.get_position() - unit.get_position()).xy());
  auto range = unit.get_weapons()[weapon_slot].get_max_range(unit.get_player());
  if (!has_artillery(unit))
  {
    return range >= distance && LINE_OF_FIRE_MINIMUM <= calculate_line_of_fire(unit, target, weapon_slot);
  }
  auto min_angle = calculate_line_of_fire(unit, target, weapon_slot);
  if (min_angle > DEG(PROJECTILE_MAX_PITCH)) {
    if (iSin(2 * min_angle) < iSin(2 * DEG(PROJECTILE_MAX_PITCH))) {
      range = range * iSin(2 * min_angle) / iSin(2 * DEG(PROJECTILE_MAX_PITCH));
    }
  }
  return range >= distance;
}

unsigned num_weapons(const Unit& unit)
{
  return static_cast<unsigned>(unit.get_weapons().size());
}


unsigned get_max_weapon_range(const Unit& unit)
{
  auto max = 0;
  for (const auto& weapon : unit.get_weapons())
  {
    const auto max_weapon_range = weapon.get_max_range(unit.get_player());
    if (max_weapon_range > max)
      max = max_weapon_range;
  }
  return max;
}
