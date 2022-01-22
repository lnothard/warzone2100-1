//
// Created by luna on 08/12/2021.
//

#include <vector>

#include "lib/framework/fixedpoint.h"
#include "lib/framework/geometry.h"
#include "lib/framework/math_ext.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "wzmaplib/map.h"

#include "constructedobject.h"
#include "displaydef.h"
#include "map.h"
#include "objmem.h"
#include "weapon.h"

int establishTargetHeight(PlayerOwnedObject const* psTarget);
static constexpr auto PROJ_MAX_PITCH = 45;


struct ConstructedObject::Impl
{
  /// Current resistance points, 0 = cannot be attacked electrically
  int resistance = 0;
  unsigned lastEmissionTime = 0;
  WEAPON_SUBCLASS lastHitWeapon = WEAPON_SUBCLASS::COUNT;
  std::vector<TILEPOS> watchedTiles{};
  std::vector<Weapon> weapons{};
};

ConstructedObject::ConstructedObject(unsigned id, unsigned player)
  : PlayerOwnedObject(id, player),
    pimpl{std::make_unique<Impl>()}
{
}

ConstructedObject::ConstructedObject(ConstructedObject const& rhs)
  : PlayerOwnedObject(rhs),
    pimpl{std::make_unique<Impl>(*rhs.pimpl)}
{
}

ConstructedObject& ConstructedObject::operator=(ConstructedObject const& rhs)
{
  if (this == &rhs) return *this;
  *pimpl = *rhs.pimpl;
  return *this;
}


int ConstructedObject::getResistance() const
{
  return pimpl
         ? pimpl->resistance
         : -1;
}

const std::vector<Weapon>& ConstructedObject::getWeapons() const
{
  assert(pimpl);
  return pimpl->weapons;
}


bool hasFullAmmo(const ConstructedObject& unit) noexcept
{
	auto& weapons = unit.getWeapons();
	return std::all_of(weapons.begin(), weapons.end(),
	                   [](const auto& weapon)
  {
       return weapon.hasFullAmmo();
  });
}

bool hasArtillery(const ConstructedObject& unit) noexcept
{
	auto& weapons = unit.getWeapons();
	return std::any_of(weapons.begin(), weapons.end(),
                     [](const auto& weapon)
	{
		return weapon.isArtillery();
	});
}

Vector3i calculateMuzzleBaseLocation(const ConstructedObject& unit, int weapon_slot)
{
	auto& imd_shape = unit.getImdShape();
	const auto position = unit.getPosition();
	auto muzzle = Vector3i{0, 0, 0};

	if (imd_shape.nconnectors) {
		Affine3F af;
		auto rotation = unit.getRotation();
		af.Trans(position.x, -position.z, position.y);
		af.RotY(rotation.direction);
		af.RotX(rotation.pitch);
		af.RotZ(-rotation.roll);
		af.Trans(imd_shape.connectors[weapon_slot].x,
             -imd_shape.connectors[weapon_slot].z,
		         -imd_shape.connectors[weapon_slot].y);

		const auto barrel = Vector3i{};
		muzzle = (af * barrel).xzy();
		muzzle.z = -muzzle.z;
	}
	else {
		muzzle = position + Vector3i{0, 0, unit.getDisplayData()->imd_shape->max.y};
	}
	return muzzle;
}

Vector3i calculateMuzzleTipLocation(const ConstructedObject& unit, int weapon_slot)
{
	const auto& imd_shape = unit.getImdShape();
	const auto& weapon = unit.getWeapons()[weapon_slot];
	const auto& position = unit.getPosition();
	const auto& rotation = unit.getRotation();
	auto muzzle = Vector3i{0, 0, 0};

	if (imd_shape.nconnectors) {
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

		if (mount_imd->nconnectors) {
			af.Trans(mount_imd->connectors->x,
               -mount_imd->connectors->z,
               -mount_imd->connectors->y);
		}
		af.RotX(weapon.getRotation().pitch);

		if (weapon_imd->nconnectors) {
			auto connector_num = unsigned{0};
			if (weapon.getShotsFired() && weapon_imd->nconnectors > 1) {
				connector_num = (weapon.getShotsFired() - 1) % weapon_imd->nconnectors;
			}
			const auto connector = weapon_imd->connectors[connector_num];
			barrel = Vector3i{connector.x,
                        -connector.z,
                        -connector.y};
		}
		muzzle = (af * barrel).xzy();
		muzzle.z = -muzzle.z;
	}
	else {
		muzzle = position + Vector3i{
            0, 0, 0 + unit.getDisplayData()->imd_shape->max.y};
	}
	return muzzle;
}

