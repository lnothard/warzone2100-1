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
		: Impl::SimpleObject(id, player)
	{
	}

	unsigned Unit::getHp() const noexcept
	{
		return hitPoints;
	}

  int Unit::getResistance() const
  {
    return resistance;
  }

  void Unit::setHp(unsigned hp)
  {
    hitPoints = hp;
  }

	const std::vector<Weapon>& Unit::getWeapons() const
	{
		return weapons;
	}

	void Unit::alignTurret(int weapon_slot)
	{
		if (numWeapons(*this) == 0)  {
      return;
    }
		auto& weapon = weapons[weapon_slot];
		const auto turret_rotation = gameTimeAdjustedIncrement(DEG(TURRET_ROTATION_RATE));
		auto weapon_rotation = weapon.getRotation().direction;
		auto weapon_pitch = weapon.getRotation().pitch;
		const auto nearest_right_angle = (weapon_rotation + DEG(45)) / DEG(90) * DEG(90);

		weapon_rotation += clip(angleDelta(nearest_right_angle - weapon_rotation), -turret_rotation / 2,
		                        turret_rotation / 2);
		weapon_pitch += clip(angleDelta(0 - weapon_pitch), -turret_rotation / 2, turret_rotation / 2);

    weapon.setRotation({weapon_rotation, weapon_pitch, weapon.getRotation().roll});
	}

  bool Unit::isSelected() const noexcept
  {
    return selected;
  }
}

bool hasFullAmmo(const Unit& unit) noexcept
{
	auto& weapons = unit.getWeapons();
	return std::all_of(weapons.begin(), weapons.end(),
	                   [](const auto& weapon)
  {
       return weapon.hasFullAmmo();
  });
}

bool hasArtillery(const Unit& unit) noexcept
{
	auto& weapons = unit.getWeapons();
	return std::any_of(weapons.begin(), weapons.end(),
                     [](const auto& weapon)
	{
		return weapon.isArtillery();
	});
}

Vector3i calculateMuzzleBaseLocation(const Unit& unit, int weapon_slot)
{
	auto& imd_shape = unit.getImdShape();
	const auto position = unit.getPosition();
	auto muzzle = Vector3i{0, 0, 0};

	if (imd_shape.nconnectors)
	{
		Affine3F af;
		auto rotation = unit.getRotation();
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
		muzzle = position + Vector3i{0, 0, unit.getDisplayData().imd_shape->max.y};
	}
	return muzzle;
}

Vector3i calculateMuzzleTipLocation(const Unit& unit, int weapon_slot)
{
	const auto& imd_shape = unit.getImdShape();
	const auto& weapon = unit.getWeapons()[weapon_slot];
	const auto& position = unit.getPosition();
	const auto& rotation = unit.getRotation();
	auto muzzle = Vector3i{0, 0, 0};

	if (imd_shape.nconnectors)
	{
		auto barrel = Vector3i{0, 0, 0};
		const auto weapon_imd = weapon.getImdShape();
		const auto mount_imd = weapon.getMountGraphic();

		Affine3F af;
		af.Trans(position.x, -position.z, position.y);
		af.RotY(rotation.direction);
		af.RotX(rotation.pitch);
		af.RotZ(-rotation.roll);
		af.Trans(imd_shape.connectors[weapon_slot].x, -imd_shape.connectors[weapon_slot].z,
		         -imd_shape.connectors[weapon_slot].y);

		af.RotY(weapon.getRotation().direction);

		if (mount_imd.nconnectors)
		{
			af.Trans(mount_imd.connectors->x, -mount_imd.connectors->z, -mount_imd.connectors->y);
		}
		af.RotX(weapon.getRotation().pitch);

		if (weapon_imd.nconnectors)
		{
			auto connector_num = unsigned{0};
			if (weapon.getShotsFired() && weapon_imd.nconnectors > 1)
			{
				connector_num = (weapon.getShotsFired() - 1) % weapon_imd.nconnectors;
			}
			const auto connector = weapon_imd.connectors[connector_num];
			barrel = Vector3i{connector.x, -connector.z, -connector.y};
		}
		muzzle = (af * barrel).xzy();
		muzzle.z = -muzzle.z;
	}
	else
	{
		muzzle = position + Vector3i{0, 0, 0 + unit.getDisplayData().imd_shape->max.y};
	}
	return muzzle;
}

