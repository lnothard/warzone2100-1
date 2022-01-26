//
// Created by luna on 08/12/2021.
//

#include "lib/framework/geometry.h"
#include "lib/framework/vector.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "wzmaplib/map.h"

#include "basedef.h"
#include "displaydef.h"
#include "objmem.h"
#include "visibility.h"

bool aiCheckAlliances(unsigned, unsigned);
int establishTargetHeight(BaseObject const*);
void visRemoveVisibility(BaseObject*);
int map_Height(Vector2i);
bool map_Intersect(int*, int*, int*, int*, int*, int*);


Spacetime::Spacetime(unsigned time, Position position, Rotation rotation)
	: time{time}, position{position}, rotation{rotation}
{
}

struct BaseObject::Impl
{
  ~Impl() = default;
  explicit Impl(unsigned id);

  Impl(Impl const& rhs);
  Impl& operator=(Impl const& rhs);

  Impl(Impl&& rhs) noexcept = default;
  Impl& operator=(Impl&& rhs) noexcept = default;

  unsigned id;
  unsigned time = 0;
  Position position {0, 0, 0};
  Rotation rotation {0, 0, 0};
  Spacetime previousLocation;
  std::unique_ptr<DisplayData> display;
  std::array<uint8_t, MAX_PLAYERS> visibleToPlayer{};
  std::bitset<static_cast<size_t>(OBJECT_FLAG::COUNT)> flags;
};

struct DamageManager::Impl
{
  Impl() = default;

  WEAPON_SUBCLASS lastHitWeapon = WEAPON_SUBCLASS::COUNT;
  bool isSelected = false;
  unsigned hitPoints = 0;
  unsigned originalHp = 0;
  unsigned timeOfDeath = 0;
  unsigned resistanceToElectric = 0;
  unsigned expectedDamageDirect = 0;
  unsigned expectedDamageIndirect = 0;
  unsigned periodicalDamage = 0;
  unsigned periodicalDamageStartTime = 0;
};

struct PlayerManager::Impl
{
  explicit Impl(unsigned player);

  unsigned player = 0;
};

BaseObject::BaseObject(unsigned id)
  : pimpl{std::make_unique<Impl>(id)}
{
}

BaseObject::BaseObject(unsigned id, std::unique_ptr<PlayerManager> playerManager)
  : pimpl{std::make_unique<Impl>(id)}
  , playerManager{std::move(playerManager)}
{
}

BaseObject::BaseObject(unsigned id, std::unique_ptr<DamageManager> damageManager)
        : pimpl{std::make_unique<Impl>(id)}
        , damageManager{std::move(damageManager)}
{
}

BaseObject::BaseObject(unsigned id,
                       std::unique_ptr<PlayerManager> playerManager,
                       std::unique_ptr<DamageManager> damageManager)
  : pimpl{std::make_unique<Impl>(id)}
  , playerManager{std::move(playerManager)}
  , damageManager{std::move(damageManager)}
{
}

BaseObject::BaseObject(BaseObject const& rhs)
  : pimpl{std::make_unique<Impl>(*rhs.pimpl)}
  , playerManager{std::make_unique<PlayerManager>(*rhs.playerManager)}
  , damageManager{std::make_unique<DamageManager>(*rhs.damageManager)}
{
}

BaseObject& BaseObject::operator=(BaseObject const& rhs)
{
  if (this == &rhs) return *this;
  *pimpl = *rhs.pimpl;
  *playerManager = *rhs.playerManager;
  *damageManager = *rhs.damageManager;
  return *this;
}

BaseObject::Impl::Impl(unsigned id)
  : id{id}
{
}

BaseObject::Impl::Impl(Impl const& rhs)
  : id{rhs.id}
  , display{rhs.display
            ? std::make_unique<DisplayData>(*rhs.display)
            : nullptr}
  , time{rhs.time}
  , position{rhs.position}
  , rotation{rhs.rotation}
  , previousLocation{rhs.previousLocation}
  , flags{rhs.flags}
  , visibleToPlayer(rhs.visibleToPlayer)
{
}

BaseObject::Impl& BaseObject::Impl::operator=(Impl const& rhs)
{
  display = rhs.display
            ? std::make_unique<DisplayData>(*rhs.display)
            : nullptr;
  time = rhs.time;
  position = rhs.position;
  rotation = rhs.rotation;
  previousLocation = rhs.previousLocation;
  flags = rhs.flags;
  visibleToPlayer = rhs.visibleToPlayer;
}

DamageManager::DamageManager()
  : pimpl{std::make_unique<Impl>()}
{
}

DamageManager::DamageManager(DamageManager const& rhs)
  : pimpl{std::make_unique<Impl>(*rhs.pimpl)}
{
}

DamageManager &DamageManager::operator=(DamageManager const& rhs)
{
  if (this == &rhs) return *this;
  *pimpl = *rhs.pimpl;
  return *this;
}

PlayerManager::PlayerManager(unsigned player)
  : pimpl{std::make_unique<Impl>(player)}
{
}

