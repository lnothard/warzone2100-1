//
// Created by luna on 08/12/2021.
//

#include "lib/framework/geometry.h"
#include "lib/framework/vector.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "wzmaplib/map.h"

#include "ai.h"
#include "action.h"
#include "basedef.h"
#include "displaydef.h"
#include "objmem.h"

int establishTargetHeight(BaseObject const*);
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

  std::string name;
  uint8_t selectionGroup = 0;
  unsigned id;
  unsigned time = 0;
  unsigned bornTime = 0;
  Position position {0, 0, 0};
  Rotation rotation {0, 0, 0};
  Spacetime previousLocation;
  std::unique_ptr<DisplayData> display;
  std::array<uint8_t, MAX_PLAYERS> seenThisTick{};
  std::array<uint8_t, MAX_PLAYERS> visibleToPlayer{};
  std::bitset<static_cast<size_t>(OBJECT_FLAG::COUNT)> flags;
};

struct ConstructedObject::Impl
{
  unsigned lastEmissionTime = 0;
  unsigned timeAnimationStarted = 0;
  ANIMATION_EVENTS animationEvent = ANIM_EVENT_NONE;
};

struct Health::Impl
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
  unsigned timeLastHit = 0;
};

BaseObject::BaseObject(unsigned id)
  : pimpl{std::make_unique<Impl>(id)}
{
}

BaseObject::BaseObject(unsigned id, Player* playerManager)
  : pimpl{std::make_unique<Impl>(id)}
  , playerManager{playerManager}
{
}

BaseObject::BaseObject(unsigned id, std::unique_ptr<Health> damageManager)
  : pimpl{std::make_unique<Impl>(id)}
  , damageManager{std::move(damageManager)}
{
}

BaseObject::BaseObject(unsigned id, Player* playerManager,
                       std::unique_ptr<Health> damageManager)
  : pimpl{std::make_unique<Impl>(id)}
  , playerManager{playerManager}
  , damageManager{std::move(damageManager)}
{
}

BaseObject::BaseObject(BaseObject const& rhs)
  : pimpl{std::make_unique<Impl>(*rhs.pimpl)}
  , playerManager{rhs.playerManager}
  , damageManager{std::make_unique<Health>(*rhs.damageManager)}
{
}

BaseObject& BaseObject::operator=(BaseObject const& rhs)
{
  if (this == &rhs) return *this;
  *pimpl = *rhs.pimpl;
  playerManager = rhs.playerManager;
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

ConstructedObject::ConstructedObject(unsigned id, Player* playerManager)
  : BaseObject(id, playerManager)
  , pimpl{std::make_unique<Impl>()}
{
}

ConstructedObject::ConstructedObject(ConstructedObject const& rhs)
  : BaseObject(rhs)
  , pimpl{std::make_unique<Impl>(*rhs.pimpl)}
{
}

ConstructedObject& ConstructedObject::operator=(ConstructedObject const& rhs)
{
  if (this == &rhs) return *this;
  *pimpl = *rhs.pimpl;
  return *this;
}

Health::Health()
  : pimpl{std::make_unique<Impl>()}
{
}

Health::Health(Health const& rhs)
  : pimpl{std::make_unique<Impl>(*rhs.pimpl)}
{
}

Health &Health::operator=(Health const& rhs)
{
  if (this == &rhs) return *this;
  *pimpl = *rhs.pimpl;
  return *this;
}

std::string const* BaseObject::getName() const
{
  return pimpl ? &pimpl->name : nullptr;
}

unsigned BaseObject::getId() const noexcept
{
  return pimpl ? pimpl->id : 0;
}

unsigned BaseObject::getBornTime() const noexcept
{
  return pimpl ? pimpl->bornTime : 0;
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

uint8_t BaseObject::getSelectionGroup() const
{
  return pimpl ? pimpl->selectionGroup : 0;
}

void BaseObject::setSelectionGroup(uint8_t sel)
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Object undefined");
  pimpl->selectionGroup = sel;
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

void BaseObject::setId(unsigned id) noexcept
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Undefined object");
  pimpl->id = id;
}

void BaseObject::setBornTime(unsigned t) noexcept
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Undefined object");
  pimpl->bornTime = t;
}