void checkAngle(int64_t& angle_tan, int start_coord, int height,
                int square_distance, int target_height, bool is_direct)
{
	auto current_angle = int64_t{0};

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

/**
 * Check fire line from psViewer to psTarget
 * psTarget can be any type of SimpleObject (e.g. a tree).
 */
int calculateLineOfFire(const Unit& unit, const ::SimpleObject& target, int weapon_slot, bool walls_block,
                        bool is_direct)
{
	Vector3i pos(0, 0, 0), dest(0, 0, 0);
	Vector2i start(0, 0), diff(0, 0), current(0, 0), halfway(0, 0), next(0, 0), part(0, 0);
	Vector3i muzzle(0, 0, 0);
	int distSq, partSq, oldPartSq;
	int64_t angletan;

	muzzle = calculateMuzzleBaseLocation(unit, weapon_slot);

	pos = muzzle;
	dest = target.getPosition();
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
      checkAngle(angletan, partSq, calculate_map_height(current) - pos.z, distSq, dest.z - pos.z, is_direct);
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
        checkAngle(angletan, partSq, calculate_map_height(halfway) - pos.z, distSq, dest.z - pos.z, is_direct);
			}
		}

		// check for walls and other structures
		// TODO: if there is a structure on the same tile as the shooter (and the shooter is not that structure) check if LOF is blocked by it.
		if (walls_block && oldPartSq > 0)
		{
			const Tile* psTile;
			halfway = current + (next - current) / 2;
			psTile = mapTile(map_coord(halfway.x), map_coord(halfway.y));
			if (tile_is_occupied_by_structure(*psTile) &&
          psTile->occupying_object != &target)
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
          checkAngle(angletan, oldPartSq,
                     psTile->occupying_object->getPosition().z + establishTargetHeight(
                             psTile->occupying_object) - pos.z,
                     distSq, dest.z - pos.z, is_direct);
				}
			}
		}
		// next
		current = next;
		part = current - start;
		partSq = dot(part, part);
		ASSERT(partSq > oldPartSq, "areaOfFire(): no progress in tile-walk! From: %i,%i to %i,%i stuck in %i,%i",
		       map_coord(pos.x), map_coord(pos.y), map_coord(dest.x), map_coord(dest.y), map_coord(current.x),
		       map_coord(current.y));
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

bool hasElectronicWeapon(const Unit& unit) noexcept
{
	auto& weapons = unit.getWeapons();
	if (weapons.empty()) {
    return false;
  }

	return std::any_of(weapons.begin(), weapons.end(),
                     [](const auto& weapon)
	{
		return weapon.getSubclass() == WEAPON_SUBCLASS::ELECTRONIC;
	});
}

bool targetInLineOfFire(const Unit& unit, const ::Unit& target, int weapon_slot)
{
	const auto distance = iHypot((target.getPosition() - unit.getPosition()).xy());
	auto range = unit.getWeapons()[weapon_slot].getMaxRange(unit.getPlayer());
	if (!hasArtillery(unit)) {
		return range >= distance && LINE_OF_FIRE_MINIMUM <=
                                        calculateLineOfFire(unit, target, weapon_slot);
	}
	auto min_angle = calculateLineOfFire(unit, target, weapon_slot);
	if (min_angle > DEG(PROJ_MAX_PITCH)) {
		if (iSin(2 * min_angle) < iSin(2 * DEG(PROJ_MAX_PITCH))) {
			range = range * iSin(2 * min_angle) / iSin(2 * DEG(PROJ_MAX_PITCH));
		}
	}
	return range >= distance;
}

Unit* find_target(Unit& unit, TARGET_ORIGIN attacker_type,
                 int weapon_slot, Weapon weapon)
{
  Unit* target = nullptr;
  bool is_cb_sensor = false;
  bool found_cb = false;
  auto target_dist = weapon.getMaxRange(unit.getPlayer());
  auto min_dist = weapon.getMinRange(unit.getPlayer());

  for (const auto& sensor : apsSensorList)
  {
    if (!alliance_formed(sensor->getPlayer(), unit.getPlayer())) {
      continue;
    }
    // Artillery should not fire at objects observed
    // by VTOL CB/Strike sensors.
    if (sensor->hasVtolCbSensor() ||
        sensor->hasVtolInterceptSensor() ||
        sensor->isRadarDetector()) {
      continue;
    }

    if (auto as_droid = dynamic_cast<const Droid&>(sensor)) {
      // Skip non-observing droids. This includes Radar Detectors
      // at the moment since they never observe anything.
      if (as_droid.get_current_action() == ACTION::OBSERVE) {
        continue;
      }
    } else { // structure
      auto as_structure = dynamic_cast<const Structure &>(sensor);
      // skip incomplete structures
      if (as_structure.get_state() != STRUCTURE_STATE::BUILT) {
        continue;
      }
    }
    target = &sensor.getTarget(0);
    is_cb_sensor = sensor.hasCbSensor();

    if (!target ||
        !target->isAlive() ||
        target->is_probably_doomed() ||
        !target->isValidTarget() ||
        alliance_formed(target->getPlayer(),
                        unit.getPlayer())) {
      continue;
    }

    auto square_dist = objectPositionSquareDiff(target->getPosition(),
                                                unit.getPosition());
    if (target_within_weapon_range(unit, target, weapon_slot) &&
        is_target_visible()) {
      target_dist = square_dist;
      if (is_cb_sensor) {
        // got CB target, drop everything and shoot!
        found_cb = true;
      }
    }
  }
  return target;
}

unsigned numWeapons(const Unit& unit)
{
	return static_cast<unsigned>(unit.getWeapons().size());
}

unsigned getMaxWeaponRange(const Unit& unit)
{
	auto max = unsigned{0};
	for (const auto& weapon : unit.getWeapons())
	{
		const auto max_weapon_range = weapon.getMaxRange(unit.getPlayer());
		if (max_weapon_range > max) {
      max = max_weapon_range;
    }
	}
	return max;
}

