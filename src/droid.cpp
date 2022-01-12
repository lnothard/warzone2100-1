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
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/sound/audio.h"
#include "lib/sound/audio_id.h"

#include "objects.h"
#include "loop.h"
#include "visibility.h"
#include "droid.h"
#include "effects.h"
#include "action.h"
#include "geometry.h"
#include "lighting.h"
#include "warcam.h"
#include "text.h"
#include "cmddroid.h"
#include "projectile.h"
#include "mission.h"
#include "transporter.h"
#include "selection.h"
#include "edit3d.h"
#include "scores.h"
#include "research.h"
#include "qtscript.h"
#include "mapgrid.h"
#include "combat.h"

// the structure that was last hit
Droid* psLastDroidHit;

//determines the best IMD to draw for the droid - A TEMP MEASURE!
static void groupConsoleInformOfSelection(unsigned groupNumber);
static void groupConsoleInformOfCreation(unsigned groupNumber);
static void groupConsoleInformOfCentering(unsigned groupNumber);
static void groupConsoleInformOfRemoval();
static void droidUpdateDroidSelfRepair(Droid* psRepairDroid);
static unsigned calcDroidBaseBody(Droid* psDroid);

namespace Impl
{
    Droid::Droid(unsigned id, unsigned player)
            : Unit(id, player)
            , type(ANY)
            , group(nullptr)
            , secondaryOrder(DSS_ARANGE_LONG | DSS_REPLEV_NEVER | DSS_ALEV_ALWAYS | DSS_HALT_GUARD)
            , secondaryOrderPending(DSS_ARANGE_LONG | DSS_REPLEV_NEVER | DSS_ALEV_ALWAYS | DSS_HALT_GUARD)
            , secondaryOrderPendingCount(0)
            , action(ACTION::NONE)
            , actionPos(0, 0)
    {
      order->type = ORDER_TYPE::NONE;
      order->pos = Vector2i(0, 0);
      order->pos2 = Vector2i(0, 0);
      order->direction = 0;
      order->target = nullptr;
      order->structure_stats = nullptr;
      movement->status = MOVE_STATUS::INACTIVE;
      listSize = 0;
      listPendingBegin = 0;
      iAudioID = NO_SOUND;
      associatedStructure = nullptr;

      for (unsigned vPlayer = 0; vPlayer < MAX_PLAYERS; ++vPlayer)
      {
        visibilityState[vPlayer] = hasSharedVision(vPlayer, player) ? UINT8_MAX : 0;
      }

      memset(seenThisTick, 0, sizeof(seenThisTick));
      periodicalDamageStart = 0;
      periodicalDamage = 0;
      illuminationLevel = UBYTE_MAX;
      resistance_to_electric = ACTION_START_TIME; // init the resistance to indicate no EW performed on this droid
      lastFrustratedTime = 0; // make sure we do not start the game frustrated
    }

   /* DROID::~DROID: release all resources associated with a droid -
    * should only be called by objmem - use vanishDroid preferably
    */
    Droid::~Droid()
    {
      // Make sure to get rid of some final references in the sound code to this object first
      // In SimpleObject::~SimpleObject() is too late for this, since some callbacks require us to still be a DROID.
      audio_RemoveObj(this);

      if (isTransporter(*this)) {
        if (group) {
          // free all droids associated with this transporter
          for (auto psCurr : group->getMembers())
          {
            group->remove(psCurr);
          }
        }
      }

      fpathRemoveDroidData(static_cast<int>(getId()));

      // leave the current group if any
      if (group) {
        group->remove(this);
      }
    }

    ACTION Droid::getAction() const noexcept {
      return action;
    }

    const Order& Droid::getOrder() const {
      return *order;
    }

    const Movement& Droid::getMovementData() const
    {
      return *movement;
    }

    bool Droid::isProbablyDoomed(bool isDirectDamage) const {
      auto is_doomed = [this](unsigned damage) {
          const auto hit_points = getHp();
          return damage > hit_points && damage - hit_points > hit_points / 5;
      };

      if (isDirectDamage) {
        return is_doomed(expectedDamageDirect);
      }

      return is_doomed(expectedDamageIndirect);
    }

    void Droid::cancelBuild()
    {
      using enum ORDER_TYPE;
      if (order->type == NONE || order->type == PATROL || order->type == HOLD ||
          order->type == SCOUT || order->type == GUARD) {
        order->target = nullptr;
        action = ACTION::NONE;
      }
      else {
        action = ACTION::NONE;
        order->type = NONE;

        // stop moving
        if (isFlying()) {
          movement->status = MOVE_STATUS::HOVER;
        }
        else {
          movement->status = MOVE_STATUS::INACTIVE;
        }
        triggerEventDroidIdle(this);
      }
    }

  unsigned Droid::getLevel() const
  {
   if (!brain) {
     return 0;
   }
   const auto& rankThresholds = brain->upgraded[getPlayer()].rankThresholds;
   for (int i = 1; i < rankThresholds.size(); ++i)
   {
     if (kills < rankThresholds.at(i)) {
       return i - 1;
     }
   }
   return rankThresholds.size() - 1;
  }

  bool Droid::isStationary() const
  {
     using enum MOVE_STATUS;
     return movement->status == INACTIVE ||
            movement->status == HOVER ||
            movement->status == SHUFFLE;
  }

  bool Droid::hasCommander() const {
    if (type == COMMAND &&
        group != nullptr &&
        group->isCommandGroup()) {
      return true;
    }
    return false;
  }

  void Droid::upgradeHitPoints() {
    // use big numbers to scare away rounding errors
    const auto factor = 10000;
    auto prev = originalHp;
    originalHp = calcDroidBaseBody(this);
    auto increase = originalHp * factor / prev;
    auto hp = MIN(originalHp, (getHp() * increase) / factor + 1);
    DroidTemplate sTemplate;
    templateSetParts(this, &sTemplate);

    // update engine too
    baseSpeed = calcDroidBaseSpeed(&sTemplate, weight, getPlayer());
    if (isTransporter(*this)) {
      for (auto droid: group->getMembers()) {
        if (droid != this) {
          droid->upgradeHitPoints();
        }
      }
    }
  }

  void Droid::resetAction() noexcept
  {
    timeActionStarted = gameTime;
    actionPointsDone = 0;
  }

  bool Droid::isDamaged() const
  {
    return getHp() < originalHp;
  }

  void Droid::gainExperience(unsigned exp) {
    experience += exp;
  }

  bool Droid::isVtol() const
  {
    if (!propulsion)
      return false;

    return !isTransporter(*this) &&
           propulsion->propulsionType == PROPULSION_TYPE::LIFT;
  }

  void Droid::updateExpectedDamage(unsigned damage, bool is_direct) noexcept {
    if (is_direct)
      expectedDamageDirect += damage;
    else
      expectedDamageIndirect += damage;
  }

  unsigned Droid::calculateSensorRange() const {
    const auto ecm_range = ecm->upgraded[getPlayer()].range;
    if (ecm_range > 0) return ecm_range;

    return sensor->upgraded[getPlayer()].range;
  }

  int Droid::calculateHeight() const {
    const auto &imd = body->pIMD;
    const auto height = imd->max.y - imd->min.y;
    auto utility_height = 0, y_max = 0, y_min = 0;

    if (isVtol()) {
      return height + VTOL_HITBOX_MODIFIER;
    }

    const auto &weapon_stats = getWeapons()[0].getStats();
    switch (type) {
      case WEAPON:
        if (numWeapons(*this) == 0) {
          break;
        }
        y_max = weapon_stats.pIMD->max.y;
        y_min = weapon_stats.pIMD->min.y;
        break;
      case SENSOR:
        y_max = sensor->pIMD->max.y;
        y_min = sensor->pIMD->min.y;
        break;
      case ECM:
        y_max = ecm->pIMD->max.y;
        y_min = ecm->pIMD->min.y;
        break;
      case CONSTRUCT:
        break;
    }
  }

  DROID_TYPE Droid::getType() const noexcept
  {
    return type;
  }

  bool Droid::hasElectronicWeapon() const
  {
    if (::hasElectronicWeapon(*this)) {
      return true;
    }
    if (type != COMMAND) {
      return false;
    }
    return group->hasElectronicWeapon();
  }

  int Droid::spaceOccupiedOnTransporter() const {
    return bMultiPlayer ? static_cast<int>(body->size) + 1 : 1;
  }

  int Droid::getVerticalSpeed() const noexcept {
    return movement->vertical_speed;
  }

  bool Droid::isFlying() const
  {
    if (!propulsion) {
      return false;
    }

    return (movement->status != MOVE_STATUS::INACTIVE ||
            isTransporter(*this)) &&
            propulsion->propulsionType == PROPULSION_TYPE::LIFT;
  }

  unsigned Droid::getSecondaryOrder() const noexcept {
    return secondaryOrder;
  }

  const Vector2i &Droid::getDestination() const {
    return movement->destination;
  }

  void Droid::incrementKills() noexcept {
    ++kills;
  }

  unsigned Droid::getArmourPointsAgainstWeapon(WEAPON_CLASS weaponClass) const
  {
    assert(body);
    switch (weaponClass) {
      case WEAPON_CLASS::KINETIC:
        return body->upgraded[getPlayer()].armour;
      case WEAPON_CLASS::HEAT:
        return body->upgraded[getPlayer()].thermal;
      default:
        // should never get here!
        abort();
    }
  }

  void Droid::assignVtolToRearmPad(RearmPad* rearmPad)
  {
    associatedStructure = dynamic_cast<Structure*>(rearmPad);
  }

  bool Droid::isAttacking() const noexcept
  {
    if (!(type == WEAPON || type == CYBORG ||
          type == CYBORG_SUPER)) {
      return false;
    }
    if (action == ATTACK ||
        action == MOVE_TO_ATTACK ||
        action == ROTATE_TO_ATTACK ||
        action == VTOL_ATTACK ||
        action == MOVE_FIRE) {
      return true;
    }
    return false;
  }

  bool Droid::isSelectable() const
  {
    if (!SimpleObject::isSelectable()) {
      return false;
    }
    if (isTransporter(*this) && !bMultiPlayer) {
      return false;
    }
    return true;
  }

  int Droid::calculateElectronicResistance() const
  {
    auto resistance = experience /
                      (65536 / MAX(1, body->upgraded[getPlayer()].resistance));
    resistance = MAX(resistance, body->upgraded[getPlayer()].resistance);
    return MIN(resistance, INT16_MAX);
  }

  bool Droid::isRadarDetector() const
  {
    if (!sensor) {
      return false;
    }
    return sensor->type == SENSOR_TYPE::RADAR_DETECTOR;
  }

  bool Droid::hasStandardSensor() const
  {
    if (type != SENSOR) {
      return false;
    }
    if (sensor->type == SENSOR_TYPE::VTOL_INTERCEPT ||
        sensor->type == SENSOR_TYPE::STANDARD ||
        sensor->type == SENSOR_TYPE::SUPER)  {
      return true;
    }
    return false;
  }

  bool Droid::hasCbSensor() const
  {
    if (type != SENSOR) {
      return false;
    }
    if (sensor->type == SENSOR_TYPE::VTOL_CB ||
        sensor->type == SENSOR_TYPE::INDIRECT_CB)  {
      return true;
    }
    return false;
  }

  void Droid::actionUpdateTransporter()
  {
    //check if transporter has arrived
    if (updateTransporter(this))
    {
      // Got to destination
      action = ACTION::NONE;
    }
  }

  void Droid::actionSanity()
  {
    // Don't waste ammo unless given a direct attack order.
    bool avoidOverkill = order->type != ORDER_TYPE::ATTACK &&
                         (action == ACTION::ATTACK ||
                          action == ACTION::MOVE_FIRE ||
                          action == ACTION::MOVE_TO_ATTACK	||
                          action == ACTION::ROTATE_TO_ATTACK ||
                          action == ACTION::VTOL_ATTACK);

    bool bDirect = false;

    // clear the target if it has died
    for (int i = 0; i < MAX_WEAPONS; i++)
    {
      bDirect = proj_Direct(&getWeapons()[i].getStats());
      if (!actionTarget[i] || !(avoidOverkill
                                 ? aiObjectIsProbablyDoomed(actionTarget[i], bDirect)
                                 : actionTarget[i]->died)) {
        return;
      }
      syncDebugObject(actionTarget[i], '-');
      setDroidActionTarget(this, nullptr, i);
      if (i != 0) {
        continue;
      }
      if (action == MOVE_FIRE || action == TRANSPORT_IN ||
          action == TRANSPORT_OUT) {
        continue;
      }
      action = NONE;
      // if VTOL - return to rearm pad if not patrolling
      if (!isVtol()) {
        continue;
      }
      if ((order->type == ORDER_TYPE::PATROL || order->type == ORDER_TYPE::CIRCLE) &&
      (!vtolEmpty(*this) || (secondaryOrder & DSS_ALEV_MASK) == DSS_ALEV_NEVER)) {
        // Back to the patrol.
        ::actionDroid(this, ACTION::MOVE, order->pos.x, order->pos.y);
      } else {
        moveToRearm(this);
      }
    }
  }

  /* Overall action function that is called by the specific action functions */
  void Droid::actionDroidBase(Action* psAction)
  {
    ASSERT_OR_RETURN(, psAction->psObj == nullptr || !psAction->psObj->died, "Droid dead");

    auto& psWeapStats = getWeapons()[0].getStats();
    Vector2i pos{0, 0};

    auto secHoldActive = secondaryGetState(SECONDARY_ORDER::HALT_TYPE) == DSS_HALT_HOLD;
    timeActionStarted = gameTime;
    syncDebugDroid(this, '-');
    syncDebug("%d does %s", getId(), getDroidActionName(psAction->action));
    objTrace(getId(), "base set action to %s (was %s)", getDroidActionName(psAction->action),
             getDroidActionName(action));

    bool hasValidWeapon = false;
    for (int i = 0; i < MAX_WEAPONS; i++)
    {
      hasValidWeapon |= validTarget(this, psAction->psObj, i);
    }

    switch (psAction->action) {
      case NONE:
        // Clear up what ever the droid was doing before if necessary
        if (!isStationary()) {
          moveStopDroid(this);
        }
        action = NONE;
        actionPos = Vector2i(0, 0);
        timeActionStarted = 0;
        actionPointsDone = 0;
        if (numWeapons(*this) > 0) {
          for (int i = 0; i < numWeapons(*this); i++)
          {
            setDroidActionTarget(this, nullptr, i);
          }
        }
        else {
          setDroidActionTarget(this, nullptr, 0);
        }
        break;

      case ACTION::TRANSPORT_WAIT_TO_FLY_IN:
        action = TRANSPORT_WAIT_TO_FLY_IN;
        break;

      case ACTION::ATTACK:
        if (numWeapons(*this) == 0 || isTransporter(*this) || psAction->psObj == this) {
          break;
        }
        if (!hasValidWeapon) {
          // continuing is pointless, we were given an invalid target
          // for ex. AA gun can't attack ground unit
          break;
        }
        if (hasElectronicWeapon()) {
          //check for low or zero resistance - just zero resistance!
          if (dynamic_cast<Structure*>(psAction->psObj) &&
              !validStructResistance(dynamic_cast<Structure*>(psAction->psObj))) {
            //structure is low resistance already so don't attack
            action = NONE;
            break;
          }

          // in multiplayer cannot electronically attack a transporter
          if (bMultiPlayer
              && dynamic_cast<Droid*>(psAction->psObj)
              && isTransporter(*dynamic_cast<Droid*>(psAction->psObj))) {
            action = NONE;
            break;
          }
        }

        // note the droid's current pos so that scout & patrol orders know how far the
        // droid has gone during an attack
        // slightly strange place to store this I know, but I didn't want to add any more to the droid
        actionPos = pos.xy();
        setDroidActionTarget(this, psAction->psObj, 0);

        if (((order->type == ORDER_TYPE::ATTACK_TARGET
              || order->type == ORDER_TYPE::NONE
              || order->type == ORDER_TYPE::HOLD
              || (order->type == ORDER_TYPE::GUARD && hasCommander())
              || order->type == ORDER_TYPE::FIRE_SUPPORT)
             && secHoldActive)
            || (!isVtol() && (orderStateObj(this, ORDER_TYPE::FIRE_SUPPORT) != nullptr))) {
          action = ATTACK; // holding, try attack straightaway
        }
        else if (actionInsideMinRange(this, psAction->psObj, psWeapStats)) // too close?
        {
          if (!proj_Direct(psWeapStats)) {
            if (psWeapStats.rotate) {
              action = ATTACK;
            }
            else {
              action = ROTATE_TO_ATTACK;
              moveTurnDroid(actionTarget[0]->getPosition().x, actionTarget[0]->getPosition().y);
            }
          }
          else if (order->type != ORDER_TYPE::HOLD &&
                   secondaryGetState(SECONDARY_ORDER::HALT_TYPE) != DSS_HALT_HOLD) {
            auto pbx = 0;
            auto pby = 0;
            /* direct fire - try and extend the range */
            action = MOVE_TO_ATTACK;
            actionCalcPullBackPoint(this, psAction->psObj, &pbx, &pby);

            turnOffMultiMsg(true);
            moveDroidTo(this, (unsigned)pbx, (unsigned)pby);
            turnOffMultiMsg(false);
          }
        }
        else if (order->type != ORDER_TYPE::HOLD &&
                 secondaryGetState(SECONDARY_ORDER::HALT_TYPE) != DSS_HALT_HOLD) {
          // approach closer?
          action = MOVE_TO_ATTACK;
          turnOffMultiMsg(true);
          moveDroidTo(this, psAction->psObj->getPosition().x,
                   psAction->psObj->getPosition().y);
          turnOffMultiMsg(false);
        }
        else if (order->type != ORDER_TYPE::HOLD &&
                 secondaryGetState(SECONDARY_ORDER::HALT_TYPE) == DSS_HALT_HOLD) {
          action = ATTACK;
        }
        break;

      case MOVE_TO_REARM:
        action = MOVE_TO_REARM;
        actionPos = psAction->psObj->getPosition().xy();
        timeActionStarted = gameTime;
        setDroidActionTarget(this, psAction->psObj, 0);
        pos = actionTarget[0]->getPosition().xy();
        if (!actionVTOLLandingPos(this, &pos)) {
          // totally bunged up - give up
          objTrace(getId(), "move to rearm action failed!");
          orderDroid(this, ORDER_TYPE::RETURN_TO_BASE, ModeImmediate);
          break;
        }
        objTrace(getId(), "move to rearm");
        moveDroidToDirect(pos.x, pos.y);
        break;
      case CLEAR_REARM_PAD:
        debug(LOG_NEVER, "Unit %d clearing rearm pad", getId());
        action = CLEAR_REARM_PAD;
        setDroidActionTarget(this, psAction->psObj, 0);
        pos = actionTarget[0]->getPosition().xy();
        if (!actionVTOLLandingPos(this, &pos)) {
          // totally bunged up - give up
          objTrace(getId(), "clear rearm pad action failed!");
          orderDroid(this, ORDER_TYPE::RETURN_TO_BASE, ModeImmediate);
          break;
        }
        objTrace(getId(), "move to clear rearm pad");
        moveDroidToDirect(pos.x, pos.y);
        break;
      case MOVE:
      case TRANSPORT_IN:
      case TRANSPORT_OUT:
      case RETURN_TO_POS:
      case FIRE_SUPPORT_RETREAT:
        action = psAction->action;
        actionPos.x = psAction->x;
        actionPos.y = psAction->y;
        timeActionStarted = gameTime;
        setDroidActionTarget(this, psAction->psObj, 0);
        moveDroidTo(this, psAction->x, psAction->y);
        break;

      case BUILD:
        if (!order->structure_stats) {
          action = NONE;
          break;
        }
        //ASSERT_OR_RETURN(, order->type == ORDER_TYPE::BUILD || order->type == ORDER_TYPE::HELPBUILD || order->type == ORDER_TYPE::LINEBUILD, "cannot start build action without a build order");
        ASSERT_OR_RETURN(, psAction->x > 0 && psAction->y > 0, "Bad build order position");
        action = MOVE_TO_BUILD;
        actionPos.x = psAction->x;
        actionPos.y = psAction->y;
        moveDroidToNoFormation(this, actionPos.x, actionPos.y);
        break;
      case DEMOLISH:
        ASSERT_OR_RETURN(, order->type == ORDER_TYPE::DEMOLISH, "cannot start demolish action without a demolish order");
        action = MOVE_TO_DEMOLISH;
        actionPos.x = psAction->x;
        actionPos.y = psAction->y;
        ASSERT_OR_RETURN(, (order->target != nullptr) && (dynamic_cast<Structure*>(order->target)),
                           "invalid target for demolish order");
        order->structure_stats = std::make_shared<StructureStats>(dynamic_cast<Structure*>(order->target)->getStats());
        setDroidActionTarget(this, psAction->psObj, 0);
        moveDroidTo(this, psAction->x, psAction->y);
        break;
      case REPAIR:
        action = psAction->action;
        actionPos.x = psAction->x;
        actionPos.y = psAction->y;
        //this needs setting so that automatic repair works
        setDroidActionTarget(this, psAction->psObj, 0);
        ASSERT_OR_RETURN(,
                (actionTarget[0] != nullptr) && (dynamic_cast<Structure*>(actionTarget[0])),
                "invalid target for repair order");
        order->structure_stats = std::make_shared<StructureStats>(
                dynamic_cast<Structure*>(actionTarget[0])->getStats());
        if (secHoldActive && (order->type == ORDER_TYPE::NONE ||
                              order->type == ORDER_TYPE::HOLD)) {
          action = REPAIR;
        }
        else if ((!secHoldActive && order->type != ORDER_TYPE::HOLD) ||
                 (secHoldActive && order->type == ORDER_TYPE::REPAIR)) {
          action = MOVE_TO_REPAIR;
          moveDroidTo(this, psAction->x, psAction->y);
        }
        break;
      case OBSERVE:
        action = psAction->action;
        setDroidActionTarget(this, psAction->psObj, 0);
        actionPos.x = getPosition().x;
        actionPos.y = getPosition().y;
        if (secondaryGetState(SECONDARY_ORDER::HALT_TYPE) != DSS_HALT_GUARD &&
            (order->type == ORDER_TYPE::NONE || order->type == ORDER_TYPE::HOLD)) {
          action = visibleObject(this, actionTarget[0], false)
                            ? OBSERVE
                            : NONE;
        }
        else if (!hasCbSensor() && ((!secHoldActive && order->type != ORDER_TYPE::HOLD) ||
        (secHoldActive && order->type == ORDER_TYPE::OBSERVE))) {
          action = MOVE_TO_OBSERVE;
          moveDroidTo(this, actionTarget[0]->getPosition().x, actionTarget[0]->getPosition().y);
        }
        break;
      case FIRE_SUPPORT:
        action = FIRE_SUPPORT;
        if (!isVtol() && !secHoldActive && !dynamic_cast<Structure*>(order->target)) {
          moveDroidTo(this, order->target->getPosition().x, order->target->getPosition().y); // movetotarget.
        }
        break;
      case SULK:
        action = SULK;
        // hmmm, hope this doesn't cause any problems!
        timeActionStarted = gameTime + MIN_SULK_TIME + (gameRand(MAX_SULK_TIME - MIN_SULK_TIME));
        break;
      case WAIT_FOR_REPAIR:
        action = WAIT_FOR_REPAIR;
        // set the time so we can tell whether the start the self repair or not
        timeActionStarted = gameTime;
        break;
      case MOVE_TO_REPAIR_POINT:
        action = psAction->action;
        actionPos.x = psAction->x;
        actionPos.y = psAction->y;
        timeActionStarted = gameTime;
        setDroidActionTarget(this, psAction->psObj, 0);
        moveDroidToNoFormation(this, psAction->x, psAction->y);
        break;
      case WAIT_DURING_REPAIR:
        action = WAIT_DURING_REPAIR;
        break;
      case MOVE_TO_REARM_POINT:
        objTrace(getId(), "set to move to rearm pad");
        action = psAction->action;
        actionPos.x = psAction->x;
        actionPos.y = psAction->y;
        timeActionStarted = gameTime;
        setDroidActionTarget(this, psAction->psObj, 0);
        moveDroidToDirect(psAction->x, psAction->y);

        // make sure there aren't any other VTOLs on the rearm pad
        ensureRearmPadClear(dynamic_cast<Structure*>(psAction->psObj), this);
        break;
      case DROID_REPAIR:
      {
        action = psAction->action;
        actionPos.x = psAction->x;
        actionPos.y = psAction->y;
        setDroidActionTarget(this, psAction->psObj, 0);
        //initialise the action points
        actionPointsDone = 0;
        timeActionStarted = gameTime;
        const auto xdiff = (int)getPosition().x - (int)psAction->x;
        const auto ydiff = (int)getPosition().y - (int)psAction->y;
        if (secHoldActive && (order->type == ORDER_TYPE::NONE ||
            order->type == ORDER_TYPE::HOLD)) {
          action = DROID_REPAIR;
        }
        else if (((!secHoldActive && order->type != ORDER_TYPE::HOLD) ||
                 (secHoldActive && order->type == ORDER_TYPE::DROID_REPAIR))
                 // check that we actually need to move closer
                 && ((xdiff * xdiff + ydiff * ydiff) > REPAIR_RANGE * REPAIR_RANGE)) {
          action = MOVE_TO_DROID_REPAIR;
          moveDroidTo(this, psAction->x, psAction->y);
        }
        break;
      }
      case RESTORE:
        ASSERT_OR_RETURN(, order->type == ORDER_TYPE::RESTORE, "cannot start restore action without a restore order");
        action = psAction->action;
        actionPos.x = psAction->x;
        actionPos.y = psAction->y;
        ASSERT_OR_RETURN(, (order->target != nullptr) && (dynamic_cast<Structure*>(order->target)),
                           "invalid target for restore order");
        order->structure_stats = std::make_shared<StructureStats>(dynamic_cast<Structure*>(order->target)->getStats());
        setDroidActionTarget(this, psAction->psObj, 0);
        if (order->type != ORDER_TYPE::HOLD) {
          action = MOVE_TO_RESTORE;
          moveDroidTo(this, psAction->x, psAction->y);
        }
        break;
      default:
        ASSERT(!"unknown action", "actionUnitBase: unknown action");
        break;
    }
    syncDebugDroid(this, '+');
  }