void checkAngle(int64_t& angle_tan, int start_coord, int height,
                int square_distance, int target_height, bool is_direct)
{
	auto current_angle = int64_t{0};

	if (is_direct) {
		current_angle = (65536 * height) / iSqrt(start_coord);
	}
	else {
		const auto distance = iSqrt(square_distance);
		const auto position = iSqrt(start_coord);
		current_angle = (position * target_height) / distance;

		if (current_angle < height &&
        position > TILE_UNITS / 2 &&
        position < distance - TILE_UNITS / 2) {

			current_angle = (65536 * square_distance * height - start_coord * target_height)
				/ (square_distance * position - distance * start_coord);
		}
		else {
			current_angle = 0;
		}
	}
	angle_tan = std::max(angle_tan, current_angle);
}

/**
 * Check fire line from psViewer to psTarget
 * psTarget can be any type of SimpleObject (e.g. a tree).
 */
int calculateLineOfFire(const ConstructedObject& unit, const ::PlayerOwnedObject & target,
                        int weapon_slot, bool walls_block, bool is_direct)
{
	auto muzzle = calculateMuzzleBaseLocation(unit, weapon_slot);

	auto pos = muzzle;
  auto dest = target.getPosition();
	auto diff = (dest - pos).xy();

	auto distSq = dot(diff, diff);
	if (distSq == 0) {
		// Should never be on top of each other, but ...
		return 1000;
	}

	auto current = pos.xy();
	auto start = current;
	auto angletan = static_cast<int64_t>(-1000) * 65536;
	auto partSq = 0;

	// run a manual trace along the line of fire until target is reached

  auto oldPartSq = partSq;
  Vector2i next{}; Vector2i halfway{}; Vector2i part{};
  bool hasSplitIntersection;
	while (partSq < distSq) {
    oldPartSq = partSq;

		if (partSq > 0) {
      checkAngle(angletan, partSq,
                 map_Height(current) - pos.z, distSq,
                 dest.z - pos.z, is_direct);
		}

		// intersect current tile with line of fire
		next = diff;
		hasSplitIntersection = map_Intersect(&current.x, &current.y, &next.x,
                                         &next.y, &halfway.x, &halfway.y);

		if (hasSplitIntersection) {
			// check whether target was reached before tile split line:
			part = halfway - start;
			partSq = dot(part, part);

			if (partSq >= distSq) {
				break;
			}

			if (partSq > 0) {
        checkAngle(angletan, partSq,
                   map_Height(halfway) - pos.z, distSq,
                   dest.z - pos.z, is_direct);
			}
		}

		// check for walls and other structures
		if (walls_block && oldPartSq > 0) {
			halfway = current + (next - current) / 2;
			auto psTile = mapTile(map_coord(halfway.x), map_coord(halfway.y));
			if (TileHasStructure(psTile) &&
          psTile->psObject != &target) {
				// check whether target was reached before tile's "half way" line
				part = halfway - start;
				partSq = dot(part, part);

				if (partSq >= distSq) {
					break;
				}

				// allowed to shoot over enemy structures if they are NOT the target
				if (partSq > 0) {
          checkAngle(angletan, oldPartSq,
                     psTile->psObject->getPosition().z + establishTargetHeight(
                             psTile->psObject) - pos.z,
                     distSq, dest.z - pos.z, is_direct);
				}
			}
		}
		// next
		current = next;
		part = current - start;
		partSq = dot(part, part);
		ASSERT(partSq > oldPartSq, "areaOfFire(): no progress in tile-walk! From: %i,%i to %i,%i stuck in %i,%i",
		       map_coord(pos.x), map_coord(pos.y),
           map_coord(dest.x), map_coord(dest.y),
           map_coord(current.x), map_coord(current.y));
	}
	if (is_direct) {
		return establishTargetHeight(&target) -
            (pos.z + (angletan * iSqrt(distSq)) / 65536 - dest.z);
	}
	else
	{
		angletan = iAtan2(angletan, 65536);
		angletan = angleDelta(angletan);
		return DEG(1) + angletan;
	}
}