PlayerManager::PlayerManager(PlayerManager const& rhs)
  : pimpl{std::make_unique<Impl>(*rhs.pimpl)}
{
}

PlayerManager &PlayerManager::operator=(PlayerManager const& rhs)
{
  if (this == &rhs) return *this;
  *pimpl = *rhs.pimpl;
  return *this;
}

PlayerManager::Impl::Impl(unsigned player)
  : player{player}
{
}

unsigned BaseObject::getId() const noexcept
{
  return pimpl ? pimpl->id : 0;
}

Spacetime BaseObject::getSpacetime() const noexcept
{
  return pimpl
         ? Spacetime(pimpl->time, pimpl->position, pimpl->rotation)
         : Spacetime();
}

Position BaseObject::getPosition() const noexcept
{
  return pimpl ? pimpl->position : Position();
}

Rotation BaseObject::getRotation() const noexcept
{
  return pimpl ? pimpl->rotation : Rotation();
}

unsigned BaseObject::getTime() const noexcept
{
  return pimpl ? pimpl->time : 0;
}

Spacetime BaseObject::getPreviousLocation() const noexcept
{
  return pimpl ? pimpl->previousLocation : Spacetime();
}

const DisplayData* BaseObject::getDisplayData() const noexcept
{
  return pimpl ? pimpl->display.get() : nullptr;
}

uint8_t BaseObject::isVisibleToPlayer(unsigned player) const
{
  return pimpl->visibleToPlayer[player];
}

uint8_t BaseObject::isVisibleToSelectedPlayer() const
{
  return pimpl && pimpl->visibleToPlayer[selectedPlayer];
}

bool BaseObject::testFlag(size_t pos) const
{
  return pimpl && pimpl->flags.test(pos);
}

void BaseObject::setFlag(size_t pos, bool val)
{
  if (!pimpl) return;
  pimpl->flags.set(pos, val);
}

void BaseObject::setTime(unsigned t) noexcept
{
  if (!pimpl) return;
  pimpl->time = t;
}

void BaseObject::setPosition(Position pos) noexcept
{
  if (!pimpl) return;
  pimpl->position = pos;
}

void BaseObject::setRotation(Rotation new_rotation) noexcept
{
  if (!pimpl) return;
  pimpl->rotation = new_rotation;
}

void BaseObject::setHeight(int height) noexcept
{
  if (!pimpl) return;
  pimpl->position.z = height;
}

void BaseObject::setHidden()
{
  if (!pimpl) return;
  pimpl->visibleToPlayer.fill(0);
}

void BaseObject::setVisibleToPlayer(unsigned player, uint8_t vis)
{
  if (!pimpl) return;
  pimpl->visibleToPlayer[player] = vis;
}

void BaseObject::setPreviousLocation(Spacetime prevLoc)
{
  if (!pimpl) return;
  pimpl->previousLocation = prevLoc;
}

void DamageManager::setTimeOfDeath(unsigned t)
{
  if (!pimpl) return;
  pimpl->timeOfDeath = t;
}

void DamageManager::setHp(unsigned hp)
{
  if (!pimpl) return;
  pimpl->hitPoints = hp;
}

void DamageManager::setOriginalHp(unsigned hp)
{
  if (!pimpl) return;
  pimpl->originalHp = hp;
}

void DamageManager::setSelected(bool sel)
{
  if (!pimpl) return;
  pimpl->isSelected = sel;
}

void DamageManager::setResistance(unsigned res)
{
  if (!pimpl) return;
  pimpl->resistanceToElectric = res;
}

void DamageManager::setExpectedDamageDirect(unsigned damage)
{
  if (!pimpl) return;
  pimpl->expectedDamageDirect = damage;
}

void DamageManager::setExpectedDamageIndirect(unsigned damage)
{
  if (!pimpl) return;
  pimpl->expectedDamageIndirect = damage;
}

void DamageManager::setLastHitWeapon(WEAPON_SUBCLASS weap)
{
  if (!pimpl) return;
  pimpl->lastHitWeapon = weap;
}

void DamageManager::setPeriodicalDamage(unsigned damage)
{
  if (!pimpl) return;
  pimpl->periodicalDamage = damage;
}

void DamageManager::setPeriodicalDamageStartTime(unsigned time)
{
  if (!pimpl) return;
  pimpl->periodicalDamageStartTime = time;
}

bool DamageManager::isSelected() const
{
  return pimpl && pimpl->isSelected;
}

unsigned DamageManager::getHp() const
{
  return pimpl ? pimpl->hitPoints : 0;
}

unsigned DamageManager::getOriginalHp() const
{
  return pimpl ? pimpl->originalHp : 0;
}

unsigned DamageManager::getResistance() const
{
  return pimpl ? pimpl->resistanceToElectric : 0;
}

unsigned DamageManager::getExpectedDamageDirect() const
{
  return pimpl ? pimpl->expectedDamageDirect : 0;
}

