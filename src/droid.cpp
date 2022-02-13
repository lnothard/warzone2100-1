/*
	This file is part of Warzone 2100.
	Copyright (C) 1999-2004  Eidos Interactive
	Copyright (C) 2005-2020  Warzone 2100 Project

	Warzone 2100 is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Warzone 2100 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Warzone 2100; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/

/**
 * @file droid.cpp
 */

#include "lib/framework/math_ext.h"
#include "lib/framework/strres.h"
#include "lib/framework/vector.h"
#include "lib/sound/audio.h"

#include "action.h"
#include "baseobject.h"
#include "cmddroid.h"
#include "combat.h"
#include "component.h"
#include "display3d.h"
#include "displaydef.h"
#include "droid.h"
#include "edit3d.h"
#include "effects.h"
#include "feature.h"
#include "fpath.h"
#include "geometry.h"
#include "group.h"
#include "levels.h"
#include "lighting.h"
#include "loop.h"
#include "mapgrid.h"
#include "mission.h"
#include "move.h"
#include "multiplay.h"
#include "objmem.h"
#include "projectile.h"
#include "qtscript.h"
#include "random.h"
#include "raycast.h"
#include "scores.h"
#include "selection.h"
#include "template.h"
#include "text.h"
#include "transporter.h"
#include "visibility.h"
#include "warcam.h"
#include "game.h"


// the structure that was last hit
Droid* psLastDroidHit;

//determines the best IMD to draw for the droid - A TEMP MEASURE!
static void groupConsoleInformOfSelection(unsigned groupNumber);
static void groupConsoleInformOfCreation(unsigned groupNumber);
static void groupConsoleInformOfCentering(unsigned groupNumber);
static void groupConsoleInformOfRemoval();
static void droidUpdateDroidSelfRepair(Droid* psRepairDroid);
static unsigned calcDroidBaseBody(Droid* psDroid);


struct Droid::Impl
{
  ~Impl() = default;
  Impl();

  Impl(Impl const& rhs);
  Impl& operator=(Impl const& rhs);

  Impl(Impl&& rhs) noexcept = default;
  Impl& operator=(Impl&& rhs) noexcept = default;


  Droid* commander = nullptr;
  DROID_TYPE type = DROID_TYPE::ANY;
  unsigned weight = 0;

  /// Base speed depends on propulsion type
  unsigned baseSpeed = 0;

  unsigned experience = 0;
  unsigned kills = 0;

  /// Set when stuck. Used for, e.g., firing indiscriminately
  /// at map features to clear the way
  unsigned lastFrustratedTime = 0;
  unsigned lastEmissionTime = 0;

  Group* group = nullptr;

  /// A structure that this droid might be associated with.
  /// For VTOLs this is the rearming pad
  Structure* associatedStructure = nullptr;

//   /// The range [0; listSize - 1] corresponds to synchronised orders, and the range
//   /// [listPendingBegin; listPendingEnd - 1] corresponds to the orders that will
//   /// remain, once all orders are synchronised.
//   unsigned listPendingBegin;

  /// The number of synchronised orders. Orders from `listSize` to
  /// the real end of the list may not affect game state.
  std::vector<Order> asOrderList;

  /// Index of first order which will not be erased by
  /// a pending order. After all messages are processed
  /// the orders in the range [listPendingBegin; listPendingEnd - 1]
  /// will remain.
  std::unique_ptr<Order> order;
  unsigned secondaryOrder = 0;
  /// What `secondary_order` will be after synchronisation.
  unsigned secondaryOrderPending = 0;
  /// Number of pending `secondary_order` synchronisations.
  int secondaryOrderPendingCount = 0;

  ACTION action = ACTION::NONE;
  Vector2i actionPos {0, 0};
  std::array<BaseObject*, MAX_WEAPONS> actionTargets;
  unsigned timeActionStarted = 0;
  unsigned actionPointsDone = 0;

  uint8_t illuminationLevel = 0;
  std::unique_ptr<Movement> movement;

  /* Animation stuff */
  unsigned timeAnimationStarted;
  ANIMATION_EVENTS animationEvent;

  /// Bit set telling which tiles block this type of droid (TODO)
  uint8_t blockedBits = 0;

  int iAudioID = NO_SOUND;

  std::unordered_map<COMPONENT_TYPE, ComponentStats> components;
};

Droid::~Droid()
{
  audio_RemoveObj(this);

  if (isTransporter(*this) && pimpl->group) {
    // free all droids associated with this transporter
    for (auto psCurr : *pimpl->group->getMembers())
    {
      removeDroidFromGroup(psCurr);
    }
  }

  fpathRemoveDroidData(static_cast<int>(getId()));

  // leave the current group if any
  if (pimpl->group) {
    pimpl->group->removeDroid(this);
  }
}

Droid::Droid(unsigned id, Player* player)
  : BaseObject(id, player, std::make_unique<Health>(), std::make_unique<WeaponManager>())
  , pimpl{std::make_unique<Impl>()}
{
  initVisibility();
}

Droid::Droid(Droid const& rhs)
  : BaseObject(rhs)
  , pimpl{std::make_unique<Impl>(*rhs.pimpl)}
{
}

Droid& Droid::operator=(Droid const& rhs)
{
  if (this == &rhs) return *this;
  *pimpl = *rhs.pimpl;
  playerManager = rhs.playerManager;
  *damageManager = *rhs.damageManager;
  return *this;
}

Droid::Impl::Impl()
  : type{DROID_TYPE::ANY},
    action{ACTION::NONE},
    secondaryOrder{DSS_ARANGE_LONG | DSS_REPLEV_NEVER | DSS_ALEV_ALWAYS | DSS_HALT_GUARD},
    secondaryOrderPending{DSS_ARANGE_LONG | DSS_REPLEV_NEVER | DSS_ALEV_ALWAYS | DSS_HALT_GUARD}
{
}

Droid::Impl::Impl(Impl const& rhs)
  : type{rhs.type},
    weight{rhs.weight},
    baseSpeed{rhs.baseSpeed},
    experience{rhs.experience},
    kills{rhs.kills},
    lastFrustratedTime{rhs.lastFrustratedTime},
    group{rhs.group},
    associatedStructure{rhs.associatedStructure},
    asOrderList{rhs.asOrderList},
    order{rhs.order ? std::make_unique<Order>(*rhs.order) : nullptr},
    secondaryOrder{rhs.secondaryOrder},
    secondaryOrderPending(rhs.secondaryOrderPending),
    secondaryOrderPendingCount(rhs.secondaryOrderPendingCount),
    action{rhs.action},
    actionPos{rhs.actionPos},
    actionTargets{rhs.actionTargets},
    timeActionStarted{rhs.timeActionStarted},
    actionPointsDone{rhs.actionPointsDone},
    illuminationLevel{rhs.illuminationLevel},
    movement{rhs.movement ? std::make_unique<Movement>(*rhs.movement) : nullptr},
    timeAnimationStarted{rhs.timeAnimationStarted},
    animationEvent{rhs.animationEvent},
    blockedBits{rhs.blockedBits},
    iAudioID{rhs.iAudioID},
    components{rhs.components}
{
}

Droid::Impl& Droid::Impl::operator=(Impl const& rhs)
{
  if (this == &rhs) return *this;

  type = rhs.type;
  weight = rhs.weight;
  baseSpeed = rhs.baseSpeed;
  experience = rhs.experience;
  kills = rhs.kills;
  lastFrustratedTime = rhs.lastFrustratedTime;
  group = rhs.group;
  associatedStructure = rhs.associatedStructure;
  asOrderList = rhs.asOrderList;
  order = rhs.order ? std::make_unique<Order>(*rhs.order) : nullptr;
  secondaryOrder = rhs.secondaryOrder;
  secondaryOrderPending = rhs.secondaryOrderPending;
  secondaryOrderPendingCount = rhs.secondaryOrderPendingCount;
  action = rhs.action;
  actionPos = rhs.actionPos;
  actionTargets = rhs.actionTargets;
  timeActionStarted = rhs.timeActionStarted;
  actionPointsDone = rhs.actionPointsDone;
  illuminationLevel = rhs.illuminationLevel;
  movement = rhs.movement ? std::make_unique<Movement>(*rhs.movement) : nullptr;
  timeAnimationStarted = rhs.timeAnimationStarted;
  animationEvent = rhs.animationEvent;
  blockedBits = rhs.blockedBits;
  iAudioID = rhs.iAudioID;
  components = rhs.components;
  return *this;
}

void Droid::initVisibility()
{
  for (auto vPlayer = 0; vPlayer < MAX_PLAYERS; ++vPlayer)
  {
    setVisibleToPlayer(vPlayer,
                       hasSharedVision(
                               vPlayer, playerManager->getPlayer()) ? UINT8_MAX : 0);
  }
}

ComponentStats const* DroidTemplate::getComponent(COMPONENT_TYPE compName) const
{
  return &components.at(compName);
}

const ComponentStats* Droid::getComponent(COMPONENT_TYPE compName) const
{
  return pimpl ? &pimpl->components.at(compName) : nullptr;
}

ANIMATION_EVENTS Droid::getAnimationEvent() const
{
  return pimpl ? pimpl->animationEvent : ANIMATION_EVENTS::ANIM_EVENT_NONE;
}

unsigned Droid::getTimeActionStarted() const
{
  return pimpl ? pimpl->timeActionStarted : 0;
}

ACTION Droid::getAction() const noexcept
{
  return pimpl ? pimpl->action : ACTION::NONE;
}

Vector2i Droid::getActionPos() const
{
  return pimpl ? pimpl->actionPos : Vector2i();
}

unsigned Droid::getWeight() const
{
  return pimpl ? pimpl->weight : 0;
}

BaseObject const* Droid::getTarget(int idx) const
{
  return pimpl ? pimpl->actionTargets[idx] : nullptr;
}

Order const* Droid::getOrder() const
{
  return pimpl ? pimpl->order.get() : nullptr;
}

unsigned Droid::getBaseSpeed() const
{
  return pimpl ? pimpl->baseSpeed : 0;
}

Movement const* Droid::getMovementData() const
{
  return pimpl ? pimpl->movement.get() : nullptr;
}

unsigned Droid::getExperience() const
{
  return pimpl ? pimpl->experience : 0;
}

unsigned Droid::getKills() const
{
  return pimpl ? pimpl->kills : 0;
}

int Droid::getAudioId() const
{
  return pimpl ? pimpl->iAudioID : NO_SOUND;
}

void Droid::setAudioId(int audio)
{
  pimpl->iAudioID = audio;
}

void Droid::cancelBuild()
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Droid object is undefined");

  using enum ORDER_TYPE;
  if (pimpl->order->type == NONE || pimpl->order->type == PATROL ||
      pimpl->order->type == HOLD || pimpl->order->type == SCOUT ||
      pimpl->order->type == GUARD) {
    pimpl->order->target = nullptr;
    pimpl->action = ACTION::NONE;
    return;
  }
  pimpl->action = ACTION::NONE;
  pimpl->order->type = NONE;

  // stop moving
  if (isFlying()) {
    pimpl->movement->status = MOVE_STATUS::HOVER;
  }
  else {
    pimpl->movement->status = MOVE_STATUS::INACTIVE;
  }
  triggerEventDroidIdle(this);
}

unsigned Droid::getLevel() const
{
  ASSERT_OR_RETURN(0, pimpl != nullptr, "Droid object is undefined");
  auto brain = dynamic_cast<CommanderStats const*>(
          getComponent(COMPONENT_TYPE::BRAIN));

  if (!brain) return 0;
  auto const& rankThresholds = brain->
          upgraded[playerManager->getPlayer()].rankThresholds;

  for (auto i = 1; i < rankThresholds.size(); ++i)
  {
    if (pimpl->kills < rankThresholds.at(i)) {
      return i - 1;
    }
  }
  return rankThresholds.size() - 1;
}

bool Droid::isStationary() const
{
   assert(pimpl != nullptr);
   using enum MOVE_STATUS;
   return pimpl->movement->status == INACTIVE ||
          pimpl->movement->status == HOVER ||
          pimpl->movement->status == SHUFFLE;
}

bool Droid::hasCommander() const {
  ASSERT_OR_RETURN(false, pimpl != nullptr, "Droid object is undefined");
  return pimpl->type == DROID_TYPE::COMMAND &&
         pimpl->group != nullptr &&
         pimpl->group->isCommandGroup();
}

void Droid::upgradeHitPoints() {
  ASSERT_OR_RETURN(, pimpl != nullptr, "Droid object is undefined");

  // use big numbers to scare away rounding errors
  static constexpr auto factor = 10000;
  damageManager->setOriginalHp(calcDroidBaseBody(this));
  auto const increase = damageManager->getOriginalHp() *
          factor / damageManager->getOriginalHp();

  auto hp = MIN(damageManager->getOriginalHp(), damageManager->getHp() * increase / factor + 1);

  DroidTemplate sTemplate;
  templateSetParts(this, &sTemplate);

  // update engine too
  pimpl->baseSpeed = calcDroidBaseSpeed(
          &sTemplate, pimpl->weight, playerManager->getPlayer());

  if (!isTransporter(*this)) return;

  for (auto droid: *pimpl->group->getMembers()) {
    if (droid != this) {
      droid->upgradeHitPoints();
    }
  }
}

void Droid::resetAction() noexcept
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Droid object is undefined");
  pimpl->timeActionStarted = gameTime;
  pimpl->actionPointsDone = 0;
}

bool Droid::isDamaged() const
{
  return damageManager->getHp() < damageManager->getOriginalHp();
}

void Droid::gainExperience(unsigned exp)
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Droid object is undefined");
  pimpl->experience += exp;
}

bool Droid::isVtol() const
{
  auto propulsion = dynamic_cast<PropulsionStats const*>(
          getComponent(COMPONENT_TYPE::PROPULSION));

  return propulsion && !isTransporter(*this) &&
         propulsion->propulsionType == PROPULSION_TYPE::LIFT;
}

void Droid::updateExpectedDamage(unsigned damage, bool isDirect) noexcept {
  if (isDirect) {
    damageManager->setExpectedDamageDirect(damageManager->getExpectedDamageDirect() + damage);
    return;
  }
  damageManager->setExpectedDamageIndirect(damageManager->getExpectedDamageIndirect() + damage);
}

unsigned Droid::calculateSensorRange() const
{
  ASSERT_OR_RETURN(0, pimpl != nullptr, "Droid object is undefined");
  auto const ecm_range = dynamic_cast<EcmStats const*>(
          getComponent(COMPONENT_TYPE::ECM))->upgraded[playerManager->getPlayer()].range;

  return ecm_range == 0 ? dynamic_cast<SensorStats const*>( getComponent(
          COMPONENT_TYPE::SENSOR))->upgraded[playerManager->getPlayer()].range : ecm_range;
}

DROID_TYPE Droid::getType() const noexcept
{
  return pimpl ? pimpl->type : DROID_TYPE::ANY;
}

Droid const* Droid::getCommander() const
{
  return pimpl ? pimpl->commander : nullptr;
}

bool Droid::hasElectronicWeapon() const
{
  return pimpl && pimpl->group->hasElectronicWeapon() &&
         pimpl->type == DROID_TYPE::COMMAND;
}

int Droid::spaceOccupiedOnTransporter() const
{
  if (auto body = dynamic_cast<BodyStats const*>(getComponent(COMPONENT_TYPE::BODY))) {
    return bMultiPlayer ? static_cast<int>(body->size) + 1 : 1;
  }
  return -1;
}

int Droid::getVerticalSpeed() const noexcept
{
  return pimpl ? pimpl->movement->vertical_speed : -1;
}

bool Droid::isFlying() const
{
  ASSERT_OR_RETURN(false, pimpl != nullptr, "Droid object is undefined");
  auto propulsion = dynamic_cast<PropulsionStats const*>(
        getComponent(COMPONENT_TYPE::PROPULSION));

  return (pimpl->movement->status != MOVE_STATUS::INACTIVE || isTransporter(*this)) &&
         propulsion && propulsion->propulsionType == PROPULSION_TYPE::LIFT;
}

unsigned Droid::getSecondaryOrder() const noexcept
{
  return pimpl ? pimpl->secondaryOrder : 0;
}

Group const* Droid::getGroup() const
{
  return pimpl ? pimpl->group : nullptr;
}

Vector2i Droid::getDestination() const
{
  return pimpl ? pimpl->movement->destination : Vector2i();
}

void Droid::incrementKills() noexcept
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Droid object is undefined");
  ++pimpl->kills;
}

/**
 * This function clears all the orders from droid's order list
 * that don't have target as psTarget.
 */
void Droid::orderClearTargetFromDroidList(BaseObject const* psTarget)
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Droid object is undefined");
  for (auto i = 0; i < pimpl->asOrderList.size(); ++i)
  {
    if (pimpl->asOrderList[i].target != psTarget) {
      continue;
    }
    if (i < pimpl->asOrderList.size()) {
      syncDebug("droid%d list erase%d", getId(), psTarget->getId());
    }
    orderDroidListEraseRange(i, i + 1);
      // if this underflows, the ++i will overflow it back
    --i;
  }
}

/**
 * This function checks if the droid is off range. If yes, it uses
 * newAction() to make the droid to move to its target if its
 * target is on range, or to move to its order position if not.
 * @todo droid doesn't shoot while returning to the guard position.
 */
void Droid::orderCheckGuardPosition(int range)
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Droid object is undefined");

  if (pimpl->order->target)  {
    unsigned x, y;
    // repair droids always follow behind - we don't want them
    // jumping into the line of fire
    if ((!(pimpl->type == DROID_TYPE::REPAIRER || pimpl->type == DROID_TYPE::CYBORG_REPAIR)) &&
        dynamic_cast<Droid*>(pimpl->order->target) &&
        orderStateLoc(dynamic_cast<Droid*>(pimpl->order->target),
                      ORDER_TYPE::MOVE, &x, &y)) {
      // got a moving droid - check against where the unit is going
      pimpl->order->pos = {x, y};
    }
    else {
      pimpl->order->pos = pimpl->order->target->getPosition().xy();
    }
  }
  auto xdiff = getPosition().x - pimpl->order->pos.x;
  auto ydiff = getPosition().y - pimpl->order->pos.y;

  if (xdiff * xdiff + ydiff * ydiff <= range * range)
    return;

  if (pimpl->movement->status != MOVE_STATUS::INACTIVE &&
      (pimpl->action == ACTION::MOVE || pimpl->action == ACTION::MOVE_FIRE)) {
    xdiff = pimpl->movement->destination.x - pimpl->order->pos.x;
    ydiff = pimpl->movement->destination.y - pimpl->order->pos.y;

    if (xdiff * xdiff + ydiff * ydiff > range * range) {
      newAction(this, ACTION::MOVE, pimpl->order->pos);
    }
    return;
  }
  newAction(this, ACTION::MOVE, pimpl->order->pos);
}

/**
* This function goes to the droid's order list and erases its
* elements from indexBegin to indexEnd.
*/
void Droid::orderDroidListEraseRange(int indexBegin, int indexEnd)
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Droid object is undefined");
  // do nothing if trying to pop an empty list
  indexEnd = MIN(indexEnd, pimpl->asOrderList.size());
  pimpl->asOrderList.erase(pimpl->asOrderList.begin() + indexBegin,
                    pimpl->asOrderList.begin() + indexEnd);

  // update the indices into list
  pimpl->asOrderList.resize(pimpl->asOrderList.size()
                     - MIN(indexEnd, pimpl->asOrderList.size())
                     - MIN(indexBegin, pimpl->asOrderList.size()));

//  listPendingBegin -= MIN(indexEnd, listPendingBegin)
//                     - MIN(indexBegin, listPendingBegin);
}

/// This function goes to the droid's order list and sets a new order
/// to it from its order list
bool Droid::orderDroidList()
{
  ASSERT_OR_RETURN(false, pimpl != nullptr, "Droid object is undefined");

  if (pimpl->asOrderList.empty())
    return false;

  // there are some orders to give
  auto sOrder = pimpl->asOrderList[0];
  orderDroidListEraseRange(0, 1);

  switch (sOrder.type) {
    using enum ORDER_TYPE;
    case MOVE:
    case SCOUT:
    case DISEMBARK:
      ASSERT(sOrder.target == nullptr && sOrder.structure_stats == nullptr,
             "Extra %s parameters.", getDroidOrderName(sOrder.type).c_str());
      sOrder.target = nullptr;
      sOrder.structure_stats = nullptr;
      break;

    case ATTACK:
    case REPAIR:
    case OBSERVE:
    case DROID_REPAIR:
    case FIRE_SUPPORT:
    case DEMOLISH:
    case HELP_BUILD:
    case BUILD_MODULE:
    case RECOVER:
      ASSERT(sOrder.structure_stats == nullptr,
             "Extra %s parameters.", getDroidOrderName(sOrder.type).c_str());
      sOrder.structure_stats = nullptr;
      break;

    case BUILD:
    case LINE_BUILD:
      ASSERT(sOrder.target == nullptr,
             "Extra %s parameters.",
             getDroidOrderName(sOrder.type).c_str());
      sOrder.target = nullptr;
      break;
    default:
      ASSERT(false, "orderDroidList: Invalid order");
      return false;
  }
  orderDroidBase(&sOrder);
  return true;
}

unsigned Droid::getLastFrustratedTime() const
{
  return pimpl ? pimpl->lastFrustratedTime : 0;
}

unsigned Droid::getArmourPointsAgainstWeapon(WEAPON_CLASS weaponClass) const
{
  ASSERT_OR_RETURN(0, pimpl != nullptr, "Droid object is undefined");
  auto body = dynamic_cast<BodyStats const*>(getComponent(COMPONENT_TYPE::BODY));
  if (!body) return 0;

  switch (weaponClass) {
    case WEAPON_CLASS::KINETIC:
      return body->upgraded[playerManager->getPlayer()].armour;
    case WEAPON_CLASS::HEAT:
      return body->upgraded[playerManager->getPlayer()].thermal;
    default:
      return 0;
  }
}

void Droid::assignVtolToRearmPad(RearmPad* rearmPad)
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Null object");
  pimpl->associatedStructure = dynamic_cast<Structure*>(rearmPad);
}

bool Droid::isAttacking() const noexcept
{
  ASSERT_OR_RETURN(-1, pimpl != nullptr, "Null object");
  using enum DROID_TYPE;
  using enum ACTION;

  return (pimpl->type == WEAPON || pimpl->type == CYBORG || pimpl->type == CYBORG_SUPER) &&
         (pimpl->action == ATTACK || pimpl->action == MOVE_TO_ATTACK || pimpl->action == ROTATE_TO_ATTACK ||
          pimpl->action == VTOL_ATTACK || pimpl->action == MOVE_FIRE);
}

int Droid::calculateElectronicResistance() const
{
  ASSERT_OR_RETURN(-1, pimpl != nullptr, "Null object");
  auto body = dynamic_cast<BodyStats const*>(getComponent(COMPONENT_TYPE::BODY));
  auto resistance = pimpl->experience /
                    (65536 / MAX(1, body->upgraded[playerManager->getPlayer()].resistance));

  resistance = MAX(resistance, body->upgraded[playerManager->getPlayer()].resistance);
  return MIN(resistance, INT16_MAX);
}

bool Droid::isRadarDetector() const
{
  auto sensor = dynamic_cast<SensorStats const*>(getComponent(COMPONENT_TYPE::SENSOR));
  return sensor && sensor->type == SENSOR_TYPE::RADAR_DETECTOR;
}

bool Droid::hasStandardSensor() const
{
  ASSERT_OR_RETURN(false, pimpl != nullptr, "Null object");
  auto sensor = dynamic_cast<SensorStats const*>(getComponent(COMPONENT_TYPE::SENSOR));

  return sensor != nullptr && pimpl->type == DROID_TYPE::SENSOR &&
         (sensor->type == SENSOR_TYPE::VTOL_INTERCEPT ||
          sensor->type == SENSOR_TYPE::STANDARD ||
          sensor->type == SENSOR_TYPE::SUPER);
}

bool Droid::hasCbSensor() const
{
  ASSERT_OR_RETURN(false, pimpl != nullptr, "Null object");
  auto sensor = dynamic_cast<SensorStats const*>(getComponent(COMPONENT_TYPE::SENSOR));

  return sensor != nullptr && pimpl->type == DROID_TYPE::SENSOR &&
         (sensor->type == SENSOR_TYPE::VTOL_CB ||
          sensor->type == SENSOR_TYPE::INDIRECT_CB);
}

void Droid::actionUpdateTransporter()
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Droid object is undefined");
  //check if transporter has arrived
  if (updateTransporter(this)) {
    // Got to destination
    pimpl->action = ACTION::NONE;
  }
}

void Droid::actionSanity()
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Droid object is undefined");
  // Don't waste ammo unless given a direct attack order.
  bool avoidOverkill = pimpl->order->type != ORDER_TYPE::ATTACK &&
                       (pimpl->action == ACTION::ATTACK ||
                        pimpl->action == ACTION::MOVE_FIRE ||
                        pimpl->action == ACTION::MOVE_TO_ATTACK	||
                        pimpl->action == ACTION::ROTATE_TO_ATTACK ||
                        pimpl->action == ACTION::VTOL_ATTACK);

  // clear the target if it has died
  for (auto i = 0; i < MAX_WEAPONS; i++)
  {
    auto bDirect = proj_Direct(weaponManager->weapons[i].stats.get());
    if (!pimpl->actionTargets[i] ||
        !(avoidOverkill
          ? pimpl->actionTargets[i]->damageManager->isProbablyDoomed(bDirect)
          : pimpl->actionTargets[i]->damageManager->isDead())) {
      return;
    }
    setActionTarget(nullptr, i);
    if (i != 0) {
      continue;
    }
    if (pimpl->action == ACTION::MOVE_FIRE || pimpl->action == ACTION::TRANSPORT_IN ||
        pimpl->action == ACTION::TRANSPORT_OUT) {
      continue;
    }
    pimpl->action = ACTION::NONE;
    // if VTOL - return to rearm pad if not patrolling
    if (!isVtol()) continue;
    if ((pimpl->order->type == ORDER_TYPE::PATROL ||
         pimpl->order->type == ORDER_TYPE::CIRCLE) &&
        (!vtolEmpty(*this) ||
         (pimpl->secondaryOrder & DSS_ALEV_MASK) == DSS_ALEV_NEVER)) {
      // Back to the patrol.
      newAction(this, ACTION::MOVE, pimpl->order->pos);
    }
    else {
      moveToRearm();
    }
  }
}

void Droid::setGroup(Group* group)
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Droid object is undefined");
  pimpl->group = group;
}

void Droid::removeDroidFromGroup(Droid* droid)
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Droid object is undefined");
  ASSERT_OR_RETURN(, droid != nullptr, "Droid is null");
  pimpl->group->removeDroid(droid);
}

/* Overall action function that is called by the specific action functions */
void Droid::actionDroidBase(Action const* psAction)
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Droid object is undefined");
  ASSERT_OR_RETURN(, psAction->targetObject == nullptr ||
                     !psAction->targetObject->damageManager->isDead(),
                    "Droid is dead");

  auto psWeapStats = weaponManager->weapons[0].stats.get();
  Vector2i pos{0, 0};

  auto secHoldActive = secondaryGetState(
          SECONDARY_ORDER::HALT_TYPE) == DSS_HALT_HOLD;

  pimpl->timeActionStarted = gameTime;
  bool hasValidWeapon = false;

  for (auto i = 0; i < MAX_WEAPONS; ++i)
  {
    hasValidWeapon |= validTarget(this, psAction->targetObject, i);
  }

  switch (psAction->action) {
    using enum ACTION;
    case NONE:
      // Clear up what ever the droid was doing before if necessary
      if (!isStationary()) {
        moveStopDroid();
      }
      pimpl->action = NONE;
      pimpl->actionPos = Vector2i(0, 0);
      pimpl->timeActionStarted = 0;
      pimpl->actionPointsDone = 0;

      if (numWeapons(*this) == 0) {
        setActionTarget(nullptr, 0);
        break;
      }
      for (auto i = 0; i < numWeapons(*this); ++i)
      {
        setActionTarget(nullptr, i);
      }
      break;

    case ACTION::TRANSPORT_WAIT_TO_FLY_IN:
      pimpl->action = TRANSPORT_WAIT_TO_FLY_IN;
      break;

    case ACTION::ATTACK:
      if (numWeapons(*this) == 0 ||
          isTransporter(*this) ||
          psAction->targetObject == this) {
        break;
      }
      if (!hasValidWeapon) {
        // continuing is pointless, we were given an invalid target
        // for ex. AA gun can't attack ground unit
        break;
      }
      if (hasElectronicWeapon()) {
        //check for low or zero resistance - just zero resistance!
        if (dynamic_cast<Structure*>(psAction->targetObject) &&
            !validStructResistance(dynamic_cast<Structure*>(psAction->targetObject))) {
          //structure is low resistance already so don't attack
          pimpl->action = NONE;
          break;
        }

        // in multiplayer cannot electronically attack a transporter
        if (bMultiPlayer && dynamic_cast<Droid*>(psAction->targetObject) &&
            isTransporter(*dynamic_cast<Droid*>(psAction->targetObject))) {
          pimpl->action = NONE;
          break;
        }
      }

      // note the droid's current pos so that scout & patrol orders know how far the
      // droid has gone during an attack
      // slightly strange place to store this I know, but I didn't want to add any more to the droid
      pimpl->actionPos = pos.xy();
      setActionTarget(psAction->targetObject, 0);

      if (((pimpl->order->type == ORDER_TYPE::ATTACK_TARGET ||
            pimpl->order->type == ORDER_TYPE::NONE ||
            pimpl->order->type == ORDER_TYPE::HOLD ||
            (pimpl->order->type == ORDER_TYPE::GUARD && hasCommander()) ||
            pimpl->order->type == ORDER_TYPE::FIRE_SUPPORT) &&
           secHoldActive) || (!isVtol() && (orderStateObj(
              this, ORDER_TYPE::FIRE_SUPPORT) != nullptr))) {
        pimpl->action = ATTACK;// holding, try attack straightaway
      }

      else if (targetInsideFiringDistance(this, psAction->targetObject, psWeapStats)) { // too close?
        if (!proj_Direct(psWeapStats)) {
          if (psWeapStats->rotate) pimpl->action = ATTACK;
          else {
            pimpl->action = ROTATE_TO_ATTACK;
            moveTurnDroid(pimpl->actionTargets[0]->getPosition().x,
                          pimpl->actionTargets[0]->getPosition().y);
          }
        }
        else if (pimpl->order->type != ORDER_TYPE::HOLD &&
                 secondaryGetState(SECONDARY_ORDER::HALT_TYPE) != DSS_HALT_HOLD) {
          auto pbx = 0;
          auto pby = 0;
          /* direct fire - try and extend the range */
          pimpl->action = MOVE_TO_ATTACK;
          getFallbackPosition(this, psAction->targetObject, &pbx, &pby);

          turnOffMultiMsg(true);
          moveDroidTo(this, {pbx, pby});
          turnOffMultiMsg(false);
        }
      }
      else if (pimpl->order->type != ORDER_TYPE::HOLD &&
               secondaryGetState(SECONDARY_ORDER::HALT_TYPE) != DSS_HALT_HOLD) {
        // approach closer?
        pimpl->action = MOVE_TO_ATTACK;
        turnOffMultiMsg(true);
        moveDroidTo(this, {psAction->targetObject->getPosition().x,
                           psAction->targetObject->getPosition().y});
        turnOffMultiMsg(false);
      }
      else if (pimpl->order->type != ORDER_TYPE::HOLD &&
               secondaryGetState(SECONDARY_ORDER::HALT_TYPE) == DSS_HALT_HOLD) {
        pimpl->action = ATTACK;
      }
      break;

    case MOVE_TO_REARM:
      pimpl->action = MOVE_TO_REARM;
      pimpl->actionPos = psAction->targetObject->getPosition().xy();
      pimpl->timeActionStarted = gameTime;
      setActionTarget(psAction->targetObject, 0);
      pos = pimpl->actionTargets[0]->getPosition().xy();

      if (!actionVTOLLandingPos(this, &pos)) {
        // totally bunged up - give up
        objTrace(getId(), "move to rearm action failed!");
        orderDroid(this, ORDER_TYPE::RETURN_TO_BASE, ModeImmediate);
        break;
      }
      objTrace(getId(), "move to rearm");
      moveDroidToDirect({pos.x, pos.y});
      break;

    case CLEAR_REARM_PAD:
      debug(LOG_NEVER, "Unit %d clearing rearm pad", getId());
      pimpl->action = CLEAR_REARM_PAD;
      setActionTarget(psAction->targetObject, 0);
      pos = pimpl->actionTargets[0]->getPosition().xy();

      if (!actionVTOLLandingPos(this, &pos)) {
        // totally bunged up - give up
        objTrace(getId(), "clear rearm pad action failed!");
        orderDroid(this, ORDER_TYPE::RETURN_TO_BASE, ModeImmediate);
        break;
      }
      objTrace(getId(), "move to clear rearm pad");
      moveDroidToDirect({pos.x, pos.y});
      break;

    case MOVE:
    case TRANSPORT_IN:
    case TRANSPORT_OUT:
    case RETURN_TO_POS:
    case FIRE_SUPPORT_RETREAT:
      pimpl->action = psAction->action;
      pimpl->actionPos = psAction->location;
      pimpl->timeActionStarted = gameTime;
      setActionTarget(psAction->targetObject, 0);
      moveDroidTo(this, psAction->location);
      break;

    case BUILD:
      if (!pimpl->order->structure_stats) {
        pimpl->action = NONE;
        break;
      }
      ASSERT_OR_RETURN(, psAction->location.x > 0 && psAction->location.y > 0, "Bad build order position");
      pimpl->action = MOVE_TO_BUILD;
      pimpl->actionPos = psAction->location;
      moveDroidToNoFormation(this, pimpl->actionPos);
      break;

    case DEMOLISH:
      ASSERT_OR_RETURN(, pimpl->order->type == ORDER_TYPE::DEMOLISH, "cannot start demolish action without a demolish order");
      pimpl->action = MOVE_TO_DEMOLISH;
      pimpl->actionPos = psAction->location;

      ASSERT_OR_RETURN(, pimpl->order->target != nullptr &&
                         dynamic_cast<Structure*>(pimpl->order->target),
                        "invalid target for demolish order");

      pimpl->order->structure_stats = std::make_shared<StructureStats>(
              *dynamic_cast<Structure*>(pimpl->order->target)->getStats());

      setActionTarget(psAction->targetObject, 0);
      moveDroidTo(this, psAction->location);
      break;

    case REPAIR:
      pimpl->action = psAction->action;
      pimpl->actionPos = psAction->location;
      //this needs setting so that automatic repair works
      setActionTarget(psAction->targetObject, 0);

      ASSERT_OR_RETURN(, pimpl->actionTargets[0] != nullptr &&
                         dynamic_cast<Structure*>(pimpl->actionTargets[0]),
                         "invalid target for repair order");

      pimpl->order->structure_stats = std::make_shared<StructureStats>(
              *dynamic_cast<Structure*>(pimpl->actionTargets[0])->getStats());

      if (secHoldActive && (pimpl->order->type == ORDER_TYPE::NONE ||
                            pimpl->order->type == ORDER_TYPE::HOLD)) {
        pimpl->action = REPAIR;
      }
      else if (!secHoldActive && pimpl->order->type != ORDER_TYPE::HOLD ||
               secHoldActive && pimpl->order->type == ORDER_TYPE::REPAIR) {
        pimpl->action = MOVE_TO_REPAIR;
        moveDroidTo(this, psAction->location);
      }
      break;

    case OBSERVE:
      pimpl->action = psAction->action;
      setActionTarget(psAction->targetObject, 0);
      pimpl->actionPos.x = getPosition().x;
      pimpl->actionPos.y = getPosition().y;
      if (secondaryGetState(SECONDARY_ORDER::HALT_TYPE) != DSS_HALT_GUARD &&
          (pimpl->order->type == ORDER_TYPE::NONE || pimpl->order->type == ORDER_TYPE::HOLD)) {
        pimpl->action = visibleObject(
                this, pimpl->actionTargets[0], false) ? OBSERVE : NONE;
        break;
      }
      if (hasCbSensor() ||
          !(!secHoldActive && pimpl->order->type != ORDER_TYPE::HOLD ||
            secHoldActive && pimpl->order->type == ORDER_TYPE::OBSERVE)) {
        break;
      }
      pimpl->action = MOVE_TO_OBSERVE;
      moveDroidTo(this, {pimpl->actionTargets[0]->getPosition().x,
                         pimpl->actionTargets[0]->getPosition().y});
      break;

    case FIRE_SUPPORT:
      pimpl->action = FIRE_SUPPORT;
      if (!isVtol() && !secHoldActive && !dynamic_cast<Structure*>(pimpl->order->target)) {
        moveDroidTo(this, {pimpl->order->target->getPosition().x,
                           pimpl->order->target->getPosition().y}); // movetotarget.
      }
      break;

    case SULK:
      pimpl->action = SULK;
      // hmmm, hope this doesn't cause any problems!
      pimpl->timeActionStarted = gameTime + MIN_SULK_TIME + gameRand(MAX_SULK_TIME - MIN_SULK_TIME);
      break;

    case WAIT_FOR_REPAIR:
      pimpl->action = WAIT_FOR_REPAIR;
      // set the time so we can tell whether the start the self repair or not
      pimpl->timeActionStarted = gameTime;
      break;

    case MOVE_TO_REPAIR_POINT:
      pimpl->action = psAction->action;
      pimpl->actionPos = psAction->location;
      pimpl->timeActionStarted = gameTime;
      setActionTarget(psAction->targetObject, 0);
      moveDroidToNoFormation(this, psAction->location);
      break;

    case WAIT_DURING_REPAIR:
      pimpl->action = WAIT_DURING_REPAIR;
      break;

    case MOVE_TO_REARM_POINT:
      objTrace(getId(), "set to move to rearm pad");
      pimpl->action = psAction->action;
      pimpl->actionPos = psAction->location;
      pimpl->timeActionStarted = gameTime;
      setActionTarget(psAction->targetObject, 0);
      moveDroidToDirect(psAction->location);

      // make sure there aren't any other VTOLs on the rearm pad
      ensureRearmPadClear(dynamic_cast<Structure*>(psAction->targetObject), this);
      break;

    case DROID_REPAIR:
    {
      pimpl->action = psAction->action;
      pimpl->actionPos = psAction->location;
      setActionTarget(psAction->targetObject, 0);
      //initialise the action points
      pimpl->actionPointsDone = 0;
      pimpl->timeActionStarted = gameTime;
      const auto xdiff = (int)getPosition().x - (int)psAction->location.x;
      const auto ydiff = (int)getPosition().y - (int)psAction->location.y;
      if (secHoldActive && (pimpl->order->type == ORDER_TYPE::NONE ||
          pimpl->order->type == ORDER_TYPE::HOLD)) {
        pimpl->action = DROID_REPAIR;
        break;
      }
      if ((!secHoldActive && pimpl->order->type != ORDER_TYPE::HOLD ||
           secHoldActive && pimpl->order->type == ORDER_TYPE::DROID_REPAIR) &&
          xdiff * xdiff + ydiff * ydiff > REPAIR_RANGE * REPAIR_RANGE) {
        pimpl->action = MOVE_TO_DROID_REPAIR;
        moveDroidTo(this, psAction->location);
      }
      break;
    }

    case RESTORE:
      ASSERT_OR_RETURN(, pimpl->order->type == ORDER_TYPE::RESTORE,
                        "cannot start restore action without a restore order");

      pimpl->action = psAction->action;
      pimpl->actionPos = psAction->location;
      ASSERT_OR_RETURN(, pimpl->order->target != nullptr &&
                         dynamic_cast<Structure *>(pimpl->order->target),
                         "invalid target for restore order");

      pimpl->order->structure_stats = std::make_shared<StructureStats>(
              *dynamic_cast<Structure*>(pimpl->order->target)->getStats());

      setActionTarget(psAction->targetObject, 0);
      if (pimpl->order->type != ORDER_TYPE::HOLD) {
        pimpl->action = MOVE_TO_RESTORE;
        moveDroidTo(this, psAction->location);
      }
      break;
    default:
      ASSERT(!"unknown action", "actionUnitBase: unknown action");
      break;
  }
}

/// This function updates all the orders status, according with droid's current order and state
void Droid::orderUpdateDroid()
{
  if (!pimpl) return;
  BaseObject* psObj = nullptr;
  Structure* psStruct, *psWall;
  int xdiff, ydiff;
  bool bAttack;
  int xoffset, yoffset;

  // clear the target if it has died
  if (pimpl->order->target && pimpl->order->target->damageManager->isDead()) {
    setTarget(nullptr);
    objTrace(getId(), "Target dead");
  }

  //clear its base struct if its died
  if (pimpl->associatedStructure && pimpl->associatedStructure->damageManager->isDead()) {
    setBase(nullptr);
    objTrace(getId(), "Base struct dead");
  }

  // check for died objects in the list
  orderCheckList();

  if (damageManager->isDead())
    return;

  using enum ORDER_TYPE;
  switch (pimpl->order->type) {
    case NONE:
    case HOLD:
      // see if there are any orders queued up
      if (orderDroidList()) {
        // started a new order, quit
        break;
      }
        // if you are in a command group, default to guarding the commander
      if (hasCommander() && pimpl->order->type != HOLD &&
          pimpl->order->structure_stats.get() != structGetDemolishStat()) {
        // stop the constructor auto repairing when it is about to demolish
        orderDroidObj(this, GUARD, pimpl->commander, ModeImmediate);
      }
      else if (isTransporter(*this) && !bMultiPlayer) break;
      // default to guarding
      else if (!tryDoRepairlikeAction() && pimpl->order->type != HOLD &&
               pimpl->order->structure_stats.get() != structGetDemolishStat() &&
               secondaryGetState(SECONDARY_ORDER::HALT_TYPE) == DSS_HALT_GUARD && !isVtol()) {
        orderDroidLoc(this, ORDER_TYPE::GUARD,
                      getPosition().x, getPosition().y, ModeImmediate);
      }
      break;

    case TRANSPORT_RETURN:
      if (pimpl->action != ACTION::NONE) break;

      missionMoveTransporterOffWorld(this);
      /* clear order */
      pimpl->order = std::make_unique<Order>(NONE);
      break;

    case TRANSPORT_OUT:
      if (pimpl->action != ACTION::NONE || !playerManager->isSelectedPlayer())
        break;

      if (!getDroidsToSafetyFlag()) {
        //the script can call startMission for this callback for offworld missions
        triggerEvent(TRIGGER_TRANSPORTER_EXIT, this);
        /* clear order */
        pimpl->order = std::make_unique<Order>(NONE);
        // Prevent radical movement vector when adjusting from home to away map exit and entry coordinates.
        pimpl->movement->speed = 0;
        break;
      }

      //move droids in Transporter into holding list
      moveDroidsToSafety(this);
      //we need the transporter to just sit off world for a while...
      orderDroid(this, TRANSPORT_IN, ModeImmediate);
      /* set action transporter waits for timer */
      newAction(this, ACTION::TRANSPORT_WAIT_TO_FLY_IN);
      missionSetReinforcementTime(gameTime);
      pimpl->movement->speed = 0;
      break;

    case TRANSPORT_IN:
      if (pimpl->action != ACTION::NONE || pimpl->movement->status != MOVE_STATUS::INACTIVE)
        break;

      /* clear order */
      pimpl->order = std::make_unique<Order>(NONE);

      if (this == getTrackingDroid() && getWarCamStatus()) {
        camToggleStatus();
      }

      DeSelectDroid(this);

      /*don't try the unload if moving droids to safety and still got some
      droids left  - wait until full and then launch again*/
      if (playerManager->getPlayer() == selectedPlayer && getDroidsToSafetyFlag() &&
          missionDroidsRemaining(selectedPlayer)) {
        resetTransporter();
      }
      else {
        unloadTransporter(this, getPosition().x, getPosition().y, false);
      }

    case MOVE:
      // Just wait for the action to finish then clear the order
      if (pimpl->action == ACTION::NONE || pimpl->action == ACTION::ATTACK) {
        pimpl->order = std::make_unique<Order>(NONE);
      }
      break;

    case RECOVER:
      if (pimpl->order->target == nullptr) {
        pimpl->order = std::make_unique<Order>(NONE);
        break;
      }
      if (pimpl->action != ACTION::NONE) {
        break;
      }
      // stopped moving, but still haven't got the artifact
      newAction(this, ACTION::MOVE,
                {pimpl->order->target->getPosition().x,
                 pimpl->order->target->getPosition().y});
      break;

    case SCOUT:
    case PATROL:
      // if there is an enemy around, attack it
      if (pimpl->action == ACTION::MOVE || pimpl->action == ACTION::MOVE_FIRE ||
          pimpl->action == ACTION::NONE && isVtol()) {
        bool tooFarFromPath = false;
        if (isVtol() && pimpl->order->type == PATROL) {
          // Don't stray too far from the patrol path - only attack if we're near it
          // A fun algorithm to detect if we're near the path
          auto delta = pimpl->order->pos - pimpl->order->pos2;
          if (delta == Vector2i(0, 0)) {
            tooFarFromPath = false;
          }
          else if (abs(delta.x) >= abs(delta.y) &&
                   MIN(pimpl->order->pos.x, pimpl->order->pos2.x) - SCOUT_DIST <= getPosition().x &&
                   getPosition().x <= MAX(pimpl->order->pos.x, pimpl->order->pos2.x) + SCOUT_DIST) {
            tooFarFromPath = (abs((getPosition().x - pimpl->order->pos.x) * delta.y / delta.x +
                                  pimpl->order->pos.y - getPosition().y) > SCOUT_DIST);
          }
          else if (abs(delta.x) <= abs(delta.y) &&
                   MIN(pimpl->order->pos.y, pimpl->order->pos2.y) - SCOUT_DIST <= getPosition().y &&
                   getPosition().y <= MAX(pimpl->order->pos.y, pimpl->order->pos2.y) + SCOUT_DIST) {
            tooFarFromPath = (abs((getPosition().y - pimpl->order->pos.y) * delta.x / delta.y +
                                  pimpl->order->pos.x - getPosition().x) > SCOUT_DIST);
          }
          else {
            tooFarFromPath = true;
          }
        }

        if (!tooFarFromPath) {
          // true if in condition to set actionDroid to attack/observe
          bool attack = secondaryGetState(SECONDARY_ORDER::ATTACK_LEVEL) == DSS_ALEV_ALWAYS &&
                        aiBestNearestTarget(&psObj, 0, SCOUT_ATTACK_DIST) >= 0;

          switch (pimpl->type) {
            using enum DROID_TYPE;
            case CONSTRUCT:
            case CYBORG_CONSTRUCT:
            case REPAIRER:
            case CYBORG_REPAIR:
              tryDoRepairlikeAction();
              break;
            case WEAPON:
            case CYBORG:
            case CYBORG_SUPER:
            case PERSON:
            case COMMAND:
              if (attack) {
                newAction(this, ACTION::ATTACK, psObj);
              }
              break;
            case SENSOR:
              if (attack) {
                newAction(this, ACTION::OBSERVE, psObj);
              }
              break;
            default:
              newAction(this, ACTION::NONE);
              break;
          }
        }
      }

      if (pimpl->action == ACTION::NONE) {
        xdiff = getPosition().x - pimpl->order->pos.x;
        ydiff = getPosition().y - pimpl->order->pos.y;
        if (xdiff * xdiff + ydiff * ydiff < SCOUT_DIST * SCOUT_DIST) {
          if (pimpl->order->type == PATROL) {
            // see if we have anything queued up
            if (orderDroidList()) {
              // started a new order, quit
              break;
            }
            if (isVtol() && !vtolFull(*this) &&
                (pimpl->secondaryOrder & DSS_ALEV_MASK) != DSS_ALEV_NEVER) {
              moveToRearm();
              break;
            }
            // head back to the other point
            std::swap(pimpl->order->pos, pimpl->order->pos2);
            newAction(this, ACTION::MOVE, pimpl->order->pos);
          }
          else {
            pimpl->order = std::make_unique<Order>(NONE);
          }
        }
        else {
          newAction(this, ACTION::MOVE, pimpl->order->pos);
        }
      }

      else if ((pimpl->action == ACTION::ATTACK || pimpl->action == ACTION::VTOL_ATTACK ||
                pimpl->action == ACTION::MOVE_TO_ATTACK || pimpl->action == ACTION::ROTATE_TO_ATTACK ||
                pimpl->action == ACTION::OBSERVE || pimpl->action == ACTION::MOVE_TO_OBSERVE) &&
               secondaryGetState(SECONDARY_ORDER::HALT_TYPE) != DSS_HALT_PURSUE &&
               objectPositionSquareDiff(getPosition(), Vector3i(pimpl->actionPos, 0))
               > SCOUT_ATTACK_DIST * SCOUT_ATTACK_DIST * 4) {
        newAction(this, ACTION::RETURN_TO_POS, pimpl->actionPos);
      }

      if (pimpl->order->type == PATROL && isVtol() && vtolEmpty(*this) &&
          (pimpl->secondaryOrder & DSS_ALEV_MASK) != DSS_ALEV_NEVER) {
        moveToRearm(); // Completely empty (and we're not set to hold fire), don't bother patrolling.
      }
      break;

    case CIRCLE:
      // if there is an enemy around, attack it
      if (pimpl->action == ACTION::MOVE &&
          secondaryGetState(SECONDARY_ORDER::ATTACK_LEVEL) == DSS_ALEV_ALWAYS &&
          aiBestNearestTarget(&psObj, 0, SCOUT_ATTACK_DIST) >= 0) {

        switch (pimpl->type) {
          using enum DROID_TYPE;
          case WEAPON:
          case CYBORG:
          case CYBORG_SUPER:
          case PERSON:
          case COMMAND:
            newAction(this, ACTION::ATTACK, psObj);
            break;
          case SENSOR:
            newAction(this, ACTION::OBSERVE, psObj);
            break;
          default:
            newAction(this, ACTION::NONE);
            break;
        }
      }

      if (pimpl->action == ACTION::NONE || pimpl->action == ACTION::MOVE) {
        if (pimpl->action == ACTION::MOVE && orderDroidList()) {
          // see if we have anything queued up
          // started a new order, quit
          break;
        }

        auto edgeDiff = getPosition().xy() - pimpl->actionPos;
        if (pimpl->action != ACTION::MOVE ||
            dot(edgeDiff, edgeDiff) <= TILE_UNITS * TILE_UNITS * 16) {
          auto diff = getPosition().xy() - pimpl->order->pos;
          auto angle = iAtan2(diff) - DEG(30);
          do
          {
            xoffset = iSinR(angle, 1500);
            yoffset = iCosR(angle, 1500);
            angle -= DEG(10);
          } while (!worldOnMap(pimpl->order->pos.x + xoffset, pimpl->order->pos.y + yoffset));

          // Don't try to fly off map.
          newAction(this, ACTION::MOVE,
                    {pimpl->order->pos.x + xoffset, pimpl->order->pos.y + yoffset});
        }

        if (isVtol() && vtolEmpty(*this) &&
            (pimpl->secondaryOrder & DSS_ALEV_MASK) != DSS_ALEV_NEVER) {
          moveToRearm(); // Completely empty (and we're not set to hold fire), don't bother circling.
        }
        break;
      }

      if (!(pimpl->action == ACTION::ATTACK || pimpl->action == ACTION::VTOL_ATTACK ||
            pimpl->action == ACTION::MOVE_TO_ATTACK || pimpl->action == ACTION::ROTATE_TO_ATTACK ||
            pimpl->action == ACTION::OBSERVE || pimpl->action == ACTION::MOVE_TO_OBSERVE) ||
          secondaryGetState(SECONDARY_ORDER::HALT_TYPE) == DSS_HALT_PURSUE) {
        break;
      }

      // attacking something - see if the droid has gone too far
      xdiff = getPosition().x - pimpl->actionPos.x;
      ydiff = getPosition().y - pimpl->actionPos.y;
      if (xdiff * xdiff + ydiff * ydiff > 2000 * 2000) {
        // head back to the target location
        newAction(this, ACTION::RETURN_TO_POS, pimpl->order->pos);
      }
      break;

    case HELP_BUILD:
    case DEMOLISH:
    case OBSERVE:
    case REPAIR:
    case DROID_REPAIR:
    case RESTORE:
      if (pimpl->action != ACTION::NONE && pimpl->order->target != nullptr) {
        break;
      }

      pimpl->order = std::make_unique<Order>(NONE);
      newAction(this, ACTION::NONE);
      if (playerManager->isSelectedPlayer()) {
        intRefreshScreen();
      }
      break;

    case REARM:
      if (pimpl->order->target == nullptr || pimpl->actionTargets[0] == nullptr) {
        // arm pad destroyed find another
        pimpl->order = std::make_unique<Order>(NONE);
        moveToRearm();
        break;
      }
      if (pimpl->action == ACTION::NONE) {
        pimpl->order = std::make_unique<Order>(NONE);
      }
      break;

    case ATTACK:
    case ATTACK_TARGET:
      if (pimpl->order->target == nullptr || pimpl->order->target->damageManager->isDead()) {
        // if vtol then return to rearm pad as long as there are no other
        // orders queued up
        pimpl->order = std::make_unique<Order>(NONE);
        if (isVtol() && orderDroidList()) break;
        if (!isVtol()) {
          newAction(this, ACTION::NONE);
          break;
        }
        if (!orderDroidList()) {
          moveToRearm();
        }
        break;
      }

      if ((pimpl->action == ACTION::MOVE || pimpl->action == ACTION::MOVE_FIRE) &&
          targetVisible(this, pimpl->order->target, 0) && !isVtol()) {
        // moved near enough to attack change to attack action
        newAction(this, ACTION::ATTACK, pimpl->order->target);
      }

      if (pimpl->action == ACTION::MOVE_TO_ATTACK && !isVtol() &&
          !targetVisible(this, pimpl->order->target, 0) &&
          secondaryGetState(SECONDARY_ORDER::HALT_TYPE) != DSS_HALT_HOLD) {
        // lost sight of the target while chasing it - change to a move action so
        // that the unit will fire on other things while moving
        newAction(this, ACTION::MOVE,
                  {pimpl->order->target->getPosition().x,
                   pimpl->order->target->getPosition().y});
      }

      if (!isVtol() && pimpl->order->target == pimpl->actionTargets[0] &&
          withinRange(this, pimpl->order->target, 0) &&
          (psWall = visGetBlockingWall(this, pimpl->order->target)) &&
          !aiCheckAlliances(psWall->playerManager->getPlayer(), playerManager->getPlayer())) {
        // there is a wall in the way - attack that
        newAction(this, ACTION::ATTACK, psWall);
      }

      if (pimpl->action != ACTION::NONE && pimpl->action != ACTION::CLEAR_REARM_PAD) {
        break;
      }

      if ((pimpl->order->type == ATTACK_TARGET || pimpl->order->type == ATTACK) &&
          secondaryGetState(SECONDARY_ORDER::HALT_TYPE) == DSS_HALT_HOLD &&
          !withinRange(this, pimpl->order->target, 0)) {
        // target is not in range and DSS_HALT_HOLD: give up, don't move
        pimpl->order = std::make_unique<Order>(NONE);
      }
      else if (!isVtol() || allVtolsRearmed(this)) {
        newAction(this, ACTION::ATTACK, pimpl->order->target);
      }
      break;

    case BUILD:
      if (pimpl->action == ACTION::BUILD && pimpl->order->target == nullptr) {
        pimpl->order = std::make_unique<Order>(NONE);
        newAction(this, ACTION::NONE);
        objTrace(getId(), "Clearing build order since build target is gone");
        break;
      }
      if (pimpl->action != ACTION::NONE) break;
      pimpl->order = std::make_unique<Order>(NONE);
      objTrace(getId(), "Clearing build order since build action is reset");

    case EMBARK:
    {
      // only place it can be trapped - in multiPlayer can only put cyborgs onto a Cyborg Transporter
      auto temp = dynamic_cast<Droid const*>(pimpl->order->target); // NOTE: It is possible to have a NULL here

      if (temp && temp->getType() == DROID_TYPE::TRANSPORTER && !isCyborg(this)) {
        pimpl->order = std::make_unique<Order>(NONE);
        newAction(this, ACTION::NONE);
        if (!playerManager->isSelectedPlayer())
          break;

        audio_PlayBuildFailedOnce();
        addConsoleMessage( _("We can't do that! We must be a Cyborg unit to use a Cyborg Transport!"),
                           CONSOLE_TEXT_JUSTIFICATION::DEFAULT, selectedPlayer);
        break;
      }

      // Wait for the action to finish then assign to Transporter (if not already flying)
      if (pimpl->order->target == nullptr || transporterFlying(
              dynamic_cast<Droid*>(pimpl->order->target))) {
        pimpl->order = std::make_unique<Order>(NONE);
        newAction(this, ACTION::NONE);
        break;
      }

      if ((abs(getPosition().x - pimpl->order->target->getPosition().x) >= TILE_UNITS ||
           abs(getPosition().y - pimpl->order->target->getPosition().y) >= TILE_UNITS) &&
          pimpl->action == ACTION::NONE) {
        newAction(this, ACTION::MOVE,
                  {pimpl->order->target->getPosition().x,
                   pimpl->order->target->getPosition().y});
        break;
      }

      // save the target of current droid (the transporter)
      auto transporter = dynamic_cast<Droid *>(pimpl->order->target);
      // order the droid to stop so moveUpdateDroid does not process this unit
      orderDroid(this, STOP, ModeImmediate);
      setTarget(nullptr);
      pimpl->order->target = nullptr;
      secondarySetState(SECONDARY_ORDER::RETURN_TO_LOCATION, DSS_NONE);
      // we must add the droid to the transporter only *after* processing changing its orders (see above)
      transporterAddDroid(transporter, this);
      break;
    }

    case DISEMBARK:
      //only valid in multiPlayer mode
      //this order can only be given to Transporter droids
      /*once the Transporter has reached its destination (and landed),
      get all the units to disembark*/
      if (!bMultiPlayer || !isTransporter(*this) || pimpl->action == ACTION::MOVE ||
          pimpl->action == ACTION::MOVE_FIRE || pimpl->movement->status != MOVE_STATUS::INACTIVE ||
          pimpl->movement->vertical_speed != 0) {
        break;
      }

      unloadTransporter(this, getPosition().x, getPosition().y, false);
      // reset the transporter's order
      pimpl->order = std::make_unique<Order>(NONE);
      break;

    case RETURN_TO_BASE:
      // Just wait for the action to finish then clear the order
      if (pimpl->action == ACTION::NONE) {
        pimpl->order = std::make_unique<Order>(NONE);
        secondarySetState(SECONDARY_ORDER::RETURN_TO_LOCATION, DSS_NONE);
      }
      break;

    case RETURN_TO_REPAIR:
    case RTR_SPECIFIED:
      if (pimpl->order->target == nullptr) {
        // Our target got lost. Let's try again.
        pimpl->order = std::make_unique<Order>(NONE);
        orderDroid(this, RETURN_TO_REPAIR, ModeImmediate);
        break;
      }
      if (pimpl->action != ACTION::NONE) break;

      /* get repair facility pointer */
      psStruct = dynamic_cast<Structure *>(pimpl->order->target);
      ASSERT(psStruct != nullptr, "orderUpdateUnit: invalid structure pointer");

      if (objectPositionSquareDiff(
              getPosition(), pimpl->order->target->getPosition()) < TILE_UNITS * TILE_UNITS * 64) {
        /* action droid to wait */
        newAction(this, ACTION::WAIT_FOR_REPAIR);
        break;
      }
      // move the droid closer to the repair point
      // setting target to null will trigger search for nearest repair point: we might have a better option after all
      pimpl->order->target = nullptr;

    case LINE_BUILD:
    {
      if (pimpl->action != ACTION::NONE &&
          !(pimpl->action == ACTION::BUILD &&
            pimpl->order->target == nullptr)) {
        break;
      }
      // finished building the current structure
      auto const lb = calcLineBuild(pimpl->order->structure_stats.get(),
                                    pimpl->order->direction, pimpl->order->pos, pimpl->order->pos2);
      if (lb.count <= 1) {
        // finished all the structures - done
        pimpl->order = std::make_unique<Order>(NONE);
        break;
      }

      // update the position for another structure
      pimpl->order->pos = lb[1];

      // build another structure
      setTarget(nullptr);
      newAction(this, ACTION::BUILD, pimpl->order->pos);
    }

    case FIRE_SUPPORT:
    {
      if (pimpl->order->target == nullptr) {
        pimpl->order = std::make_unique<Order>(NONE);
        if (isVtol() && !vtolFull(*this)) {
          moveToRearm();
          break;
        }
        newAction(this, ACTION::NONE);
        break;
      }

      //before targetting - check VTOL's are fully armed
      if (vtolEmpty(*this)) {
        moveToRearm();
        break;
      }

      // indirect weapon droid attached to (standard)sensor droid
      BaseObject const *psFireTarget = nullptr;
      if (dynamic_cast<Droid *>(pimpl->order->target)) {
        auto psSpotter = dynamic_cast<Droid *>(pimpl->order->target);

        if (psSpotter->getAction() == ACTION::OBSERVE ||
            (psSpotter->getType() == DROID_TYPE::COMMAND &&
             psSpotter->getAction() == ACTION::ATTACK)) {
          psFireTarget = psSpotter->getTarget(0);
        }
      }
      else if (dynamic_cast<Structure *>(pimpl->order->target)) {
        auto psSpotter = dynamic_cast<Structure const*>(pimpl->order->target);
        psFireTarget = psSpotter->getTarget(0);
      }

      if (!psFireTarget || psFireTarget->damageManager->isDead() ||
          !checkAnyWeaponsTarget(this, psFireTarget)) {
        if (isVtol() && !vtolFull(*this) &&
            pimpl->action != ACTION::NONE &&
            pimpl->action != ACTION::FIRE_SUPPORT) {
          moveToRearm();
        }
        else if (pimpl->action != ACTION::FIRE_SUPPORT &&
                 pimpl->action != ACTION::FIRE_SUPPORT_RETREAT) {
          newAction(this, ACTION::FIRE_SUPPORT, pimpl->order->target);
        }
        break;
      }

      bAttack = false;
      if (isVtol() && !vtolEmpty(*this) &&
          (pimpl->action == ACTION::MOVE_TO_REARM ||
           pimpl->action == ACTION::WAIT_FOR_REARM) &&
          pimpl->movement->status != MOVE_STATUS::INACTIVE) {
        // catch vtols that were attacking another target which was destroyed
        // get them to attack the new target rather than returning to rearm
        bAttack = true;
      }
      else if (isVtol() && allVtolsRearmed(this)) {
        bAttack = true;
      }
      else {
        bAttack = true;
      }

      //if not currently attacking or target has changed
      if (bAttack && (!droidAttacking(this) ||
                      psFireTarget != pimpl->actionTargets[0])) {
        //get the droid to attack
        dynamic_cast<Structure *>(pimpl->order->target)->actionDroidTarget(this, ACTION::ATTACK, 0);
      }
      break;
    }

    case RECYCLE:
      if (pimpl->order->target == nullptr) {
        pimpl->order = std::make_unique<Order>(NONE);
        newAction(this, ACTION::NONE);
        break;
      }
      if (adjacentToBuildSite(this, dynamic_cast<Structure *>(pimpl->order->target)->getStats(),
                              {pimpl->order->target->getPosition().x, pimpl->order->target->getPosition().y},
                              dynamic_cast<Structure *>(pimpl->order->target)->getRotation().direction)) {
        recycleDroid();
        break;
      }

      if (pimpl->action == ACTION::NONE) {
        newAction(this, ACTION::MOVE,
                  {pimpl->order->target->getPosition().x,
                   pimpl->order->target->getPosition().y});
      }
      break;

    case GUARD:
      if (orderDroidList()) {
        // started a queued order - quit
        break;
      }
      if (pimpl->action == ACTION::NONE || pimpl->action == ACTION::MOVE ||
               pimpl->action == ACTION::MOVE_FIRE) {
        // not doing anything, make sure the droid is close enough
        // to the thing it is defending
        if ((!(pimpl->type == DROID_TYPE::REPAIRER || pimpl->type == DROID_TYPE::CYBORG_REPAIR)) &&
            pimpl->order->target != nullptr &&
            dynamic_cast<Droid*>(pimpl->order->target) &&
            dynamic_cast<Droid*>(pimpl->order->target)->getType() == DROID_TYPE::COMMAND) {
          // guarding a commander, allow more space
          orderCheckGuardPosition(DEFEND_CMD_BASEDIST);
        }
        else {
          orderCheckGuardPosition(DEFEND_BASEDIST);
        }
      }
      else if (pimpl->type == DROID_TYPE::REPAIRER ||
               pimpl->type == DROID_TYPE::CYBORG_REPAIR) {
        // repairing something, make sure the droid doesn't go too far
        orderCheckGuardPosition(REPAIR_MAXDIST);
      }
      else if (pimpl->type == DROID_TYPE::CONSTRUCT ||
               pimpl->type == DROID_TYPE::CYBORG_CONSTRUCT) {
        // repairing something, make sure the droid doesn't go too far
        orderCheckGuardPosition(CONSTRUCT_MAXDIST);
      }
      else if (isTransporter(*this)) {}
      else if (!vtolRearming(*this)) {
        // attacking something, make sure the droid doesn't go too far
        if (pimpl->order->target != nullptr &&
            dynamic_cast<Droid*>(pimpl->order->target) &&
            dynamic_cast<Droid*>(pimpl->order->target)->getType() == DROID_TYPE::COMMAND) {
          // guarding a commander, allow more space
          orderCheckGuardPosition(DEFEND_CMD_MAXDIST);
        }
        else {
          orderCheckGuardPosition(DEFEND_MAXDIST);
        }
      }

      // get combat units in a command group to attack the commanders target
      if (!hasCommander() || !(numWeapons(*this) > 0)) {
        tryDoRepairlikeAction();
        break;
      }

      if (pimpl->commander->getAction() == ACTION::ATTACK &&
          pimpl->commander->getTarget(0) &&
          !pimpl->commander->getTarget(0)->damageManager->isDead()) {
        psObj = pimpl->commander->getTarget(0);

        if (pimpl->action == ACTION::ATTACK || pimpl->action == ACTION::MOVE_TO_ATTACK) {
          if (pimpl->actionTargets[0] != psObj) {
            newAction(this, ACTION::ATTACK, psObj);
          }
        }
        else if (pimpl->action != ACTION::MOVE) {
          newAction(this, ACTION::ATTACK, psObj);
        }
      }

      // make sure units in a command group are actually guarding the commander
      psObj = orderStateObj(this, GUARD);// find out who is being guarded by the droid
      if (psObj == nullptr || psObj != dynamic_cast<BaseObject const*>(pimpl->commander)) {
        orderDroidObj(this, GUARD, pimpl->commander, ModeImmediate);
      }

      tryDoRepairlikeAction();
      break;
    default:
      ASSERT(false, "orderUpdateUnit: unknown order");
  }

  // catch any vtol that is rearming but has finished his order
  if (pimpl->order->type == NONE && vtolRearming(*this) &&
      (pimpl->actionTargets[0] == nullptr || !pimpl->actionTargets[0]->damageManager->isDead())) {
    pimpl->order = std::make_unique<Order>(REARM, *pimpl->actionTargets[0]);
  }

  if (!damageManager->isSelected())
    return;

  // Tell us what the droid is doing.
  snprintf(DROIDDOING, sizeof(DROIDDOING),
           "%.12s,id(%d) order(%d):%s action(%d):%s secondary:%x move:%s",
           droidGetName(this), getId(),
           pimpl->order->type, getDroidOrderName(pimpl->order->type).c_str(), pimpl->action,
           actionToString(pimpl->action).c_str(), pimpl->secondaryOrder,
           moveDescription(pimpl->movement->status).c_str());
}

void Droid::removeDroidBase()
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Droid object is undefined");
  if (damageManager->isDead()) {
    // droid has already been killed, quit
    syncDebug("droid already dead");
    return;
  }

  // kill all the droids inside the transporter
  if (isTransporter(*this) && pimpl->group) {
    //free all droids associated with this Transporter
    pimpl->group->vanishAll();
  }

  // leave the current group if any
  if (pimpl->group) {
    pimpl->group->removeDroid(this);
    pimpl->group = nullptr;
  }

  /* Put Deliv. Pts back into world when a command droid dies */
  if (pimpl->type == DROID_TYPE::COMMAND) {
    for (auto& psStruct : playerList[playerManager->getPlayer()].structures)
    {
      if (StructIsFactory(&psStruct) &&
          dynamic_cast<Factory*>(&psStruct)->getCommander() == this) {
        assignFactoryCommandDroid(&psStruct, nullptr);
      }
    }
  }

  // Check to see if constructor droid currently trying to find a location to build
  if (playerManager->isSelectedPlayer() && damageManager->isSelected() &&
      isConstructionDroid(this) && tryingToGetLocation()) {
    auto numSelectedConstructors = 0;
    for (auto& psDroid : playerList[playerManager->getPlayer()].droids)
    {
      numSelectedConstructors += static_cast<int>(psDroid.damageManager->isSelected() &&
                                                  isConstructionDroid(&psDroid));
    }
    if (numSelectedConstructors <= 1) { // If we were the last selected construction droid.
      kill3DBuilding();
    }
  }

  if (playerManager->isSelectedPlayer()) {
    intRefreshScreen();
  }

  killDroid(this);
}

// Update the action state for a droid
void Droid::actionUpdateDroid()
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Droid object is undefined");
  using enum ACTION;
  bool (*actionUpdateFunc)(Droid* psDroid) = nullptr;
  bool nonNullWeapon[MAX_WEAPONS] = {false};
  BaseObject* psTargets[MAX_WEAPONS] = {nullptr};
  bool hasValidWeapon = false;
  bool hasVisibleTarget = false;
  bool targetVisibile[MAX_WEAPONS] = {false};
  bool bHasTarget = false;
  bool bDirect = false;
  Structure* blockingWall = nullptr;
  bool wallBlocked = false;

  auto psPropStats = dynamic_cast<PropulsionStats const*>(getComponent(COMPONENT_TYPE::PROPULSION));
  bool secHoldActive = secondaryGetState(SECONDARY_ORDER::HALT_TYPE) == DSS_HALT_HOLD;

  actionSanity();
  //if the droid has been attacked by an EMP weapon, it is temporarily disabled
  if (damageManager->getLastHitWeapon() == WEAPON_SUBCLASS::EMP) {
    if (gameTime - damageManager->getTimeLastHit() <= EMP_DISABLE_TIME) {
      // get out without updating
      return;
    }
    // the actionStarted time needs to be adjusted
    pimpl->timeActionStarted += (gameTime - damageManager->getTimeLastHit());
    // reset the lastHit parameters
    damageManager->setTimeLastHit(0);
    damageManager->setLastHitWeapon(WEAPON_SUBCLASS::COUNT);
  }

  // HACK: Apparently we can't deal with a droid that only has NULL weapons ?
  // FIXME: Find out whether this is really necessary
  if (numWeapons(*this) <= 1) {
    nonNullWeapon[0] = true;
  }

  WeaponStats const* psWeapStats;
  switch (pimpl->action) {
    case NONE:
    case WAIT_FOR_REPAIR:
      // doing nothing
      // see if there's anything to shoot.
      if (numWeapons(*this) <= 0 || isVtol() ||
          !(pimpl->order->type == ORDER_TYPE::NONE ||
            pimpl->order->type == ORDER_TYPE::HOLD ||
            pimpl->order->type == ORDER_TYPE::RETURN_TO_REPAIR ||
            pimpl->order->type == ORDER_TYPE::GUARD)) {
        break;
      }

      for (auto i = 0; i < numWeapons(*this); ++i)
      {
        if (!nonNullWeapon[i]) continue;

        BaseObject* psTemp1 = nullptr;
        psWeapStats = weaponManager->weapons[i].stats.get();
        if (!psWeapStats->rotate || aiBestNearestTarget(&psTemp1, i) < 0 ||
            secondaryGetState(SECONDARY_ORDER::ATTACK_LEVEL) != DSS_ALEV_ALWAYS) {
          continue;
        }
        pimpl->action = ATTACK;
        setActionTarget(psTemp1, i);
      }
      break;

    case WAIT_DURING_REPAIR:
      // Check that repair facility still exists
      if (!pimpl->order->target) {
        pimpl->action = NONE;
        break;
      }
      if (pimpl->order->type == ORDER_TYPE::RETURN_TO_REPAIR &&
          pimpl->order->rtrType == RTR_DATA_TYPE::REPAIR_FACILITY) {
        // move back to the repair facility if necessary
        if (isStationary() &&
            !adjacentToBuildSite(this,
                                 {pimpl->order->target->getPosition().x,
                                  pimpl->order->target->getPosition().y},
                                 dynamic_cast<Structure *>(pimpl->order->target)->getRotation().direction,
                                 dynamic_cast<Structure *>(pimpl->order->target)->getStats())) {

          moveDroidToNoFormation(this,
                                 {pimpl->order->target->getPosition().x,
                                  pimpl->order->target->getPosition().y});
        }
        break;
      }

      if (pimpl->order->type == ORDER_TYPE::RETURN_TO_REPAIR &&
          pimpl->order->rtrType == RTR_DATA_TYPE::DROID && isStationary()) {
        if (!adjacentToOtherDroid(this, dynamic_cast<Droid *>(pimpl->order->target))) {
          moveDroidToNoFormation(this, {pimpl->order->target->getPosition().x,
                                        pimpl->order->target->getPosition().y});
          break;
        }
        moveStopDroid();
      }
      break;

    case TRANSPORT_WAIT_TO_FLY_IN:
    {
      //if we're moving droids to safety and currently waiting to fly back in, see if time is up
      if (playerManager->getPlayer() != selectedPlayer || !getDroidsToSafetyFlag()) {
        break;
      }

      bool enoughTimeRemaining = (mission.time - (gameTime - mission.startTime)) >= (60 * GAME_TICKS_PER_SEC);
      if (!((int) (mission.ETA - (gameTime - missionGetReinforcementTime())) <= 0) || !enoughTimeRemaining) {
        break;
      }

      unsigned droidX, droidY;
      if (!droidRemove(this, mission.apsDroidLists)) {
        ASSERT_OR_RETURN(, false, "Unable to remove transporter from mission list");
      }
      addDroid(this);
        //set the x/y up since they were set to INVALID_XY when moved offWorld
      missionGetTransporterExit(selectedPlayer, &droidX, &droidY);
      setPosition({droidX, droidY, getPosition().z});
      //fly Transporter back to get some more droids
      orderDroidLoc(this, ORDER_TYPE::TRANSPORT_IN, getLandingX(selectedPlayer),
                    getLandingY(selectedPlayer), ModeImmediate);
      break;
    }

    case MOVE:
    case RETURN_TO_POS:
    case FIRE_SUPPORT_RETREAT:
      // moving to a location
      if (isStationary()) {
        bool notify = pimpl->action == MOVE;
        // Got to destination
        pimpl->action = NONE;
        if (notify) {
          // notify scripts we have reached the destination
          // (also triggers when patrolling and reached a waypoint)
          triggerEventDroidIdle(this);
        }
        break;
      }

      //added multiple weapon check
      if (numWeapons(*this) == 0)
        break;

      for (auto i = 0; i < numWeapons(*this); ++i)
      {
        if (!nonNullWeapon[i]) continue;
        BaseObject*psTemp1 = nullptr;

        // i moved psWeapStats flag update there
        psWeapStats = weaponManager->weapons[i].stats.get();
        if (!isVtol() && psWeapStats->rotate && psWeapStats->fireOnMove &&
            aiBestNearestTarget(&psTemp1, i) >= 0 &&
            secondaryGetState(SECONDARY_ORDER::ATTACK_LEVEL) == DSS_ALEV_ALWAYS) {
          pimpl->action = MOVE_FIRE;
          setActionTarget(psTemp1, i);
        }
      }
      break;

    case TRANSPORT_IN:
    case TRANSPORT_OUT:
      actionUpdateTransporter();
      break;

    case MOVE_FIRE:
      // check if vtol is armed
      if (vtolEmpty(*this)) {
        moveToRearm();
      }
      // if droid stopped, it can no longer be in ACTION::MOVE_FIRE
      if (isStationary()) {
        pimpl->action = NONE;
        break;
      }

      // loop through weapons and look for target for each weapon
      bHasTarget = false;
      for (auto i = 0; i < numWeapons(*this); ++i)
      {
        bDirect = proj_Direct(weaponManager->weapons[i].stats.get());
        blockingWall = nullptr;
        // Does this weapon have a target?
        if (pimpl->actionTargets[i] != nullptr) {
          // Is target worth shooting yet?
          if (pimpl->actionTargets[i]->damageManager->isProbablyDoomed(bDirect)) {
            setActionTarget(nullptr, i);
          }
            // Is target from our team now? (Electronic Warfare)
          else if (electronicDroid(this) &&
                   playerManager->getPlayer() == pimpl->actionTargets[i]->playerManager->getPlayer()) {
            setActionTarget(nullptr, i);
          }
            // Is target blocked by a wall?
          else if (bDirect && visGetBlockingWall(this, pimpl->actionTargets[i])) {
            setActionTarget(nullptr, i);
          }
            // I have a target!
          else {
            bHasTarget = true;
          }
        }
          // This weapon doesn't have a target
        else {
          // Can we find a good target for the weapon?
          BaseObject* psTemp;
          if (aiBestNearestTarget(&psTemp, i) >= 0) {
            // assuming aiBestNearestTarget checks for electronic warfare
            bHasTarget = true;
            setActionTarget(psTemp, i); // this updates psDroid->psActionTarget[i] to != NULL
          }
        }
        // If we have a target for the weapon: is it visible?
        if (pimpl->actionTargets[i] != nullptr &&
            visibleObject(this, pimpl->actionTargets[i],
                          false) > UBYTE_MAX / 2) {

          hasVisibleTarget = true; // droid have a visible target to shoot
          targetVisibile[i] = true; // it is at least visible for this weapon
        }
      }
      // if there is at least one target
      if (bHasTarget) {
        // loop through weapons
        for (auto i = 0; i < numWeapons(*this); ++i)
        {
          auto psStats = weaponManager->weapons[i].stats.get();
          wallBlocked = false;

          // has weapon a target? is target valid?
          if (pimpl->actionTargets[i] == nullptr ||
              !validTarget(this, pimpl->actionTargets[i], i)) {
            continue;
          }

          // is target visible and weapon is not a Nullweapon?
          if (!targetVisibile[i] || !nonNullWeapon[i])
            continue;

          // to fix a AA-weapon attack ground unit exploit
          BaseObject*psActionTarget1 = nullptr;
          blockingWall = dynamic_cast<Structure*>(
                  visGetBlockingWall(this, pimpl->actionTargets[i]));

          if (proj_Direct(psStats) && blockingWall) {
            WEAPON_EFFECT weapEffect1 = psStats->weaponEffect;

            if (!aiCheckAlliances(playerManager->getPlayer(), blockingWall->playerManager->getPlayer()) &&
                asStructStrengthModifier[weapEffect1][blockingWall->getStats()->strength] >=
                   MIN_STRUCTURE_BLOCK_STRENGTH) {
              psActionTarget1 = blockingWall;
              setActionTarget(psActionTarget1, i); // attack enemy wall
            }
            else {
              wallBlocked = true;
            }
          }
          else {
            psActionTarget1 = pimpl->actionTargets[i];
          }

          // is the turret aligned with the target?
          if (!wallBlocked &&
              rotateTurret(this, psActionTarget1, i)) {
            // In range - fire !!!
            combFire(&weaponManager->weapons[i], this, psActionTarget1, i);
          }
        }

        // Droid don't have a visible target and it is not in pursue mode
        if (!hasVisibleTarget &&
            secondaryGetState(SECONDARY_ORDER::ATTACK_LEVEL) != DSS_ALEV_ALWAYS) {
          // Target lost
          pimpl->action = MOVE;
        }
      }
        // it don't have a target, change to ACTION::MOVE
      else {
        pimpl->action = ACTION::MOVE;
      }
      //check its a VTOL unit since adding Transporter's into multiPlayer
      /* check vtol attack runs */
      if (isVtol()) {
        updateAttackRuns(this);
      }
      break;
    case ACTION::ATTACK:
    case ACTION::ROTATE_TO_ATTACK:
    {
      if (pimpl->actionTargets[0] == nullptr && pimpl->actionTargets[1] != nullptr) {
        break;
      }
      ASSERT_OR_RETURN(, pimpl->actionTargets[0] != nullptr, "target is NULL while attacking");

      if (pimpl->action == ACTION::ROTATE_TO_ATTACK) {
        if (pimpl->movement->status == MOVE_STATUS::TURN_TO_TARGET) {
          moveTurnDroid(pimpl->actionTargets[0]->getPosition().x,
                        pimpl->actionTargets[0]->getPosition().y);
          break; // Still turning.
        }
        pimpl->action = ATTACK;
      }

      //check the target hasn't become one the same player ID - Electronic Warfare
      if (electronicDroid(this) &&
          playerManager->getPlayer() == pimpl->actionTargets[0]->playerManager->getPlayer()) {
        for (auto i = 0; i < numWeapons(*this); ++i)
        {
          setActionTarget(nullptr, i);
        }
        pimpl->action = NONE;
        break;
      }

      bHasTarget = false;
      wallBlocked = false;
      for (auto i = 0; i < numWeapons(*this); ++i)
      {
        BaseObject* psActionTarget;

        if (i > 0) {
          // if we're ordered to shoot something, and we can, shoot it
          if ((pimpl->order->type == ORDER_TYPE::ATTACK ||
               pimpl->order->type == ORDER_TYPE::ATTACK_TARGET) &&
              pimpl->actionTargets[i] != pimpl->actionTargets[0] &&
              validTarget(this, pimpl->actionTargets[0], i) &&
              withinRange(this, pimpl->actionTargets[0], i)) {
            setActionTarget(pimpl->actionTargets[0], i);
          }
          // if we still don't have a target, try to find one
          else {
            if (pimpl->actionTargets[i] == nullptr &&
                aiChooseTarget(this, &psTargets[i],
                               i, false, nullptr)) {
              // Can probably just use psTarget instead of psTargets[i], and delete the psTargets variable.
              setActionTarget(psTargets[i], i);
            }
          }
        }

        if (pimpl->actionTargets[i]) {
          psActionTarget = pimpl->actionTargets[i];
        }
        else {
          psActionTarget = pimpl->actionTargets[0];
        }

        if (nonNullWeapon[i]
            && targetVisible(this, psActionTarget, i)
            && withinRange(this, psActionTarget, i)) {
          const auto& psWeapStats = weaponManager->weapons[i].stats.get();
          auto weapEffect = psWeapStats->weaponEffect;
          blockingWall = dynamic_cast<Structure*>(visGetBlockingWall(this, psActionTarget));

          // if a wall is inbetween us and the target, try firing at the wall if our
          // weapon is good enough
          if (proj_Direct(psWeapStats) && blockingWall) {
            if (!aiCheckAlliances(playerManager->getPlayer(), blockingWall->playerManager->getPlayer()
                && asStructStrengthModifier[weapEffect][blockingWall->getStats()->strength] >=
                   MIN_STRUCTURE_BLOCK_STRENGTH) {
              psActionTarget = blockingWall;
              setDroidActionTarget(psDroid, psActionTarget, i);
            }
            else {
              wallBlocked = true;
            }
          }

          if (!bHasTarget) {
            bHasTarget = withinRange(this, psActionTarget,
                                     i, false);
          }

          if (validTarget(this, psActionTarget, i) && !wallBlocked) {
            auto dirDiff = 0;

            if (!psWeapStats->rotate) {
              // no rotating turret - need to check aligned with target
              const auto targetDir = calcDirection(
                      getPosition().x,
                      getPosition().y,
                      psActionTarget->getPosition().x,
                      psActionTarget->getPosition().y);

              dirDiff = abs(angleDelta(targetDir - getRotation().direction));
            }

            if (dirDiff > FIXED_TURRET_DIR) {
              if (i > 0) {
                if (pimpl->actionTargets[i] != pimpl->actionTargets[0]) {
                  // Nope, can't shoot this, try something else next time
                  setActionTarget(nullptr, i);
                }
              }
              else if (pimpl->movement->status != MOVE_STATUS::SHUFFLE) {
                pimpl->action = ROTATE_TO_ATTACK;
                moveTurnDroid(psActionTarget->getPosition().x,
                              psActionTarget->getPosition().y);
              }
            }
            else if (!psWeapStats->rotate ||
                       rotateTurret(this, psActionTarget, i)) {
              /* In range - fire !!! */
              combFire(&weaponManager->weapons[i], this, psActionTarget, i);
            }
          }
          else if (i > 0) {
            // Nope, can't shoot this, try something else next time
            setActionTarget(nullptr, i);
          }
        }
        else if (i > 0) {
          // Nope, can't shoot this, try something else next time
          setActionTarget(nullptr, i);
        }
      }

      if (bHasTarget && !wallBlocked)
        break;

      BaseObject* psTarget;
      bool supportsSensorTower = !isVtol() &&
              (psTarget = orderStateObj(this, ORDER_TYPE::FIRE_SUPPORT))
              && dynamic_cast<Structure*>(psTarget);

      if (secHoldActive && (pimpl->order->type == ORDER_TYPE::ATTACK_TARGET ||
                            pimpl->order->type == ORDER_TYPE::FIRE_SUPPORT)) {
        pimpl->action = ACTION::NONE; // secondary holding, cancel the order.
      }
      else if (secondaryGetState(SECONDARY_ORDER::HALT_TYPE) == DSS_HALT_PURSUE &&
               !supportsSensorTower &&
               !(pimpl->order->type == ORDER_TYPE::HOLD ||
                 pimpl->order->type == ORDER_TYPE::RETURN_TO_REPAIR)) {
        //We need this so pursuing doesn't stop if a unit is ordered to move somewhere while
        //it is still in weapon range of the target when reaching the end destination.
        //Weird case, I know, but keeps the previous pursue order intact.
        pimpl->action = MOVE_TO_ATTACK; // out of range - chase it
      }
      else if (supportsSensorTower ||
               pimpl->order->type == ORDER_TYPE::NONE ||
               pimpl->order->type == ORDER_TYPE::HOLD ||
               pimpl->order->type == ORDER_TYPE::RETURN_TO_REPAIR) {
        // don't move if on hold or firesupport for a sensor tower
        // also don't move if we're holding position or waiting for repair
        pimpl->action = NONE; // holding, cancel the order.
      }
        //Units attached to commanders are always guarding the commander
      else if (secHoldActive && pimpl->order->type == ORDER_TYPE::GUARD && hasCommander()) {
        auto commander = pimpl->commander;

        if (commander->getOrder()->type == ORDER_TYPE::ATTACK_TARGET ||
            commander->getOrder()->type == ORDER_TYPE::FIRE_SUPPORT ||
            commander->getOrder()->type == ORDER_TYPE::ATTACK) {
          pimpl->action = MOVE_TO_ATTACK;
        }
        else {
          pimpl->action = NONE;
        }
      }
      else if (secondaryGetState(SECONDARY_ORDER::HALT_TYPE) != DSS_HALT_HOLD) {
        pimpl->action = MOVE_TO_ATTACK; // out of range - chase it
      }
      else {
        pimpl->order->target = nullptr;
        pimpl->action = NONE;
      }
      break;
    }

    case VTOL_ATTACK:
    {
      const bool targetIsValid = validTarget(this, pimpl->actionTargets[0], 0);
      //uses vtResult
      if (pimpl->actionTargets[0] != nullptr && targetIsValid) {
        //check if vtol that its armed
        if ((vtolEmpty(*this)) ||
            (pimpl->actionTargets[0] == nullptr) ||
            // check the target hasn't become one the same player ID - Electronic Warfare
            hasElectronicWeapon() &&
            playerManager->getPlayer() == pimpl->actionTargets[0]->playerManager->getPlayer() ||
            // Huh? !targetIsValid can't be true, we just checked for it
            !targetIsValid) {
          moveToRearm();
          break;
        }

        for (auto i = 0; i < numWeapons(*this); ++i)
        {
          if (!nonNullWeapon[i] || !validTarget(this, pimpl->actionTargets[0], i))
            continue;

          psWeapStats = weaponManager->weapons[i].stats.get();
          if (!targetVisible(this, pimpl->actionTargets[0], i))
            continue;

          if (withinRange(this, pimpl->actionTargets[0], i)) {
            if (playerManager->getPlayer() == selectedPlayer) {
              audio_QueueTrackMinDelay(ID_SOUND_COMMENCING_ATTACK_RUN2,
                                       VTOL_ATTACK_AUDIO_DELAY);
            }

            if (rotateTurret(this, pimpl->actionTargets[0], i)) {
              // In range - fire !!!
              combFire(&weaponManager->weapons[i], this,
                       pimpl->actionTargets[0], i);
            }
          }
          else {
            rotateTurret(this, pimpl->actionTargets[0], i);
          }
        }
      }

      /* circle around target if hovering and not cyborg */
      Vector2i attackRunDelta = getPosition().xy() - pimpl->movement->destination;
      if (isStationary() || dot(attackRunDelta, attackRunDelta) < TILE_UNITS * TILE_UNITS) {
        addAttackRun(this);
      }
      else if (pimpl->actionTargets[0] != nullptr && targetIsValid) {
        // if the vtol is close to the target, go around again
        Vector2i diff = (getPosition() - pimpl->actionTargets[0]->getPosition()).xy();
        const auto rangeSq = dot(diff, diff);
        if (rangeSq < VTOL_ATTACK_TARGET_DIST * VTOL_ATTACK_TARGET_DIST) {
          // don't do another attack run if already moving away from the target
          diff = pimpl->movement->destination - pimpl->actionTargets[0]->getPosition().xy();
          if (dot(diff, diff) < VTOL_ATTACK_TARGET_DIST * VTOL_ATTACK_TARGET_DIST) {
            addAttackRun(this);
          }
        }
          // in case psWeapStats is still NULL
        else if (psWeapStats) {
          // if the vtol is far enough away head for the target again
          const auto maxRange = proj_GetLongRange(psWeapStats, playerManager->getPlayer());
          if (rangeSq > maxRange * maxRange) {
            // don't do another attack run if already heading for the target
            diff = pimpl->movement->destination - pimpl->actionTargets[0]->getPosition().xy();
            if (dot(diff, diff) > VTOL_ATTACK_TARGET_DIST * VTOL_ATTACK_TARGET_DIST) {
              moveDroidToDirect({pimpl->actionTargets[0]->getPosition().x,
                                 pimpl->actionTargets[0]->getPosition().y});
            }
          }
        }
      }
      break;
    }
    case ACTION::MOVE_TO_ATTACK:
      // send vtols back to rearm
      if (isVtol() && vtolEmpty(*this)) {
        moveToRearm();
        break;
      }
      ASSERT_OR_RETURN(, pimpl->actionTargets[0] != nullptr, "action update move to attack target is NULL");
      for (auto i = 0; i < numWeapons(*this); ++i)
      {
        hasValidWeapon |= validTarget(this, pimpl->actionTargets[0], i);
      }
      //check the target hasn't become one the same player ID - Electronic Warfare, and that the target is still valid.
      if ((hasElectronicWeapon() &&
           playerManager->getPlayer() == pimpl->actionTargets[0]->playerManager->getPlayer()) || !hasValidWeapon) {
        for (auto i = 0; i < numWeapons(*this); ++i)
        {
          setActionTarget(nullptr, i);
        }
        pimpl->action = NONE;
      }
      else {
        if (targetVisible(this, pimpl->actionTargets[0], 0)) {
          for (auto i = 0; i < numWeapons(*this); ++i)
          {
            if (nonNullWeapon[i] &&
                validTarget(this, pimpl->actionTargets[0], i) &&
                targetVisible(this, pimpl->actionTargets[0], i)) {
              bool chaseBloke = false;
              psWeapStats = weaponManager->weapons[i].stats.get();

              if (psWeapStats->rotate) {
                rotateTurret(this, pimpl->actionTargets[0], i);
              }

              if (!isVtol() &&
                  dynamic_cast<Droid*>(pimpl->actionTargets[0]) &&
                  dynamic_cast<Droid*>(pimpl->actionTargets[0])->getType() == DROID_TYPE::PERSON &&
                  psWeapStats->fireOnMove) {
                chaseBloke = true;
              }

              if (withinRange(this, pimpl->actionTargets[0], i) &&
                  !chaseBloke) {
                /* init vtol attack runs count if necessary */
                if (psPropStats->propulsionType == PROPULSION_TYPE::LIFT) {
                  pimpl->action = VTOL_ATTACK;
                }
                else {
                  if (withinRange(
                              this, pimpl->actionTargets[0],
                              i, false)) {
                    moveStopDroid();
                  }

                  if (psWeapStats->rotate) {
                    pimpl->action = ATTACK;
                  }
                  else {
                    pimpl->action = ROTATE_TO_ATTACK;
                    moveTurnDroid(pimpl->actionTargets[0]->getPosition().x,
                                  pimpl->actionTargets[0]->getPosition().y);
                  }
                }
              }
              else if (withinRange(this, pimpl->actionTargets[0], i)) {
                // fire while closing range
                if ((blockingWall = visGetBlockingWall(this, pimpl->actionTargets[0])) &&
                    proj_Direct(psWeapStats)) {
                  auto weapEffect = psWeapStats->weaponEffect;

                  if (!aiCheckAlliances(playerManager->getPlayer(), blockingWall->playerManager->getPlayer())
                      && asStructStrengthModifier[weapEffect][blockingWall->getStats()->strength] >=
                         MIN_STRUCTURE_BLOCK_STRENGTH) {
                    //Shoot at wall if the weapon is good enough against them
                    combFire(&weaponManager->weapons[i], this, blockingWall, i);
                  }
                }
                else {
                  combFire(&weaponManager->weapons[i], this, pimpl->actionTargets[0], i);
                }
              }
            }
          }
        }
        else {
          for (auto i = 0; i < numWeapons(*this); ++i)
          {
            if (weaponManager->weapons[i].getRotation().direction != 0 ||
                weaponManager->weapons[i].getRotation().pitch != 0) {
              weaponManager->weapons[i].alignTurret();
            }
          }
        }

        if (isStationary() && pimpl->action != ATTACK) {
          /* Stopped moving but haven't reached the target - possibly move again */

          // 'hack' to make the droid to check the primary turret instead of all
          psWeapStats = weaponManager->weapons[0].stats.get();

          if (pimpl->order->type == ORDER_TYPE::ATTACK_TARGET && secHoldActive) {
            pimpl->action = NONE; // on hold, give up.
          }
          else if (targetInsideFiringDistance(this, pimpl->actionTargets[0], psWeapStats)) {
            if (proj_Direct(psWeapStats) && pimpl->order->type != ORDER_TYPE::HOLD) {
              int pbx, pby;

              // try and extend the range
              getFallbackPosition(this, pimpl->actionTargets[0], &pbx, &pby);
              moveDroidTo(this, {pbx, pby});
            }
            else {
              if (psWeapStats->rotate) {
                pimpl->action = ATTACK;
              }
              else {
                pimpl->action = ROTATE_TO_ATTACK;
                moveTurnDroid(pimpl->actionTargets[0]->getPosition().x,
                              pimpl->actionTargets[0]->getPosition().y);
              }
            }
          }
          else if (pimpl->order->type != ORDER_TYPE::HOLD) {
            // try to close the range
            moveDroidTo(this, {pimpl->actionTargets[0]->getPosition().x,
                               pimpl->actionTargets[0]->getPosition().y});
          }
        }
      }
      break;

    case SULK:
      // unable to route to target ... don't do anything aggressive until time is up
      // we need to do something defensive at this point ???
      if (gameTime > pimpl->timeActionStarted) {
        pimpl->action = NONE;
        // Sulking is over lets get back to the action ... is this all I need to do to get it back into the action?
      }
      break;

    case MOVE_TO_BUILD:
      if (!pimpl->order->structure_stats) {
        pimpl->action = NONE;
        break;
      }
      else {
        // Determine if the droid can still build or help to build the ordered structure at the specified location
        auto desiredStructure = pimpl->order->structure_stats;
        auto structureAtBuildPosition = getTileStructure(
                map_coord(pimpl->actionPos.x), map_coord(pimpl->actionPos.y));

        if (nullptr != structureAtBuildPosition) {
          bool droidCannotBuild = false;

          if (!aiCheckAlliances(structureAtBuildPosition->playerManager->getPlayer(), playerManager->getPlayer())) {
            // Not our structure
            droidCannotBuild = true;
          }
          else if (isWall(structureAtBuildPosition->getStats()->type) &&
          // there's an allied structure already there.  Is it a wall, and can the droid upgrade it to a defence or gate?
              (desiredStructure->type == STRUCTURE_TYPE::DEFENSE ||
               desiredStructure->type == STRUCTURE_TYPE::GATE)) {
            // It's always valid to upgrade a wall to a defence or gate
            droidCannotBuild = false; // Just to avoid an empty branch
          }
          else if ((structureAtBuildPosition->getStats() != desiredStructure.get()) &&
                   // ... it's not the exact same type as the droid was ordered to build
                   (structureAtBuildPosition->getStats()->type == STRUCTURE_TYPE::WALL_CORNER &&
                    // and not a wall corner when the droid wants to build a wall
                    desiredStructure->type != STRUCTURE_TYPE::WALL)) {
            // and so the droid can't build or help with building this structure
            droidCannotBuild = true;
          }
          else if (structureAtBuildPosition->getState() == STRUCTURE_STATE::BUILT &&
          // so it's a structure that the droid could help to build, but is it already complete?
              (!IsStatExpansionModule(desiredStructure.get()) ||
               !canStructureHaveAModuleAdded(structureAtBuildPosition))) {
            // the building is complete and the droid hasn't been told to add a module, or
            // can't add one, so can't help with that.
            droidCannotBuild = true;
          }
          if (droidCannotBuild) {
            if (pimpl->order->type == ORDER_TYPE::LINE_BUILD &&
                map_coord(pimpl->order->pos) != map_coord(pimpl->order->pos2)) {
              // the droid is doing a line build, and there's more to build. This will force
              // the droid to move to the next structure in the line build
              objTrace(getId(), "ACTION::MOVETOBUILD: line target is already built, "
                                "or can't be built - moving to next structure in line");
              pimpl->action = NONE;
            }
            else {
              // Cancel the current build order. This will move the truck onto the next order, if it has one, or halt in place.
              objTrace(getId(), "ACTION::MOVETOBUILD: target is already built,"
                                " or can't be built - executing next order or halting");
              cancelBuild();
            }
            break;
          }
        }
      } // End of check for whether the droid can still succesfully build the ordered structure

      // The droid can still build or help with a build, and is moving to a location to do so - are we there yet, are we there yet, are we there yet?
      if (adjacentToBuildSite(this, pimpl->actionPos,
                              pimpl->order->direction,
                              pimpl->order->structure_stats.get())) {
        // We're there, go ahead and build or help to build the structure
        bool buildPosEmpty = pushDroidsAwayFromBuildSite(
                playerManager->getPlayer(), pimpl->actionPos, pimpl->order->direction,
                pimpl->order->structure_stats.get());
        if (!buildPosEmpty) {
          break;
        }
        bool helpBuild = false;
        // Got to destination - start building
        auto psStructStats = pimpl->order->structure_stats;
        uint16_t dir = pimpl->order->direction;
        moveStopDroid();
        objTrace(getId(), "Halted in our tracks - at construction site");
        if (pimpl->order->type == ORDER_TYPE::BUILD && pimpl->order->target == nullptr) {
          // Starting a new structure
          const Vector2i pos = pimpl->actionPos;

          //need to check if something has already started building here?
          //unless its a module!
          if (IsStatExpansionModule(psStructStats.get())) {
            syncDebug("Reached build target: module");
            debug(LOG_NEVER, "ACTION::MOVETOBUILD: setUpBuildModule");
            setUpBuildModule();
          }
          else if (TileHasStructure(worldTile(pos))) {
            // structure on the build location - see if it is the same type
            auto psStruct = getTileStructure(map_coord(pos.x),
                                             map_coord(pos.y));
            if (psStruct->getStats() == pimpl->order->structure_stats.get() ||
                (pimpl->order->structure_stats->type == STRUCTURE_TYPE::WALL &&
                psStruct->getStats()->type == STRUCTURE_TYPE::WALL_CORNER)) {
              // same type - do a help build
              syncDebug("Reached build target: do-help");
              setTarget(psStruct);
              helpBuild = true;
            }
            else if ((psStruct->getStats()->type == STRUCTURE_TYPE::WALL ||
                      psStruct->getStats()->type == STRUCTURE_TYPE::WALL_CORNER) &&
                     (pimpl->order->structure_stats->type == STRUCTURE_TYPE::DEFENSE ||
                      pimpl->order->structure_stats->type == STRUCTURE_TYPE::GATE)) {
              // building a gun tower or gate over a wall - OK
              if (droidStartBuild()) {
                syncDebug("Reached build target: tower");
                pimpl->action = BUILD;
              }
            }
            else {
              syncDebug("Reached build target: already-structure");
              objTrace(getId(), "ACTION::MOVETOBUILD: tile has structure already");
              cancelBuild();
            }
          }
          else if (!validLocation(pimpl->order->structure_stats.get(), pos,
                                  dir, playerManager->getPlayer(), false)) {
            syncDebug("Reached build target: invalid");
            objTrace(getId(), "ACTION::MOVETOBUILD: !validLocation");
            cancelBuild();
          }
          else if (droidStartBuild() == DroidStartBuildSuccess)
            // If DroidStartBuildPending, then there's a burning oil well, and we don't want to change to ACTION::BUILD until it stops burning.
          {
            syncDebug("Reached build target: build");
            pimpl->action = ACTION::BUILD;
            pimpl->timeActionStarted = gameTime;
            pimpl->actionPointsDone = 0;
          }
        }
        else if (pimpl->order->type == ORDER_TYPE::LINE_BUILD ||
                 pimpl->order->type == ORDER_TYPE::BUILD) {
          // building a wall.
          auto psTile = worldTile(pimpl->actionPos);
          syncDebug("Reached build target: wall");
          if (pimpl->order->target == nullptr
              && (TileHasStructure(psTile)
                  || TileHasFeature(psTile))) {
            if (TileHasStructure(psTile)) {
              // structure on the build location - see if it is the same type
              auto psStruct = getTileStructure(map_coord(pimpl->actionPos.x),
                                                           map_coord(pimpl->actionPos.y));
              ASSERT(psStruct, "TileHasStructure, but getTileStructure returned nullptr");

      #if defined(WZ_CC_GNU) && !defined(WZ_CC_INTEL) && !defined(WZ_CC_CLANG) && (7 <= __GNUC__)
                      # pragma GCC diagnostic push
      # pragma GCC diagnostic ignored "-Wnull-dereference"
      #endif

              if (psStruct->getStats() == pimpl->order->structure_stats.get()) {
                // same type - do a help build
                setTarget(psStruct);
                helpBuild = true;
              }
              else if ((psStruct->getStats()->type == STRUCTURE_TYPE::WALL ||
                        psStruct->getStats()->type == STRUCTURE_TYPE::WALL_CORNER) &&
                       (pimpl->order->structure_stats->type == STRUCTURE_TYPE::DEFENSE ||
                        pimpl->order->structure_stats->type == STRUCTURE_TYPE::GATE)) {
                // building a gun tower over a wall - OK
                if (droidStartBuild()) {
                  objTrace(getId(), "ACTION::MOVETOBUILD: start building defense");
                  pimpl->action = BUILD;
                }
              }
              else if ((psStruct->getStats()->type == STRUCTURE_TYPE::FACTORY &&
                        pimpl->order->structure_stats->type == STRUCTURE_TYPE::FACTORY_MODULE) ||
                       (psStruct->getStats()->type == STRUCTURE_TYPE::RESEARCH &&
                        pimpl->order->structure_stats->type == STRUCTURE_TYPE::RESEARCH_MODULE) ||
                       (psStruct->getStats()->type == STRUCTURE_TYPE::POWER_GEN &&
                        pimpl->order->structure_stats->type == STRUCTURE_TYPE::POWER_MODULE) ||
                       (psStruct->getStats()->type == STRUCTURE_TYPE::VTOL_FACTORY &&
                        pimpl->order->structure_stats->type == STRUCTURE_TYPE::FACTORY_MODULE)) {
                // upgrade current structure in a row
                if (droidStartBuild()) {
                  objTrace(getId(), "ACTION::MOVETOBUILD: start building module");
                  pimpl->action = BUILD;
                }
              }
              else {
                objTrace(getId(), "ACTION::MOVETOBUILD: line build hit building");
                cancelBuild();
              }
      #if defined(WZ_CC_GNU) && !defined(WZ_CC_INTEL) && !defined(WZ_CC_CLANG) && (7 <= __GNUC__)
      # pragma GCC diagnostic pop
      #endif
            }
            else if (TileHasFeature(psTile)) {
              auto feature = getTileFeature(
                      map_coord(pimpl->actionPos.x),
                      map_coord(pimpl->actionPos.y));
              objTrace(getId(), "ACTION::MOVETOBUILD: tile has feature %d",
                       feature->getStats()->subType);

              if (feature->getStats()->subType == FEATURE_TYPE::OIL_RESOURCE &&
                  pimpl->order->structure_stats->type == STRUCTURE_TYPE::RESOURCE_EXTRACTOR) {
                if (droidStartBuild()) {
                  objTrace(getId(), "ACTION::MOVETOBUILD: start building oil derrick");
                  pimpl->action = BUILD;
                }
              }
            }
            else {
              objTrace(getId(), "ACTION::MOVETOBUILD: blocked line build");
              cancelBuild();
            }
          }
          else if (droidStartBuild()) {
            pimpl->action = BUILD;
          }
        }
        else {
          syncDebug("Reached build target: planned-help");
          objTrace(getId(), "ACTION::MOVETOBUILD: planned-help");
          helpBuild = true;
        }

        if (helpBuild) {
          // continuing a partially built structure (order = helpBuild)
          if (droidStartBuild()) {
            objTrace(getId(), "ACTION::MOVETOBUILD: starting help build");
            pimpl->action = BUILD;
          }
        }
      }
      else if (isStationary()) {
        objTrace(getId(),
                 "ACTION::MOVETOBUILD: Starting to drive toward construction site - move status was %d",
                 (int)pimpl->movement->status);
        moveDroidToNoFormation(this, pimpl->actionPos);
      }
      break;
    case BUILD:
      if (!pimpl->order->structure_stats)
      {
        objTrace(getId(), "No target stats for build order - resetting");
        pimpl->action = NONE;
        break;
      }
      if (isStationary() &&
          !adjacentToBuildSite(this, pimpl->actionPos,
                               pimpl->order->direction,
                               pimpl->order->structure_stats.get())) {
        objTrace(getId(), "ACTION::BUILD: Starting to drive toward construction site");
        moveDroidToNoFormation(this, pimpl->actionPos);
      }
      else if (!isStationary() &&
               pimpl->movement->status != MOVE_STATUS::TURN_TO_TARGET &&
               pimpl->movement->status != MOVE_STATUS::SHUFFLE &&
                 adjacentToBuildSite(this, pimpl->actionPos, pimpl->order->direction,
                                     pimpl->order->structure_stats.get())) {
        objTrace(getId(), "ACTION::BUILD: Stopped - at construction site");
        moveStopDroid();
      }
      if (pimpl->action == SULK) {
        objTrace(getId(), "Failed to go to objective, aborting build action");
        pimpl->action = NONE;
        break;
      }
      if (droidUpdateBuild()) {
        rotateTurret(this, pimpl->actionTargets[0], 0);
      }
      break;
    case MOVE_TO_DEMOLISH:
    case MOVE_TO_REPAIR:
    case MOVE_TO_RESTORE:
      if (!pimpl->order->structure_stats) {
        pimpl->action = NONE;
        break;
      }
      else {
        auto structureAtPos =
                getTileStructure(map_coord(pimpl->actionPos.x),
                                 map_coord(pimpl->actionPos.y));

        if (structureAtPos == nullptr) {
          // no structure located at desired position. Move on
          pimpl->action = NONE;
          break;
        }
        else if (pimpl->order->type != ORDER_TYPE::RESTORE) {
          bool cantDoRepairLikeAction = false;

          if (!aiCheckAlliances(structureAtPos->playerManager->getPlayer(), playerManager->getPlayer())) {
            cantDoRepairLikeAction = true;
          }
          else if (pimpl->order->type != ORDER_TYPE::DEMOLISH &&
                   structureAtPos->damageManager->getHp() == structureBody(structureAtPos)) {
            cantDoRepairLikeAction = true;
          }
          else if (pimpl->order->type == ORDER_TYPE::DEMOLISH &&
                   structureAtPos->playerManager->getPlayer() != playerManager->getPlayer()) {
            cantDoRepairLikeAction = true;
          }

          if (cantDoRepairLikeAction) {
            pimpl->action = NONE;
            moveStopDroid();
            break;
          }
        }
      }
      // see if the droid is at the edge of what it is moving to
      if (adjacentToBuildSite(this, pimpl->actionPos,
                              ((Structure *) pimpl->actionTargets[0])->getRotation().direction,
                              pimpl->order->structure_stats.get())) {
        moveStopDroid();

        // got to the edge - start doing whatever it was meant to do
        droidStartAction(this);
        switch (pimpl->action) {
          case MOVE_TO_DEMOLISH:
            pimpl->action = DEMOLISH;
            break;
          case MOVE_TO_REPAIR:
            pimpl->action = REPAIR;
            break;
          case MOVE_TO_RESTORE:
            pimpl->action = RESTORE;
            break;
          default:
            break;
        }
      }
      else if (isStationary()) {
        moveDroidToNoFormation(this, pimpl->actionPos);
      }
      break;

    case DEMOLISH:
    case REPAIR:
    case RESTORE:
      if (!pimpl->order->structure_stats) {
        pimpl->action = NONE;
        break;
      }
      // setup for the specific action
      switch (pimpl->action) {
        case DEMOLISH:
          actionUpdateFunc = droidUpdateDemolishing;
          break;
        case REPAIR:
          // ACTION::MOVETOREPAIR;
          actionUpdateFunc = droidUpdateRepair;
          break;
        case RESTORE:
          // ACTION::MOVETORESTORE;
          actionUpdateFunc = droidUpdateRestore;
          break;
        default:
          break;
      }

      // now do the action update
      if (isStationary() && !adjacentToBuildSite(this, pimpl->actionPos,
                                                 ((Structure *) pimpl->actionTargets[0])->getRotation().direction,
                                                 pimpl->order->structure_stats.get())) {
        if (pimpl->order->type != ORDER_TYPE::HOLD && (!secHoldActive ||
            (secHoldActive && pimpl->order->type != ORDER_TYPE::NONE))) {
          objTrace(getId(), "Secondary order: Go to construction site");
          moveDroidToNoFormation(this, pimpl->actionPos);
        }
        else {
          pimpl->action = NONE;
        }
      }
      else if (!isStationary() &&
               pimpl->movement->status != MOVE_STATUS::TURN_TO_TARGET &&
               pimpl->movement->status != MOVE_STATUS::SHUFFLE &&
                 adjacentToBuildSite(this, pimpl->actionPos,
                                     dynamic_cast<Structure *>(
                                             pimpl->actionTargets[0])
                                             ->getRotation()
                                             .direction,
                                     pimpl->order->structure_stats.get())) {
        objTrace(getId(), "Stopped - reached build position");
        moveStopDroid();
      }
      else if (actionUpdateFunc(this)) {
        //use 0 for non-combat(only 1 'weapon')
        rotateTurret(this, pimpl->actionTargets[0], 0);
      }
      else {
        pimpl->action = NONE;
      }
      break;

    case MOVE_TO_REARM_POINT:
      if (isStationary()) {
        objTrace(getId(), "Finished moving onto the rearm pad");
        pimpl->action = WAIT_DURING_REARM;
      }
      break;
    case MOVE_TO_REPAIR_POINT:
      if (pimpl->order->rtrType == RTR_DATA_TYPE::REPAIR_FACILITY) {
        /* moving from front to rear of repair facility or rearm pad */
        if (adjacentToBuildSite(this, {pimpl->actionTargets[0]->getPosition().x, pimpl->actionTargets[0]->getPosition().y},
                                dynamic_cast<Structure *>(pimpl->actionTargets[0])->getRotation().direction,
                                dynamic_cast<Structure *>(pimpl->actionTargets[0])->getStats())) {
          objTrace(getId(), "Arrived at repair point - waiting for our turn");
          moveStopDroid();
          pimpl->action = WAIT_DURING_REPAIR;
        }
        else if (isStationary()) {
          moveDroidToNoFormation(this, {pimpl->actionTargets[0]->getPosition().x,
                                        pimpl->actionTargets[0]->getPosition().y});
        }
      }
      else if (pimpl->order->rtrType == RTR_DATA_TYPE::DROID)
      {
        bool reached = adjacentToOtherDroid(this, dynamic_cast<Droid *>(pimpl->order->target));
        if (reached) {
          if (damageManager->getHp() >= damageManager->getOriginalHp()) {
            objTrace(getId(), "Repair not needed of droid %d", (int)getId());
            /* set droid points to max */
            damageManager->setHp(damageManager->getOriginalHp());
            // if completely repaired then reset order
            secondarySetState(SECONDARY_ORDER::RETURN_TO_LOCATION, DSS_NONE);
            orderDroidObj(this, ORDER_TYPE::GUARD, getOrder()->target, ModeImmediate);
          }
          else {
            objTrace(getId(), "Stopping and waiting for repairs %d", (int)getId());
            moveStopDroid();
            pimpl->action = WAIT_DURING_REPAIR;
          }
        }
        else if (isStationary()) {
          //objTrace(psDroid->id, "Droid was stopped, but havent reach the target, moving now");
          //moveDroidToNoFormation(psDroid, psDroid->order.psObj->getPosition().x, psDroid->order.psObj->getPosition().y);
        }
      }
      break;
    case OBSERVE:
      // align the turret
      rotateTurret(this, pimpl->actionTargets[0], 0);
      if (!hasCbSensor()) {
        // make sure the target is within sensor range
        const auto xdiff = getPosition().x - pimpl->actionTargets[0]->getPosition().x;
        const auto ydiff = getPosition().y - pimpl->actionTargets[0]->getPosition().y;
        auto rangeSq = droidSensorRange(this);
        rangeSq = rangeSq * rangeSq;
        if (!visibleObject(this, pimpl->actionTargets[0], false)
            || xdiff * xdiff + ydiff * ydiff >= rangeSq) {
          if (secondaryGetState(SECONDARY_ORDER::HALT_TYPE) != DSS_HALT_GUARD &&
              (pimpl->order->type == ORDER_TYPE::NONE || pimpl->order->type == ORDER_TYPE::HOLD)) {
            pimpl->action = NONE;
          }
          else if ((!secHoldActive && pimpl->order->type != ORDER_TYPE::HOLD) ||
                   (secHoldActive && pimpl->order->type == ORDER_TYPE::OBSERVE)) {
            pimpl->action = MOVE_TO_OBSERVE;
            moveDroidTo(this, {pimpl->actionTargets[0]->getPosition().x,
                               pimpl->actionTargets[0]->getPosition().y});
          }
        }
      }
      break;
    case MOVE_TO_OBSERVE:
      // align the turret
      rotateTurret(this, pimpl->actionTargets[0], 0);

      if (visibleObject(this, pimpl->actionTargets[0], false)) {
        // make sure the target is within sensor range
        const auto xdiff = getPosition().x - pimpl->actionTargets[0]->getPosition().x;
        const auto ydiff = getPosition().y - pimpl->actionTargets[0]->getPosition().y;
        auto rangeSq = droidSensorRange(this);
        rangeSq = rangeSq * rangeSq;
        if ((xdiff * xdiff + ydiff * ydiff < rangeSq) &&
            !isStationary()) {
          pimpl->action = OBSERVE;
          moveStopDroid();
        }
      }
      if (isStationary() && pimpl->action == MOVE_TO_OBSERVE) {
        moveDroidTo(this, {pimpl->actionTargets[0]->getPosition().x,
                           pimpl->actionTargets[0]->getPosition().y});
      }
      break;
    case FIRE_SUPPORT:
      if (!pimpl->order->target) {
        pimpl->action = NONE;
        return;
      }
      //can be either a droid or a structure now - AB 7/10/98
      ASSERT_OR_RETURN(, (dynamic_cast<Droid*>(pimpl->order->target) ||
                          dynamic_cast<Structure*>(pimpl->order->target)) &&
                         aiCheckAlliances(pimpl->order->target->playerManager->getPlayer(), playerManager->getPlayer()),
                        "ACTION::FIRESUPPORT: incorrect target type");

      // don't move VTOL's
      // also don't move closer to sensor towers
      if (!isVtol() && !dynamic_cast<Structure*>(pimpl->order->target)) {
        auto diff = (getPosition() - pimpl->order->target->getPosition()).xy();
        //Consider .shortRange here
        auto rangeSq = weaponManager->weapons[0].stats->upgraded[playerManager->getPlayer()].maxRange / 2;
        // move close to sensor
        rangeSq = rangeSq * rangeSq;
        if (dot(diff, diff) < rangeSq) {
          if (!isStationary()) {
            moveStopDroid();
          }
        }
        else {
          if (!isStationary()) {
            diff = pimpl->order->target->getPosition().xy() - pimpl->movement->destination;
          }
          if (isStationary() || dot(diff, diff) > rangeSq) {
            if (secHoldActive) {
              // droid on hold, don't allow moves.
              pimpl->action = NONE;
            }
            else {
              // move in range
              moveDroidTo(this, {pimpl->order->target->getPosition().x,
                                 pimpl->order->target->getPosition().y});
            }
          }
        }
      }
      break;
    case MOVE_TO_DROID_REPAIR:
    {
      auto actionTargetObj = pimpl->actionTargets[0];
      ASSERT_OR_RETURN(, actionTargetObj != nullptr &&
                         dynamic_cast<Droid*>(actionTargetObj),
                         "unexpected repair target");
      auto actionTarget_ = dynamic_cast<Droid*>(actionTargetObj);
      if (actionTarget_->damageManager->getHp() == actionTarget_->damageManager->getOriginalHp()) {
        // target is healthy: nothing to do
        pimpl->action = NONE;
        moveStopDroid();
        break;
      }
      auto diff = (getPosition() - actionTarget_->getPosition()).xy();
      // moving to repair a droid
      if (!actionTarget_ || // Target missing.
          (pimpl->order->type != ORDER_TYPE::DROID_REPAIR &&
           dot(diff, diff) > 2 * REPAIR_MAXDIST * REPAIR_MAXDIST)) {
        // target further than 1.4142 * REPAIR_MAXDIST, and we aren't ordered to follow.
        pimpl->action = NONE;
        return;
      }
      if (dot(diff, diff) < REPAIR_RANGE * REPAIR_RANGE) {
        // got to destination - start repair
        // rotate turret to point at droid being repaired
        // use 0 for repair droid
        rotateTurret(this, actionTarget_, 0);
        droidStartAction(this);
        pimpl->action = DROID_REPAIR;
      }
      if (isStationary()) {
        // Couldn't reach destination - try and find a new one
        pimpl->actionPos = actionTarget_->getPosition().xy();
        moveDroidTo(this, pimpl->actionPos);
      }
      break;
    }
    case DROID_REPAIR:
    {
      // If not doing self-repair (psActionTarget[0] is repair target)
      if (pimpl->actionTargets[0] != this) {
        rotateTurret(this, pimpl->actionTargets[0], 0);
      }
        // Just self-repairing.
        // See if there's anything to shoot.
      else if (numWeapons(*this) > 0 && !isVtol() &&
               (pimpl->order->type == ORDER_TYPE::NONE ||
                pimpl->order->type == ORDER_TYPE::HOLD ||
                pimpl->order->type == ORDER_TYPE::RETURN_TO_REPAIR)) {
        for (auto i = 0; i < numWeapons(*this); ++i)
        {
          if (nonNullWeapon[i]) {
            BaseObject* psTemp = nullptr;
            const auto psWeapStats = weaponManager->weapons[i].stats.get();
            if (psWeapStats->rotate &&
                secondaryGetState(SECONDARY_ORDER::ATTACK_LEVEL) == DSS_ALEV_ALWAYS &&
                aiBestNearestTarget(&psTemp, i) >= 0 && psTemp) {
              pimpl->action = ATTACK;
              setActionTarget(psTemp, 0);
              break;
            }
          }
        }
      }
      if (pimpl->action != DROID_REPAIR) {
        break; // action has changed
      }

      //check still next to the damaged droid
      auto xdiff = getPosition().x - pimpl->actionTargets[0]->getPosition().x;
      auto ydiff = getPosition().y - pimpl->actionTargets[0]->getPosition().y;
      if (xdiff * xdiff + ydiff * ydiff > REPAIR_RANGE * REPAIR_RANGE) {
        if (pimpl->order->type == ORDER_TYPE::DROID_REPAIR) {
          // damaged droid has moved off - follow if we're not holding position!
          pimpl->actionPos = pimpl->actionTargets[0]->getPosition().xy();
          pimpl->action = MOVE_TO_DROID_REPAIR;
          moveDroidTo(this, pimpl->actionPos);
        }
        else {
          pimpl->action = NONE;
        }
      }
      else {
        if (!droidUpdateDroidRepair()) {
          pimpl->action = NONE;
          moveStopDroid();
          //if the order is RTR then resubmit order so that the unit will go to repair facility point
          if (orderState(this, ORDER_TYPE::RETURN_TO_REPAIR)) {
            orderDroid(this, ORDER_TYPE::RETURN_TO_REPAIR, ModeImmediate);
          }
        }
        else {
          // don't let the target for a repair shuffle
          auto asdroid = dynamic_cast<Droid*>(pimpl->actionTargets[0]);
          if (asdroid->getMovementData()->status == MOVE_STATUS::SHUFFLE) {
            asdroid->moveStopDroid();
          }
        }
      }
      break;
    }
    case WAIT_FOR_REARM:
      // wait here for the rearm pad to instruct the vtol to move
      if (pimpl->actionTargets[0] == nullptr) {
        // rearm pad destroyed - move to another
        objTrace(getId(), "rearm pad gone - switch to new one");
        moveToRearm();
        break;
      }
      if (isStationary() && vtolHappy(*this)) {
        objTrace(getId(), "do not need to rearm after all");
        // don't actually need to rearm so just sit next to the rearm pad
        pimpl->action = NONE;
      }
      break;
    case CLEAR_REARM_PAD:
      if (isStationary()) {
        pimpl->action = NONE;
        objTrace(getId(), "clearing rearm pad");
        if (!vtolHappy(*this))
          // Droid has cleared the rearm pad without getting rearmed. One way this can happen if a rearming pad was built under the VTOL while it was waiting for a pad.
        {
          moveToRearm(); // Rearm somewhere else instead.
        }
      }
      break;
    case WAIT_DURING_REARM:
      // this gets cleared by the rearm pad
      break;
    case MOVE_TO_REARM:
      if (pimpl->actionTargets[0] == nullptr) {
        // base destroyed - find another
        objTrace(getId(), "rearm gone - find another");
        moveToRearm();
        break;
      }

      if (visibleObject(this, pimpl->actionTargets[0], false)) {
        auto psStruct = findNearestReArmPad(
                this, (Structure*)pimpl->actionTargets[0], true);
        // got close to the rearm pad - now find a clear one
        objTrace(getId(), "Seen rearm pad - searching for available one");

        if (psStruct != nullptr) {
          // found a clear landing pad - go for it
          objTrace(getId(), "Found clear rearm pad");
          setActionTarget(psStruct, 0);
        }
        pimpl->action = WAIT_FOR_REARM;
      }

      if (isStationary() || pimpl->action == WAIT_FOR_REARM) {
        Vector2i pos = pimpl->actionTargets[0]->getPosition().xy();
        if (!actionVTOLLandingPos(this, &pos)) {
          // totally bunged up - give up
          objTrace(getId(), "Couldn't find a clear tile near rearm pad - returning to base");
          orderDroid(this, ORDER_TYPE::RETURN_TO_BASE, ModeImmediate);
          break;
        }
        objTrace(getId(), "moving to rearm pad at %d,%d (%d,%d)", (int)pos.x, (int)pos.y,
                 (int)(pos.x/TILE_UNITS), (int)(pos.y/TILE_UNITS));
        moveDroidToDirect(pos);
      }
      break;
    default:
      ASSERT(!"unknown action", "unknown action");
      break;
  }

  if (pimpl->action == MOVE_FIRE || pimpl->action == ATTACK ||
      pimpl->action == MOVE_TO_ATTACK || pimpl->action == MOVE_TO_DROID_REPAIR ||
      pimpl->action == DROID_REPAIR || pimpl->action == BUILD ||
      pimpl->action == OBSERVE || pimpl->action == MOVE_TO_OBSERVE) {
    return;
  }
  //use 0 for all non-combat droid types
  if (numWeapons(*this) == 0) {
    if (weaponManager->weapons[0].getRotation().direction != 0 ||
        weaponManager->weapons[0].getRotation().pitch != 0) {
      weaponManager->weapons[0].alignTurret();
    }
  }
  else {
    for (auto i1 = 0; i1 < numWeapons(*this); ++i1)
    {
      if (weaponManager->weapons[i1].getRotation().direction != 0 ||
          weaponManager->weapons[i1].getRotation().pitch != 0) {
        weaponManager->weapons[i1].alignTurret();
      }
    }
  }
}

/* Deals with building a module - checking if any droid is currently doing this
 - if so, helping to build the current one */
void Droid::setUpBuildModule()
{
  if (!pimpl) return;
  auto tile = map_coord(pimpl->order->pos);

  // check not another truck started
  auto psStruct = getTileStructure(tile.x, tile.y);
  if (!psStruct) {
    cancelBuild();
    return;
  }
  // if a droid is currently building, or building is in progress of
  // being built/upgraded the droid's order should be HELP_BUILD
  if (checkDroidsBuilding(psStruct)) {
    // set up the help build scenario
    pimpl->order->type = ORDER_TYPE::HELP_BUILD;
    setTarget(psStruct);
    if (droidStartBuild()) {
      pimpl->action = ACTION::BUILD;
      return;
    }
  }
  else if (nextModuleToBuild(psStruct, -1) > 0) {
    // no other droids building so just start it off
    if (droidStartBuild()) {
      pimpl->action = ACTION::BUILD;
      return;
    }
  }
  cancelBuild();
}

 /* Deals damage to a droid
  * \param psDroid droid to deal damage to
  * \param damage amount of damage to deal
  * \param weaponClass the class of the weapon that deals the damage
  * \param weaponSubClass the subclass of the weapon that deals the damage
  * \param angle angle of impact (from the damage dealing projectile in relation to this droid)
  * \return > 0 when the dealt damage destroys the droid, < 0 when the droid survives
  *
  */
int Droid::droidDamage(unsigned damage, WEAPON_CLASS weaponClass,
                       WEAPON_SUBCLASS weaponSubClass, unsigned impactTime,
                       bool isDamagePerSecond, int minDamage)
{
  if (!pimpl) return -1;
  // VTOLs (and transporters in MP) on the ground take triple damage
  if ((isVtol() || (isTransporter(*this) && bMultiPlayer)) &&
      (pimpl->movement->status == MOVE_STATUS::INACTIVE)) {
    damage *= 3;
  }

  auto relativeDamage = objDamage(this, damage, damageManager->getOriginalHp(), weaponClass,
                             weaponSubClass, isDamagePerSecond, minDamage);

  if (relativeDamage > 0) {
    // reset the attack level
    if (secondaryGetState(SECONDARY_ORDER::ATTACK_LEVEL) == DSS_ALEV_ATTACKED) {
      secondarySetState(SECONDARY_ORDER::ATTACK_LEVEL, DSS_ALEV_ALWAYS);
    }
    // Now check for auto return on droid's secondary orders (i.e. return on medium/heavy damage)
    secondaryCheckDamageLevel(this);
    return relativeDamage;
  }
  if (relativeDamage == 0) return relativeDamage;

  // Droid destroyed
  debug(LOG_ATTACK, "droid (%d): DESTROYED", getId());

  // Deal with score increase/decrease and messages to the player
  if (playerManager->getPlayer() == selectedPlayer) {
    // TRANSLATORS:	Refers to the loss of a single unit, known by its name
    CONPRINTF(_("%s Lost!"), objInfo(this));
    scoreUpdateVar(WD_UNITS_LOST);
    audio_QueueTrackMinDelayPos(ID_SOUND_UNIT_DESTROYED, UNIT_LOST_DELAY,
                                getPosition().x, getPosition().y, getPosition().z);
  }
    // only counts as a kill if it's not our ally
  else if (selectedPlayer < MAX_PLAYERS &&
           !aiCheckAlliances(playerManager->getPlayer(), selectedPlayer)) {
    scoreUpdateVar(WD_UNITS_KILLED);
  }

  // Do we have a dying animation?
  if (getDisplayData()->imd_shape->objanimpie[ANIM_EVENT_DYING] &&
      pimpl->animationEvent != ANIM_EVENT_DYING) {
    bool useDeathAnimation = true;
    //Babas should not burst into flames from non-heat weapons
    if (pimpl->type == DROID_TYPE::PERSON) {
      if (weaponClass == WEAPON_CLASS::HEAT) {
        // NOTE: 3 types of screams are available ID_SOUND_BARB_SCREAM - ID_SOUND_BARB_SCREAM3
        audio_PlayObjDynamicTrack(
                this, ID_SOUND_BARB_SCREAM + (rand() % 3),
                nullptr);
      }
      else {
        useDeathAnimation = false;
      }
    }
    if (useDeathAnimation) {
      debug(LOG_DEATH, "%s droid %d (%p) is starting death animation",
            objInfo(this), (int)getId(),
            static_cast<void*>(this));
      pimpl->timeAnimationStarted = gameTime;
      pimpl->animationEvent = ANIM_EVENT_DYING;
    }
  }
  // Otherwise use the default destruction animation
  if (pimpl->animationEvent == ANIM_EVENT_DYING) return relativeDamage;

  debug(LOG_DEATH, "%s droid %d (%p) is toast", objInfo(this), (int)getId(),
        static_cast<void*>(this));
  // This should be sent even if multi messages are turned off, as the group message that was
  // sent won't contain the destroyed droid
  if (bMultiPlayer && !bMultiMessages) {
    bMultiMessages = true;
    destroyDroid(this, impactTime);
    bMultiMessages = false;
  }
  else {
    destroyDroid(this, impactTime);
  }
  return relativeDamage;
}

void Droid::aiUpdateDroid()
{
  if (!pimpl || damageManager->isDead()) return;
  if (pimpl->type != DROID_TYPE::SENSOR && numWeapons(*this) == 0) return;

  bool lookForTarget = false;
  bool updateTarget = false;

  // look for a target if doing nothing
  if (orderState(this, ORDER_TYPE::NONE) ||
      orderState(this, ORDER_TYPE::GUARD) ||
      orderState(this, ORDER_TYPE::HOLD)) {
    lookForTarget = true;
  }
  // but do not choose another target if doing anything while guarding
  // exception for sensors, to allow re-targetting when target is doomed
  if (orderState(this, ORDER_TYPE::GUARD) &&
      pimpl->action != ACTION::NONE && pimpl->type != DROID_TYPE::SENSOR) {
    lookForTarget = false;
  }
  // don't look for a target if sulking
  if (pimpl->action == ACTION::SULK) {
    lookForTarget = false;
  }

  /* Only try to update target if already have some target */
  if (pimpl->action == ACTION::ATTACK ||
      pimpl->action == ACTION::MOVE_FIRE ||
      pimpl->action == ACTION::MOVE_TO_ATTACK ||
      pimpl->action == ACTION::ROTATE_TO_ATTACK) {
    updateTarget = true;
  }
  if ((orderState(this, ORDER_TYPE::OBSERVE) ||
       orderState(this, ORDER_TYPE::ATTACK_TARGET)) &&
      pimpl->order->target &&
      dynamic_cast<Health *>(pimpl->order->target)->isDead()) {
    lookForTarget = true;
    updateTarget = false;
  }

  /* Don't update target if we are sent to attack and reached attack destination (attacking our target) */
  if (orderState(this, ORDER_TYPE::ATTACK) &&
     pimpl->actionTargets[0] == pimpl->order->target) {
    updateTarget = false;
  }

  // don't look for a target if there are any queued orders
  if (!pimpl->asOrderList.empty()) {
    lookForTarget = false;
    updateTarget = false;
  }

  // don't allow units to start attacking if they will switch to guarding the commander
  // except for sensors: they still look for targets themselves, because
  // they have wider view
  if (hasCommander() && pimpl->type != DROID_TYPE::SENSOR) {
    lookForTarget = false;
    updateTarget = false;
  }

  if (bMultiPlayer && isVtol() &&
      isHumanPlayer(playerManager->getPlayer())) {
    lookForTarget = false;
    updateTarget = false;
  }

  // CB and VTOL CB droids can't autotarget.
  if (pimpl->type == DROID_TYPE::SENSOR && !hasStandardSensor()) {
    lookForTarget = false;
    updateTarget = false;
  }

  // do not attack if the attack level is wrong
  if (secondaryGetState(SECONDARY_ORDER::ATTACK_LEVEL) != DSS_ALEV_ALWAYS) {
    lookForTarget = false;
  }

  /* For commanders and non-assigned non-commanders: look for a better target once in a while */
  if (!lookForTarget && updateTarget &&
      numWeapons(*this) > 0 && !hasCommander() &&
      (getId() + gameTime) / TARGET_UPD_SKIP_FRAMES !=
        (getId() + gameTime - deltaGameTime) / TARGET_UPD_SKIP_FRAMES) {
    for (auto i = 0; i < numWeapons(*this); ++i)
    {
      updateAttackTarget(this, i);
    }
  }

  /* Null target - see if there is an enemy to attack */
  if (!lookForTarget || updateTarget) return;
  BaseObject* psTarget;
  if (pimpl->type == DROID_TYPE::SENSOR) {
    if (aiChooseSensorTarget(this, &psTarget)) {
      if (!orderState(this, ORDER_TYPE::HOLD)
          && secondaryGetState(SECONDARY_ORDER::HALT_TYPE) == DSS_HALT_PURSUE) {
        pimpl->order = std::make_unique<Order>(ORDER_TYPE::OBSERVE, *psTarget);
      }
      newAction(this, ACTION::OBSERVE, psTarget);
    }
  }
  else if (aiChooseTarget(this, &psTarget, 0, true, nullptr)) {
    if (!orderState(this, ORDER_TYPE::HOLD) &&
        secondaryGetState(SECONDARY_ORDER::HALT_TYPE) == DSS_HALT_PURSUE) {
      pimpl->order = std::make_unique<Order>(ORDER_TYPE::ATTACK, *psTarget);
    }
    newAction(this, ACTION::ATTACK, psTarget);
  }
}

bool Droid::droidUpdateRestore()
{
  auto psStruct = dynamic_cast<Structure*>(pimpl->order->target);

  ASSERT_OR_RETURN(false, pimpl != nullptr, "Droid object is undefined");
  ASSERT_OR_RETURN(false, pimpl->action == ACTION::RESTORE, "Unit is not restoring");
  ASSERT_OR_RETURN(false, psStruct, "Target is not a structure");
  ASSERT_OR_RETURN(false, numWeapons(*this) > 0, "Droid doesn't have any weapons");

  auto const psStats = weaponManager->weapons[0].stats.get();
  ASSERT_OR_RETURN(false,
                   psStats->weaponSubClass == WEAPON_SUBCLASS::ELECTRONIC,
                   "unit's weapon is not EW");

  auto restorePoints = calcDamage(
          weaponDamage(psStats, playerManager->getPlayer()),
          psStats->weaponEffect, psStruct);

  auto pointsToAdd = restorePoints * (gameTime - pimpl->timeActionStarted) /
                         GAME_TICKS_PER_SEC;

  psStruct->damageManager->setResistance(psStruct->damageManager->getResistance() + (pointsToAdd - pimpl->actionPointsDone));

  //store the amount just added
  pimpl->actionPointsDone = pointsToAdd;

  /* check if structure is restored */
  if (psStruct->damageManager->getResistance() < (int)structureResistance(
          psStruct->getStats(), psStruct->playerManager->getPlayer())) {
    return true;
  }
  else {
    addConsoleMessage(_("Structure Restored"),
                      CONSOLE_TEXT_JUSTIFICATION::DEFAULT,
                      SYSTEM_MESSAGE);
    psStruct->damageManager->setResistance(
            structureResistance(psStruct->getStats(),
                                psStruct->playerManager->getPlayer()));
    return false;
  }
}

bool Droid::droidUpdateDroidRepair()
{
  auto repair = dynamic_cast<RepairStats const*>(getComponent(COMPONENT_TYPE::REPAIR_UNIT));
  ASSERT_OR_RETURN(false, pimpl != nullptr, "Droid object is undefined");
  ASSERT_OR_RETURN(false, pimpl->action == ACTION::DROID_REPAIR, "Unit does not have unit repair order");
  ASSERT_OR_RETURN(false, repair, "Unit does not have a repair turret");

  auto psDroidToRepair = dynamic_cast<Droid*>(pimpl->actionTargets[0]);
  ASSERT_OR_RETURN(false, psDroidToRepair, "Target is not a unit");
  auto needMoreRepair = droidUpdateDroidRepairBase(this, psDroidToRepair);
  if (needMoreRepair &&
      psDroidToRepair->getOrder()->type == ORDER_TYPE::RETURN_TO_REPAIR &&
      psDroidToRepair->getOrder()->rtrType == RTR_DATA_TYPE::DROID &&
      psDroidToRepair->getAction() == ACTION::NONE) {
    psDroidToRepair->pimpl->action = ACTION::WAIT_DURING_REPAIR;
  }
  if (!needMoreRepair &&
      psDroidToRepair->getOrder()->type == ORDER_TYPE::RETURN_TO_REPAIR &&
      psDroidToRepair->getOrder()->rtrType == RTR_DATA_TYPE::DROID) {
    // if psDroidToRepair has a commander, commander will call him back anyway
    // if no commanders, just DORDER_GUARD the repair turret
    orderDroidObj(psDroidToRepair, ORDER_TYPE::GUARD,
                  this, ModeImmediate);
    psDroidToRepair->secondarySetState(
            SECONDARY_ORDER::RETURN_TO_LOCATION, DSS_NONE);
    psDroidToRepair->pimpl->order->target = nullptr;
  }
  return needMoreRepair;
}

/* Update a construction droid while it is building
   returns true while building continues */
bool Droid::droidUpdateBuild()
{
  ASSERT_OR_RETURN(false, pimpl != nullptr, "Droid object is undefined");
  ASSERT_OR_RETURN(false, pimpl->action == ACTION::BUILD, "%s (order %s) has wrong action for construction: %s",
                   droidGetName(this), getDroidOrderName(pimpl->order->type).c_str(),
                   actionToString(pimpl->action).c_str());

  auto psStruct = dynamic_cast<Structure*>(pimpl->order->target);
  if (psStruct == nullptr) {
    // target missing, stop trying to build it.
    pimpl->action = ACTION::NONE;
    return false;
  }
  auto construct = dynamic_cast<ConstructStats const*>(getComponent(COMPONENT_TYPE::CONSTRUCT));
  ASSERT_OR_RETURN(false, psStruct, "target is not a structure");
  ASSERT_OR_RETURN(false, construct, "Invalid construct pointer for unit");

  // First check the structure hasn't been completed by another droid
  if (psStruct->getState() == STRUCTURE_STATE::BUILT) {
    // Check if line order build is completed, or we are not carrying out a line order build
    if (pimpl->order->type != ORDER_TYPE::LINE_BUILD ||
        map_coord(pimpl->order->pos) == map_coord(pimpl->order->pos2)) {
      cancelBuild();
    }
    else {
     pimpl-> action = ACTION::NONE; // make us continue line build
      setTarget(nullptr);
      setActionTarget(nullptr, 0);
    }
    return false;
  }

  // make sure we still 'own' the building in question
  if (!aiCheckAlliances(psStruct->playerManager->getPlayer(), playerManager->getPlayer())) {
    cancelBuild(); // stop what you are doing fool it isn't ours anymore!
    return false;
  }

  auto constructPoints = constructorPoints(construct, playerManager->getPlayer());

  auto pointsToAdd = constructPoints * (gameTime - pimpl->timeActionStarted) /
                         GAME_TICKS_PER_SEC;

  structureBuild(psStruct, this,
                 pointsToAdd - pimpl->actionPointsDone,
                 constructPoints);

  // store the amount just added
  pimpl->actionPointsDone = pointsToAdd;
  addConstructorEffect(psStruct);
  return true;
}

// recycle a droid (retain its experience and some of its cost)
void Droid::recycleDroid()
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Droid object is undefined");
  // store the droids kills
  if (pimpl->experience > 0) {
    recycled_experience[playerManager->getPlayer()].push(pimpl->experience);
  }

  // return part of the cost of the droid
  auto cost = calcDroidPower(this);
  cost = (cost / 2) * damageManager->getHp() / damageManager->getOriginalHp();

  addPower(playerManager->getPlayer(), cost);

  // hide the droid
  setHidden();

  if (pimpl->group) {
    pimpl->group->removeDroid(this);
  }

  triggerEvent(TRIGGER_OBJECT_RECYCLED, this);
  vanishDroid(this);

  auto pos = getPosition().xzy();
  const auto mapCoord = map_coord({getPosition().x, getPosition().y});
  const auto psTile = mapTile(mapCoord);

  if (tileIsClearlyVisible(psTile)) {
    addEffect(&pos, EFFECT_GROUP::EXPLOSION,
              EFFECT_TYPE::EXPLOSION_TYPE_DISCOVERY,
              false, nullptr, false,
              gameTime - deltaGameTime + 1);
  }
}

/* The main update routine for all droids */
void Droid::droidUpdate()
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Droid object in undefined state");
  Vector3i dv;
  BaseObject* psBeingTargetted = nullptr;

  if (testFlag(static_cast<std::size_t>(OBJECT_FLAG::DIRTY))) {
    visTilesUpdate(this);
    upgradeHitPoints();
    setFlag(static_cast<std::size_t>(OBJECT_FLAG::DIRTY), false);
  }

  // Save old droid position, update time.
  setPreviousLocation(getSpacetime());
  setTime(gameTime);
  for (auto i = 0; i < MAX(1, numWeapons(*this)); ++i)
  {
    weaponManager->weapons[i].previousRotation = weaponManager->weapons[i].getRotation();
  }

  if (pimpl->animationEvent != ANIM_EVENT_NONE) {
    auto imd = getDisplayData()->imd_shape->objanimpie[pimpl->animationEvent];
    if (imd && imd->objanimcycles > 0 &&
        gameTime > pimpl->timeAnimationStarted +
          imd->objanimtime * imd-> objanimcycles) {
      // Done animating (animation is defined by body - other components should follow suit)
      if (pimpl->animationEvent == ANIM_EVENT_DYING) {
        debug(LOG_DEATH, "%s (%d) died to burn anim (died=%d)",
              objInfo(this), (int)getId(), (int)damageManager->isDead());
        destroyDroid(this, gameTime);
        return;
      }
      pimpl->animationEvent = ANIM_EVENT_NONE;
    }
  }
  else if (pimpl->animationEvent == ANIM_EVENT_DYING) {
    return; // rest below is irrelevant if dead
  }

  // ai update droid
  aiUpdateDroid();

  // Update the droids order.
  orderUpdateDroid();

  // update the action of the droid
  actionUpdateDroid();

  // update the move system
  moveUpdateDroid();

  /* Only add smoke if they're visible */
  if (isVisibleToSelectedPlayer() && pimpl->type != DROID_TYPE::PERSON) {
    // need to clip this value to prevent overflow condition
    auto percentDamage = 100 - clip<unsigned>(PERCENT(damageManager->getHp(), damageManager->getOriginalHp()), 0, 100);

    // Is there any damage?
    if (percentDamage >= 25) {
      if (percentDamage >= 100) {
        percentDamage = 99;
      }

      auto emissionInterval = CALC_DROID_SMOKE_INTERVAL(percentDamage);

      auto effectTime = std::max(gameTime - deltaGameTime + 1, pimpl->lastEmissionTime + emissionInterval);
      if (gameTime >= effectTime) {
        dv.x = getPosition().x + DROID_DAMAGE_SPREAD;
        dv.z = getPosition().y + DROID_DAMAGE_SPREAD;
        dv.y = getPosition().z;

        dv.y += getDisplayData()->imd_shape->max.y * 2;
        addEffect(&dv, EFFECT_GROUP::SMOKE, EFFECT_TYPE::SMOKE_TYPE_DRIFTING_SMALL,
                  false, nullptr, 0, effectTime);
        pimpl->lastEmissionTime = effectTime;
      }
    }
  }

  /* Are we a sensor droid or a command droid? Show where we target for selectedPlayer. */
  if (playerManager->getPlayer() == selectedPlayer &&
      (pimpl->type == DROID_TYPE::SENSOR || pimpl->type == DROID_TYPE::COMMAND)) {

    /* If we're attacking or sensing (observing), then... */
    if ((psBeingTargetted = orderStateObj(this, ORDER_TYPE::ATTACK))
        || (psBeingTargetted = orderStateObj(this, ORDER_TYPE::OBSERVE))) {
      psBeingTargetted->setFlag(static_cast<size_t>(OBJECT_FLAG::TARGETED), true);
    }
    else if (secondaryGetState(SECONDARY_ORDER::HALT_TYPE) != DSS_HALT_PURSUE &&
             pimpl->actionTargets[0] != nullptr &&
             validTarget(this, pimpl->actionTargets[0], 0) &&
             (pimpl->action == ACTION::ATTACK || pimpl->action == ACTION::OBSERVE ||
              orderState(this, ORDER_TYPE::HOLD))) {
      psBeingTargetted = pimpl->actionTargets[0];
      psBeingTargetted->setFlag(static_cast<size_t>(OBJECT_FLAG::TARGETED), true);
    }
  }

  // if we are a repair turret, then manage incoming damaged droids, (just like repair facility)
  // unlike a repair facility
  // 	- we don't really need to move droids to us, we can come ourselves
  //	- we don't steal work from other repair turrets/ repair facilities
  if (pimpl->type == DROID_TYPE::REPAIRER || pimpl->type == DROID_TYPE::CYBORG_REPAIR) {
    for (auto& psOther : playerList[playerManager->getPlayer()].droids)
    {
      // unlike repair facility, no droid  can have DORDER_RTR_SPECIFIED with another droid as target, so skip that check
      if (psOther.getOrder()->type == ORDER_TYPE::RETURN_TO_REPAIR &&
          psOther.getOrder()->rtrType == RTR_DATA_TYPE::DROID &&
          psOther.getAction() != ACTION::WAIT_FOR_REPAIR &&
          psOther.getAction() != ACTION::MOVE_TO_REPAIR_POINT &&
          psOther.getAction() != ACTION::WAIT_DURING_REPAIR) {
        if (psOther.damageManager->getHp() >= psOther.damageManager->getOriginalHp()) {
          // set droid points to max
          psOther.damageManager->setHp(psOther.damageManager->getOriginalHp());
          // if completely repaired reset order
          psOther.secondarySetState(SECONDARY_ORDER::RETURN_TO_LOCATION, DSS_NONE);

          if (psOther.hasCommander()) {
            // return a droid to it's command group
            orderDroidObj(&psOther, ORDER_TYPE::GUARD,
                          psOther.pimpl->commander, ModeImmediate);
          }
          continue;
        }
      }

      else if (psOther.getOrder()->rtrType == RTR_DATA_TYPE::DROID
               // is being, or waiting for repairs..
               && (psOther.getAction() == ACTION::WAIT_FOR_REPAIR ||
                   psOther.getAction() == ACTION::WAIT_DURING_REPAIR) &&
                   psOther.getOrder()->target == this) {
        if (!adjacentToOtherDroid(this, &psOther)) {
          newAction(&psOther, ACTION::MOVE, this, {getPosition().x, getPosition().y});
        }
      }
    }
  }

  // See if we can and need to self repair.
  auto repair = dynamic_cast<RepairStats const*>(getComponent(COMPONENT_TYPE::REPAIR_UNIT));
  if (!isVtol() && damageManager->getHp() < damageManager->getOriginalHp() &&
      repair != nullptr && selfRepairEnabled(playerManager->getPlayer())) {
    droidUpdateDroidSelfRepair(this);
  }

  /* Update the fire damage data */
  if (damageManager->getPeriodicalDamageStartTime() != 0 &&
      damageManager->getPeriodicalDamageStartTime() != gameTime - deltaGameTime) {
    // (-deltaGameTime, since projectiles are updated after droids)
    // the periodicalDamageStart has been set, but is not from the previous tick, so we must be out of the fire
    damageManager->setPeriodicalDamage(0); // reset periodical damage done this tick
    if (damageManager->getPeriodicalDamageStartTime() + BURN_TIME < gameTime) {
      // finished periodical damaging
      damageManager->setPeriodicalDamageStartTime(0);
    }
    else {
      // do hardcoded burn damage (this damage automatically applied after periodical damage finished)
      droidDamage(BURN_DAMAGE, WEAPON_CLASS::HEAT,
                  WEAPON_SUBCLASS::FLAME, gameTime - deltaGameTime / 2 + 1,
                  true, BURN_MIN_DAMAGE);
    }
  }

  // At this point, the droid may be dead due to periodical damage or hardcoded burn damage.
  if (damageManager->isDead()) {
    return;
  }

  calcDroidIllumination(this);

  // Check the resistance level of the droid
  if ((getId() + gameTime) / 833 != (getId() + gameTime - deltaGameTime) / 833) {
    // Zero resistance means not currently been attacked - ignore these
    if (damageManager->getResistance() < droidResistance(this)) {
      // Increase over time if low
      damageManager->setResistance(damageManager->getResistance() + 1);
    }
  }
}

/* Set up a droid to build a structure - returns true if successful */
DroidStartBuild Droid::droidStartBuild()
{
  if (!pimpl) return {};
  Structure* psStruct = nullptr;

  /* See if we are starting a new structure */
  if (pimpl->order->target == nullptr &&
      (pimpl->order->type == ORDER_TYPE::BUILD ||
       pimpl->order->type == ORDER_TYPE::LINE_BUILD)) {
    auto psStructStat = pimpl->order->structure_stats;

    auto ia = (ItemAvailability)apStructTypeLists[playerManager->getPlayer()][psStructStat - asStructureStats];
    if (ia != AVAILABLE && ia != REDUNDANT) {
      ASSERT(false, "Cannot build \"%s\" for player %d.",
             psStructStat->name.toUtf8().c_str(), playerManager->getPlayer());

      cancelBuild();
      objTrace(getId(), "DroidStartBuildFailed: not researched");
      return DroidStartBuildFailed;
    }

    //need to check structLimits have not been exceeded
    if (psStructStat->curCount[playerManager->getPlayer()] >= psStructStat->upgraded_stats[playerManager->getPlayer()].limit) {
      cancelBuild();
      objTrace(getId(), "DroidStartBuildFailed: structure limits");
      return DroidStartBuildFailed;
    }
    // Can't build on burning oil derricks.
    if (psStructStat->type == STRUCTURE_TYPE::RESOURCE_EXTRACTOR &&
        fireOnLocation(pimpl->order->pos.x, pimpl->order->pos.y)) {
      // Don't cancel build, since we can wait for it to stop burning.
      objTrace(getId(), "DroidStartBuildPending: burning");
      return DroidStartBuildPending;
    }
    //ok to build
    psStruct = buildStructureDir(psStructStat.get(),
                                 pimpl->order->pos.x, pimpl->order->pos.y,
                                 pimpl->order->direction,
                                 playerManager->getPlayer(), false);
    if (!psStruct) {
      cancelBuild();
      objTrace(getId(), "DroidStartBuildFailed: buildStructureDir failed");
      return DroidStartBuildFailed;
    }
    // Structures start at 10% health. Round up.
    psStruct->damageManager->setHp(psStruct->damageManager->getHp() + 9 / 10);
  }
  else {
    // Check the structure is still there to build (joining
    // a partially built struct)
    psStruct = dynamic_cast<Structure*>(pimpl->order->target);
    if (!psStruct) {
      psStruct = dynamic_cast<Structure*>(
              worldTile(pimpl->actionPos)->psObject);
    }
    if (psStruct && !droidNextToStruct(this, psStruct)) {
      /* Nope - stop building */
      debug(LOG_NEVER, "not next to structure");
      objTrace(getId(), "DroidStartBuildSuccess: not next to structure");
    }
  }

  //check structure not already built, and we still 'own' it
  if (psStruct) {
    if (psStruct->getState() != STRUCTURE_STATE::BUILT &&
        aiCheckAlliances(psStruct->playerManager->getPlayer(), playerManager->getPlayer())) {
      pimpl->timeActionStarted = gameTime;
      pimpl->actionPointsDone = 0;
      setTarget(psStruct);
      setActionTarget(psStruct, 0);
      objTrace(getId(), "DroidStartBuild: set target");
    }

    if (psStruct->isVisibleToSelectedPlayer()) {
      audio_PlayObjStaticTrackCallback(this, ID_SOUND_CONSTRUCTION_START,
                                       droidBuildStartAudioCallback);
    }
  }
  objTrace(getId(), "DroidStartBuildSuccess");
  return DroidStartBuildSuccess;
}

/**
* Set a target location in world coordinates for a droid to move to.
*
* @return true if the routing was successful, if false then the calling code
*         should not try to route here again for a while
*/
bool Droid::moveDroidToBase(Vector2i location, bool bFormation, FPATH_MOVETYPE moveType)
{
  if (!pimpl) return false;
  using enum MOVE_STATUS;
  using enum FPATH_RESULT;
  FPATH_RESULT retVal = OK;

  // in multiPlayer make Transporter move like the vtols
  if (isTransporter(*this) && game.maxPlayers == 0) {
    fpathSetDirectRoute(x, y);
    pimpl->movement->status = NAVIGATE;
    pimpl->movement->pathIndex = 0;
    return true;
  }
    // NOTE: While Vtols can fly, then can't go through things, like the transporter.
  else if ((game.maxPlayers > 0 && isTransporter(*this))) {
    fpathSetDirectRoute(x, y);
    retVal = OK;
  }
  else {
    retVal = fpathDroidRoute(this, x, y, moveType);
  }

  if (retVal == OK) {
    // bit of a hack this - john
    // if astar doesn't have a complete route, it returns a route to the nearest clear tile.
    // the location of the clear tile is in DestinationX,DestinationY.
    // reset x,y to this position so the formation gets set up correctly
    x = pimpl->movement->destination.x;
    y = pimpl->movement->destination.y;

    objTrace(getId(), "unit %d: path ok - base Speed %u, speed %d, target(%u|%d, %u|%d)",
             (int)getId(), pimpl->baseSpeed, pimpl->movement->speed, x,
             map_coord(x), y, map_coord(y));

    pimpl->movement->status = NAVIGATE;
    pimpl->movement->pathIndex = 0;
  }
  else if (retVal == WAIT) {
    // the route will be calculated by the path-finding thread
    pimpl->movement->status = WAIT_FOR_ROUTE;
    pimpl->movement->destination.x = x;
    pimpl->movement->destination.y = y;
  }
  else // if (retVal == FPR_FAILED)
  {
    objTrace(getId(), "Path to (%d, %d) failed for droid %d", (int)x, (int)y, (int)getId());
    pimpl->movement->status = INACTIVE;
    newAction(this, ACTION::SULK);
    return false;
  }
  return true;
}

/**
 * Move a droid directly to a location.
 * @note This is (or should be) used for VTOLs only.
 */
void Droid::moveDroidToDirect(Vector2i location)
{
  if (!pimpl) return;
  ASSERT_OR_RETURN(, isVtol(), "Only valid for a VTOL unit");

  fpathSetDirectRoute(x, y);
  pimpl->movement->status = MOVE_STATUS::NAVIGATE;
  pimpl->movement->pathIndex = 0;
}

/**
 * Turn a droid towards a given location.
 */
void Droid::moveTurnDroid(unsigned x, unsigned y)
{
  if (!pimpl) return;
  auto moveDir = calcDirection(getPosition().x,
                               getPosition().y, x, y);

  if (getRotation().direction != moveDir) {
    pimpl->movement->target.x = x;
    pimpl->movement->target.y = y;
    pimpl->movement->status = MOVE_STATUS::TURN_TO_TARGET;
  }
}

// Tell a droid to move out the way for a shuffle
void Droid::moveShuffleDroid(Vector2i s)
{
  if (!pimpl) return;
  bool frontClear = true, leftClear = true, rightClear = true;
  auto shuffleDir = iAtan2(s);
  auto shuffleMag = iHypot(s);

  if (shuffleMag == 0) {
    return;
  }

  auto shuffleMove = SHUFFLE_MOVE;

  // calculate the possible movement vectors
  auto svx = s.x * shuffleMove / shuffleMag; // Straight in the direction of s.
  auto svy = s.y * shuffleMove / shuffleMag;

  auto lvx = -svy; // 90° to the... right?
  auto lvy = svx;

  auto rvx = svy; // 90° to the... left?
  auto rvy = -svx;

  // check for blocking tiles
  if (auto propulsion = dynamic_cast<PropulsionStats const*>(getComponent(COMPONENT_TYPE::PROPULSION))) {
    if (fpathBlockingTile(map_coord((int) getPosition().x + lvx),
                          map_coord((int) getPosition().y + lvy),
                          propulsion->propulsionType)) {
      leftClear = false;
    }
    else if (fpathBlockingTile(map_coord((int) getPosition().x + rvx),
                                 map_coord((int) getPosition().y + rvy),
                                 propulsion->propulsionType)) {
      rightClear = false;
    }
    else if (fpathBlockingTile(map_coord((int) getPosition().x + svx),
                                 map_coord((int) getPosition().y + svy),
                                 propulsion->propulsionType)) {
      frontClear = false;
    }
  }

  // find any droids that could block the shuffle
  static GridList gridList; // static to avoid allocations.
  gridList = gridStartIterate(getPosition().x, getPosition().y, SHUFFLE_DIST);
  for (auto & gi : gridList)
  {
    auto psCurr = dynamic_cast<Droid*>(gi);
    if (psCurr == nullptr || psCurr->damageManager->isDead() || psCurr == this) {
      continue;
    }

    auto droidDir = iAtan2((psCurr->getPosition() - getPosition()).xy());
    auto diff = angleDelta(shuffleDir - droidDir);
    if (diff > -DEG(135) && diff < -DEG(45)) {
      leftClear = false;
    }
    else if (diff > DEG(45) && diff < DEG(135)) {
      rightClear = false;
    }
  }

  // calculate a target
  int mx, my;
  if (leftClear) {
    mx = lvx;
    my = lvy;
  }
  else if (rightClear) {
    mx = rvx;
    my = rvy;
  }
  else if (frontClear) {
    mx = svx;
    my = svy;
  }
  else {
    // nowhere to shuffle to, quit
    return;
  }

  // check the location for vtols
  auto tar = getPosition().xy() + Vector2i(mx, my);
  if (isVtol()) {
    actionVTOLLandingPos(this, &tar);
  }

  // set up the move state
  if (pimpl->movement->status != MOVE_STATUS::SHUFFLE) {
    pimpl->movement->shuffleStart = gameTime;
  }
  pimpl->movement->status = MOVE_STATUS::SHUFFLE;
  pimpl->movement->src = getPosition().xy();
  pimpl->movement->target = tar;
  pimpl->movement->path.clear();
  pimpl->movement->pathIndex = 0;
}

/*compares the droid sensor type with the droid weapon type to see if the
  FIRE_SUPPORT order can be assigned*/
bool Droid::droidSensorDroidWeapon(const BaseObject* psObj) const
{
  if (!psObj || !pimpl) return false;
  const SensorStats* psStats = nullptr;

  //first check if the object is a droid or a structure
  if (!dynamic_cast<const Droid*>(psObj) &&
      !dynamic_cast<const Structure*>(psObj)) {
    return false;
  }
  //check same player
  if (psObj->playerManager->getPlayer() != playerManager->getPlayer()) {
    return false;
  }
  //check obj is a sensor droid/structure
  if (auto psDroid = dynamic_cast<const Droid*>(psObj)) {
    if (psDroid->getType() != DROID_TYPE::SENSOR &&
        psDroid->getType() != DROID_TYPE::COMMAND) {
      return false;
    }
    psStats = dynamic_cast<SensorStats const*>(getComponent(COMPONENT_TYPE::SENSOR));
  }
  else {
    auto psStruct = dynamic_cast<const Structure*>(psObj);
    psStats = psStruct->getStats()->sensor_stats.get();
    if ((psStats == nullptr) ||
        (psStats->location != LOC::TURRET)) {
      return false;
    }
  }

  //check droid is a weapon droid - or Cyborg!!
  if (!(getType() == DROID_TYPE::WEAPON ||
        getType() == DROID_TYPE::CYBORG ||
        getType() == DROID_TYPE::CYBORG_SUPER)) {
    return false;
  }

  //finally check the right droid/sensor combination
  // check vtol droid with commander
  if ((isVtol() || !proj_Direct(weaponManager->weapons[0].stats.get())) &&
      dynamic_cast<const Droid*>(psObj) &&
      dynamic_cast<const Droid*>(psObj)->getType() == DROID_TYPE::COMMAND) {
    return true;
  }

  //check vtol droid with vtol sensor
  using enum SENSOR_TYPE;
  if (isVtol()) {
    if (psStats->type == VTOL_INTERCEPT ||
        psStats->type == VTOL_CB ||
        psStats->type == SUPER) {
      return true;
    }
    return false;
  }

  // Check indirect weapon droid with standard/CB/radar detector sensor
  if (!proj_Direct(weaponManager->weapons[0].stats.get())) {
    if (psStats->type == STANDARD ||
        psStats->type == INDIRECT_CB ||
        psStats->type == SUPER ) {
      return true;
    }
    return false;
  }
  return false;
}

/**
 * This function assigns a state to a droid. It returns true if
 * it assigned and false if it failed to assign.
 */
bool Droid::secondarySetState(SECONDARY_ORDER sec, SECONDARY_STATE state, QUEUE_MODE mode)
{
  if (!pimpl) return false;
  using enum SECONDARY_ORDER;

  auto currState = pimpl->secondaryOrder;
  if (bMultiMessages && mode == ModeQueue) {
    currState = pimpl->secondaryOrderPending;
  }

  // figure out what the new secondary state will be (once the order is synchronised).
  // why does this have to be so ridiculously complicated?
  auto secondaryMask = 0;
  auto secondarySet = 0;
  switch (sec) {
    case ATTACK_RANGE:
      secondaryMask = DSS_ARANGE_MASK;
      secondarySet = state;
      break;
    case REPAIR_LEVEL:
      secondaryMask = DSS_REPLEV_MASK;
      secondarySet = state;
      break;
    case ATTACK_LEVEL:
      secondaryMask = DSS_ALEV_MASK;
      secondarySet = state;
      break;
    case ASSIGN_PRODUCTION:
      if (pimpl->type == DROID_TYPE::COMMAND) {
        secondaryMask = DSS_ASSPROD_FACT_MASK;
        secondarySet = state & DSS_ASSPROD_MASK;
      }
      break;
    case ASSIGN_CYBORG_PRODUCTION:
      if (pimpl->type == DROID_TYPE::COMMAND) {
        secondaryMask = DSS_ASSPROD_CYB_MASK;
        secondarySet = state & DSS_ASSPROD_MASK;
      }
      break;
    case ASSIGN_VTOL_PRODUCTION:
      if (pimpl->type == DROID_TYPE::COMMAND) {
        secondaryMask = DSS_ASSPROD_VTOL_MASK;
        secondarySet = state & DSS_ASSPROD_MASK;
      }
      break;
    case CLEAR_PRODUCTION:
      if (pimpl->type == DROID_TYPE::COMMAND) {
        secondaryMask = state & DSS_ASSPROD_MASK;
      }
      break;
    case RECYCLE:
      if (state & DSS_RECYCLE_MASK) {
        secondaryMask = DSS_RTL_MASK | DSS_RECYCLE_MASK | DSS_HALT_MASK;
        secondarySet = DSS_RECYCLE_SET | DSS_HALT_GUARD;
      }
      else {
        secondaryMask = DSS_RECYCLE_MASK;
      }
      break;
    case SECONDARY_ORDER::CIRCLE:
      secondaryMask = DSS_CIRCLE_MASK;
      secondarySet = (state & DSS_CIRCLE_SET) ? DSS_CIRCLE_SET : 0;
      break;
    case PATROL:
      secondaryMask = DSS_PATROL_MASK;
      secondarySet = (state & DSS_PATROL_SET) ? DSS_PATROL_SET : 0;
      break;
    case HALT_TYPE:
      switch (state & DSS_HALT_MASK) {
        case DSS_HALT_PURSUE:
        case DSS_HALT_GUARD:
        case DSS_HALT_HOLD:
          secondaryMask = DSS_HALT_MASK;
          secondarySet = state;
          break;
      }
      break;
    case RETURN_TO_LOCATION:
      secondaryMask = DSS_RTL_MASK;
      switch (state & DSS_RTL_MASK) {
        case DSS_RTL_REPAIR:
        case DSS_RTL_BASE:
          secondarySet = state;
          break;
        case DSS_RTL_TRANSPORT:
          auto psTransport = FindATransporter(this);
          if (psTransport != nullptr) {
            secondarySet = state;
          }
          break;
      }
      if ((currState & DSS_HALT_MASK) == DSS_HALT_HOLD) {
        secondaryMask |= DSS_HALT_MASK;
        secondarySet |= DSS_HALT_GUARD;
      }
      break;
    case FIRE_DESIGNATOR:
      // do nothing.
      break;
  }
  auto newSecondaryState = (currState & ~secondaryMask) | secondarySet;

  if (bMultiMessages && mode == ModeQueue) {
    if (sec == REPAIR_LEVEL) {
      secondaryCheckDamageLevelDeselect(this, state);
      // deselect droid immediately, if applicable, so it isn't
      // ordered around by mistake.
    }

    sendDroidSecondary(this, sec, state);
    pimpl->secondaryOrderPending = newSecondaryState;
    ++pimpl->secondaryOrderPendingCount;
    // wait for our order before changing the droid
    return true;
  }

  // set the state for any droids in the command group
  if ((sec != RECYCLE) &&
      pimpl->type == DROID_TYPE::COMMAND &&
      pimpl->group &&
      pimpl->group->isCommandGroup()) {
    pimpl->group->setSecondary(sec, state);
  }

  auto retVal = true;
  switch (sec) {
    case ATTACK_RANGE:
      currState = (currState & ~DSS_ARANGE_MASK) | state;
      break;

    case REPAIR_LEVEL:
      currState = (currState & ~DSS_REPLEV_MASK) | state;
      pimpl->secondaryOrder = currState;
      secondaryCheckDamageLevel(this);
      break;

    case ATTACK_LEVEL:
      currState = (currState & ~DSS_ALEV_MASK) | state;
      if (state == DSS_ALEV_NEVER) {
        if (orderState(this, ORDER_TYPE::ATTACK)) {
          // just kill these orders
          orderDroid(this, ORDER_TYPE::STOP, ModeImmediate);
          if (isVtol()) {
            moveToRearm();
          }
        }
        else if (droidAttacking(this)) {
          // send the unit back to the guard position
          newAction(this, ACTION::NONE);
        }
        else if (orderState(this, ORDER_TYPE::PATROL)) {
          // send the unit back to the patrol
          newAction(this, ACTION::RETURN_TO_POS, pimpl->actionPos);
        }
      }
      break;
    case ASSIGN_PRODUCTION:
    case ASSIGN_CYBORG_PRODUCTION:
    case ASSIGN_VTOL_PRODUCTION:
      STRUCTURE_TYPE prodType;
      if (sec == ASSIGN_PRODUCTION) {
        prodType = STRUCTURE_TYPE::FACTORY;
      }
      else if (sec == ASSIGN_CYBORG_PRODUCTION) {
        prodType = STRUCTURE_TYPE::CYBORG_FACTORY;
      }
      else {
        prodType = STRUCTURE_TYPE::VTOL_FACTORY;
      }

      if (pimpl->type == DROID_TYPE::COMMAND) {
        // look for the factories
        for (auto& psStruct : playerList[playerManager->getPlayer()].structures)
        {
          auto factType = psStruct.getStats()->type;
          if (factType == STRUCTURE_TYPE::FACTORY ||
              factType == STRUCTURE_TYPE::VTOL_FACTORY ||
              factType == STRUCTURE_TYPE::CYBORG_FACTORY) {
            auto factoryInc = dynamic_cast<Factory*>(&psStruct)->getAssemblyPoint()->factoryInc;
            if (factType == STRUCTURE_TYPE::FACTORY) {
              factoryInc += DSS_ASSPROD_SHIFT;
            }
            else if (factType == STRUCTURE_TYPE::CYBORG_FACTORY) {
              factoryInc += DSS_ASSPROD_CYBORG_SHIFT;
            }
            else {
              factoryInc += DSS_ASSPROD_VTOL_SHIFT;
            }
            if (!(currState & (1 << factoryInc)) &&
                (state & (1 << factoryInc))) {
              // assign this factory to the command droid
              assignFactoryCommandDroid(&psStruct, this);
            }
            else if ((prodType == factType) &&
                     (currState & (1 << factoryInc)) &&
                     !(state & (1 << factoryInc))) {
              // remove this factory from the command droid
              assignFactoryCommandDroid(&psStruct, nullptr);
            }
          }
        }
        if (prodType == STRUCTURE_TYPE::FACTORY) {
          currState &= ~DSS_ASSPROD_FACT_MASK;
        }
        else if (prodType == STRUCTURE_TYPE::CYBORG_FACTORY)
        {
          currState &= ~DSS_ASSPROD_CYB_MASK;
        }
        else {
          currState &= ~DSS_ASSPROD_VTOL_MASK;
        }
        currState |= (state & DSS_ASSPROD_MASK);
      }
      break;

    case CLEAR_PRODUCTION:
      if (pimpl->type == DROID_TYPE::COMMAND) {
        // simply clear the flag - all the factory stuff is done
        // in assignFactoryCommandDroid
        currState &= ~(state & DSS_ASSPROD_MASK);
      }
      break;

    case RECYCLE:
      if (state & DSS_RECYCLE_MASK) {
        if (!orderState(this, ORDER_TYPE::RECYCLE)) {
          orderDroid(this, ORDER_TYPE::RECYCLE, ModeImmediate);
        }
        currState &= ~(DSS_RTL_MASK | DSS_RECYCLE_MASK | DSS_HALT_MASK);
        currState |= DSS_RECYCLE_SET | DSS_HALT_GUARD;
        if (pimpl->group) {
          if (pimpl->type == DROID_TYPE::COMMAND) {
            // remove all the units from the commanders group
            for (auto psCurr : *pimpl->group->getMembers())
            {
              psCurr->removeDroidFromGroup(psCurr);
              orderDroid(psCurr, ORDER_TYPE::STOP, ModeImmediate);
            }
          }
          else if (pimpl->group->isCommandGroup()) {
            removeDroidFromGroup(this);
          }
        }
      }
      else {
        if (orderState(this, ORDER_TYPE::RECYCLE)) {
          orderDroid(this, ORDER_TYPE::STOP, ModeImmediate);
        }
        currState &= ~DSS_RECYCLE_MASK;
      }
      break;
    case SECONDARY_ORDER::CIRCLE:
      if (state & DSS_CIRCLE_SET) {
        currState |= DSS_CIRCLE_SET;
      }
      else {
        currState &= ~DSS_CIRCLE_MASK;
      }
      break;
    case PATROL:
      if (state & DSS_PATROL_SET) {
        currState |= DSS_PATROL_SET;
      }
      else {
        currState &= ~DSS_PATROL_MASK;
      }
      break;
    case HALT_TYPE:
      switch (state & DSS_HALT_MASK) {
        case DSS_HALT_PURSUE:
          currState &= ~ DSS_HALT_MASK;
          currState |= DSS_HALT_PURSUE;
          if (orderState(this, ORDER_TYPE::GUARD)) {
            orderDroid(this, ORDER_TYPE::STOP, ModeImmediate);
          }
          break;
        case DSS_HALT_GUARD:
          currState &= ~ DSS_HALT_MASK;
          currState |= DSS_HALT_GUARD;
          orderDroidLoc(this, ORDER_TYPE::GUARD, getPosition().x,
                        getPosition().y, ModeImmediate);
          break;
        case DSS_HALT_HOLD:
          currState &= ~ DSS_HALT_MASK;
          currState |= DSS_HALT_HOLD;
          if (!orderState(this, ORDER_TYPE::FIRE_SUPPORT)) {
            orderDroid(this, ORDER_TYPE::STOP, ModeImmediate);
          }
          break;
      }
      break;
    case RETURN_TO_LOCATION:
      if ((state & DSS_RTL_MASK) == 0) {
        if (orderState(this, ORDER_TYPE::RETURN_TO_REPAIR) ||
            orderState(this, ORDER_TYPE::RETURN_TO_BASE) ||
            orderState(this, ORDER_TYPE::EMBARK)) {

              orderDroid(this, ORDER_TYPE::STOP, ModeImmediate);
        }
        currState &= ~DSS_RTL_MASK;
      }
      else {
        auto order_ = ORDER_TYPE::NONE;
        currState &= ~DSS_RTL_MASK;
        if ((currState & DSS_HALT_MASK) == DSS_HALT_HOLD) {
          currState &= ~DSS_HALT_MASK;
          currState |= DSS_HALT_GUARD;
        }
        switch (state & DSS_RTL_MASK) {
          case DSS_RTL_REPAIR:
            order_ = ORDER_TYPE::RETURN_TO_REPAIR;
            currState |= DSS_RTL_REPAIR;
            // can't clear the selection here as it breaks the
            // secondary order screen
            break;
          case DSS_RTL_BASE:
            order_ = ORDER_TYPE::RETURN_TO_BASE;
            currState |= DSS_RTL_BASE;
            break;
          case DSS_RTL_TRANSPORT:
          {
            auto psTransport = FindATransporter(this);
            if (psTransport != nullptr) {
              order_ = ORDER_TYPE::EMBARK;
              currState |= DSS_RTL_TRANSPORT;
              if (!orderState(this, ORDER_TYPE::EMBARK)) {
                orderDroidObj(this, ORDER_TYPE::EMBARK, psTransport, ModeImmediate);
              }
            } else {
              retVal = false;
            }
            break;
          }
          default:
            order_ = ORDER_TYPE::NONE;
            break;
        }
        if (!orderState(this, order_)) {
          orderDroid(this, order_, ModeImmediate);
        }
      }
      break;

    case FIRE_DESIGNATOR:
      // don't actually set any secondary flags - the cmdDroid array is
      // always used to determine which commander is the designator
      if (state & DSS_FIREDES_SET) {
        cmdDroidSetDesignator(this);
      }
      else if (cmdDroidGetDesignator(playerManager->getPlayer()) == this) {
        cmdDroidClearDesignator(playerManager->getPlayer());
      }
      break;
    default:
      break;
  }

  if (currState != newSecondaryState) {
    debug(LOG_WARNING,
          "Guessed the new secondary state incorrectly, expected 0x%08X, got 0x%08X, "
          "was 0x%08X, sec = %d, state = 0x%08X.",
          newSecondaryState, currState, pimpl->secondaryOrder, sec, state);
  }
  pimpl->secondaryOrder = currState;
  pimpl->secondaryOrderPendingCount = std::max(pimpl->secondaryOrderPendingCount - 1, 0);
  if (pimpl->secondaryOrderPendingCount == 0) {
    pimpl->secondaryOrderPending = pimpl->secondaryOrder;
    // if no orders are pending, make sure UI uses the actual state.
  }
  return retVal;
}

/// Balance the load at random - always prefer faster repairs
RtrBestResult Droid::decideWhereToRepairAndBalance()
{
  if (!pimpl) return {};
  int bestDistToRepairFac = INT32_MAX, bestDistToRepairDroid = INT32_MAX;
  int thisDistToRepair = 0;
  Structure* psHq = nullptr;
  Position bestDroidPos, bestFacPos;
  // static to save allocations
  static std::vector<Position> vFacilityPos;
  static std::vector<::Structure*> vFacility;
  static std::vector<int> vFacilityCloseEnough;
  static std::vector<Position> vDroidPos;
  static std::vector<::Droid*> vDroid;
  static std::vector<int> vDroidCloseEnough;
  // clear vectors from previous invocations
  vFacilityPos.clear();
  vFacility.clear();
  vFacilityCloseEnough.clear();
  vDroidCloseEnough.clear();
  vDroidPos.clear();
  vDroid.clear();

  using enum STRUCTURE_TYPE;
  for (auto& psStruct : playerList[playerManager->getPlayer()].structures)
  {
    if (psStruct.getStats()->type == HQ) {
      psHq = &psStruct;
      continue;
    }
    if (psStruct.getStats()->type == REPAIR_FACILITY &&
        psStruct.getState() == STRUCTURE_STATE::BUILT) {
      thisDistToRepair = droidSqDist(this, &psStruct);
      if (thisDistToRepair <= 0) {
        continue; // cannot reach position
      }
      vFacilityPos.push_back(psStruct.getPosition());
      vFacility.push_back(&psStruct);
      if (bestDistToRepairFac > thisDistToRepair) {
        bestDistToRepairFac = thisDistToRepair;
        bestFacPos = psStruct.getPosition();
      }
    }
  }
  // if we are repair droid ourselves, don't consider other repairs droids
  // because that causes havoc on front line: RT repairing themselves,
  // blocking everyone else. And everyone else moving toward RT, also toward front line.s
  // Ideally, we should just avoid retreating toward "danger", but dangerMap is only for multiplayer
  if (pimpl->type != DROID_TYPE::REPAIRER &&
      pimpl->type != DROID_TYPE::CYBORG_REPAIR) {
    // one of these lists is empty when on mission
    auto psdroidList = !(playerList[playerManager->getPlayer()].droids.empty())
                         ? playerList[playerManager->getPlayer()].droids
                         : mission.players[playerManager->getPlayer()].droids;

    for (auto& psCurr : psdroidList)
    {
      if (psCurr.getType() == DROID_TYPE::REPAIRER ||
          psCurr.getType() == DROID_TYPE::CYBORG_REPAIR) {
        thisDistToRepair = droidSqDist(this, &psCurr);
        if (thisDistToRepair <= 0) {
          continue; // unreachable
        }
        vDroidPos.push_back(psCurr.getPosition());
        vDroid.push_back(&psCurr);
        if (bestDistToRepairDroid > thisDistToRepair) {
          bestDistToRepairDroid = thisDistToRepair;
          bestDroidPos = psCurr.getPosition();
        }
      }
    }
  }

  ASSERT(bestDistToRepairFac > 0, "Bad distance to repair facility");
  ASSERT(bestDistToRepairDroid > 0, "Bad distance to repair droid");
  // debug(LOG_INFO, "found a total of %lu RT, and %lu RF", vDroid.size(), vFacility.size());

  // the center of this area starts at the closest repair droid/facility!
  static constexpr auto MAGIC_SUITABLE_REPAIR_AREA = (REPAIR_RANGE * 3) * (REPAIR_RANGE * 3);

  auto bestRepairPoint = bestDistToRepairFac < bestDistToRepairDroid
          ? bestFacPos
          : bestDroidPos;

  // find all close enough repairing candidates
  for (auto i = 0; i < vFacilityPos.size(); i++)
  {
    Vector2i diff = (bestRepairPoint - vFacilityPos[i]).xy();
    if (dot(diff, diff) < MAGIC_SUITABLE_REPAIR_AREA) {
      vFacilityCloseEnough.push_back(i);
    }
  }
  for (auto i = 0; i < vDroidPos.size(); i++)
  {
    Vector2i diff = (bestRepairPoint - vDroidPos[i]).xy();
    if (dot(diff, diff) < MAGIC_SUITABLE_REPAIR_AREA) {
      vDroidCloseEnough.push_back(i);
    }
  }

  // debug(LOG_INFO, "found  %lu RT, and %lu RF in suitable area", vDroidCloseEnough.size(), vFacilityCloseEnough.size());
  // prefer facilities, they re much more efficient than droids
  if (vFacilityCloseEnough.size() == 1) {
    return {RTR_DATA_TYPE::REPAIR_FACILITY, vFacility[vFacilityCloseEnough[0]]};
  }
  else if (vFacilityCloseEnough.size() > 1) {
    auto which = gameRand(vFacilityCloseEnough.size());
    return {RTR_DATA_TYPE::REPAIR_FACILITY, vFacility[vFacilityCloseEnough[which]]};
  }

  // no facilities :( fallback on droids
  if (vDroidCloseEnough.size() == 1) {
    return {RTR_DATA_TYPE::DROID, vDroid[vDroidCloseEnough[0]]};
  }
  else if (vDroidCloseEnough.size() > 1) {
    auto which = gameRand(vDroidCloseEnough.size());
    return {RTR_DATA_TYPE::DROID, vDroid[vDroidCloseEnough[which]]};
  }

  // go to headquarters, if any
  if (psHq != nullptr) {
    return {RTR_DATA_TYPE::HQ, psHq};
  }

  // screw it
  return {RTR_DATA_TYPE::NO_RESULT, nullptr};
}

/** This function returns the droid order's secondary state of the secondary order.*/
SECONDARY_STATE Droid::secondaryGetState(SECONDARY_ORDER sec, QUEUE_MODE mode)
{
  auto state = pimpl->secondaryOrder;

  if (mode == ModeQueue) {
    state = pimpl->secondaryOrderPending;
    // the UI wants to know the state, so return what the state
    // will be after orders are synchronised.
  }

  using enum SECONDARY_ORDER;
  switch (sec) {
    case ATTACK_RANGE:
      return (SECONDARY_STATE)(state & DSS_ARANGE_MASK);
      break;
    case REPAIR_LEVEL:
      return (SECONDARY_STATE)(state & DSS_REPLEV_MASK);
      break;
    case ATTACK_LEVEL:
      return (SECONDARY_STATE)(state & DSS_ALEV_MASK);
      break;
    case ASSIGN_PRODUCTION:
    case ASSIGN_CYBORG_PRODUCTION:
    case ASSIGN_VTOL_PRODUCTION:
      return (SECONDARY_STATE)(state & DSS_ASSPROD_MASK);
      break;
    case RECYCLE:
      return (SECONDARY_STATE)(state & DSS_RECYCLE_MASK);
      break;
    case PATROL:
      return (SECONDARY_STATE)(state & DSS_PATROL_MASK);
      break;
    case CIRCLE:
      return (SECONDARY_STATE)(state & DSS_CIRCLE_MASK);
      break;
    case HALT_TYPE:
      if (pimpl->order->type == ORDER_TYPE::HOLD) {
        return DSS_HALT_HOLD;
      }
      return (SECONDARY_STATE)(state & DSS_HALT_MASK);
      break;
    case RETURN_TO_LOCATION:
      return (SECONDARY_STATE)(state & DSS_RTL_MASK);
      break;
    case FIRE_DESIGNATOR:
      if (cmdDroidGetDesignator(playerManager->getPlayer()) == this) {
        return DSS_FIREDES_SET;
      }
      break;
    default:
      break;
  }
  return DSS_NONE;
}

/**
 * Add an order to a droid's order list
 */
void Droid::orderDroidAdd(Order* order_)
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Droid object is undefined");
  if (pimpl->asOrderList.size() >= pimpl->asOrderList.size()) {
    // Make more room to store the order.
    pimpl->asOrderList.resize(pimpl->asOrderList.size() + 1);
  }

  pimpl->asOrderList[pimpl->asOrderList.size()] = *order_;
  using enum ORDER_TYPE;
  // if not doing anything - do it immediately
  if (pimpl->asOrderList.size() <= 1 &&
      (pimpl->order->type == NONE ||
       pimpl->order->type == GUARD ||
       pimpl->order->type == PATROL ||
       pimpl->order->type == CIRCLE ||
       pimpl->order->type == HOLD)) {
    orderDroidList();
  }
}

void Droid::orderDroidAddPending(Order* order_)
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Droid object is undefined");
  pimpl->asOrderList.push_back(*order_);

  // only display one arrow, bOrderEffectDisplayed must be set
  // to false once per arrow.
  if (!bOrderEffectDisplayed) {
    Vector3i position(0, 0, 0);
    if (order_->target == nullptr) {
      position.x = order_->pos.x;
      position.z = order_->pos.y;
    }
    else {
      position = order_->target->getPosition().xzy();
    }
    position.y = map_Height(position.x, position.z) + 32;
    if (order_->target != nullptr &&
        order_->target->getDisplayData()->imd_shape != nullptr) {
      position.y += order_->target->getDisplayData()->imd_shape->max.y;
    }
    addEffect(&position, EFFECT_GROUP::WAYPOINT,EFFECT_TYPE::WAYPOINT_TYPE,
              false, nullptr, 0);
    bOrderEffectDisplayed = true;
  }
}

/**
 * This function checks its order list: if a given order needs a target and
 * the target has died, the order is removed from the list.
 */
void Droid::orderCheckList()
{
  if (!pimpl) return;
  for (auto i = 0; i < pimpl->asOrderList.size(); ++i)
  {
    auto psTarget = pimpl->asOrderList[i].target;
    if (psTarget == nullptr || !dynamic_cast<Health *>(psTarget)->isDead()) {
      continue;
    }
    if ((int)i < pimpl->asOrderList.size()) {
      syncDebug("droid%d list erase dead droid%d", getId(), psTarget->getId());
    }
    orderDroidListEraseRange(i, i + 1);
    --i; // If this underflows, the ++i will overflow it back.
  }
}

void Droid::moveStopDroid()
{
  if (!pimpl) return;
  using enum MOVE_STATUS;
  auto psPropStats = dynamic_cast<PropulsionStats const*>(getComponent(COMPONENT_TYPE::PROPULSION));
  ASSERT_OR_RETURN(, psPropStats != nullptr, "invalid propulsion stats pointer");

  if (psPropStats->propulsionType == PROPULSION_TYPE::LIFT) {
    pimpl->movement->status = HOVER;
  }
  else {
    pimpl->movement->status = INACTIVE;
  }
}

/** Stops a droid dead in its tracks.
 *  Doesn't allow for any little skidding bits.
 *  @param psDroid the droid to stop from moving
 */
void Droid::moveReallyStopDroid()
{
  if (!pimpl) return;
  pimpl->movement->status = MOVE_STATUS::INACTIVE;
  pimpl->movement->speed = 0;
}

// Returns true if still able to find the path.
bool Droid::moveBestTarget()
{
  if (!pimpl) return false;
  auto positionIndex = std::max(pimpl->movement->pathIndex - 1, 0);
  auto dist = moveDirectPathToWaypoint(positionIndex);
  if (dist >= 0) {
    // Look ahead in the path.
    while (dist >= 0 && dist < TILE_UNITS * 5)
    {
      ++positionIndex;
      if (positionIndex >= pimpl->movement->path.size()) {
        dist = -1;
        break; // Reached end of path.
      }
      dist = moveDirectPathToWaypoint(positionIndex);
    }
    if (dist < 0) {
      --positionIndex;
    }
  }
  else {
    // Lost sight of path, backtrack.
    while (dist < 0 && dist >= -TILE_UNITS * 7 && positionIndex > 0)
    {
      --positionIndex;
      dist = moveDirectPathToWaypoint(positionIndex);
    }
    if (dist < 0) {
      return false; // Couldn't find path, and backtracking didn't help.
    }
  }
  pimpl->movement->pathIndex = positionIndex + 1;
  pimpl->movement->src = getPosition().xy();
  pimpl->movement->target = pimpl->movement->path[positionIndex];
  return true;
}

/* Get the next target point from the route */
bool Droid::moveNextTarget()
{
  if (!pimpl) return false;
  // See if there is anything left in the move list
  if (pimpl->movement->pathIndex == pimpl->movement->path.size()) {
    return false;
  }
  ASSERT_OR_RETURN(
          false, pimpl->movement->pathIndex >= 0 && pimpl->movement->pathIndex < pimpl->movement->path.size(),
          "psDroid->sMove.pathIndex out of bounds %d/%d.", pimpl->movement->pathIndex, pimpl->movement->path.size());

  if (pimpl->movement->pathIndex == 0) {
    pimpl->movement->src = getPosition().xy();
  }
  else {
    pimpl->movement->src = pimpl->movement->path[pimpl->movement->pathIndex - 1];
  }
  pimpl->movement->target = pimpl->movement->path[pimpl->movement->pathIndex];
  ++pimpl->movement->pathIndex;
  return true;
}
  
// See if the droid has been stopped long enough to give up on the move
bool Droid::moveBlocked()
{
  if (!pimpl) return false;
  if (pimpl->movement->bumpTime == 0 || pimpl->movement->bumpTime > gameTime) {
    // no bump - can't be blocked
    return false;
  }

  // See if the block can be cancelled
  if (abs(angleDelta(getRotation().direction - pimpl->movement->bumpDir)) > DEG(BLOCK_DIR)) {
    // Move on, clear the bump
    pimpl->movement->bumpTime = 0;
    pimpl->movement->lastBump = 0;
    return false;
  }
  auto xdiff = getPosition().x - (SDWORD)pimpl->movement->bumpPos.x;
  auto ydiff = getPosition().y - (SDWORD)pimpl->movement->bumpPos.y;
  auto diffSq = xdiff * xdiff + ydiff * ydiff;
  if (diffSq > BLOCK_DIST * BLOCK_DIST) {
    // Move on, clear the bump
    pimpl->movement->bumpTime = 0;
    pimpl->movement->lastBump = 0;
    return false;
  }

  unsigned blockTime;
  if (pimpl->movement->status == MOVE_STATUS::SHUFFLE) {
    blockTime = SHUFFLE_BLOCK_TIME;
  }
  else {
    blockTime = BLOCK_TIME;
  }

  if (gameTime - pimpl->movement->bumpTime > blockTime) {
    // Stopped long enough - blocked
    pimpl->movement->bumpTime = 0;
    pimpl->movement->lastBump = 0;
    if (!isHumanPlayer(playerManager->getPlayer()) && bMultiPlayer) {
      pimpl->lastFrustratedTime = gameTime;
      objTrace(getId(), "FRUSTRATED");
    }
    else {
      objTrace(getId(), "BLOCKED");
    }
    // if the unit cannot see the next way point - reroute it's got stuck
    if ((bMultiPlayer || playerManager->getPlayer() == selectedPlayer ||
         pimpl->lastFrustratedTime == gameTime) &&
        pimpl->movement->pathIndex != (int)pimpl->movement->path.size()) {
      objTrace(getId(), "Trying to reroute to (%d,%d)",
               pimpl->movement->destination.x, pimpl->movement->destination.y);
      moveDroidTo(this, pimpl->movement->destination.x,
                  pimpl->movement->destination.y);
      return false;
    }
    return true;
  }
  return false;
}

std::string Droid::getDroidLevelName() const
{
  auto psStats = dynamic_cast<CommanderStats const*>(getComponent(COMPONENT_TYPE::BRAIN));
  if (!psStats) return "";
  return PE_("rank", psStats->rankNames[getDroidLevel(this)].c_str());
}

// see if a droid has run into a blocking tile
// TODO See if this function can be simplified.
void Droid::moveCalcBlockingSlide(int* pmx, int* pmy, uint16_t tarDir, uint16_t* pSlideDir)
{
  if (!pimpl) return;
  auto propulsion = dynamic_cast<PropulsionStats const*>(getComponent(COMPONENT_TYPE::PROPULSION));
  if (!propulsion) return;
  auto propulsionType = propulsion->propulsionType;
  int horizX, horizY, vertX, vertY;
  uint16_t slideDir;
  // calculate the new coords and see if they are on a different tile
  const auto mx = gameTimeAdjustedAverage(*pmx, EXTRA_PRECISION);
  const auto my = gameTimeAdjustedAverage(*pmy, EXTRA_PRECISION);
  const auto tx = map_coord(getPosition().x);
  const auto ty = map_coord(getPosition().y);
  const auto nx = getPosition().x + mx;
  const auto ny = getPosition().y + my;
  const auto ntx = map_coord(nx);
  const auto nty = map_coord(ny);
  const auto blkCX = world_coord(ntx) + TILE_UNITS / 2;
  const auto blkCY = world_coord(nty) + TILE_UNITS / 2;

  // is the new tile a gate?
  moveOpenGates(this, Vector2i(ntx, nty));

  // is the new tile blocking?
  if (!fpathBlockingTile(ntx, nty, propulsionType)) {
    // not blocking, don't change the move vector
    return;
  }

  // if the droid is shuffling - just stop
  if (pimpl->movement->status == MOVE_STATUS::SHUFFLE) {
    objTrace(getId(), "Was shuffling, now stopped");
    pimpl->movement->status = MOVE_STATUS::INACTIVE;
  }

  // note the bump time and position if necessary
  if (!isVtol() &&
      pimpl->movement->bumpTime == 0) {
    pimpl->movement->bumpTime = gameTime;
    pimpl->movement->lastBump = 0;
    pimpl->movement->pauseTime = 0;
    pimpl->movement->bumpPos = getPosition();
    pimpl->movement->bumpDir = getRotation().direction;
  }

  if (tx != ntx && ty != nty) {
    // moved diagonally
    // figure out where the other two possible blocking tiles are
    horizX = mx < 0 ? ntx + 1 : ntx - 1;
    horizY = nty;

    vertX = ntx;
    vertY = my < 0 ? nty + 1 : nty - 1;

    if (fpathBlockingTile(horizX, horizY, propulsionType) &&
        fpathBlockingTile(vertX, vertY, propulsionType)) {
      // in a corner - choose an arbitrary slide
      if (gameRand(2) == 0) {
        *pmx = 0;
        *pmy = -*pmy;
      }
      else {
        *pmx = -*pmx;
        *pmy = 0;
      }
    }
    else if (fpathBlockingTile(horizX, horizY, propulsionType)) {
      *pmy = 0;
    }
    else if (fpathBlockingTile(vertX, vertY, propulsionType)) {
      *pmx = 0;
    }
    else {
      moveCalcSlideVector(this, blkCX, blkCY, pmx, pmy);
    }
  }
  else if (tx != ntx) {
    // moved horizontally - see which half of the tile were in
    if ((getPosition().y & TILE_MASK) > TILE_UNITS / 2) {
      // top half
      if (fpathBlockingTile(ntx, nty + 1, propulsionType)) {
        *pmx = 0;
      }
      else {
        moveCalcSlideVector(this, blkCX, blkCY, pmx, pmy);
      }
    }
    else {
      // bottom half
      if (fpathBlockingTile(ntx, nty - 1, propulsionType)) {
        *pmx = 0;
      }
      else {
        moveCalcSlideVector(this, blkCX, blkCY, pmx, pmy);
      }
    }
  }
  else if (ty != nty) {
    // moved vertically
    if ((getPosition().x & TILE_MASK) > TILE_UNITS / 2) {
      // top half
      if (fpathBlockingTile(ntx + 1, nty, propulsionType)) {
        *pmy = 0;
      }
      else {
        moveCalcSlideVector(this, blkCX, blkCY, pmx, pmy);
      }
    }
    else {
      // bottom half
      if (fpathBlockingTile(ntx - 1, nty, propulsionType)) {
        *pmy = 0;
      }
      else {
        moveCalcSlideVector(this, blkCX, blkCY, pmx, pmy);
      }
    }
  }
  // if (tx == ntx && ty == nty)
  else {
    // on a blocking tile - see if we need to jump off
    auto intx = getPosition().x & TILE_MASK;
    auto inty = getPosition().y & TILE_MASK;
    bool bJumped = false;
    auto jumpx = getPosition().x;
    auto jumpy = getPosition().y;

    if (intx < TILE_UNITS / 2)
    {
      if (inty < TILE_UNITS / 2)
      {
        // top left
        if ((mx < 0) && fpathBlockingTile(tx - 1, ty, propulsionType))
        {
          bJumped = true;
          jumpy = (jumpy & ~TILE_MASK) - 1;
        }
        if ((my < 0) && fpathBlockingTile(tx, ty - 1, propulsionType))
        {
          bJumped = true;
          jumpx = (jumpx & ~TILE_MASK) - 1;
        }
      }
      else
      {
        // bottom left
        if ((mx < 0) && fpathBlockingTile(tx - 1, ty, propulsionType))
        {
          bJumped = true;
          jumpy = (jumpy & ~TILE_MASK) + TILE_UNITS;
        }
        if ((my >= 0) && fpathBlockingTile(tx, ty + 1, propulsionType))
        {
          bJumped = true;
          jumpx = (jumpx & ~TILE_MASK) - 1;
        }
      }
    }
    else
    {
      if (inty < TILE_UNITS / 2)
      {
        // top right
        if ((mx >= 0) && fpathBlockingTile(tx + 1, ty, propulsionType))
        {
          bJumped = true;
          jumpy = (jumpy & ~TILE_MASK) - 1;
        }
        if ((my < 0) && fpathBlockingTile(tx, ty - 1, propulsionType))
        {
          bJumped = true;
          jumpx = (jumpx & ~TILE_MASK) + TILE_UNITS;
        }
      }
      else
      {
        // bottom right
        if ((mx >= 0) && fpathBlockingTile(tx + 1, ty, propulsionType))
        {
          bJumped = true;
          jumpy = (jumpy & ~TILE_MASK) + TILE_UNITS;
        }
        if ((my >= 0) && fpathBlockingTile(tx, ty + 1, propulsionType))
        {
          bJumped = true;
          jumpx = (jumpx & ~TILE_MASK) + TILE_UNITS;
        }
      }
    }

    if (bJumped) {
      setPosition({MAX(0, jumpx), MAX(0, jumpy), getPosition().z});
      *pmx = 0;
      *pmy = 0;
    }
    else
    {
      moveCalcSlideVector(this, blkCX, blkCY, pmx, pmy);
    }
  }

  slideDir = iAtan2(*pmx, *pmy);
  if (ntx != tx)
  {
    // hit a horizontal block
    if ((tarDir < DEG(90) || tarDir > DEG(270)) &&
        (slideDir >= DEG(90) && slideDir <= DEG(270)))
    {
      slideDir = tarDir;
    }
    else if ((tarDir >= DEG(90) && tarDir <= DEG(270)) &&
             (slideDir < DEG(90) || slideDir > DEG(270)))
    {
      slideDir = tarDir;
    }
  }
  if (nty != ty)
  {
    // hit a vertical block
    if ((tarDir < DEG(180)) &&
        (slideDir >= DEG(180)))
    {
      slideDir = tarDir;
    }
    else if ((tarDir >= DEG(180)) &&
             (slideDir < DEG(180)))
    {
      slideDir = tarDir;
    }
  }
  *pSlideDir = slideDir;
}

int Droid::objRadius() const
{
  if (getType() == DROID_TYPE::PERSON) return mvPersRad;
  else if (isCyborg(this)) return mvCybRad;

  const auto bodyStats = dynamic_cast<BodyStats const*>(getComponent(COMPONENT_TYPE::BODY));
  switch (bodyStats->size) {
    using enum BODY_SIZE;
    case LIGHT:
      return mvSmRad;
    case MEDIUM:
      return mvMedRad;
    case HEAVY:
      return mvLgRad;
    case SUPER_HEAVY:
      return 130;
    default:
      return getDisplayData()->imd_shape->radius;
  }
}

void Droid::movePlayDroidMoveAudio()
{
  int iAudioID = NO_SOUND;

  if (!isVisibleToSelectedPlayer()) return;

  auto psPropStats = dynamic_cast<PropulsionStats const*>(getComponent(COMPONENT_TYPE::PROPULSION));
  ASSERT_OR_RETURN(, psPropStats != nullptr, "Invalid propulsion stats pointer");
  auto psPropType = psPropStats->propulsionType;

  /* play specific wheeled and transporter or stats-specified noises */
  if (psPropType == PROPULSION_TYPE::WHEELED &&
      getType() != DROID_TYPE::CONSTRUCT) {
    iAudioID = ID_SOUND_TREAD;
  }
  else if (isTransporter(*this)) {
    iAudioID = ID_SOUND_BLIMP_FLIGHT;
  }
  else if (psPropType == PROPULSION_TYPE::LEGGED && isCyborg(this)) {
    iAudioID = ID_SOUND_CYBORG_MOVE;
  }
  else {
    iAudioID = asPropulsionTypes[static_cast<int>(psPropType)].moveID;
  }

  if (iAudioID != NO_SOUND) {
    if (audio_PlayObjDynamicTrack(this, iAudioID,
                                  moveCheckDroidMovingAndVisible)) {
      iAudioID = iAudioID;
    }
  }
}

/* Update a tracked droids position and speed given target values */
void Droid::moveUpdateGroundModel(int speed, uint16_t direction)
{
  if (!pimpl) return;
  uint16_t iDroidDir;
  uint16_t slideDir;
  int dx, dy;

  // nothing to do if the droid is stopped
  if (moveDroidStopped(this, speed)) {
    return;
  }

  auto psPropStats = dynamic_cast<PropulsionStats const*>(getComponent(COMPONENT_TYPE::PROPULSION));
  if (!psPropStats) return;

  auto spinSpeed = pimpl->baseSpeed * psPropStats->spinSpeed;
  auto turnSpeed = pimpl->baseSpeed * psPropStats->turnSpeed;
  auto spinAngle = DEG(psPropStats->spinAngle);

  moveCheckFinalWaypoint(this, &speed);

  moveUpdateDroidDirection(this, &speed, direction, spinAngle, spinSpeed, turnSpeed, &iDroidDir);

  auto fNormalSpeed = moveCalcNormalSpeed(this, speed, iDroidDir, psPropStats->acceleration, psPropStats->deceleration);
  auto fPerpSpeed = moveCalcPerpSpeed(this, iDroidDir, psPropStats->skidDeceleration);

  moveCombineNormalAndPerpSpeeds(fNormalSpeed, fPerpSpeed, iDroidDir);
  moveGetDroidPosDiffs(this, &dx, &dy);
  moveOpenGates(this);
  moveCheckSquished(this, dx, dy);
  moveCalcDroidSlide(&dx, &dy);
  auto bx = dx;
  auto by = dy;
  moveCalcBlockingSlide(&bx, &by, direction, &slideDir);
  if (bx != dx || by != dy) {
    moveUpdateDroidDirection(this, &speed, slideDir, spinAngle, pimpl->baseSpeed * DEG(1),
                             pimpl->baseSpeed * DEG(1) / 3, &iDroidDir);
    setRotation({iDroidDir, getRotation().pitch, getRotation().roll});
  }

  moveUpdateDroidPos(this, bx, by);

  //set the droid height here so other routines can use it
  auto z = map_Height(getPosition().x, getPosition().y); //jps 21july96
  setPosition({getPosition().x, getPosition().y, z});
  updateDroidOrientation();
}
    
void Droid::moveCombineNormalAndPerpSpeeds(int fNormalSpeed, int fPerpSpeed, uint16_t iDroidDir)
{
  if (!pimpl) return;
  /* set current direction */
  setRotation({iDroidDir, getRotation().pitch, getRotation().roll});

  /* set normal speed and direction if perpendicular speed is zero */
  if (fPerpSpeed == 0) {
    pimpl->movement->speed = fNormalSpeed;
    pimpl->movement->moveDir = iDroidDir;
    return;
  }

  auto finalSpeed = iHypot(fNormalSpeed, fPerpSpeed);

  // calculate the angle between the droid facing and movement direction
  auto relDir = iAtan2(fPerpSpeed, fNormalSpeed);

  // choose the finalDir on the same side as the old movement direction
  auto adiff = angleDelta(iDroidDir - pimpl->movement->moveDir);

  pimpl->movement->moveDir = adiff < 0 ? iDroidDir + relDir : iDroidDir - relDir; // Cast wrapping intended.
  pimpl->movement->speed = finalSpeed;
}

bool Droid::droidUpdateDemolishing()
{
  ASSERT_OR_RETURN(false, getAction() == ACTION::DEMOLISH, "unit is not demolishing");
  auto psStruct = dynamic_cast<Structure*>(getOrder()->target);
  ASSERT_OR_RETURN(false, psStruct, "target is not a structure");

  if (auto construct = dynamic_cast<ConstructStats const*>(getComponent(COMPONENT_TYPE::PROPULSION))) {
    auto constructRate = 5 * constructorPoints(construct, playerManager->getPlayer());
    auto pointsToAdd = gameTimeAdjustedAverage(constructRate);

    structureDemolish(psStruct, this, pointsToAdd);
    addConstructorEffect(psStruct);
    return true;
  }
  return false;
}

/** Calculate the new speed for a droid based on factors like pitch.
 *  @todo Remove hack for steep slopes not properly marked as blocking on some maps.
 */
int Droid::moveCalcDroidSpeed()
{
  if (!pimpl) return -1;
  const auto maxPitch = DEG(MAX_SPEED_PITCH);
  unsigned speed, pitch;

  // NOTE: This screws up since the transporter is offscreen still (on a mission!), and we are trying to find terrainType of a tile (that is offscreen!)
  auto propulsion = dynamic_cast<PropulsionStats const*>(getComponent(COMPONENT_TYPE::PROPULSION));
  if (propulsion && pimpl->type == DROID_TYPE::SUPER_TRANSPORTER && missionIsOffworld()) {
    speed = propulsion->maxSpeed;
  }
  else {
    auto mapX = map_coord(getPosition().x);
    auto mapY = map_coord(getPosition().y);
    speed = calcDroidSpeed(pimpl->baseSpeed, terrainType(mapTile(mapX, mapY)),
                           propulsion, getDroidEffectiveLevel(this));
  }


  // now offset the speed for the slope of the droid
  pitch = angleDelta(getRotation().pitch);
  speed = (maxPitch - pitch) * speed / maxPitch;
  if (speed <= 10) {
    // Very nasty hack to deal with buggy maps, where some cliffs are
    // not properly marked as being cliffs, but too steep to drive over.
    // This confuses the heck out of the path-finding code! - Per
    speed = 10;
  }

  // stop droids that have just fired a no fire while moving weapon
  if (numWeapons(*this) > 0) {
    if (weaponManager->weapons[0].timeLastFired + FOM_MOVEPAUSE > gameTime) {
      auto psWStats = weaponManager->weapons[0].stats.get();
      if (!psWStats->fireOnMove) {
        speed = 0;
      }
    }
  }

  // slow down shuffling VTOLs
  if (isVtol() &&
      pimpl->movement->status == MOVE_STATUS::SHUFFLE &&
      (speed > MIN_END_SPEED)) {
    speed = MIN_END_SPEED;
  }

  return speed;
}

// get an obstacle avoidance vector
Vector2i Droid::moveGetObstacleVector(Vector2i dest)
{
  int numObst = 0, distTot = 0;
  Vector2i dir(0, 0);
  auto psPropStats = dynamic_cast<PropulsionStats const*>(getComponent(COMPONENT_TYPE::PROPULSION));
  ASSERT_OR_RETURN(dir, psPropStats, "invalid propulsion stats pointer");

  int ourMaxSpeed = psPropStats->maxSpeed;
  auto ourRadius = objRadius();
  if (ourMaxSpeed == 0) {
    return dest; // No point deciding which way to go, if we can't move...
  }

  // scan the neighbours for obstacles
  static GridList gridList; // static to avoid allocations.
  gridList = gridStartIterate(getPosition().x, getPosition().y, AVOID_DIST);
  for (auto & gi : gridList)
  {
    if (gi == this) continue; // Don't try to avoid ourselves.

    auto psObstacle = dynamic_cast<Droid*>(gi);
    if (psObstacle == nullptr) continue; // Object wrong type to worry about

    // vtol droids only avoid each other and don't affect ground droids
    if (isVtol() != psObstacle->isVtol()) {
      continue;
    }

    if (isTransporter(*psObstacle) ||
        (psObstacle->getType() == DROID_TYPE::PERSON &&
         psObstacle->playerManager->getPlayer() != playerManager->getPlayer())) {
      // don't avoid people on the other side - run over them
      continue;
    }

    auto temp = psObstacle->getComponent(COMPONENT_TYPE::PROPULSION);
    auto obstaclePropStats = dynamic_cast<PropulsionStats const*>(temp);
    auto obstacleMaxSpeed = obstaclePropStats->maxSpeed;
    auto obstacleRadius = psObstacle->objRadius();
    auto totalRadius = ourRadius + obstacleRadius;

    // Try to guess where the obstacle will be when we get close.
    // Velocity guess 1: Guess the velocity the droid is actually moving at.
    Vector2i obstVelocityGuess1 = iSinCosR(psObstacle->getMovementData()->moveDir, psObstacle->getMovementData()->speed);
    // Velocity guess 2: Guess the velocity the droid wants to move at.
    Vector2i obstTargetDiff = psObstacle->getMovementData()->target - psObstacle->getPosition().xy();
    Vector2i obstVelocityGuess2 = iSinCosR(iAtan2(obstTargetDiff),
                                           obstacleMaxSpeed * std::min(iHypot(obstTargetDiff), AVOID_DIST) /
                                           AVOID_DIST);
    if (psObstacle->moveBlocked()) {
      obstVelocityGuess2 = Vector2i(0, 0); // This obstacle isn't going anywhere, even if it wants to.
      //obstVelocityGuess2 = -obstVelocityGuess2;
    }
    // Guess the average of the two guesses.
    Vector2i obstVelocityGuess = (obstVelocityGuess1 + obstVelocityGuess2) / 2;

    // Find the guessed obstacle speed and direction, clamped to half our speed.
    auto obstSpeedGuess = std::min(iHypot(obstVelocityGuess), ourMaxSpeed / 2);
    uint16_t obstDirectionGuess = iAtan2(obstVelocityGuess);

    // Position of obstacle relative to us.
    Vector2i diff = (psObstacle->getPosition() - getPosition()).xy();

    // Find very approximate position of obstacle relative to us when we get close, based on our guesses.
    Vector2i deltaDiff = iSinCosR(obstDirectionGuess,
                                  std::max(iHypot(diff) - totalRadius * 2 / 3, 0)
                                  * obstSpeedGuess / ourMaxSpeed);
    if (!fpathBlockingTile(
            map_coord(psObstacle->getPosition().x + deltaDiff.x),
            map_coord(psObstacle->getPosition().y + deltaDiff.y),
            obstaclePropStats->propulsionType)) // Don't assume obstacle can go through cliffs.
    {
      diff += deltaDiff;
    }

    if (dot(diff, dest) < 0) {
      // object behind
      continue;
    }

    auto centreDist = std::max(iHypot(diff), 1);
    auto dist = std::max(centreDist - totalRadius, 1);

    dir += diff * 65536 / (centreDist * dist);
    distTot += 65536 / dist;
    numObst += 1;
  }

  if (dir == Vector2i(0, 0) || numObst == 0) {
    return dest;
  }

  dir = Vector2i(dir.x / numObst, dir.y / numObst);
  distTot /= numObst;

  // Create the avoid vector
  Vector2i o(dir.y, -dir.x);
  Vector2i avoid = dot(dest, o) < 0 ? -o : o;

  // Normalise dest and avoid.
  dest = dest * 32767 / (iHypot(dest) + 1);
  avoid = avoid * 32767 / (iHypot(avoid) + 1);
  // avoid.x and avoid.y are up to 65536, so we can multiply by at most 32767 here without potential overflow.

  // combine the avoid vector and the target vector
  auto ratio = std::min(distTot * ourRadius / 2, 65536);

  return dest * (65536 - ratio) + avoid * ratio;
}

/* Update a persons position and speed given target values */
void Droid::moveUpdatePersonModel(int speed, uint16_t direction)
{
  if (!pimpl) return;
  int dx, dy;
  uint16_t iDroidDir;
  uint16_t slideDir;

  // if the droid is stopped, only make sure animations are set correctly
  if (moveDroidStopped(this, speed)) {
    if (pimpl->type == DROID_TYPE::PERSON &&
        (pimpl->action == ACTION::ATTACK ||
         pimpl->action == ACTION::ROTATE_TO_ATTACK) &&
        pimpl->animationEvent != ANIM_EVENT_DYING &&
        pimpl->animationEvent != ANIM_EVENT_FIRING) {

      pimpl->timeAnimationStarted = gameTime;
      pimpl->animationEvent = ANIM_EVENT_FIRING;
    }
    else if (pimpl->animationEvent == ANIM_EVENT_ACTIVE) {
      pimpl->timeAnimationStarted = 0; // turn off movement animation, since we stopped
      pimpl->animationEvent = ANIM_EVENT_NONE;
    }
    return;
  }

  auto psPropStats = dynamic_cast<PropulsionStats const*>(getComponent(COMPONENT_TYPE::PROPULSION));
  if (!psPropStats) return;

  auto spinSpeed = pimpl->baseSpeed * psPropStats->spinSpeed;
  auto turnSpeed = pimpl->baseSpeed * psPropStats->turnSpeed;

  moveUpdateDroidDirection(this, &speed, direction,
                           DEG(psPropStats->spinAngle), spinSpeed,
                           turnSpeed, &iDroidDir);

  auto fNormalSpeed = moveCalcNormalSpeed(this, speed,
                                          iDroidDir, psPropStats->acceleration,
                                          psPropStats->deceleration);

  /* people don't skid at the moment so set zero perpendicular speed */
  auto fPerpSpeed = 0;

  moveCombineNormalAndPerpSpeeds(fNormalSpeed, fPerpSpeed, iDroidDir);
  moveGetDroidPosDiffs(this, &dx, &dy);
  moveOpenGates(this);
  moveCalcDroidSlide(&dx, &dy);
  moveCalcBlockingSlide(&dx, &dy, direction, &slideDir);
  moveUpdateDroidPos(this, dx, dy);

  //set the droid height here so other routines can use it
  auto z = map_Height(getPosition().x, getPosition().y); //jps 21july96
  setPosition({getPosition().x, getPosition().y, z});

  /* update anim if moving */
  if (pimpl->type == DROID_TYPE::PERSON && speed != 0 &&
      pimpl->animationEvent != ANIM_EVENT_ACTIVE &&
      pimpl->animationEvent != ANIM_EVENT_DYING) {
    pimpl->timeAnimationStarted = gameTime;
    pimpl->animationEvent = ANIM_EVENT_ACTIVE;
  }
}
  
void Droid::moveUpdateVtolModel(int speed, uint16_t direction)
{
  if (!pimpl) return;
  uint16_t iDroidDir;
  uint16_t slideDir;
  int dx, dy;

  // nothing to do if the droid is stopped
  if (moveDroidStopped(this, speed)) {
    return;
  }

  auto psPropStats = dynamic_cast<PropulsionStats const*>(getComponent(COMPONENT_TYPE::PROPULSION));
  if (!psPropStats) return;

  auto spinSpeed = DEG(psPropStats->spinSpeed);
  auto turnSpeed = DEG(psPropStats->turnSpeed);

  moveCheckFinalWaypoint(this, &speed);

  if (isTransporter(*this)) {
    moveUpdateDroidDirection(this, &speed, direction, DEG(psPropStats->spinAngle), spinSpeed, turnSpeed,
                             &iDroidDir);
  }
  else {
    auto iSpinSpeed = std::max<int>(pimpl->baseSpeed * DEG(1) / 2, spinSpeed);
    auto iTurnSpeed = std::max<int>(pimpl->baseSpeed * DEG(1) / 8, turnSpeed);
    moveUpdateDroidDirection(this, &speed, direction, DEG(psPropStats->spinAngle), iSpinSpeed, iTurnSpeed,
                             &iDroidDir);
  }

  auto fNormalSpeed = moveCalcNormalSpeed(this, speed, iDroidDir, psPropStats->acceleration, psPropStats->deceleration);
  auto fPerpSpeed = moveCalcPerpSpeed(this, iDroidDir, psPropStats->skidDeceleration);

  moveCombineNormalAndPerpSpeeds(fNormalSpeed, fPerpSpeed, iDroidDir);

  moveGetDroidPosDiffs(this, &dx, &dy);

  /* set slide blocking tile for map edge */
  if (!isTransporter(*this)) {
    moveCalcBlockingSlide(&dx, &dy, direction, &slideDir);
  }

  moveUpdateDroidPos(this, dx, dy);

  /* update vtol orientation */
  auto targetRoll = clip(4 * angleDelta(pimpl->movement->moveDir - getRotation().direction), -DEG(60), DEG(60));
  auto roll = getRotation().roll + (uint16_t)gameTimeAdjustedIncrement(
          3 * angleDelta(targetRoll - getRotation().roll));
  setRotation({getRotation().direction, getRotation().pitch, roll});

  /* do vertical movement - only if on the map */
  if (worldOnMap(getPosition().x, getPosition().y)) {
    auto iMapZ = map_Height(getPosition().x, getPosition().y);
    auto z = MAX(iMapZ, getPosition().z + gameTimeAdjustedIncrement(pimpl->movement->vertical_speed));
    setPosition({getPosition().x, getPosition().y, z});
    moveAdjustVtolHeight(iMapZ);
  }
}

/* Frame update for the movement of a tracked droid */
void Droid::moveUpdateDroid()
{
  if (!pimpl) return;
  using enum MOVE_STATUS;
  UDWORD oldx, oldy;
  auto oldStatus = pimpl->movement->status;
  Vector3i pos(0, 0, 0);
  bool bStarted = false, bStopped;

  auto psPropStats = dynamic_cast<PropulsionStats const*>(getComponent(COMPONENT_TYPE::PROPULSION));
  ASSERT_OR_RETURN(, psPropStats != nullptr, "Invalid propulsion stats pointer");

  // If the droid has been attacked by an EMP weapon, it is temporarily disabled
  if (damageManager->getLastHitWeapon() == WEAPON_SUBCLASS::EMP) {
    if (gameTime - damageManager->getTimeLastHit() < EMP_DISABLE_TIME) {
      // Get out without updating
      return;
    }
  }

  /* save current motion status of droid */
  bStopped = moveDroidStopped(this, 0);

  auto moveSpeed = 0;
  auto moveDir = getRotation().direction;

  switch (pimpl->movement->status) {
    case INACTIVE:
      if (pimpl->animationEvent == ANIM_EVENT_ACTIVE) {
        pimpl->timeAnimationStarted = 0;
        pimpl->animationEvent = ANIM_EVENT_NONE;
      }
      break;
    case SHUFFLE:
      if (moveReachedWayPoint(this) ||
          pimpl->movement->shuffleStart + MOVE_SHUFFLETIME < gameTime) {
        if (psPropStats->propulsionType == PROPULSION_TYPE::LIFT) {
          pimpl->movement->status = HOVER;
        }
        else {
          pimpl->movement->status = INACTIVE;
        }
      }
      else {
        // Calculate a target vector
        moveDir = moveGetDirection(this);

        moveSpeed = moveCalcDroidSpeed();
      }
      break;
    case WAIT_FOR_ROUTE:
      moveDroidTo(this, pimpl->movement->destination.x, pimpl->movement->destination.y);
      moveSpeed = MAX(0, pimpl->movement->speed - 1);
      if (pimpl->movement->status != NAVIGATE) {
        break;
      }
      // fallthrough
    case NAVIGATE:
      // Get the next control point
      if (!moveNextTarget()) {
        // No more waypoints - finish
        if (psPropStats->propulsionType == PROPULSION_TYPE::LIFT) {
          pimpl->movement->status = HOVER;
        }
        else {
          pimpl->movement->status = INACTIVE;
        }
        break;
      }

      if (isVtol()) {
        setRotation({getRotation().direction, 0, getRotation().roll});
      }

      pimpl->movement->status = POINT_TO_POINT;
      pimpl->movement->bumpTime = 0;
      moveSpeed = MAX(0, pimpl->movement->speed - 1);

      /* save started status for movePlayAudio */
      if (pimpl->movement->speed == 0)
      {
        bStarted = true;
      }
      // fallthrough
    case POINT_TO_POINT:
    case PAUSE:
      // moving between two way points
      if (pimpl->movement->path.size() == 0) {
        debug(LOG_WARNING, "No path to follow, but psDroid->sMove.Status = %d", pimpl->movement->status);
      }

      // Get the best control point.
      if (pimpl->movement->path.size() == 0 || !moveBestTarget()) {
        // Got stuck somewhere, can't find the path.
        moveDroidTo(this, pimpl->movement->destination.x, pimpl->movement->destination.y);
      }

      // See if the target point has been reached
      if (moveReachedWayPoint(this)) {
        // Got there - move onto the next waypoint
        if (!moveNextTarget()) {
          // No more waypoints - finish
          if (psPropStats->propulsionType == PROPULSION_TYPE::LIFT) {
            // check the location for vtols
            Vector2i tar = getPosition().xy();
            if (pimpl->order->type != ORDER_TYPE::PATROL &&
                pimpl->order->type != ORDER_TYPE::CIRCLE &&
                // Not doing an order which means we never land (which means we might want to land).
                pimpl->action != ACTION::MOVE_TO_REARM &&
                pimpl->action != ACTION::MOVE_TO_REARM_POINT
                && actionVTOLLandingPos(this, &tar) // Can find a sensible place to land.
                && map_coord(tar) != map_coord(pimpl->movement->destination))
              // We're not at the right place to land.
            {
              pimpl->movement->destination = tar;
              moveDroidTo(this, pimpl->movement->destination.x, pimpl->movement->destination.y);
            }
            else {
              pimpl->movement->status = HOVER;
            }
          }
          else {
            pimpl->movement->status = TURN;
          }
          objTrace(getId(), "Arrived at destination!");
          break;
        }
      }

      moveDir = moveGetDirection(this);
      moveSpeed = moveCalcDroidSpeed();

      if ((pimpl->movement->bumpTime != 0) &&
          (pimpl->movement->pauseTime + pimpl->movement->bumpTime + BLOCK_PAUSETIME < gameTime)) {
        if (pimpl->movement->status == POINT_TO_POINT) {
          pimpl->movement->status = PAUSE;
        }
        else {
          pimpl->movement->status = POINT_TO_POINT;
        }
        pimpl->movement->pauseTime = (UWORD)(gameTime - pimpl->movement->bumpTime);
      }

      if ((pimpl->movement->status == PAUSE) &&
          (pimpl->movement->bumpTime != 0) &&
          (pimpl->movement->lastBump > pimpl->movement->pauseTime) &&
          (pimpl->movement->lastBump + pimpl->movement->bumpTime + BLOCK_PAUSERELEASE < gameTime)) {
        pimpl->movement->status = POINT_TO_POINT;
      }

      break;
    case TURN:
      // Turn the droid to it's final facing
      if (psPropStats->propulsionType == PROPULSION_TYPE::LIFT) {
        pimpl->movement->status = POINT_TO_POINT;
      }
      else {
        pimpl->movement->status = INACTIVE;
      }
      break;
    case TURN_TO_TARGET:
      moveSpeed = 0;
      moveDir = iAtan2(pimpl->movement->target - getPosition().xy());
      break;
    case HOVER:
      moveDescending();
      break;

    default:
      ASSERT(false, "unknown move state");
      return;
      break;
  }

  // Update the movement model for the droid
  oldx = getPosition().x;
  oldy = getPosition().y;

  if (pimpl->type == DROID_TYPE::PERSON) {
    moveUpdatePersonModel(moveSpeed, moveDir);
  }
  else if (isCyborg(this)) {
    moveUpdateCyborgModel(moveSpeed, moveDir, oldStatus);
  }
  else if (psPropStats->propulsionType == PROPULSION_TYPE::LIFT) {
    moveUpdateVtolModel(moveSpeed, moveDir);
  }
  else {
    moveUpdateGroundModel(moveSpeed, moveDir);
  }

  if (map_coord(oldx) != map_coord(getPosition().x)
      || map_coord(oldy) != map_coord(getPosition().y)) {
    visTilesUpdate(this);

    // object moved from one tile to next, check to see if droid is near stuff.(oil)
    checkLocalFeatures(this);

    triggerEventDroidMoved(this, oldx, oldy);
  }

  // See if it's got blocked
  if (psPropStats->propulsionType != PROPULSION_TYPE::LIFT &&
      moveBlocked()) {
    objTrace(getId(), "status: id %d blocked", getId());
    pimpl->movement->status = TURN;
  }

  /* If it's sitting in water then it's got to go with the flow! */
  if (worldOnMap(getPosition().x, getPosition().y) && terrainType(
          mapTile(map_coord(getPosition().x), map_coord(getPosition().y))) == TER_WATER) {
    updateDroidOrientation();
  }

  if (pimpl->movement->status == TURN_TO_TARGET &&
      getRotation().direction == moveDir) {
    if (psPropStats->propulsionType == PROPULSION_TYPE::LIFT) {
      pimpl->movement->status = POINT_TO_POINT;
    }
    else {
      pimpl->movement->status = INACTIVE;
    }
    objTrace(getId(), "MOVETURNTOTARGET complete");
  }

  if (damageManager->getPeriodicalDamageStartTime() != 0 &&
      pimpl->type != DROID_TYPE::PERSON &&
      isVisibleToSelectedPlayer()) {
    // display-only check for adding effect
    pos.x = getPosition().x + (18 - rand() % 36);
    pos.z = getPosition().y + (18 - rand() % 36);
    pos.y = getPosition().z + (getDisplayData()->imd_shape->max.y / 3);

    addEffect(&pos, EFFECT_GROUP::EXPLOSION,
              EFFECT_TYPE::EXPLOSION_TYPE_SMALL,
              false, nullptr, 0,
              gameTime - deltaGameTime + 1);
  }

  movePlayAudio(bStarted, bStopped, moveSpeed);
  ASSERT(droidOnMap(this), "%s moved off map (%u, %u)->(%u, %u)",
         droidGetName(this), oldx, oldy,
         (unsigned)getPosition().x, (unsigned)getPosition().y);
}
  
void Droid::moveUpdateCyborgModel(int moveSpeed, uint16_t moveDir, MOVE_STATUS oldStatus)
{
  if (!pimpl) return;
  // nothing to do if the droid is stopped
  if (moveDroidStopped(this, moveSpeed)) {
    if (pimpl->animationEvent == ANIM_EVENT_ACTIVE) {
      pimpl->timeAnimationStarted = 0;
      pimpl->animationEvent = ANIM_EVENT_NONE;
    }
    return;
  }

  if (pimpl->animationEvent == ANIM_EVENT_NONE) {
    pimpl->timeAnimationStarted = gameTime;
    pimpl->animationEvent = ANIM_EVENT_ACTIVE;
  }

  /* use baba person movement */
  moveUpdatePersonModel(moveSpeed, moveDir);

  setRotation({getRotation().direction, 0, 0});
}
  
// see if a droid has run into another droid
// Only consider stationery droids
void Droid::moveCalcDroidSlide(int* pmx, int* pmy)
{
  if (!pimpl) return;
  bool bLegs = false;
  if (pimpl->type == DROID_TYPE::PERSON || isCyborg(this)) {
    bLegs = true;
  }
  auto spmx = gameTimeAdjustedAverage(*pmx, EXTRA_PRECISION);
  auto spmy = gameTimeAdjustedAverage(*pmy, EXTRA_PRECISION);

  auto droidR = objRadius();
  BaseObject* psObst = nullptr;
  static GridList gridList; // static to avoid allocations.
  gridList = gridStartIterate(getPosition().x, getPosition().y, OBJ_MAXRADIUS);
  for (auto psObj : gridList)
  {
    if (auto psObjcast = dynamic_cast<Droid*>(psObj)) {
      auto objR = psObj->objRadius();
      if (isTransporter(*psObjcast)) {
        // ignore transporters
        continue;
      }
      if ((!isFlying() && psObjcast->isFlying() && psObjcast->getPosition().z > (getPosition().z + droidR)) ||
          (!psObjcast->isFlying() && isFlying() && getPosition().z > (psObjcast->getPosition().z + objR))) {
        // ground unit can't bump into a flying saucer..
        continue;
      }
      if (!bLegs && (psObjcast)->getType() == DROID_TYPE::PERSON) {
        // everything else doesn't avoid people
        continue;
      }
      if (psObjcast->playerManager->getPlayer() == playerManager->getPlayer()
          && pimpl->lastFrustratedTime > 0
          && gameTime - pimpl->lastFrustratedTime < FRUSTRATED_TIME) {
        continue; // clip straight through own units when sufficient frustrated -- using cheat codes!
      }
    }
    else {
      // ignore anything that isn't a droid
      continue;
    }

    auto objR = psObj->objRadius();
    auto rad = droidR + objR;
    auto radSq = rad * rad;

    auto xdiff = getPosition().x + spmx - psObj->getPosition().x;
    auto ydiff = getPosition().y + spmy - psObj->getPosition().y;
    auto distSq = xdiff * xdiff + ydiff * ydiff;
    if (xdiff * spmx + ydiff * spmy >= 0) {
      // object behind
      continue;
    }

    if (radSq <= distSq) {
      continue;
    }
    if (psObst != nullptr) {
      // hit more than one droid - stop
      *pmx = 0;
      *pmy = 0;
      psObst = nullptr;
      break;
    }
    else {
      psObst = psObj;

      // note the bump time and position if necessary
      if (pimpl->movement->bumpTime == 0) {
        pimpl->movement->bumpTime = gameTime;
        pimpl->movement->lastBump = 0;
        pimpl->movement->pauseTime = 0;
        pimpl->movement->bumpPos = getPosition();
        pimpl->movement->bumpDir = getRotation().direction;
      }
      else {
        pimpl->movement->lastBump = gameTime - pimpl->movement->bumpTime;
      }

      // tell inactive droids to get out the way
      auto psShuffleDroid = dynamic_cast<Droid*>(psObst);
      if (psObst && psShuffleDroid) {

        if (aiCheckAlliances(psObst->playerManager->getPlayer(), playerManager->getPlayer())
            && psShuffleDroid->getAction() != ACTION::WAIT_DURING_REARM
            && psShuffleDroid->getMovementData()->status == MOVE_STATUS::INACTIVE) {
          psShuffleDroid->moveShuffleDroid(getMovementData()->target - getPosition().xy());
        }
      }
    }
  }

  if (psObst != nullptr) {
    // Try to slide round it
    moveCalcSlideVector(this, psObst->getPosition().x, psObst->getPosition().y, pmx, pmy);
  }
}
    
/* primitive 'bang-bang' vtol height controller */
void Droid::moveAdjustVtolHeight(int iMapHeight)
{
  if (!pimpl) return;
  int iMinHeight, iMaxHeight, iLevelHeight;
  if (isTransporter(*this) && !bMultiPlayer) {
    iMinHeight = 2 * VTOL_HEIGHT_MIN;
    iLevelHeight = 2 * VTOL_HEIGHT_LEVEL;
    iMaxHeight = 2 * VTOL_HEIGHT_MAX;
  }
  else {
    iMinHeight = VTOL_HEIGHT_MIN;
    iLevelHeight = VTOL_HEIGHT_LEVEL;
    iMaxHeight = VTOL_HEIGHT_MAX;
  }

  if (getPosition().z >= (iMapHeight + iMaxHeight)) {
    pimpl->movement->vertical_speed = (SWORD)- VTOL_VERTICAL_SPEED;
  }
  else if (getPosition().z < (iMapHeight + iMinHeight)) {
    pimpl->movement->vertical_speed = (SWORD)VTOL_VERTICAL_SPEED;
  }
  else if ((getPosition().z < iLevelHeight) &&
           (pimpl->movement->vertical_speed < 0)) {
    pimpl->movement->vertical_speed = 0;
  }
  else if ((getPosition().z > iLevelHeight) &&
           (pimpl->movement->vertical_speed > 0)) {
    pimpl->movement->vertical_speed = 0;
  }
}

void Droid::moveDescending()
{
  if (!pimpl) return;
  auto iMapHeight = map_Height(getPosition().x, getPosition().y);

  pimpl->movement->speed = 0;

  if (getPosition().z > iMapHeight) {
    /* descending */
    pimpl->movement->vertical_speed = (SWORD)- VTOL_VERTICAL_SPEED;
  }
  else {
    /* on floor - stop */
    setPosition({getPosition().x, getPosition().y, iMapHeight});
    pimpl->movement->vertical_speed = 0;

    /* reset move state */
    pimpl->movement->status = MOVE_STATUS::INACTIVE;

    /* conform to terrain */
    updateDroidOrientation();
  }
}

void Droid::movePlayAudio(bool bStarted, bool bStoppedBefore, int iMoveSpeed)
{
  Propulsion* psPropType;
  bool bStoppedNow;
  int iAudioID = NO_SOUND;
  AUDIO_CALLBACK pAudioCallback = nullptr;

  /* get prop stats */
  auto psPropStats = dynamic_cast<PropulsionStats const*>(getComponent(COMPONENT_TYPE::PROPULSION));
  ASSERT_OR_RETURN(, psPropStats != nullptr, "Invalid propulsion stats pointer");
  auto propType = psPropStats->propulsionType;

  /* get current droid motion status */
  bStoppedNow = moveDroidStopped(this, iMoveSpeed);

  if (bStarted) {
    /* play start audio */
    if ((propType == PROPULSION_TYPE::WHEELED &&
         getType() != DROID_TYPE::CONSTRUCT) ||
        psPropType->startID == NO_SOUND) {
      movePlayDroidMoveAudio();
      return;
    }
    else if (isTransporter(*this)) {
      iAudioID = ID_SOUND_BLIMP_TAKE_OFF;
    }
    else {
      iAudioID = psPropType->startID;
    }

    pAudioCallback = moveDroidStartCallback;
  }
  else if (!bStoppedBefore && bStoppedNow &&
           (psPropType->shutDownID != NO_SOUND)) {
    /* play stop audio */
    if (isTransporter(*this)) {
      iAudioID = ID_SOUND_BLIMP_LAND;
    }
    else if (propType != PROPULSION_TYPE::WHEELED ||
             getType() == DROID_TYPE::CONSTRUCT) {
      iAudioID = psPropType->shutDownID;
    }
  }
  else if (!bStoppedBefore && !bStoppedNow && iAudioID == NO_SOUND) {
    /* play move audio */
    movePlayDroidMoveAudio();
    return;
  }

  if ((iAudioID != NO_SOUND) &&
      (isVisibleToSelectedPlayer())) {
    if (audio_PlayObjDynamicTrack(this, iAudioID,
                                  pAudioCallback)) {
      pimpl->iAudioID = iAudioID;
    }
  }
}

// Returns -1 - distance if the direct path to the waypoint is blocked, otherwise returns the distance to the waypoint.
int Droid::moveDirectPathToWaypoint(unsigned positionIndex)
{
  if (!pimpl) return -1;
  Vector2i src(getPosition().xy());
  Vector2i dst = pimpl->movement->path[positionIndex];
  Vector2i delta = dst - src;
  int32_t dist = iHypot(delta);
  BLOCKING_CALLBACK_DATA data;
  data.propulsionType = dynamic_cast<PropulsionStats const*>(getComponent(COMPONENT_TYPE::PROPULSION))->propulsionType;
  data.blocking = false;
  data.src = src;
  data.dst = dst;
  rayCast(src, dst, &moveBlockingTileCallback, &data);
  return data.blocking ? -1 - dist : dist;
}

/* Get pitch and roll from direction and tile data */
void Droid::updateDroidOrientation()
{
  const int d = 20;

  if (getType() == DROID_TYPE::PERSON || isCyborg(this) ||
      isTransporter(*this) || isFlying()) {
    /* The ground doesn't affect the pitch/roll of these droids*/
    return;
  }

  // Find the height of 4 points around the droid.
  //    hy0
  // hx0 * hx1      (* = droid)
  //    hy1
  auto hx1 = map_Height(getPosition().x + d, getPosition().y);
  auto hx0 = map_Height(MAX(0, getPosition().x - d), getPosition().y);
  auto hy1 = map_Height(getPosition().x, getPosition().y + d);
  auto hy0 = map_Height(getPosition().x, MAX(0, getPosition().y - d));

  //update height in case were in the bottom of a trough
  auto z = MAX(getPosition().z, (hy0 + hy1) / 2);
  setPosition({getPosition().x, getPosition().y, z});

  // Vector of length 65536 pointing in direction droid is facing.
  auto vX = iSin(getRotation().direction);
  auto vY = iCos(getRotation().direction);

  // Calculate pitch of ground.
  auto dzdx = hx1 - hx0; // 2*d*∂z(x, y)/∂x       of ground
  auto dzdy = hy1 - hy0; // 2*d*∂z(x, y)/∂y       of ground
  auto dzdv = dzdx * vX + dzdy * vY; // 2*d*∂z(x, y)/∂v << 16 of ground, where v is the direction the droid is facing.
  auto newPitch = iAtan2(dzdv, (2 * d) << 16); // pitch = atan(∂z(x, y)/∂v)/2π << 16

  auto deltaPitch = angleDelta(newPitch - getRotation().pitch);

  // Limit the rate the front comes down to simulate momentum
  auto pitchLimit = gameTimeAdjustedIncrement(DEG(PITCH_LIMIT));
  deltaPitch = MAX(deltaPitch, -pitchLimit);

  // Update pitch.
  auto pitch = getRotation().pitch + deltaPitch;

  // Calculate and update roll of ground (not taking pitch into account, but good enough).
  auto dzdw = dzdx * vY - dzdy * vX;
  // 2*d*∂z(x, y)/∂w << 16 of ground, where w is at right angles to the direction the droid is facing.
  auto roll = iAtan2(dzdw, (2 * d) << 16); // pitch = atan(∂z(x, y)/∂w)/2π << 16
  setRotation({getRotation().direction, pitch, roll});
}

/** This function actually tells the droid to perform the psOrder.
 * This function is called everytime to send a direct order to a droid.
 */
void Droid::orderDroidBase(Order* psOrder)
{
  if (!pimpl) return;
  unsigned iFactoryDistSq;
  Structure* psFactory;
  auto psPropStats = dynamic_cast<PropulsionStats const*>(getComponent(COMPONENT_TYPE::PROPULSION));
  const Vector3i rPos(psOrder->pos, 0);
  syncDebug("%d ordered %s", getId(),
            getDroidOrderName(psOrder->type).c_str());
  objTrace(getId(), "base set order to %s (was %s)",
           getDroidOrderName(psOrder->type).c_str(),
           getDroidOrderName(pimpl->order->type).c_str());

  using enum ORDER_TYPE;
  if (psOrder->type != TRANSPORT_IN // transporters special
      && psOrder->target == nullptr // location-type order
      && (validOrderForLoc(psOrder->type) || psOrder->type == BUILD)
      && !fpathCheck(getPosition(), rPos, psPropStats->propulsionType)) {
    if (!isHumanPlayer(playerManager->getPlayer())) {
      debug(LOG_SCRIPT, "Invalid order %s given to player %d's %s for position (%d, %d) - ignoring",
            getDroidOrderName(psOrder->type).c_str(), playerManager->getPlayer(),
            droidGetName(this), psOrder->pos.x, psOrder->pos.y);
    }
    objTrace(getId(), "Invalid order %s for position (%d, %d) - ignoring",
             getDroidOrderName(psOrder->type).c_str(),
             psOrder->pos.x, psOrder->pos.y);
    return;
  }

  // deal with a droid receiving a primary order
  if (!isTransporter(*this) && psOrder->type != NONE &&
      psOrder->type != STOP && psOrder->type != GUARD) {

    // reset secondary order
    const unsigned oldState = pimpl->secondaryOrder;
    pimpl->secondaryOrder &= ~(DSS_RTL_MASK | DSS_RECYCLE_MASK | DSS_PATROL_MASK);
    pimpl->secondaryOrderPending &= ~(DSS_RTL_MASK | DSS_RECYCLE_MASK | DSS_PATROL_MASK);
    objTrace(getId(), "secondary order reset due to primary order set");

    if (oldState != pimpl->secondaryOrder && playerManager->getPlayer() == selectedPlayer) {
      intRefreshScreen();
    }
  }

  // if this is a command droid - all it's units do the same thing
  if ((pimpl->type == DROID_TYPE::COMMAND) &&
      (pimpl->group != nullptr) &&
      (pimpl->group->isCommandGroup()) &&
      (psOrder->type != GUARD) && //(psOrder->psObj == NULL)) &&
      (psOrder->type != RETURN_TO_REPAIR) &&
      (psOrder->type != RECYCLE)) {

    if (psOrder->type == ATTACK) {
      // change to attack target so that the group members
      // guard order does not get canceled
      psOrder->type = ATTACK_TARGET;
      orderCmdGroupBase(pimpl->group, psOrder);
      psOrder->type = ATTACK;
    }
    else {
      orderCmdGroupBase(pimpl->group, psOrder);
    }

    // the commander doesn't have to pick up artifacts, one
    // of his units will do it for him (if there are any in his group).
    if ((psOrder->type == RECOVER) &&
        (!pimpl->group->getMembers()->empty())) {
      psOrder->type = NONE;
    }
  }

  // A selected campaign transporter shouldn't be given orders by the player.
  // Campaign transporter selection is required for it to be tracked by the camera, and
  // should be the only case when it does get selected.
  if (isTransporter(*this) && !bMultiPlayer &&
      damageManager->isSelected() && psOrder->type != TRANSPORT_OUT &&
      psOrder->type != TRANSPORT_IN && psOrder->type != TRANSPORT_RETURN) {
    return;
  }

  switch (psOrder->type) {
    case NONE:
      // used when choose order cannot assign an order
      break;
    case STOP:
      // get the droid to stop doing whatever it is doing
      newAction(this, ACTION::NONE);
      pimpl->order = std::make_unique<Order>(Order(NONE));
      break;
    case HOLD:
      // get the droid to stop doing whatever it is doing and temp hold
      newAction(this, ACTION::NONE);
      pimpl->order = std::make_unique<Order>(*psOrder);
      break;
    case MOVE:
    case SCOUT:
      // can't move vtols to blocking tiles
      if (isVtol() &&
          fpathBlockingTile(map_coord(psOrder->pos),
                            psPropStats->propulsionType)) {
        break;
      }
      //in multiPlayer, cannot move Transporter to blocking tile either
      if (game.type == LEVEL_TYPE::SKIRMISH
          && isTransporter(*this)
          && fpathBlockingTile(map_coord(psOrder->pos), psPropStats->propulsionType)) {
        break;
      }
      // move a droid to a location
      pimpl->order = std::make_unique<Order>(*psOrder);
      newAction(this, ACTION::MOVE, psOrder->pos);
      break;
    case PATROL:
      pimpl->order = std::make_unique<Order>(*psOrder);
      pimpl->order->pos2 = getPosition().xy();
      newAction(this, ACTION::MOVE, psOrder->pos);
      break;
    case RECOVER:
      pimpl->order = std::make_unique<Order>(*psOrder);
      newAction(this, ACTION::MOVE,
                {psOrder->target->getPosition().x,
                 psOrder->target->getPosition().y});
      break;
    case TRANSPORT_OUT:
      // tell a (transporter) droid to leave home base for the offworld mission
      pimpl->order = std::make_unique<Order>(*psOrder);
      newAction(this, ACTION::TRANSPORT_OUT, psOrder->pos);
      break;
    case TRANSPORT_RETURN:
      // tell a (transporter) droid to return after unloading
      pimpl->order = std::make_unique<Order>(*psOrder);
      newAction(this, ACTION::TRANSPORT_OUT, psOrder->pos);
      break;
    case TRANSPORT_IN:
      // tell a (transporter) droid to fly onworld
      pimpl->order = std::make_unique<Order>(*psOrder);
      newAction(this, ACTION::TRANSPORT_IN, psOrder->pos);
      break;
    case ATTACK:
    case ATTACK_TARGET:
      if (numWeapons(*this) == 0 || isTransporter(*this)) {
        break;
      }
      else if (pimpl->order->type == GUARD && psOrder->type == ATTACK_TARGET) {
        // attacking something while guarding, don't change the order
        newAction(this, ACTION::ATTACK, psOrder->target);
      }
      else if (psOrder->target && !dynamic_cast<Health *>(psOrder->target)->isDead()) {
        //cannot attack a Transporter with EW in multiPlayer
        // FIXME: Why not ?
        if (game.type == LEVEL_TYPE::SKIRMISH &&
            hasElectronicWeapon() &&
            dynamic_cast<Droid*>(psOrder->target) &&
            isTransporter(*dynamic_cast<Droid*>(psOrder->target))) {
          break;
        }
        pimpl->order = std::make_unique<Order>(*psOrder);

        if (isVtol() || withinRange(this, psOrder->target, 0) ||
            ((psOrder->type == ATTACK_TARGET ||
              psOrder->type == ATTACK) &&
              secondaryGetState(SECONDARY_ORDER::HALT_TYPE) == DSS_HALT_HOLD)) {
          // when DSS_HALT_HOLD, don't move to attack
          newAction(this, ACTION::ATTACK, psOrder->target);
        }
        else {
          newAction(this, ACTION::MOVE,
                    {psOrder->target->getPosition().x,
                     psOrder->target->getPosition().y});
        }
      }
      break;
    case BUILD:
    case LINE_BUILD:
      // build a new structure or line of structures
      ASSERT_OR_RETURN(, isConstructionDroid(this), "%s cannot construct things!", objInfo(this));
      ASSERT_OR_RETURN(, psOrder->structure_stats != nullptr, "invalid structure stats pointer");
      pimpl->order = std::make_unique<Order>(*psOrder);
      ASSERT_OR_RETURN(, !pimpl->order->structure_stats ||
                       pimpl->order->structure_stats->type != STRUCTURE_TYPE::DEMOLISH,
                       "Cannot build demolition");
      newAction(this, ACTION::BUILD, psOrder->pos);
      objTrace(getId(), "Starting new construction effort of %s",
               psOrder->structure_stats ? getStatsName(psOrder->structure_stats) : "NULL");
      break;
    case BUILD_MODULE:
      //build a module onto the structure
      if (!isConstructionDroid(this) ||
          psOrder->index < nextModuleToBuild(
                  dynamic_cast<Structure*>(psOrder->target),
                  -1)) {
        break;
      }
      pimpl->order = std::make_unique<Order>(
                BUILD,
                *getModuleStat(dynamic_cast<Structure*>(psOrder->target)),
                psOrder->target->getPosition().xy(), 0);

      ASSERT_OR_RETURN(, pimpl->order->structure_stats != nullptr, "should have found a module stats");
      ASSERT_OR_RETURN(, !pimpl->order->structure_stats || pimpl->order->structure_stats->type != STRUCTURE_TYPE::DEMOLISH,
                         "Cannot build demolition");
      newAction(this, ACTION::BUILD,
                {psOrder->target->getPosition().x,
                 psOrder->target->getPosition().y});

      objTrace(getId(), "Starting new upgrade of %s", psOrder->structure_stats
        ? getStatsName(psOrder->structure_stats)
        : "NULL");
      break;
    case HELP_BUILD:
      // help to build a structure that is starting to be built
      ASSERT_OR_RETURN(, isConstructionDroid(this), "Not a constructor droid");
      ASSERT_OR_RETURN(, psOrder->target != nullptr, "Help to build a NULL pointer?");
      if (pimpl->action == ACTION::BUILD && psOrder->target == pimpl->actionTargets[0]
          // skip LINEBUILD -> we still want to drop pending structure blueprints
          // this isn't a perfect solution, because ordering a LINEBUILD with negative energy, and then clicking
          // on first structure being built, will remove it, as we change order from DORDR_LINEBUILD to BUILD
          && (pimpl->order->type != LINE_BUILD)) {
        // we are already building it, nothing to do
        objTrace(getId(), "Ignoring HELPBUILD because already building object %i", psOrder->target->getId());
        break;
      }
      pimpl->order = std::make_unique<Order>(*psOrder);
      pimpl->order->pos = psOrder->target->getPosition().xy();
      pimpl->order->structure_stats = std::make_shared<StructureStats>(*dynamic_cast<Structure*>(psOrder->target)->getStats());

      ASSERT_OR_RETURN(, !pimpl->order->structure_stats ||
                         pimpl->order->structure_stats->type != STRUCTURE_TYPE::DEMOLISH,
                         "Cannot build demolition");
      ::newAction(this, ACTION::BUILD, pimpl->order->pos.x, pimpl->order->pos.y);
      objTrace(getId(), "Helping construction of %s",
               psOrder->structure_stats ? getStatsName(pimpl->order->structure_stats) : "NULL");
      break;
    case DEMOLISH:
      if (!(pimpl->type == DROID_TYPE::CONSTRUCT ||
            pimpl->type == DROID_TYPE::CYBORG_CONSTRUCT)) {
        break;
      }
      pimpl->order = std::make_unique<Order>(*psOrder);
      pimpl->order->pos = psOrder->target->getPosition().xy();
      newAction(this, ACTION::DEMOLISH, psOrder->target);
      break;
    case REPAIR:
      if (!(pimpl->type == DROID_TYPE::CONSTRUCT ||
            pimpl->type == DROID_TYPE::CYBORG_CONSTRUCT)) {
        break;
      }
      pimpl->order = std::make_unique<Order>(*psOrder);
      pimpl->order->pos = psOrder->target->getPosition().xy();
      newAction(this, ACTION::REPAIR, psOrder->target);
      break;
    case DROID_REPAIR:
      if (!(pimpl->type == DROID_TYPE::REPAIRER ||
            pimpl->type == DROID_TYPE::CYBORG_REPAIR)) {
        break;
      }
      pimpl->order = std::make_unique<Order>(*psOrder);
      newAction(this, ACTION::DROID_REPAIR, psOrder->target);
      break;
    case OBSERVE:
      // keep an object within sensor view
      pimpl->order = std::make_unique<Order>(*psOrder);
      newAction(this, ACTION::OBSERVE, psOrder->target);
      break;
    case FIRE_SUPPORT:
      if (isTransporter(*this)) {
        debug(LOG_ERROR, "Sorry, transports cannot be assigned to commanders.");
        pimpl->order = std::make_unique<Order>(NONE);
        break;
      }
      pimpl->order = std::make_unique<Order>(*psOrder);
      // let the order update deal with vtol droids
      if (!isVtol()) {
        newAction(this, ACTION::FIRE_SUPPORT, psOrder->target);
      }

      if (playerManager->getPlayer() == selectedPlayer) {
        orderPlayFireSupportAudio(psOrder->target);
      }
      break;
    case COMMANDER_SUPPORT:
      if (isTransporter(*this)) {
        debug(LOG_ERROR, "Sorry, transports cannot be assigned to commanders.");
        pimpl->order = std::make_unique<Order>(NONE);
        break;
      }
        ASSERT_OR_RETURN(, psOrder->target != nullptr, "Can't command a NULL");
      if (cmdDroidAddDroid(dynamic_cast<Droid*>(psOrder->target), this) &&
          playerManager->getPlayer() == selectedPlayer) {
        orderPlayFireSupportAudio(psOrder->target);
      }
      else if (playerManager->getPlayer() == selectedPlayer) {
        audio_PlayBuildFailedOnce();
      }
      break;
    case RETURN_TO_BASE:
      for (auto& psStruct : playerList[playerManager->getPlayer()].structures)
      {
        if (psStruct.getStats()->type == STRUCTURE_TYPE::HQ) {
          auto pos = psStruct.getPosition().xy();

          pimpl->order = std::make_unique<Order>(*psOrder);
          // find a place to land for vtols (and transporters in a multiplayer game)
          if (isVtol() || (game.type == LEVEL_TYPE::SKIRMISH && isTransporter(*this))) {
            actionVTOLLandingPos(this, &pos);
          }
          newAction(this, ACTION::MOVE, pos.x, pos.y);
          break;
        }
      }
      // no HQ so go to the landing zone
      if (pimpl->order->type != RETURN_TO_BASE) {
        // see if the LZ has been set up
        auto iDX = getLandingX(playerManager->getPlayer());
        auto iDY = getLandingY(playerManager->getPlayer());

        if (iDX && iDY) {
          pimpl->order = std::make_unique<Order>(*psOrder);
          newAction(this, ACTION::MOVE, {iDX, iDY});
        }
        else {
          // haven't got an LZ set up so don't do anything
          newAction(this, ACTION::NONE);
          pimpl->order = std::make_unique<Order>(NONE);
        }
      }
      break;
    case RETURN_TO_REPAIR:
    case RTR_SPECIFIED:
    {
      if (isVtol()) {
        moveToRearm();
        break;
      }
      // if already has a target repair, don't override it: it might be different
      // and we don't want come back and forth between 2 repair points
      if (pimpl->order->type == RETURN_TO_REPAIR && psOrder->target &&
          !dynamic_cast<Health *>(psOrder->target)->isDead()) {
        objTrace(getId(), "DONE FOR NOW");
        break;
      }
      RtrBestResult rtrData;
      if (psOrder->rtrType == RTR_DATA_TYPE::NO_RESULT ||
          !psOrder->target) {
        rtrData = decideWhereToRepairAndBalance();
      }
      else {
        rtrData = RtrBestResult(*psOrder);
      }

      /* give repair order if repair facility found */
      if (rtrData.type == RTR_DATA_TYPE::REPAIR_FACILITY) {
        /* move to front of structure */
        pimpl->order = std::make_unique<Order>(psOrder->type, *rtrData.target, RTR_DATA_TYPE::REPAIR_FACILITY);
        pimpl->order->pos = rtrData.target->getPosition().xy();
        /* If in multiPlayer, and the Transporter has been sent to be
          * repaired, need to find a suitable location to drop down. */
        if (game.type == LEVEL_TYPE::SKIRMISH && isTransporter(*this)) {
          Vector2i pos = pimpl->order->pos;

          objTrace(getId(), "Repair transport");
          actionVTOLLandingPos(this, &pos);
          newAction(this, ACTION::MOVE, pos.x, pos.y);
        }
        else {
          objTrace(getId(), "Go to repair facility at (%d, %d) using (%d, %d)!", rtrData.target->getPosition().x,
                   rtrData.target->getPosition().y, pimpl->order->pos.x, pimpl->order->pos.y);
          newAction(this, ACTION::MOVE, rtrData.target, pimpl->order->pos.x, pimpl->order->pos.y);
        }
      }
        /* give repair order if repair droid found */
      else if (rtrData.type == RTR_DATA_TYPE::DROID && !isTransporter(*this)) {
        pimpl->order = std::make_unique<Order>(psOrder->type,
                                        Vector2i(rtrData.target->getPosition().x,
                                                         rtrData.target->getPosition().y),
                                RTR_DATA_TYPE::DROID);
        pimpl->order->pos = rtrData.target->getPosition().xy();
        pimpl->order->target = rtrData.target;
        objTrace(getId(), "Go to repair at (%d, %d) using (%d, %d), time %i!", rtrData.target->getPosition().x,
                 rtrData.target->getPosition().y, pimpl->order->pos.x, pimpl->order->pos.y, gameTime);
        newAction(this, ACTION::MOVE, pimpl->order->pos.x, pimpl->order->pos.y);
      }
      else {
        // no repair facility or HQ go to the landing zone
        if (!bMultiPlayer && selectedPlayer == 0) {
          objTrace(getId(), "could not RTR, doing RTL instead");
          orderDroid(this, RETURN_TO_BASE, ModeImmediate);
        }
      }
    }
      break;
    case EMBARK:
    {
      auto embarkee = dynamic_cast<Droid*>(psOrder->target);
      if (isTransporter(*this) // require a transporter for embarking.
          || embarkee == nullptr || !isTransporter(*embarkee)) // nor can a transporter load another transporter
      {
        debug(LOG_ERROR, "Sorry, can only load things that aren't transporters into things that are.");
        pimpl->order = std::make_unique<Order>(NONE);
        break;
      }
      // move the droid to the transporter location
      pimpl->order = std::make_unique<Order>(*psOrder);
      pimpl->order->pos = psOrder->target->getPosition().xy();
      newAction(this, ACTION::MOVE,
                psOrder->target->getPosition().x,
                psOrder->target->getPosition().y);
      break;
    }
    case DISEMBARK:
      //only valid in multiPlayer mode
      if (bMultiPlayer) {
        //this order can only be given to Transporter droids
        if (isTransporter(*this)) {
          pimpl->order = std::make_unique<Order>(*psOrder);
          //move the Transporter to the requested location
          newAction(this, ACTION::MOVE, psOrder->pos.x, psOrder->pos.y);
          //close the Transporter interface - if up
          if (widgGetFromID(psWScreen, IDTRANS_FORM) != nullptr) {
            intRemoveTrans();
          }
        }
      }
      break;
    case RECYCLE:
      psFactory = nullptr;
      iFactoryDistSq = 0;
      for (auto& psStruct : playerList[playerManager->getPlayer()].structures)
      {
        using enum STRUCTURE_TYPE;
        // Look for nearest factory or repair facility
        if (psStruct.getStats()->type == FACTORY ||
            psStruct.getStats()->type == CYBORG_FACTORY ||
            psStruct.getStats()->type == VTOL_FACTORY ||
            psStruct.getStats()->type == REPAIR_FACILITY) {
          /* get droid->facility distance squared */
          auto iStructDistSq = droidSqDist(this, &psStruct);

          /* Choose current structure if first facility found or nearer than previously chosen facility */
          if (psStruct.getState() == STRUCTURE_STATE::BUILT &&
              iStructDistSq > 0 &&
              (psFactory == nullptr ||
               iFactoryDistSq > iStructDistSq)) {
            psFactory = &psStruct;
            iFactoryDistSq = iStructDistSq;
          }
        }
      }

      /* give recycle order if facility found */
      if (psFactory != nullptr) {
        /* move to front of structure */
        pimpl->order = std::make_unique<Order>(psOrder->type, *psFactory);
        pimpl->order->pos = psFactory->getPosition().xy();
        setTarget(psFactory);
        newAction(this, ACTION::MOVE, psFactory, pimpl->order->pos.x, pimpl->order->pos.y);
      }
      break;
    case GUARD:
      pimpl->order = std::make_unique<Order>(*psOrder);
      if (psOrder->target != nullptr) {
        pimpl->order->pos = psOrder->target->getPosition().xy();
      }
      newAction(this, ACTION::NONE);
      break;
    case RESTORE:
      if (!hasElectronicWeapon()) {
        break;
      }
      if (!dynamic_cast<Structure*>(psOrder->target)) {
        ASSERT(false, "orderDroidBase: invalid object type for Restore order");
        break;
      }
      pimpl->order = std::make_unique<Order>(*psOrder);
      pimpl->order->pos = psOrder->target->getPosition().xy();
      newAction(this, ACTION::RESTORE, psOrder->target);
      break;
    case REARM:
      // didn't get executed before
      if (!vtolRearming(*this)) {
        pimpl->order = std::make_unique<Order>(*psOrder);
        newAction(this, ACTION::MOVE_TO_REARM, psOrder->target);
        assignVTOLPad(this, dynamic_cast<Structure*>(psOrder->target));
      }
      break;
    case CIRCLE:
      if (!isVtol()) {
        break;
      }
      pimpl->order = std::make_unique<Order>(*psOrder);
      newAction(this, ACTION::MOVE, psOrder->pos.x, psOrder->pos.y);
      break;
    default:
      ASSERT(false, "orderUnitBase: unknown order");
      break;
  }
}

bool Droid::isRepairDroid() const noexcept
{
  return getType() == DROID_TYPE::REPAIRER || getType() == DROID_TYPE::CYBORG_REPAIR;
}

bool Droid::tryDoRepairlikeAction()
{
  if (!pimpl) return false;
  if (isRepairLikeAction(pimpl->action)) return true; // Already doing something.

  switch (pimpl->type) {
    using enum DROID_TYPE;
    case REPAIRER:
    case CYBORG_REPAIR:
      //repair droids default to repairing droids within a given range
      if (auto repairTarget = checkForRepairRange(this)) {
        newAction(this, ACTION::DROID_REPAIR, repairTarget);
      }
      break;
    case CONSTRUCT:
    case CYBORG_CONSTRUCT:
    {
      //construct droids default to repairing and helping structures within a given range
      auto damaged = checkForDamagedStruct(this);
      if (damaged.second == ACTION::REPAIR) {
        newAction(this, damaged.second, damaged.first);
      }
      else if (damaged.second == ACTION::BUILD) {
        pimpl->order->structure_stats = std::make_shared<StructureStats>(*damaged.first->getStats());
        pimpl->order->direction = damaged.first->getRotation().direction;
        newAction(this, damaged.second, damaged.first->getPosition().x, damaged.first->getPosition().y);
      }
      break;
    }
    default:
      return false;
  }
  return true;
}

//Builds an instance of a Droid - the x/y passed in are in world coords.
std::unique_ptr<Droid> Droid::reallyBuildDroid(const DroidTemplate* pTemplate,
                                                 Position pos, unsigned player,
                                                 bool onMission, Rotation rot)
{
  // Don't use this assertion in single player, since droids can finish building while on an away mission
  ASSERT(!bMultiPlayer || worldOnMap(pos.x, pos.y),
         "the build locations are not on the map");

  ASSERT_OR_RETURN(nullptr, player < MAX_PLAYERS,
                   "Invalid player: %" PRIu32 "", player);

  auto psDroid = std::make_unique<Droid>(
          generateSynchronisedObjectId(), &playerList[player]);

  droidSetName(psDroid.get(), getStatsName(pTemplate));

  // Set the droids type
  psDroid->pimpl->type = droidTemplateType(pTemplate); // Is set again later to the same thing, in droidSetBits.
  psDroid->setPosition(pos);
  psDroid->setRotation(rot);

  // don't worry if not on homebase cos not being drawn yet
  if (!onMission) {
    //set droid height
    auto z = map_Height(psDroid->getPosition().x, psDroid->getPosition().y);
    setPosition({getPosition().x, getPosition().y, z});
  }

  if (isTransporter(*psDroid) ||
      psDroid->getType() == DROID_TYPE::COMMAND) {
    auto psGrp = addGroup(-1);
    psGrp->addDroid(psDroid.get());
  }

  // find the highest stored experience
  // unless game time is stopped, then we're hopefully loading a game and
  // don't want to use up recycled experience for the droids we just loaded
  if (!gameTimeIsStopped() &&
      psDroid->getType() != DROID_TYPE::CONSTRUCT &&
      psDroid->getType() != DROID_TYPE::CYBORG_CONSTRUCT &&
      psDroid->getType() != DROID_TYPE::REPAIRER &&
      psDroid->getType() != DROID_TYPE::CYBORG_REPAIR &&
      !isTransporter(*psDroid) &&
      !recycled_experience[psDroid->playerManager->getPlayer()].empty()) {
    psDroid->pimpl->experience = recycled_experience[psDroid->playerManager->getPlayer()].top();
    recycled_experience[psDroid->playerManager->getPlayer()].pop();
  }
  else {
    psDroid->pimpl->experience = 0;
  }
  psDroid->pimpl->kills = 0;

  droidSetBits(pTemplate);

  //calculate the droids total weight
  psDroid->pimpl->weight = calcDroidWeight(pTemplate);

  // Initialise the movement stuff
  psDroid->pimpl->baseSpeed = calcDroidBaseSpeed(
          pTemplate, psDroid->pimpl->weight, (UBYTE)player);

  initDroidMovement();

  //allocate 'easy-access' data!
  psDroid->damageManager->setHp(calcDroidBaseBody(psDroid.get())); // includes upgrades
  ASSERT(psDroid->damageManager->getHp() > 0, "Invalid number of hitpoints");
  psDroid->damageManager->setOriginalHp(psDroid->damageManager->getHp());

  /* Set droid's initial illumination */
  psDroid->setImdShape(dynamic_cast<BodyStats const*>(getComponent(COMPONENT_TYPE::BODY))->pIMD.get());

  //don't worry if not on homebase cos not being drawn yet
  if (!onMission) {
    /* People always stand upright */
    if (psDroid->getType() != DROID_TYPE::PERSON) {
      psDroid->updateDroidOrientation();
    }
    visTilesUpdate(psDroid.get());
  }

  /* transporter-specific stuff */
  if (isTransporter(*psDroid)) {
    //add transporter launch button if selected player and not a reinforcable situation
    if (player == selectedPlayer && !missionCanReEnforce()) {
      (void)intAddTransporterLaunch(psDroid.get());
    }

    //set droid height to be above the terrain
    setPosition({getPosition().x, getPosition().y, getPosition().z + TRANSPORTER_HOVER_HEIGHT});

    /* reset halt secondary order from guard to hold */
    psDroid->secondarySetState(SECONDARY_ORDER::HALT_TYPE, DSS_HALT_HOLD);
  }

  if (player == selectedPlayer) {
    scoreUpdateVar(WD_UNITS_BUILT);
  }

  // Avoid droid appearing to jump or turn on spawn.
  setPreviousLocation(Spacetime(getPreviousLocation().time, getPosition(), getRotation()));

  debug(LOG_LIFE, "created droid for player %d, droid = %p,"
                  " id=%d (%s): position: x(%d)y(%d)z(%d)", player,
        static_cast<void*>(psDroid.get()), (int)psDroid->getId(),
        psDroid->getName().c_str(), psDroid->getPosition().x,
        psDroid->getPosition().y, psDroid->getPosition().z);

  return psDroid;
}

Structure const* Droid::getBase() const
{
  return pimpl ? pimpl->associatedStructure : nullptr;
}

//initialises the droid movement model
void Droid::initDroidMovement()
{
  if (!pimpl) return;
  pimpl->movement->path.clear();
  pimpl->movement->pathIndex = 0;
}

// Give a droid from one player to another - used in Electronic Warfare and multiplayer.
// Got to destroy the droid and build another since there are too many complications otherwise.
// Returns the droid created.
std::unique_ptr<Droid> Droid::giftSingleDroid(unsigned to, bool electronic)
{
  ASSERT_OR_RETURN(nullptr, !damageManager->isDead(), "Cannot gift dead unit");
  ASSERT_OR_RETURN(std::make_unique<Droid>(*this), playerManager->getPlayer() != to, "Cannot gift to self");
  ASSERT_OR_RETURN(nullptr, to < MAX_PLAYERS, "Cannot gift to = %" PRIu32 "", to);
  std::unique_ptr<Droid> psNewDroid;

  // Check unit limits (multiplayer only)
  syncDebug("Limits: %u/%d %u/%d %u/%d", getNumDroids(to), getMaxDroids(to), getNumConstructorDroids(to),
            getMaxConstructors(to), getNumCommandDroids(to), getMaxCommanders(to));
  if (bMultiPlayer
      && ((int)getNumDroids(to) >= getMaxDroids(to)
          || ((getType() == DROID_TYPE::CYBORG_CONSTRUCT ||
               getType() == DROID_TYPE::CONSTRUCT)
              && (int)getNumConstructorDroids(to) >= getMaxConstructors(to))
          || (getType() == DROID_TYPE::COMMAND && (int)getNumCommandDroids(to) >= getMaxCommanders(to)))) {
    if (to == selectedPlayer || playerManager->getPlayer() == selectedPlayer) {
      CONPRINTF("%s", _("Unit transfer failed -- unit limits exceeded"));
    }
    return nullptr;
  }

  // electronic or campaign will destroy and recreate the droid.
  if (electronic || !bMultiPlayer) {
    DroidTemplate sTemplate;

    templateSetParts(this, &sTemplate); // create a template based on the droid
    sTemplate.name = WzString::fromUtf8(getName()); // copy the name across
    // update score
    if (playerManager->getPlayer() == selectedPlayer &&
        to != selectedPlayer && !bMultiPlayer) {
      scoreUpdateVar(WD_UNITS_LOST);
    }
    // make the old droid vanish (but is not deleted until next tick)
    adjustDroidCount(this, -1);
    vanishDroid(this);
    // create a new droid
    psNewDroid = reallyBuildDroid(&sTemplate,
                                       Position(getPosition().x, getPosition().y, 0),
                                       to, false, getRotation());

    ASSERT_OR_RETURN(nullptr, psNewDroid.get(), "Unable to build unit");

    addDroid(psNewDroid.get());
    adjustDroidCount(psNewDroid.get(), 1);

    psNewDroid->damageManager->setHp(clip(
            (damageManager->getHp() * psNewDroid->damageManager->getOriginalHp() +
                    damageManager->getOriginalHp() / 2) / std::max(damageManager->getOriginalHp(), 1u), 1u,
            psNewDroid->damageManager->getOriginalHp()));
    psNewDroid->pimpl->experience = pimpl->experience;
    psNewDroid->pimpl->kills = pimpl->kills;

    if (!(psNewDroid->getType() == DROID_TYPE::PERSON ||
          isCyborg(psNewDroid.get()) ||
          isTransporter(*psNewDroid))) {
      psNewDroid->updateDroidOrientation();
    }

    triggerEventObjectTransfer(psNewDroid.get(), playerManager->getPlayer());
    return psNewDroid;
  }

  auto oldPlayer = playerManager->getPlayer();

  // reset the assigned state of units attached to a leader
  for (auto& psCurr : playerList[oldPlayer].droids)
  {
    BaseObject* psLeader;

    if (psCurr.hasCommander()) {
      psLeader = psCurr.pimpl->commander;
    }
    else {
      //psLeader can be either a droid or a structure
      psLeader = orderStateObj(&psCurr, ORDER_TYPE::FIRE_SUPPORT);
    }

    if (psLeader && psLeader->getId() == getId()) {
      psCurr.damageManager->setSelected(false);
      orderDroid(&psCurr, ORDER_TYPE::STOP, ModeQueue);
    }
  }

  visRemoveVisibility(this);
  damageManager->setSelected(false);

  adjustDroidCount(this, -1);
  scriptRemoveObject(this); //Remove droid from any script groups

  if (droidRemove(this)) {
    playerManager->setPlayer(to);

    addDroid(this);
    adjustDroidCount(this, 1);

//      // the new player may have different default sensor/ecm/repair components
//      if (auto sensor = dynamic_cast<const SensorStats*>(psNewDroid->getComponent("sensor")))
//      if ((psNewDroid->getComponent("sensor")->location == LOC::DEFAULT) {
//        if (psNewDroid->asBits[COMPONENT_TYPE::SENSOR] != aDefaultSensor[getPlayer()]) {
//          psNewDroid->asBits[COMPONENT_TYPE::SENSOR] = aDefaultSensor[getPlayer()];
//        }
//      }
//      if ((asECMStats + psNewDroid->asBits[COMPONENT_TYPE::ECM])->location == LOC::DEFAULT) {
//        if (psNewDroid->asBits[COMPONENT_TYPE::ECM] != aDefaultECM[getPlayer()]){
//          psNewDroid->asBits[COMPONENT_TYPE::ECM] = aDefaultECM[getPlayer()];
//        }
//      }
//      if ((asRepairStats + psD->asBits[COMPONENT_TYPE::REPAIRUNIT])->location == LOC::DEFAULT) {
//        if (psD->asBits[COMPONENT_TYPE::REPAIRUNIT] != aDefaultRepair[getPlayer()]) {
//          psD->asBits[COMPONENT_TYPE::REPAIRUNIT] = aDefaultRepair[getPlayer()];
//        }
//      }
  }
  else {
    // if we couldn't remove it, then get rid of it.
    return nullptr;
  }

  // Update visibility
  visTilesUpdate((BaseObject *)this);

  // check through the players, and our allies, list of droids to see if any are targeting it
  for (auto i = 0; i < MAX_PLAYERS; ++i)
  {
    if (!aiCheckAlliances(i, to)) {
      continue;
    }

    for (auto& psCurr : playerList[i].droids)
    {
      if (psCurr.getOrder()->target == this ||
          psCurr.getTarget(0) == this) {
        orderDroid(&psCurr, ORDER_TYPE::STOP, ModeQueue);
        break;
      }
      for (auto iWeap = 0; iWeap < numWeapons(psCurr); ++iWeap)
      {
        if (psCurr.getTarget(iWeap) == this) {
          orderDroid(&psCurr, ORDER_TYPE::STOP, ModeImmediate);
          break;
        }
      }
      // check through order list
      orderClearTargetFromDroidList(&psCurr);
    }
  }

  for (auto i = 0; i < MAX_PLAYERS; ++i)
  {
    if (!aiCheckAlliances(i, to)) {
      continue;
    }

    // check through the players list, and our allies, of structures to see if any are targeting it
    for (auto& psStruct : playerList[i].structures)
    {
      if (psStruct.getTarget(0) == this) {
        setStructureTarget(&psStruct, nullptr, 0, TARGET_ORIGIN::UNKNOWN);
      }
    }
  }

  triggerEventObjectTransfer(this, oldPlayer);
  return std::make_unique<Droid>(*this);
}

// Set the asBits in a Droid structure given its template.
void Droid::droidSetBits(const DroidTemplate* pTemplate)
{
  if (!pimpl) return;
  pimpl->type = droidTemplateType(pTemplate);
  damageManager->setHp(calcTemplateBody(pTemplate, playerManager->getPlayer()));
  damageManager->setOriginalHp(damageManager->getHp());
  damageManager->setExpectedDamageDirect(0); // Begin life optimistically.
  damageManager->setExpectedDamageIndirect(0); // Begin life optimistically.
  setTime(gameTime - deltaGameTime + 1); // Start at beginning of tick.
  setPreviousLocation({getTime() - 1, getPreviousLocation().position, getPreviousLocation().rotation}); // -1 for interpolation.

  //create the droids weapons
  for (auto inc = 0; inc < MAX_WEAPONS; inc++)
  {
    pimpl->actionTargets[inc] = nullptr;
    weaponManager->weapons[inc].timeLastFired = 0;
    weaponManager->weapons[inc].shotsFired = 0;
    // no weapon (could be a construction droid for example)
    // this is also used to check if a droid has a weapon, so zero it
    weaponManager->weapons[inc].ammo = 0;
    weaponManager->weapons[inc].setRotation({0, 0, 0});
    weaponManager->weapons[inc].previousRotation = weaponManager->weapons[inc].getRotation();
    weaponManager->weapons[inc].origin = TARGET_ORIGIN::UNKNOWN;
    if (inc < pTemplate->weapons.size()) {
      weaponManager->weapons[inc].ammo = weaponManager->weapons[inc].stats->upgraded[playerManager->getPlayer()].numRounds;
    }
    weaponManager->weapons[inc].ammoUsed = 0;
  }
  auto propulsion = dynamic_cast<PropulsionStats const*>(getComponent(COMPONENT_TYPE::PROPULSION));
  if (!propulsion) return;
  switch (propulsion->propulsionType) {
    // getPropulsionStats(psDroid) only defined after psDroid->asBits[COMPONENT_TYPE::PROPULSION] is set.
    case PROPULSION_TYPE::LIFT:
      pimpl->blockedBits = AIR_BLOCKED;
      break;
    case PROPULSION_TYPE::HOVER:
      pimpl->blockedBits = FEATURE_BLOCKED;
      break;
    case PROPULSION_TYPE::PROPELLOR:
      pimpl->blockedBits = FEATURE_BLOCKED | LAND_BLOCKED;
      break;
    default:
      pimpl->blockedBits = FEATURE_BLOCKED | WATER_BLOCKED;
      break;
  }
}
  
void Droid::setTarget(BaseObject* psNewTarget)
{
  if (!pimpl) return;
  pimpl->order->target = psNewTarget;
  ASSERT(psNewTarget == nullptr || !dynamic_cast<Health *>(psNewTarget)->isDead(),
         "setDroidTarget: Set dead target");
}

void Droid::setAction(ACTION action)
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Droid object is undefined");
  pimpl->action = action;
}

void Droid::setOrder(std::unique_ptr<Order> order)
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Droid object is undefined");
  pimpl->order = std::move(order);
}

void Droid::setActionTarget(BaseObject* psNewTarget, unsigned idx)
{
  if (!pimpl) return;
  pimpl->actionTargets[idx] = psNewTarget;
  ASSERT(psNewTarget == nullptr || !dynamic_cast<Health *>(psNewTarget)->isDead(),
         "setDroidActionTarget: Set dead target");
}

void Droid::setBase(Structure* psNewBase)
{
  if (!pimpl) return;
  pimpl->associatedStructure = psNewBase;
  ASSERT(psNewBase == nullptr ||
         !psNewBase->damageManager->isDead(),
         "setDroidBase: Set dead target");
}

// initialise droid module
bool droidInit()
{
	for (auto& i : recycled_experience)
	{
		i = std::priority_queue<int>(); // clear it
	}
	psLastDroidHit = nullptr;
	return true;
}

int droidReloadBar(BaseObject const& psObj, Weapon const* psWeap, int weapon_slot)
{

	if (!psObj.weaponManager || psObj.weaponManager->weapons.empty()) {
		return -1;
	}
	auto psStats = psWeap->stats.get();

	/* Justifiable on. when greater than a one second reload or intra salvo time  */
  auto player = psObj.playerManager->getPlayer();
	auto bSalvo = (psStats->upgraded[player].numRounds > 1);
  auto psDroid = dynamic_cast<const Droid*>(&psObj);
  if (!(bSalvo && psStats->upgraded[player].reloadTime > GAME_TICKS_PER_SEC) &&
        psStats->upgraded[player].firePause <= GAME_TICKS_PER_SEC && !(psDroid->isVtol())) {
    return -1;
  }

  unsigned firingStage, interval;
  if (psDroid && psDroid->isVtol()) {
  //deal with VTOLs
  firingStage = getNumAttackRuns(psDroid, weapon_slot)
          - psDroid->weaponManager->weapons[weapon_slot].ammoUsed;

  //compare with max value
  interval = getNumAttackRuns(psDroid, weapon_slot);
  }
  else {
  firingStage = gameTime - psWeap->timeLastFired;
  interval = bSalvo
          ? weaponReloadTime(psStats, player)
          : weaponFirePause(psStats, player);
}
  if (firingStage < interval && interval > 0) {
    return PERCENT(firingStage, interval);
  }
  return 100;
}

std::priority_queue<int> copy_experience_queue(unsigned player)
{
	return recycled_experience[player];
}

void add_to_experience_queue(unsigned player, int value)
{
	recycled_experience[player].push(value);
}


static void removeDroidFX(Droid* psDel, unsigned impactTime)
{
	Vector3i pos;

	// only display anything if the droid is visible
	if (!psDel->isVisibleToSelectedPlayer()) {
		return;
	}

	if (psDel->getAnimationEvent() != ANIM_EVENT_DYING) {
		compPersonToBits(psDel);
	}

	/* if baba then squish */
	if (psDel->getType() == DROID_TYPE::PERSON) {
		// The barbarian has been run over ...
		audio_PlayStaticTrack(psDel->getPosition().x, psDel->getPosition().y, ID_SOUND_BARB_SQUISH);
	}
	else {
		destroyFXDroid(psDel, impactTime);
		pos.x = psDel->getPosition().x;
		pos.z = psDel->getPosition().y;
		pos.y = psDel->getPosition().z;
		if (psDel->getType() == DROID_TYPE::SUPER_TRANSPORTER) {
			addEffect(&pos, EFFECT_GROUP::EXPLOSION,
                EFFECT_TYPE::EXPLOSION_TYPE_LARGE,
                false, nullptr, 0, impactTime);
		}
		else {
			addEffect(&pos, EFFECT_GROUP::DESTRUCTION,
                EFFECT_TYPE::DESTRUCTION_TYPE_DROID,
                false, nullptr, 0, impactTime);
		}
		audio_PlayStaticTrack(psDel->getPosition().x, psDel->getPosition().y, ID_SOUND_EXPLOSION);
	}
}

bool destroyDroid(Droid* psDel, unsigned impactTime)
{
	ASSERT(gameTime - deltaGameTime <= impactTime,
         "Expected %u <= %u, gameTime = %u, bad impactTime",
	       gameTime - deltaGameTime, impactTime, gameTime);

	if (psDel->damageManager->getLastHitWeapon() == WEAPON_SUBCLASS::LAS_SAT) // darken tile if lassat.
	{
		auto mapX = map_coord(psDel->getPosition().x);
		auto mapY = map_coord(psDel->getPosition().y);
		for (auto width = mapX - 1; width <= mapX + 1; width++)
		{
			for (auto breadth = mapY - 1; breadth <= mapY + 1; breadth++)
			{
				auto psTile = mapTile(width, breadth);
				if (TEST_TILE_VISIBLE_TO_SELECTEDPLAYER(psTile)) {
					psTile->illumination /= 2;
				}
			}
		}
	}

	removeDroidFX(psDel, impactTime);
	removeDroidBase(psDel);
	psDel->damageManager->setTimeOfDeath(impactTime);
	return true;
}

void vanishDroid(Droid* psDel)
{
	removeDroidBase(psDel);
}

/* Remove a droid from the List so doesn't update or get drawn etc
TAKE CARE with removeDroid() - usually want droidRemove since it deal with grid code*/
//returns false if the droid wasn't removed - because it died!
void Droid::droidRemove()
{
	if (damageManager->isDead()) {
		// droid has already been killed, quit
		return;
	}

	// leave the current group if any - not if its a Transporter droid
	if (!isTransporter(*this) && pimpl->group) {
		removeDroidFromGroup(this);
		pimpl->group = nullptr;
	}

	// reset the baseStruct
	setBase(nullptr);

	removeDroid(this);

	if (playerManager->isSelectedPlayer()) {
		intRefreshScreen();
	}
}

static bool droidNextToStruct(Droid const* psDroid, Structure const* psStruct)
{
	auto pos = map_coord(psDroid->getPosition());
	auto minY = std::max(pos.y - 1, 0);
	auto minX = std::max(pos.x - 1, 0);
	auto maxX = std::min(pos.x + 1, mapWidth);
	auto maxY = std::min(pos.y + 1, mapHeight);
	for (int y = minY; y <= maxY; ++y)
	{
		for (int x = minX; x <= maxX; ++x)
		{
			if (TileHasStructure(mapTile(x, y)) &&
			  	getTileStructure(x, y) == psStruct) {
				return true;
			}
		}
	}
	return false;
}

static bool droidCheckBuildStillInProgress(Droid const* psDroid)
{
	return !psDroid->damageManager->isDead() && psDroid->getAction() == ACTION::BUILD;
}

static bool droidBuildStartAudioCallback(Droid* psDroid)
{
	if (psDroid != nullptr && psDroid->isVisibleToSelectedPlayer()) {
		audio_PlayObjDynamicTrack(psDroid, ID_SOUND_CONSTRUCTION_LOOP,
                              droidCheckBuildStillInProgress);
	}
	return true;
}


static void droidAddWeldSound(Vector3i iVecEffect)
{
	auto iAudioID = ID_SOUND_CONSTRUCTION_1 + (rand() % 4);

	audio_PlayStaticTrack(iVecEffect.x, iVecEffect.z, iAudioID);
}

static void addConstructorEffect(Structure const* psStruct)
{
	if (ONEINTEN && psStruct->isVisibleToSelectedPlayer()) {
		/* This needs fixing - it's an arse effect! */
		const Vector2i size = psStruct->getSize() * TILE_UNITS / 4;
		Vector3i temp;
		temp.x = psStruct->getPosition().x + ((rand() % (2 * size.x)) - size.x);
		temp.y = map_TileHeight(map_coord(psStruct->getPosition().x),
                            map_coord(psStruct->getPosition().y)) + (psStruct->getDisplayData()->imd_shape->max.y
			/ 6);
		temp.z = psStruct->getPosition().y + ((rand() % (2 * size.y)) - size.y);
		if (rand() % 2) {
			droidAddWeldSound(temp);
		}
	}
}

bool droidUpdateRepair(Droid* psDroid)
{
	ASSERT_OR_RETURN(false, psDroid->getAction() == ACTION::REPAIR, "unit does not have repair order");
  
	auto psStruct = dynamic_cast<Structure const*>(psDroid->getTarget(0));
  auto construct = dynamic_cast<ConstructStats const*>(psDroid->getComponent(COMPONENT_TYPE::CONSTRUCT));

  auto iRepairRate = -1;
  if (construct) {
    iRepairRate = constructorPoints(construct, psDroid->playerManager->getPlayer());
  }

	/* add points to structure */
	structureRepair(psStruct, iRepairRate);

	/* if not finished repair return true else complete repair and return false */
	if (psStruct->damageManager->getHp() < structureBody(psStruct)) {
		return true;
	}
	else {
		objTrace(psDroid->getId(), "Repaired of %s all done with %u",
             objInfo(psStruct), iRepairRate);
		return false;
	}
}

/*Updates a Repair Droid working on a damaged droid*/
static bool droidUpdateDroidRepairBase(Droid const* psRepairDroid, Droid* psDroidToRepair)
{
  auto repairStats = dynamic_cast<const RepairStats*>(psRepairDroid->getComponent(COMPONENT_TYPE::REPAIR_UNIT));
	auto iRepairRateNumerator = repairPoints(repairStats, psRepairDroid->playerManager->getPlayer());
	auto iRepairRateDenominator = 1;

	//if self repair then add repair points depending on the time delay for the stat
	if (psRepairDroid == psDroidToRepair) {
		iRepairRateNumerator *= GAME_TICKS_PER_SEC;
		iRepairRateDenominator *= repairStats->time;
	}

	auto iPointsToAdd = gameTimeAdjustedAverage(iRepairRateNumerator, iRepairRateDenominator);

	psDroidToRepair->damageManager->setHp(clip<unsigned>(
          psDroidToRepair->damageManager->getHp() + iPointsToAdd, 0,
          psDroidToRepair->damageManager->getOriginalHp()));

	/* add plasma repair effect whilst being repaired */
	if (ONEINFIVE && psDroidToRepair->isVisibleToSelectedPlayer()) {
		Vector3i iVecEffect = (psDroidToRepair->getPosition() + Vector3i(
            DROID_REPAIR_SPREAD, DROID_REPAIR_SPREAD, rand() % 8)). xzy();
		effectGiveAuxVar(90 + rand() % 20);
		addEffect(&iVecEffect, EFFECT_GROUP::EXPLOSION,
              EFFECT_TYPE::EXPLOSION_TYPE_LASER,
              false, nullptr, 0,
		          gameTime - deltaGameTime + 1 + rand() % deltaGameTime);
		droidAddWeldSound(iVecEffect);
	}
	/* if not finished repair return true else complete repair and return false */
	return psDroidToRepair->damageManager->getHp() < psDroidToRepair->damageManager->getOriginalHp();
}

static void droidUpdateDroidSelfRepair(Droid* psRepairDroid)
{
	droidUpdateDroidRepairBase(psRepairDroid, psRepairDroid);
}

bool isIdf(Droid const& droid)
{
  return (droid.getType() != DROID_TYPE::WEAPON ||
          !isCyborg(&droid)) && hasArtillery(droid);
}

/* Return the type of a droid from it's template */
DROID_TYPE droidTemplateType(DroidTemplate const* psTemplate)
{
  using enum DROID_TYPE;
	auto type = DEFAULT;

	if (psTemplate->type == PERSON ||
      psTemplate->type == CYBORG ||
      psTemplate->type == CYBORG_SUPER ||
      psTemplate->type == CYBORG_CONSTRUCT ||
      psTemplate->type == CYBORG_REPAIR ||
      psTemplate->type == TRANSPORTER ||
      psTemplate->type == SUPER_TRANSPORTER) {
		type = psTemplate->type;
	}
	else if (psTemplate->getComponent(COMPONENT_TYPE::BRAIN) != nullptr)
	{
		type = COMMAND;
	}
	else if (dynamic_cast<SensorStats const*>(psTemplate->getComponent(COMPONENT_TYPE::SENSOR))->location == LOC::TURRET)
	{
		type = SENSOR;
	}
	else if (dynamic_cast<EcmStats const*>(psTemplate->getComponent(COMPONENT_TYPE::ECM))->location == LOC::TURRET)
	{
		type = ECM;
	}
	else if (psTemplate->getComponent(COMPONENT_TYPE::CONSTRUCT) != nullptr)
	{
		type = CONSTRUCT;
	}
	else if (dynamic_cast<RepairStats const*>(psTemplate->getComponent(COMPONENT_TYPE::REPAIR_UNIT))->location == LOC::TURRET)
	{
		type = REPAIRER;
	}
	else if (!psTemplate->weapons.empty())
	{
		type = WEAPON;
	}

	return type;
}

template <typename F, typename G>
static unsigned calcSum(std::unordered_map<std::string, std::unique_ptr<ComponentStats>> const& asParts,
                        std::array<Weapon*, MAX_WEAPONS> const& asWeaps, F func, G propulsionFunc)
{
  auto sum =
    func(asParts.at("brain")) +
		func(asParts.at("sensor")) +
		func(asParts.at("ecm")) +
		func(asParts.at("repair")) +
		func(asParts.at("construct")) +
		propulsionFunc(asParts.at("body"), asParts.at("propulsion"));

	for (auto weap : asWeaps)
	{
		sum += func(weap->stats.get());
	}
	return sum;
}

#define ASSERT_PLAYER_OR_RETURN(retVal, player) \
	ASSERT_OR_RETURN(retVal, player >= 0 &&        \
                   player < MAX_PLAYERS,        \
                   "Invalid player: %" PRIu32 "", \
                   player);

template <typename F, typename G>
static unsigned calcUpgradeSum(const std::unordered_map<std::string, std::unique_ptr<ComponentStats>> &asParts,
                               const std::array<Weapon*, MAX_WEAPONS> &asWeaps, unsigned player,
                               F func, G propulsionFunc)
{
	ASSERT_PLAYER_OR_RETURN(0, player);
	unsigned sum =
          func(asParts.at("brain")->upgraded[player]) +
          func(asParts.at("sensor")->upgraded[player]) +
          func(asParts.at("ecm")->upgraded[player]) +
          func(asParts.at("repair")->upgraded[player]) +
          func(asParts.at("construct")->upgraded[player]) +
          propulsionFunc(asParts.at("body")->upgraded[player], asParts.at("propulsion")->upgraded[player]);

  for (auto weap : asWeaps)
  {
    sum += func(weap->stats.get()->upgraded[player]);
  }
	return sum;
}

struct FilterDroidWeaps
{
	FilterDroidWeaps(size_t numWeaps, std::array<Weapon, MAX_WEAPONS> *const weapons)
	{
		std::transform(asWeaps, asWeaps + numWeaps, this->asWeaps, [](auto const& weap)
		{
			return weap.nStat;
		});
		this->numWeaps = std::remove_if(this->asWeaps, this->asWeaps + numWeaps, [](unsigned stat)
		{
			return stat == 0;
		}) - this->asWeaps;
	}

	unsigned numWeaps;
	unsigned asWeaps[MAX_WEAPONS];
};

template <typename F, typename G>
static unsigned calcSum(const DroidTemplate* psTemplate, F func, G propulsionFunc)
{
	return calcSum(psTemplate->components, psTemplate->weapons, func, propulsionFunc);
}

template <typename F, typename G>
static unsigned calcSum(Droid const* psDroid, F func, G propulsionFunc)
{
	FilterDroidWeaps f = {numWeapons(*psDroid), psDroid->weaponManager->weapons};
	return calcSum(psDroid->asBits, f.numWeaps, f.asWeaps,
                 func, propulsionFunc);
}

template <typename F, typename G>
static unsigned calcUpgradeSum(const DroidTemplate* psTemplate, unsigned player, F func, G propulsionFunc)
{
	return calcUpgradeSum(psTemplate->components,
                        psTemplate->weapons,
                        player, func, propulsionFunc);
}

template <typename F, typename G>
static unsigned calcUpgradeSum(Droid const* psDroid, unsigned player, F func, G propulsionFunc)
{
	FilterDroidWeaps f {numWeapons(*psDroid), psDroid->weaponManager->weapons};
	return calcUpgradeSum(psDroid->asBits, f.numWeaps, f.asWeaps, player, func, propulsionFunc);
}

/* Calculate the weight of a droid from it's template */
unsigned calcDroidWeight(DroidTemplate const* psTemplate)
{
	return calcSum(psTemplate, [](ComponentStats const& stat)
	               {
		               return stat.weight;
	               }, [](BodyStats const& bodyStat, PropulsionStats const& propStat)
	               {
		               // Propulsion weight is a percentage of the body weight.
		               return bodyStat.weight * (100 + propStat.weight) / 100;
	               });
}

template <typename T>
static unsigned calcBody(T const* obj, unsigned player)
{
	auto hitpoints = calcUpgradeSum(obj, player,
                                  [](ComponentStats::Upgradeable const& upgrade) {
                                    return upgrade.hitPoints;
	                                }
                                  , [](BodyStats::Upgradeable const& bodyUpgrade,
                                       PropulsionStats::Upgradeable const& propUpgrade) {
		                               // propulsion hitpoints can be a percentage of the body's hitpoints
		                                return bodyUpgrade.hitPoints * (100 + propUpgrade.hitpointPctOfBody) / 100 +
                                           propUpgrade.hitPoints;
	                                });

	auto hitpointPct = calcUpgradeSum(obj, player, [](ComponentStats::Upgradeable const& upgrade)
	                                 {
		                                 return upgrade.hitpointPct - 100;
	                                 }, [](BodyStats::Upgradeable const& bodyUpgrade,
                                         PropulsionStats::Upgradeable const& propUpgrade)
	                                 {
		                                 return bodyUpgrade.hitpointPct - 100 + propUpgrade.hitpointPct - 100;
	                                 });

	// Final adjustment based on the hitpoint modifier
	return hitpoints * (100 + hitpointPct) / 100;
}

// Calculate the body points of a droid from its template
unsigned calcTemplateBody(DroidTemplate const* psTemplate, unsigned player)
{
	if (psTemplate == nullptr) {
		ASSERT(false, "null template");
		return 0;
	}
	return calcBody(psTemplate, player);
}

// Calculate the base body points of a droid with upgrades
static unsigned calcDroidBaseBody(Droid const* psDroid)
{
	return calcBody(psDroid, psDroid->playerManager->getPlayer());
}


/* Calculate the base speed of a droid from it's template */
unsigned calcDroidBaseSpeed(DroidTemplate const* psTemplate, unsigned weight, unsigned player)
{
  auto propulsion = dynamic_cast<PropulsionStats const*>(psTemplate->getComponent(COMPONENT_TYPE::PROPULSION));

	auto speed = asPropulsionTypes[(int)propulsion->propulsionType].powerRatioMult *
          bodyPower(dynamic_cast<BodyStats const*>(
                            psTemplate->getComponent(COMPONENT_TYPE::BODY)), player) / MAX(1, weight);

  auto body = dynamic_cast<BodyStats const*>(psTemplate->getComponent(COMPONENT_TYPE::BODY));
  // reduce the speed of medium/heavy VTOLs
	if (propulsion->propulsionType == PROPULSION_TYPE::LIFT) {
		if (body->size == BODY_SIZE::HEAVY) {
			speed /= 4;
		}
		else if (body->size == BODY_SIZE::MEDIUM) {
			speed = speed * 3 / 4;
		}
	}

	// applies the engine output bonus if output > weight
	if (body->base.power > weight) {
		speed = speed * 3 / 2;
	}
	return speed;
}


/* Calculate the speed of a droid over a terrain */
unsigned calcDroidSpeed(unsigned baseSpeed, unsigned terrainType, PropulsionStats const* propulsion, unsigned level)
{
  if (propulsion == nullptr) {
    return 0;
  }

	// factor in terrain
	auto speed = baseSpeed * getSpeedFactor(terrainType,
                                          static_cast<unsigned>(propulsion->propulsionType)) / 100;

	// need to ensure doesn't go over the max speed possible
  // for this propulsion
	speed = std::min(speed, propulsion->maxSpeed);

	// factor in experience
	speed *= 100 + EXP_SPEED_BONUS * level;
	speed /= 100;

	return speed;
}

template <typename T>
static unsigned calcBuild(T const* obj)
{
	return calcSum(obj, [](ComponentStats const& stat) {
     return stat.buildPoints;
  }, [](BodyStats const& bodyStat, PropulsionStats const& propStat) {
    // Propulsion power points are a percentage of the body's build points.
    return bodyStat.buildPoints * (100 + propStat.buildPoints) / 100;
  });
}

/* Calculate the points required to build the template - used to calculate time*/
unsigned calcTemplateBuild(DroidTemplate const* psTemplate)
{
	return calcBuild(psTemplate);
}

unsigned calcDroidPoints(Droid const* psDroid)
{
	return calcBuild(psDroid);
}

template <typename T>
static int calcPower(const T* obj)
{
	ASSERT_NOT_NULLPTR_OR_RETURN(0, obj);
	return calcSum(obj, [](ComponentStats const& stat) {
     return stat.buildPower;
   }, [](BodyStats const& bodyStat, PropulsionStats const& propStat) {
     // Propulsion power points are a percentage of the body's power points.
     return bodyStat.buildPower * (100 + propStat.buildPower) / 100;
   });
}

/* Calculate the power points required to build/maintain a template */
int calcTemplatePower(DroidTemplate const* psTemplate)
{
	return calcPower(psTemplate);
}

/* Calculate the power points required to build/maintain a droid */
int calcDroidPower(Droid const* psDroid)
{
	return calcPower(psDroid);
}

std::unique_ptr<Droid> buildDroid(DroidTemplate* pTemplate, unsigned x, unsigned y, unsigned player, bool onMission,
                                  InitialOrders const* initialOrders, Rotation rot)
{
	ASSERT_OR_RETURN(nullptr, player < MAX_PLAYERS, "invalid player?: %" PRIu32 "", player);
	// ajl. droid will be created, so inform others
	if (bMultiMessages) {
		// Only sends if it's ours, otherwise the owner should send the message.
		SendDroid(pTemplate, x, y, player, generateNewObjectId(), initialOrders);
		return nullptr;
	}
	else {
		return reallyBuildDroid(pTemplate, Position(x, y, 0), player, onMission, rot);
	}
}

// Sets the parts array in a template given a droid.
void Droid::templateSetParts(DroidTemplate* psTemplate) const
{
	psTemplate->type = pimpl->type;
  psTemplate->weapons = weaponManager->weapons;
  psTemplate->components = pimpl->components;
}

/* Make all the droids for a certain player a member of a specific group */
void assignDroidsToGroup(unsigned playerNumber, unsigned groupNumber, bool clearGroup)
{
	bool bAtLeastOne = false;

	ASSERT_OR_RETURN(, playerNumber < MAX_PLAYERS, "Invalid player: %" PRIu32 "", playerNumber);

	if (groupNumber < UBYTE_MAX) {
		/* Run through all the droids */
		for (auto& psDroid : playerList[playerNumber].droids)
		{
			/* Clear out the old ones */
			if (clearGroup && psDroid.getSelectionGroup() == groupNumber) {
				psDroid.setSelectionGroup(UBYTE_MAX);
			}

			/* Only assign the currently selected ones */
			if (psDroid.damageManager->isSelected()) {
				/* Set them to the right group - they can only be a member of one group */
				psDroid.setSelectionGroup((UBYTE)groupNumber);
				bAtLeastOne = true;
			}
		}
	}
	if (bAtLeastOne) {
		//clear the Deliv Point if one
		ASSERT_OR_RETURN(, selectedPlayer < MAX_PLAYERS, "Unsupported selectedPlayer: %" PRIu32 "", selectedPlayer);
		for (auto psFlagPos : apsFlagPosLists[selectedPlayer])
		{
			psFlagPos->selected = false;
		}
		groupConsoleInformOfCreation(groupNumber);
		secondarySetAverageGroupState(selectedPlayer, groupNumber);
	}
}

void removeDroidsFromGroup(unsigned playerNumber)
{
	ASSERT_OR_RETURN(, playerNumber < MAX_PLAYERS, "Invalid player: %" PRIu32 "", playerNumber);

  unsigned removedCount = 0;
	for (auto& psDroid : playerList[playerNumber].droids)
	{
		if (psDroid.damageManager->isSelected()) {
			psDroid.setSelectionGroup(UBYTE_MAX);
			removedCount++;
		}
	}
	if (removedCount) {
		groupConsoleInformOfRemoval();
	}
}

bool activateGroupAndMove(unsigned playerNumber, unsigned groupNumber)
{
  ASSERT_OR_RETURN(false, playerNumber < MAX_PLAYERS, "Invalid player: %" PRIu32 "", playerNumber);

	Droid *psCentreDroid = nullptr;
	bool selected = false;

	if (groupNumber < UBYTE_MAX) {
		for (auto& psDroid : playerList[playerNumber].droids)
		{
			/* Wipe out the ones in the wrong group */
			if (psDroid.damageManager->isSelected() &&
          psDroid.getSelectionGroup() != groupNumber) {
				DeSelectDroid(&psDroid);
			}
			/* Get the right ones */
			if (psDroid.getSelectionGroup() == groupNumber) {
				SelectDroid(&psDroid);
				psCentreDroid = &psDroid;
			}
		}

		/* There was at least one in the group */
		if (psCentreDroid) {
			//clear the Deliv Point if one
			ASSERT(selectedPlayer < MAX_PLAYERS, "Unsupported selectedPlayer: %" PRIu32 "", selectedPlayer);
			if (selectedPlayer < MAX_PLAYERS) {
				for (auto psFlagPos : apsFlagPosLists[selectedPlayer]) {
					psFlagPos->selected = false;
				}
			}

			selected = true;
			if (getWarCamStatus()) {
				camToggleStatus(); // messy - fix this
				processWarCam(); //odd, but necessary
				camToggleStatus(); // messy - FIXME
			}
			else {
				/* Centre display on him if warcam isn't active */
				setViewPos(map_coord(psCentreDroid->getPosition().x),
                   map_coord(psCentreDroid->getPosition().y), true);
			}
		}
	}

	if (selected) {
		groupConsoleInformOfCentering(groupNumber);
	}

	return selected;
}

bool activateNoGroup(unsigned playerNumber, SELECTIONTYPE selectionType,
                     SELECTION_CLASS selectionClass, bool bOnScreen)
{
	bool selected = false;
	SELECTIONTYPE dselectionType = selectionType;
	SELECTION_CLASS dselectionClass = selectionClass;
	bool dbOnScreen = bOnScreen;

	ASSERT_OR_RETURN(false, playerNumber < MAX_PLAYERS, "Invalid player: %" PRIu32 "", playerNumber);

	selDroidSelection(selectedPlayer, dselectionClass, dselectionType, dbOnScreen);
	for (auto& psDroid : playerList[playerNumber].droids)
	{
		/* Wipe out the ones in the wrong group */
		if (psDroid.damageManager->isSelected() &&
        psDroid.getSelectionGroup() != UBYTE_MAX) {
			DeSelectDroid(&psDroid);
		}
	}
	if (selected) {
		//clear the Deliv Point if one
		ASSERT_OR_RETURN(false, selectedPlayer < MAX_PLAYERS, "Unsupported selectedPlayer: %" PRIu32 "",
		                 selectedPlayer);
		for (auto psFlagPos : apsFlagPosLists[selectedPlayer])
		{
			psFlagPos->selected = false;
		}
	}
	return selected;
}

bool activateGroup(unsigned playerNumber, unsigned groupNumber)
{
	ASSERT_OR_RETURN(false, playerNumber < MAX_PLAYERS, "Invalid player: %" PRIu32 "", playerNumber);

  bool selected = false;
	if (groupNumber < UBYTE_MAX) {
		for (auto& psDroid : playerList[playerNumber].droids)
		{
			/* Wipe out the ones in the wrong group */
			if (psDroid.damageManager->isSelected() &&
          psDroid.getSelectionGroup() != groupNumber) {
				DeSelectDroid(&psDroid);
			}
			/* Get the right ones */
			if (psDroid.getSelectionGroup() == groupNumber) {
				SelectDroid(&psDroid);
				selected = true;
			}
		}
	}

	if (selected) {
		//clear the Deliv Point if one
		ASSERT_OR_RETURN(false, selectedPlayer < MAX_PLAYERS, "Unsupported selectedPlayer: %" PRIu32 "",
		                 selectedPlayer);
		for (auto psFlagPos : apsFlagPosLists[selectedPlayer])
		{
			psFlagPos->selected = false;
		}
		groupConsoleInformOfSelection(groupNumber);
	}
	return selected;
}

void groupConsoleInformOfSelection(unsigned groupNumber)
{
	auto num_selected = selNumSelected(selectedPlayer);

	CONPRINTF(ngettext("Group %u selected - %u Unit", "Group %u selected - %u Units", num_selected), groupNumber,
	          num_selected);
}

void groupConsoleInformOfCreation(unsigned groupNumber)
{
	if (!getWarCamStatus()) {
		auto num_selected = selNumSelected(selectedPlayer);

		CONPRINTF(ngettext("%u unit assigned to Group %u", "%u units assigned to Group %u", num_selected), num_selected,
		          groupNumber);
	}
}

void groupConsoleInformOfRemoval()
{
	if (!getWarCamStatus()) {
		auto num_selected = selNumSelected(selectedPlayer);

		CONPRINTF(ngettext("%u units removed from their Group", "%u units removed from their Group", num_selected),
		          num_selected);
	}
}

void groupConsoleInformOfCentering(unsigned groupNumber)
{
	auto num_selected = selNumSelected(selectedPlayer);

	if (!getWarCamStatus()) {
		CONPRINTF(ngettext("Centered on Group %u - %u Unit",
                       "Centered on Group %u - %u Units", num_selected),
		          groupNumber, num_selected);
	}
	else {
		CONPRINTF(ngettext("Aligning with Group %u - %u Unit",
                       "Aligning with Group %u - %u Units", num_selected),
              groupNumber, num_selected);
	}
}

/**
 * calculate muzzle base location in 3d world
 */
//bool calcDroidMuzzleBaseLocation(const DROID* psDroid, Vector3i* muzzle, int weapon_slot)
//{
//	const iIMDShape* psBodyImd = BODY_IMD(psDroid, psDroid->player);
//
//	CHECK_DROID(psDroid);
//
//	if (psBodyImd && psBodyImd->nconnectors)
//	{
//		Vector3i barrel(0, 0, 0);
//
//		Affine3F af;
//
//		af.Trans(psDroid->getPosition().x, -psDroid->getPosition().z, psDroid->getPosition().y);
//
//		//matrix = the center of droid
//		af.RotY(psDroid->rot.direction);
//		af.RotX(psDroid->rot.pitch);
//		af.RotZ(-psDroid->rot.roll);
//		af.Trans(psBodyImd->connectors[weapon_slot].x, -psBodyImd->connectors[weapon_slot].z,
//		         -psBodyImd->connectors[weapon_slot].y); //note y and z flipped
//
//		*muzzle = (af * barrel).xzy();
//		muzzle->z = -muzzle->z;
//	}
//	else
//	{
//		*muzzle = psDroid->getPosition() + Vector3i(0, 0, psDroid->sDisplay.imd->max.y);
//	}
//
//	CHECK_DROID(psDroid);
//
//	return true;
//}

///**
// * calculate muzzle tip location in 3d world
// */
//bool calcDroidMuzzleLocation(const DROID* psDroid, Vector3i* muzzle, int weapon_slot)
//{
//	const iIMDShape* psBodyImd = BODY_IMD(psDroid, psDroid->player);
//
//	CHECK_DROID(psDroid);
//
//	if (psBodyImd && psBodyImd->nconnectors)
//	{
//		char debugStr[250], debugLen = 0;
//		// Each "(%d,%d,%d)" uses up to 34 bytes, for very large values. So 250 isn't exaggerating.
//
//		Vector3i barrel(0, 0, 0);
//		const iIMDShape *psWeaponImd = nullptr, *psMountImd = nullptr;
//
//		if (psDroid->asWeaps[weapon_slot].nStat)
//		{
//			psMountImd = WEAPON_MOUNT_IMD(psDroid, weapon_slot);
//			psWeaponImd = WEAPON_IMD(psDroid, weapon_slot);
//		}
//
//		Affine3F af;
//
//		af.Trans(psDroid->getPosition().x, -psDroid->getPosition().z, psDroid->getPosition().y);
//
//		//matrix = the center of droid
//		af.RotY(psDroid->rot.direction);
//		af.RotX(psDroid->rot.pitch);
//		af.RotZ(-psDroid->rot.roll);
//		af.Trans(psBodyImd->connectors[weapon_slot].x, -psBodyImd->connectors[weapon_slot].z,
//		         -psBodyImd->connectors[weapon_slot].y); //note y and z flipped
//		debugLen += sprintf(debugStr + debugLen, "connect:body[%d]=(%d,%d,%d)", weapon_slot,
//		                    psBodyImd->connectors[weapon_slot].x, -psBodyImd->connectors[weapon_slot].z,
//		                    -psBodyImd->connectors[weapon_slot].y);
//
//		//matrix = the weapon[slot] mount on the body
//		af.RotY(psDroid->asWeaps[weapon_slot].rot.direction); // +ve anticlockwise
//
//		// process turret mount
//		if (psMountImd && psMountImd->nconnectors)
//		{
//			af.Trans(psMountImd->connectors->x, -psMountImd->connectors->z, -psMountImd->connectors->y);
//			debugLen += sprintf(debugStr + debugLen, ",turret=(%d,%d,%d)", psMountImd->connectors->x,
//			                    -psMountImd->connectors->z, -psMountImd->connectors->y);
//		}
//
//		//matrix = the turret connector for the gun
//		af.RotX(psDroid->asWeaps[weapon_slot].rot.pitch); // +ve up
//
//		//process the gun
//		if (psWeaponImd && psWeaponImd->nconnectors)
//		{
//			unsigned int connector_num = 0;
//
//			// which barrel is firing if model have multiple muzzle connectors?
//			if (psDroid->asWeaps[weapon_slot].shotsFired && (psWeaponImd->nconnectors > 1))
//			{
//				// shoot first, draw later - substract one shot to get correct results
//				connector_num = (psDroid->asWeaps[weapon_slot].shotsFired - 1) % (psWeaponImd->nconnectors);
//			}
//
//			barrel = Vector3i(psWeaponImd->connectors[connector_num].x,
//			                  -psWeaponImd->connectors[connector_num].z,
//			                  -psWeaponImd->connectors[connector_num].y);
//			debugLen += sprintf(debugStr + debugLen, ",barrel[%u]=(%d,%d,%d)", connector_num,
//			                    psWeaponImd->connectors[connector_num].x, -psWeaponImd->connectors[connector_num].y,
//			                    -psWeaponImd->connectors[connector_num].z);
//		}
//
//		*muzzle = (af * barrel).xzy();
//		muzzle->z = -muzzle->z;
//		sprintf(debugStr + debugLen, ",muzzle=(%d,%d,%d)", muzzle->x, muzzle->y, muzzle->z);
//
//		syncDebug("%s", debugStr);
//	}
//	else
//	{
//		*muzzle = psDroid->getPosition() + Vector3i(0, 0, psDroid->sDisplay.imd->max.y);
//	}
//
//	CHECK_DROID(psDroid);
//
//	return true;
//}

struct rankMap
{
	unsigned int kills; // required minimum amount of kills to reach this rank
	unsigned int commanderKills; // required minimum amount of kills for a commander (or sensor) to reach this rank
	const char* name; // name of this rank
};


//unsigned int getDroidLevel(const DROID *psDroid)
//{
//	unsigned int numKills = psDroid->experience / 65536;
//	unsigned int i;
//
//	// Search through the array of ranks until one is found
//	// which requires more kills than the droid has.
//	// Then fall back to the previous rank.
//	const BRAIN_STATS *psStats = getBrainStats(psDroid);
//	auto &vec = psStats->upgrade[psDroid->player].rankThresholds;
//	for (i = 1; i < vec.size(); ++i)
//	{
//		if (numKills < vec.at(i))
//		{
//			return i - 1;
//		}
//	}
//
//	// If the criteria of the last rank are met, then select the last one
//	return vec.size() - 1;
//}

unsigned get_effective_level(Droid const& droid)
{
  const auto level = droid.getLevel();
  if (!droid.hasCommander()) {
    return level;
  }

  const auto cmd_level = droid.getCommanderLevel();
  if (cmd_level > level + 1) {
    return cmd_level;
  }
  return level;
}

unsigned count_droids_for_level(unsigned player, unsigned level)
{
  auto const& droids = playerList[player].droids;
  return std::count_if(droids.begin(), droids.end(),
                       [level](auto const& droid) {
    return droid.getLevel() == level;
  });
}

//unsigned	getNumDroidsForLevel(uint32_t player, unsigned level)
//{
//	DROID	*psDroid;
//	unsigned	count;
//
//	if (player >= MAX_PLAYERS) { return 0; }
//
//	for (psDroid = apsDroidLists[player], count = 0;
//		 psDroid; psDroid = psDroid->psNext)
//	{
//		if (getDroidLevel(psDroid) == level)
//		{
//			count++;
//		}
//	}
//
//	return count;
//}

//// returns true when no droid on x,y square.
//bool noDroid(unsigned x, unsigned y)
//{
//	unsigned int i;
//
//	// check each droid list
//	for (i = 0; i < MAX_PLAYERS; ++i)
//	{
//		const DROID *psDroid;
//		for (psDroid = apsDroidLists[i]; psDroid; psDroid = psDroid->psNext)
//		{
//			if (map_coord(psDroid->getPosition().x) == x
//				&& map_coord(psDroid->getPosition().y) == y)
//			{
//				return false;
//			}
//		}
//	}
//	return true;
//}

// returns true when at most one droid on x,y square.
static bool oneDroidMax(unsigned x, unsigned y)
{
	// check each droid list
  bool bFound = false;
	for (auto i = 0; i < MAX_PLAYERS; i++)
	{
		for (auto const& pD : playerList[i].droids)
		{
			if (map_coord(pD.getPosition().x) == x &&
          map_coord(pD.getPosition().y) == y) {
				if (bFound) return false;
				bFound = true; //first droid on this square so continue
			}
		}
	}
	return true;
}

bool sensiblePlace(int x, int y, PROPULSION_TYPE propulsion)
{
  if (x < TOO_NEAR_EDGE || x > mapWidth - TOO_NEAR_EDGE ||
      y < TOO_NEAR_EDGE || y > mapHeight - TOO_NEAR_EDGE)  {
    return false;
  }

  if (fpathBlockingTile(x, y, propulsion)) {
    return false;
  }
  return true;
}

// Should stop things being placed in inaccessible areas? Assume wheeled propulsion.
bool zonedPAT(unsigned x, unsigned y)
{
	return sensiblePlace(x, y, PROPULSION_TYPE::WHEELED) && noDroid(x, y);
}

static bool canFitDroid(unsigned x, unsigned y)
{
	return sensiblePlace(x, y, PROPULSION_TYPE::WHEELED) && oneDroidMax(x, y);
}

/// find a tile for which the function will return true
bool pickATileGen(unsigned* x, unsigned* y, UBYTE numIterations,
                  bool (*function)(unsigned x, unsigned y))
{
	return pickATileGenThreat(x, y, numIterations, -1, -1, function);
}

bool pickATileGen(Vector2i* pos, unsigned numIterations, bool (*function)(unsigned x, unsigned y))
{
	unsigned x = pos->x, y = pos->y;
	bool ret = pickATileGenThreat(&x, &y, numIterations, -1, -1, function);
	*pos = Vector2i(x, y);
	return ret;
}

static bool ThreatInRange(SDWORD player, SDWORD range, SDWORD rangeX, SDWORD rangeY, bool bVTOLs)
{
	const auto tx = map_coord(rangeX);
	const auto ty = map_coord(rangeY);

	for (auto i = 0; i < MAX_PLAYERS; i++)
	{
		if ((alliances[player][i] == ALLIANCE_FORMED) || (i == player)) {
			continue;
		}

		//check structures
		for (auto& psStruct : playerList[i].structures)
		{
      if (!psStruct.isVisibleToPlayer(player) && psStruct.getBornTime() != 2) { // if can see it or started there
        continue;
      }
      if (psStruct.getState() == STRUCTURE_STATE::BUILT) {
        auto structType = psStruct.getStats()->type;

        using enum STRUCTURE_TYPE;
        switch (structType) {
          //dangerous to get near these structures
          case DEFENSE:
          case CYBORG_FACTORY:
          case FACTORY:
          case VTOL_FACTORY:
          case REARM_PAD:
            if (range < 0 || world_coord(
              static_cast<int>(
                hypotf(tx - map_coord(psStruct.getPosition().x),
                       ty - map_coord(psStruct.getPosition().y)))) < range) {
              //enemy in range
              return true;
            }
            break;
          default:
            break;
        }
      }
    }

		//check droids
		for (auto& psDroid : playerList[i].droids)
		{
			if (psDroid.isVisibleToPlayer(player)) //can see this droid? {
				if (!objHasWeapon(&psDroid)) {
					continue;
				}

        auto propulsion = dynamic_cast<PropulsionStats const*>(psDroid.getComponent(COMPONENT_TYPE::PROPULSION));
				//if VTOLs are excluded, skip them
				if (!bVTOLs && propulsion &&
            propulsion->propulsionType == PROPULSION_TYPE::LIFT ||
            isTransporter(psDroid)) {
					continue;
				}

				if (range < 0 || world_coord(
						static_cast<int>(hypotf(
                    tx - map_coord(psDroid.getPosition().x),
                    ty - map_coord(psDroid.getPosition().y)))) < range) {
          //enemy in range
					return true;
				}
			}
		}
  return false;
}

/// find a tile for which the passed function will return true without any threat in the specified range
bool pickATileGenThreat(unsigned* x, unsigned* y, UBYTE numIterations, int threatRange,
                        unsigned player, bool (*function)(unsigned x, unsigned y))
{
	int startX, endX, startY, endY;
	unsigned passes;
	Vector3i origin(world_coord(*x), world_coord(*y), 0);

	ASSERT_OR_RETURN(false, *x < mapWidth, "x coordinate is off-map for pickATileGen");
	ASSERT_OR_RETURN(false, *y < mapHeight, "y coordinate is off-map for pickATileGen");

	if (function(*x, *y) && ((threatRange <= 0) ||
      (!ThreatInRange(player, threatRange, *x, *y, false)))) {
    //TODO: vtol check really not needed?
		return (true);
	}

	/* Initial box dimensions and set iteration count to zero */
	startX = endX = *x;
	startY = endY = *y;
	passes = 0;

	/* Keep going until we get a tile or we exceed distance */
	while (passes < numIterations)
	{
		/* Process whole box */
		for (auto i = startX; i <= endX; i++)
		{
			for (auto j = startY; j <= endY; j++)
			{
				/* Test only perimeter as internal tested previous iteration */
				if (i == startX || i == endX || j == startY || j == endY)
				{
					Vector3i newPos(world_coord(i), world_coord(j), 0);

					/* Good enough? */
					if (function(i, j) &&
						  fpathCheck(origin, newPos, PROPULSION_TYPE::WHEELED) &&
						  ((threatRange <= 0) || (!ThreatInRange(
                    player, threatRange, world_coord(i),
                    world_coord(j), false)))) {
						/* Set exit conditions and get out NOW */
						*x = i;
						*y = j;
						return true;
					}
				}
			}
		}
		/* Expand the box out in all directions - off map handled by tileAcceptable */
		startX--;
		startY--;
		endX++;
		endY++;
		passes++;
	}
	/* If we got this far, then we failed - passed in values will be unchanged */
	return false;
}

/// find a tile for a wheeled droid with only one other droid present
PICK_TILE pickHalfATile(unsigned* x, unsigned* y, UBYTE numIterations)
{
  using enum PICK_TILE;
	return pickATileGen(x, y, numIterations, canFitDroid) ? FREE_TILE : NO_FREE_TILE;
}

///* Looks through the players list of droids to see if any of them are
//building the specified structure - returns true if finds one*/
//bool checkDroidsBuilding(STRUCTURE *psStructure)
//{
//	DROID				*psDroid;
//
//	for (psDroid = apsDroidLists[psStructure->player]; psDroid != nullptr; psDroid =
//			 psDroid->psNext)
//	{
//		//check DORDER_BUILD, HELP_BUILD is handled the same
//		SimpleObject *const psStruct = orderStateObj(psDroid, DORDER_BUILD);
//		if ((STRUCTURE *)psStruct == psStructure)
//		{
//			return true;
//		}
//	}
//	return false;
//}

///* Looks through the players list of droids to see if any of them are
//demolishing the specified structure - returns true if finds one*/
//bool checkDroidsDemolishing(STRUCTURE *psStructure)
//{
//	DROID				*psDroid;
//
//	for (psDroid = apsDroidLists[psStructure->player]; psDroid != nullptr; psDroid =
//			 psDroid->psNext)
//	{
//		//check DORDER_DEMOLISH
//		SimpleObject *const psStruct = orderStateObj(psDroid, DORDER_DEMOLISH);
//		if ((STRUCTURE *)psStruct == psStructure)
//		{
//			return true;
//		}
//	}
//	return false;
//}

int nextModuleToBuild(Structure const* psStruct, int lastOrderedModule)
{
	auto order = 0;
	auto i = 0;

	auto next = psStruct->getState() == STRUCTURE_STATE::BUILT ? 1 : 0;
	// If complete, next is one after the current number of modules, otherwise next is the one we're working on.
	int max;
  using enum STRUCTURE_TYPE;
	switch (psStruct->getStats()->type) {
	case POWER_GEN:
		//check room for one more!
		max = std::max<int>(psStruct->getCapacity() + next, lastOrderedModule + 1);
		if (max <= 1) {
			i = powerModuleStat;
			order = max;
		}
		break;
	case FACTORY:
	case VTOL_FACTORY:
		//check room for one more!
		max = std::max<int>(psStruct->getCapacity() + next, lastOrderedModule + 1);
		if (max <= NUM_FACTORY_MODULES) {
			i = factoryModuleStat;
			order = max;
		}
		break;
	case RESEARCH:
		//check room for one more!
		max = std::max<int>(psStruct->getCapacity() + next, lastOrderedModule + 1);
		if (max <= 1) {
			i = researchModuleStat;
			order = max; // Research modules are weird. Build one, get three free.
		}
		break;
	default:
		//no other structures can have modules attached
		break;
	}

	if (order) {
		// Check availability of Module
		if (!((i < numStructureStats) &&
			(apStructTypeLists[psStruct->playerManager->getPlayer()][i] == AVAILABLE))) {
			order = 0;
		}
	}
	return order;
}


///* Just returns true if the droid's present body points aren't as high as the original*/
//bool	droidIsDamaged(const DROID *psDroid)
//{
//	return psDroid->body < psDroid->originalBody;
//}

char const* getDroidResourceName(char const* pName)
{
	// See if the name has a string resource associated with it by trying
	// to get the string resource.
	return strresGetString(psStringRes, pName);
}

bool being_repaired(Droid const& droid)
{
  if (!droid.isDamaged()) {
    return false;
  }

  const auto& droids = playerList[droid.playerManager->getPlayer()].droids;
  return std::any_of(droids.begin(), droids.end(),
                     [&droid](const auto& other_droid) {
    return other_droid.isRepairDroid() &&
           other_droid.getAction() == ACTION::DROID_REPAIR &&
           other_droid.getOrder()->target->getId() == droid.getId();
  });
}
//bool droidUnderRepair(const DROID *psDroid)
//{
//	CHECK_DROID(psDroid);
//
//	//droid must be damaged
//	if (droidIsDamaged(psDroid))
//	{
//		//look thru the list of players droids to see if any are repairing this droid
//		for (const DROID *psCurr = apsDroidLists[psDroid->player]; psCurr != nullptr; psCurr = psCurr->psNext)
//		{
//			if ((psCurr->droidType == DROID_REPAIR || psCurr->droidType ==
//				 DROID_CYBORG_REPAIR) && psCurr->action ==
//				DACTION_DROIDREPAIR && psCurr->order.psObj == psDroid)
//			{
//				return true;
//			}
//		}
//	}
//	return false;
//}

unsigned count_player_command_droids(unsigned player)
{
  const auto& droids = playerList[player].droids;
  return std::count_if(droids.begin(), droids.end(),
                       [](const auto& droid) {
    return droid.getType() == DROID_TYPE::COMMAND;
  });
}

bool isTransporter(Droid const& droid)
{
  using enum DROID_TYPE;
  return droid.getType() == TRANSPORTER ||
         droid.getType() == SUPER_TRANSPORTER;
}

bool isTransporter(DroidTemplate const& templ)
{
  using enum DROID_TYPE;
  return templ.type == TRANSPORTER ||
         templ.type == SUPER_TRANSPORTER;
}

void Droid::orderDroidCmd(ORDER_TYPE order, QUEUE_MODE mode)
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Droid object is undefined");
  orderDroidObj(this, order, pimpl->commander, mode);
}

bool vtolEmpty(Droid const& droid)
{
  ASSERT_OR_RETURN(false, droid.isVtol(), "Not a VTOL");
  if (droid.getType() != DROID_TYPE::WEAPON) {
    return false;
  }
  return std::all_of(droid.weaponManager->weapons.begin(),
                     droid.weaponManager->weapons.end(),
                     [&droid](const auto& weapon) {
    return weapon.isVtolWeapon() &&
           weapon.isEmptyVtolWeapon(droid.playerManager->getPlayer());
  });
}

bool vtolFull(Droid const& droid)
{
  ASSERT_OR_RETURN(false, droid.isVtol(), "Not a VTOL");
  if (droid.getType() != DROID_TYPE::WEAPON) {
    return false;
  }
  return std::all_of(droid.weaponManager->weapons.begin(),
                     droid.weaponManager->weapons.end(),
                     [](const auto& weapon) {
      return weapon.isVtolWeapon() && weapon.hasFullAmmo();
  });
}

bool vtolReadyToRearm(Droid const& droid, RearmPad const& rearmPad)
{
  if (droid.isVtol() || droid.getAction() == ACTION::WAIT_FOR_REARM ||
      !vtolHappy(droid) || rearmPad.isClear() || !vtolRearming(droid)) {
    return true;
  }
  return false;
}

//// true if a vtol is waiting to be rearmed by a particular rearm pad
//bool vtolReadyToRearm(DROID *psDroid, STRUCTURE *psStruct)
//{
//	CHECK_DROID(psDroid);
//
//	if (!isVtolDroid(psDroid) || psDroid->action != DACTION_WAITFORREARM)
//	{
//		return false;
//	}
//
//	// If a unit has been ordered to rearm make sure it goes to the correct base
//	STRUCTURE *psRearmPad = castStructure(orderStateObj(psDroid, DORDER_REARM));
//	if (psRearmPad && psRearmPad != psStruct && !vtolOnRearmPad(psRearmPad, psDroid))
//	{
//		// target rearm pad is clear - let it go there
//		objTrace(psDroid->id, "rearm pad at %d,%d won't snatch us - we already have another available at %d,%d", psStruct->getPosition().x / TILE_UNITS, psStruct->getPosition().y / TILE_UNITS, psRearmPad->getPosition().x / TILE_UNITS, psRearmPad->getPosition().y / TILE_UNITS);
//		return false;
//	}
//
//	if (vtolHappy(psDroid) && vtolOnRearmPad(psStruct, psDroid))
//	{
//		// there is a vtol on the pad and this vtol is already rearmed
//		// don't bother shifting the other vtol off
//		objTrace(psDroid->id, "rearm pad at %d,%d won't snatch us - we're rearmed and pad is busy", psStruct->getPosition().x / TILE_UNITS, psStruct->getPosition().y / TILE_UNITS);
//		return false;
//	}
//
//	STRUCTURE *psTarget = castStructure(psDroid->psActionTarget[0]);
//	if (psTarget && psTarget->pFunctionality && psTarget->pFunctionality->rearmPad.psObj == psDroid)
//	{
//		// vtol is rearming at a different base, leave it alone
//		objTrace(psDroid->id, "rearm pad at %d,%d won't snatch us - we already are snatched by %d,%d", psStruct->getPosition().x / TILE_UNITS, psStruct->getPosition().y / TILE_UNITS, psTarget->getPosition().x / TILE_UNITS, psTarget->getPosition().y / TILE_UNITS);
//		return false;
//	}
//
//	return true;
//}

bool vtolRearming(Droid const& droid)
{
  if (!droid.isVtol() || droid.getType() != DROID_TYPE::WEAPON) {
    return false;
  }

  auto action = droid.getAction();
  using enum ACTION;
  if (action == MOVE_TO_REARM || action == WAIT_FOR_REARM ||
      action == MOVE_TO_REARM_POINT || action == WAIT_DURING_REARM) {
    return true;
  }
  return false;
}

bool allVtolsRearmed(Droid const& droid)
{
  if (!droid.isVtol())  {
    return true;
  }

  const auto& droids = playerList[droid.playerManager->getPlayer()].droids;
  return std::none_of(droids.begin(), droids.end(),
                      [&droid](const auto& other_droid) {
      return vtolRearming(other_droid) &&
             other_droid.getOrder()->type == droid.getOrder()->type &&
             other_droid.getOrder()->target == droid.getOrder()->target;
  });
}

//// see if there are any other vtols attacking the same target
//// but still rearming
//bool allVtolsRearmed(const DROID *psDroid)
//{
//	CHECK_DROID(psDroid);
//
//	// ignore all non vtols
//	if (!isVtolDroid(psDroid))
//	{
//		return true;
//	}
//
//	bool stillRearming = false;
//	for (const DROID *psCurr = apsDroidLists[psDroid->player]; psCurr; psCurr = psCurr->psNext)
//	{
//		if (vtolRearming(psCurr) &&
//			psCurr->order.type == psDroid->order.type &&
//			psCurr->order.psObj == psDroid->order.psObj)
//		{
//			stillRearming = true;
//			break;
//		}
//	}
//
//	return !stillRearming;
//}

/*returns a count of the base number of attack runs for the weapon attached to the droid*/
unsigned getNumAttackRuns(Droid const* psDroid, int weapon_slot)
{
	ASSERT_OR_RETURN(0, psDroid->isVtol(), "not a VTOL Droid");
	// if weapon is a salvo weapon, then number of shots that can be fired = vtolAttackRuns * numRounds
	if (psDroid->weaponManager->weapons[weapon_slot].stats.get()->upgraded[psDroid->playerManager->getPlayer()].reloadTime) {
		return psDroid->weaponManager->weapons[weapon_slot].stats.get()->upgraded[psDroid->playerManager->getPlayer()].numRounds
			* psDroid->weaponManager->weapons[weapon_slot].stats.get()->vtolAttackRuns;
	}
	return psDroid->weaponManager->weapons[weapon_slot].stats.get()->vtolAttackRuns;
}

bool vtolHappy(Droid const& droid)
{
  assert(droid.isVtol());
  if (droid.isDamaged() || !hasFullAmmo(droid) ||
       droid.getType() == DROID_TYPE::WEAPON) {
    return false;
  }
  return true;
}
///*Checks a vtol for being fully armed and fully repaired to see if ready to
//leave reArm pad */
//bool vtolHappy(const DROID *psDroid)
//{
//	CHECK_DROID(psDroid);
//
//	ASSERT_OR_RETURN(false, isVtolDroid(psDroid), "not a VTOL droid");
//
//	if (psDroid->body < psDroid->originalBody)
//	{
//		// VTOLs with less health than their original aren't happy
//		return false;
//	}
//
//	if (psDroid->droidType != DROID_WEAPON)
//	{
//		// Not an armed droid, so don't check the (non-existent) weapons
//		return true;
//	}
//
//	/* NOTE: Previous code (r5410) returned false if a droid had no weapon,
//	 *       which IMO isn't correct, but might be expected behaviour. I'm
//	 *       also not sure if weapon droids (see the above droidType check)
//	 *       can even have zero weapons. -- Giel
//	 */
//	ASSERT_OR_RETURN(false, psDroid->numWeaps > 0, "VTOL weapon droid without weapons found!");
//
//	//check full complement of ammo
//	for (int i = 0; i < psDroid->numWeaps; ++i)
//	{
//		if (asWeaponStats[psDroid->asWeaps[i].nStat].vtolAttackRuns > 0
//			&& psDroid->asWeaps[i].usedAmmo != 0)
//		{
//			return false;
//		}
//	}
//
//	return true;
//}

void updateVtolAttackRun(Droid& droid, int weapon_slot)
{
  if (!droid.isVtol() || numWeapons(droid) == 0) {
    return;
  }

  auto weapon = droid.weaponManager->weapons[weapon_slot];
  if (weapon.stats.get()->vtolAttackRuns == 0) {
    return;
  }
  ++droid.weaponManager->weapons[weapon_slot].ammoUsed;
}

///*checks if the droid is a VTOL droid and updates the attack runs as required*/
//void updateVtolAttackRun(DROID* psDroid, int weapon_slot)
//{
//	if (isVtolDroid(psDroid))
//	{
//		if (psDroid->numWeaps > 0)
//		{
//			if (asWeaponStats[psDroid->asWeaps[weapon_slot].nStat].vtolAttackRuns > 0)
//			{
//				++psDroid->asWeaps[weapon_slot].usedAmmo;
//				if (psDroid->asWeaps[weapon_slot].usedAmmo == getNumAttackRuns(psDroid, weapon_slot))
//				{
//					psDroid->asWeaps[weapon_slot].ammo = 0;
//				}
//				//quick check doesn't go over limit
//				ASSERT(psDroid->asWeaps[weapon_slot].usedAmmo < UWORD_MAX, "too many attack runs");
//			}
//		}
//	}
//}

////assign rearmPad to the VTOL
//void assignVTOLPad(DROID* psNewDroid, STRUCTURE* psReArmPad)
//{
//	ASSERT_OR_RETURN(, isVtolDroid(psNewDroid), "%s is not a VTOL droid", objInfo(psNewDroid));
//	ASSERT_OR_RETURN(, psReArmPad->type == OBJ_STRUCTURE && psReArmPad->pStructureType->type == REF_REARM_PAD,
//	                   "%s cannot rearm", objInfo(psReArmPad));
//
//	setDroidBase(psNewDroid, psReArmPad);
//}



//// return whether a droid has a CB sensor on it
//bool cbSensorDroid(const DROID* psDroid)
//{
//	if (psDroid->droidType != DROID_SENSOR)
//	{
//		return false;
//	}
//	if (asSensorStats[psDroid->asBits[COMPONENT_TYPE::SENSOR]].type == VTOL_CB_SENSOR
//		|| asSensorStats[psDroid->asBits[COMPONENT_TYPE::SENSOR]].type == INDIRECT_CB_SENSOR)
//	{
//		return true;
//	}
//
//	return false;
//}

//// return whether a droid has a standard sensor on it (standard, VTOL strike, or wide spectrum)
//bool standardSensorDroid(const DROID* psDroid)
//{
//	if (psDroid->droidType != DROID_SENSOR)
//	{
//		return false;
//	}
//	if (asSensorStats[psDroid->asBits[COMPONENT_TYPE::SENSOR]].type == VTOL_INTERCEPT_SENSOR
//		|| asSensorStats[psDroid->asBits[COMPONENT_TYPE::SENSOR]].type == STANDARD_SENSOR
//		|| asSensorStats[psDroid->asBits[COMPONENT_TYPE::SENSOR]].type == SUPER_SENSOR)
//	{
//		return true;
//	}
//
//	return false;
//}

///*calculates the electronic resistance of a droid based on its experience level*/
//SWORD droidResistance(const DROID* psDroid)
//{
//	CHECK_DROID(psDroid);
//	const BODY_STATS* psStats = asBodyStats + psDroid->asBits[COMPONENT_TYPE::BODY];
//	int resistance = psDroid->experience / (65536 / MAX(1, psStats->upgrade[psDroid->player].resistance));
//	// ensure resistance is a base minimum
//	resistance = MAX(resistance, psStats->upgrade[psDroid->player].resistance);
//	return MIN(resistance, INT16_MAX);
//}

/*this is called to check the weapon is 'allowed'. Check if VTOL, the weapon is
direct fire. Also check numVTOLattackRuns for the weapon is not zero - return
true if valid weapon*/
/* this will be buggy if the droid being checked has both AA weapon and non-AA weapon
Cannot think of a solution without adding additional return value atm.
*/
bool checkValidWeaponForProp(DroidTemplate const* psTemplate)
{
	//check propulsion stat for vtol
	auto psPropStats = dynamic_cast<PropulsionStats const*>(psTemplate->getComponent(COMPONENT_TYPE::PROPULSION));

	ASSERT_OR_RETURN(false, psPropStats != nullptr, "invalid propulsion stats pointer");

	// if there are no weapons, then don't even bother continuing
	if (psTemplate->weapons.empty()) {
		return false;
	}

	if (asPropulsionTypes[(int)psPropStats->propulsionType].travel == TRAVEL_MEDIUM::AIR) {
		//check weapon stat for indirect
		if (!proj_Direct(psTemplate->weapons[0].stats.get()) ||
        !psTemplate->weapons[0].stats.get()->vtolAttackRuns) {
			return false;
		}
	}
	else {
		// VTOL weapons do not go on non-AIR units
		if (psTemplate->weapons[0].stats.get()->vtolAttackRuns) {
			return false;
		}
	}

	//also checks that there is no other system component
	if (psTemplate->getComponent(COMPONENT_TYPE::BRAIN) != nullptr &&
      psTemplate->weapons[0].stats.get()->weaponSubClass != WEAPON_SUBCLASS::COMMAND) {
		assert(false);
		return false;
	}
	return true;
}

// Check if a droid can be selected.
bool isSelectable(Droid const* psDroid)
{
	if (psDroid->testFlag(static_cast<size_t>(OBJECT_FLAG::UNSELECTABLE))) {
		return false;
	}

	// we shouldn't ever control the transporter in SP games
	if (isTransporter(*psDroid) && !bMultiPlayer) {
		return false;
	}

	return true;
}

// Select a droid and do any necessary housekeeping.
//
void SelectDroid(Droid* psDroid)
{
	if (!isSelectable(psDroid)) {
		return;
	}
	psDroid->damageManager->setSelected(true);
	intRefreshScreen();
	triggerEventSelected();
	jsDebugSelected(psDroid);
}

// De-select a droid and do any necessary housekeeping.
//
void DeSelectDroid(Droid* psDroid)
{
	psDroid->damageManager->setSelected(false);
	intRefreshScreen();
	triggerEventSelected();
}

/**
 * Callback function for stopped audio tracks
 * Sets the droid's current track id to NO_SOUND
 *
 * @return true on success, false on failure
 */
bool droidAudioTrackStopped(Droid* psDroid)
{
	if (psDroid == nullptr) {
		debug(LOG_ERROR, "droid pointer invalid");
		return false;
	}
	if (!dynamic_cast<Droid*>(psDroid)|| psDroid->damageManager->isDead()) {
		return false;
	}

	psDroid->setAudioId(NO_SOUND);
	return true;
}

bool isCyborg(const Droid& droid)
{
  using enum DROID_TYPE;
  return droid.getType() == CYBORG ||
         droid.getType() == CYBORG_CONSTRUCT ||
         droid.getType() == CYBORG_REPAIR ||
         droid.getType() == CYBORG_SUPER;
}

bool isBuilder(const Droid& droid)
{
  using enum DROID_TYPE;
  return droid.getType() == CONSTRUCT ||
         droid.getType() == CYBORG_CONSTRUCT;
}

bool droidOnMap(const Droid* psDroid)
{
	if (psDroid->damageManager->getTimeOfDeath() == NOT_CURRENT_LIST ||
      isTransporter(*psDroid) ||
      psDroid->getPosition().x == INVALID_XY ||
      psDroid->getPosition().y == INVALID_XY ||
      missionIsOffworld() ||
      mapHeight == 0) {
		// off world or on a transport or is a transport or
    // in mission list, or on a mission, or no map - ignore
		return true;
	}
	return worldOnMap(psDroid->getPosition().x, psDroid->getPosition().y);
}

/** Teleport a droid to a new position on the map */
void droidSetPosition(Droid* psDroid, Vector2i pos)
{
  psDroid->setPosition({pos.x, pos.y, map_Height(psDroid->getPosition().x, psDroid->getPosition().y)});
	initDroidMovement(psDroid);
	visTilesUpdate(psDroid);
}

int droidSqDist(Droid const* psDroid, BaseObject const* psObj)
{
	auto psPropStats = dynamic_cast<PropulsionStats const*>(psDroid->getComponent(COMPONENT_TYPE::PROPULSION));

	if (psPropStats && !fpathCheck(psDroid->getPosition(), psObj->getPosition(), psPropStats->propulsionType)) {
		return -1;
	}
	return objectPositionSquareDiff(psDroid->getPosition(), psObj->getPosition());
}

unsigned calculate_max_range(Droid const& droid)
{
  if (droid.getType() == DROID_TYPE::SENSOR)
    return droid.calculateSensorRange();
  if (numWeapons(droid) == 0)
    return 0;

  return getMaxWeaponRange(droid);
}

bool transporter_is_flying(Droid const& transporter)
{
  assert(isTransporter(transporter));
  auto order = transporter.getOrder();

  if (bMultiPlayer) {
    return order->type == ORDER_TYPE::MOVE || order->type == ORDER_TYPE::DISEMBARK ||
           order->type == ORDER_TYPE::NONE && transporter.getVerticalSpeed() != 0;
  }

  return order->type == ORDER_TYPE::TRANSPORT_OUT ||
         order->type == ORDER_TYPE::TRANSPORT_IN ||
         order->type == ORDER_TYPE::TRANSPORT_RETURN;
}

bool still_building(Droid const& droid)
{
  return !droid.damageManager->isDead() && droid.getAction() == ACTION::BUILD;
}

bool can_assign_fire_support(Droid const& droid, Structure const& structure)
{
  if (numWeapons(droid) == 0 || !structure.hasSensor()) {
    return false;
  }

  if (droid.isVtol()) {
    return structure.hasVtolInterceptSensor() || structure.hasVtolCbSensor();
  }
  if (hasArtillery(droid)) {
    return structure.hasStandardSensor() || structure.hasCbSensor();
  }
  return false;
}

bool vtolCanLandHere(int x, int y)
{
  if (x < 0 || x >= mapWidth || y < 0 || y >= mapHeight) {
    return false;
  }

  const auto tile = mapTile(x, y);
  if (tile->tileInfoBits & AUXBITS_BLOCKING || TileIsOccupied(tile) ||
      terrainType(tile) == TER_CLIFFFACE || terrainType(tile) == TER_WATER) {
    return false;
  }
  return true;
}

Vector2i choose_landing_position(const Droid& vtol, Vector2i position)
{
  auto start_pos = Vector2i{map_coord(position.x), map_coord(position.y)};

  set_blocking_flags(vtol);

  auto landing_tile = spiralSearch(start_pos, VTOL_LANDING_RADIUS);
  landing_tile.x = world_coord(landing_tile.x) + TILE_UNITS / 2;
  landing_tile.y = world_coord(landing_tile.y) + TILE_UNITS / 2;

  clear_blocking_flags(vtol);
  return landing_tile;
}

Droid* find_nearest_droid(unsigned x, unsigned y, bool selected)
{
  auto& droids = playerList[selectedPlayer].droids;
  Droid* nearest_droid = nullptr;
  auto shortest_distance = UINT32_MAX;

  std::for_each(droids.begin(), droids.end(), [&](auto& droid) {
    if (droid.isVtol() || selected && !droid.damageManager->isSelected())
      return;

    auto const distance = iHypot(droid.getPosition().x - x, droid.getPosition().y - y);
    if (distance < shortest_distance) {
      shortest_distance = distance;
      nearest_droid = &droid;
    }
  });
  return nearest_droid;
}

void set_blocking_flags(Droid const& droid, uint8_t flag)
{
  const auto &droids = playerList[droid.playerManager->getPlayer()].droids;
  std::for_each(droids.begin(), droids.end(),
                [&](auto const& other_droid) {
    static Vector2i tile{0, 0};
    if (&droid == &other_droid)
      return;

    tile = other_droid.isStationary()
           ? map_coord(other_droid.getPosition().xy())
           : map_coord(other_droid.getDestination());

    if (tileOnMap(tile)) {
      mapTile(tile)->tileInfoBits |= flag;
    }
  });
}

void clear_blocking_flags(Droid const& droid, uint8_t flag)
{
  auto const& droids = playerList[droid.playerManager->getPlayer()].droids;
  std::for_each(droids.begin(), droids.end(),
                [&](auto const& other_droid) {
    static Vector2i tile{0, 0};
    if (&droid == &other_droid)
      return;

    tile = other_droid.isStationary()
           ? map_coord(other_droid.getPosition().xy())
           : map_coord(other_droid.getDestination());

    if (tileOnMap(tile)) {
      mapTile(tile)->tileInfoBits &= ~flag;
    }
  });
}

bool tile_occupied_by_droid(unsigned x, unsigned y)
{
  for (auto& player : playerList)
  {
    if (std::any_of(player.droids.begin(), player.droids.end(),
                    [x, y](auto const& droid)  {
        return map_coord(droid.getPosition().x) == x &&
               map_coord(droid.getPosition().y == y);
    })) return true;
  }
  return false;
}

void Droid::setOrderTarget(BaseObject* psNewTarget)
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Droid object is undefined");
  pimpl->order->target = psNewTarget;
}

static unsigned droidSensorRange(Droid const* psDroid)
{
  return objSensorRange(psDroid);
}

static Rotation getInterpolatedWeaponRotation(Droid const* psDroid, int weaponSlot, unsigned time)
{
  return interpolateRot(psDroid->weaponManager->weapons[weaponSlot].previousRotation,
                        psDroid->weaponManager->weapons[weaponSlot].getRotation(),
                        psDroid->getPreviousLocation().time,
                        psDroid->getTime(), time);
}

bool Droid::loadSaveDroid(const char* pFileName)
{
  if (!PHYSFS_exists(pFileName)) {
    debug(LOG_SAVE, "No %s found -- use fallback method", pFileName);
    return false; // try to use fallback method
  }
  WzString fName = WzString::fromUtf8(pFileName);
  WzConfig ini(fName, WzConfig::ReadOnly);
  std::vector<WzString> list = ini.childGroups();
  // Sort list so transports are loaded first, since they must be loaded before the droids they contain.
  std::vector<std::pair<int, WzString>> sortedList;
  bool missionList = fName.compare("mdroid");
  for (auto i = 0; i < list.size(); ++i)
  {
    ini.beginGroup(list[i]);
    DROID_TYPE droidType = (DROID_TYPE)ini.value("droidType").toInt();
    int priority = 0;
    switch (droidType) {
      case DROID_TYPE::TRANSPORTER:
        ++priority; // fallthrough
      case DROID_TYPE::SUPER_TRANSPORTER:
        ++priority; // fallthrough
      case DROID_TYPE::COMMAND:
        //Don't care about sorting commanders in the mission list for safety missions. They
        //don't have a group to command and it messes up the order of the list sorting them
        //which causes problems getting the first transporter group for Gamma-1.
        if (!missionList || (missionList && !getDroidsToSafetyFlag())) {
          ++priority;
        }
      default:
        break;
    }
    sortedList.push_back(std::make_pair(-priority, list[i]));
    ini.endGroup();
  }
  std::sort(sortedList.begin(), sortedList.end());

  for (auto& pair : sortedList)
  {
    ini.beginGroup(pair.second);
    auto player = getPlayer(ini);
    auto id = ini.value("id", -1).toInt();
    auto pos = ini.vector3i("position");
    auto rot = ini.vector3i("rotation");
    auto onMission = ini.value("onMission", false).toBool();
    DroidTemplate templ;
    const DroidTemplate* psTemplate = nullptr;

    if (skipForDifficulty(ini, player)) {
      ini.endGroup();
      continue;
    }

    if (ini.contains("template")) {
      // Use real template (for maps)
      WzString templName(ini.value("template").toWzString());
      psTemplate = getTemplateFromTranslatedNameNoPlayer(templName.toUtf8().c_str());
      if (psTemplate == nullptr) {
        debug(LOG_ERROR, "Unable to find template for %s for player %d -- unit skipped",
              templName.toUtf8().c_str(), player);
        ini.endGroup();
        continue;
      }
    }
    else {
      // Create fake template
      templ.name = ini.string("name", "UNKNOWN");
      templ.type = (DROID_TYPE)ini.value("droidType").toInt();
      ini.beginGroup("parts"); // the following is copy-pasted from loadSaveTemplate() -- fixme somehow
      templ.components.insert_or_assign(COMPONENT_TYPE::BODY, getCompFromName(COMPONENT_TYPE::BODY, ini.value("body", "ZNULLBODY").toWzString()));
      templ.components.insert_or_assign(COMPONENT_TYPE::BRAIN, getCompFromName(COMPONENT_TYPE::BRAIN, ini.value("brain", "ZNULLBRAIN").toWzString()));
      templ.components.insert_or_assign(COMPONENT_TYPE::PROPULSION, getCompFromName(COMPONENT_TYPE::PROPULSION, ini.value("propulsion", "ZNULLPROP").toWzString()));
      templ.components.insert_or_assign(COMPONENT_TYPE::REPAIR_UNIT, getCompFromName(COMPONENT_TYPE::REPAIR_UNIT, ini.value("repair", "ZNULLREPAIR").toWzString()));
      templ.components.insert_or_assign(COMPONENT_TYPE::ECM, getCompFromName(COMPONENT_TYPE::ECM, ini.value("ecm", "ZNULLECM").toWzString()));
      templ.components.insert_or_assign(COMPONENT_TYPE::SENSOR, getCompFromName(COMPONENT_TYPE::SENSOR, ini.value("sensor", "ZNULLSENSOR").toWzString()));
      templ.components.insert_or_assign(COMPONENT_TYPE::CONSTRUCT, getCompFromName(COMPONENT_TYPE::CONSTRUCT, ini.value("construct", "ZNULLCONSTRUCT").toWzString()));
      templ.weapons[0] = getCompFromName(COMPONENT_TYPE::WEAPON, ini.value("weapon/1", "ZNULLWEAPON").toWzString());
      templ.weapons[1] = getCompFromName(COMPONENT_TYPE::WEAPON, ini.value("weapon/2", "ZNULLWEAPON").toWzString());
      templ.weapons[2] = getCompFromName(COMPONENT_TYPE::WEAPON, ini.value("weapon/3", "ZNULLWEAPON").toWzString());
      ini.endGroup();
      psTemplate = &templ;
    }

    // If droid is on a mission, calling with the saved position might cause an assertion. Or something like that.
    if (!onMission) {
      pos.x = clip(pos.x, world_coord(1), world_coord(mapWidth - 1));
      pos.y = clip(pos.y, world_coord(1), world_coord(mapHeight - 1));
    }

    /* Create the Droid */
    turnOffMultiMsg(true);
    auto psDroid = reallyBuildDroid(psTemplate, pos, player, onMission, rot).get();
    ASSERT_OR_RETURN(false, psDroid != nullptr, "Failed to build unit %s", pair.second.toUtf8().c_str());
    turnOffMultiMsg(false);

    // Copy the values across
    if (id > 0) {
      psDroid->setId(id);
      // force correct ID, unless ID is set to eg -1, in which case we should keep new ID (useful for starting units in campaign)
    }
    ASSERT(id != 0, "Droid ID should never be zero here");
    // conditional check so that existing saved games don't break
    if (ini.contains("originalBody"))
    {
      // we need to set "originalBody" before setting "body", otherwise CHECK_DROID throws assertion errors
      // we cannot use droidUpgradeBody here to calculate "originalBody", because upgrades aren't loaded yet
      // so it's much simplier just store/retrieve originalBody value
      psDroid->damageManager->setOriginalHp(ini.value("originalBody").toInt());
    }
    psDroid->damageManager->setHp(healthValue(ini, psDroid->damageManager->getOriginalHp()));
    ASSERT(psDroid->damageManager->getHp() != 0, "%s : %d has zero hp!", pFileName, i);
    psDroid->pimpl->experience = ini.value("experience", 0).toInt();
    psDroid->pimpl->kills = ini.value("kills", 0).toInt();
    psDroid->pimpl->secondaryOrder = ini.value("secondaryOrder", psDroid->pimpl->secondaryOrder).toInt();
    psDroid->pimpl->secondaryOrderPending = psDroid->pimpl->secondaryOrder;
    psDroid->pimpl->action = (ACTION)ini.value("action", (int)ACTION::NONE).toInt();
    psDroid->pimpl->actionPos = ini.vector2i("action/pos");
    psDroid->pimpl->timeActionStarted = ini.value("actionStarted", 0).toInt();
    psDroid->pimpl->actionPointsDone = ini.value("actionPoints", 0).toInt();
    psDroid->damageManager->setResistance(ini.value("resistance", 0).toInt()); // zero resistance == no electronic damage
    psDroid->pimpl->lastFrustratedTime = ini.value("lastFrustratedTime", 0).toInt();

    // common SimpleObject info
    loadSaveObject(ini, psDroid);

    // copy the droid's weapon stats
    for (auto j = 0; j < numWeapons(*psDroid); j++)
    {
      if (psDroid->asWeaps[j].nStat > 0) {
        psDroid->weaponManager->weapons[j].ammo = ini.value("ammo/" + WzString::number(j)).toInt();
        psDroid->weaponManager->weapons[j].timeLastFired = ini.value("lastFired/" + WzString::number(j)).toInt();
        psDroid->weaponManager->weapons[j].shotsFired = ini.value("shotsFired/" + WzString::number(j)).toInt();
        psDroid->weaponManager->weapons[j].setRotation(ini.vector3i("rotation/" + WzString::number(j)));
      }
    }

    psDroid->setSelectionGroup(ini.value("group", UBYTE_MAX).toInt());
    auto aigroup = ini.value("aigroup", -1).toInt();
    if (aigroup >= 0) {
      auto psGroup = findGroupById(aigroup);
      psGroup->addDroid(psDroid);
      if (psGroup->getType() == GROUP_TYPE::TRANSPORTER) {
        psDroid->damageManager->setSelected(false); // Droid should be visible in the transporter interface.
        visRemoveVisibility(psDroid); // should not have visibility data when in a transporter
      }
    }
    else {
      if (isTransporter(*psDroid) || psDroid->getType() == DROID_TYPE::COMMAND) {
        Group* psGroup = addGroup(-1);
        psGroup->addDroid(psDroid);
      }
      else {
        psDroid->pimpl->group = nullptr;
      }
    }

    psDroid->pimpl->movement->status = (MOVE_STATUS)ini.value("moveStatus", 0).toInt();
    psDroid->pimpl->movement->pathIndex = ini.value("pathIndex", 0).toInt();
    const int numPoints = ini.value("pathLength", 0).toInt();
    psDroid->pimpl->movement->path.resize(numPoints);
    for (int j = 0; j < numPoints; j++)
    {
      psDroid->pimpl->movement->path[j] = ini.vector2i("pathNode/" + WzString::number(j));
    }
    psDroid->pimpl->movement->destination = ini.vector2i("moveDestination");
    psDroid->pimpl->movement->src = ini.vector2i("moveSource");
    psDroid->pimpl->movement->target = ini.vector2i("moveTarget");
    psDroid->pimpl->movement->speed = ini.value("moveSpeed").toInt();
    psDroid->pimpl->movement->moveDir = ini.value("moveDirection").toInt();
    psDroid->pimpl->movement->bumpDir = ini.value("bumpDir").toInt();
    psDroid->pimpl->movement->vertical_speed = ini.value("vertSpeed").toInt();
    psDroid->pimpl->movement->bumpTime = ini.value("bumpTime").toInt();
    psDroid->pimpl->movement->shuffleStart = ini.value("shuffleStart").toInt();

    for (auto j = 0; j < MAX_WEAPONS; ++j)
    {
      psDroid->weaponManager->weapons[j].ammoUsed = ini.value("attackRun/" + WzString::number(j)).toInt();
    }
    psDroid->pimpl->movement->lastBump = ini.value("lastBump").toInt();
    psDroid->pimpl->movement->pauseTime = ini.value("pauseTime").toInt();
    Vector2i tmp = ini.vector2i("bumpPosition");
    psDroid->pimpl->movement->bumpPos = Vector3i(tmp.x, tmp.y, 0);

    // Recreate path-finding jobs
    if (psDroid->pimpl->movement->status == MOVE_STATUS::WAIT_FOR_ROUTE) {
      psDroid->pimpl->movement->status = MOVE_STATUS::INACTIVE;
      fpathDroidRoute(psDroid, psDroid->getMovementData()->destination, FPATH_MOVETYPE::FMT_MOVE);
      psDroid->pimpl->movement->status = MOVE_STATUS::WAIT_FOR_ROUTE;

      // Droid might be on a mission, so finish pathfinding now, in case pointers swap and map size changes.
      auto dr = fpathDroidRoute(psDroid, psDroid->getMovementData()->destination,
                                        FPATH_MOVETYPE::FMT_MOVE);
      if (dr == FPATH_RESULT::OK) {
        psDroid->pimpl->movement->status = MOVE_STATUS::NAVIGATE;
        psDroid->pimpl->movement->pathIndex = 0;
      }
      else // if (retVal == FPR_FAILED)
      {
        psDroid->pimpl->movement->status = MOVE_STATUS::INACTIVE;
        newAction(psDroid, ACTION::SULK);
      }
      ASSERT(dr != FPATH_RESULT::WAIT, " ");
    }

    auto startpos = getPlayerStartPosition(player);
    if (psDroid->getType() == DROID_TYPE::CONSTRUCT &&
        startpos.x == 0 && startpos.y == 0) {
      scriptSetStartPos(psDroid->playerManager->getPlayer(),
                        psDroid->getPosition().x, psDroid->getPosition().y);
    }

    if (psDroid->getGroup() == nullptr ||
        psDroid->getGroup()->getType() != GROUP_TYPE::TRANSPORTER ||
        isTransporter(*psDroid)) {
      // do not add to list if on a transport, then the group list is used instead
      playerList[psDroid->playerManager->getPlayer()].addDroid(*psDroid);
    }

    ini.endGroup();
  }
  return true;
}

void Droid::fpathSetDirectRoute(Vector2i targetLocation)
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Undefined");
  fpathSetMove(pimpl->movement.get(), targetLocation);
}

/**
 * Send the vtol droid back to the nearest rearming pad - if
 * there is one, otherwise return to base
 */
void Droid::moveToRearm()
{
  if (!isVtol()) return;

  //if droid is already returning - ignore
  if (vtolRearming(*this)) return;

  //get the droid to fly back to a ReArming Pad
  // don't worry about finding a clear one for the minute
  auto psStruct = findNearestReArmPad(
          this, pimpl->associatedStructure, false);

  if (!psStruct) {
    //return to base un-armed
    objTrace(getId(), "Did not find an available rearm pad - RTB instead");
    orderDroid(this, ORDER_TYPE::RETURN_TO_BASE, ModeImmediate);
    return;
  }

  // note a base rearm pad if the vtol doesn't have one
  if (pimpl->associatedStructure == nullptr) {
    pimpl->associatedStructure = psStruct;
  }

  //return to re-arming pad
  if (pimpl->order->type == ORDER_TYPE::NONE) {
    // no order set - use the rearm order to ensure the unit goes back
    // to the landing pad
    orderDroidObj(this, ORDER_TYPE::REARM, psStruct, ModeImmediate);
  }
  else {
    newAction(this, ACTION::MOVE_TO_REARM, psStruct);
  }
}

int Droid::aiBestNearestTarget(BaseObject** ppsObj, int weapon_slot, int extraRange) const
{
  int bestMod = 0;
  BaseObject* bestTarget = nullptr;
  BaseObject const* psTarget = nullptr;
  BaseObject* tempTarget;
  Structure* targetStructure;
  TARGET_ORIGIN tmpOrigin = TARGET_ORIGIN::UNKNOWN;

  //don't bother looking if empty vtol droid
  if (vtolEmpty(*this)) {
    return -1;
  }

  /* Return if have no weapons */
  // The ai orders a non-combat droid to patrol = crash without it...
  if (numWeapons(*this) == 0 &&
      pimpl->type != DROID_TYPE::SENSOR) {
    return -1;
  }
  // Check if we have a CB target to begin with
  if (!proj_Direct(weaponManager->weapons[0].stats.get())) {
    auto psWStats = weaponManager->weapons[0].stats.get();

    bestTarget = aiSearchSensorTargets(this, weapon_slot, psWStats, &tmpOrigin);
    bestMod = targetAttackWeight(bestTarget, this, weapon_slot);
  }

  auto weaponEffect = weaponManager->weapons[weapon_slot].stats->weaponEffect;

  auto electronic = electronicDroid(this);

  // Range was previously 9*TILE_UNITS. Increasing this doesn't seem to help much, though. Not sure why.
  auto droidRange = std::min(aiDroidRange(this, weapon_slot) + extraRange,
                             objSensorRange(this) + 6 * TILE_UNITS);

  static GridList gridList; // static to avoid allocations.
  gridList = gridStartIterate(getPosition().x, getPosition().y, droidRange);
  for (auto targetInQuestion : gridList)
  {
    BaseObject* friendlyObj = nullptr;
    /* This is a friendly unit, check if we can reuse its target */
    if (aiCheckAlliances(targetInQuestion->playerManager->getPlayer(),
                         playerManager->getPlayer())) {
      friendlyObj = targetInQuestion;
      targetInQuestion = nullptr;

      /* Can we see what it is doing? */
      if (friendlyObj->isVisibleToPlayer(playerManager->getPlayer()) == UBYTE_MAX) {
        if (auto friendlyDroid = dynamic_cast<Droid*>(friendlyObj)) {
          /* See if friendly droid has a target */
          tempTarget = friendlyDroid->pimpl->actionTargets[0];
          if (tempTarget && !tempTarget->damageManager->isDead()) {
            //make sure a weapon droid is targeting it
            if (numWeapons(*friendlyDroid) > 0) {
              // make sure this target wasn't assigned explicitly to this droid
              if (friendlyDroid->getOrder()->type != ORDER_TYPE::ATTACK) {
                targetInQuestion = tempTarget; //consider this target
              }
            }
          }
        }
        else if (auto friendlyStruct = dynamic_cast<Structure*>(friendlyObj)) {
          tempTarget = friendlyStruct->getTarget(0);
          if (tempTarget && !tempTarget->damageManager->isDead()) {
            targetInQuestion = tempTarget;
          }
        }
      }
    }

    if (targetInQuestion &&
        targetInQuestion != this && //< in case friendly unit had me as target
        (dynamic_cast<Droid*>(targetInQuestion) ||
         dynamic_cast<Structure*>(targetInQuestion) ||
         dynamic_cast<Feature*>(targetInQuestion) &&
         targetInQuestion->isVisibleToPlayer(playerManager->getPlayer()) == UBYTE_MAX &&
         !aiCheckAlliances(targetInQuestion->playerManager->getPlayer(), playerManager->getPlayer()) &&
         validTarget(this, targetInQuestion, weapon_slot) &&
         objectPositionSquareDiff(this, targetInQuestion) < droidRange * droidRange)) {

      if (auto droid = dynamic_cast<Droid*>(targetInQuestion)) {
        // don't attack transporters with EW in multiplayer
        if (bMultiPlayer) {
          // if not electronic then valid target
          if (!electronic ||
              !isTransporter(*droid)) {
            // only a valid target if NOT a transporter
            psTarget = targetInQuestion;
          }
        }
        else {
          psTarget = targetInQuestion;
        }
      }
      else if (auto psStruct = dynamic_cast<Structure*>(targetInQuestion)) {
        if (electronic) {
          // don't want to target structures with resistance of zero
          // if using electronic warfare
          if (validStructResistance(psStruct)) {
            psTarget = targetInQuestion;
          }
        }
        else if (numWeapons(*psStruct) > 0) {
          // structure with weapons - go for this
          psTarget = targetInQuestion;
        }
        else if (isHumanPlayer(playerManager->getPlayer()) &&
                 psStruct->getStats()->type != STRUCTURE_TYPE::WALL &&
                 psStruct->getStats()->type != STRUCTURE_TYPE::WALL_CORNER ||
                 !isHumanPlayer(playerManager->getPlayer())) {
          psTarget = targetInQuestion;
        }
      }
      else if (dynamic_cast<Feature*>(targetInQuestion) &&
               getLastFrustratedTime() > 0 &&
               gameTime - getLastFrustratedTime() < FRUSTRATED_TIME &&
               dynamic_cast<Feature*>(targetInQuestion)->getStats()->damageable &&
               playerManager->getPlayer() != scavengerPlayer()) { //< hack to avoid scavs blowing up their nice feature walls

        psTarget = targetInQuestion;
        objTrace(getId(),
                 "considering shooting at %s in frustration",
                 objInfo(targetInQuestion));
      }

      // check if our weapon is most effective against this object
      if (psTarget == nullptr || psTarget != targetInQuestion)
        continue;//< was assigned?

      auto newMod = targetAttackWeight(psTarget, this, weapon_slot);

      // remember this one if it's our best target so far
      if (newMod >= 0 && (newMod > bestMod || !bestTarget)) {
        bestMod = newMod;
        tmpOrigin = TARGET_ORIGIN::ALLY;
        bestTarget = psTarget;
      }
    }
  }

  if (!bestTarget) return -1;
  ASSERT(!bestTarget->damageManager->isDead(), "AI gave us a target that is already dead.");

  targetStructure = visGetBlockingWall(this, bestTarget);
  // See if target is blocked by a wall; only affects direct weapons
  // Ignore friendly walls here
  if (proj_Direct(weaponManager->weapons[weapon_slot].stats.get()) && targetStructure &&
      !aiCheckAlliances(playerManager->getPlayer(), targetStructure->playerManager->getPlayer())) {
    //are we any good against walls?
    if (asStructStrengthModifier[weaponEffect][targetStructure->getStats()->strength] >=
        MIN_STRUCTURE_BLOCK_STRENGTH) {
      bestTarget = targetStructure; //attack wall
    }
  }
  *ppsObj = bestTarget;
  return bestMod;
}

/** This function adds experience to the command droid of the psShooter's command group.*/
void Droid::cmdDroidUpdateExperience(unsigned experienceInc) const
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "invalid Unit pointer");
  if (!hasCommander()) return;

  pimpl->commander->pimpl->experience += MIN(experienceInc, UINT32_MAX - pimpl->commander->getExperience());
}

void Droid::addDroidToGroup(Droid* psDroid) const
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "invalid Unit pointer");
  pimpl->group->addDroid(psDroid);
}