bool hasElectronicWeapon(const ConstructedObject& unit) noexcept
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

bool targetInLineOfFire(const ConstructedObject& unit, const ConstructedObject& target, int weapon_slot)
{
	const auto distance = iHypot(
          (target.getPosition() - unit.getPosition()).xy());

	auto range = unit.getWeapons()[weapon_slot].
          getMaxRange(unit.getPlayer());

	if (!hasArtillery(unit)) {
		return range >= distance &&
           LINE_OF_FIRE_MINIMUM <= calculateLineOfFire(unit, target, weapon_slot);
	}
	auto min_angle = calculateLineOfFire(unit, target, weapon_slot);
	if (min_angle > DEG(PROJ_MAX_PITCH)) {
		if (iSin(2 * min_angle) < iSin(2 * DEG(PROJ_MAX_PITCH))) {
			range = range * iSin(2 * min_angle) / iSin(2 * DEG(PROJ_MAX_PITCH));
		}
	}
	return range >= distance;
}

ConstructedObject* find_target(ConstructedObject& unit, TARGET_ORIGIN attacker_type,
                               int weapon_slot, Weapon& weapon)
{
  ConstructedObject* target = nullptr;
  bool is_cb_sensor = false;
  bool found_cb = false;
  auto target_dist = weapon.getMaxRange(unit.getPlayer());
  auto min_dist = weapon.getMinRange(unit.getPlayer());

  for (const auto sensor : apsSensorList)
  {
    if (!aiCheckAlliances(sensor->getPlayer(), unit.getPlayer())) {
      continue;
    }
    // Artillery should not fire at objects observed
    // by VTOL CB/Strike sensors.
    if (sensor->hasVtolCbSensor() ||
        sensor->hasVtolInterceptSensor() ||
        sensor->isRadarDetector()) {
      continue;
    }

    if (auto as_droid = dynamic_cast<const Droid*>(sensor)) {
      // Skip non-observing droids. This includes Radar Detectors
      // at the moment since they never observe anything.
      if (as_droid->getAction() == ACTION::OBSERVE) {
        continue;
      }
    } else { // structure
      auto as_structure = dynamic_cast<const Structure*>(sensor);
      // skip incomplete structures
      if (as_structure->getState() != STRUCTURE_STATE::BUILT) {
        continue;
      }
    }
    target = sensor->getTarget(0);
    is_cb_sensor = sensor->hasCbSensor();

    if (!target ||
        !target->isAlive() ||
        target->isProbablyDoomed(false) ||
        !unit.isValidTarget(target, 0) ||
        aiCheckAlliances(target->getPlayer(),
                        unit.getPlayer())) {
      continue;
    }

    auto square_dist = objectPositionSquareDiff(
            target->getPosition(), unit.getPosition());

    if (targetInLineOfFire(unit, *target, weapon_slot) &&
        unit.isTargetVisible(target, false)) {

      target_dist = square_dist;
      if (is_cb_sensor) {
        // got CB target, drop everything and shoot!
        found_cb = true;
      }
    }
  }
  return target;
}

size_t numWeapons(const ConstructedObject& unit)
{
	return unit.getWeapons().size();
}

unsigned getMaxWeaponRange(const ConstructedObject& unit)
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