void BaseObject::setImdShape(iIMDShape* imd)
{
  pimpl->display->imd_shape = std::make_unique<iIMDShape>(*imd);
}

void BaseObject::setFlag(size_t pos, bool val)
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Null object");
  pimpl->flags.set(pos, val);
}

void BaseObject::setTime(unsigned t) noexcept
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Null object");
  pimpl->time = t;
}

void BaseObject::setPosition(Position pos) noexcept
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Null object");
  pimpl->position = pos;
}

uint8_t BaseObject::seenThisTick(unsigned player) const
{
  return pimpl ? pimpl->seenThisTick[player] : 0;
}

void BaseObject::setRotation(Rotation new_rotation) noexcept
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Null object");
  pimpl->rotation = new_rotation;
}

void BaseObject::setHeight(int height) noexcept
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Null object");
  pimpl->position.z = height;
}

void BaseObject::setHidden()
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Null object");
  pimpl->visibleToPlayer.fill(0);
}

void BaseObject::setFrameNumber(unsigned num)
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Object undefined");
  pimpl->display->frame_number = num;
}

void BaseObject::setVisibleToPlayer(unsigned player, uint8_t vis)
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Null object");
  pimpl->visibleToPlayer[player] = vis;
}

void BaseObject::setPreviousLocation(Spacetime prevLoc)
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Null object");
  pimpl->previousLocation = prevLoc;
}

void Health::setTimeOfDeath(unsigned t)
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Null object");
  pimpl->timeOfDeath = t;
}

void Health::setHp(unsigned hp)
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Null object");
  pimpl->hitPoints = hp;
}

void Health::setOriginalHp(unsigned hp)
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Null object");
  pimpl->originalHp = hp;
}

unsigned Health::getTimeLastHit() const
{
  return pimpl ? pimpl->timeLastHit : 0;
}

unsigned Health::getTimeOfDeath() const
{
  return pimpl ? pimpl->timeOfDeath : 1000;
}

void Health::setSelected(bool sel)
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Null object");
  pimpl->isSelected = sel;
}

void Health::setResistance(unsigned res)
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Null object");
  pimpl->resistanceToElectric = res;
}

void Health::setExpectedDamageDirect(unsigned damage)
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Null object");
  pimpl->expectedDamageDirect = damage;
}

void Health::setExpectedDamageIndirect(unsigned damage)
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Null object");
  pimpl->expectedDamageIndirect = damage;
}

void Health::setTimeLastHit(unsigned time)
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Object undefined");
  pimpl->timeLastHit = time;
}

void Health::setLastHitWeapon(WEAPON_SUBCLASS weap)
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Null object");
  pimpl->lastHitWeapon = weap;
}

void Health::setPeriodicalDamage(unsigned damage)
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Null object");
  pimpl->periodicalDamage = damage;
}

void Health::setPeriodicalDamageStartTime(unsigned time)
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Null object");
  pimpl->periodicalDamageStartTime = time;
}

bool Health::isSelected() const
{
  return pimpl && pimpl->isSelected;
}

unsigned Health::getHp() const
{
  return pimpl ? pimpl->hitPoints : 0;
}

unsigned Health::getOriginalHp() const
{
  return pimpl ? pimpl->originalHp : 0;
}

unsigned Health::getResistance() const
{
  return pimpl ? pimpl->resistanceToElectric : 0;
}

unsigned Health::getExpectedDamageDirect() const
{
  return pimpl ? pimpl->expectedDamageDirect : 0;
}

unsigned Health::getExpectedDamageIndirect() const
{
  return pimpl ? pimpl->expectedDamageIndirect : 0;
}

WEAPON_SUBCLASS Health::getLastHitWeapon() const
{
  return pimpl ? pimpl->lastHitWeapon : WEAPON_SUBCLASS::COUNT;
}

unsigned Health::getPeriodicalDamage() const
{
  return pimpl ? pimpl->periodicalDamage : 0;
}

unsigned Health::getPeriodicalDamageStartTime() const
{
  return pimpl ? pimpl->periodicalDamageStartTime : 0;
}

bool Health::isDead() const
{
  return !pimpl || pimpl->timeOfDeath != 0;
}

