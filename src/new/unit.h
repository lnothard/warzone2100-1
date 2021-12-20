//
// Created by luna on 08/12/2021.
//

#ifndef WARZONE2100_UNIT_H
#define WARZONE2100_UNIT_H

#include <vector>

#include "lib/ivis_opengl/ivisdef.h"
#include "lib/framework/fixedpoint.h"
#include "lib/framework/math_ext.h"
#include "basedef.h"
#include "map.h"
#include "projectile.h"
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
  virtual bool is_radar_detector() const = 0;
  virtual uint8_t is_target_visible(const Simple_Object* target, bool walls_block) const = 0;
  virtual unsigned get_hp() const = 0;
  virtual unsigned calculate_sensor_range() const = 0;
  virtual const std::vector<Weapon>& get_weapons() const = 0;
  virtual const iIMDShape& get_IMD_shape() const = 0;
};

constexpr void check_angle(int64_t& angle_tan, int start_coord, int height, int square_distance, int target_height, bool is_direct)
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

Vector3i calculate_muzzle_base_location(const Unit& unit, int weapon_slot);
Vector3i calculate_muzzle_tip_location(const Unit& unit, int weapon_slot);

namespace Impl
{
  class Unit : public virtual ::Unit, public Impl::Simple_Object
  {
  public:
    Unit(uint32_t id, uint32_t player);

    [[nodiscard]] bool is_alive() const final;
    [[nodiscard]] unsigned get_hp() const final;
    [[nodiscard]] const std::vector<Weapon>& get_weapons() const final;
  private:
    unsigned hit_points { 0 };
    std::vector<Weapon> weapons { 0 };
  };

  [[nodiscard]] constexpr bool has_full_ammo(const Unit& unit) noexcept
  {
    auto& weapons = unit.get_weapons();
    return std::all_of(weapons.begin(), weapons.end(),
                       [](const auto& weapon){ return weapon.has_full_ammo(); });
  }

  [[nodiscard]] constexpr bool has_artillery(const Unit& unit) noexcept
  {
    auto& weapons = unit.get_weapons();
    return std::any_of(weapons.begin(), weapons.end(), [] (const auto& weapon) {
        return weapon.is_artillery();
    });
  }

  [[nodiscard]] constexpr bool has_electronic_weapon(const Unit& unit) noexcept
  {
    auto& weapons = unit.get_weapons();
    if (weapons.size() == 0) return false;

    return std::any_of(weapons.begin(), weapons.end(), [] (const auto& weapon) {
        return weapon.get_subclass() == WEAPON_SUBCLASS::ELECTRONIC;
    });
  }

  /**
   * Check fire line from psViewer to psTarget
   * psTarget can be any type of BASE_OBJECT (e.g. a tree).
   */
  constexpr int calculate_line_of_fire(const Unit& unit, const ::Simple_Object& target, int weapon_slot, bool walls_block, bool is_direct)
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

  [[nodiscard]] constexpr bool target_in_line_of_fire(const Unit& unit, const ::Unit& target, int weapon_slot)
  {
    auto distance = iHypot((target.get_position() - unit.get_position()).xy());
    auto range = unit.get_weapons()[weapon_slot].get_max_range(unit.get_player());
    if (!has_artillery(unit))
    {
      return range >= distance && LINE_OF_FIRE_MINIMUM <= unit.calculate_line_of_fire(target, weapon_slot);
    }
    auto min_angle = unit.calculate_line_of_fire(target, weapon_slot);
    if (min_angle > DEG(PROJECTILE_MAX_PITCH)) {
      if (iSin(2 * min_angle) < iSin(2 * DEG(PROJECTILE_MAX_PITCH))) {
        range = range * iSin(2 * min_angle) / iSin(2 * DEG(PROJECTILE_MAX_PITCH));
      }
    }
    return range >= distance;
  }

  [[nodiscard]] constexpr unsigned num_weapons(const Unit& unit)
  {
    return static_cast<unsigned>(unit.get_weapons().size());
  }

  constexpr void align_turret(const Unit& unit, int weapon_slot)
  {
    if (num_weapons(unit) == 0) return;
    auto& weapon = unit.get_weapons()[weapon_slot];

    auto turret_rotation = gameTimeAdjustedIncrement(DEG(TURRET_ROTATION_RATE));
    auto weapon_rotation = weapon.get_rotation().direction;
    auto weapon_pitch = weapon.get_rotation().pitch;
    auto nearest_right_angle = (weapon_rotation + DEG(45)) / DEG(90) * DEG(90);

    weapon_rotation += clip(angleDelta(nearest_right_angle - weapon_rotation), -turret_rotation / 2, turret_rotation / 2);
    weapon_pitch += clip(angleDelta(0 - weapon_pitch), -turret_rotation / 2, turret_rotation / 2);

    weapon.set_rotation({weapon_rotation, weapon_pitch, weapon.get_rotation().roll});
  }

  [[nodiscard]] constexpr unsigned get_max_weapon_range(const Unit& unit)
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

}
#endif // WARZONE2100_UNIT_H