  /** This function updates all the orders status, according with psdroid's current order and state.
   */
  void Droid::orderUpdateDroid()
  {
    ::SimpleObject* psObj = nullptr;
    ::Structure *psStruct, *psWall;
    int xdiff, ydiff;
    bool bAttack;
    int xoffset, yoffset;

    // clear the target if it has died
    if (order->target && order->target->died) {
      syncDebugObject(order->target, '-');
      setDroidTarget(this, nullptr);
      objTrace(getId(), "Target dead");
    }

    //clear its base struct if its died
    if (associatedStructure && associatedStructure->died) {
      syncDebugStructure(associatedStructure, '-');
      setDroidBase(this, nullptr);
      objTrace(getId(), "Base struct dead");
    }

    // check for died objects in the list
    orderCheckList();

    if (isDead(this)) {
      return;
    }

    using enum ORDER_TYPE;
    switch (order->type) {
      case NONE:
      case HOLD:
        // see if there are any orders queued up
        if (orderDroidList(this)) {
          // started a new order, quit
          break;
        }
          // if you are in a command group, default to guarding the commander
        else if (hasCommander() && order->type != HOLD
                 && order->structure_stats.get() != structGetDemolishStat())
          // stop the constructor auto repairing when it is about to demolish
        {
          orderDroidObj(this, GUARD, &group->getCommander(), ModeImmediate);
        }
        else if (isTransporter(*this) && !bMultiPlayer) {
        }
          // default to guarding
        else if (!tryDoRepairlikeAction()
                 && order->type != HOLD
                 && order->structure_stats.get() != structGetDemolishStat()
                 && secondaryGetState(SECONDARY_ORDER::HALT_TYPE) == DSS_HALT_GUARD
                 && !isVtol()) {
          orderDroidLoc(this, ORDER_TYPE::GUARD,
                        getPosition().x, getPosition().y, ModeImmediate);
        }
        break;
      case TRANSPORT_RETURN:
        if (action == ACTION::NONE) {
          missionMoveTransporterOffWorld(this);

          /* clear order */
          order = std::make_unique<Order>(NONE);
        }
        break;
      case TRANSPORT_OUT:
        if (action == ACTION::NONE) {
          if (getPlayer() == selectedPlayer) {
            if (getDroidsToSafetyFlag()) {
              //move droids in Transporter into holding list
              moveDroidsToSafety(this);
              //we need the transporter to just sit off world for a while...
              orderDroid(this, TRANSPORT_IN, ModeImmediate);
              /* set action transporter waits for timer */
              ::actionDroid(this, TRANSPORT_WAIT_TO_FLY_IN);

              missionSetReinforcementTime(gameTime);
              //don't do this until waited for the required time
              //fly Transporter back to get some more droids
              //orderDroidLoc( psDroid, TRANSPORTIN,
              //getLandingX(selectedPlayer), getLandingY(selectedPlayer));
            }
            else {
              //the script can call startMission for this callback for offworld missions
              triggerEvent(TRIGGER_TRANSPORTER_EXIT, this);
              /* clear order */
              order = std::make_unique<Order>(NONE);
            }

            movement->speed = 0;
            // Prevent radical movement vector when adjusting from home to away map exit and entry coordinates.
          }
        }
        break;
      case TRANSPORT_IN:
        if ((action == ACTION::NONE) &&
            (movement->status == MOVE_STATUS::INACTIVE)) {
          /* clear order */
          order = std::make_unique<Order>(NONE);

          //FFS! You only wan't to do this if the droid being tracked IS the transporter! Not all the time!
          // What about if your happily playing the game and tracking a droid, and re-enforcements come in!
          // And suddenly BLAM!!!! It drops you out of camera mode for no apparent reason! TOTALY CONFUSING
          // THE PLAYER!
          //
          // Just had to get that off my chest....end of rant.....
          //
          if (this == getTrackingDroid()) // Thats better...
          {
            /* deselect transporter if have been tracking */
            if (getWarCamStatus()) {
              camToggleStatus();
            }
          }

          DeSelectDroid(this);

          /*don't try the unload if moving droids to safety and still got some
          droids left  - wait until full and then launch again*/
          if (getPlayer() == selectedPlayer && getDroidsToSafetyFlag() &&
              missionDroidsRemaining(selectedPlayer)) {
            resetTransporter();
          }
          else {
            unloadTransporter(this, getPosition().x, getPosition().y, false);
          }
        }
        break;
      case MOVE:
        // Just wait for the action to finish then clear the order
        if (action == ACTION::NONE || action == ACTION::ATTACK) {
          order = std::make_unique<Order>(NONE);
        }
        break;
      case RECOVER:
        if (order->target == nullptr) {
          order = std::make_unique<Order>(NONE);
        }
        else if (action == ACTION::NONE) {
          // stopped moving, but still haven't got the artifact
          ::actionDroid(this, ACTION::MOVE, order->target->getPosition().x, order->target->getPosition().y);
        }
        break;
      case SCOUT:
      case PATROL:
        // if there is an enemy around, attack it
        if (action == ACTION::MOVE || action == ACTION::MOVE_FIRE || (action == ACTION::NONE && isVtol())) {
          bool tooFarFromPath = false;
          if (isVtol() && order->type == PATROL) {
            // Don't stray too far from the patrol path - only attack if we're near it
            // A fun algorithm to detect if we're near the path
            Vector2i delta = order->pos - order->pos2;
            if (delta == Vector2i(0, 0)) {
              tooFarFromPath = false;
            }
            else if (abs(delta.x) >= abs(delta.y) &&
                     MIN(order->pos.x, order->pos2.x) - SCOUT_DIST <= getPosition().x &&
                     getPosition().x <= MAX(order->pos.x, order->pos2.x) + SCOUT_DIST) {
              tooFarFromPath = (abs((getPosition().x - order->pos.x) * delta.y / delta.x +
                                    order->pos.y - getPosition().y) > SCOUT_DIST);
            }
            else if (abs(delta.x) <= abs(delta.y) &&
                     MIN(order->pos.y, order->pos2.y) - SCOUT_DIST <= getPosition().y &&
                     getPosition().y <= MAX(order->pos.y, order->pos2.y) + SCOUT_DIST) {
              tooFarFromPath = (abs((getPosition().y - order->pos.y) * delta.x / delta.y +
                                    order->pos.x - getPosition().x) > SCOUT_DIST);
            }
            else {
              tooFarFromPath = true;
            }
          }
          if (!tooFarFromPath) {
            // true if in condition to set actionDroid to attack/observe
            bool attack = secondaryGetState(SECONDARY_ORDER::ATTACK_LEVEL) == DSS_ALEV_ALWAYS &&
                          aiBestNearestTarget(this, &psObj, 0, SCOUT_ATTACK_DIST) >= 0;
            switch (type) {
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
                  ::actionDroid(this, ATTACK, psObj);
                }
                break;
              case SENSOR:
                if (attack) {
                  ::actionDroid(this, OBSERVE, psObj);
                }
                break;
              default:
                ::actionDroid(this, NONE);
                break;
            }
          }
        }
        if (action == ACTION::NONE) {
          xdiff = getPosition().x - order->pos.x;
          ydiff = getPosition().y - order->pos.y;
          if (xdiff * xdiff + ydiff * ydiff < SCOUT_DIST * SCOUT_DIST) {
            if (order->type == PATROL) {
              // see if we have anything queued up
              if (orderDroidList(this)) {
                // started a new order, quit
                break;
              }
              if (isVtol() && !vtolFull(*this) &&
                  (secondaryOrder & DSS_ALEV_MASK) != DSS_ALEV_NEVER) {
                moveToRearm(this);
                break;
              }
              // head back to the other point
              std::swap(order->pos, order->pos2);
              ::actionDroid(this, ACTION::MOVE, order->pos.x, order->pos.y);
            }
            else {
              order = std::make_unique<Order>(NONE);
            }
          }
          else {
            ::actionDroid(this, ACTION::MOVE, order->pos.x, order->pos.y);
          }
        }
        else if (((action == ACTION::ATTACK) ||
                  (action == ACTION::VTOL_ATTACK) ||
                  (action == ACTION::MOVE_TO_ATTACK) ||
                  (action == ACTION::ROTATE_TO_ATTACK) ||
                  (action == ACTION::OBSERVE) ||
                  (action == ACTION::MOVE_TO_OBSERVE)) &&
                 secondaryGetState(SECONDARY_ORDER::HALT_TYPE) != DSS_HALT_PURSUE) {
          // attacking something - see if the droid has gone too far, go up to twice the distance we want to go, so that we don't repeatedly turn back when the target is almost in range.
          if (objectPositionSquareDiff(getPosition(), Vector3i(actionPos, 0)) >
                (SCOUT_ATTACK_DIST * 2 * SCOUT_ATTACK_DIST* 2)) {
            ::actionDroid(this, RETURN_TO_POS, actionPos.x, actionPos.y);
          }
        }
        if (order->type == PATROL && isVtol() && vtolEmpty(*this) && (secondaryOrder & DSS_ALEV_MASK) != DSS_ALEV_NEVER) {
          moveToRearm(this); // Completely empty (and we're not set to hold fire), don't bother patrolling.
          break;
        }
        break;
      case CIRCLE:
        // if there is an enemy around, attack it
        if (action == ACTION::MOVE &&
            secondaryGetState(SECONDARY_ORDER::ATTACK_LEVEL) == DSS_ALEV_ALWAYS &&
            aiBestNearestTarget(this, &psObj, 0, SCOUT_ATTACK_DIST) >= 0) {
          switch (type) {
            using enum DROID_TYPE;
            case WEAPON:
            case CYBORG:
            case CYBORG_SUPER:
            case PERSON:
            case COMMAND:
              ::actionDroid(this, ACTION::ATTACK, psObj);
              break;
            case SENSOR:
              ::actionDroid(this, ACTION::OBSERVE, psObj);
              break;
            default:
              ::actionDroid(this, ACTION::NONE);
              break;
          }
        }
        else if (action == ACTION::NONE || action == ACTION::MOVE) {
          if (action == ACTION::MOVE) {
            // see if we have anything queued up
            if (orderDroidList(this)) {
              // started a new order, quit
              break;
            }
          }

          auto edgeDiff = getPosition().xy() - actionPos;
          if (action != ACTION::MOVE || dot(edgeDiff, edgeDiff) <= TILE_UNITS * 4 * TILE_UNITS * 4) {
            //Watermelon:use orderX,orderY as local space origin and calculate droid direction in local space
            auto diff = getPosition().xy() - order->pos;
            auto angle = iAtan2(diff) - DEG(30);
            do
            {
              xoffset = iSinR(angle, 1500);
              yoffset = iCosR(angle, 1500);
              angle -= DEG(10);
            }
            while (!worldOnMap(order->pos.x + xoffset, order->pos.y + yoffset));
            // Don't try to fly off map.
            ::actionDroid(this, ACTION::MOVE, order->pos.x + xoffset, order->pos.y + yoffset);
          }

          if (isVtol() && vtolEmpty(*this) &&
              (secondaryOrder & DSS_ALEV_MASK) != DSS_ALEV_NEVER) {
            moveToRearm(this); // Completely empty (and we're not set to hold fire), don't bother circling.
            break;
          }
        }
        else if (((action == ACTION::ATTACK) ||
                  (action == ACTION::VTOL_ATTACK) ||
                  (action == ACTION::MOVE_TO_ATTACK) ||
                  (action == ACTION::ROTATE_TO_ATTACK) ||
                  (action == ACTION::OBSERVE) ||
                  (action == ACTION::MOVE_TO_OBSERVE)) &&
                 secondaryGetState(SECONDARY_ORDER::HALT_TYPE) != DSS_HALT_PURSUE) {
          // attacking something - see if the droid has gone too far
          xdiff = getPosition().x - actionPos.x;
          ydiff = getPosition().y - actionPos.y;
          if (xdiff * xdiff + ydiff * ydiff > 2000 * 2000) {
            // head back to the target location
            ::actionDroid(this, ACTION::RETURN_TO_POS, order->pos.x, order->pos.y);
          }
        }
        break;
      case HELP_BUILD:
      case DEMOLISH:
      case OBSERVE:
      case REPAIR:
      case DROID_REPAIR:
      case RESTORE:
        if (action == ACTION::NONE || order->target == nullptr) {
          order = std::make_unique<Order>(NONE);
          ::actionDroid(this, NONE);
          if (getPlayer() == selectedPlayer) {
            intRefreshScreen();
          }
        }
        break;
      case REARM:
        if (order->target == nullptr ||
            actionTarget[0] == nullptr) {
          // arm pad destroyed find another
          order = std::make_unique<Order>(NONE);
          moveToRearm(this);
        }
        else if (action == ACTION::NONE) {
          order = std::make_unique<Order>(NONE);
        }
        break;
      case ATTACK:
      case ATTACK_TARGET:
        if (order->target == nullptr || order->target->died) {
          // if vtol then return to rearm pad as long as there are no other
          // orders queued up
          if (isVtol()) {
            if (!orderDroidList(this)) {
              order = std::make_unique<Order>(NONE);
              moveToRearm(this);
            }
          }
          else {
            order = std::make_unique<Order>(NONE);
            ::actionDroid(this, NONE);
          }
        }
        else if (((action == ACTION::MOVE) ||
                  (action == ACTION::MOVE_FIRE)) &&
                 actionVisibleTarget(this, order->target, 0) && !isVtol()) {
          // moved near enough to attack change to attack action
          ::actionDroid(this, ACTION::ATTACK, order->target);
        }
        else if ((action == ACTION::MOVE_TO_ATTACK) &&
                 !isVtol() &&
                 !actionVisibleTarget(this, order->target, 0) &&
                 secondaryGetState(SECONDARY_ORDER::HALT_TYPE) != DSS_HALT_HOLD) {
          // lost sight of the target while chasing it - change to a move action so
          // that the unit will fire on other things while moving
          ::actionDroid(this, ACTION::MOVE, order->target->getPosition().x, order->target->getPosition().y);
        }
        else if (!isVtol()
                 && order->target == actionTarget[0]
                 && actionInRange(this, order->target, 0)
                 && (psWall = visGetBlockingWall(this, order->target))
                 && !aiCheckAlliances(psWall->getPlayer(), getPlayer())) {
          // there is a wall in the way - attack that
          ::actionDroid(this, ATTACK, psWall);
        }
        else if ((action == ACTION::NONE) ||
                 (action == ACTION::CLEAR_REARM_PAD)) {
          if ((order->type == ATTACK_TARGET || order->type == ATTACK)
              && secondaryGetState(SECONDARY_ORDER::HALT_TYPE) == DSS_HALT_HOLD
              && !actionInRange(this, order->target, 0)) {
            // target is not in range and DSS_HALT_HOLD: give up, don't move
            order = std::make_unique<Order>(NONE);
          }
          else if (!isVtol() || allVtolsRearmed(this)) {
            ::actionDroid(this, ACTION::ATTACK, order->target);
          }
        }
        break;
      case BUILD:
        if (action == ACTION::BUILD &&
            order->target == nullptr) {
          order = std::make_unique<Order>(NONE);
          ::actionDroid(this, NONE);
          objTrace(getId(), "Clearing build order since build target is gone");
        }
        else if (action == ACTION::NONE) {
          order = std::make_unique<Order>(NONE);
          objTrace(getId(), "Clearing build order since build action is reset");
        }
        break;
      case EMBARK:
      {
        // only place it can be trapped - in multiPlayer can only put cyborgs onto a Cyborg Transporter
        auto temp = dynamic_cast<Droid*>(order->target); // NOTE: It is possible to have a NULL here

        if (temp && temp->type == DROID_TYPE::TRANSPORTER && !isCyborg(this)) {
          order = std::make_unique<Order>(NONE);
          ::actionDroid(this, NONE);
          if (getPlayer() == selectedPlayer) {
            audio_PlayBuildFailedOnce();
            addConsoleMessage(
                    _("We can't do that! We must be a Cyborg unit to use a Cyborg Transport!"),
                    CONSOLE_TEXT_JUSTIFICATION::DEFAULT,
                    selectedPlayer);
          }
        }
        else {
          // Wait for the action to finish then assign to Transporter (if not already flying)
          if (order->target == nullptr || transporterFlying(dynamic_cast<Droid*>(order->target))) {
            order = std::make_unique<Order>(NONE);
            ::actionDroid(this, NONE);
          }
          else if (abs((SDWORD)getPosition().x - (SDWORD)order->target->getPosition().x) < TILE_UNITS
                   && abs((SDWORD)getPosition().y - (SDWORD)order->target->getPosition().y) < TILE_UNITS) {
            // save the target of current droid (the transporter)
              auto* transporter = dynamic_cast<Droid*>(order->target);

            // order the droid to stop so moveUpdateDroid does not process this unit
            orderDroid(this, STOP, ModeImmediate);
            setDroidTarget(this, nullptr);
            order->target = nullptr;
            secondarySetState(SECONDARY_ORDER::RETURN_TO_LOCATION, DSS_NONE);

            /* We must add the droid to the transporter only *after*
            * processing changing its orders (see above).
            */
            transporterAddDroid(transporter, this);
          }
          else if (action == ACTION::NONE) {
              ::actionDroid(this, ACTION::MOVE, order->target->getPosition().x, order->target->getPosition().y);
          }
        }
      }
        // Do we need to clear the secondary order "DSO_EMBARK" here? (PD)
        break;
      case DISEMBARK:
        //only valid in multiPlayer mode
        if (bMultiPlayer) {
          //this order can only be given to Transporter droids
          if (isTransporter(*this)) {
            /*once the Transporter has reached its destination (and landed),
            get all the units to disembark*/
            if (action != ACTION::MOVE && action != ACTION::MOVE_FIRE &&
                movement->status == MOVE_STATUS::INACTIVE && movement->vertical_speed == 0) {
              unloadTransporter(this, getPosition().x, getPosition().y, false);
              //reset the transporter's order
              order = std::make_unique<Order>(NONE);
            }
          }
        }
        break;
      case RETURN_TO_BASE:
        // Just wait for the action to finish then clear the order
        if (action == ACTION::NONE) {
          order = std::make_unique<Order>(NONE);
          secondarySetState(SECONDARY_ORDER::RETURN_TO_LOCATION, DSS_NONE);
        }
        break;
      case RETURN_TO_REPAIR:
      case RTR_SPECIFIED:
        if (order->target == nullptr) {
          // Our target got lost. Let's try again.
          order = std::make_unique<Order>(NONE);
          orderDroid(this, RETURN_TO_REPAIR, ModeImmediate);
        }
        else if (action == ACTION::NONE) {
          /* get repair facility pointer */
          psStruct = dynamic_cast<Structure*>(order->target);
          ASSERT(psStruct != nullptr,
                 "orderUpdateUnit: invalid structure pointer");

            if (objectPositionSquareDiff(getPosition(), order->target->getPosition()) < (TILE_UNITS * 8) * (TILE_UNITS * 8)) {
            /* action droid to wait */
            ::actionDroid(this, WAIT_FOR_REPAIR);
          }
          else {
            // move the droid closer to the repair point
            // setting target to null will trigger search for nearest repair point: we might have a better option after all
            order->target = nullptr;
          }
        }
        break;
      case LINE_BUILD:
        if (action == ACTION::NONE ||
            (action == ACTION::BUILD && order->target == nullptr)) {
          // finished building the current structure
          auto lb = calcLineBuild(order->structure_stats.get(), order->direction,
                                  order->pos, order->pos2);
          if (lb.count <= 1) {
            // finished all the structures - done
            order = std::make_unique<Order>(NONE);
            break;
          }

          // update the position for another structure
          order->pos = lb[1];

          // build another structure
          setDroidTarget(this, nullptr);
          ::actionDroid(this, ACTION::BUILD, order->pos.x, order->pos.y);
          //intRefreshScreen();
        }
        break;
      case FIRE_SUPPORT:
        if (order->target == nullptr) {
          order = std::make_unique<Order>(NONE);
          if (isVtol() && !vtolFull(*this)) {
            moveToRearm(this);
          }
          else {
            ::actionDroid(this, NONE);
          }
        }
          //before targetting - check VTOL's are fully armed
        else if (vtolEmpty(*this)) {
          moveToRearm(this);
        }
          //indirect weapon droid attached to (standard)sensor droid
        else {
          ::SimpleObject* psFireTarget = nullptr;

          if (dynamic_cast<Droid*>(order->target)) {
            auto psSpotter = dynamic_cast<Droid*>(order->target);

            if (psSpotter->action == ACTION::OBSERVE ||
                (psSpotter->type == DROID_TYPE::COMMAND &&
                 psSpotter->action == ACTION::ATTACK)) {
              psFireTarget = psSpotter->actionTarget[0];
            }
          }
          else if (dynamic_cast<Structure*>(order->target)) {
            auto psSpotter = dynamic_cast<Structure*>(order->target);
            psFireTarget = &psSpotter->getTarget(0);
          }

          if (psFireTarget && !psFireTarget->died && checkAnyWeaponsTarget(this, psFireTarget)) {
            bAttack = false;
            if (isVtol()) {
              if (!vtolEmpty(*this) &&
                  ((action == ACTION::MOVE_TO_REARM) ||
                   (action == ACTION::WAIT_FOR_REARM)) &&
                  (movement->status != MOVE_STATUS::INACTIVE)) {
                // catch vtols that were attacking another target which was destroyed
                // get them to attack the new target rather than returning to rearm
                bAttack = true;
              }
              else if (allVtolsRearmed(this)) {
                bAttack = true;
              }
            }
            else {
              bAttack = true;
            }

            //if not currently attacking or target has changed
            if (bAttack &&
                (!droidAttacking(this) ||
                 psFireTarget != actionTarget[0])) {
              //get the droid to attack
              ::actionDroid(this, ACTION::ATTACK, psFireTarget);
            }
          }
          else if (isVtol() &&
                   !vtolFull(*this) &&
                   (action != ACTION::NONE) &&
                   (action != ACTION::FIRE_SUPPORT)) {
            moveToRearm(this);
          }
          else if ((action != ACTION::FIRE_SUPPORT) &&
                   (action != ACTION::FIRE_SUPPORT_RETREAT)) {
            ::actionDroid(this, ACTION::FIRE_SUPPORT, order->target);
          }
        }
        break;
      case RECYCLE:
        if (order->target == nullptr) {
          order = std::make_unique<Order>(NONE);
          ::actionDroid(this, NONE);
        }
        else if (actionReachedBuildPos(this,
                                       order->target->getPosition().x,
                                       order->target->getPosition().y,
                                       dynamic_cast<Structure*>(order->target)->getRotation().direction,
                                       &dynamic_cast<Structure*>(order->target)->getStats())) {
          recycleDroid();
        }
        else if (action == ACTION::NONE) {
          ::actionDroid(this, ACTION::MOVE, order->target->getPosition().x, order->target->getPosition().y);
        }
        break;
      case GUARD:
        if (orderDroidList(this)) {
          // started a queued order - quit
          break;
        }
        else if ((action == ACTION::NONE) ||
                 (action == ACTION::MOVE) ||
                 (action == ACTION::MOVE_FIRE)) {
          // not doing anything, make sure the droid is close enough
          // to the thing it is defending
          if ((!(type == REPAIRER || type == CYBORG_REPAIR))
              && order->target != nullptr && dynamic_cast<Droid*>(order->target)
              && (dynamic_cast<Droid*>(order->target))->type == COMMAND) {
            // guarding a commander, allow more space
            orderCheckGuardPosition(this, DEFEND_CMD_BASEDIST);
          }
          else {
            orderCheckGuardPosition(this, DEFEND_BASEDIST);
          }
        }
        else if (type == REPAIRER ||
                 type == CYBORG_REPAIR) {
          // repairing something, make sure the droid doesn't go too far
          orderCheckGuardPosition(this, REPAIR_MAXDIST);
        }
        else if (type == CONSTRUCT || type == CYBORG_CONSTRUCT) {
          // repairing something, make sure the droid doesn't go too far
          orderCheckGuardPosition(this, CONSTRUCT_MAXDIST);
        }
        else if (isTransporter(*this)) {
        }
        else {
          //let vtols return to rearm
          if (!vtolRearming(*this)) {
            // attacking something, make sure the droid doesn't go too far
            if (order->target != nullptr && dynamic_cast<Droid*>(order->target) &&
                (dynamic_cast<Droid*>(order->target))->type == COMMAND) {
              // guarding a commander, allow more space
              orderCheckGuardPosition(this, DEFEND_CMD_MAXDIST);
            }
            else {
              orderCheckGuardPosition(this, DEFEND_MAXDIST);
            }
          }
        }

        // get combat units in a command group to attack the commanders target
        if (hasCommander() && (numWeapons(*this) > 0)) {
          if (group->getCommander().getAction() == ACTION::ATTACK &&
              !group->getCommander().getTarget(0).died) {

            psObj = &group->getCommander().getTarget(0);

            if (action == ACTION::ATTACK ||
                action == ACTION::MOVE_TO_ATTACK) {
              if (actionTarget[0] != psObj) {
                ::actionDroid(this, ATTACK, psObj);
              }
            }
            else if (action != ACTION::MOVE) {
              ::actionDroid(this, ATTACK, psObj);
            }
          }

          // make sure units in a command group are actually guarding the commander
          psObj = orderStateObj(this, GUARD); // find out who is being guarded by the droid
          if (psObj == nullptr ||
              psObj != dynamic_cast<const SimpleObject*>(&group->getCommander())) {
            orderDroidObj(this, GUARD, dynamic_cast<SimpleObject*>(&group->getCommander()), ModeImmediate);
          }
        }

        tryDoRepairlikeAction();
        break;
      default:
        ASSERT(false, "orderUpdateUnit: unknown order");
    }

    // catch any vtol that is rearming but has finished his order
    if (order->type == NONE && vtolRearming(*this)
        && (actionTarget[0] == nullptr || !actionTarget[0]->died)) {
      order = std::make_unique<Order>(REARM, *actionTarget[0]);
    }