bool Health::isProbablyDoomed(bool isDirectDamage) const
{
  ASSERT_OR_RETURN(false, pimpl != nullptr, "Null object");

  auto is_doomed = [this](unsigned damage) {
    return damage > getHp() && damage - getHp() > getHp() / 5;
  };

  if (isDirectDamage)
    return is_doomed(pimpl->expectedDamageDirect);

  return is_doomed(pimpl->expectedDamageIndirect);
}

ANIMATION_EVENTS ConstructedObject::getAnimationEvent() const
{
  return pimpl ? pimpl->animationEvent : ANIMATION_EVENTS::ANIM_EVENT_NONE;
}

int objectPositionSquareDiff(Position const& first, Position const& second)
{
  auto diff = (first - second).xy();
  return dot(diff, diff);
}

bool hasFullAmmo(Droid const& droid) noexcept
{
  auto const weapons = droid.weaponManager->weapons;
  return std::all_of(weapons.begin(), weapons.end(),
                     [](const auto& weapon) {
    return weapon.hasFullAmmo();
  });
}

bool hasFullAmmo(Structure const& structure) noexcept
{
  auto weapons = structure.weaponManager->weapons;
  return std::all_of(weapons.begin(), weapons.end(),
                     [](const auto& weapon) {
    return weapon.hasFullAmmo();
  });
}

bool hasArtillery(Droid const& droid) noexcept
{
  auto const weapons = droid.weaponManager->weapons;
  return std::any_of(weapons.begin(), weapons.end(),
                     [](const auto& weapon) {
    return weapon.isArtillery();
  });
}

bool hasArtillery(Structure const& structure) noexcept
{
  auto const weapons = structure.weaponManager->weapons;
  return std::any_of(weapons.begin(), weapons.end(),
                     [](const auto& weapon) {
    return weapon.isArtillery();
  });
}

Vector3i calculateMuzzleBaseLocation(BaseObject const& unit, int weapon_slot)
{
  auto imd_shape = unit.getImdShape();
  auto const position = unit.getPosition();
  auto muzzle = Vector3i{0, 0, 0};

  if (!imd_shape->nconnectors) {
    return position + Vector3i{0, 0, unit.getDisplayData()->imd_shape->max.y};
  }

  Affine3F af;
  auto const rotation = unit.getRotation();
  af.Trans(position.x, -position.z, position.y);
  af.RotY(rotation.direction);
  af.RotX(rotation.pitch);
  af.RotZ(-rotation.roll);
  af.Trans(imd_shape->connectors[weapon_slot].x,
           -imd_shape->connectors[weapon_slot].z,
           -imd_shape->connectors[weapon_slot].y);

  auto const barrel = Vector3i{};
  muzzle = (af * barrel).xzy();
  muzzle.z = -muzzle.z;
  return muzzle;
}

Vector3i calculateMuzzleTipLocation(ConstructedObject const& unit, int weapon_slot)
{
  auto imd_shape = unit.getImdShape();
  auto const& weapon = unit.weaponManager->weapons[weapon_slot];
  auto const position = unit.getPosition();
  auto const rotation = unit.getRotation();
  auto muzzle = Vector3i{0, 0, 0};

  if (!imd_shape->nconnectors) {
    return position + Vector3i{0, 0, 0 + unit.
            getDisplayData()->imd_shape->max.y};
  }

  auto barrel = Vector3i{0, 0, 0};
  auto weapon_imd = weapon.getImdShape();
  auto mount_imd = weapon.getMountGraphic();

  Affine3F af;
  af.Trans(position.x, -position.z, position.y);
  af.RotY(rotation.direction);
  af.RotX(rotation.pitch);
  af.RotZ(-rotation.roll);
  af.Trans(imd_shape->connectors[weapon_slot].x, -imd_shape->connectors[weapon_slot].z,
           -imd_shape->connectors[weapon_slot].y);

  af.RotY(weapon.getRotation().direction);

  if (mount_imd->nconnectors) {
    af.Trans(mount_imd->connectors->x,
             -mount_imd->connectors->z,
             -mount_imd->connectors->y);
  }
  af.RotX(weapon.getRotation().pitch);

  if (weapon_imd->nconnectors) {
    auto connector_num = unsigned{0};
    if (weapon.shotsFired && weapon_imd->nconnectors > 1) {
      connector_num = (weapon.shotsFired - 1) % weapon_imd->nconnectors;
    }
    auto const connector = weapon_imd->connectors[connector_num];
    barrel = Vector3i{connector.x, -connector.z, -connector.y};
  }
  muzzle = (af * barrel).xzy();
  muzzle.z = -muzzle.z;
  return muzzle;
}