unsigned DamageManager::getExpectedDamageIndirect() const
{
  return pimpl ? pimpl->expectedDamageIndirect : 0;
}

WEAPON_SUBCLASS DamageManager::getLastHitWeapon() const
{
  return pimpl ? pimpl->lastHitWeapon : WEAPON_SUBCLASS::COUNT;
}

unsigned DamageManager::getPeriodicalDamage() const
{
  return pimpl ? pimpl->periodicalDamage : 0;
}

unsigned DamageManager::getPeriodicalDamageStartTime() const
{
  return pimpl ? pimpl->periodicalDamageStartTime : 0;
}

bool DamageManager::isDead() const
{
  return !pimpl || pimpl->timeOfDeath != 0;
}

bool DamageManager::isProbablyDoomed(bool isDirectDamage) const
{
  if (!pimpl) return false;

  auto is_doomed = [this](unsigned damage) {
    const auto hp = getHp();
    return damage > hp && damage - hp > hp / 5;
  };

  if (isDirectDamage)
    return is_doomed(pimpl->expectedDamageDirect);

  return is_doomed(pimpl->expectedDamageIndirect);
}

void PlayerManager::setPlayer(unsigned plr)
{
  if (!pimpl) return;
  pimpl->player = plr;
}

unsigned PlayerManager::getPlayer() const
{
  return pimpl ? pimpl->player : 0;
}

bool PlayerManager::isSelectedPlayer() const
{
  return getPlayer() == selectedPlayer;
}

int objectPositionSquareDiff(const Position& first, const Position& second)
{
  Vector2i diff = (first - second).xy();
  return dot(diff, diff);
}

bool hasFullAmmo(PlayerManager const& unit) noexcept
{
  auto weapons = unit.getWeapons();
  return std::all_of(weapons.begin(), weapons.end(),
                     [](const auto& weapon) {
                       return weapon.hasFullAmmo();
                     });
}

bool hasArtillery(PlayerManager const& unit) noexcept
{
  auto weapons = unit.getWeapons();
  return std::any_of(weapons.begin(), weapons.end(),
                     [](const auto& weapon) {
                       return weapon.isArtillery();
                     });
}

Vector3i calculateMuzzleBaseLocation(BaseObject const& unit, int weapon_slot)
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

Vector3i calculateMuzzleTipLocation(BaseObject const& unit, int weapon_slot)
{
  const auto& imd_shape = unit.getImdShape();
  const auto& weapon = dynamic_cast<PlayerManager const&>(unit).getWeapons()[weapon_slot];
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
int calculateLineOfFire(const BaseObject& unit, const BaseObject & target,
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

bool hasElectronicWeapon(PlayerManager const& unit) noexcept
{
  auto& weapons = unit.getWeapons();
  if (weapons.empty()) {
    return false;
  }

  return std::any_of(weapons.begin(), weapons.end(),
                     [](const auto& weapon) {
                       return weapon.getSubclass() == WEAPON_SUBCLASS::ELECTRONIC;
                     });
}

bool targetInLineOfFire(BaseObject const& unit, BaseObject const& target, int weapon_slot)
{
  const auto distance = iHypot(
          (target.getPosition() - unit.getPosition()).xy());

  auto range = dynamic_cast<PlayerManager const&>(
          unit).getWeapons()[weapon_slot].getMaxRange(
          dynamic_cast<PlayerManager const&>(unit).getPlayer());

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

BaseObject* find_target(BaseObject const& unit, TARGET_ORIGIN attacker_type,
                        int weapon_slot, Weapon const& weapon)
{
  BaseObject* target = nullptr;
  bool is_cb_sensor = false;
  bool found_cb = false;
  auto target_dist = weapon.getMaxRange(
          dynamic_cast<PlayerManager const&>(unit).getPlayer());

  auto min_dist = weapon.getMinRange(
          dynamic_cast<PlayerManager const&>(unit).getPlayer());

  for (const auto sensor : apsSensorList)
  {
    if (!aiCheckAlliances(sensor->getPlayer(), dynamic_cast<
                                                       PlayerManager const&>(unit).getPlayer()))
      continue;

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
        dynamic_cast<DamageManager *>(target)->isDead() ||
        dynamic_cast<DamageManager *>(target)->isProbablyDoomed(false) ||
        !unit.isValidTarget(target, 0) ||
        aiCheckAlliances(dynamic_cast<PlayerManager *>(target)->getPlayer(),
                         dynamic_cast<PlayerManager const&>(unit).getPlayer())) {
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

size_t numWeapons(PlayerManager const& unit)
{
  return unit.getWeapons().size();
}

unsigned getMaxWeaponRange(PlayerManager const& unit)
{
  auto max = unsigned{0};
  for (const auto& weapon : unit.getWeapons())
  {
    const auto max_weapon_range = weapon.getMaxRange(unit.getPlayer());
    if (max_weapon_range > max)
      max = max_weapon_range;
  }
  return max;
}