    if (selected) {
      // Tell us what the droid is doing.
      snprintf(DROIDDOING, sizeof(DROIDDOING), "%.12s,id(%d) order(%d):%s action(%d):%s secondary:%x move:%s",
               droidGetName(this), getId(),
               order->type, getDroidOrderName(order->type), action,
               getDroidActionName(action), secondaryOrder,
               moveDescription(movement->status));
    }
  }

  // Update the action state for a droid
  void Droid::actionUpdateDroid()
  {
    bool (*actionUpdateFunc)(Droid* psDroid) = nullptr;
    bool nonNullWeapon[MAX_WEAPONS] = {false};
    ::SimpleObject* psTargets[MAX_WEAPONS] = {nullptr};
    bool hasValidWeapon = false;
    bool hasVisibleTarget = false;
    bool targetVisibile[MAX_WEAPONS] = {false};
    bool bHasTarget = false;
    bool bDirect = false;
    ::Structure* blockingWall = nullptr;
    bool wallBlocked = false;

    auto& psPropStats = propulsion;
    bool secHoldActive = secondaryGetState(SECONDARY_ORDER::HALT_TYPE) == DSS_HALT_HOLD;

    actionSanity();

    //if the droid has been attacked by an EMP weapon, it is temporarily disabled
    if (lastHitWeapon == WEAPON_SUBCLASS::EMP) {
      if (gameTime - timeLastHit > EMP_DISABLE_TIME) {
        // the actionStarted time needs to be adjusted
        timeActionStarted += (gameTime - timeLastHit);
        // reset the lastHit parameters
        timeLastHit = 0;
        lastHitWeapon = WEAPON_SUBCLASS::COUNT;
      }
      else {
        // get out without updating
        return;
      }
    }

    // HACK: Apparently we can't deal with a droid that only has NULL weapons ?
    // FIXME: Find out whether this is really necessary
    if (numWeapons(*this) <= 1) {
      nonNullWeapon[0] = true;
    }

    switch (action) {
      case NONE:
      case WAIT_FOR_REPAIR:
        // doing nothing
        // see if there's anything to shoot.
        if (numWeapons(*this) > 0 && !isVtol() &&
            (order->type == ORDER_TYPE::NONE ||
             order->type == ORDER_TYPE::HOLD ||
             order->type == ORDER_TYPE::RETURN_TO_REPAIR ||
             order->type == ORDER_TYPE::GUARD)) {
          for (auto i = 0; i < numWeapons(*this); ++i)
          {
            if (nonNullWeapon[i]) {
              ::SimpleObject* psTemp = nullptr;

              auto const& psWeapStats = getWeapons()[i].getStats();
              if (psWeapStats.rotate &&
                  aiBestNearestTarget(this, &psTemp, i) >= 0) {
                if (secondaryGetState(SECONDARY_ORDER::ATTACK_LEVEL) == DSS_ALEV_ALWAYS) {
                  action = ATTACK;
                  setDroidActionTarget(this, psTemp, i);
                }
              }
            }
          }
        }
        break;
      case WAIT_DURING_REPAIR:
        // Check that repair facility still exists
        if (!order->target) {
          action = NONE;
          break;
        }
        if (order->type == ORDER_TYPE::RETURN_TO_REPAIR && 
            order->rtrType == RTR_DATA_TYPE::REPAIR_FACILITY) {
          // move back to the repair facility if necessary
          if (isStationary() &&
              !actionReachedBuildPos(this,
                                     order->target->getPosition().x,
                                     order->target->getPosition().y,
                                     (dynamic_cast<Structure*>(order->target))->getRotation().direction,
                                     (dynamic_cast<Structure*>(order->target))->getStats())) {
            moveDroidToNoFormation(this,
                                   order->target->getPosition().x,
                                   order->target->getPosition().y);
          }
        }
        else if (order->type == ORDER_TYPE::RETURN_TO_REPAIR && 
                 order->rtrType == RTR_DATA_TYPE::DROID &&
                 isStationary()) {
          if (!actionReachedDroid(this,
                                  dynamic_cast<Droid*>(order->target))) {
            moveDroidToNoFormation(this,
                                   order->target->getPosition().x,
                                   order->target->getPosition().y);
          }
          else {
            moveStopDroid(this);
          }
        }
        break;
      case TRANSPORT_WAIT_TO_FLY_IN:
        //if we're moving droids to safety and currently waiting to fly back in, see if time is up
        if (getPlayer() == selectedPlayer && getDroidsToSafetyFlag()) {
          bool enoughTimeRemaining = (mission.time - (gameTime - mission.startTime)) >= (60 * GAME_TICKS_PER_SEC);
          if (((int)(mission.ETA - (gameTime - missionGetReinforcementTime())) <= 0) &&
              enoughTimeRemaining) {
            unsigned droidX, droidY;

            if (!droidRemove(this, mission.apsDroidLists)) {
              ASSERT_OR_RETURN(, false, "Unable to remove transporter from mission list");
            }
            addDroid(this, apsDroidLists);
            //set the x/y up since they were set to INVALID_XY when moved offWorld
            missionGetTransporterExit(selectedPlayer, &droidX, &droidY);
            setPosition({droidX, droidY, getPosition().z});
            //fly Transporter back to get some more droids
            orderDroidLoc(this, ORDER_TYPE::TRANSPORT_IN,
                          getLandingX(selectedPlayer),
                          getLandingY(selectedPlayer),
                          ModeImmediate);
          }
        }
        break;
      case MOVE:
      case RETURN_TO_POS:
      case FIRE_SUPPORT_RETREAT:
        // moving to a location
        if (isStationary()) {
          bool notify = action == MOVE;
          // Got to destination
          action = NONE; 
          if (notify) {
            // notify scripts we have reached the destination
            // (also triggers when patrolling and reached a waypoint)
            triggerEventDroidIdle(this);
          }
        }
          //added multiple weapon check
        else if (numWeapons(*this) > 0) {
          for (auto i = 0; i < numWeapons(*this); ++i)
          {
            if (nonNullWeapon[i]) {
              ::SimpleObject* psTemp = nullptr;

              // i moved psWeapStats flag update there
              const auto& psWeapStats = getWeapons()[i].getStats();
              if (!isVtol() &&
                  psWeapStats.rotate &&
                  psWeapStats.fireOnMove &&
                  aiBestNearestTarget(this, &psTemp, i) >= 0) {
                if (secondaryGetState(SECONDARY_ORDER::ATTACK_LEVEL) == DSS_ALEV_ALWAYS) {
                  action = MOVE_FIRE;
                  setDroidActionTarget(this, psTemp, i);
                }
              }
            }
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
          moveToRearm(this);
        }
        // if droid stopped, it can no longer be in ACTION::MOVE_FIRE
        if (isStationary()) {
          action = NONE;
          break;
        }
        // loop through weapons and look for target for each weapon
        bHasTarget = false;
        for (auto i = 0; i < numWeapons(*this); ++i)
        {
          bDirect = proj_Direct(weapons[i].getStats());
          blockingWall = nullptr;
          // Does this weapon have a target?
          if (actionTarget[i] != nullptr) {
            // Is target worth shooting yet?
            if (aiObjectIsProbablyDoomed(actionTarget[i], bDirect)) {
              setDroidActionTarget(this, nullptr, i);
            }
              // Is target from our team now? (Electronic Warfare)
            else if (electronicDroid(this) && getPlayer() == actionTarget[i]->getPlayer()) {
              setDroidActionTarget(this, nullptr, i);
            }
              // Is target blocked by a wall?
            else if (bDirect && visGetBlockingWall(this, actionTarget[i])) {
              setDroidActionTarget(this, nullptr, i);
            }
              // I have a target!
            else {
              bHasTarget = true;
            }
          }
            // This weapon doesn't have a target
          else {
            // Can we find a good target for the weapon?
            ::SimpleObject* psTemp;
            if (aiBestNearestTarget(this, &psTemp, i) >= 0) {
              // assuming aiBestNearestTarget checks for electronic warfare
              bHasTarget = true;
              setDroidActionTarget(this, psTemp, i); // this updates psDroid->psActionTarget[i] to != NULL
            }
          }
          // If we have a target for the weapon: is it visible?
          if (actionTarget[i] != nullptr &&
              visibleObject(this, actionTarget[i],
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
            const auto& psStats = weapons[i].getStats();
            wallBlocked = false;

            // has weapon a target? is target valid?
            if (actionTarget[i] != nullptr &&
                validTarget(this, actionTarget[i], i)) {
              // is target visible and weapon is not a Nullweapon?
              if (targetVisibile[i] && nonNullWeapon[i]) {
                // to fix a AA-weapon attack ground unit exploit 
                ::SimpleObject* psActionTarget = nullptr;
                blockingWall = dynamic_cast<Structure*>(visGetBlockingWall(this, actionTarget[i]));

                if (proj_Direct(psStats) && blockingWall) {
                  WEAPON_EFFECT weapEffect = psStats.weaponEffect;

                  if (!aiCheckAlliances(getPlayer(), blockingWall->getPlayer()) &&
                      asStructStrengthModifier[weapEffect][blockingWall->getStats().strength] >=
                         MIN_STRUCTURE_BLOCK_STRENGTH) {
                    psActionTarget = blockingWall;
                    setDroidActionTarget(this, psActionTarget, i); // attack enemy wall
                  }
                  else {
                    wallBlocked = true;
                  }
                }
                else {
                  psActionTarget = actionTarget[i];
                }

                // is the turret aligned with the target?
                if (!wallBlocked &&
                    actionTargetTurret(this, psActionTarget, &weapons[i])) {
                  // In range - fire !!!
                  combFire(&weapons[i], this, psActionTarget, i);
                }
              }
            }
          }
          // Droid don't have a visible target and it is not in pursue mode
          if (!hasVisibleTarget &&
              secondaryGetState(SECONDARY_ORDER::ATTACK_LEVEL) != DSS_ALEV_ALWAYS) {
            // Target lost
            action = MOVE;
          }
        }
          // it don't have a target, change to ACTION::MOVE
        else {
          action = ACTION::MOVE;
        }
        //check its a VTOL unit since adding Transporter's into multiPlayer
        /* check vtol attack runs */
        if (isVtol()) {
          actionUpdateVtolAttack(this);
        }
        break;
      case ACTION::ATTACK:
      case ACTION::ROTATE_TO_ATTACK:
        if (actionTarget[0] == nullptr &&
            actionTarget[1] != nullptr) {
          break;
        }
        ASSERT_OR_RETURN(, actionTarget[0] != nullptr, "target is NULL while attacking");

        if (action == ACTION::ROTATE_TO_ATTACK) {
          if (movement->status == MOVE_STATUS::TURN_TO_TARGET) {
            moveTurnDroid(actionTarget[0]->getPosition().x,
                          actionTarget[0]->getPosition().y);
            break; // Still turning.
          }
          action = ATTACK;
        }

        //check the target hasn't become one the same player ID - Electronic Warfare
        if (electronicDroid(this) &&
            getPlayer() == actionTarget[0]->getPlayer()) {
          for (unsigned i = 0; i < numWeapons(*this); ++i)
          {
            setDroidActionTarget(this, nullptr, i);
          }
          action = NONE;
          break;
        }

        bHasTarget = false;
        wallBlocked = false;
        for (auto i = 0; i < numWeapons(*this); ++i)
        {
          ::SimpleObject* psActionTarget;

          if (i > 0) {
            // if we're ordered to shoot something, and we can, shoot it
            if ((order->type == ORDER_TYPE::ATTACK || 
                 order->type == ORDER_TYPE::ATTACK_TARGET) &&
                actionTarget[i] != actionTarget[0] &&
                validTarget(this, actionTarget[0], i) &&
                actionInRange(this, actionTarget[0], i)) {
              setDroidActionTarget(this, actionTarget[0], i);
            }
            // if we still don't have a target, try to find one
            else {
              if (actionTarget[i] == nullptr &&
                  aiChooseTarget(this, &psTargets[i],
                                 i, false, nullptr)) {
                // Can probably just use psTarget instead of psTargets[i], and delete the psTargets variable.
                setDroidActionTarget(this, psTargets[i], i);
              }
            }
          }

          if (actionTarget[i]) {
            psActionTarget = actionTarget[i];
          }
          else {
            psActionTarget = actionTarget[0];
          }

          if (nonNullWeapon[i]
              && actionVisibleTarget(this, psActionTarget, i)
              && actionInRange(this, psActionTarget, i)) {
            const auto& psWeapStats = getWeapons()[i].getStats();
            auto weapEffect = psWeapStats.weaponEffect;
            blockingWall = dynamic_cast<Structure *>(visGetBlockingWall(this, psActionTarget));

            // if a wall is inbetween us and the target, try firing at the wall if our
            // weapon is good enough
            if (proj_Direct(psWeapStats) && blockingWall) {
              if (!aiCheckAlliances(getPlayer(), blockingWall->getPlayer()
                  && asStructStrengthModifier[weapEffect][blockingWall->getStats().strength] >=
                     MIN_STRUCTURE_BLOCK_STRENGTH) {
                psActionTarget = (SimpleObject*)blockingWall;
                setDroidActionTarget(psDroid, psActionTarget, i);
              }
              else {
                wallBlocked = true;
              }
            }

            if (!bHasTarget) {
              bHasTarget = actionInRange(this, psActionTarget,
                                         i, false);
            }

            if (validTarget(this, psActionTarget, i) &&
                !wallBlocked) {
              auto dirDiff = 0;

              if (!psWeapStats.rotate) {
                // no rotating turret - need to check aligned with target
                const auto targetDir = calcDirection(getPosition().x,
                                                                   getPosition().y,
                                                                   psActionTarget->getPosition().x,
                                                                   psActionTarget->getPosition().y);

                dirDiff = abs(angleDelta(targetDir - getRotation().direction));
              }

              if (dirDiff > FIXED_TURRET_DIR) {
                if (i > 0) {
                  if (actionTarget[i] != actionTarget[0]) {
                    // Nope, can't shoot this, try something else next time
                    setDroidActionTarget(this, nullptr, i);
                  }
                }
                else if (movement->status != MOVE_STATUS::SHUFFLE) {
                  action = ROTATE_TO_ATTACK;
                  moveTurnDroid(psActionTarget->getPosition().x,
                                psActionTarget->getPosition().y);
                }
              }
              else if (!psWeapStats.rotate ||
                       actionTargetTurret(this, psActionTarget, &weapons[i])) {
                /* In range - fire !!! */
                combFire(&weapons[i], this, psActionTarget, i);
              }
            }
            else if (i > 0) {
              // Nope, can't shoot this, try something else next time
              setDroidActionTarget(this, nullptr, i);
            }
          }
          else if (i > 0) {
            // Nope, can't shoot this, try something else next time
            setDroidActionTarget(this, nullptr, i);
          }
        }

        if (!bHasTarget || wallBlocked) {
          ::SimpleObject* psTarget;
          bool supportsSensorTower = !isVtol() &&
                  (psTarget = orderStateObj(this, ORDER_TYPE::FIRE_SUPPORT))
                  && dynamic_cast<Structure*>(psTarget);

          if (secHoldActive && (order->type == ORDER_TYPE::ATTACK_TARGET || 
                                order->type == ORDER_TYPE::FIRE_SUPPORT)) {
            action = ACTION::NONE; // secondary holding, cancel the order.
          }
          else if (secondaryGetState(SECONDARY_ORDER::HALT_TYPE) == DSS_HALT_PURSUE &&
                   !supportsSensorTower &&
                   !(order->type == ORDER_TYPE::HOLD ||
                     order->type == ORDER_TYPE::RETURN_TO_REPAIR)) {
            //We need this so pursuing doesn't stop if a unit is ordered to move somewhere while
            //it is still in weapon range of the target when reaching the end destination.
            //Weird case, I know, but keeps the previous pursue order intact.
            action = MOVE_TO_ATTACK; // out of range - chase it
          }
          else if (supportsSensorTower ||
                   order->type == ORDER_TYPE::NONE ||
                   order->type == ORDER_TYPE::HOLD ||
                   order->type == ORDER_TYPE::RETURN_TO_REPAIR) {
            // don't move if on hold or firesupport for a sensor tower
            // also don't move if we're holding position or waiting for repair
            action = NONE; // holding, cancel the order.
          }
            //Units attached to commanders are always guarding the commander
          else if (secHoldActive && order->type == ORDER_TYPE::GUARD && hasCommander()) {
            auto& commander = group->getCommander();

            if (commander.getOrder().type == ORDER_TYPE::ATTACK_TARGET ||
                commander.getOrder().type == ORDER_TYPE::FIRE_SUPPORT ||
                commander.getOrder().type == ORDER_TYPE::ATTACK) {
              action = MOVE_TO_ATTACK;
            }
            else {
              action = NONE;
            }
          }
          else if (secondaryGetState(SECONDARY_ORDER::HALT_TYPE) != DSS_HALT_HOLD) {
            action = MOVE_TO_ATTACK; // out of range - chase it
          }
          else {
            order->target = nullptr;
            action = NONE;
          }
        }
        break;

      case VTOL_ATTACK:
      {
        const bool targetIsValid = validTarget(this, actionTarget[0], 0);
        //uses vtResult
        if (actionTarget[0] != nullptr &&
            targetIsValid) {
          //check if vtol that its armed
          if ((vtolEmpty(*this)) ||
              (actionTarget[0] == nullptr) ||
              // check the target hasn't become one the same player ID - Electronic Warfare
              (hasElectronicWeapon() && (getPlayer() == actionTarget[0]->getPlayer())) ||
              // Huh? !targetIsValid can't be true, we just checked for it
              !targetIsValid) {
            moveToRearm(this);
            break;
          }

          for (auto i = 0; i < numWeapons(*this); ++i)
          {
            if (nonNullWeapon[i] &&
                validTarget(this, actionTarget[0], i)) {
              //I moved psWeapStats flag update there
              auto& psWeapStats = weapons[i].getStats();
              if (actionVisibleTarget(this, actionTarget[0], i)) {
                if (actionInRange(this, actionTarget[0], i)) {
                  if (getPlayer() == selectedPlayer) {
                    audio_QueueTrackMinDelay(ID_SOUND_COMMENCING_ATTACK_RUN2,
                                             VTOL_ATTACK_AUDIO_DELAY);
                  }

                  if (actionTargetTurret(this, actionTarget[0], &weapons[i])) {
                    // In range - fire !!!
                    combFire(&weapons[i], this,
                             actionTarget[0], i);
                  }
                }
                else {
                  actionTargetTurret(this, actionTarget[0], &weapons[i]);
                }
              }
            }
          }
        }

        /* circle around target if hovering and not cyborg */
        Vector2i attackRunDelta = getPosition().xy() - movement->destination;
        if (isStationary() || dot(attackRunDelta, attackRunDelta) < TILE_UNITS * TILE_UNITS) {
          actionAddVtolAttackRun(this);
        }
        else if (actionTarget[0] != nullptr &&
                 targetIsValid) {
          // if the vtol is close to the target, go around again
          Vector2i diff = (getPosition() - actionTarget[0]->getPosition()).xy();
          const unsigned rangeSq = dot(diff, diff);
          if (rangeSq < VTOL_ATTACK_TARGET_DIST * VTOL_ATTACK_TARGET_DIST)
          {
            // don't do another attack run if already moving away from the target
            diff = movement->destination - actionTarget[0]->getPosition().xy();
            if (dot(diff, diff) < VTOL_ATTACK_TARGET_DIST * VTOL_ATTACK_TARGET_DIST)
            {
              actionAddVtolAttackRun(this);
            }
          }
            // in case psWeapStats is still NULL
          else if (psWeapStats) {
            // if the vtol is far enough away head for the target again
            const int maxRange = proj_GetLongRange(psWeapStats, getPlayer());
            if (rangeSq > maxRange * maxRange) {
              // don't do another attack run if already heading for the target
              diff = movement->destination - actionTarget[0]->getPosition().xy();
              if (dot(diff, diff) > VTOL_ATTACK_TARGET_DIST * VTOL_ATTACK_TARGET_DIST) {
                moveDroidToDirect(actionTarget[0]->getPosition().x,
                                  actionTarget[0]->getPosition().y);
              }
            }
          }
        }
        break;
      }
      case ACTION::MOVE_TO_ATTACK:
        // send vtols back to rearm
        if (isVtol() && vtolEmpty(*this)) {
          moveToRearm(this);
          break;
        }
        ASSERT_OR_RETURN(, actionTarget[0] != nullptr, "action update move to attack target is NULL");
        for (auto i = 0; i < numWeapons(*this); ++i)
        {
          hasValidWeapon |= validTarget(this, actionTarget[0], i);
        }
        //check the target hasn't become one the same player ID - Electronic Warfare, and that the target is still valid.
        if ((hasElectronicWeapon() && getPlayer() == actionTarget[0]->getPlayer()) || !hasValidWeapon) {
          for (auto i = 0; i < numWeapons(*this); ++i)
          {
            setDroidActionTarget(this, nullptr, i);
          }
          action = NONE;
        }
        else {
          if (actionVisibleTarget(this, actionTarget[0], 0)) {
            for (auto i = 0; i < numWeapons(*this); ++i)
            {
              if (nonNullWeapon[i]
                  && validTarget(this, actionTarget[0], i)
                  && actionVisibleTarget(this, actionTarget[0], i)) {
                bool chaseBloke = false;
                const auto& psWeapStats = weapons[i].getStats();

                if (psWeapStats.rotate) {
                  actionTargetTurret(this, actionTarget[0], &weapons[i]);
                }

                if (!isVtol() &&
                    dynamic_cast<Droid*>(actionTarget[0]) &&
                    ((Droid*)actionTarget[0])->type == DROID_TYPE::PERSON &&
                    psWeapStats.fireOnMove) {
                  chaseBloke = true;
                }

                if (actionInRange(this, actionTarget[0], i) && !chaseBloke) {
                  /* init vtol attack runs count if necessary */
                  if (psPropStats->propulsionType == PROPULSION_TYPE::LIFT) {
                    action = VTOL_ATTACK;
                  }
                  else {
                    if (actionInRange(this, actionTarget[0], i, false)) {
                      moveStopDroid(this);
                    }

                    if (psWeapStats.rotate) {
                      action = ATTACK;
                    }
                    else {
                      action = ROTATE_TO_ATTACK;
                      moveTurnDroid(actionTarget[0]->getPosition().x,
                                    actionTarget[0]->getPosition().y);
                    }
                  }
                }
                else if (actionInRange(this, actionTarget[0], i)) {
                  // fire while closing range
                  if ((blockingWall = visGetBlockingWall(this, actionTarget[0])) && proj_Direct(
                          psWeapStats)) {
                    auto weapEffect = psWeapStats.weaponEffect;

                    if (!aiCheckAlliances(getPlayer(), blockingWall->getPlayer())
                        && asStructStrengthModifier[weapEffect][blockingWall->getStats().strength] >=
                           MIN_STRUCTURE_BLOCK_STRENGTH) {
                      //Shoot at wall if the weapon is good enough against them
                      combFire(&weapons[i], this, blockingWall, i);
                    }
                  }
                  else {
                    combFire(&weapons[i], this, actionTarget[0], i);
                  }
                }
              }
            }
          }
          else {
            for (auto i = 0; i < numWeapons(*this); ++i)
            {
              if ((weapons[i].getRotation().direction != 0) ||
                  (weapons[i].getRotation().pitch != 0)) {
                actionAlignTurret(this, i);
              }
            }
          }

          if (isStationary() && action != ATTACK) {
            /* Stopped moving but haven't reached the target - possibly move again */

            //'hack' to make the droid to check the primary turrent instead of all
            const auto& psWeapStats = weapons[0].getStats();

            if (order->type == ORDER_TYPE::ATTACK_TARGET && secHoldActive) {
              action = NONE; // on hold, give up.
            }
            else if (actionInsideMinRange(this, actionTarget[0], psWeapStats)) {
              if (proj_Direct(psWeapStats) && order->type != ORDER_TYPE::HOLD) {
                int pbx, pby;

                // try and extend the range
                actionCalcPullBackPoint(this, actionTarget[0], &pbx, &pby);
                moveDroidTo(this, (unsigned)pbx, (unsigned)pby);
              }
              else {
                if (psWeapStats.rotate) {
                  action = ATTACK;
                }
                else {
                  action = ROTATE_TO_ATTACK;
                  moveTurnDroid(actionTarget[0]->getPosition().x,
                                actionTarget[0]->getPosition().y);
                }
              }
            }
            else if (order->type != ORDER_TYPE::HOLD) {
              // try to close the range
              moveDroidTo(this, actionTarget[0]->getPosition().x, actionTarget[0]->getPosition().y);
            }
          }
        }
        break;
      case SULK:
        // unable to route to target ... don't do anything aggressive until time is up
        // we need to do something defensive at this point ???
        if (gameTime > timeActionStarted) {
          action = NONE;
          // Sulking is over lets get back to the action ... is this all I need to do to get it back into the action?
        }
        break;

      case MOVE_TO_BUILD:
        if (!order->structure_stats) {
          action = NONE;
          break;
        }
        else {
          // Determine if the droid can still build or help to build the ordered structure at the specified location
          auto desiredStructure = order->structure_stats;
          auto structureAtBuildPosition = getTileStructure(
                  map_coord(actionPos.x), map_coord(actionPos.y));

          if (nullptr != structureAtBuildPosition) {
            bool droidCannotBuild = false;

            if (!aiCheckAlliances(structureAtBuildPosition->getPlayer(), getPlayer())) {
              // Not our structure
              droidCannotBuild = true;
            }
            else if (isWall(structureAtBuildPosition->getStats().type) &&
            // there's an allied structure already there.  Is it a wall, and can the droid upgrade it to a defence or gate?
                (desiredStructure->type == STRUCTURE_TYPE::DEFENSE ||
                 desiredStructure->type == STRUCTURE_TYPE::GATE)) {
              // It's always valid to upgrade a wall to a defence or gate
              droidCannotBuild = false; // Just to avoid an empty branch
            }
            else if ((&structureAtBuildPosition->getStats() != desiredStructure.get()) &&
                     // ... it's not the exact same type as the droid was ordered to build
                     (structureAtBuildPosition->getStats().type == STRUCTURE_TYPE::WALL_CORNER &&
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
              if (order->type == ORDER_TYPE::LINE_BUILD &&
                  map_coord(order->pos) != map_coord(order->pos2)) {
                // the droid is doing a line build, and there's more to build. This will force
                // the droid to move to the next structure in the line build
                objTrace(getId(), "ACTION::MOVETOBUILD: line target is already built, or can't be built - moving to next structure in line");
                action = NONE;
              }
              else {
                // Cancel the current build order. This will move the truck onto the next order, if it has one, or halt in place.
                objTrace(getId(), "ACTION::MOVETOBUILD: target is already built, or can't be built - executing next order or halting");
                cancelBuild();
              }
              break;
            }
          }
        } // End of check for whether the droid can still succesfully build the ordered structure

        // The droid can still build or help with a build, and is moving to a location to do so - are we there yet, are we there yet, are we there yet?
        if (actionReachedBuildPos(this, actionPos.x, actionPos.y, order->direction,
                                  order->structure_stats.get())) {
          // We're there, go ahead and build or help to build the structure
          bool buildPosEmpty = actionRemoveDroidsFromBuildPos(getPlayer(), actionPos, order->direction,
                                                              order->structure_stats);
          if (!buildPosEmpty) {
            break;
          }
          bool helpBuild = false;
          // Got to destination - start building
          auto psStructStats = order->structure_stats;
          uint16_t dir = order->direction;
          moveStopDroid(this);
          objTrace(getId(), "Halted in our tracks - at construction site");
          if (order->type == ORDER_TYPE::BUILD && order->target == nullptr) {
            // Starting a new structure
            const Vector2i pos = actionPos;

            //need to check if something has already started building here?
            //unless its a module!
            if (IsStatExpansionModule(psStructStats.get())) {
              syncDebug("Reached build target: module");
              debug(LOG_NEVER, "ACTION::MOVETOBUILD: setUpBuildModule");
              setUpBuildModule();
            }
            else if (TileHasStructure(worldTile(pos))) {
              // structure on the build location - see if it is the same type
              auto psStruct = getTileStructure(map_coord(pos.x), map_coord(pos.y));
              if (&psStruct->getStats() == order->structure_stats.get() ||
                  (order->structure_stats->type == STRUCTURE_TYPE::WALL &&
                  psStruct->getStats().type == STRUCTURE_TYPE::WALL_CORNER)) {
                // same type - do a help build
                syncDebug("Reached build target: do-help");
                setDroidTarget(this, psStruct);
                helpBuild = true;
              }
              else if ((psStruct->getStats().type == STRUCTURE_TYPE::WALL ||
                        psStruct->getStats().type == STRUCTURE_TYPE::WALL_CORNER) &&
                       (order->structure_stats->type == STRUCTURE_TYPE::DEFENSE ||
                        order->structure_stats->type == STRUCTURE_TYPE::GATE)) {
                // building a gun tower or gate over a wall - OK
                if (droidStartBuild(this)) {
                  syncDebug("Reached build target: tower");
                  action = BUILD;
                }
              }
              else {
                syncDebug("Reached build target: already-structure");
                objTrace(getId(), "ACTION::MOVETOBUILD: tile has structure already");
                cancelBuild();
              }
            }
            else if (!validLocation(order->structure_stats.get(), pos, dir, getPlayer(), false))
            {
              syncDebug("Reached build target: invalid");
              objTrace(getId(), "ACTION::MOVETOBUILD: !validLocation");
              cancelBuild();
            }
            else if (droidStartBuild(this) == DroidStartBuildSuccess)
              // If DroidStartBuildPending, then there's a burning oil well, and we don't want to change to ACTION::BUILD until it stops burning.
            {
              syncDebug("Reached build target: build");
              action = ACTION::BUILD;
              timeActionStarted = gameTime;
              actionPointsDone = 0;
            }
          }
          else if (order->type == ORDER_TYPE::LINE_BUILD ||
                   order->type == ORDER_TYPE::BUILD) {
            // building a wall.
            auto psTile = worldTile(actionPos);
            syncDebug("Reached build target: wall");
            if (order->target == nullptr
                && (TileHasStructure(psTile)
                    || TileHasFeature(psTile))) {
              if (TileHasStructure(psTile)) {
                // structure on the build location - see if it is the same type
                auto psStruct = getTileStructure(map_coord(actionPos.x),
                                                             map_coord(actionPos.y));
                ASSERT(psStruct, "TileHasStructure, but getTileStructure returned nullptr");

        #if defined(WZ_CC_GNU) && !defined(WZ_CC_INTEL) && !defined(WZ_CC_CLANG) && (7 <= __GNUC__)
                        # pragma GCC diagnostic push
        # pragma GCC diagnostic ignored "-Wnull-dereference"
        #endif

                if (&psStruct->getStats() == order->structure_stats.get()) {
                  // same type - do a help build
                  setDroidTarget(this, psStruct);
                  helpBuild = true;
                }
                else if ((psStruct->getStats().type == STRUCTURE_TYPE::WALL ||
                          psStruct->getStats().type == STRUCTURE_TYPE::WALL_CORNER) &&
                         (order->structure_stats->type == STRUCTURE_TYPE::DEFENSE ||
                          order->structure_stats->type == STRUCTURE_TYPE::GATE)) {
                  // building a gun tower over a wall - OK
                  if (droidStartBuild(this)) {
                    objTrace(getId(), "ACTION::MOVETOBUILD: start building defense");
                    action = BUILD;
                  }
                }
                else if ((psStruct->getStats().type == STRUCTURE_TYPE::FACTORY &&
                          order->structure_stats->type == STRUCTURE_TYPE::FACTORY_MODULE) ||
                         (psStruct->getStats().type == STRUCTURE_TYPE::RESEARCH &&
                          order->structure_stats->type == STRUCTURE_TYPE::RESEARCH_MODULE) ||
                         (psStruct->getStats().type == STRUCTURE_TYPE::POWER_GEN &&
                          order->structure_stats->type == STRUCTURE_TYPE::POWER_MODULE) ||
                         (psStruct->getStats().type == STRUCTURE_TYPE::VTOL_FACTORY &&
                          order->structure_stats->type == STRUCTURE_TYPE::FACTORY_MODULE)) {
                  // upgrade current structure in a row
                  if (droidStartBuild(this)) {
                    objTrace(getId(), "ACTION::MOVETOBUILD: start building module");
                    action = BUILD;
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
                Feature* feature = getTileFeature(map_coord(actionPos.x), map_coord(actionPos.y));
                objTrace(getId(), "ACTION::MOVETOBUILD: tile has feature %d", feature->getStats()->subType);
                if (feature->getStats()->subType == FEATURE_TYPE::OIL_RESOURCE &&
                    order->structure_stats->type == STRUCTURE_TYPE::RESOURCE_EXTRACTOR) {
                  if (droidStartBuild(this)) {
                    objTrace(getId(), "ACTION::MOVETOBUILD: start building oil derrick");
                    action = BUILD;
                  }
                }
              }
              else {
                objTrace(getId(), "ACTION::MOVETOBUILD: blocked line build");
                cancelBuild();
              }
            }
            else if (droidStartBuild(this)) {
              action = BUILD;
            }
          }
          else {
            syncDebug("Reached build target: planned-help");
            objTrace(getId(), "ACTION::MOVETOBUILD: planned-help");
            helpBuild = true;
          }

          if (helpBuild) {
            // continuing a partially built structure (order = helpBuild)
            if (droidStartBuild(this)) {
              objTrace(getId(), "ACTION::MOVETOBUILD: starting help build");
              action = BUILD;
            }
          }
        }
        else if (isStationary()) {
          objTrace(getId(),
                   "ACTION::MOVETOBUILD: Starting to drive toward construction site - move status was %d",
                   (int)movement->status);
          moveDroidToNoFormation(this, actionPos.x, actionPos.y);
        }
        break;
      case BUILD:
        if (!order->structure_stats)
        {
          objTrace(getId(), "No target stats for build order - resetting");
          action = NONE;
          break;
        }
        if (isStationary() &&
            !actionReachedBuildPos(this, actionPos.x, actionPos.y, order->direction,
                                   order->structure_stats.get())) {
          objTrace(getId(), "ACTION::BUILD: Starting to drive toward construction site");
          moveDroidToNoFormation(this, actionPos.x, actionPos.y);
        }
        else if (!isStationary() &&
                 movement->status != MOVE_STATUS::TURN_TO_TARGET &&
                 movement->status != MOVE_STATUS::SHUFFLE &&
                 actionReachedBuildPos(this, actionPos.x, actionPos.y, order->direction,
                                       order->structure_stats.get()))
        {
          objTrace(getId(), "ACTION::BUILD: Stopped - at construction site");
          moveStopDroid(this);
        }
        if (action == SULK) {
          objTrace(getId(), "Failed to go to objective, aborting build action");
          action = NONE;
          break;
        }
        if (droidUpdateBuild()) {
          actionTargetTurret(this, actionTarget[0], &weapons[0]);
        }
        break;
      case MOVE_TO_DEMOLISH:
      case MOVE_TO_REPAIR:
      case MOVE_TO_RESTORE:
        if (!order->structure_stats) {
          action = NONE;
          break;
        }
        else {
          auto structureAtPos =
                  getTileStructure(map_coord(actionPos.x),
                                   map_coord(actionPos.y));

          if (structureAtPos == nullptr) {
            // no structure located at desired position. Move on
            action = NONE;
            break;
          }
          else if (order->type != ORDER_TYPE::RESTORE) {
            bool cantDoRepairLikeAction = false;

            if (!aiCheckAlliances(structureAtPos->getPlayer(), getPlayer())) {
              cantDoRepairLikeAction = true;
            }
            else if (order->type != ORDER_TYPE::DEMOLISH &&
                     structureAtPos->getHp() == structureBody(structureAtPos)) {
              cantDoRepairLikeAction = true;
            }
            else if (order->type == ORDER_TYPE::DEMOLISH &&
                     structureAtPos->getPlayer() != getPlayer()) {
              cantDoRepairLikeAction = true;
            }

            if (cantDoRepairLikeAction) {
              action = NONE;
              moveStopDroid(this);
              break;
            }
          }
        }
        // see if the droid is at the edge of what it is moving to
        if (actionReachedBuildPos(this, actionPos.x, actionPos.y,
                                  ((Structure*)actionTarget[0])->getRotation().direction,
                                  order->structure_stats.get())) {
          moveStopDroid(this);

          // got to the edge - start doing whatever it was meant to do
          droidStartAction(this);
          switch (action) {
            case MOVE_TO_DEMOLISH:
              action = DEMOLISH;
              break;
            case MOVE_TO_REPAIR:
              action = REPAIR;
              break;
            case MOVE_TO_RESTORE:
              action = RESTORE;
              break;
            default:
              break;
          }
        }
        else if (isStationary()) {
          moveDroidToNoFormation(this, actionPos.x, actionPos.y);
        }
        break;

      case DEMOLISH:
      case REPAIR:
      case RESTORE:
        if (!order->structure_stats) {
          action = NONE;
          break;
        }
        // setup for the specific action
        switch (action) {
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
        if (isStationary() && !actionReachedBuildPos(this, actionPos.x, actionPos.y,
                                                             ((Structure*)actionTarget[0])->getRotation().direction,
                                                             order->structure_stats.get())) {
          if (order->type != ORDER_TYPE::HOLD && (!secHoldActive ||
              (secHoldActive && order->type != ORDER_TYPE::NONE))) {
            objTrace(getId(), "Secondary order: Go to construction site");
            moveDroidToNoFormation(this, actionPos.x, actionPos.y);
          }
          else {
            action = NONE;
          }
        }
        else if (!isStationary() &&
                 movement->status != MOVE_STATUS::TURN_TO_TARGET &&
                 movement->status != MOVE_STATUS::SHUFFLE &&
                 actionReachedBuildPos(this, actionPos.x, actionPos.y,
                                       dynamic_cast<Structure*>(actionTarget[0])->getRotation().direction,
                                       order->structure_stats.get())) {
          objTrace(getId(), "Stopped - reached build position");
          moveStopDroid(this);
        }
        else if (actionUpdateFunc(this)) {
          //use 0 for non-combat(only 1 'weapon')
          actionTargetTurret(this, actionTarget[0], &weapons[0]);
        }
        else {
          action = NONE;
        }
        break;

      case MOVE_TO_REARM_POINT:
        if (isStationary()) {
          objTrace(getId(), "Finished moving onto the rearm pad");
          action = WAIT_DURING_REARM;
        }
        break;
      case MOVE_TO_REPAIR_POINT:
        if (order->rtrType == RTR_DATA_TYPE::REPAIR_FACILITY) {
          /* moving from front to rear of repair facility or rearm pad */
          if (actionReachedBuildPos(this, actionTarget[0]->getPosition().x,
                                    actionTarget[0]->getPosition().y,
                                    dynamic_cast<Structure*>(actionTarget[0])->getRotation().direction,
                                    &dynamic_cast<Structure*>(actionTarget[0])->getStats())) {
            objTrace(getId(), "Arrived at repair point - waiting for our turn");
            moveStopDroid(this);
            action = WAIT_DURING_REPAIR;
          }
          else if (isStationary()) {
            moveDroidToNoFormation(this, actionTarget[0]->getPosition().x,
                                   actionTarget[0]->getPosition().y);
          }
        }
        else if (order->rtrType == RTR_DATA_TYPE::DROID)
        {
          bool reached = actionReachedDroid(this, dynamic_cast<Droid*>(order->target));
          if (reached) {
            if (getHp() >= originalHp) {
              objTrace(getId(), "Repair not needed of droid %d", (int)getId());
              /* set droid points to max */
              body = originalHp;
              // if completely repaired then reset order
              secondarySetState(SECONDARY_ORDER::RETURN_TO_LOCATION, DSS_NONE);
              orderDroidObj(this, ORDER_TYPE::GUARD, order->target, ModeImmediate);
            }
            else {
              objTrace(getId(), "Stopping and waiting for repairs %d", (int)getId());
              moveStopDroid(this);
              action = WAIT_DURING_REPAIR;
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
        actionTargetTurret(this, actionTarget[0], &weapons[0]);
        if (!hasCbSensor()) {
          // make sure the target is within sensor range
          const auto xdiff = (int)getPosition().x - (int)actionTarget[0]->getPosition().x;
          const auto ydiff = (int)getPosition().y - (int)actionTarget[0]->getPosition().y;
          auto rangeSq = droidSensorRange(this);
          rangeSq = rangeSq * rangeSq;
          if (!visibleObject(this, actionTarget[0], false)
              || xdiff * xdiff + ydiff * ydiff >= rangeSq) {
            if (secondaryGetState(SECONDARY_ORDER::HALT_TYPE) != DSS_HALT_GUARD &&
                (order->type == ORDER_TYPE::NONE || order->type == ORDER_TYPE::HOLD)) {
              action = NONE;
            }
            else if ((!secHoldActive && order->type != ORDER_TYPE::HOLD) || 
                     (secHoldActive && order->type == ORDER_TYPE::OBSERVE)) {
              action = MOVE_TO_OBSERVE;
              moveDroidTo(this, actionTarget[0]->getPosition().x, 
                          actionTarget[0]->getPosition().y);
            }
          }
        }
        break;
      case MOVE_TO_OBSERVE:
        // align the turret
        actionTargetTurret(this, actionTarget[0], &weapons[0]);

        if (visibleObject(this, actionTarget[0], false)) {
          // make sure the target is within sensor range
          const auto xdiff = (int)getPosition().x - (int)actionTarget[0]->getPosition().x;
          const auto ydiff = (int)getPosition().y - (int)actionTarget[0]->getPosition().y;
          auto rangeSq = droidSensorRange(this);
          rangeSq = rangeSq * rangeSq;
          if ((xdiff * xdiff + ydiff * ydiff < rangeSq) &&
              !isStationary()) {
            action = OBSERVE;
            moveStopDroid(this);
          }
        }
        if (isStationary() && action == MOVE_TO_OBSERVE) {
          moveDroidTo(this, actionTarget[0]->getPosition().x, actionTarget[0]->getPosition().y);
        }
        break;
      case FIRE_SUPPORT:
        if (!order->target) {
          action = NONE;
          return;
        }
        //can be either a droid or a structure now - AB 7/10/98
        ASSERT_OR_RETURN(, (dynamic_cast<Droid*>(order->target) ||
                            dynamic_cast<Structure*>(order->target)) &&
                           aiCheckAlliances(order->target->getPlayer(), getPlayer()),
                          "ACTION::FIRESUPPORT: incorrect target type");

        // don't move VTOL's
        // also don't move closer to sensor towers
        if (!isVtol() && !dynamic_cast<Structure*>(order->target)) {
          auto diff = (getPosition() - order->target->getPosition()).xy();
          //Consider .shortRange here
          auto rangeSq = weapons[0].getStats().upgraded[getPlayer()].maxRange / 2;
          // move close to sensor
          rangeSq = rangeSq * rangeSq;
          if (dot(diff, diff) < rangeSq) {
            if (!isStationary()) {
              moveStopDroid(this);
            }
          }
          else {
            if (!isStationary()) {
              diff = order->target->getPosition().xy() - movement->destination;
            }
            if (isStationary() || dot(diff, diff) > rangeSq) {
              if (secHoldActive) {
                // droid on hold, don't allow moves.
                action = NONE;
              }
              else {
                // move in range
                moveDroidTo(this, order->target->getPosition().x,
                            order->target->getPosition().y);
              }
            }
          }
        }
        break;
      case MOVE_TO_DROID_REPAIR:
      {
        auto actionTargetObj = actionTarget[0];
        ASSERT_OR_RETURN(, actionTargetObj != nullptr &&
                           dynamic_cast<Droid*>(actionTargetObj),
                           "unexpected repair target");
        auto actionTarget_ = dynamic_cast<const Droid*>(actionTargetObj);
        if (actionTarget_->getHp() == actionTarget_->originalHp) {
          // target is healthy: nothing to do
          action = NONE;
          moveStopDroid(this);
          break;
        }
        auto diff = (getPosition() - actionTarget_[0].getPosition()).xy();
        // moving to repair a droid
        if (!actionTarget_[0] || // Target missing.
            (order->type != ORDER_TYPE::DROID_REPAIR &&
             dot(diff, diff) > 2 * REPAIR_MAXDIST * REPAIR_MAXDIST)) {
          // target further than 1.4142 * REPAIR_MAXDIST, and we aren't ordered to follow.
          action = NONE;
          return;
        }
        if (dot(diff, diff) < REPAIR_RANGE * REPAIR_RANGE) {
          // got to destination - start repair
          // rotate turret to point at droid being repaired
          // use 0 for repair droid
          actionTargetTurret(this, actionTarget_[0], &weapons[0]);
          droidStartAction(this);
          action = DROID_REPAIR;
        }
        if (isStationary()) {
          // Couldn't reach destination - try and find a new one
          actionPos = actionTarget_[0].getPosition().xy();
          moveDroidTo(this, actionPos.x, actionPos.y);
        }
        break;
      }
      case DROID_REPAIR:
      {
        int xdiff, ydiff;

        // If not doing self-repair (psActionTarget[0] is repair target)
        if (actionTarget[0] != this) {
          actionTargetTurret(this, actionTarget[0], &weapons[0]);
        }
          // Just self-repairing.
          // See if there's anything to shoot.
        else if (numWeapons(*this) > 0 && !isVtol() &&
                 (order->type == ORDER_TYPE::NONE ||
                  order->type == ORDER_TYPE::HOLD ||
                  order->type == ORDER_TYPE::RETURN_TO_REPAIR)) {
          for (auto i = 0; i < numWeapons(*this); ++i)
          {
            if (nonNullWeapon[i]) {
              ::SimpleObject* psTemp = nullptr;

              const auto& psWeapStats = weapons[i].getStats();
              if (psWeapStats.rotate &&
                  secondaryGetState(SECONDARY_ORDER::ATTACK_LEVEL) == DSS_ALEV_ALWAYS &&
                  aiBestNearestTarget(this, &psTemp, i) >= 0 && psTemp) {
                action = ATTACK;
                setDroidActionTarget(this, psTemp, 0);
                break;
              }
            }
          }
        }
        if (action != DROID_REPAIR) {
          break; // action has changed
        }

        //check still next to the damaged droid
        xdiff = (int)getPosition().x - (int)actionTarget[0]->getPosition().x;
        ydiff = (int)getPosition().y - (int)actionTarget[0]->getPosition().y;
        if (xdiff * xdiff + ydiff * ydiff > REPAIR_RANGE * REPAIR_RANGE) {
          if (order->type == ORDER_TYPE::DROID_REPAIR) {
            // damaged droid has moved off - follow if we're not holding position!
            actionPos = actionTarget[0]->getPosition().xy();
            action = MOVE_TO_DROID_REPAIR;
            moveDroidTo(this, actionPos.x, actionPos.y);
          }
          else {
            action = NONE;
          }
        }
        else {
          if (!droidUpdateDroidRepair()) {
            action = NONE;
            moveStopDroid(this);
            //if the order is RTR then resubmit order so that the unit will go to repair facility point
            if (orderState(this, ORDER_TYPE::RETURN_TO_REPAIR)) {
              orderDroid(this, ORDER_TYPE::RETURN_TO_REPAIR, ModeImmediate);
            }
          }
          else {
            // don't let the target for a repair shuffle
            if (((Droid*)actionTarget[0])->movement->status == MOVE_STATUS::SHUFFLE)
            {
              moveStopDroid((Droid*)actionTarget[0]);
            }
          }
        }
        break;
      }
      case WAIT_FOR_REARM:
        // wait here for the rearm pad to instruct the vtol to move
        if (actionTarget[0] == nullptr) {
          // rearm pad destroyed - move to another
          objTrace(getId(), "rearm pad gone - switch to new one");
          moveToRearm(this);
          break;
        }
        if (isStationary() && vtolHappy(*this)) {
          objTrace(getId(), "do not need to rearm after all");
          // don't actually need to rearm so just sit next to the rearm pad
          action = NONE;
        }
        break;
      case CLEAR_REARM_PAD:
        if (isStationary())
        {
          action = NONE;
          objTrace(getId(), "clearing rearm pad");
          if (!vtolHappy(*this))
            // Droid has cleared the rearm pad without getting rearmed. One way this can happen if a rearming pad was built under the VTOL while it was waiting for a pad.
          {
            moveToRearm(this); // Rearm somewhere else instead.
          }
        }
        break;
      case WAIT_DURING_REARM:
        // this gets cleared by the rearm pad
        break;
      case MOVE_TO_REARM:
        if (actionTarget[0] == nullptr) {
          // base destroyed - find another
          objTrace(getId(), "rearm gone - find another");
          moveToRearm(this);
          break;
        }

        if (visibleObject(this, actionTarget[0], false)) {
          auto psStruct = findNearestReArmPad(this, (Structure*)actionTarget[0], true);
          // got close to the rearm pad - now find a clear one
          objTrace(getId(), "Seen rearm pad - searching for available one");

          if (psStruct != nullptr) {
            // found a clear landing pad - go for it
            objTrace(getId(), "Found clear rearm pad");
            setDroidActionTarget(this, psStruct, 0);
          }
          action = WAIT_FOR_REARM;
        }

        if (isStationary() || action == WAIT_FOR_REARM) {
          Vector2i pos = actionTarget[0]->getPosition().xy();
          if (!actionVTOLLandingPos(this, &pos)) {
            // totally bunged up - give up
            objTrace(getId(), "Couldn't find a clear tile near rearm pad - returning to base");
            orderDroid(this, ORDER_TYPE::RETURN_TO_BASE, ModeImmediate);
            break;
          }
          objTrace(getId(), "moving to rearm pad at %d,%d (%d,%d)", (int)pos.x, (int)pos.y,
                   (int)(pos.x/TILE_UNITS), (int)(pos.y/TILE_UNITS));
          moveDroidToDirect(pos.x, pos.y);
        }
        break;
      default:
        ASSERT(!"unknown action", "unknown action");
        break;
    }

    if (action != MOVE_FIRE &&
        action != ATTACK &&
        action != MOVE_TO_ATTACK &&
        action != MOVE_TO_DROID_REPAIR &&
        action != DROID_REPAIR &&
        action != BUILD &&
        action != OBSERVE &&
        action != MOVE_TO_OBSERVE) {
      //use 0 for all non-combat droid types
      if (numWeapons(*this) == 0) {
        if (weapons[0].getRotation().direction != 0 ||
            weapons[0].getRotation().pitch != 0) {
          actionAlignTurret(this, 0);
        }
      }
      else {
        for (auto i = 0; i < numWeapons(*this); ++i)
        {
          if (weapons[i].getRotation().direction != 0 ||
              weapons[i].getRotation().pitch != 0) {
            actionAlignTurret(this, i);
          }
        }
      }
    }
  }

  /* Deals with building a module - checking if any droid is currently doing this
   - if so, helping to build the current one */
  void Droid::setUpBuildModule()
  {
    Vector2i tile = map_coord(order->pos);

    // check not another truck started
    auto psStruct = getTileStructure(tile.x, tile.y);
    if (psStruct) {
      // if a droid is currently building, or building is in progress of
      // being built/upgraded the droid's order should be HELP_BUILD
      if (checkDroidsBuilding(psStruct)) {
        // set up the help build scenario
        order->type = ORDER_TYPE::HELP_BUILD;
        setDroidTarget(this, (SimpleObject *)psStruct);
        if (droidStartBuild(this)) {
          action = BUILD;
          return;
        }
      }
      else {
        if (nextModuleToBuild(psStruct, -1) > 0) {
          // no other droids building so just start it off
          if (droidStartBuild(this)) {
            action = BUILD;
            return;
          }
        }
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
    int relativeDamage;

    // VTOLs (and transporters in MP) on the ground take triple damage
    if ((isVtol() || (isTransporter(*this) && bMultiPlayer)) &&
        (movement->status == MOVE_STATUS::INACTIVE)) {
      damage *= 3;
    }

    relativeDamage = objDamage(this, damage, originalHp, weaponClass,
                               weaponSubClass, isDamagePerSecond, minDamage);

    if (relativeDamage > 0) {
      // reset the attack level
      if (secondaryGetState(SECONDARY_ORDER::ATTACK_LEVEL) == DSS_ALEV_ATTACKED) {
        secondarySetState(SECONDARY_ORDER::ATTACK_LEVEL, DSS_ALEV_ALWAYS);
      }
      // Now check for auto return on droid's secondary orders (i.e. return on medium/heavy damage)
      secondaryCheckDamageLevel(this);
    }
    else if (relativeDamage < 0) {
      // Droid destroyed
      debug(LOG_ATTACK, "droid (%d): DESTROYED", getId());

      // Deal with score increase/decrease and messages to the player
      if (getPlayer() == selectedPlayer) {
        // TRANSLATORS:	Refers to the loss of a single unit, known by its name
        CONPRINTF(_("%s Lost!"), objInfo(this));
        scoreUpdateVar(WD_UNITS_LOST);
        audio_QueueTrackMinDelayPos(ID_SOUND_UNIT_DESTROYED, UNIT_LOST_DELAY,
                                    getPosition().x, getPosition().y, getPosition().z);
      }
        // only counts as a kill if it's not our ally
      else if (selectedPlayer < MAX_PLAYERS && !aiCheckAlliances(getPlayer(), selectedPlayer)) {
        scoreUpdateVar(WD_UNITS_KILLED);
      }

      // Do we have a dying animation?
      if (getDisplayData().imd_shape->objanimpie[ANIM_EVENT_DYING] &&
          animationEvent != ANIM_EVENT_DYING) {
        bool useDeathAnimation = true;
        //Babas should not burst into flames from non-heat weapons
        if (type == DROID_TYPE::PERSON) {
          if (weaponClass == WEAPON_CLASS::HEAT) {
            // NOTE: 3 types of screams are available ID_SOUND_BARB_SCREAM - ID_SOUND_BARB_SCREAM3
            audio_PlayObjDynamicTrack(this, ID_SOUND_BARB_SCREAM + (rand() % 3), nullptr);
          }
          else {
            useDeathAnimation = false;
          }
        }
        if (useDeathAnimation) {
          debug(LOG_DEATH, "%s droid %d (%p) is starting death animation", objInfo(this), (int)getId(),
                static_cast<void*>(this));
          timeAnimationStarted = gameTime;
          animationEvent = ANIM_EVENT_DYING;
        }
      }
      // Otherwise use the default destruction animation
      if (animationEvent != ANIM_EVENT_DYING) {
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
      }
    }
    return relativeDamage;
  }

  /* Do the AI for a droid */
  void Droid::aiUpdateDroid()
  {
    bool lookForTarget, updateTarget;

    if (isDead((SimpleObject*)this)) {
      return;
    }

    if (type != SENSOR && numWeapons(*this) == 0) {
      return;
    }

    lookForTarget = false;
    updateTarget = false;

    // look for a target if doing nothing
    if (orderState(this, ORDER_TYPE::NONE) ||
        orderState(this, ORDER_TYPE::GUARD) ||
        orderState(this, ORDER_TYPE::HOLD)) {
      lookForTarget = true;
    }
    // but do not choose another target if doing anything while guarding
    // exception for sensors, to allow re-targetting when target is doomed
    if (orderState(this, ORDER_TYPE::GUARD) &&
        action != NONE && type != SENSOR) {
      lookForTarget = false;
    }
    // don't look for a target if sulking
    if (action == SULK) {
      lookForTarget = false;
    }

    /* Only try to update target if already have some target */
    if (action == ATTACK ||
        action == MOVE_FIRE ||
        action == MOVE_TO_ATTACK ||
        action == ROTATE_TO_ATTACK) {
      updateTarget = true;
    }
    if ((orderState(this, ORDER_TYPE::OBSERVE) ||
         orderState(this, ORDER_TYPE::ATTACK_TARGET)) &&
        order->target && order->target->died) {
      lookForTarget = true;
      updateTarget = false;
    }

    /* Don't update target if we are sent to attack and reached attack destination (attacking our target) */
    if (orderState(this, ORDER_TYPE::ATTACK) &&
        actionTarget[0] == order->target) {
      updateTarget = false;
    }

    // don't look for a target if there are any queued orders
    if (listSize > 0) {
      lookForTarget = false;
      updateTarget = false;
    }

    // don't allow units to start attacking if they will switch to guarding the commander
    // except for sensors: they still look for targets themselves, because
    // they have wider view
    if (hasCommander() && type != SENSOR) {
      lookForTarget = false;
      updateTarget = false;
    }

    if (bMultiPlayer && isVtol() &&
        isHumanPlayer(getPlayer())) {
      lookForTarget = false;
      updateTarget = false;
    }

    // CB and VTOL CB droids can't autotarget.
    if (type == SENSOR && !hasStandardSensor()) {
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
        updateAttackTarget(dynamic_cast<::SimpleObject*>(this), i);
      }
    }

    /* Null target - see if there is an enemy to attack */
    if (lookForTarget && !updateTarget) {
      ::SimpleObject* psTarget;
      if (type == SENSOR) {
        if (aiChooseSensorTarget(this, &psTarget)) {
          if (!orderState(this, ORDER_TYPE::HOLD)
              && secondaryGetState(SECONDARY_ORDER::HALT_TYPE) == DSS_HALT_PURSUE) {
            order = std::make_unique<Order>(ORDER_TYPE::OBSERVE, *psTarget);
          }
          ::actionDroid(this, ACTION::OBSERVE, psTarget);
        }
      }
      else {
        if (aiChooseTarget((SimpleObject*)this, &psTarget, 0, true, nullptr)) {
          if (!orderState(this, ORDER_TYPE::HOLD) &&
              secondaryGetState(SECONDARY_ORDER::HALT_TYPE) == DSS_HALT_PURSUE) {
            order = std::make_unique<Order>(ORDER_TYPE::ATTACK, *psTarget);
          }
          ::actionDroid(this, ACTION::ATTACK, psTarget);
        }
      }
    }
  }

  /* Continue restoring a structure */
  bool Droid::droidUpdateRestore()
  {
    auto psStruct = dynamic_cast<Structure*>(order->target);

    ASSERT_OR_RETURN(false, action == RESTORE, "Unit is not restoring");
    ASSERT_OR_RETURN(false, psStruct, "Target is not a structure");
    ASSERT_OR_RETURN(false, numWeapons(*this) > 0, "Droid doesn't have any weapons");

    auto psStats = &weapons[0].getStats();
    ASSERT_OR_RETURN(false,
                     psStats->weaponSubClass == WEAPON_SUBCLASS::ELECTRONIC,
                     "unit's weapon is not EW");

    auto restorePoints = calcDamage(weaponDamage(psStats, getPlayer()),
                                        psStats->weaponEffect, (SimpleObject*)psStruct);

    auto pointsToAdd = restorePoints * (gameTime - timeActionStarted) /
                           GAME_TICKS_PER_SEC;

    psStruct->resistance = (SWORD)(psStruct->getResistance() + (pointsToAdd - actionPointsDone));

    //store the amount just added
    actionPointsDone = pointsToAdd;

    /* check if structure is restored */
    if (psStruct->getResistance() < (int)structureResistance(
            &psStruct->getStats(), psStruct->getPlayer())) {
      return true;
    }
    else {
      addConsoleMessage(_("Structure Restored"),
                        CONSOLE_TEXT_JUSTIFICATION::DEFAULT,
                        SYSTEM_MESSAGE);
      psStruct->resistance = (UWORD)structureResistance(&psStruct->getStats(),
                                                        psStruct->getPlayer());
      return false;
    }
  }

  bool Droid::droidUpdateDroidRepair()
  {
    ASSERT_OR_RETURN(false, action == DROID_REPAIR, "Unit does not have unit repair order");
    ASSERT_OR_RETURN(false, asBits[COMP_REPAIRUNIT] != 0, "Unit does not have a repair turret");

    auto psDroidToRepair = dynamic_cast<Droid*>(actionTarget[0]);
    ASSERT_OR_RETURN(false, psDroidToRepair, "Target is not a unit");
    auto needMoreRepair = droidUpdateDroidRepairBase(this, psDroidToRepair);
    if (needMoreRepair &&
        psDroidToRepair->order->type == ORDER_TYPE::RETURN_TO_REPAIR &&
        psDroidToRepair->order->rtrType == RTR_DATA_TYPE::DROID
        && psDroidToRepair->getAction() == NONE) {
      psDroidToRepair->action = WAIT_DURING_REPAIR;
    }
    if (!needMoreRepair &&
        psDroidToRepair->order->type == ORDER_TYPE::RETURN_TO_REPAIR &&
        psDroidToRepair->order->rtrType == RTR_DATA_TYPE::DROID) {
      // if psDroidToRepair has a commander, commander will call him back anyway
      // if no commanders, just DORDER_GUARD the repair turret
      orderDroidObj(psDroidToRepair, ORDER_TYPE::GUARD, this, ModeImmediate);
      psDroidToRepair->secondarySetState(SECONDARY_ORDER::RETURN_TO_LOCATION, DSS_NONE);
      psDroidToRepair->order->target = nullptr;
    }
    return needMoreRepair;
  }

  /* Update a construction droid while it is building
     returns true while building continues */
  bool Droid::droidUpdateBuild()
  {
    ASSERT_OR_RETURN(false, action == BUILD, "%s (order %s) has wrong action for construction: %s",
                     droidGetName(this), getDroidOrderName(order->type),
                     getDroidActionName(action));

    auto psStruct = dynamic_cast<Structure*>(order->target);
    if (psStruct == nullptr) {
      // target missing, stop trying to build it.
      action = NONE;
      return false;
    }
    ASSERT_OR_RETURN(false, psStruct, "target is not a structure");
    ASSERT_OR_RETURN(false, asBits[COMP_CONSTRUCT] < numConstructStats, "Invalid construct pointer for unit");

    // First check the structure hasn't been completed by another droid
    if (psStruct->getState() == STRUCTURE_STATE::BUILT) {
      // Check if line order build is completed, or we are not carrying out a line order build
      if (order->type != ORDER_TYPE::LINE_BUILD ||
          map_coord(order->pos) == map_coord(order->pos2)) {
        cancelBuild();
      }
      else {
        action = NONE; // make us continue line build
        setDroidTarget(this, nullptr);
        setDroidActionTarget(this, nullptr, 0);
      }
      return false;
    }

    // make sure we still 'own' the building in question
    if (!aiCheckAlliances(psStruct->getPlayer(), getPlayer())) {
      cancelBuild(); // stop what you are doing fool it isn't ours anymore!
      return false;
    }

    auto constructPoints = constructorPoints(asConstructStats + psDroid->
            asBits[COMP_CONSTRUCT], getPlayer());

    auto pointsToAdd = constructPoints * (gameTime - timeActionStarted) /
                           GAME_TICKS_PER_SEC;

    structureBuild(psStruct, this, pointsToAdd - actionPointsDone, constructPoints);

    // store the amount just added
    actionPointsDone = pointsToAdd;
    addConstructorEffect(psStruct);
    return true;
  }

  // recycle a droid (retain it's experience and some of it's cost)
  void Droid::recycleDroid()
  {
    // store the droids kills
    if (experience > 0) {
      recycled_experience[getPlayer()].push(experience);
    }

    // return part of the cost of the droid
    auto cost = calcDroidPower(this);
    cost = (cost / 2) * getHp() / originalHp;

    addPower(getPlayer(), cost);

    // hide the droid
    visibilityState.fill(0);

    if (group) {
      group->remove(this);
    }

    triggerEvent(TRIGGER_OBJECT_RECYCLED, this);
    vanishDroid(this);

    auto position = getPosition().xzy();
    const auto mapCoord = map_coord({getPosition().x, getPosition().y});
    const auto psTile = mapTile(mapCoord);

    if (tileIsClearlyVisible(psTile)) {
      addEffect(&position, EFFECT_GROUP::EXPLOSION,
                EFFECT_TYPE::EXPLOSION_TYPE_DISCOVERY,
                false, nullptr, false,
                gameTime - deltaGameTime + 1);
    }
  }

  unsigned Droid::getOriginalHp() const
  {
    return originalHp;
  }

  /* The main update routine for all droids */
  void Droid::droidUpdate()
  {
    Vector3i dv;
    unsigned percentDamage, emissionInterval;
    ::SimpleObject* psBeingTargetted = nullptr;
    unsigned i;

#ifdef DEBUG
    // Check that we are (still) in the sensor list
if (psDroid->droidType == DROID_SENSOR)
{
  SimpleObject* psSensor;

  for (psSensor = apsSensorList[0]; psSensor; psSensor = psSensor->psNextFunc)
  {
    if (psSensor == (SimpleObject*)psDroid)
    {
      break;
    }
  }
  ASSERT(psSensor == (SimpleObject *)psDroid, "%s(%p) not in sensor list!",
         droidGetName(psDroid), static_cast<void *>(psDroid));
}
#endif

    syncDebugDroid(this, '<');

    if (flags.test(static_cast<std::size_t>(OBJECT_FLAG::DIRTY))) {
      visTilesUpdate(this);
      upgradeHitPoints();
      flags.set(static_cast<std::size_t>(OBJECT_FLAG::DIRTY), false);
    }

    // Save old droid position, update time.
    previousLocation = getSpacetime();
    setTime(gameTime);
    for (i = 0; i < MAX(1, numWeapons(*this)); ++i)
    {
      weapons[i].previousRotation = weapons[i].getRotation();
    }

    if (animationEvent != ANIM_EVENT_NONE) {
      auto imd = getDisplayData().imd_shape->objanimpie[animationEvent];
      if (imd && imd->objanimcycles > 0 && gameTime > timeAnimationStarted + imd->objanimtime * imd->
              objanimcycles) {
        // Done animating (animation is defined by body - other components should follow suit)
        if (animationEvent == ANIM_EVENT_DYING) {
          debug(LOG_DEATH, "%s (%d) died to burn anim (died=%d)", objInfo(this), (int)getId(),
                (int)psDroid->died);
          destroyDroid(this, gameTime);
          return;
        }
        animationEvent = ANIM_EVENT_NONE;
      }
    }
    else if (animationEvent == ANIM_EVENT_DYING) {
      return; // rest below is irrelevant if dead
    }

    // ai update droid
    aiUpdateDroid();

    // Update the droids order.
    orderUpdateDroid();

    // update the action of the droid
    actionUpdateDroid();

    syncDebugDroid(this, 'M');

    // update the move system
    moveUpdateDroid(this);

    /* Only add smoke if they're visible */
    if (visibleToSelectedPlayer() && type != PERSON) {
      // need to clip this value to prevent overflow condition
      percentDamage = 100 - clip<unsigned>(PERCENT(getHp(), originalHp), 0, 100);

      // Is there any damage?
      if (percentDamage >= 25) {
        if (percentDamage >= 100) {
          percentDamage = 99;
        }

        emissionInterval = CALC_DROID_SMOKE_INTERVAL(percentDamage);

        uint32_t effectTime = std::max(gameTime - deltaGameTime + 1, lastEmission + emissionInterval);
        if (gameTime >= effectTime) {
          dv.x = getPosition().x + DROID_DAMAGE_SPREAD;
          dv.z = getPosition().y + DROID_DAMAGE_SPREAD;
          dv.y = getPosition().z;

          dv.y += (getDisplayData().imd_shape->max.y * 2);
          addEffect(&dv, EFFECT_GROUP::SMOKE, EFFECT_TYPE::SMOKE_TYPE_DRIFTING_SMALL,
                    false, nullptr, 0, effectTime);
          lastEmission = effectTime;
        }
      }
    }

    /* Are we a sensor droid or a command droid? Show where we target for selectedPlayer. */
    if (getPlayer() == selectedPlayer &&
        (type == SENSOR || type == DROID_TYPE::COMMAND)) {

      /* If we're attacking or sensing (observing), then... */
      if ((psBeingTargetted = orderStateObj(this, ORDER_TYPE::ATTACK))
          || (psBeingTargetted = orderStateObj(this, ORDER_TYPE::OBSERVE))) {
        psBeingTargetted->flags.set(static_cast<std::size_t>(OBJECT_FLAG::TARGETED), true);
      }
      else if (secondaryGetState(SECONDARY_ORDER::HALT_TYPE) != DSS_HALT_PURSUE &&
               actionTarget[0] != nullptr &&
               validTarget(this, actionTarget[0], 0) &&
               (action == ATTACK || action == OBSERVE ||
                orderState(this, ORDER_TYPE::HOLD))) {
        psBeingTargetted = actionTarget[0];
        psBeingTargetted->flags.set(OBJECT_FLAG::TARGETED, true);
      }
    }

    // if we are a repair turret, then manage incoming damaged droids, (just like repair facility)
    // unlike a repair facility
    // 	- we don't really need to move droids to us, we can come ourselves
    //	- we don't steal work from other repair turrets/ repair facilities
    if (type == REPAIRER || type == CYBORG_REPAIR) {
      for (auto& psOther : apsDroidLists[getPlayer()])
      {
        // unlike repair facility, no droid  can have DORDER_RTR_SPECIFIED with another droid as target, so skip that check
        if (psOther.getOrder().type == ORDER_TYPE::RETURN_TO_REPAIR &&
            psOther.getOrder().rtrType == RTR_DATA_TYPE::DROID &&
            psOther.getAction() != WAIT_FOR_REPAIR &&
            psOther.getAction() != MOVE_TO_REPAIR_POINT &&
            psOther.getAction() != WAIT_DURING_REPAIR) {
          if (psOther.getHp() >= psOther.getOriginalHp()) {
            // set droid points to max
            psOther.setHp(psOther.getOriginalHp());
            // if completely repaired reset order
            psOther.secondarySetState(SECONDARY_ORDER::RETURN_TO_LOCATION, DSS_NONE);

            if (psOther.hasCommander()) {
              // return a droid to it's command group
              auto psCommander = psOther.group->psCommander;
              orderDroidObj(psOther, ORDER_TYPE::GUARD, psCommander, ModeImmediate);
            }
            continue;
          }
        }

        else if (psOther.getOrder().rtrType == RTR_DATA_TYPE::DROID
                 // is being, or waiting for repairs..
                 && (psOther.getAction() == WAIT_FOR_REPAIR ||
                     psOther.getAction() == WAIT_DURING_REPAIR) &&
                     psOther.getOrder().target == this) {
          if (!actionReachedDroid(this, &psOther)) {
            ::actionDroid(psOther, MOVE, this, getPosition().x, getPosition().y);
          }
        }
      }
    }

    // See if we can and need to self repair.
    if (!isVtol() && getHp() < originalHp && asBits[COMP_REPAIRUNIT] != 0 &&
        selfRepairEnabled(getPlayer()) {
      droidUpdateDroidSelfRepair(this);
    }

    /* Update the fire damage data */
    if (psDroid->periodicalDamageStart != 0 &&
        psDroid->periodicalDamageStart != gameTime - deltaGameTime) {
      // (-deltaGameTime, since projectiles are updated after droids)
      // the periodicalDamageStart has been set, but is not from the previous tick, so we must be out of the fire
      psDroid->periodicalDamage = 0; // reset periodical damage done this tick
      if (psDroid->periodicalDamageStart + BURN_TIME < gameTime) {
        // finished periodical damaging
        psDroid->periodicalDamageStart = 0;
      }
      else {
        // do hardcoded burn damage (this damage automatically applied after periodical damage finished)
        droidDamage(psDroid, BURN_DAMAGE, WC_HEAT, WSC_FLAME, gameTime - deltaGameTime / 2 + 1, true,
                    BURN_MIN_DAMAGE);
      }
    }

    // At this point, the droid may be dead due to periodical damage or hardcoded burn damage.
    if (isDead(psDroid)) {
      return;
    }

    calcDroidIllumination(psDroid);

    // Check the resistance level of the droid
    if ((getId() + gameTime) / 833 != (getId() + gameTime - deltaGameTime) / 833) {
      // Zero resistance means not currently been attacked - ignore these
      if (resistance && resistance < droidResistance(this)) {
        // Increase over time if low
        resistance++;
      }
    }

    syncDebugDroid(this, '>');
  }

  /* Set up a droid to build a structure - returns true if successful */
  DroidStartBuild Droid::droidStartBuild()
  {
    ::Structure* psStruct = nullptr;

    /* See if we are starting a new structure */
    if (order->target == nullptr &&
        (order->type == ORDER_TYPE::BUILD ||
         order->type == ORDER_TYPE::LINE_BUILD)) {
      auto psStructStat = order->structure_stats;

      auto ia = (ItemAvailability)apStructTypeLists[getPlayer()][psStructStat - asStructureStats];
      if (ia != AVAILABLE && ia != REDUNDANT) {
        ASSERT(false, "Cannot build \"%s\" for player %d.", psStructStat->name.toUtf8().c_str(), getPlayer());
        cancelBuild();
        objTrace(getId(), "DroidStartBuildFailed: not researched");
        return DroidStartBuildFailed;
      }

      //need to check structLimits have not been exceeded
      if (psStructStat->curCount[getPlayer()] >= psStructStat->upgraded_stats[getPlayer()].limit) {
        cancelBuild();
        objTrace(getId(), "DroidStartBuildFailed: structure limits");
        return DroidStartBuildFailed;
      }
      // Can't build on burning oil derricks.
      if (psStructStat->type == STRUCTURE_TYPE::RESOURCE_EXTRACTOR &&
          fireOnLocation(order->pos.x, order->pos.y)) {
        // Don't cancel build, since we can wait for it to stop burning.
        objTrace(getId(), "DroidStartBuildPending: burning");
        return DroidStartBuildPending;
      }
      //ok to build
      psStruct = buildStructureDir(psStructStat.get(),
                                   order->pos.x, order->pos.y,
                                   order->direction,
                                   getPlayer(), false);
      if (!psStruct) {
        cancelBuild();
        objTrace(getId(), "DroidStartBuildFailed: buildStructureDir failed");
        return DroidStartBuildFailed;
      }
      psStruct->setHp(psStruct->getHp() + 9 / 10); // Structures start at 10% health. Round up.
    }
    else {
      // Check the structure is still there to build (joining
      // a partially built struct)
      psStruct = dynamic_cast<Structure*>(order->target);
      if (!psStruct) {
        psStruct = dynamic_cast<Structure*>(
                worldTile(actionPos)->psObject);
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
          aiCheckAlliances(psStruct->getPlayer(), getPlayer())) {
        timeActionStarted = gameTime;
        actionPointsDone = 0;
        setDroidTarget(this, psStruct);
        setDroidActionTarget(this, psStruct, 0);
        objTrace(getId(), "DroidStartBuild: set target");
      }

      if (psStruct->visibleToSelectedPlayer()) {
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
  bool Droid::moveDroidToBase(unsigned x, unsigned y, bool bFormation, FPATH_MOVETYPE moveType)
  {
    using enum MOVE_STATUS;
    FPATH_RETVAL retVal = FPR_OK;

    // in multiPlayer make Transporter move like the vtols
    if (isTransporter(*this) && game.maxPlayers == 0) {
      fpathSetDirectRoute(this, x, y);
      movement->status = NAVIGATE;
      movement->pathIndex = 0;
      return true;
    }
      // NOTE: While Vtols can fly, then can't go through things, like the transporter.
    else if ((game.maxPlayers > 0 && isTransporter(*this))) {
      fpathSetDirectRoute(this, x, y);
      retVal = FPR_OK;
    }
    else {
      retVal = fpathDroidRoute(this, x, y, moveType);
    }

    if (retVal == FPR_OK) {
      // bit of a hack this - john
      // if astar doesn't have a complete route, it returns a route to the nearest clear tile.
      // the location of the clear tile is in DestinationX,DestinationY.
      // reset x,y to this position so the formation gets set up correctly
      x = movement->destination.x;
      y = movement->destination.y;

      objTrace(getId(), "unit %d: path ok - base Speed %u, speed %d, target(%u|%d, %u|%d)",
               (int)getId(), baseSpeed, movement->speed, x, map_coord(x), y, map_coord(y));

      movement->status = NAVIGATE;
      movement->pathIndex = 0;
    }
    else if (retVal == FPR_WAIT) {
      // the route will be calculated by the path-finding thread
      movement->status = WAIT_FOR_ROUTE;
      movement->destination.x = x;
      movement->destination.y = y;
    }
    else // if (retVal == FPR_FAILED)
    {
      objTrace(getId(), "Path to (%d, %d) failed for droid %d", (int)x, (int)y, (int)getId());
      movement->status = INACTIVE;
      ::actionDroid(this, SULK);
      return false;
    }
    return true;
  }

  /**
   * Move a droid directly to a location.
   * @note This is (or should be) used for VTOLs only.
   */
  void Droid::moveDroidToDirect(unsigned x, unsigned y)
  {
    ASSERT_OR_RETURN(, isVtol(), "Only valid for a VTOL unit");

    fpathSetDirectRoute(this, x, y);
    movement->status = MOVE_STATUS::NAVIGATE;
    movement->pathIndex = 0;
  }

  /**
   * Turn a droid towards a given location.
   */
  void Droid::moveTurnDroid(unsigned x, unsigned y)
  {
    auto moveDir = calcDirection(getPosition().x, getPosition().y, x, y);

    if (getRotation().direction != moveDir) {
      movement->target.x = x;
      movement->target.y = y;
      movement->status = MOVE_STATUS::TURN_TO_TARGET;
    }
  }

  // Tell a droid to move out the way for a shuffle
  void Droid::moveShuffleDroid(Vector2i s)
  {
    int mx, my;
    bool frontClear = true, leftClear = true, rightClear = true;
    int lvx, lvy, rvx, rvy, svx, svy;
    int shuffleMove;

    auto shuffleDir = iAtan2(s);
    auto shuffleMag = iHypot(s);

    if (shuffleMag == 0) {
      return;
    }

    shuffleMove = SHUFFLE_MOVE;

    // calculate the possible movement vectors
    svx = s.x * shuffleMove / shuffleMag; // Straight in the direction of s.
    svy = s.y * shuffleMove / shuffleMag;

    lvx = -svy; // 90° to the... right?
    lvy = svx;

    rvx = svy; // 90° to the... left?
    rvy = -svx;

    // check for blocking tiles
    if (fpathBlockingTile(map_coord((int)getPosition().x + lvx),
                          map_coord((int)getPosition().y + lvy), propulsion->propulsionType)) {
      leftClear = false;
    }
    else if (fpathBlockingTile(map_coord((int)getPosition().x + rvx),
                               map_coord((int)getPosition().y + rvy), propulsion->propulsionType)) {
      rightClear = false;
    }
    else if (fpathBlockingTile(map_coord((int)getPosition().x + svx),
                               map_coord((int)getPosition().y + svy), propulsion->propulsionType)) {
      frontClear = false;
    }

    // find any droids that could block the shuffle
    static GridList gridList; // static to avoid allocations.
    gridList = gridStartIterate(getPosition().x, getPosition().y, SHUFFLE_DIST);
    for (auto & gi : gridList)
    {
      auto psCurr = dynamic_cast<Droid*>(gi);
      if (psCurr == nullptr || psCurr->died || psCurr == this) {
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
    if (movement->status != MOVE_STATUS::SHUFFLE) {
      movement->shuffleStart = gameTime;
    }
    movement->status = MOVE_STATUS::SHUFFLE;
    movement->src = getPosition().xy();
    movement->target = tar;
    movement->path.clear();
    movement->pathIndex = 0;
  }

  /** 
   * This function assigns a state to a droid. It returns true if
   * it assigned and false if it failed to assign.
   */
  bool Droid::secondarySetState(SECONDARY_ORDER sec, SECONDARY_STATE state, QUEUE_MODE mode)
  {
    unsigned currState;
    STRUCTURE_TYPE factType;
    STRUCTURE_TYPE prodType;
    int factoryInc;
    bool retVal;
    ::Droid *psTransport, *psNext;
    ORDER_TYPE order_;

    currState = secondaryOrder;
    if (bMultiMessages && mode == ModeQueue) {
      currState = secondaryOrderPending;
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
        if (type == COMMAND) {
          secondaryMask = DSS_ASSPROD_FACT_MASK;
          secondarySet = state & DSS_ASSPROD_MASK;
        }
        break;
      case ASSIGN_CYBORG_PRODUCTION:
        if (type == COMMAND) {
          secondaryMask = DSS_ASSPROD_CYB_MASK;
          secondarySet = state & DSS_ASSPROD_MASK;
        }
        break;
      case ASSIGN_VTOL_PRODUCTION:
        if (type == COMMAND) {
          secondaryMask = DSS_ASSPROD_VTOL_MASK;
          secondarySet = state & DSS_ASSPROD_MASK;
        }
        break;
      case CLEAR_PRODUCTION:
        if (type == COMMAND) {
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
            psTransport = FindATransporter(this);
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
      secondaryOrderPending = newSecondaryState;
      ++secondaryOrderPendingCount;
      // wait for our order before changing the droid
      return true; 
    }


    // set the state for any droids in the command group
    if ((sec != RECYCLE) &&
        type == COMMAND &&
        group &&
        group->isCommandGroup()) {
      group->setSecondary(sec, state);
    }

    retVal = true;
    switch (sec) {
      case ATTACK_RANGE:
        currState = (currState & ~DSS_ARANGE_MASK) | state;
        break;

      case REPAIR_LEVEL:
        currState = (currState & ~DSS_REPLEV_MASK) | state;
        secondaryOrder = currState;
        secondaryCheckDamageLevel(this);
        break;

      case ATTACK_LEVEL:
        currState = (currState & ~DSS_ALEV_MASK) | state;
        if (state == DSS_ALEV_NEVER) {
          if (orderState(this, ORDER_TYPE::ATTACK)) {
            // just kill these orders
            orderDroid(this, ORDER_TYPE::STOP, ModeImmediate);
            if (isVtol()) {
              moveToRearm(this);
            }
          }
          else if (droidAttacking(this)) {
            // send the unit back to the guard position
            ::actionDroid(this, NONE);
          }
          else if (orderState(this, ORDER_TYPE::PATROL)) {
            // send the unit back to the patrol
            ::actionDroid(this, RETURN_TO_POS, actionPos.x, actionPos.y);
          }
        }
        break;


      case ASSIGN_PRODUCTION:
      case ASSIGN_CYBORG_PRODUCTION:
      case ASSIGN_VTOL_PRODUCTION:
        
      #ifdef DEBUG
         debug(LOG_NEVER, "order factories %s\n", secondaryPrintFactories(State));
      #endif
                
        if (sec == ASSIGN_PRODUCTION) {
          prodType = STRUCTURE_TYPE::FACTORY;
        }
        else if (sec == ASSIGN_CYBORG_PRODUCTION) {
          prodType = STRUCTURE_TYPE::CYBORG_FACTORY;
        }
        else {
          prodType = STRUCTURE_TYPE::VTOL_FACTORY;
        }

        if (type == COMMAND) {
          // look for the factories
          for (auto& psStruct : apsStructLists[getPlayer()])
          {
            factType = psStruct->getStats().type;
            if (factType == STRUCTURE_TYPE::FACTORY ||
                factType == STRUCTURE_TYPE::VTOL_FACTORY ||
                factType == STRUCTURE_TYPE::CYBORG_FACTORY) {
              factoryInc = dynamic_cast<Factory*>(psStruct.get())->psAssemblyPoint->factoryInc;
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
                assignFactoryCommandDroid(psStruct.get(), this);
              }
              else if ((prodType == factType) &&
                       (currState & (1 << factoryInc)) &&
                       !(state & (1 << factoryInc))) {
                // remove this factory from the command droid
                assignFactoryCommandDroid(psStruct.get(), nullptr);
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

          #ifdef DEBUG
              debug(LOG_NEVER, "final factories %s\n", secondaryPrintFactories(CurrState));
          #endif
        }
        break;

      case CLEAR_PRODUCTION:
        if (type == COMMAND) {
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
          group = UBYTE_MAX;
          if (group) {
            if (type == COMMAND) {
              // remove all the units from the commanders group
              for (auto psCurr : group->getMembers())
              {
                psCurr->group->remove(psCurr);
                orderDroid(psCurr, ORDER_TYPE::STOP, ModeImmediate);
              }
            }
            else if (group->isCommandGroup()) {
              group->remove(this);
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
        else
        {
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
          order_ = ORDER_TYPE::NONE;
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
              psTransport = FindATransporter(this);
              if (psTransport != nullptr) {
                order_ = ORDER_TYPE::EMBARK;
                currState |= DSS_RTL_TRANSPORT;
                if (!orderState(this, ORDER_TYPE::EMBARK)) {
                  orderDroidObj(this, ORDER_TYPE::EMBARK, psTransport, ModeImmediate);
                }
              }
              else {
                retVal = false;
              }
              break;
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
        else if (cmdDroidGetDesignator(getPlayer()) == this) {
          cmdDroidClearDesignator(getPlayer());
        }
        break;
      default:
        break;
    }

    if (currState != newSecondaryState) {
      debug(LOG_WARNING,
            "Guessed the new secondary state incorrectly, expected 0x%08X, got 0x%08X, "
            "was 0x%08X, sec = %d, state = 0x%08X.",
            newSecondaryState, currState, secondaryOrder, sec, state);
    }
    secondaryOrder = currState;
    secondaryOrderPendingCount = std::max(secondaryOrderPendingCount - 1, 0);
    if (secondaryOrderPendingCount == 0) {
      secondaryOrderPending = secondaryOrder;
      // if no orders are pending, make sure UI uses the actual state.
    }
    return retVal;
  }

  /// Balance the load at random - always prefer faster repairs
  RtrBestResult Droid::decideWhereToRepairAndBalance()
  {
    int bestDistToRepairFac = INT32_MAX, bestDistToRepairDroid = INT32_MAX;
    int thisDistToRepair = 0;
    Structure* psHq = nullptr;
    Position bestDroidPos, bestFacPos;
    // static to save allocations
    static std::vector<Position> vFacilityPos;
    static std::vector<Structure*> vFacility;
    static std::vector<int> vFacilityCloseEnough;
    static std::vector<Position> vDroidPos;
    static std::vector<Droid*> vDroid;
    static std::vector<int> vDroidCloseEnough;
    // clear vectors from previous invocations
    vFacilityPos.clear();
    vFacility.clear();
    vFacilityCloseEnough.clear();
    vDroidCloseEnough.clear();
    vDroidPos.clear();
    vDroid.clear();

    using enum STRUCTURE_TYPE;
    for (auto& psStruct : apsStructLists[getPlayer()])
    {
      if (psStruct->getStats().type == HQ) {
        psHq = psStruct.get();
        continue;
      }
      if (psStruct->getStats().type == REPAIR_FACILITY &&
          psStruct->getState() == STRUCTURE_STATE::BUILT) {
        thisDistToRepair = droidSqDist(this, psStruct.get());
        if (thisDistToRepair <= 0) {
          continue; // cannot reach position
        }
        vFacilityPos.push_back(psStruct->getPosition());
        vFacility.push_back(psStruct.get());
        if (bestDistToRepairFac > thisDistToRepair) {
          bestDistToRepairFac = thisDistToRepair;
          bestFacPos = psStruct->getPosition();
        }
      }
    }
    // if we are repair droid ourselves, don't consider other repairs droids
    // because that causes havoc on front line: RT repairing themselves,
    // blocking everyone else. And everyone else moving toward RT, also toward front line.s
    // Ideally, we should just avoid retreating toward "danger", but dangerMap is only for multiplayer
    if (type != DROID_TYPE::REPAIRER && type != DROID_TYPE::CYBORG_REPAIR) {
      // one of these lists is empty when on mission
      auto psdroidList = !(apsDroidLists[getPlayer()].empty())
                           ? apsDroidLists[getPlayer()]
                           : mission.apsDroidLists[getPlayer()];
      
      for (auto& psCurr : psdroidList)
      {
        if (psCurr->type == DROID_TYPE::REPAIRER || psCurr->type == DROID_TYPE::CYBORG_REPAIR) {
          thisDistToRepair = droidSqDist(this, psCurr);
          if (thisDistToRepair <= 0) {
            continue; // unreachable
          }
          vDroidPos.push_back(psCurr->pos);
          vDroid.push_back(psCurr);
          if (bestDistToRepairDroid > thisDistToRepair) {
            bestDistToRepairDroid = thisDistToRepair;
            bestDroidPos = psCurr->pos;
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
    for (int i = 0; i < vFacilityPos.size(); i++)
    {
      Vector2i diff = (bestRepairPoint - vFacilityPos[i]).xy();
      if (dot(diff, diff) < MAGIC_SUITABLE_REPAIR_AREA) {
        vFacilityCloseEnough.push_back(i);
      }
    }
    for (int i = 0; i < vDroidPos.size(); i++)
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
    auto state = secondaryOrder;

    if (mode == ModeQueue) {
      state = secondaryOrderPending;
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
        if (order->type == ORDER_TYPE::HOLD) {
          return DSS_HALT_HOLD;
        }
        return (SECONDARY_STATE)(state & DSS_HALT_MASK);
        break;
      case RETURN_TO_LOCATION:
        return (SECONDARY_STATE)(state & DSS_RTL_MASK);
        break;
      case FIRE_DESIGNATOR:
        if (cmdDroidGetDesignator(getPlayer()) == this) {
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
    if (listSize >= asOrderList.size()) {
      // Make more room to store the order.
      asOrderList.resize(asOrderList.size() + 1);
    }

    asOrderList[listSize] = *order_;
    listSize += 1;

    using enum ORDER_TYPE;
    // if not doing anything - do it immediately
    if (listSize <= 1 &&
        (order->type == NONE ||
         order->type == GUARD ||
         order->type == PATROL ||
         order->type == CIRCLE ||
         order->type == HOLD)) {

      orderDroidList(this);
    }
  }

  void Droid::orderDroidAddPending(Order* order_)
  {
    asOrderList.push_back(*order_);

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
          order_->target->getDisplayData().imd_shape != nullptr) {
        position.y += order_->target->getDisplayData().imd_shape->max.y;
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
    for (unsigned i = 0; i < asOrderList.size(); ++i)
    {
      auto psTarget = asOrderList[i].target;
      if (psTarget == nullptr || !psTarget->died) {
        continue;
      }
      if ((int)i < listSize) {
        syncDebugObject(psTarget, '-');
        syncDebug("droid%d list erase dead droid%d", getId(), psTarget->getId());
      }
      orderDroidListEraseRange(this, i, i + 1);
      --i; // If this underflows, the ++i will overflow it back.
    }
  }

  /** This function actually tells the droid to perform the psOrder.
   * This function is called everytime to send a direct order to a droid.
   */
  void Droid::orderDroidBase(Order* psOrder)
  {
    unsigned iFactoryDistSq;
    Structure* psFactory;
    auto& psPropStats = propulsion;
    const Vector3i rPos(psOrder->pos, 0);
    syncDebugDroid(this, '-');
    syncDebug("%d ordered %s", getId(),
              getDroidOrderName(psOrder->type).c_str());
    objTrace(getId(), "base set order to %s (was %s)",
             getDroidOrderName(psOrder->type).c_str(),
             getDroidOrderName(order->type).c_str());

    using enum ORDER_TYPE;
    if (psOrder->type != TRANSPORT_IN // transporters special
        && psOrder->target == nullptr // location-type order
        && (validOrderForLoc(psOrder->type) || psOrder->type == BUILD)
        && !fpathCheck(getPosition(), rPos, psPropStats->propulsionType)) {
      if (!isHumanPlayer(getPlayer())) {
        debug(LOG_SCRIPT, "Invalid order %s given to player %d's %s for position (%d, %d) - ignoring",
              getDroidOrderName(psOrder->type).c_str(), getPlayer(),
              droidGetName(this), psOrder->pos.x, psOrder->pos.y);
      }
      objTrace(getId(), "Invalid order %s for position (%d, %d) - ignoring",
               getDroidOrderName(psOrder->type).c_str(),
               psOrder->pos.x, psOrder->pos.y);
      syncDebugDroid(this, '?');
      return;
    }

    // deal with a droid receiving a primary order
    if (!isTransporter(*this) && psOrder->type != NONE &&
        psOrder->type != STOP && psOrder->type != GUARD) {

      // reset secondary order
      const unsigned oldState = secondaryOrder;
      secondaryOrder &= ~(DSS_RTL_MASK | DSS_RECYCLE_MASK | DSS_PATROL_MASK);
      secondaryOrderPending &= ~(DSS_RTL_MASK | DSS_RECYCLE_MASK | DSS_PATROL_MASK);
      objTrace(getId(), "secondary order reset due to primary order set");

      if (oldState != secondaryOrder && getPlayer() == selectedPlayer) {
        intRefreshScreen();
      }
    }

    // if this is a command droid - all it's units do the same thing
    if ((type == DROID_TYPE::COMMAND) &&
        (group != nullptr) &&
        (group->isCommandGroup()) &&
        (psOrder->type != GUARD) && //(psOrder->psObj == NULL)) &&
        (psOrder->type != RETURN_TO_REPAIR) &&
        (psOrder->type != RECYCLE)) {

      if (psOrder->type == ATTACK) {
        // change to attack target so that the group members
        // guard order does not get canceled
        psOrder->type = ATTACK_TARGET;
        orderCmdGroupBase(group, psOrder);
        psOrder->type = ATTACK;
      }
      else {
        orderCmdGroupBase(group, psOrder);
      }

      // the commander doesn't have to pick up artifacts, one
      // of his units will do it for him (if there are any in his group).
      if ((psOrder->type == RECOVER) &&
          (!group->getMembers().empty())) {
        psOrder->type = NONE;
      }
    }

    // A selected campaign transporter shouldn't be given orders by the player.
    // Campaign transporter selection is required for it to be tracked by the camera, and
    // should be the only case when it does get selected.
    if (isTransporter(*this) &&
        !bMultiPlayer &&
        selected &&
        (psOrder->type != TRANSPORT_OUT &&
         psOrder->type != TRANSPORT_IN &&
         psOrder->type != TRANSPORT_RETURN)) {
      return;
    }

    switch (psOrder->type) {
      case NONE:
        // used when choose order cannot assign an order
        break;
      case STOP:
        // get the droid to stop doing whatever it is doing
        ::actionDroid(this, ACTION::NONE);
        order = std::make_unique<Order>(Order(NONE));
        break;
      case HOLD:
        // get the droid to stop doing whatever it is doing and temp hold
        ::actionDroid(this, ACTION::NONE);
        order = std::make_unique<Order>(*psOrder);
        break;
      case MOVE:
      case SCOUT:
        // can't move vtols to blocking tiles
        if (isVtol() &&
            fpathBlockingTile(map_coord(psOrder->pos),
                              propulsion->propulsionType)) {
          break;
        }
        //in multiPlayer, cannot move Transporter to blocking tile either
        if (game.type == LEVEL_TYPE::SKIRMISH
            && isTransporter(*this)
            && fpathBlockingTile(map_coord(psOrder->pos), propulsion->propulsionType)) {
          break;
        }
        // move a droid to a location
        order = std::make_unique<Order>(*psOrder);
        ::actionDroid(this, ACTION::MOVE, psOrder->pos.x, psOrder->pos.y);
        break;
      case PATROL:
        order = std::make_unique<Order>(*psOrder);
        order->pos2 = getPosition().xy();
        ::actionDroid(this, ACTION::MOVE, psOrder->pos.x, psOrder->pos.y);
        break;
      case RECOVER:
        order = std::make_unique<Order>(*psOrder);
        ::actionDroid(this, ACTION::MOVE,
                    psOrder->target->getPosition().x,
                    psOrder->target->getPosition().y);
        break;
      case TRANSPORT_OUT:
        // tell a (transporter) droid to leave home base for the offworld mission
        order = std::make_unique<Order>(*psOrder);
        ::actionDroid(this, ACTION::TRANSPORT_OUT, psOrder->pos.x, psOrder->pos.y);
        break;
      case TRANSPORT_RETURN:
        // tell a (transporter) droid to return after unloading
        order = std::make_unique<Order>(*psOrder);
        ::actionDroid(this, ACTION::TRANSPORT_OUT, psOrder->pos.x, psOrder->pos.y);
        break;
      case TRANSPORT_IN:
        // tell a (transporter) droid to fly onworld
        order = std::make_unique<Order>(*psOrder);
        ::actionDroid(this, ACTION::TRANSPORT_IN, psOrder->pos.x, psOrder->pos.y);
        break;
      case ATTACK:
      case ATTACK_TARGET:
        if (numWeapons(*this) == 0
            || asWeaps[0].nStat == 0
            || isTransporter(*this)) {
          break;
        }
        else if (order->type == GUARD && psOrder->type == ATTACK_TARGET) {
          // attacking something while guarding, don't change the order
          ::actionDroid(this, ACTION::ATTACK, psOrder->target);
        }
        else if (psOrder->target && !psOrder->target->died) {
          //cannot attack a Transporter with EW in multiPlayer
          // FIXME: Why not ?
          if (game.type == LEVEL_TYPE::SKIRMISH &&
              hasElectronicWeapon() &&
              dynamic_cast<Droid*>(psOrder->target) &&
              isTransporter(*dynamic_cast<Droid*>(psOrder->target))) {
            break;
          }
          order = std::make_unique<Order>(*psOrder);

          if (isVtol() || actionInRange(this, psOrder->target, 0) ||
              ((psOrder->type == ATTACK_TARGET ||
                psOrder->type == ATTACK) &&
                secondaryGetState(SECONDARY_ORDER::HALT_TYPE) == DSS_HALT_HOLD)) {
            // when DSS_HALT_HOLD, don't move to attack
            ::actionDroid(this, ACTION::ATTACK, psOrder->target);
          }
          else {
            ::actionDroid(this, ACTION::MOVE,
                        psOrder->target->getPosition().x,
                        psOrder->target->getPosition().y);
          }
        }
        break;
      case BUILD:
      case LINE_BUILD:
        // build a new structure or line of structures
        ASSERT_OR_RETURN(, isConstructionDroid(this), "%s cannot construct things!", objInfo(this));
        ASSERT_OR_RETURN(, psOrder->structure_stats != nullptr, "invalid structure stats pointer");
        order = std::make_unique<Order>(*psOrder);
        ASSERT_OR_RETURN(, !order->structure_stats ||
                         order->structure_stats->type != STRUCTURE_TYPE::DEMOLISH,
                         "Cannot build demolition");
        ::actionDroid(this, ACTION::BUILD, psOrder->pos.x, psOrder->pos.y);
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
        order = std::make_unique<Order>(
                  BUILD,
                  *getModuleStat(dynamic_cast<Structure*>(psOrder->target)),
                  psOrder->target->getPosition().xy(), 0);

        ASSERT_OR_RETURN(, order->structure_stats != nullptr, "should have found a module stats");
        ASSERT_OR_RETURN(, !order->structure_stats || order->structure_stats->type != STRUCTURE_TYPE::DEMOLISH,
                           "Cannot build demolition");
        ::actionDroid(this, ACTION::BUILD,
                    psOrder->target->getPosition().x,
                    psOrder->target->getPosition().y);
        objTrace(getId(), "Starting new upgrade of %s", psOrder->structure_stats
          ? getStatsName(psOrder->structure_stats)
          : "NULL");
        break;
      case HELP_BUILD:
        // help to build a structure that is starting to be built
        ASSERT_OR_RETURN(, isConstructionDroid(this), "Not a constructor droid");
        ASSERT_OR_RETURN(, psOrder->target != nullptr, "Help to build a NULL pointer?");
        if (action == ACTION::BUILD && psOrder->target == actionTarget[0]
            // skip LINEBUILD -> we still want to drop pending structure blueprints
            // this isn't a perfect solution, because ordering a LINEBUILD with negative energy, and then clicking
            // on first structure being built, will remove it, as we change order from DORDR_LINEBUILD to BUILD
            && (order->type != LINE_BUILD)) {
          // we are already building it, nothing to do
          objTrace(getId(), "Ignoring HELPBUILD because already building object %i", psOrder->target->getId());
          break;
        }
        order = std::make_unique<Order>(*psOrder);
        order->pos = psOrder->target->getPosition().xy();
        order->structure_stats = dynamic_cast<Structure*>(psOrder->target)->getStats();
        ASSERT_OR_RETURN(, !order->structure_stats || order->structure_stats->type != STRUCTURE_TYPE::DEMOLISH,
                           "Cannot build demolition");
        ::actionDroid(this, ACTION::BUILD, order->pos.x, order->pos.y);
        objTrace(getId(), "Helping construction of %s",
                 psOrder->structure_stats ? getStatsName(order->structure_stats) : "NULL");
        break;
      case DEMOLISH:
        if (!(type == DROID_TYPE::CONSTRUCT ||
              type == DROID_TYPE::CYBORG_CONSTRUCT)) {
          break;
        }
        order = std::make_unique<Order>(*psOrder);
        order->pos = psOrder->target->getPosition().xy();
        ::actionDroid(this, ACTION::DEMOLISH, psOrder->target);
        break;
      case REPAIR:
        if (!(type == DROID_TYPE::CONSTRUCT ||
              type == DROID_TYPE::CYBORG_CONSTRUCT)) {
          break;
        }
        order = std::make_unique<Order>(*psOrder);
        order->pos = psOrder->target->getPosition().xy();
        ::actionDroid(this, ACTION::REPAIR, psOrder->target);
        break;
      case DROID_REPAIR:
        if (!(type == DROID_TYPE::REPAIRER ||
              type == DROID_TYPE::CYBORG_REPAIR)) {
          break;
        }
        order = std::make_unique<Order>(*psOrder);
        ::actionDroid(this, ACTION::DROID_REPAIR, psOrder->target);
        break;
      case OBSERVE:
        // keep an object within sensor view
        order = std::make_unique<Order>(*psOrder);
        ::actionDroid(this, ACTION::OBSERVE, psOrder->target);
        break;
      case FIRE_SUPPORT:
        if (isTransporter(*this)) {
          debug(LOG_ERROR, "Sorry, transports cannot be assigned to commanders.");
          order = std::make_unique<Order>(NONE);
          break;
        }
        order = std::make_unique<Order>(*psOrder);
        // let the order update deal with vtol droids
        if (!isVtol()) {
          ::actionDroid(this, ACTION::FIRE_SUPPORT, psOrder->target);
        }

        if (getPlayer() == selectedPlayer) {
          orderPlayFireSupportAudio(psOrder->target);
        }
        break;
      case COMMANDER_SUPPORT:
        if (isTransporter(*this)) {
          debug(LOG_ERROR, "Sorry, transports cannot be assigned to commanders.");
          order = std::make_unique<Order>(NONE);
          break;
        }
          ASSERT_OR_RETURN(, psOrder->target != nullptr, "Can't command a NULL");
        if (cmdDroidAddDroid(dynamic_cast<Droid*>(psOrder->target), this) &&
            getPlayer() == selectedPlayer) {
          orderPlayFireSupportAudio(psOrder->target);
        }
        else if (getPlayer() == selectedPlayer) {
          audio_PlayBuildFailedOnce();
        }
        break;
      case RETURN_TO_BASE:
        for (auto& psStruct : apsStructLists[getPlayer()])
        {
          if (psStruct->getStats().type == STRUCTURE_TYPE::HQ) {
            auto pos = psStruct->getPosition().xy();

            order = std::make_unique<Order>(*psOrder);
            // find a place to land for vtols (and transporters in a multiplayer game)
            if (isVtol() || (game.type == LEVEL_TYPE::SKIRMISH &&
                             isTransporter(*this))) {
              actionVTOLLandingPos(this, &pos);
            }
            ::actionDroid(this, ACTION::MOVE, pos.x, pos.y);
            break;
          }
        }
        // no HQ so go to the landing zone
        if (order->type != RETURN_TO_BASE) {
          // see if the LZ has been set up
          auto iDX = getLandingX(getPlayer());
          auto iDY = getLandingY(getPlayer());

          if (iDX && iDY) {
            order = std::make_unique<Order>(*psOrder);
            ::actionDroid(this, ACTION::MOVE, iDX, iDY);
          }
          else {
            // haven't got an LZ set up so don't do anything
            ::actionDroid(this, NONE);
            order = std::make_unique<Order>(NONE);
          }
        }
        break;
      case RETURN_TO_REPAIR:
      case RTR_SPECIFIED:
      {
        if (isVtol()) {
          moveToRearm(this);
          break;
        }
        // if already has a target repair, don't override it: it might be different
        // and we don't want come back and forth between 2 repair points
        if (order->type == RETURN_TO_REPAIR && psOrder->target && !psOrder->target->died) {
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
          order = std::make_unique<Order>(psOrder->type, *rtrData.target, RTR_DATA_TYPE::REPAIR_FACILITY);
          order->pos = rtrData.target->getPosition().xy();
          /* If in multiPlayer, and the Transporter has been sent to be
            * repaired, need to find a suitable location to drop down. */
          if (game.type == LEVEL_TYPE::SKIRMISH && isTransporter(*this)) {
            Vector2i pos = order->pos;

            objTrace(getId(), "Repair transport");
            actionVTOLLandingPos(this, &pos);
            ::actionDroid(this, ACTION::MOVE, pos.x, pos.y);
          }
          else {
            objTrace(getId(), "Go to repair facility at (%d, %d) using (%d, %d)!", rtrData.target->getPosition().x,
                     rtrData.target->getPosition().y, order->pos.x, order->pos.y);
            ::actionDroid(this, ACTION::MOVE, rtrData.target, order->pos.x, order->pos.y);
          }
        }
          /* give repair order if repair droid found */
        else if (rtrData.type == RTR_DATA_TYPE::DROID && !isTransporter(*this)) {
          order = std::make_unique<Order>(psOrder->type,
                                          Vector2i(rtrData.target->getPosition().x,
                                                           rtrData.target->getPosition().y),
                                  RTR_DATA_TYPE::DROID);
          order->pos = rtrData.target->getPosition().xy();
          order->target = rtrData.target;
          objTrace(getId(), "Go to repair at (%d, %d) using (%d, %d), time %i!", rtrData.target->getPosition().x,
                   rtrData.target->getPosition().y, order->pos.x, order->pos.y, gameTime);
          ::actionDroid(this, ACTION::MOVE, order->pos.x, order->pos.y);
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
          order = std::make_unique<Order>(NONE);
          break;
        }
        // move the droid to the transporter location
        order = std::make_unique<Order>(*psOrder);
        order->pos = psOrder->target->getPosition().xy();
        ::actionDroid(this, ACTION::MOVE, psOrder->target->getPosition().x, psOrder->target->getPosition().y);
        break;
      }
      case DISEMBARK:
        //only valid in multiPlayer mode
        if (bMultiPlayer) {
          //this order can only be given to Transporter droids
          if (isTransporter(*this)) {
            order = std::make_unique<Order>(*psOrder);
            //move the Transporter to the requested location
            ::actionDroid(this, ACTION::MOVE, psOrder->pos.x, psOrder->pos.y);
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
        for (auto& psStruct : apsStructLists[getPlayer()])
        {
          using enum STRUCTURE_TYPE;
          // Look for nearest factory or repair facility
          if (psStruct->getStats().type == FACTORY ||
              psStruct->getStats().type == CYBORG_FACTORY ||
              psStruct->getStats().type == VTOL_FACTORY ||
              psStruct->getStats().type == REPAIR_FACILITY) {
            /* get droid->facility distance squared */
            auto iStructDistSq = droidSqDist(this, psStruct.get());

            /* Choose current structure if first facility found or nearer than previously chosen facility */
            if (psStruct->getState() == STRUCTURE_STATE::BUILT &&
                iStructDistSq > 0 && (psFactory == nullptr || iFactoryDistSq > iStructDistSq)) {
              psFactory = psStruct.get();
              iFactoryDistSq = iStructDistSq;
            }
          }
        }

        /* give recycle order if facility found */
        if (psFactory != nullptr) {
          /* move to front of structure */
          order = std::make_unique<Order>(psOrder->type, *psFactory);
          order->pos = psFactory->getPosition().xy();
          setDroidTarget(this, psFactory);
          ::actionDroid(this, ACTION::MOVE, psFactory, order->pos.x, order->pos.y);
        }
        break;
      case GUARD:
        order = std::make_unique<Order>(*psOrder);
        if (psOrder->target != nullptr) {
          order->pos = psOrder->target->getPosition().xy();
        }
        ::actionDroid(this, ACTION::NONE);
        break;
      case RESTORE:
        if (!hasElectronicWeapon()) {
          break;
        }
        if (!dynamic_cast<Structure*>(psOrder->target)) {
          ASSERT(false, "orderDroidBase: invalid object type for Restore order");
          break;
        }
        order = std::make_unique<Order>(*psOrder);
        order->pos = psOrder->target->getPosition().xy();
        ::actionDroid(this, ACTION::RESTORE, psOrder->target);
        break;
      case REARM:
        // didn't get executed before
        if (!vtolRearming(*this)) {
          order = std::make_unique<Order>(*psOrder);
          ::actionDroid(this, ACTION::MOVE_TO_REARM, psOrder->target);
          assignVTOLPad(this, dynamic_cast<Structure*>(psOrder->target));
        }
        break;
      case CIRCLE:
        if (!isVtol()) {
          break;
        }
        order = std::make_unique<Order>(*psOrder);
        ::actionDroid(this, ACTION::MOVE, psOrder->pos.x, psOrder->pos.y);
        break;
      default:
        ASSERT(false, "orderUnitBase: unknown order");
        break;
    }
    syncDebugDroid(this, '+');
  }

  bool Droid::isRepairDroid() const noexcept
  {
    return type == REPAIRER || type == CYBORG_REPAIR;
  }

  bool Droid::tryDoRepairlikeAction()
  {
    if (isRepairlikeAction(action)) {
      return true; // Already doing something.
    }

    switch (type) {
      case REPAIRER:
      case CYBORG_REPAIR:
        //repair droids default to repairing droids within a given range
        if (auto repairTarget = checkForRepairRange(psDroid)) {
          ::actionDroid(this, ACTION::DROID_REPAIR, repairTarget);
        }
        break;
      case CONSTRUCT:
      case CYBORG_CONSTRUCT:
        //construct droids default to repairing and helping structures within a given range
        auto damaged = checkForDamagedStruct(this);
        if (damaged.second == REPAIR) {
          actionDroid(this, damaged.second, damaged.first);
        }
        else if (damaged.second == BUILD) {
          order->structure_stats = damaged.first->stats;
          order->direction = damaged.first->rotation.direction;
          ::actionDroid(this, damaged.second, damaged.first->pos.x, damaged.first->pos.y);
        }
        break;
      default:
        return false;
    }
    return true;
  }

  //Builds an instance of a Droid - the x/y passed in are in world coords.
  std::unique_ptr<Droid> Droid::reallyBuildDroid(const DroidTemplate* pTemplate, Position pos, unsigned player, bool onMission, Rotation rot)
  {
    // Don't use this assertion in single player, since droids can finish building while on an away mission
    ASSERT(!bMultiPlayer || worldOnMap(pos.x, pos.y), "the build locations are not on the map");

    ASSERT_OR_RETURN(nullptr, player < MAX_PLAYERS, "Invalid player: %" PRIu32 "", player);

    auto psDroid = std::make_unique<Droid>(generateSynchronisedObjectId(), player);
    droidSetName(psDroid.get(), getStatsName(pTemplate));

    // Set the droids type
    psDroid->type = droidTemplateType(pTemplate); // Is set again later to the same thing, in droidSetBits.
    psDroid->setPosition(pos);
    psDroid->setRotation(rot);

    // don't worry if not on homebase cos not being drawn yet
    if (!onMission) {
      //set droid height
      psDroid->position.z = map_Height(psDroid->getPosition().x, psDroid->getPosition().y);
    }

    if (isTransporter(*psDroid) ||
        psDroid->type == DROID_TYPE::COMMAND) {
      auto psGrp = grpCreate();
      psGrp->add(psDroid.get());
    }

    // find the highest stored experience
    // unless game time is stopped, then we're hopefully loading a game and
    // don't want to use up recycled experience for the droids we just loaded
    if (!gameTimeIsStopped() &&
        (psDroid->getType() != DROID_TYPE::CONSTRUCT) &&
        (psDroid->getType() != DROID_TYPE::CYBORG_CONSTRUCT) &&
        (psDroid->getType() != DROID_TYPE::REPAIRER) &&
        (psDroid->getType() != DROID_TYPE::CYBORG_REPAIR) &&
        !isTransporter(*psDroid) &&
        !recycled_experience[psDroid->getPlayer()].empty()) {
      psDroid->experience = recycled_experience[psDroid->getPlayer()].top();
      recycled_experience[psDroid->getPlayer()].pop();
    }
    else {
      psDroid->experience = 0;
    }
    psDroid->kills = 0;

    droidSetBits(pTemplate, psDroid.get());

    //calculate the droids total weight
    psDroid->weight = calcDroidWeight(pTemplate);

    // Initialise the movement stuff
    psDroid->baseSpeed = calcDroidBaseSpeed(pTemplate, psDroid->weight, (UBYTE)player);

    initDroidMovement(psDroid.get());

    //allocate 'easy-access' data!
    psDroid->setHp(calcDroidBaseBody(psDroid.get())); // includes upgrades
    ASSERT(psDroid->getHp() > 0, "Invalid number of hitpoints");
    psDroid->originalHp = psDroid->getHp();

    /* Set droid's initial illumination */
    psDroid->display->imd_shape = BODY_IMD(psDroid, psDroid->getPlayer());

    //don't worry if not on homebase cos not being drawn yet
    if (!onMission) {
      /* People always stand upright */
      if (psDroid->type != DROID_TYPE::PERSON) {
        updateDroidOrientation(psDroid.get());
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
      psDroid->position.z += TRANSPORTER_HOVER_HEIGHT;

      /* reset halt secondary order from guard to hold */
      psDroid->secondarySetState(SECONDARY_ORDER::HALT_TYPE, DSS_HALT_HOLD);
    }

    if (player == selectedPlayer) {
      scoreUpdateVar(WD_UNITS_BUILT);
    }

    // Avoid droid appearing to jump or turn on spawn.
    psDroid->previousLocation.position = psDroid->getPosition();
    psDroid->previousLocation.rotation = psDroid->getRotation();

    debug(LOG_LIFE, "created droid for player %d, droid = %p, id=%d (%s): position: x(%d)y(%d)z(%d)", player,
          static_cast<void*>(psDroid.get()), (int)psDroid->getId(), 
          psDroid->name.c_str(), psDroid->getPosition().x, 
          psDroid->getPosition().y, psDroid->getPosition().z);

    return psDroid;
  }

  //initialises the droid movement model
  void Droid::initDroidMovement()
  {
    movement->path.clear();
    movement->pathIndex = 0;
  }

  // Give a droid from one player to another - used in Electronic Warfare and multiplayer.
  // Got to destroy the droid and build another since there are too many complications otherwise.
  // Returns the droid created.
 std::unique_ptr<Droid> Droid::giftSingleDroid(unsigned to, bool electronic)
  {
    ASSERT_OR_RETURN(nullptr, !isDead(this), "Cannot gift dead unit");
    ASSERT_OR_RETURN(std::make_unique<Droid>(this), getPlayer() != to, "Cannot gift to self");
    ASSERT_OR_RETURN(nullptr, to < MAX_PLAYERS, "Cannot gift to = %" PRIu32 "", to);

    // Check unit limits (multiplayer only)
    syncDebug("Limits: %u/%d %u/%d %u/%d", getNumDroids(to), getMaxDroids(to), getNumConstructorDroids(to),
              getMaxConstructors(to), getNumCommandDroids(to), getMaxCommanders(to));
    if (bMultiPlayer
        && ((int)getNumDroids(to) >= getMaxDroids(to)
            || ((type == DROID_TYPE::CYBORG_CONSTRUCT ||
                 type == DROID_TYPE::CONSTRUCT)
                && (int)getNumConstructorDroids(to) >= getMaxConstructors(to))
            || (type == DROID_TYPE::COMMAND && (int)getNumCommandDroids(to) >= getMaxCommanders(to)))) {
      if (to == selectedPlayer || getPlayer() == selectedPlayer) {
        CONPRINTF("%s", _("Unit transfer failed -- unit limits exceeded"));
      }
      return nullptr;
    }

    // electronic or campaign will destroy and recreate the droid.
    if (electronic || !bMultiPlayer) {
      DroidTemplate sTemplate;

      templateSetParts(this, &sTemplate); // create a template based on the droid
      sTemplate.name = WzString::fromUtf8(name); // copy the name across
      // update score
      if (getPlayer() == selectedPlayer &&
          to != selectedPlayer && !bMultiPlayer) {
        scoreUpdateVar(WD_UNITS_LOST);
      }
      // make the old droid vanish (but is not deleted until next tick)
      adjustDroidCount(this, -1);
      vanishDroid(this);
      // create a new droid
      auto psNewDroid = reallyBuildDroid(&sTemplate,
                                         Position(getPosition().x, getPosition().y, 0),
                                         to, false, getRotation());

      ASSERT_OR_RETURN(nullptr, psNewDroid.get(), "Unable to build unit");

      addDroid(psNewDroid, apsDroidLists);
      adjustDroidCount(psNewDroid, 1);

      psNewDroid->setHp(clip(
              (getHp() * psNewDroid->originalHp + originalHp / 2) / std::max(originalHp, 1u), 1u,
              psNewDroid->originalHp));
      psNewDroid->experience = experience;
      psNewDroid->kills = kills;

      if (!(psNewDroid->type == DROID_TYPE::PERSON ||
            isCyborg(psNewDroid.get()) ||
            isTransporter(*psNewDroid))) {
        updateDroidOrientation(psNewDroid.get());
      }

      triggerEventObjectTransfer(psNewDroid.get(), getPlayer());
      return psNewDroid;
    }

    auto oldPlayer = getPlayer();

    // reset the assigned state of units attached to a leader
    for (auto& psCurr : apsDroidLists[oldPlayer])
    {
      ::SimpleObject* psLeader;

      if (psCurr.hasCommander()) {
        psLeader = (SimpleObject*)psCurr.group->psCommander;
      }
      else {
        //psLeader can be either a droid or a structure
        psLeader = orderStateObj(&psCurr, ORDER_TYPE::FIRE_SUPPORT);
      }

      if (psLeader && psLeader->getId() == getId()) {
        psCurr.selected = false;
        orderDroid(&psCurr, ORDER_TYPE::STOP, ModeQueue);
      }
    }

    visRemoveVisibility((SimpleObject*)this);
    selected = false;

    adjustDroidCount(this, -1);
    scriptRemoveObject(this); //Remove droid from any script groups

    if (droidRemove(this, apsDroidLists)) {
      setPlayer(to);

      addDroid(this, apsDroidLists);
      adjustDroidCount(this, 1);

      // the new player may have different default sensor/ecm/repair components
      if ((asSensorStats + psD->asBits[COMP_SENSOR])->location == LOC::DEFAULT)
      {
        if (psD->asBits[COMP_SENSOR] != aDefaultSensor[getPlayer()])
        {
          psD->asBits[COMP_SENSOR] = aDefaultSensor[getPlayer()];
        }
      }
      if ((asECMStats + psD->asBits[COMP_ECM])->location == LOC::DEFAULT)
      {
        if (psD->asBits[COMP_ECM] != aDefaultECM[getPlayer()]){
          psD->asBits[COMP_ECM] = aDefaultECM[getPlayer()];
        }
      }
      if ((asRepairStats + psD->asBits[COMP_REPAIRUNIT])->location == LOC::DEFAULT)
      {
        if (psD->asBits[COMP_REPAIRUNIT] != aDefaultRepair[getPlayer()])
        {
          psD->asBits[COMP_REPAIRUNIT] = aDefaultRepair[getPlayer()];
        }
      }
    }
    else {
      // if we couldn't remove it, then get rid of it.
      return nullptr;
    }

    // Update visibility
    visTilesUpdate((SimpleObject*)this);

    // check through the players, and our allies, list of droids to see if any are targeting it
    for (unsigned int i = 0; i < MAX_PLAYERS; ++i)
    {
      if (!aiCheckAlliances(i, to)) {
        continue;
      }

      for (auto& psCurr : apsDroidLists[i])
      {
        if (psCurr.getOrder().target == this ||
            &psCurr.getTarget(0) == this) {
          orderDroid(&psCurr, ORDER_TYPE::STOP, ModeQueue);
          break;
        }
        for (auto iWeap = 0; iWeap < numWeapons(psCurr); ++iWeap)
        {
          if (&psCurr.getTarget(iWeap) == this) {
            orderDroid(&psCurr, ORDER_TYPE::STOP, ModeImmediate);
            break;
          }
        }
        // check through order list
        orderClearTargetFromDroidList(&psCurr, (SimpleObject*)this);
      }
    }

    for (unsigned int i = 0; i < MAX_PLAYERS; ++i)
    {
      if (!aiCheckAlliances(i, to)) {
        continue;
      }

      // check through the players list, and our allies, of structures to see if any are targeting it
      for (auto& psStruct : apsStructLists[i])
      {
        if (&psStruct->getTarget(0) == this) {
          setStructureTarget(psStruct.get(), nullptr, 0, TARGET_ORIGIN::UNKNOWN);
        }
      }
    }

    triggerEventObjectTransfer(this, oldPlayer);
    return std::make_unique<Droid>(this);
  }

  // Set the asBits in a Droid structure given its template.
  void Droid::droidSetBits(const DroidTemplate* pTemplate)
  {
    type = droidTemplateType(pTemplate);
    setHp(calcTemplateBody(pTemplate, getPlayer()));
    originalHp = getHp();
    expectedDamageDirect = 0; // Begin life optimistically.
    expectedDamageIndirect = 0; // Begin life optimistically.
    setTime(gameTime - deltaGameTime + 1); // Start at beginning of tick.
    previousLocation.time = getTime() - 1; // -1 for interpolation.

    //create the droids weapons
    for (int inc = 0; inc < MAX_WEAPONS; inc++)
    {
      actionTarget[inc] = nullptr;
      weapons[inc].timeLastFired = 0;
      weapons[inc].shotsFired = 0;
      // no weapon (could be a construction droid for example)
      // this is also used to check if a droid has a weapon, so zero it
      weapons[inc].ammo = 0;
      weapons[inc].rotation.direction = 0;
      weapons[inc].rotation.pitch = 0;
      weapons[inc].rotation.roll = 0;
      weapons[inc].previousRotation = psDroid->asWeaps[inc].rotation;
      weapons[inc].origin = TARGET_ORIGIN::UNKNOWN;
      if (inc < pTemplate->weaponCount) {
        weapons[inc].ammo = (asWeaponStats + asWeaps[inc].nStat)->upgraded_stats[getPlayer()].
                numRounds;
      }
      weapons[inc].ammoUsed = 0;
    }
    memcpy(asBits, pTemplate->asParts, sizeof(asBits));

    switch (propulsion->propulsionType)
      // getPropulsionStats(psDroid) only defined after psDroid->asBits[COMP_PROPULSION] is set.
    {
      case PROPULSION_TYPE::LIFT:
        blockedBits = AIR_BLOCKED;
        break;
      case PROPULSION_TYPE::HOVER:
        blockedBits = FEATURE_BLOCKED;
        break;
      case PROPULSION_TYPE::PROPELLOR:
        blockedBits = FEATURE_BLOCKED | LAND_BLOCKED;
        break;
      default:
        blockedBits = FEATURE_BLOCKED | WATER_BLOCKED;
        break;
    }
  }
}

//void cancelBuild(DROID *psDroid)
//{
//	if (psDroid->order.type == DORDER_NONE || psDroid->order.type == DORDER_PATROL || psDroid->order.type == DORDER_HOLD || psDroid->order.type == DORDER_SCOUT || psDroid->order.type == DORDER_GUARD)
//	{
//		objTrace(psDroid->id, "Droid build action cancelled");
//		psDroid->order.psObj = nullptr;
//		psDroid->action = DACTION_NONE;
//		setDroidActionTarget(psDroid, nullptr, 0);
//		return;  // Don't cancel orders.
//	}
//
//	if (orderDroidList(psDroid))
//	{
//		objTrace(psDroid->id, "Droid build order cancelled - changing to next order");
//	}
//	else
//	{
//		objTrace(psDroid->id, "Droid build order cancelled");
//		psDroid->action = DACTION_NONE;
//		psDroid->order = DroidOrder(DORDER_NONE);
//		setDroidActionTarget(psDroid, nullptr, 0);
//
//		// The droid has no more build orders, so halt in place rather than clumping around the build objective
//		moveStopDroid(psDroid);
//
//		triggerEventDroidIdle(psDroid);
//	}
//}

// initialise droid module
bool droidInit()
{
	for (auto & i : recycled_experience)
	{
		i = std::priority_queue<int>(); // clear it
	}
	psLastDroidHit = nullptr;
	return true;
}

int droidReloadBar(const Impl::Unit& psObj, const Weapon* psWeap, int weapon_slot)
{
	bool bSalvo;
	int firingStage, interval;

	if (numWeapons(psObj) == 0) {
		return -1;
	}
	auto& psStats = psWeap->getStats();

	/* Justifiable on. when greater than a one second reload or intra salvo time  */
	bSalvo = (psStats.upgraded[psObj.getPlayer()].numRounds > 1);
	if ((bSalvo && psStats.upgraded[psObj.getPlayer()].reloadTime > GAME_TICKS_PER_SEC)
		|| psStats.upgraded[psObj.getPlayer()].firePause > GAME_TICKS_PER_SEC
		|| (dynamic_cast<const Droid&>(psObj).isVtol())) {
		if (psObj.type == OBJ_DROID && isVtolDroid((const Droid*)psObj)) {
			//deal with VTOLs
			firingStage = getNumAttackRuns((const Droid*)psObj, weapon_slot) - ((const Droid*)psObj)->asWeaps[
				weapon_slot].ammoUsed;

			//compare with max value
			interval = getNumAttackRuns((const Droid*)psObj, weapon_slot);
		}
		else {
			firingStage = gameTime - psWeap->timeLastFired;
			interval = bSalvo ? weaponReloadTime(psStats, psObj.getPlayer()) : weaponFirePause(psStats, psObj.getPlayer());
		}
		if (firingStage < interval && interval > 0) {
			return PERCENT(firingStage, interval);
		}
		return 100;
	}
	return -1;
}



std::priority_queue<int> copy_experience_queue(int player)
{
	return recycled_experience[player];
}

void add_to_experience_queue(int player, int value)
{
	recycled_experience[player].push(value);
}

bool removeDroidBase(Droid* psDel)
{
	if (isDead(psDel)) {
		// droid has already been killed, quit
		syncDebug("droid already dead");
		return true;
	}

	syncDebugDroid(psDel, '#');

	// kill all the droids inside the transporter
	if (isTransporter(*psDel)) {
		if (psDel->group) {
			//free all droids associated with this Transporter
			for (auto psCurr = psDel->group->members; psCurr != nullptr && psCurr != psDel; psCurr = psNext)
			{
				/* add droid to droid list then vanish it - hope this works! - GJ */
				addDroid(psCurr, apsDroidLists);
				vanishDroid(psCurr);
			}
		}
	}

	// leave the current group if any
	if (psDel->group) {
		psDel->group->remove(psDel);
		psDel->group = nullptr;
	}

	/* Put Deliv. Pts back into world when a command droid dies */
	if (psDel->getType() == DROID_TYPE::COMMAND) {
		for (auto& psStruct : apsStructLists[psDel->getPlayer()])
		{
			// alexl's stab at a right answer.
			if (StructIsFactory(psStruct.get())
				&& dynamic_cast<Factory*>(psStruct.get())->psCommander == psDel) {
				assignFactoryCommandDroid(psStruct.get(), nullptr);
			}
		}
	}

	// Check to see if constructor droid currently trying to find a location to build
	if (psDel->getPlayer() == selectedPlayer &&
      psDel->selected && isConstructionDroid(psDel)) {
		// If currently trying to build, kill off the placement
		if (tryingToGetLocation()) {
			auto numSelectedConstructors = 0;
			for (auto& psDroid : apsDroidLists[psDel->getPlayer()]) {
				numSelectedConstructors += psDroid.selected && isConstructionDroid(&psDroid);
			}
			if (numSelectedConstructors <= 1) // If we were the last selected construction droid.
			{
				kill3DBuilding();
			}
		}
	}

	if (psDel->getPlayer() == selectedPlayer) {
		intRefreshScreen();
	}

	killDroid(psDel);
	return true;
}

static void removeDroidFX(Droid* psDel, unsigned impactTime)
{
	Vector3i pos;

	// only display anything if the droid is visible
	if (!psDel->visibleToSelectedPlayer()) {
		return;
	}

	if (psDel->animationEvent != ANIM_EVENT_DYING) {
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

	if (psDel->lastHitWeapon == WEAPON_SUBCLASS::LAS_SAT) // darken tile if lassat.
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
	psDel->died = impactTime;
	return true;
}

void vanishDroid(Droid* psDel)
{
	removeDroidBase(psDel);
}

/* Remove a droid from the List so doesn't update or get drawn etc
TAKE CARE with removeDroid() - usually want droidRemove since it deal with grid code*/
//returns false if the droid wasn't removed - because it died!
bool droidRemove(Droid* psDroid, Droid* pList[MAX_PLAYERS])
{
	if (isDead(psDroid)) {
		// droid has already been killed, quit
		return false;
	}

	// leave the current group if any - not if its a Transporter droid
	if (!isTransporter(*psDroid) && psDroid->group) {
		psDroid->group->remove(psDroid);
		psDroid->group = nullptr;
	}

	// reset the baseStruct
	setDroidBase(psDroid, nullptr);

	removeDroid(psDroid, pList);

	if (psDroid->getPlayer() == selectedPlayer) {
		intRefreshScreen();
	}
	return true;
}

void _syncDebugDroid(const char* function, Droid const* psDroid, char ch)
{
	int list[] =
	{
          ch,
          (int)psDroid->getId(),
          psDroid->getPlayer(),
          psDroid->getPosition().x, psDroid->getPosition().y, psDroid->getPosition().z,
          psDroid->getRotation().direction,
          psDroid->getRotation().pitch,
          psDroid->getRotation().roll,
          (int)psDroid->getOrder().type,
          psDroid->getOrder().pos.x,
          psDroid->getOrder().pos.y,
          psDroid->listSize,
          (int)psDroid->getAction(),
          (int)psDroid->secondaryOrder,
          (int)psDroid->getHp(),
          (int)psDroid->getMovementData().status,
          psDroid->getMovementData().speed,
          psDroid->getMovementData().moveDir,
          psDroid->getMovementData().pathIndex,
          (int)psDroid->getMovementData().path.size(),
          psDroid->getMovementData().src.x,
          psDroid->getMovementData().src.y,
          psDroid->getMovementData().target.x,
          psDroid->getMovementData().target.y,
          psDroid->getMovementData().destination.x,
          psDroid->getMovementData().destination.y,
          psDroid->getMovementData().bumpDir,
          (int)psDroid->getMovementData().bumpTime,
          (int)psDroid->getMovementData().lastBump,
          (int)psDroid->getMovementData().pauseTime,
          psDroid->getMovementData().bumpPos.x,
          psDroid->getMovementData().bumpPos.y,
          (int)psDroid->getMovementData().shuffleStart,
          (int)psDroid->experience,
	};
	_syncDebugIntList(
		function,
		"%c droid%d = p%d;pos(%d,%d,%d),rot(%d,%d,%d),order%d(%d,%d)^%d,action%d,secondaryOrder%X,body%d,sMove(status%d,speed%d,moveDir%d,path%d/%d,src(%d,%d),target(%d,%d),destination(%d,%d),bump(%d,%d,%d,%d,(%d,%d),%d)),exp%u",
		list, ARRAY_SIZE(list));
}

static bool droidNextToStruct(Droid* psDroid, Structure* psStruct)
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

static bool droidCheckBuildStillInProgress(void* psObj)
{
	if (psObj == nullptr) {
		return false;
	}
	auto psDroid = static_cast<Droid*>(psObj);

	return !psDroid->died &&
         psDroid->getAction() = ACTION::BUILD;
}

static bool droidBuildStartAudioCallback(void* psObj)
{
	auto psDroid = static_cast<Droid*>(psObj);

	if (psDroid != nullptr && psDroid->visibleToSelectedPlayer()) {
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

static void addConstructorEffect(Structure* psStruct)
{
	if ((ONEINTEN) && (psStruct->visibleToSelectedPlayer())) {
		/* This needs fixing - it's an arse effect! */
		const Vector2i size = psStruct->getSize() * TILE_UNITS / 4;
		Vector3i temp;
		temp.x = psStruct->getPosition().x + ((rand() % (2 * size.x)) - size.x);
		temp.y = map_TileHeight(map_coord(psStruct->getPosition().x),
                            map_coord(psStruct->getPosition().y)) + (psStruct->getDisplayData().imd_shape->max.y
			/ 6);
		temp.z = psStruct->getPosition().y + ((rand() % (2 * size.y)) - size.y);
		if (rand() % 2) {
			droidAddWeldSound(temp);
		}
	}
}

bool droidUpdateDemolishing(Droid* psDroid)
{
	ASSERT_OR_RETURN(false, psDroid->getAction() == ACTION::DEMOLISH, "unit is not demolishing");
	auto psStruct = dynamic_cast<Structure*>(psDroid->getOrder().target);
	ASSERT_OR_RETURN(false, psStruct, "target is not a structure");

	auto constructRate = 5 * constructorPoints(
          asConstructStats + psDroid->asBits[COMP_CONSTRUCT],
          psDroid->getPlayer());

	auto pointsToAdd = gameTimeAdjustedAverage(constructRate);

	structureDemolish(psStruct, psDroid, pointsToAdd);
	addConstructorEffect(psStruct);
	return true;
}

//void droidStartAction(DROID *psDroid)
//{
//	psDroid->actionStarted = gameTime;
//	psDroid->actionPoints  = 0;
//}

// Declared in weapondef.h.
int getRecoil(Weapon const& weapon)
{
		if (graphicsTime >= weapon.timeLastFired &&
        graphicsTime < weapon.timeLastFired + DEFAULT_RECOIL_TIME) {
			int recoilTime = graphicsTime - weapon.timeLastFired;
			auto recoilAmount = DEFAULT_RECOIL_TIME / 2 - abs(recoilTime - DEFAULT_RECOIL_TIME / 2);
			auto maxRecoil = weapon.getStats().recoilValue; // Max recoil is 1/10 of this value.
			return maxRecoil * recoilAmount / (DEFAULT_RECOIL_TIME / 2 * 10);
		  // recoil effect is over
	}
	return 0;
}

bool droidUpdateRepair(Droid* psDroid)
{
	ASSERT_OR_RETURN(false, psDroid->getAction() == ACTION::REPAIR, "unit does not have repair order");
  
	auto psStruct = &dynamic_cast<const Structure&>(psDroid->getTarget(0));
	auto iRepairRate = constructorPoints(asConstructStats + psDroid->asBits[COMP_CONSTRUCT], psDroid->getPlayer());

	/* add points to structure */
	structureRepair(psStruct, psDroid, iRepairRate);

	/* if not finished repair return true else complete repair and return false */
	if (psStruct->getHp() < structureBody(psStruct)) {
		return true;
	}
	else {
		objTrace(psDroid->getId(), "Repaired of %s all done with %u", objInfo(psStruct), iRepairRate);
		return false;
	}
}

/*Updates a Repair Droid working on a damaged droid*/
static bool droidUpdateDroidRepairBase(Droid* psRepairDroid, Droid* psDroidToRepair)
{
	auto iRepairRateNumerator = repairPoints(asRepairStats + psRepairDroid->asBits[COMP_REPAIRUNIT],
	                                        psRepairDroid->getPlayer());
	auto iRepairRateDenominator = 1;

	//if self repair then add repair points depending on the time delay for the stat
	if (psRepairDroid == psDroidToRepair) {
		iRepairRateNumerator *= GAME_TICKS_PER_SEC;
		iRepairRateDenominator *= (asRepairStats + psRepairDroid->asBits[COMP_REPAIRUNIT])->time;
	}

	auto iPointsToAdd = gameTimeAdjustedAverage(iRepairRateNumerator, iRepairRateDenominator);

	psDroidToRepair->setHp(clip<unsigned>(
          psDroidToRepair->getHp() + iPointsToAdd, 0,
          psDroidToRepair->originalHp));

	/* add plasma repair effect whilst being repaired */
	if ((ONEINFIVE) && (psDroidToRepair->visibleToSelectedPlayer())) {
		Vector3i iVecEffect = (psDroidToRepair->getPosition() + Vector3i(DROID_REPAIR_SPREAD, DROID_REPAIR_SPREAD, rand() % 8)).
			xzy();
		effectGiveAuxVar(90 + rand() % 20);
		addEffect(&iVecEffect, EFFECT_GROUP::EXPLOSION, EFFECT_TYPE::EXPLOSION_TYPE_LASER, false, nullptr, 0,
		          gameTime - deltaGameTime + 1 + rand() % deltaGameTime);
		droidAddWeldSound(iVecEffect);
	}
	/* if not finished repair return true else complete repair and return false */
	return psDroidToRepair->getHp() < psDroidToRepair->getOriginalHp();
}

static void droidUpdateDroidSelfRepair(Droid* psRepairDroid)
{
	droidUpdateDroidRepairBase(psRepairDroid, psRepairDroid);
}

bool isIdf(const Droid& droid)
{
  using enum DROID_TYPE;
  return (droid.getType() != WEAPON ||
          !isCyborg(&droid)) &&
          hasArtillery(droid);
}
//bool idfDroid(DROID *psDroid)
//{
//	//add Cyborgs
//	//if (psDroid->droidType != DROID_WEAPON)
//	if (!(psDroid->droidType == DROID_WEAPON || psDroid->droidType == DROID_CYBORG ||
//		  psDroid->droidType == DROID_CYBORG_SUPER))
//	{
//		return false;
//	}
//
//	return !proj_Direct(psDroid->asWeaps[0].nStat + asWeaponStats);
//}


///* Return the type of a droid */
//DROID_TYPE droidType(DROID *psDroid)
//{
//	return psDroid->droidType;
//}

/* Return the type of a droid from it's template */
DROID_TYPE droidTemplateType(const DroidTemplate* psTemplate)
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
	else if (psTemplate->asParts[COMP_BRAIN] != 0)
	{
		type = COMMAND;
	}
	else if ((asSensorStats + psTemplate->asParts[COMP_SENSOR])->location == LOC::TURRET)
	{
		type = SENSOR;
	}
	else if ((asECMStats + psTemplate->asParts[COMP_ECM])->location == LOC::TURRET)
	{
		type = ECM;
	}
	else if (psTemplate->asParts[COMP_CONSTRUCT] != 0)
	{
		type = CONSTRUCT;
	}
	else if ((asRepairStats + psTemplate->asParts[COMP_REPAIRUNIT])->location == LOC::TURRET)
	{
		type = REPAIRER;
	}
	else if (psTemplate->asWeaps[0] != 0)
	{
		type = WEAPON;
	}
	/* with more than weapon is still a WEAPON */
	else if (psTemplate->weaponCount > 1)
	{
		type = WEAPON;
	}

	return type;
}

template <typename F, typename G>
static unsigned calcSum(const uint8_t (&asParts)[DROID_MAXCOMP], int numWeaps, const unsigned (&asWeaps)[MAX_WEAPONS],
                        F func, G propulsionFunc)
{
	auto sum =
		func(asBrainStats[asParts[COMP_BRAIN]]) +
		func(asSensorStats[asParts[COMP_SENSOR]]) +
		func(asECMStats[asParts[COMP_ECM]]) +
		func(asRepairStats[asParts[COMP_REPAIRUNIT]]) +
		func(asConstructStats[asParts[COMP_CONSTRUCT]]) +
		propulsionFunc(asBodyStats[asParts[COMP_BODY]], asPropulsionStats[asParts[COMP_PROPULSION]]);
	for (int i = 0; i < numWeaps; ++i)
	{
		sum += func(asWeaponStats[asWeaps[i]]);
	}
	return sum;
}

#define ASSERT_PLAYER_OR_RETURN(retVal, player) \
	ASSERT_OR_RETURN(retVal, player >= 0 &&        \
                   player < MAX_PLAYERS,        \
                   "Invalid player: %" PRIu32 "", \
                   player);

template <typename F, typename G>
static unsigned calcUpgradeSum(const uint8_t (&asParts)[DROID_MAXCOMP], int numWeaps,
                               const unsigned (&asWeaps)[MAX_WEAPONS], unsigned player,
                               F func, G propulsionFunc)
{
	ASSERT_PLAYER_OR_RETURN(0, player);
	unsigned sum =
          func(asBrainStats[asParts[COMP_BRAIN]].upgraded[player]) +
          func(asSensorStats[asParts[COMP_SENSOR]].upgraded[player]) +
          func(asECMStats[asParts[COMP_ECM]].upgraded[player]) +
          func(asRepairStats[asParts[COMP_REPAIRUNIT]].upgraded[player]) +
          func(asConstructStats[asParts[COMP_CONSTRUCT]].upgraded[player]) +
          propulsionFunc(asBodyStats[asParts[COMP_BODY]].upgraded[player],
		               asPropulsionStats[asParts[COMP_PROPULSION]].upgraded[player]);
	for (int i = 0; i < numWeaps; ++i)
	{
		// asWeaps[i] > 0 check only needed for droids, not templates.
		if (asWeaps[i] > 0) {
			sum += func(asWeaponStats[asWeaps[i]].upgraded[player]);
		}
	}
	return sum;
}

struct FilterDroidWeaps
{
	FilterDroidWeaps(unsigned numWeaps, const Weapon (&asWeaps)[MAX_WEAPONS])
	{
		std::transform(asWeaps, asWeaps + numWeaps, this->asWeaps, [](const Weapon& weap)
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
	return calcSum(psTemplate->asParts, psTemplate->weaponCount, psTemplate->asWeaps, func, propulsionFunc);
}

template <typename F, typename G>
static unsigned calcSum(const Droid* psDroid, F func, G propulsionFunc)
{
	FilterDroidWeaps f = {psDroid->numWeaps, psDroid->asWeaps};
	return calcSum(psDroid->asBits, f.numWeaps, f.asWeaps,
                 func, propulsionFunc);
}

template <typename F, typename G>
static unsigned calcUpgradeSum(const DroidTemplate* psTemplate, int player, F func, G propulsionFunc)
{
	return calcUpgradeSum(psTemplate->asParts,
                        psTemplate->weaponCount,
                        psTemplate->asWeaps,
                        player, func, propulsionFunc);
}

template <typename F, typename G>
static unsigned calcUpgradeSum(const Droid* psDroid, int player, F func, G propulsionFunc)
{
	FilterDroidWeaps f {numWeapons(*psDroid), psDroid->getWeapons()};
	return calcUpgradeSum(psDroid->asBits, f.numWeaps, f.asWeaps, player, func, propulsionFunc);
}

/* Calculate the weight of a droid from it's template */
unsigned calcDroidWeight(const DroidTemplate* psTemplate)
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
static unsigned calcBody(T* obj, unsigned player)
{
	auto hitpoints = calcUpgradeSum(obj, player, [](ComponentStats::Upgradeable const& upgrade)
	                               {
		                               return upgrade.hit_points;
	                               }, [](BodyStats::Upgradeable const& bodyUpgrade,
                                       PropulsionStats::Upgradeable const& propUpgrade)
	                               {
		                               // propulsion hitpoints can be a percentage of the body's hitpoints
		                               return bodyUpgrade.hit_points * (100 + propUpgrade.hitpointPctOfBody) / 100 +
                                          propUpgrade.hit_points;
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
unsigned calcTemplateBody(const DroidTemplate* psTemplate, unsigned player)
{
	if (psTemplate == nullptr) {
		ASSERT(false, "null template");
		return 0;
	}
	return calcBody(psTemplate, player);
}

// Calculate the base body points of a droid with upgrades
static unsigned calcDroidBaseBody(Droid* psDroid)
{
	return calcBody(psDroid, psDroid->getPlayer());
}


/* Calculate the base speed of a droid from it's template */
unsigned calcDroidBaseSpeed(const DroidTemplate* psTemplate, unsigned weight, UBYTE player)
{
	auto speed = asPropulsionTypes[asPropulsionStats[psTemplate->asParts[COMP_PROPULSION]].propulsionType].
		powerRatioMult *
		bodyPower(&asBodyStats[psTemplate->asParts[COMP_BODY]], player) / MAX(1, weight);

	// reduce the speed of medium/heavy VTOLs
	if (asPropulsionStats[psTemplate->asParts[COMP_PROPULSION]].propulsionType == PROPULSION_TYPE::LIFT) {
		if (asBodyStats[psTemplate->asParts[COMP_BODY]].size == BODY_SIZE::HEAVY) {
			speed /= 4;
		}
		else if (asBodyStats[psTemplate->asParts[COMP_BODY]].size == BODY_SIZE::MEDIUM) {
			speed = speed * 3 / 4;
		}
	}

	// applies the engine output bonus if output > weight
	if (asBodyStats[psTemplate->asParts[COMP_BODY]].base.power > weight) {
		speed = speed * 3 / 2;
	}
	return speed;
}


/* Calculate the speed of a droid over a terrain */
unsigned calcDroidSpeed(unsigned baseSpeed, unsigned terrainType, unsigned propIndex, unsigned level)
{
	auto const& propulsion = asPropulsionStats[propIndex];

	// factor in terrain
	auto speed = baseSpeed * getSpeedFactor(terrainType,
                                          static_cast<unsigned>(propulsion.propulsionType)) / 100;

	// need to ensure doesn't go over the max speed possible
  // for this propulsion
	speed = std::min(speed, propulsion.maxSpeed);

	// factor in experience
	speed *= 100 + EXP_SPEED_BONUS * level;
	speed /= 100;

	return speed;
}

template <typename T>
static unsigned calcBuild(T* obj)
{
	return calcSum(obj, [](ComponentStats const& stat)
	               {
		               return stat.buildPoints;
	               }, [](BodyStats const& bodyStat, PropulsionStats const& propStat)
	               {
		               // Propulsion power points are a percentage of the body's build points.
		               return bodyStat.buildPoints * (100 + propStat.buildPoints) / 100;
	               });
}

/* Calculate the points required to build the template - used to calculate time*/
unsigned calcTemplateBuild(const DroidTemplate* psTemplate)
{
	return calcBuild(psTemplate);
}

unsigned calcDroidPoints(Droid* psDroid)
{
	return calcBuild(psDroid);
}

template <typename T>
static unsigned calcPower(const T* obj)
{
	ASSERT_NOT_NULLPTR_OR_RETURN(0, obj);
	return calcSum(obj, [](ComponentStats const& stat)
	               {
		               return stat.buildPower;
	               }, [](BodyStats const& bodyStat, PropulsionStats const& propStat)
	               {
		               // Propulsion power points are a percentage of the body's power points.
		               return bodyStat.buildPower * (100 + propStat.buildPower) / 100;
	               });
}

/* Calculate the power points required to build/maintain a template */
unsigned calcTemplatePower(const DroidTemplate* psTemplate)
{
	return calcPower(psTemplate);
}


/* Calculate the power points required to build/maintain a droid */
unsigned calcDroidPower(const Droid* psDroid)
{
	return calcPower(psDroid);
}


std::unique_ptr<Droid> buildDroid(DroidTemplate* pTemplate, unsigned x, unsigned y, unsigned player, bool onMission,
                  const INITIAL_DROID_ORDERS* initialOrders, Rotation rot)
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
void templateSetParts(const Droid* psDroid, DroidTemplate* psTemplate)
{
	psTemplate->weaponCount = 0;
	psTemplate->type = psDroid->type;
	for (int inc = 0; inc < MAX_WEAPONS; inc++)
	{
		//this should fix the NULL weapon stats for empty weaponslots
		psTemplate->asWeaps[inc] = 0;
		if (psDroid->asWeaps[inc].nStat > 0) {
			psTemplate->weaponCount += 1;
			psTemplate->asWeaps[inc] = psDroid->asWeaps[inc].nStat;
		}
	}
	memcpy(psTemplate->asParts, psDroid->asBits, sizeof(psDroid->asBits));
}

/* Make all the droids for a certain player a member of a specific group */
void assignDroidsToGroup(unsigned playerNumber, unsigned groupNumber, bool clearGroup)
{
	bool bAtLeastOne = false;
	FlagPosition* psFlagPos;

	ASSERT_OR_RETURN(, playerNumber < MAX_PLAYERS, "Invalid player: %" PRIu32 "", playerNumber);

	if (groupNumber < UBYTE_MAX) {
		/* Run through all the droids */
		for (auto& psDroid : apsDroidLists[playerNumber])
		{
			/* Clear out the old ones */
			if (clearGroup && psDroid.group == groupNumber) {
				psDroid.group = UBYTE_MAX;
			}

			/* Only assign the currently selected ones */
			if (psDroid.selected) {
				/* Set them to the right group - they can only be a member of one group */
				psDroid.group = (UBYTE)groupNumber;
				bAtLeastOne = true;
			}
		}
	}
	if (bAtLeastOne) {
		//clear the Deliv Point if one
		ASSERT_OR_RETURN(, selectedPlayer < MAX_PLAYERS, "Unsupported selectedPlayer: %" PRIu32 "", selectedPlayer);
		for (psFlagPos = apsFlagPosLists[selectedPlayer]; psFlagPos; psFlagPos = psFlagPos->psNext)
		{
			psFlagPos->selected = false;
		}
		groupConsoleInformOfCreation(groupNumber);
		secondarySetAverageGroupState(selectedPlayer, groupNumber);
	}
}


void removeDroidsFromGroup(unsigned playerNumber)
{
	Droid* psDroid;
	unsigned removedCount = 0;

	ASSERT_OR_RETURN(, playerNumber < MAX_PLAYERS, "Invalid player: %" PRIu32 "", playerNumber);

	for (psDroid = apsDroidLists[playerNumber]; psDroid != nullptr; psDroid = psDroid->psNext)
	{
		if (psDroid->selected) {
			psDroid->group = UBYTE_MAX;
			removedCount++;
		}
	}
	if (removedCount)
	{
		groupConsoleInformOfRemoval();
	}
}

bool activateGroupAndMove(unsigned playerNumber, unsigned groupNumber)
{
	Droid *psDroid, *psCentreDroid = nullptr;
	bool selected = false;
	FlagPosition* psFlagPos;

	ASSERT_OR_RETURN(false, playerNumber < MAX_PLAYERS, "Invalid player: %" PRIu32 "", playerNumber);

	if (groupNumber < UBYTE_MAX)
	{
		for (psDroid = apsDroidLists[playerNumber]; psDroid != nullptr; psDroid = psDroid->psNext)
		{
			/* Wipe out the ones in the wrong group */
			if (psDroid->selected && psDroid->group != groupNumber)
			{
				DeSelectDroid(psDroid);
			}
			/* Get the right ones */
			if (psDroid->group == groupNumber)
			{
				SelectDroid(psDroid);
				psCentreDroid = psDroid;
			}
		}

		/* There was at least one in the group */
		if (psCentreDroid)
		{
			//clear the Deliv Point if one
			ASSERT(selectedPlayer < MAX_PLAYERS, "Unsupported selectedPlayer: %" PRIu32 "", selectedPlayer);
			if (selectedPlayer < MAX_PLAYERS)
			{
				for (psFlagPos = apsFlagPosLists[selectedPlayer]; psFlagPos;
				     psFlagPos = psFlagPos->psNext)
				{
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

bool activateNoGroup(unsigned playerNumber, const SELECTIONTYPE selectionType,
                     const SELECTION_CLASS selectionClass, const bool bOnScreen)
{
	Droid* psDroid;
	bool selected = false;
	FlagPosition* psFlagPos;
	SELECTIONTYPE dselectionType = selectionType;
	SELECTION_CLASS dselectionClass = selectionClass;
	bool dbOnScreen = bOnScreen;

	ASSERT_OR_RETURN(false, playerNumber < MAX_PLAYERS, "Invalid player: %" PRIu32 "", playerNumber);

	selDroidSelection(selectedPlayer, dselectionClass, dselectionType, dbOnScreen);
	for (psDroid = apsDroidLists[playerNumber]; psDroid; psDroid = psDroid->psNext)
	{
		/* Wipe out the ones in the wrong group */
		if (psDroid->selected && psDroid->group != UBYTE_MAX) {
			DeSelectDroid(psDroid);
		}
	}
	if (selected) {
		//clear the Deliv Point if one
		ASSERT_OR_RETURN(false, selectedPlayer < MAX_PLAYERS, "Unsupported selectedPlayer: %" PRIu32 "",
		                 selectedPlayer);
		for (psFlagPos = apsFlagPosLists[selectedPlayer]; psFlagPos;
		     psFlagPos = psFlagPos->psNext)
		{
			psFlagPos->selected = false;
		}
	}
	return selected;
}

bool activateGroup(unsigned playerNumber, unsigned groupNumber)
{
	Droid* psDroid;
	bool selected = false;
	FlagPosition* psFlagPos;

	ASSERT_OR_RETURN(false, playerNumber < MAX_PLAYERS, "Invalid player: %" PRIu32 "", playerNumber);

	if (groupNumber < UBYTE_MAX) {
		for (psDroid = apsDroidLists[playerNumber]; psDroid; psDroid = psDroid->psNext)
		{
			/* Wipe out the ones in the wrong group */
			if (psDroid->selected && psDroid->group != groupNumber) {
				DeSelectDroid(psDroid);
			}
			/* Get the right ones */
			if (psDroid->group == groupNumber) {
				SelectDroid(psDroid);
				selected = true;
			}
		}
	}

	if (selected) {
		//clear the Deliv Point if one
		ASSERT_OR_RETURN(false, selectedPlayer < MAX_PLAYERS, "Unsupported selectedPlayer: %" PRIu32 "",
		                 selectedPlayer);
		for (psFlagPos = apsFlagPosLists[selectedPlayer]; psFlagPos;
		     psFlagPos = psFlagPos->psNext)
		{
			psFlagPos->selected = false;
		}
		groupConsoleInformOfSelection(groupNumber);
	}
	return selected;
}

void groupConsoleInformOfSelection(unsigned groupNumber)
{
	unsigned int num_selected = selNumSelected(selectedPlayer);

	CONPRINTF(ngettext("Group %u selected - %u Unit", "Group %u selected - %u Units", num_selected), groupNumber,
	          num_selected);
}

void groupConsoleInformOfCreation(unsigned groupNumber)
{
	if (!getWarCamStatus())
	{
		unsigned int num_selected = selNumSelected(selectedPlayer);

		CONPRINTF(ngettext("%u unit assigned to Group %u", "%u units assigned to Group %u", num_selected), num_selected,
		          groupNumber);
	}
}

void groupConsoleInformOfRemoval()
{
	if (!getWarCamStatus())
	{
		unsigned int num_selected = selNumSelected(selectedPlayer);

		CONPRINTF(ngettext("%u units removed from their Group", "%u units removed from their Group", num_selected),
		          num_selected);
	}
}

void groupConsoleInformOfCentering(unsigned groupNumber)
{
	unsigned int num_selected = selNumSelected(selectedPlayer);

	if (!getWarCamStatus())
	{
		CONPRINTF(ngettext("Centered on Group %u - %u Unit", "Centered on Group %u - %u Units", num_selected),
		          groupNumber, num_selected);
	}
	else
	{
		CONPRINTF(ngettext("Aligning with Group %u - %u Unit", "Aligning with Group %u - %u Units", num_selected),
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

unsigned get_effective_level(const Droid& droid)
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
//unsigned getDroidEffectiveLevel(const DROID *psDroid)
//{
//	unsigned level = getDroidLevel(psDroid);
//	unsigned cmdLevel = 0;
//
//	// get commander level
//	if (hasCommander(psDroid))
//	{
//		cmdLevel = cmdGetCommanderLevel(psDroid);
//
//		// Commanders boost units' effectiveness just by being assigned to it
//		level++;
//	}
//
//	return MAX(level, cmdLevel);
//}

std::string getDroidLevelName(const Droid* psDroid)
{
	const CommanderStats* psStats = getBrainStats(psDroid);
	return PE_("rank", psStats->rankNames[getDroidLevel(psDroid)].c_str());
}

unsigned count_droids_for_level(unsigned player, unsigned level)
{
  const auto& droids = apsDroidLists[player];
  return std::count_if(droids.begin(), droids.end(),
                       [level](const auto& droid)
  {
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

// Get the name of a droid from it's DROID structure.
//
const char* droidGetName(const Droid* psDroid)
{
	ASSERT_NOT_NULLPTR_OR_RETURN("", psDroid);
	return psDroid->name;
}

//
// Set the name of a droid in it's DROID structure.
//
// - only possible on the PC where you can adjust the names,
//
void droidSetName(Droid* psDroid, const char* pName)
{
	sstrcpy(psDroid->name, pName);
}

//// ////////////////////////////////////////////////////////////////////////////
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

//// ////////////////////////////////////////////////////////////////////////////
//// returns true when at most one droid on x,y square.
//static bool oneDroidMax(unsigned x, unsigned y)
//{
//	unsigned i;
//	bool bFound = false;
//	DROID* pD;
//	// check each droid list
//	for (i = 0; i < MAX_PLAYERS; i++)
//	{
//		for (pD = apsDroidLists[i]; pD; pD = pD->psNext)
//		{
//			if (map_coord(pD->getPosition().x) == x
//				&& map_coord(pD->getPosition().y) == y)
//			{
//				if (bFound)
//				{
//					return false;
//				}
//
//				bFound = true; //first droid on this square so continue
//			}
//		}
//	}
//	return true;
//}

bool valid_position_for_droid(int x, int y, PROPULSION_TYPE propulsion)
{
  if (x < TOO_NEAR_EDGE || x > mapWidth - TOO_NEAR_EDGE ||
      y < TOO_NEAR_EDGE || y > mapHeight - TOO_NEAR_EDGE)  {
    return false;
  }

  if (is_droid_blocked_by_tile(x, y, propulsion)) {
    return false;
  }
  return true;
}
//static bool sensiblePlace(SDWORD x, SDWORD y, PROPULSION_TYPE propulsion)
//{
//	// not too near the edges.
//	if ((x < TOO_NEAR_EDGE) || (x > (SDWORD)(mapWidth - TOO_NEAR_EDGE)))
//	{
//		return false;
//	}
//	if ((y < TOO_NEAR_EDGE) || (y > (SDWORD)(mapHeight - TOO_NEAR_EDGE)))
//	{
//		return false;
//	}
//
//	// not on a blocking tile.
//	if (fpathBlockingTile(x, y, propulsion))
//	{
//		return false;
//	}
//
//	return true;
//}

// ------------------------------------------------------------------------------------
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
	Structure* psStruct;
	Droid* psDroid;

	const auto tx = map_coord(rangeX);
	const auto ty = map_coord(rangeY);

	for (auto i = 0; i < MAX_PLAYERS; i++)
	{
		if ((alliances[player][i] == ALLIANCE_FORMED) || (i == player)) {
			continue;
		}

		//check structures
		for (psStruct = apsStructLists[i]; psStruct; psStruct = psStruct->psNext)
		{
			if (psStruct->visible[player] || psStruct->born == 2) // if can see it or started there
			{
				if (psStruct->getState() == STRUCTURE_STATE::BUILT) {
					auto structType = psStruct->getStats().type;

          using enum STRUCTURE_TYPE;
					switch (structType) //dangerous to get near these structures
					{
					case DEFENSE:
					case CYBORG_FACTORY:
					case FACTORY:
					case VTOL_FACTORY:
					case REARM_PAD:
						if (range < 0
							|| world_coord(
								static_cast<int32_t>(hypotf(tx - map_coord(psStruct->getPosition().x),
								                            ty - map_coord(psStruct->getPosition().y)))) < range) //enemy in range
						{
							return true;
						}
						break;
					}
				}
			}
		}

		//check droids
		for (psDroid = apsDroidLists[i]; psDroid; psDroid = psDroid->psNext)
		{
			if (psDroid->visible[player]) //can see this droid? {
				if (!objHasWeapon((SimpleObject*)psDroid)) {
					continue;
				}

				//if VTOLs are excluded, skip them
				if (!bVTOLs && ((asPropulsionStats[psDroid->asBits[COMP_PROPULSION]].propulsionType ==
					PROPULSION_TYPE::LIFT) || isTransporter(*psDroid))) {
					continue;
				}

				if (range < 0
					|| world_coord(
						static_cast<int32_t>(hypotf(tx - map_coord(psDroid->getPosition().x), ty - map_coord(psDroid->getPosition().y)))) <
					range) //enemy in range
				{
					return true;
				}
			}
		}
	}

	return false;
}

/// find a tile for which the passed function will return true without any threat in the specified range
bool pickATileGenThreat(unsigned* x, unsigned* y, UBYTE numIterations, SDWORD threatRange,
                        SDWORD player, bool (*function)(unsigned x, unsigned y))
{
	int i, j;
	int startX, endX, startY, endY;
	unsigned passes;
	Vector3i origin(world_coord(*x), world_coord(*y), 0);

	ASSERT_OR_RETURN(false, *x < mapWidth, "x coordinate is off-map for pickATileGen");
	ASSERT_OR_RETURN(false, *y < mapHeight, "y coordinate is off-map for pickATileGen");

	if (function(*x, *y) && ((threatRange <= 0) || (!ThreatInRange(player, threatRange, *x, *y, false))))
	//TODO: vtol check really not needed?
	{
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
		for (i = startX; i <= endX; i++)
		{
			for (j = startY; j <= endY; j++)
			{
				/* Test only perimeter as internal tested previous iteration */
				if (i == startX || i == endX || j == startY || j == endY)
				{
					Vector3i newPos(world_coord(i), world_coord(j), 0);

					/* Good enough? */
					if (function(i, j)
						&& fpathCheck(origin, newPos, PROPULSION_TYPE::WHEELED)
						&& ((threatRange <= 0) || (!ThreatInRange(player, threatRange, world_coord(i), world_coord(j),
						                                          false))))
					{
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
PICKTILE pickHalfATile(unsigned* x, unsigned* y, UBYTE numIterations)
{
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
	switch (psStruct->getStats().type) {
	case POWER_GEN:
		//check room for one more!
		max = std::max<int>(psStruct->capacity + next, lastOrderedModule + 1);
		if (max <= 1) {
			i = powerModuleStat;
			order = max;
		}
		break;
	case FACTORY:
	case VTOL_FACTORY:
		//check room for one more!
		max = std::max<int>(psStruct->capacity + next, lastOrderedModule + 1);
		if (max <= NUM_FACTORY_MODULES) {
			i = factoryModuleStat;
			order = max;
		}
		break;
	case RESEARCH:
		//check room for one more!
		max = std::max<int>(psStruct->capacity + next, lastOrderedModule + 1);
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
			(apStructTypeLists[psStruct->getPlayer()][i] == AVAILABLE))) {
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
	/* See if the name has a string resource associated with it by trying
	 * to get the string resource.
	 */
	return strresGetString(psStringRes, pName);
}

bool being_repaired(const Droid& droid)
{
  using enum ACTION;
  if (!droid.isDamaged()) {
    return false;
  }

  const auto& droids = apsDroidLists[droid.getPlayer()];
  return std::any_of(droids.begin(), droids.end(),
                     [&droid](const auto& other_droid)
  {
      return other_droid.isRepairDroid() &&
             other_droid.getAction() == DROID_REPAIR &&
              other_droid.getOrder().target->getId() == droid.getId();
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
  const auto& droids = apsDroidLists[player];
  return std::count_if(droids.begin(), droids.end(),
                       [](const auto& droid)
  {
      return droid.getType() == DROID_TYPE::COMMAND;
  });
}

bool isTransporter(const Droid& droid)
{
  using enum DROID_TYPE;
  return droid.getType() == TRANSPORTER ||
         droid.getType() == SUPER_TRANSPORTER;
}

bool vtolEmpty(const Droid& droid)
{
  assert(droid.isVtol());
  if (droid.getType() != DROID_TYPE::WEAPON) {
    return false;
  }

  return std::all_of(droid.getWeapons().begin(), droid.getWeapons().end(),
                     [&droid](const auto& weapon)
  {
      return weapon.isVtolWeapon() &&
              weapon.isEmptyVtolWeapon(droid.getPlayer());
  });
}

bool vtolFull(const Droid& droid)
{
  assert(droid.isVtol());
  if (droid.getType() != DROID_TYPE::WEAPON) {
    return false;
  }

  return std::all_of(droid.getWeapons().begin(), droid.getWeapons().end(),
                     [](const auto& weapon)
  {
      return weapon.isVtolWeapon() && weapon.hasFullAmmo();
  });
}

bool vtolReadyToRearm(const Droid& droid, const RearmPad& rearmPad)
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

bool vtolRearming(const Droid& droid)
{
  if (!droid.isVtol() ||
      droid.getType() != DROID_TYPE::WEAPON) {
    return false;
  }

  auto action = droid.getAction();
  using enum ACTION;
  if (action == MOVE_TO_REARM ||
      action == WAIT_FOR_REARM ||
      action == MOVE_TO_REARM_POINT ||
      action == WAIT_DURING_REARM) {
    return true;
  }
  return false;
}


bool all_VTOLs_rearmed(const Droid& droid)
{
  if (!droid.isVtol())  {
    return true;
  }

  const auto& droids = apsDroidLists[droid.getPlayer()];
  return std::none_of(droids.begin(), droids.end(),
                      [&droid](const auto& other_droid)
  {
      return vtolRearming(other_droid) &&
             other_droid.getOrder().type ==
               droid.getOrder().type &&
             other_droid.getOrder().target ==
              droid.getOrder().target;
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
unsigned getNumAttackRuns(const Droid* psDroid, int weapon_slot)
{
	ASSERT_OR_RETURN(0, psDroid->isVtol(), "not a VTOL Droid");
	// if weapon is a salvo weapon, then number of shots that can be fired = vtolAttackRuns * numRounds
	if (psDroid->getWeapons()[weapon_slot].getStats().upgraded[psDroid->getPlayer()].reloadTime) {
		return psDroid->getWeapons()[weapon_slot].getStats().upgraded[psDroid->getPlayer()].numRounds
			* psDroid->getWeapons()[weapon_slot].getStats().vtolAttackRuns;
	}
	return psDroid->getWeapons()[weapon_slot].getStats().vtolAttackRuns;
}

bool vtolHappy(const Droid& droid)
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
  if (!droid.isVtol() ||
      numWeapons(droid) == 0) {
    return;
  }

  auto& weapon = droid.getWeapons()[weapon_slot];
  if (weapon.getStats().vtolAttackRuns == 0) {
    return;
  }
  droid.use_ammo(weapon_slot);
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

/*compares the droid sensor type with the droid weapon type to see if the
FIRE_SUPPORT order can be assigned*/
bool droidSensorDroidWeapon(const SimpleObject* psObj, const Droid* psDroid)
{
	const SensorStats* psStats = nullptr;

	if (!psObj || !psDroid) {
		return false;
	}

	//first check if the object is a droid or a structure
	if (!dynamic_cast<const Droid*>(psObj) &&
		  !dynamic_cast<const Structure*>(psObj)) {
		return false;
	}
	//check same player
	if (psObj->getPlayer() != psDroid->getPlayer()) {
		return false;
	}
	//check obj is a sensor droid/structure
	if (auto psDroid = dynamic_cast<const Droid*>(psObj)) {
    if (psDroid->getType() != DROID_TYPE::SENSOR &&
        psDroid->getType() != DROID_TYPE::COMMAND) {
      return false;
    }
    compIndex = psDroid->sensor;
    ASSERT_OR_RETURN(false, compIndex < numSensorStats, "Invalid range referenced for numSensorStats, %d > %d",
                     compIndex, numSensorStats);
    psStats = asSensorStats + compIndex;
  }
  else {
    auto psStruct = dynamic_cast<const Structure*>(psObj);
		psStats = psStruct->sensor_stats;
		if ((psStats == nullptr) ||
			(psStats->location != LOC::TURRET)) {
			return false;
		}
	}

	//check droid is a weapon droid - or Cyborg!!
	if (!(psDroid->getType() == DROID_TYPE::WEAPON ||
        psDroid->getType() == DROID_TYPE::CYBORG ||
        psDroid->getType() == DROID_TYPE::CYBORG_SUPER)) {
		return false;
	}

	//finally check the right droid/sensor combination
	// check vtol droid with commander
	if ((psDroid->isVtol() || !proj_Direct(asWeaponStats + psDroid->asWeaps[0].nStat)) &&
		psObj->type == OBJ_DROID && (dynamic_cast<const Droid*>(psObj)->getType() == DROID_TYPE::COMMAND) {
		return true;
	}

	//check vtol droid with vtol sensor
	if (psDroid->isVtol() && psDroid->asWeaps[0].nStat > 0) {
		if (psStats->type == VTOL_INTERCEPT_SENSOR || psStats->type == VTOL_CB_SENSOR || psStats->type == SUPER_SENSOR
			/*|| psStats->type == RADAR_DETECTOR_SENSOR*/) {
			return true;
		}
		return false;
	}

	// Check indirect weapon droid with standard/CB/radar detector sensor
	if (!proj_Direct(asWeaponStats + psDroid->asWeaps[0].nStat)) {
		if (psStats->type == STANDARD_SENSOR || psStats->type == INDIRECT_CB_SENSOR || psStats->type == SUPER_SENSOR
			/*|| psStats->type == RADAR_DETECTOR_SENSOR*/)
		{
			return true;
		}
		return false;
	}
	return false;
}


//// return whether a droid has a CB sensor on it
//bool cbSensorDroid(const DROID* psDroid)
//{
//	if (psDroid->droidType != DROID_SENSOR)
//	{
//		return false;
//	}
//	if (asSensorStats[psDroid->asBits[COMP_SENSOR]].type == VTOL_CB_SENSOR
//		|| asSensorStats[psDroid->asBits[COMP_SENSOR]].type == INDIRECT_CB_SENSOR)
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
//	if (asSensorStats[psDroid->asBits[COMP_SENSOR]].type == VTOL_INTERCEPT_SENSOR
//		|| asSensorStats[psDroid->asBits[COMP_SENSOR]].type == STANDARD_SENSOR
//		|| asSensorStats[psDroid->asBits[COMP_SENSOR]].type == SUPER_SENSOR)
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
//	const BODY_STATS* psStats = asBodyStats + psDroid->asBits[COMP_BODY];
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
bool checkValidWeaponForProp(DroidTemplate* psTemplate)
{
	PropulsionStats* psPropStats;

	//check propulsion stat for vtol
	psPropStats = asPropulsionStats + psTemplate->asParts[COMP_PROPULSION];

	ASSERT_OR_RETURN(false, psPropStats != nullptr, "invalid propulsion stats pointer");

	// if there are no weapons, then don't even bother continuing
	if (psTemplate->weaponCount == 0) {
		return false;
	}

	if (asPropulsionTypes[psPropStats->propulsionType].travel == TRAVEL_MEDIUM::AIR) {
		//check weapon stat for indirect
		if (!proj_Direct(asWeaponStats + psTemplate->asWeaps[0])
			|| !asWeaponStats[psTemplate->asWeaps[0]].vtolAttackRuns) {
			return false;
		}
	}
	else {
		// VTOL weapons do not go on non-AIR units
		if (asWeaponStats[psTemplate->asWeaps[0]].vtolAttackRuns) {
			return false;
		}
	}

	//also checks that there is no other system component
	if (psTemplate->asParts[COMP_BRAIN] != 0
		&& asWeaponStats[psTemplate->asWeaps[0]].weaponSubClass != WEAPON_SUBCLASS::COMMAND) {
		assert(false);
		return false;
	}
	return true;
}

//// Check if a droid can be selected.
//bool isSelectable(DROID const* psDroid)
//{
//	if (psDroid->flags.test(OBJECT_FLAG_UNSELECTABLE))
//	{
//		return false;
//	}
//
//	// we shouldn't ever control the transporter in SP games
//	if (isTransporter(psDroid) && !bMultiPlayer)
//	{
//		return false;
//	}
//
//	return true;
//}

// Select a droid and do any necessary housekeeping.
//
void SelectDroid(Droid* psDroid)
{
	if (!psDroid->isSelectable()) {
		return;
	}
	psDroid->selected = true;
	intRefreshScreen();
	triggerEventSelected();
	jsDebugSelected(psDroid);
}

// De-select a droid and do any necessary housekeeping.
//
void DeSelectDroid(Droid* psDroid)
{
	psDroid->selected = false;
	intRefreshScreen();
	triggerEventSelected();
}

/**
 * Callback function for stopped audio tracks
 * Sets the droid's current track id to NO_SOUND
 *
 * @return true on success, false on failure
 */
bool droidAudioTrackStopped(void* psObj)
{
	auto psDroid = static_cast<Droid*>(psObj);

	if (psDroid == nullptr) {
		debug(LOG_ERROR, "droid pointer invalid");
		return false;
	}
	if (!dynamic_cast<Droid*>(psDroid)|| psDroid->died) {
		return false;
	}

	psDroid->iAudioID = NO_SOUND;
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
///*returns true if droid type is one of the Cyborg types*/
//bool cyborgDroid(const DROID* psDroid)
//{
//	return (psDroid->droidType == DROID_CYBORG
//		|| psDroid->droidType == DROID_CYBORG_CONSTRUCT
//		|| psDroid->droidType == DROID_CYBORG_REPAIR
//		|| psDroid->droidType == DROID_CYBORG_SUPER);
//}

bool isBuilder(const Droid& droid)
{
  using enum DROID_TYPE;
  return droid.getType() == CONSTRUCT ||
         droid.getType() == CYBORG_CONSTRUCT;
}
//bool isConstructionDroid(DROID const* psDroid)
//{
//	return psDroid->droidType == DROID_CONSTRUCT || psDroid->droidType == DROID_CYBORG_CONSTRUCT;
//}
//
//bool isConstructionDroid(SimpleObject const* psObject)
//{
//	return isDroid(psObject) && isConstructionDroid(castDroid(psObject));
//}

bool droidOnMap(const Droid* psDroid)
{
	if (psDroid->died == NOT_CURRENT_LIST ||
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
void droidSetPosition(Droid* psDroid, int x, int y)
{
  psDroid->setPosition({x, y, map_Height(psDroid->getPosition().x,
                                          psDroid->getPosition().y)});
	initDroidMovement(psDroid);
	visTilesUpdate((SimpleObject*)psDroid);
}

int droidSqDist(Droid* psDroid, SimpleObject* psObj)
{
	auto& psPropStats = psDroid->getPropulsion();

	if (!fpathCheck(psDroid->getPosition(),
                  psObj->getPosition(),
                  psPropStats->propulsionType)) {
		return -1;
	}
	return objectPositionSquareDiff(psDroid->getPosition(), psObj->getPosition());
}

unsigned calculate_max_range(const Droid& droid)
{
  if (droid.getType() == DROID_TYPE::SENSOR)
    return droid.calculateSensorRange();
  else if (numWeapons(droid) == 0)
    return 0;
  else
    return getMaxWeaponRange(droid);
}

bool transporter_is_flying(const Droid& transporter)
{
  assert(isTransporter(transporter));
  auto& order = transporter.getOrder();

  if (bMultiPlayer) {
    return order.type == ORDER_TYPE::MOVE ||
           order.type == ORDER_TYPE::DISEMBARK ||
           (order.type == ORDER_TYPE::NONE && transporter.getVerticalSpeed() != 0);
  }

  return order.type == ORDER_TYPE::TRANSPORT_OUT ||
         order.type == ORDER_TYPE::TRANSPORT_IN ||
         order.type == ORDER_TYPE::TRANSPORT_RETURN;
}

bool still_building(const Droid& droid)
{
  return droid.isAlive() &&
         droid.getAction() == ACTION::BUILD;
}

bool can_assign_fire_support(const Droid& droid, const Structure& structure)
{
  if (numWeapons(droid) == 0 ||
      !structure.hasSensor()) {
    return false;
  }

  if (droid.isVtol()) {
    return structure.hasVtolInterceptSensor() ||
           structure.hasVtolCbSensor();
  }
  else if (hasArtillery(droid)) {
    return structure.hasStandardSensor() || structure.hasCbSensor();
  }
  return false;
}

bool vtol_can_land_here(int x, int y)
{
  if (x < 0 || x >= mapWidth ||
      y < 0 || y >= mapHeight) {
    return false;
  }

  const auto tile = mapTile(x, y);
  if (tile->tileInfoBits & AUXBITS_BLOCKING ||
      TileIsOccupied(tile) ||
      terrainType(tile) == TER_CLIFFFACE ||
      terrainType(tile) == TER_WATER) {
    return false;
  }
  return true;
}

Vector2i choose_landing_position(const Droid& vtol, Vector2i position)
{
  Vector2i start_pos = {map_coord(position.x),
                        map_coord(position.y)};

  set_blocking_flags(vtol);

  auto landing_tile = spiral_search(start_pos, VTOL_LANDING_RADIUS);
  landing_tile.x = world_coord(landing_tile.x) + TILE_UNITS / 2;
  landing_tile.y = world_coord(landing_tile.y) + TILE_UNITS / 2;

  clear_blocking_flags(vtol);
  return landing_tile;
}

Droid* find_nearest_droid(unsigned x, unsigned y, bool selected)
{
  auto& droids = apsDroidLists[selectedPlayer];
  Droid* nearest_droid = nullptr;
  auto shortest_distance = UINT32_MAX;
  std::for_each(droids.begin(), droids.end(), [&](auto& droid)
  {
      if (droid.isVtol()) {
        return;
      }
      if (selected && !droid.isSelected()) {
        return;
      }

      const auto distance = iHypot(droid.getPosition().x - x,
                                   droid.getPosition().y - y);
      if (distance < shortest_distance) {
        shortest_distance = distance;
        nearest_droid = &droid;
      }
  });
  return nearest_droid;
}

Vector2i spiral_search(Vector2i start_pos, int max_radius)
{
  // test center tile
  if (vtol_can_land_here(start_pos.x, start_pos.y)) {
    return start_pos;
  }

  // test for each radius, from 1 to max_radius (inclusive)
  for (auto radius = 1; radius <= max_radius; ++radius)
  {
    // choose tiles that are between radius and radius+1 away from center
    // distances are squared
    const auto min_distance = radius * radius;
    const auto max_distance = min_distance + 2 * radius;

    // X offset from startX
    // dx starts with 1, to visiting tiles on same row or col as start twice
    for (auto dx = 1; dx <= max_radius; dx++)
    {
      // Y offset from startY
      for (auto dy = 0; dy <= max_radius; dy++)
      {
        // Current distance, squared
        const auto distance = dx * dx + dy * dy;

        // Ignore tiles outside the current circle
        if (distance < min_distance || distance > max_distance)
        {
          continue;
        }

        // call search function for each of the 4 quadrants of the circle
        if (vtol_can_land_here(start_pos.x + dx, start_pos.y + dy))
        {
          return {start_pos.x + dx, start_pos.y + dy};
        }
        if (vtol_can_land_here(start_pos.x - dx, start_pos.y - dy))
        {
          return {start_pos.x - dx, start_pos.y - dy};
        }
        if (vtol_can_land_here(start_pos.x + dy, start_pos.y - dx))
        {
          return {start_pos.x + dy, start_pos.y - dx};
        }
        if (vtol_can_land_here(start_pos.x - dy, start_pos.y + dx))
        {
          return {start_pos.x - dy, start_pos.y + dx};
        }
      }
    }
  }
}

void set_blocking_flags(const Droid& droid)
{
  const auto &droids = apsDroidLists[droid.getPlayer()];
  std::for_each(droids.begin(), droids.end(),
                [&droid](const auto &other_droid)
  {
      Vector2i tile{0, 0};
      if (other_droid.isStationary()) {
        tile = map_coord(other_droid.getPosition().xy());
      } else {
        tile = map_coord(other_droid.getDestination());
      }

      if (&droid == &other_droid) {
        return;
      } else if (tileOnMap(tile)) {
        mapTile(tile)->tileInfoBits |= AUXBITS_BLOCKING;
      }
  });
}

void clear_blocking_flags(const Droid& droid)
{
  const auto &droids = apsDroidLists[droid.getPlayer()];
  std::for_each(droids.begin(), droids.end(),
                [&droid](const auto &other_droid)
  {
      Vector2i tile{0, 0};
      if (other_droid.isStationary()) {
        tile = map_coord(other_droid.getPosition().xy());
      } else {
        tile = map_coord(other_droid.getDestination());
      }

      if (tileOnMap(tile)) {
        mapTile(tile)->tileInfoBits &= ~AUXBITS_BLOCKING;
      }
  });
}

bool tile_occupied_by_droid(unsigned x, unsigned y)
{
  for (const auto& player_droids : apsDroidLists)
  {
    if (std::any_of(player_droids.begin(), player_droids.end(),
                    [x, y](const auto& droid)  {
        return map_coord(droid.getPosition().x) == x &&
               map_coord(droid.getPosition().y == y);
    }))  {
      return true;
    }
  }
  return false;
}