void checkAngle(int64_t& angle_tan, int start_coord, int height,
                int square_distance, int target_height, bool is_direct)
{
  int64_t current_angle;

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
  else {
    angletan = iAtan2(angletan, 65536);
    angletan = angleDelta(angletan);
    return DEG(1) + angletan;
  }
}

void BaseObject::setPreviousTime(unsigned t)
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Undefined");
  pimpl->previousLocation.time = t;
}

bool hasElectronicWeapon(ConstructedObject const& unit) noexcept
{
  return std::any_of(unit.weaponManager->weapons.begin(),
                     unit.weaponManager->weapons.end(),
                     [](auto const& weapon) {
    return weapon.stats->weaponSubClass == WEAPON_SUBCLASS::ELECTRONIC;
  });
}

bool targetInLineOfFire(ConstructedObject const& unit, BaseObject const& target, int weapon_slot)
{
  const auto distance = iHypot((target.getPosition() - unit.getPosition()).xy());
  auto range = unit.weaponManager->weapons[weapon_slot].getMaxRange(unit.playerManager->getPlayer());

  if (!unit.hasArtillery()) {
    return range >= distance &&
           LINE_OF_FIRE_MINIMUM <= calculateLineOfFire(unit, target, weapon_slot);
  }

  auto min_angle = calculateLineOfFire(unit, target, weapon_slot);
  if (min_angle > DEG(PROJ_MAX_PITCH) && iSin(2 * min_angle) < iSin(2 * DEG(PROJ_MAX_PITCH))) {
    range = range * iSin(2 * min_angle) / iSin(2 * DEG(PROJ_MAX_PITCH));
  }
  return range >= distance;
}

BaseObject const* find_target(BaseObject const& unit, TARGET_ORIGIN attacker_type,
                        int weapon_slot, Weapon const& weapon)
{
  BaseObject const* target = nullptr;
  bool found_cb = false;
  auto target_dist = weapon.getMaxRange(unit.playerManager->getPlayer());

  auto min_dist = weapon.getMinRange(unit.playerManager->getPlayer());

  for (const auto sensor : apsSensorList)
  {
    if (sensor->playerManager->getPlayer() != unit.playerManager->getPlayer())
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
    }
    else { // structure
      auto as_structure = dynamic_cast<const Structure*>(sensor);
      // skip incomplete structures
      if (as_structure->getState() != STRUCTURE_STATE::BUILT) {
        continue;
      }
    }
    target = sensor->getTarget(0);
    auto is_cb_sensor = sensor->hasCbSensor();

    if (!target || target->damageManager->isDead() ||
        target->damageManager->isProbablyDoomed(false) ||
        !validTarget(&unit, target, 0)) {
      continue;
    }

    auto const square_dist = objectPositionSquareDiff(
            target->getPosition(), unit.getPosition());

    if (targetInLineOfFire(unit, *target, weapon_slot) &&
        targetVisible(&dynamic_cast<Droid const &>(unit), target, weapon_slot, false)) {

      target_dist = square_dist;
      if (is_cb_sensor) {
        // got CB target, drop everything and shoot!
        found_cb = true;
      }
    }
  }
  return target;
}

size_t numWeapons(Droid const& droid)
{
  return droid.weaponManager->weapons.size();
}

size_t numWeapons(Structure const& structure)
{
  return structure.weaponManager->weapons.size();
}

unsigned getMaxWeaponRange(Droid const& droid)
{
  auto max = unsigned{0};
  for (const auto& weapon : droid.weaponManager->weapons)
  {
    const auto max_weapon_range = weapon.getMaxRange(droid.playerManager->getPlayer());
    if (max_weapon_range > max)
      max = max_weapon_range;
  }
  return max;
}
