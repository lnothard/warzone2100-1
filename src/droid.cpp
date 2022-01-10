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
      display->frame_number = 0; // it was never drawn before

      for (unsigned vPlayer = 0; vPlayer < MAX_PLAYERS; ++vPlayer)
      {
        visible[vPlayer] = hasSharedVision(vPlayer, player) ? UINT8_MAX : 0;
      }

      memset(seenThisTick, 0, sizeof(seenThisTick));
      periodicalDamageStart = 0;
      periodicalDamage = 0;
      display->screen_x = OFF_SCREEN;
      display->screen_y = OFF_SCREEN;
      display->screen_r = 0;
      display->imd = nullptr;
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

      Droid* psDroid = this;
      Droid *psCurr, *pNextGroupDroid = nullptr;

      if (isTransporter(*psDroid))
      {
        if (psDroid->group)
        {
          //free all droids associated with this Transporter
          for (psCurr = psDroid->group->members; psCurr != nullptr && psCurr != psDroid; psCurr = pNextGroupDroid)
          {
            delete psCurr;
          }
        }
      }

      fpathRemoveDroidData(psDroid->getId());

      // leave the current group if any
      if (psDroid->group) {
        psDroid->group->remove(psDroid);
      }
    }

    ACTION Droid::getAction() const noexcept {
      return action;
    }

    const Order &Droid::getOrder() const {
      return *order;
    }

    bool Droid::isProbablyDoomed(bool is_direct_damage) const {
      auto is_doomed = [this](unsigned damage) {
          const auto hit_points = getHp();
          return damage > hit_points && damage - hit_points > hit_points / 5;
      };

      if (is_direct_damage) {
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
      } else {
        action = ACTION::NONE;
        order->type = NONE;

        // stop moving
        if (isFlying()) {
          movement->status = MOVE_STATUS::HOVER;
        } else {
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
      for (auto droid: group->members) {
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

    const auto &weapon_stats = getWeapons()[0].get_stats();
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
    if (::hasElectronicWeapon(*this))  {
      return true;
    }
    if (type != COMMAND)  {
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
    if (!propulsion)
      return false;

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
    switch (weaponClass)
    {
      case WEAPON_CLASS::KINETIC:
        return body->upgraded[getPlayer()].armour;
      case WEAPON_CLASS::HEAT:
        return body->upgraded[getPlayer()].thermal;
    }
  }

  void Droid::assignVtolToRearmPad(RearmPad* rearmPad)
  {
    associatedStructure = rearmPad;
  }

  bool Droid::isAttacking() const noexcept
  {
    if (!(type == WEAPON || type == CYBORG || type == CYBORG_SUPER)) {
      return false;
    }
    if (action == ATTACK || action == MOVE_TO_ATTACK ||
        action == ROTATE_TO_ATTACK || action == VTOL_ATTACK ||
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
      bDirect = proj_Direct(&getWeapons()[i].get_stats());
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
        actionDroid(this, ACTION::MOVE, order->pos.x, order->pos.y);
      } else {
        moveToRearm(this);
      }
    }
  }

  /* Overall action function that is called by the specific action functions */
  void Droid::actionDroidBase(Action* psAction)
  {
    ASSERT_OR_RETURN(, psAction->psObj == nullptr || !psAction->psObj->died, "Droid dead");

    WeaponStats* psWeapStats = getWeaponStats(this, 0);
    Vector2i pos(0, 0);

    bool secHoldActive = secondaryGetState(this, SECONDARY_ORDER::HALT_TYPE) == DSS_HALT_HOLD;
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
    switch (psAction->action)
    {
      case NONE:
        // Clear up what ever the droid was doing before if necessary
        if (!isStationary())
        {
          moveStopDroid(this);
        }
        action = NONE;
        actionPos = Vector2i(0, 0);
        timeActionStarted = 0;
        actionPointsDone = 0;
        if (psDroid->numWeaps > 0) {
          for (int i = 0; i < psDroid->numWeaps; i++)
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
          if (psAction->psObj->type == OBJ_STRUCTURE
              && !validStructResistance((Structure*)psAction->psObj))
          {
            //structure is low resistance already so don't attack
            action = NONE;
            break;
          }

          //in multiPlayer cannot electronically attack a transporter
          if (bMultiPlayer
              && psAction->psObj->type == OBJ_DROID
              && isTransporter((Droid*)psAction->psObj)) {
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
            || (!isVtol() && (orderStateObj(this, ORDER_TYPE::FIRE_SUPPORT) != nullptr)))
        {
          action = ATTACK; // holding, try attack straightaway
        }
        else if (actionInsideMinRange(this, psAction->psObj, psWeapStats)) // too close?
        {
          if (!proj_Direct(psWeapStats))
          {
            if (psWeapStats->rotate)
            {
              action = ATTACK;
            }
            else
            {
              action = ROTATE_TO_ATTACK;
              moveTurnDroid(this, actionTarget[0]->getPosition().x, actionTarget[0]->getPosition().y);
            }
          }
          else if (order->type != ORDER_TYPE::HOLD && secondaryGetState(this, DSO_HALTTYPE) != DSS_HALT_HOLD)
          {
            int pbx = 0;
            int pby = 0;
            /* direct fire - try and extend the range */
            action = MOVE_TO_ATTACK;
            actionCalcPullBackPoint(this, psAction->psObj, &pbx, &pby);

            turnOffMultiMsg(true);
            moveDroidTo(this, (UDWORD)pbx, (UDWORD)pby);
            turnOffMultiMsg(false);
          }
        }
        else if (order->type != ORDER_TYPE::HOLD && secondaryGetState(this, DSO_HALTTYPE) != DSS_HALT_HOLD)
          // approach closer?
        {
          action = MOVE_TO_ATTACK;
          turnOffMultiMsg(true);
          moveDroidTo(this, psAction->psObj->getPosition().x, psAction->psObj->getPosition().y);
          turnOffMultiMsg(false);
        }
        else if (order->type != ORDER_TYPE::HOLD && secondaryGetState(this, DSO_HALTTYPE) == DSS_HALT_HOLD)
        {
          action = ATTACK;
        }
        break;

      case MOVE_TO_REARM:
        action = MOVE_TO_REARM;
        actionPos = psAction->psObj->getPosition().xy();
        timeActionStarted = gameTime;
        setDroidActionTarget(this, psAction->psObj, 0);
        pos = actionTarget[0]->pos.xy();
        if (!actionVTOLLandingPos(this, &pos)) {
          // totally bunged up - give up
          objTrace(getId(), "move to rearm action failed!");
          orderDroid(this, ORDER_TYPE::RETURN_TO_BASE, ModeImmediate);
          break;
        }
        objTrace(getId(), "move to rearm");
        moveDroidToDirect(this, pos.x, pos.y);
        break;
      case CLEAR_REARM_PAD:
        debug(LOG_NEVER, "Unit %d clearing rearm pad", getId());
        action = CLEAR_REARM_PAD;
        setDroidActionTarget(this, psAction->psObj, 0);
        pos = actionTarget[0]->getPosition().xy();
        if (!actionVTOLLandingPos(this, &pos))
        {
          // totally bunged up - give up
          objTrace(getId(), "clear rearm pad action failed!");
          orderDroid(this, ORDER_TYPE::RETURN_TO_BASE, ModeImmediate);
          break;
        }
        objTrace(getId(), "move to clear rearm pad");
        moveDroidToDirect(this, pos.x, pos.y);
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
        ASSERT_OR_RETURN(, (order->target != nullptr) && (order->target->type == OBJ_STRUCTURE),
                           "invalid target for demolish order");
        order->structure_stats = ((Structure*)order->target)->pStructureType;
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
                (actionTarget[0] != nullptr) && (actionTarget[0]->type == OBJ_STRUCTURE),
                "invalid target for repair order");
        order->structure_stats = ((Structure*)actionTarget[0])->pStructureType;
        if (secHoldActive && (order->type == ORDER_TYPE::NONE || order->type == ORDER_TYPE::HOLD)) {
          action = REPAIR;
        }
        else if ((!secHoldActive && order->type != ORDER_TYPE::HOLD) || (secHoldActive && order->type == ORDER_TYPE::REPAIR)) {
          action = MOVE_TO_REPAIR;
          moveDroidTo(this, psAction->x, psAction->y);
        }
        break;
      case OBSERVE:
        action = psAction->action;
        setDroidActionTarget(this, psAction->psObj, 0);
        actionPos.x = getPosition().x;
        actionPos.y = getPosition().y;
        if (secondaryGetState(this, SECONDARY_ORDER::HALT_TYPE) != DSS_HALT_GUARD && (order->type == ORDER_TYPE::NONE || order->type ==
                                                                                                              ORDER_TYPE::HOLD))
        {
          action = visibleObject(this, actionTarget[0], false)
                            ? OBSERVE
                            : NONE;
        }
        else if (!hasCbSensor() && ((!secHoldActive && order->type != ORDER_TYPE::HOLD) || (secHoldActive && order->
                type == ORDER_TYPE::OBSERVE)))
        {
          action = MOVE_TO_OBSERVE;
          moveDroidTo(this, actionTarget[0]->getPosition().x, actionTarget[0]->getPosition().y);
        }
        break;
      case FIRE_SUPPORT:
        action = FIRE_SUPPORT;
        if (!isVtol() && !secHoldActive && order->target->type != OBJ_STRUCTURE) {
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
        moveDroidToDirect(this, psAction->x, psAction->y);

        // make sure there aren't any other VTOLs on the rearm pad
        ensureRearmPadClear((Structure*)psAction->psObj, this);
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
        if (secHoldActive && (order->type == ORDER_TYPE::NONE || order->type == ORDER_TYPE::HOLD))
        {
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
      case ACTION::RESTORE:
        ASSERT_OR_RETURN(, order->type == ORDER_TYPE::RESTORE, "cannot start restore action without a restore order");
        psDroid->action = psAction->action;
        psDroid->actionPos.x = psAction->x;
        psDroid->actionPos.y = psAction->y;
        ASSERT_OR_RETURN(, (order->psObj != nullptr) && (order->psObj->type == OBJ_STRUCTURE),
                           "invalid target for restore order");
        order->psStats = ((Structure*)order->psObj)->pStructureType;
        setDroidActionTarget(psDroid, psAction->psObj, 0);
        if (order->type != ORDER_TYPE::HOLD)
        {
          psDroid->action = ACTION::MOVETORESTORE;
          moveDroidTo(psDroid, psAction->x, psAction->y);
        }
        break;
      default:
        ASSERT(!"unknown action", "actionUnitBase: unknown action");
        break;
    }
    syncDebugDroid(psDroid, '+');
  }

  // Update the action state for a droid
  void Droid::actionUpdateDroid()
  {
    bool (*actionUpdateFunc)(Droid* psDroid) = nullptr;
    bool nonNullWeapon[MAX_WEAPONS] = {false};
    SimpleObject* psTargets[MAX_WEAPONS] = {nullptr};
    bool hasValidWeapon = false;
    bool hasVisibleTarget = false;
    bool targetVisibile[MAX_WEAPONS] = {false};
    bool bHasTarget = false;
    bool bDirect = false;
    Structure* blockingWall = nullptr;
    bool wallBlocked = false;

    auto& psPropStats = propulsion;
    bool secHoldActive = secondaryGetState(this, 
                                           SECONDARY_ORDER::HALT_TYPE) == DSS_HALT_HOLD;

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
        if (numWeapons(*this) > 0 && !isVtol()
            && (order->type == ORDER_TYPE::NONE || order->type == ORDER_TYPE::HOLD ||
                order->type == ORDER_TYPE::RETURN_TO_REPAIR || order->type == ORDER_TYPE::GUARD)) {
          for (unsigned i = 0; i < psDroid->numWeaps; ++i)
          {
            if (nonNullWeapon[i])
            {
              SimpleObject* psTemp = nullptr;

              WeaponStats* const psWeapStats = &asWeaponStats[psDroid->asWeaps[i].nStat];
              if (psDroid->asWeaps[i].nStat > 0
                  && psWeapStats->rotate
                  && aiBestNearestTarget(this, &psTemp, i) >= 0)
              {
                if (secondaryGetState(this, SECONDARY_ORDER::ATTACK_LEVEL) == DSS_ALEV_ALWAYS)
                {
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
                                     order->target->getPosition().x, order->target->getPosition().y,
                                     (dynamic_cast<Structure*>(order->target))->getRotation().direction,
                                     (dynamic_cast<Structure*>(order->target))->pStructureType)) {
            moveDroidToNoFormation(this, order->target->getPosition().x, order->target->getPosition().y);
          }
        }
        else if (order->type == ORDER_TYPE::RETURN_TO_REPAIR && 
                 order->rtrType == RTR_DATA_TYPE::DROID &&
                 isStationary()) {
          if (!actionReachedDroid(this, dynamic_cast<Droid*>(order->target))) {
            moveDroidToNoFormation(this, order->target->getPosition().x,
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
          if (((int)(mission.ETA - (gameTime - missionGetReinforcementTime())) <= 0) && enoughTimeRemaining) {
            unsigned droidX, droidY;

            if (!droidRemove(this, mission.apsDroidLists))
            {
              ASSERT_OR_RETURN(, false, "Unable to remove transporter from mission list");
            }
            addDroid(this, apsDroidLists);
            //set the x/y up since they were set to INVALID_XY when moved offWorld
            missionGetTransporterExit(selectedPlayer, &droidX, &droidY);
            pos.x = droidX;
            pos.y = droidY;
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
        if (isStationary())
        {
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
        else if (psDroid->numWeaps > 0) {
          for (unsigned i = 0; i < psDroid->numWeaps; ++i)
          {
            if (nonNullWeapon[i]) {
              SimpleObject* psTemp = nullptr;

              //I moved psWeapStats flag update there
              WeaponStats* const psWeapStats = &asWeaponStats[psDroid->asWeaps[i].nStat];
              if (!isVtol()
                  && psDroid->asWeaps[i].nStat > 0
                  && psWeapStats->rotate
                  && psWeapStats->fireOnMove
                  && aiBestNearestTarget(this, &psTemp, i) >= 0) {
                if (secondaryGetState(this, SECONDARY_ORDER::ATTACK_LEVEL) == DSS_ALEV_ALWAYS) {
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
        for (unsigned i = 0; i < psDroid->numWeaps; ++i)
        {
          bDirect = proj_Direct(asWeaponStats + psDroid->asWeaps[i].nStat);
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
            SimpleObject* psTemp;
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
          for (unsigned i = 0; i < psDroid->numWeaps; ++i)
          {
            const unsigned compIndex = psDroid->asWeaps[i].nStat;
            const WeaponStats* psStats = asWeaponStats + compIndex;
            wallBlocked = false;

            // has weapon a target? is target valid?
            if (actionTarget[i] != nullptr && validTarget(this, actionTarget[i], i)) {
              // is target visible and weapon is not a Nullweapon?
              if (targetVisibile[i] && nonNullWeapon[i]) {
                // to fix a AA-weapon attack ground unit exploit 
                SimpleObject* psActionTarget = nullptr;
                blockingWall = dynamic_cast<Structure*>(visGetBlockingWall(this, actionTarget[i]));

                if (proj_Direct(psStats) && blockingWall) {
                  WEAPON_EFFECT weapEffect = psStats->weaponEffect;

                  if (!aiCheckAlliances(getPlayer(), blockingWall->getPlayer())
                      && asStructStrengthModifier[weapEffect][blockingWall->pStructureType->strength] >=
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
                if (!wallBlocked && actionTargetTurret(this, psActionTarget, &psDroid->asWeaps[i])) {
                  // In range - fire !!!
                  combFire(&psDroid->asWeaps[i], this, psActionTarget, i);
                }
              }
            }
          }
          // Droid don't have a visible target and it is not in pursue mode
          if (!hasVisibleTarget && secondaryGetState(this, SECONDARY_ORDER::ATTACK_LEVEL) != DSS_ALEV_ALWAYS) {
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
        if (actionTarget[0] == nullptr && actionTarget[1] != nullptr) {
          break;
        }
        ASSERT_OR_RETURN(, actionTarget[0] != nullptr, "target is NULL while attacking");

        if (action == ACTION::ROTATE_TO_ATTACK) {
          if (movement->status == MOVE_STATUS::TURN_TO_TARGET) {
            moveTurnDroid(this, actionTarget[0]->getPosition().x,
                       actionTarget[0]->getPosition().y);
            break; // Still turning.
          }
          action = ATTACK;
        }

        //check the target hasn't become one the same player ID - Electronic Warfare
        if (electronicDroid(this) && getPlayer() == actionTarget[0]->getPlayer()) {
          for (unsigned i = 0; i < psDroid->numWeaps; ++i)
          {
            setDroidActionTarget(this, nullptr, i);
          }
          action = NONE;
          break;
        }

        bHasTarget = false;
        wallBlocked = false;
        for (unsigned i = 0; i < psDroid->numWeaps; ++i)
        {
          SimpleObject* psActionTarget;

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
                  aiChooseTarget(this, &psTargets[i], i, false, nullptr)) {
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
            WeaponStats* const psWeapStats = &asWeaponStats[psDroid->asWeaps[i].nStat];
            WEAPON_EFFECT weapEffect = psWeapStats->weaponEffect;
            blockingWall = dynamic_cast<Structure *>(visGetBlockingWall(this, psActionTarget));

            // if a wall is inbetween us and the target, try firing at the wall if our
            // weapon is good enough
            if (proj_Direct(psWeapStats) && blockingWall) {
              if (!aiCheckAlliances(getPlayer(), blockingWall->getPlayer()
                  && asStructStrengthModifier[weapEffect][blockingWall->pStructureType->strength] >=
                     MIN_STRUCTURE_BLOCK_STRENGTH) {
                psActionTarget = (SimpleObject*)blockingWall;
                setDroidActionTarget(psDroid, psActionTarget, i);
              }
              else {
                wallBlocked = true;
              }
            }

            if (!bHasTarget) {
              bHasTarget = actionInRange(this, psActionTarget, i, false);
            }

            if (validTarget(this, psActionTarget, i) && !wallBlocked) {
              int dirDiff = 0;

              if (!psWeapStats->rotate) {
                // no rotating turret - need to check aligned with target
                const uint16_t targetDir = calcDirection(getPosition().x, getPosition().y,
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
                  moveTurnDroid(this, psActionTarget->getPosition().x, 
                                psActionTarget->getPosition().y);
                }
              }
              else if (!psWeapStats->rotate ||
                       actionTargetTurret(this, psActionTarget, &psDroid->asWeaps[i])) {
                /* In range - fire !!! */
                combFire(&psDroid->asWeaps[i], this, psActionTarget, i);
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
          SimpleObject* psTarget;
          bool supportsSensorTower = !isVtol() && (psTarget = orderStateObj(this, ORDER_TYPE::FIRE_SUPPORT))
                                     && dynamic_cast<Structure*>(psTarget);

          if (secHoldActive && (order->type == ORDER_TYPE::ATTACK_TARGET || 
                                order->type == ORDER_TYPE::FIRE_SUPPORT)) {
            action = :NONE; // secondary holding, cancel the order.
          }
          else if (secondaryGetState(this, SECONDARY_ORDER::HALT_TYPE) == DSS_HALT_PURSUE &&
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
            auto commander = group->psCommander;

            if (commander->getOrder().type == ORDER_TYPE::ATTACK_TARGET ||
                commander->getOrder().type == ORDER_TYPE::FIRE_SUPPORT ||
                commander->getOrder().type == ORDER_TYPE::ATTACK) {
              action = MOVE_TO_ATTACK;
            }
            else {
              action = NONE;
            }
          }
          else if (secondaryGetState(this,
                                     SECONDARY_ORDER::HALT_TYPE) != DSS_HALT_HOLD) {
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
        WeaponStats* psWeapStats = nullptr;
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

          for (unsigned i = 0; i < psDroid->numWeaps; ++i)
          {
            if (nonNullWeapon[i]
                && validTarget(this, actionTarget[0], i)) {
              //I moved psWeapStats flag update there
              psWeapStats = &asWeaponStats[psDroid->asWeaps[i].nStat];
              if (actionVisibleTarget(this, actionTarget[0], i)) {
                if (actionInRange(this, actionTarget[0], i)) {
                  if (getPlayer() == selectedPlayer) {
                    audio_QueueTrackMinDelay(ID_SOUND_COMMENCING_ATTACK_RUN2,
                                             VTOL_ATTACK_AUDIO_DELAY);
                  }

                  if (actionTargetTurret(this, actionTarget[0], &psDroid->asWeaps[i])) {
                    // In range - fire !!!
                    combFire(&psDroid->asWeaps[i], this,
                             actionTarget[0], i);
                  }
                }
                else {
                  actionTargetTurret(this, actionTarget[0], &psDroid->asWeaps[i]);
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
                moveDroidToDirect(this, actionTarget[0]->getPosition().x,
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
        for (unsigned i = 0; i < psDroid->numWeaps; ++i)
        {
          hasValidWeapon |= validTarget(this, actionTarget[0], i);
        }
        //check the target hasn't become one the same player ID - Electronic Warfare, and that the target is still valid.
        if ((hasElectronicWeapon() && getPlayer() == actionTarget[0]->getPlayer()) || !hasValidWeapon) {
          for (unsigned i = 0; i < psDroid->numWeaps; ++i)
          {
            setDroidActionTarget(this, nullptr, i);
          }
          action = NONE;
        }
        else {
          if (actionVisibleTarget(this, actionTarget[0], 0)) {
            for (unsigned i = 0; i < psDroid->numWeaps; ++i)
            {
              if (nonNullWeapon[i]
                  && validTarget(this, actionTarget[0], i)
                  && actionVisibleTarget(this, actionTarget[0], i)) {
                bool chaseBloke = false;
                WeaponStats* const psWeapStats = &asWeaponStats[psDroid->asWeaps[i].nStat];

                if (psWeapStats->rotate) {
                  actionTargetTurret(this, actionTarget[0], &psDroid->asWeaps[i]);
                }

                if (!isVtol() &&
                    dynamic_cast<Droid*>(actionTarget[0]) &&
                    ((Droid*)actionTarget[0])->type == DROID_TYPE::PERSON &&
                    psWeapStats->fireOnMove) {
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

                    if (psWeapStats->rotate) {
                      action = ATTACK;
                    }
                    else {
                      action = ROTATE_TO_ATTACK;
                      moveTurnDroid(this, actionTarget[0]->getPosition().x,
                                    actionTarget[0]->getPosition().y);
                    }
                  }
                }
                else if (actionInRange(this, actionTarget[0], i))
                {
                  // fire while closing range
                  if ((blockingWall = visGetBlockingWall(psDroid, actionTarget[0])) && proj_Direct(
                          psWeapStats)) {
                    WEAPON_EFFECT weapEffect = psWeapStats->weaponEffect;

                    if (!aiCheckAlliances(getPlayer(), blockingWall->getPlayer())
                        && asStructStrengthModifier[weapEffect][blockingWall->pStructureType->strength] >=
                           MIN_STRUCTURE_BLOCK_STRENGTH) {
                      //Shoot at wall if the weapon is good enough against them
                      combFire(&psDroid->asWeaps[i], this, (SimpleObject*)blockingWall, i);
                    }
                  }
                  else {
                    combFire(&psDroid->asWeaps[i], this, actionTarget[0], i);
                  }
                }
              }
            }
          }
          else
          {
            for (unsigned i = 0; i < psDroid->numWeaps; ++i)
            {
              if ((psDroid->asWeaps[i].rotation.direction != 0) ||
                  (psDroid->asWeaps[i].rotation.pitch != 0))
              {
                actionAlignTurret(this, i);
              }
            }
          }

          if (isStationary() && action != ATTACK) {
            /* Stopped moving but haven't reached the target - possibly move again */

            //'hack' to make the droid to check the primary turrent instead of all
            WeaponStats* const psWeapStats = &asWeaponStats[psDroid->asWeaps[0].nStat];

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
                if (psWeapStats->rotate) {
                  action = ATTACK;
                }
                else {
                  action = ROTATE_TO_ATTACK;
                  moveTurnDroid(this, actionTarget[0]->getPosition().x,
                                actionTarget[0]->getPosition().y);
                }
              }
            }
            else if (order->type != ORDER_TYPE::HOLD) // approach closer?
            {
              // try to close the range
              moveDroidTo(this, actionTarget[0]->getPosition().x, actionTarget[0]->getPosition().y);
            }
          }
        }
        break;

      case SULK:
        // unable to route to target ... don't do anything aggressive until time is up
        // we need to do something defensive at this point ???

        //hmmm, hope this doesn't cause any problems!
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
            else
              // There's an allied structure already there.  Is it a wall, and can the droid upgrade it to a defence or gate?
            if (isWall(structureAtBuildPosition->pStructureType->type) &&
                (desiredStructure->type == REF_DEFENSE || desiredStructure->type == REF_GATE)) {
              // It's always valid to upgrade a wall to a defence or gate
              droidCannotBuild = false; // Just to avoid an empty branch
            }
            else if ((structureAtBuildPosition->pStructureType != desiredStructure) &&
                     // ... it's not the exact same type as the droid was ordered to build
                     (structureAtBuildPosition->pStructureType->type == REF_WALLCORNER && desiredStructure->type !=
                                                                                          REF_WALL)) // and not a wall corner when the droid wants to build a wall
            {
              // And so the droid can't build or help with building this structure
              droidCannotBuild = true;
            }
            else
              // So it's a structure that the droid could help to build, but is it already complete?
            if (structureAtBuildPosition->status == STRUCTURE_STATE::BUILT &&
                (!IsStatExpansionModule(desiredStructure) || !canStructureHaveAModuleAdded(
                        structureAtBuildPosition))) {
              // The building is complete and the droid hasn't been told to add a module, or can't add one, so can't help with that.
              droidCannotBuild = true;
            }

            if (droidCannotBuild) {
              if (order->type == ORDER_TYPE::LINE_BUILD &&
                  map_coord(order->pos) != map_coord(order->pos2)) {
                // The droid is doing a line build, and there's more to build. This will force the droid to move to the next structure in the line build
                objTrace(getId(),
                         "ACTION::MOVETOBUILD: line target is already built, or can't be built - moving to next structure in line")
                        ;
                action = NONE;
              }
              else {
                // Cancel the current build order. This will move the truck onto the next order, if it has one, or halt in place.
                objTrace(getId(),
                         "ACTION::MOVETOBUILD: target is already built, or can't be built - executing next order or halting")
                        ;
                cancelBuild();
              }
              break;
            }
          }
        } // End of check for whether the droid can still succesfully build the ordered structure

        // The droid can still build or help with a build, and is moving to a location to do so - are we there yet, are we there yet, are we there yet?
        if (actionReachedBuildPos(this, actionPos.x, actionPos.y, order->direction,
                                  order->structure_stats)) {
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
            if (IsStatExpansionModule(psStructStats)) {
              syncDebug("Reached build target: module");
              debug(LOG_NEVER, "ACTION::MOVETOBUILD: setUpBuildModule");
              setUpBuildModule(this);
            }
            else if (TileHasStructure(worldTile(pos))) {
              // structure on the build location - see if it is the same type
              auto psStruct = getTileStructure(map_coord(pos.x), map_coord(pos.y));
              if (psStruct->pStructureType == order->structure_stats ||
                  (order->structure_stats->type == REF_WALL && psStruct->pStructureType->type == REF_WALLCORNER)) {
                // same type - do a help build
                syncDebug("Reached build target: do-help");
                setDroidTarget(this, psStruct);
                helpBuild = true;
              }
              else if ((psStruct->pStructureType->type == REF_WALL ||
                        psStruct->pStructureType->type == REF_WALLCORNER) &&
                       (order->structure_stats->type == REF_DEFENSE ||
                        order->structure_stats->type == REF_GATE)) {
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
            else if (!validLocation(order->structure_stats, pos, dir, getPlayer(), false))
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
                if (psStruct->pStructureType == order->structure_stats) {
                  // same type - do a help build
                  setDroidTarget(this, psStruct);
                  helpBuild = true;
                }
                else if ((psStruct->pStructureType->type == REF_WALL || psStruct->pStructureType->type ==
                                                                        REF_WALLCORNER) &&
                         (order->psStats->type == REF_DEFENSE || order->psStats->type == REF_GATE)) {
                  // building a gun tower over a wall - OK
                  if (droidStartBuild(this)) {
                    objTrace(getId(), "ACTION::MOVETOBUILD: start building defense");
                    action = BUILD;
                  }
                }
                else if ((psStruct->pStructureType->type == REF_FACTORY && order->psStats->type ==
                                                                           REF_FACTORY_MODULE) ||
                         (psStruct->pStructureType->type == REF_RESEARCH && order->psStats->type ==
                                                                            REF_RESEARCH_MODULE) ||
                         (psStruct->pStructureType->type == REF_POWER_GEN && order->psStats->type ==
                                                                             REF_POWER_MODULE) ||
                         (psStruct->pStructureType->type == REF_VTOL_FACTORY && order->psStats->type ==
                                                                                REF_FACTORY_MODULE)) {
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
              else if (TileHasFeature(psTile))
              {
                Feature* feature = getTileFeature(map_coord(psDroid->actionPos.x),
                                                  map_coord(psDroid->actionPos.y));
                objTrace(getId(), "ACTION::MOVETOBUILD: tile has feature %d", feature->psStats->subType);
                if (feature->psStats->subType == FEAT_OIL_RESOURCE && order->psStats->type ==
                                                                      REF_RESOURCE_EXTRACTOR) {
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
            if (droidStartBuild()) {
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
                                   order->structure_stats)) {
          objTrace(getId(), "ACTION::BUILD: Starting to drive toward construction site");
          moveDroidToNoFormation(this, actionPos.x, actionPos.y);
        }
        else if (!isStationary() &&
                 movement->status != MOVE_STATUS::TURN_TO_TARGET &&
                 movement->status != MOVE_STATUS::SHUFFLE &&
                 actionReachedBuildPos(this, actionPos.x, actionPos.y, order->direction,
                                       order->structure_stats))
        {
          objTrace(getId(), "ACTION::BUILD: Stopped - at construction site");
          moveStopDroid(this);
        }
        if (action == SULK) {
          objTrace(getId(), "Failed to go to objective, aborting build action");
          action = NONE;
          break;
        }
        if (droidUpdateBuild(this)) {
          actionTargetTurret(this, actionTarget[0], &psDroid->asWeaps[0]);
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
          auto structureAtPos = getTileStructure(map_coord(actionPos.x), map_coord(actionPos.y));

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
                     structureAtPos->body == structureBody(structureAtPos)) {
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
                                  order->structure_stats)) {
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
        // set up for the specific action
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
                                                             order->structure_stats))
        {
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
                                       ((Structure*)actionTarget[0])->getRotation().direction, order->structure_stats))
        {
          objTrace(getId(), "Stopped - reached build position");
          moveStopDroid(this);
        }
        else if (actionUpdateFunc(this))
        {
          //use 0 for non-combat(only 1 'weapon')
          actionTargetTurret(this, actionTarget[0], &psDroid->asWeaps[0]);
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
          if (actionReachedBuildPos(this, actionTarget[0]->getPosition().x, actionTarget[0]->getPosition().y,
                                    ((Structure*)actionTarget[0])->getRotation().direction,
                                    ((Structure*)actionTarget[0])->pStructureType)) {
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
          if (reached)
          {
            if (getHp() >= originalHp) {
              objTrace(getId(), "Repair not needed of droid %d", (int)getId());
              /* set droid points to max */
              body = originalHp;
              // if completely repaired then reset order
              secondarySetState(this, SECONDARY_ORDER::RETURN_TO_LOCATION, DSS_NONE);
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
        actionTargetTurret(this, actionTarget[0], &psDroid->asWeaps[0]);

        if (!hasCbSensor()) {
          // make sure the target is within sensor range
          const auto xdiff = (int)getPosition().x - (int)actionTarget[0]->getPosition().x;
          const auto ydiff = (int)getPosition().y - (int)actionTarget[0]->getPosition().y;
          int rangeSq = droidSensorRange(this);
          rangeSq = rangeSq * rangeSq;
          if (!visibleObject(this, actionTarget[0], false)
              || xdiff * xdiff + ydiff * ydiff >= rangeSq) {
            if (secondaryGetState(this, SECONDARY_ORDER::HALT_TYPE) != DSS_HALT_GUARD && 
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
        actionTargetTurret(this, actionTarget[0], &psDroid->asWeaps[0]);

        if (visibleObject(this, actionTarget[0], false)) {
          // make sure the target is within sensor range
          const int xdiff = (int)getPosition().x - (int)actionTarget[0]->getPosition().x;
          const int ydiff = (int)getPosition().y - (int)actionTarget[0]->getPosition().y;
          int rangeSq = droidSensorRange(this);
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
        ASSERT_OR_RETURN(, (order->target->type == OBJ_DROID || order->target->type == OBJ_STRUCTURE)
                           && aiCheckAlliances(order->target->getPlayer(), getPlayer()),
                           "ACTION::FIRESUPPORT: incorrect target type");

        // don't move VTOL's
        // also don't move closer to sensor towers
        if (!isVtol() && !dynamic_cast<Structure*>(order->target)) {
          Vector2i diff = (getPosition() - order->target->getPosition()).xy();
          //Consider .shortRange here
          int rangeSq = asWeaponStats[psDroid->asWeaps[0].nStat].upgraded[getPlayer()].maxRange / 2;
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
        ASSERT_OR_RETURN(, actionTargetObj != nullptr && actionTargetObj->type == OBJ_DROID,
                           "unexpected repair target");
        auto actionTarget = (const Droid*)actionTargetObj;
        if (actionTarget->hitPoints == actionTarget->originalHp) {
          // target is healthy: nothing to do
          action = NONE;
          moveStopDroid(this);
          break;
        }
        Vector2i diff = (getPosition() - actionTarget[0].getPosition()).xy();
        // moving to repair a droid
        if (!actionTarget[0] || // Target missing.
            (order->type != ORDER_TYPE::DROID_REPAIR && dot(diff, diff) > 2 * REPAIR_MAXDIST * REPAIR_MAXDIST))
          // Target farther then 1.4142 * REPAIR_MAXDIST and we aren't ordered to follow.
        {
          action = NONE;
          return;
        }
        if (dot(diff, diff) < REPAIR_RANGE * REPAIR_RANGE)
        {
          // Got to destination - start repair
          //rotate turret to point at droid being repaired
          //use 0 for repair droid
          actionTargetTurret(this, actionTarget[0], &psDroid->asWeaps[0]);
          droidStartAction(this);
          action = DROID_REPAIR;
        }
        if (isStationary()) {
          // Couldn't reach destination - try and find a new one
          actionPos = actionTarget[0].getPosition().xy();
          moveDroidTo(this, actionPos.x, actionPos.y);
        }
        break;
      }
      case DROID_REPAIR:
      {
        int xdiff, ydiff;

        // If not doing self-repair (psActionTarget[0] is repair target)
        if (actionTarget[0] != this) {
          actionTargetTurret(this, actionTarget[0], &psDroid->asWeaps[0]);
        }
          // Just self-repairing.
          // See if there's anything to shoot.
        else if (psDroid->numWeaps > 0 && !isVtol()
                 && (order->type == ORDER_TYPE::NONE || order->type == ORDER_TYPE::HOLD || order->type == ORDER_TYPE::RETURN_TO_REPAIR))
        {
          for (unsigned i = 0; i < psDroid->numWeaps; ++i)
          {
            if (nonNullWeapon[i])
            {
              SimpleObject* psTemp = nullptr;

              WeaponStats* const psWeapStats = &asWeaponStats[psDroid->asWeaps[i].nStat];
              if (psDroid->asWeaps[i].nStat > 0 && psWeapStats->rotate
                  && secondaryGetState(this, SECONDARY_ORDER::ATTACK_LEVEL) == DSS_ALEV_ALWAYS
                  && aiBestNearestTarget(this, &psTemp, i) >= 0 && psTemp)
              {
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
        if (xdiff * xdiff + ydiff * ydiff > REPAIR_RANGE * REPAIR_RANGE)
        {
          if (order->type == ORDER_TYPE::DROID_REPAIR)
          {
            // damaged droid has moved off - follow if we're not holding position!
            actionPos = actionTarget[0]->getPosition().xy();
            action = MOVE_TO_DROID_REPAIR;
            moveDroidTo(this, actionPos.x, actionPos.y);
          }
          else
          {
            action = NONE;
          }
        }
        else
        {
          if (!droidUpdateDroidRepair(this))
          {
            action = NONE;
            moveStopDroid(this);
            //if the order is RTR then resubmit order so that the unit will go to repair facility point
            if (orderState(this, ORDER_TYPE::RETURN_TO_REPAIR))
            {
              orderDroid(this, ORDER_TYPE::RETURN_TO_REPAIR, ModeImmediate);
            }
          }
          else
          {
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
        if (actionTarget[0] == nullptr)
        {
          // rearm pad destroyed - move to another
          objTrace(getId(), "rearm pad gone - switch to new one");
          moveToRearm(this);
          break;
        }
        if (isStationary() && vtolHappy(*this))
        {
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
        if (actionTarget[0] == nullptr)
        {
          // base destroyed - find another
          objTrace(getId(), "rearm gone - find another");
          moveToRearm(this);
          break;
        }

        if (visibleObject(this, actionTarget[0], false))
        {
          auto psStruct = findNearestReArmPad(this, (Structure*)actionTarget[0], true);
          // got close to the rearm pad - now find a clear one
          objTrace(getId(), "Seen rearm pad - searching for available one");

          if (psStruct != nullptr)
          {
            // found a clear landing pad - go for it
            objTrace(getId(), "Found clear rearm pad");
            setDroidActionTarget(this, psStruct, 0);
          }

          action = WAIT_FOR_REARM;
        }

        if (isStationary() || action == WAIT_FOR_REARM)
        {
          Vector2i pos = actionTarget[0]->getPosition().xy();
          if (!actionVTOLLandingPos(this, &pos))
          {
            // totally bunged up - give up
            objTrace(getId(), "Couldn't find a clear tile near rearm pad - returning to base");
            orderDroid(this, ORDER_TYPE::RETURN_TO_BASE, ModeImmediate);
            break;
          }
          objTrace(getId(), "moving to rearm pad at %d,%d (%d,%d)", (int)pos.x, (int)pos.y,
                   (int)(pos.x/TILE_UNITS), (int)(pos.y/TILE_UNITS));
          moveDroidToDirect(this, pos.x, pos.y);
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
      if (psDroid->numWeaps == 0)
      {
        if (psDroid->asWeaps[0].rotation.direction != 0 || psDroid->asWeaps[0].rotation.pitch != 0)
        {
          actionAlignTurret(this, 0);
        }
      }
      else
      {
        for (unsigned i = 0; i < psDroid->numWeaps; ++i)
        {
          if (psDroid->asWeaps[i].rotation.direction != 0 || psDroid->asWeaps[i].rotation.pitch != 0) {
            actionAlignTurret(this, i);
          }
        }
      }
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
	WeaponStats* psStats;
	bool bSalvo;
	int firingStage, interval;

	if (numWeapons(psObj) == 0) // no weapon {
		return -1;
	}
	psStats = asWeaponStats + psWeap->nStat;

	/* Justifiable only when greater than a one second reload or intra salvo time  */
	bSalvo = (psStats->upgraded[psObj->player].numRounds > 1);
	if ((bSalvo && psStats->upgrade[psObj->player].reloadTime > GAME_TICKS_PER_SEC)
		|| psStats->upgrade[psObj->player].firePause > GAME_TICKS_PER_SEC
		|| (psObj->type == OBJ_DROID && isVtolDroid((const Droid*)psObj)))
	{
		if (psObj->type == OBJ_DROID && isVtolDroid((const Droid*)psObj))
		{
			//deal with VTOLs
			firingStage = getNumAttackRuns((const Droid*)psObj, weapon_slot) - ((const Droid*)psObj)->asWeaps[
				weapon_slot].ammo_used;

			//compare with max value
			interval = getNumAttackRuns((const Droid*)psObj, weapon_slot);
		}
		else
		{
			firingStage = gameTime - psWeap->time_last_fired;
			interval = bSalvo ? weaponReloadTime(psStats, psObj->player) : weaponFirePause(psStats, psObj->player);
		}
		if (firingStage < interval && interval > 0)
		{
			return PERCENT(firingStage, interval);
		}
		return 100;
	}
	return -1;
}

#define UNIT_LOST_DELAY	(5*GAME_TICKS_PER_SEC)

/* Deals damage to a droid
 * \param psDroid droid to deal damage to
 * \param damage amount of damage to deal
 * \param weaponClass the class of the weapon that deals the damage
 * \param weaponSubClass the subclass of the weapon that deals the damage
 * \param angle angle of impact (from the damage dealing projectile in relation to this droid)
 * \return > 0 when the dealt damage destroys the droid, < 0 when the droid survives
 *
 */
int droidDamage(Droid* psDroid, unsigned damage, WEAPON_CLASS weaponClass,
                WEAPON_SUBCLASS weaponSubClass, unsigned impactTime,
                bool isDamagePerSecond, int minDamage)
{
	int relativeDamage;

	// VTOLs (and transporters in MP) on the ground take triple damage
	if ((psDroid->isVtol() || (isTransporter(*psDroid) && bMultiPlayer)) &&
      (psDroid->movement->status == MOVE_STATUS::INACTIVE))
	{
		damage *= 3;
	}

	relativeDamage = objDamage(psDroid, damage, psDroid->original_hp, weaponClass, weaponSubClass, isDamagePerSecond,
														 minDamage);

	if (relativeDamage > 0)
	{
		// reset the attack level
		if (secondaryGetState(psDroid, DSO_ATTACK_LEVEL) == DSS_ALEV_ATTACKED)
		{
			secondarySetState(psDroid, DSO_ATTACK_LEVEL, DSS_ALEV_ALWAYS);
		}
		// Now check for auto return on droid's secondary orders (i.e. return on medium/heavy damage)
		secondaryCheckDamageLevel(psDroid);
	}
	else if (relativeDamage < 0)
	{
		// Droid destroyed
		debug(LOG_ATTACK, "droid (%d): DESTROYED", psDroid->id);

		// Deal with score increase/decrease and messages to the player
		if (psDroid->player == selectedPlayer)
		{
			// TRANSLATORS:	Refers to the loss of a single unit, known by its name
			CONPRINTF(_("%s Lost!"), objInfo(psDroid));
			scoreUpdateVar(WD_UNITS_LOST);
			audio_QueueTrackMinDelayPos(ID_SOUND_UNIT_DESTROYED, UNIT_LOST_DELAY,
			                            psDroid->getPosition().x, psDroid->getPosition().y, psDroid->getPosition().z);
		}
		// only counts as a kill if it's not our ally
		else if (selectedPlayer < MAX_PLAYERS && !aiCheckAlliances(getPlayer(), selectedPlayer)) {
			scoreUpdateVar(WD_UNITS_KILLED);
		}

		// Do we have a dying animation?
		if (displayData.imd->objanimpie[ANIM_EVENT_DYING] && psDroid->animationEvent != ANIM_EVENT_DYING)
		{
			bool useDeathAnimation = true;
			//Babas should not burst into flames from non-heat weapons
			if (psDroid->getType() == DROID_TYPE::PERSON)
			{
				if (weaponClass == WEAPON_CLASS::HEAT)
				{
					// NOTE: 3 types of screams are available ID_SOUND_BARB_SCREAM - ID_SOUND_BARB_SCREAM3
					audio_PlayObjDynamicTrack(psDroid, ID_SOUND_BARB_SCREAM + (rand() % 3), nullptr);
				}
				else
				{
					useDeathAnimation = false;
				}
			}
			if (useDeathAnimation)
			{
				debug(LOG_DEATH, "%s droid %d (%p) is starting death animation", objInfo(psDroid), (int)psDroid->id,
				      static_cast<void *>(psDroid));
				timeAnimationStarted = gameTime;
				animationEvent = ANIM_EVENT_DYING;
			}
		}
		// Otherwise use the default destruction animation
		if (psDroid->animationEvent != ANIM_EVENT_DYING)
		{
			debug(LOG_DEATH, "%s droid %d (%p) is toast", objInfo(psDroid), (int)psDroid->id,
			      static_cast<void *>(psDroid));
			// This should be sent even if multi messages are turned off, as the group message that was
			// sent won't contain the destroyed droid
			if (bMultiPlayer && !bMultiMessages)
			{
				bMultiMessages = true;
				destroyDroid(psDroid, impactTime);
				bMultiMessages = false;
			}
			else
			{
				destroyDroid(psDroid, impactTime);
			}
		}
	}
	return relativeDamage;
}


std::priority_queue<int> copy_experience_queue(int player)
{
	return recycled_experience[player];
}

void add_to_experience_queue(int player, int value)
{
	recycled_experience[player].push(value);
}

// recycle a droid (retain it's experience and some of it's cost)
void recycleDroid(Droid* psDroid)
{
	// store the droids kills
	if (psDroid->experience > 0)
	{
		recycled_experience[psDroid->player].push(psDroid->experience);
	}

	// return part of the cost of the droid
	int cost = calcDroidPower(psDroid);
	cost = (cost / 2) * psDroid->body / psDroid->original_hp;
	addPower(psDroid->player, (UDWORD)cost);

	// hide the droid
	memset(psDroid->visible, 0, sizeof(psDroid->visible));

	if (psDroid->group)
	{
		psDroid->group->remove(psDroid);
	}

	triggerEvent(TRIGGER_OBJECT_RECYCLED, psDroid);
	vanishDroid(psDroid);

	Vector3i position = psDroid->getPosition().xzy();
	const auto mapCoord = map_coord({psDroid->getPosition().x, psDroid->getPosition().y});
	const auto psTile = mapTile(mapCoord);
	if (tileIsClearlyVisible(psTile))
	{
		addEffect(&position, EFFECT_EXPLOSION, EXPLOSION_TYPE_DISCOVERY, false, nullptr, false,
		          gameTime - deltaGameTime + 1);
	}
}


bool removeDroidBase(Droid* psDel)
{
	CHECK_DROID(psDel);

	if (isDead(psDel))
	{
		// droid has already been killed, quit
		syncDebug("droid already dead");
		return true;
	}

	syncDebugDroid(psDel, '#');

	//kill all the droids inside the transporter
	if (isTransporter(psDel))
	{
		if (psDel->group)
		{
			//free all droids associated with this Transporter
			Droid* psNext;
			for (auto psCurr = psDel->group->members; psCurr != nullptr && psCurr != psDel; psCurr = psNext)
			{
				psNext = psCurr->psGrpNext;

				/* add droid to droid list then vanish it - hope this works! - GJ */
				addDroid(psCurr, apsDroidLists);
				vanishDroid(psCurr);
			}
		}
	}

	// leave the current group if any
	if (psDel->group)
	{
		psDel->group->remove(psDel);
		psDel->group = nullptr;
	}

	/* Put Deliv. Pts back into world when a command droid dies */
	if (psDel->type == DROID_COMMAND)
	{
		for (auto psStruct = apsStructLists[psDel->player]; psStruct; psStruct = psStruct->psNext)
		{
			// alexl's stab at a right answer.
			if (StructIsFactory(psStruct)
				&& psStruct->pFunctionality->factory.psCommander == psDel)
			{
				assignFactoryCommandDroid(psStruct, nullptr);
			}
		}
	}

	// Check to see if constructor droid currently trying to find a location to build
	if (psDel->player == selectedPlayer && psDel->selected && isConstructionDroid(psDel))
	{
		// If currently trying to build, kill off the placement
		if (tryingToGetLocation())
		{
			int numSelectedConstructors = 0;
			for (Droid* psDroid = apsDroidLists[psDel->player]; psDroid != nullptr; psDroid = psDroid->psNext)
			{
				numSelectedConstructors += psDroid->selected && isConstructionDroid(psDroid);
			}
			if (numSelectedConstructors <= 1) // If we were the last selected construction droid.
			{
				kill3DBuilding();
			}
		}
	}

	if (psDel->player == selectedPlayer)
	{
		intRefreshScreen();
	}

	killDroid(psDel);
	return true;
}

static void removeDroidFX(Droid* psDel, unsigned impactTime)
{
	Vector3i pos;

	CHECK_DROID(psDel);

	// only display anything if the droid is visible
	if (!psDel->visibleForLocalDisplay())
	{
		return;
	}

	if (psDel->animationEvent != ANIM_EVENT_DYING)
	{
		compPersonToBits(psDel);
	}

	/* if baba then squish */
	if (psDel->type == DROID_PERSON)
	{
		// The barbarian has been run over ...
		audio_PlayStaticTrack(psDel->getPosition().x, psDel->getPosition().y, ID_SOUND_BARB_SQUISH);
	}
	else
	{
		destroyFXDroid(psDel, impactTime);
		pos.x = psDel->getPosition().x;
		pos.z = psDel->getPosition().y;
		pos.y = psDel->getPosition().z;
		if (psDel->type == DROID_SUPERTRANSPORTER)
		{
			addEffect(&pos, EFFECT_EXPLOSION, EXPLOSION_TYPE_LARGE, false, nullptr, 0, impactTime);
		}
		else
		{
			addEffect(&pos, EFFECT_DESTRUCTION, DESTRUCTION_TYPE_DROID, false, nullptr, 0, impactTime);
		}
		audio_PlayStaticTrack(psDel->getPosition().x, psDel->getPosition().y, ID_SOUND_EXPLOSION);
	}
}

bool destroyDroid(Droid* psDel, unsigned impactTime)
{
	ASSERT(gameTime - deltaGameTime <= impactTime, "Expected %u <= %u, gameTime = %u, bad impactTime",
	       gameTime - deltaGameTime, impactTime, gameTime);

	if (psDel->lastHitWeapon == WSC_LAS_SAT) // darken tile if lassat.
	{
		UDWORD width, breadth, mapX, mapY;
		Tile* psTile;

		mapX = map_coord(psDel->getPosition().x);
		mapY = map_coord(psDel->getPosition().y);
		for (width = mapX - 1; width <= mapX + 1; width++)
		{
			for (breadth = mapY - 1; breadth <= mapY + 1; breadth++)
			{
				psTile = mapTile(width, breadth);
				if (TEST_TILE_VISIBLE_TO_SELECTEDPLAYER(psTile))
				{
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
	if (isDead(psDroid))
	{
		// droid has already been killed, quit
		return false;
	}

	// leave the current group if any - not if its a Transporter droid
	if (!isTransporter(psDroid) && psDroid->group)
	{
		psDroid->group->remove(psDroid);
		psDroid->group = nullptr;
	}

	// reset the baseStruct
	setDroidBase(psDroid, nullptr);

	removeDroid(psDroid, pList);

	if (psDroid->player == selectedPlayer)
	{
		intRefreshScreen();
	}

	return true;
}

void _syncDebugDroid(const char* function, Droid const* psDroid, char ch)
{
	if (psDroid->type != OBJ_DROID)
	{
		ASSERT(false, "%c Broken psDroid->type %u!", ch, psDroid->type);
		syncDebug("Broken psDroid->type %u!", psDroid->type);
	}
	int list[] =
	{
          ch,

          (int)psDroid->id,

          psDroid->player,
          psDroid->getPosition().x, psDroid->getPosition().y, psDroid->getPosition().z,
          psDroid->rot.direction, psDroid->rot.pitch, psDroid->rot.roll,
          (int)psDroid->order.type, psDroid->order.pos.x, psDroid->order.pos.y, psDroid->listSize,
          (int)psDroid->action,
          (int)psDroid->secondary_order,
          (int)psDroid->body,
          (int)psDroid->movement.status,
          psDroid->movement.speed, psDroid->movement.moveDir,
          psDroid->movement.pathIndex, (int)psDroid->movement.path.size(),
          psDroid->movement.origin.x, psDroid->movement.origin.y, psDroid->movement.target.x, psDroid->movement.target.y,
          psDroid->movement.destination.x, psDroid->movement.destination.y,
          psDroid->movement.bumpDir, (int)psDroid->movement.bumpTime, (int)psDroid->movement.lastBump,
          (int)psDroid->movement.pauseTime, psDroid->movement.bumpPos.x, psDroid->movement.bumpPos.y,
          (int)psDroid->movement.shuffleStart,
          (int)psDroid->experience,
	};
	_syncDebugIntList(
		function,
		"%c droid%d = p%d;pos(%d,%d,%d),rot(%d,%d,%d),order%d(%d,%d)^%d,action%d,secondaryOrder%X,body%d,sMove(status%d,speed%d,moveDir%d,path%d/%d,src(%d,%d),target(%d,%d),destination(%d,%d),bump(%d,%d,%d,%d,(%d,%d),%d)),exp%u",
		list, ARRAY_SIZE(list));
}

/* The main update routine for all droids */
void droidUpdate(Droid* psDroid)
{
	Vector3i dv;
	UDWORD percentDamage, emissionInterval;
	SimpleObject* psBeingTargetted = nullptr;
	unsigned i;

	CHECK_DROID(psDroid);

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

	syncDebugDroid(psDroid, '<');

	if (psDroid->flags.test(OBJECT_FLAG_DIRTY))
	{
		visTilesUpdate(psDroid);
		droidBodyUpgrade(psDroid);
		psDroid->flags.set(OBJECT_FLAG_DIRTY, false);
	}

	// Save old droid position, update time.
	psDroid->previous_location = getSpacetime(psDroid);
	psDroid->time = gameTime;
	for (i = 0; i < MAX(1, psDroid->numWeaps); ++i)
	{
		psDroid->asWeaps[i].previous_rotation = psDroid->asWeaps[i].rotation;
	}

	if (psDroid->animationEvent != ANIM_EVENT_NONE)
	{
		iIMDShape* imd = psDroid->sDisplay.imd->objanimpie[psDroid->animationEvent];
		if (imd && imd->objanimcycles > 0 && gameTime > psDroid->timeAnimationStarted + imd->objanimtime * imd->
			objanimcycles)
		{
			// Done animating (animation is defined by body - other components should follow suit)
			if (psDroid->animationEvent == ANIM_EVENT_DYING)
			{
				debug(LOG_DEATH, "%s (%d) died to burn anim (died=%d)", objInfo(psDroid), (int)psDroid->id,
				      (int)psDroid->died);
				destroyDroid(psDroid, gameTime);
				return;
			}
			psDroid->animationEvent = ANIM_EVENT_NONE;
		}
	}
	else if (psDroid->animationEvent == ANIM_EVENT_DYING)
	{
		return; // rest below is irrelevant if dead
	}

	// ai update droid
	aiUpdateDroid(psDroid);

	// Update the droids order.
	orderUpdateDroid(psDroid);

	// update the action of the droid
	actionUpdateDroid(psDroid);

	syncDebugDroid(psDroid, 'M');

	// update the move system
	moveUpdateDroid(psDroid);

	/* Only add smoke if they're visible */
	if (psDroid->visibleForLocalDisplay() && psDroid->type != DROID_PERSON)
	{
		// need to clip this value to prevent overflow condition
		percentDamage = 100 - clip<UDWORD>(PERCENT(psDroid->body, psDroid->original_hp), 0, 100);

		// Is there any damage?
		if (percentDamage >= 25)
		{
			if (percentDamage >= 100)
			{
				percentDamage = 99;
			}

			emissionInterval = CALC_DROID_SMOKE_INTERVAL(percentDamage);

			uint32_t effectTime = std::max(gameTime - deltaGameTime + 1, psDroid->lastEmission + emissionInterval);
			if (gameTime >= effectTime)
			{
				dv.x = psDroid->getPosition().x + DROID_DAMAGE_SPREAD;
				dv.z = psDroid->getPosition().y + DROID_DAMAGE_SPREAD;
				dv.y = psDroid->getPosition().z;

				dv.y += (psDroid->sDisplay.imd->max.y * 2);
				addEffect(&dv, EFFECT_SMOKE, SMOKE_TYPE_DRIFTING_SMALL, false, nullptr, 0, effectTime);
				psDroid->lastEmission = effectTime;
			}
		}
	}

	// -----------------
	/* Are we a sensor droid or a command droid? Show where we target for selectedPlayer. */
	if (psDroid->player == selectedPlayer && (psDroid->type == DROID_SENSOR || psDroid->type ==
                                                                             DROID_COMMAND))
	{
		/* If we're attacking or sensing (observing), then... */
		if ((psBeingTargetted = orderStateObj(psDroid, DORDER_ATTACK))
			|| (psBeingTargetted = orderStateObj(psDroid, DORDER_OBSERVE)))
		{
			psBeingTargetted->flags.set(OBJECT_FLAG_TARGETED, true);
		}
		else if (secondaryGetState(psDroid, DSO_HALTTYPE) != DSS_HALT_PURSUE &&
             actionTarget[0] != nullptr &&
             validTarget(psDroid, actionTarget[0], 0) &&
             (psDroid->action == DACTION_ATTACK ||
				psDroid->action == DACTION_OBSERVE ||
				orderState(psDroid, DORDER_HOLD)))
		{
			psBeingTargetted = actionTarget[0];
			psBeingTargetted->flags.set(OBJECT_FLAG_TARGETED, true);
		}
	}
	// ------------------------
	// if we are a repair turret, then manage incoming damaged droids, (just like repair facility)
	// unlike a repair facility
	// 	- we don't really need to move droids to us, we can come ourselves
	//	- we don't steal work from other repair turrets/ repair facilities
	Droid* psOther;
	if (psDroid->type == DROID_REPAIR || psDroid->type == DROID_CYBORG_REPAIR)
	{
		for (psOther = apsDroidLists[psDroid->player]; psOther; psOther = psOther->psNext)
		{
			// unlike repair facility, no droid  can have DORDER_RTR_SPECIFIED with another droid as target, so skip that check
			if (psOther->order.type == DORDER_RTR &&
				psOther->order.rtrType == RTR_TYPE_DROID &&
				psOther->action != DACTION_WAITFORREPAIR &&
				psOther->action != DACTION_MOVETOREPAIRPOINT &&
				psOther->action != DACTION_WAITDURINGREPAIR)
			{
				if (psOther->body >= psOther->original_hp)
				{
					// set droid points to max
					psOther->body = psOther->original_hp;
					// if completely repaired reset order
					secondarySetState(psOther, DSO_RETURN_TO_LOC, DSS_NONE);

					if (hasCommander(psOther))
					{
						// return a droid to it's command group
						Droid* psCommander = psOther->group->psCommander;
						orderDroidObj(psOther, DORDER_GUARD, psCommander, ModeImmediate);
					}
					continue;
				}
			}

			else if (psOther->order.rtrType == RTR_TYPE_DROID
				//is being, or waiting for repairs..
				&& (psOther->action == DACTION_WAITFORREPAIR || psOther->action == DACTION_WAITDURINGREPAIR)
				// don't steal work from others
				&& psOther->order.psObj == psDroid)
			{
				if (!actionReachedDroid(psDroid, psOther))
				{
					actionDroid(psOther, DACTION_MOVE, psDroid, psDroid->getPosition().x, psDroid->getPosition().y);
				}
			}
		}
	}
	// ------------------------
	// See if we can and need to self repair.
	if (!isVtolDroid(psDroid) && psDroid->body < psDroid->original_hp && psDroid->asBits[COMP_REPAIRUNIT] != 0 &&
			selfRepairEnabled(psDroid->player))
	{
		droidUpdateDroidSelfRepair(psDroid);
	}


	/* Update the fire damage data */
	if (psDroid->periodicalDamageStart != 0 && psDroid->periodicalDamageStart != gameTime - deltaGameTime)
	// -deltaGameTime, since projectiles are updated after droids.
	{
		// The periodicalDamageStart has been set, but is not from the previous tick, so we must be out of the fire.
		psDroid->periodicalDamage = 0; // Reset periodical damage done this tick.
		if (psDroid->periodicalDamageStart + BURN_TIME < gameTime)
		{
			// Finished periodical damaging.
			psDroid->periodicalDamageStart = 0;
		}
		else
		{
			// do hardcoded burn damage (this damage automatically applied after periodical damage finished)
			droidDamage(psDroid, BURN_DAMAGE, WC_HEAT, WSC_FLAME, gameTime - deltaGameTime / 2 + 1, true,
			            BURN_MIN_DAMAGE);
		}
	}

	// At this point, the droid may be dead due to periodical damage or hardcoded burn damage.
	if (isDead(psDroid))
	{
		return;
	}

	calcDroidIllumination(psDroid);

	// Check the resistance level of the droid
	if ((psDroid->id + gameTime) / 833 != (psDroid->id + gameTime - deltaGameTime) / 833)
	{
		// Zero resistance means not currently been attacked - ignore these
		if (psDroid->resistance_to_electric && psDroid->resistance_to_electric < droidResistance(psDroid))
		{
			// Increase over time if low
			psDroid->resistance_to_electric++;
		}
	}

	syncDebugDroid(psDroid, '>');
}

/* See if a droid is next to a structure */
static bool droidNextToStruct(Droid* psDroid, Structure* psStruct)
{
	auto pos = map_coord(psDroid->getPosition());
	int minX = std::max(pos.x - 1, 0);
	int minY = std::max(pos.y - 1, 0);
	int maxX = std::min(pos.x + 1, mapWidth);
	int maxY = std::min(pos.y + 1, mapHeight);
	for (int y = minY; y <= maxY; ++y)
	{
		for (int x = minX; x <= maxX; ++x)
		{
			if (TileHasStructure(mapTile(x, y)) &&
				getTileStructure(x, y) == psStruct)
			{
				return true;
			}
		}
	}

	return false;
}

static bool droidCheckBuildStillInProgress(void* psObj)
{
	if (psObj == nullptr)
	{
		return false;
	}

	auto psDroid = (Droid*)psObj;

	return !psDroid->died && psDroid->action == DACTION_BUILD;
}

static bool droidBuildStartAudioCallback(void* psObj)
{
	auto psDroid = (Droid*)psObj;

	if (psDroid != nullptr && psDroid->visibleForLocalDisplay())
	{
		audio_PlayObjDynamicTrack(psDroid, ID_SOUND_CONSTRUCTION_LOOP, droidCheckBuildStillInProgress);
	}

	return true;
}


/* Set up a droid to build a structure - returns true if successful */
DroidStartBuild droidStartBuild(Droid* psDroid)
{
	Structure* psStruct = nullptr;
	ASSERT_OR_RETURN(DroidStartBuildFailed, psDroid != nullptr, "Bad Droid");

	/* See if we are starting a new structure */
	if (psDroid->order.psObj == nullptr &&
		(psDroid->order.type == DORDER_BUILD ||
			psDroid->order.type == DORDER_LINEBUILD))
	{
		StructureStats* psStructStat = psDroid->order.psStats;

		auto ia = (ItemAvailability)apStructTypeLists[psDroid->getPlayer()][psStructStat - asStructureStats];
		if (ia != AVAILABLE && ia != REDUNDANT)
		{
			ASSERT(false, "Cannot build \"%s\" for player %d.", psStructStat->name.toUtf8().c_str(), psDroid->player);
			cancelBuild(psDroid);
			objTrace(psDroid->id, "DroidStartBuildFailed: not researched");
			return DroidStartBuildFailed;
		}

		//need to check structLimits have not been exceeded
		if (psStructStat->curCount[psDroid->player] >= psStructStat->upgraded_stats[psDroid->player].limit)
		{
			cancelBuild(psDroid);
			objTrace(psDroid->id, "DroidStartBuildFailed: structure limits");
			return DroidStartBuildFailed;
		}
		// Can't build on burning oil derricks.
		if (psStructStat->type == REF_RESOURCE_EXTRACTOR && fireOnLocation(psDroid->order.pos.x, psDroid->order.pos.y))
		{
			// Don't cancel build, since we can wait for it to stop burning.
			objTrace(psDroid->id, "DroidStartBuildPending: burning");
			return DroidStartBuildPending;
		}
		//ok to build
		psStruct = buildStructureDir(psStructStat, psDroid->order.pos.x, psDroid->order.pos.y, psDroid->order.direction,
		                             psDroid->player, false);
		if (!psStruct)
		{
			cancelBuild(psDroid);
			objTrace(psDroid->id, "DroidStartBuildFailed: buildStructureDir failed");
			return DroidStartBuildFailed;
		}
		psStruct->body = (psStruct->body + 9) / 10; // Structures start at 10% health. Round up.
	}
	else
	{
		/* Check the structure is still there to build (joining a partially built struct) */
		psStruct = castStructure(psDroid->order.psObj);
		if (psStruct == nullptr)
		{
			psStruct = castStructure(worldTile(psDroid->actionPos)->psObject);
		}
		if (psStruct && !droidNextToStruct(psDroid, psStruct))
		{
			/* Nope - stop building */
			debug(LOG_NEVER, "not next to structure");
			objTrace(psDroid->id, "DroidStartBuildSuccess: not next to structure");
		}
	}

	//check structure not already built, and we still 'own' it
	if (psStruct)
	{
		if (psStruct->status != SS_BUILT && aiCheckAlliances(psStruct->player, psDroid->player))
		{
			psDroid->time_action_started = gameTime;
			psDroid->action_points_done = 0;
			setDroidTarget(psDroid, psStruct);
			setDroidActionTarget(psDroid, psStruct, 0);
			objTrace(psDroid->id, "DroidStartBuild: set target");
		}

		if (psStruct->visibleForLocalDisplay())
		{
			audio_PlayObjStaticTrackCallback(psDroid, ID_SOUND_CONSTRUCTION_START, droidBuildStartAudioCallback);
		}
	}

	objTrace(psDroid->id, "DroidStartBuildSuccess");
	return DroidStartBuildSuccess;
}

static void droidAddWeldSound(Vector3i iVecEffect)
{
	int iAudioID = ID_SOUND_CONSTRUCTION_1 + (rand() % 4);

	audio_PlayStaticTrack(iVecEffect.x, iVecEffect.z, iAudioID);
}

static void addConstructorEffect(Structure* psStruct)
{
	if ((ONEINTEN) && (psStruct->visibleForLocalDisplay()))
	{
		/* This needs fixing - it's an arse effect! */
		const Vector2i size = psStruct->size() * TILE_UNITS / 4;
		Vector3i temp;
		temp.x = psStruct->getPosition().x + ((rand() % (2 * size.x)) - size.x);
		temp.y = map_TileHeight(map_coord(psStruct->getPosition().x), map_coord(psStruct->getPosition().y)) + (psStruct->sDisplay.imd->max.y
			/ 6);
		temp.z = psStruct->getPosition().y + ((rand() % (2 * size.y)) - size.y);
		if (rand() % 2)
		{
			droidAddWeldSound(temp);
		}
	}
}

/* Update a construction droid while it is building
   returns true while building continues */
bool droidUpdateBuild(Droid* psDroid)
{
	ASSERT_OR_RETURN(false, psDroid->action == DACTION_BUILD, "%s (order %s) has wrong action for construction: %s",
	                 droidGetName(psDroid), getDroidOrderName(psDroid->order.type),
	                 getDroidActionName(psDroid->action));

	Structure* psStruct = castStructure(psDroid->order.psObj);
	if (psStruct == nullptr)
	{
		// Target missing, stop trying to build it.
		psDroid->action = DACTION_NONE;
		return false;
	}

	ASSERT_OR_RETURN(false, psStruct->type == OBJ_STRUCTURE, "target is not a structure");
	ASSERT_OR_RETURN(false, psDroid->asBits[COMP_CONSTRUCT] < numConstructStats, "Invalid construct pointer for unit");

	// First check the structure hasn't been completed by another droid
	if (psStruct->status == SS_BUILT)
	{
		// Check if line order build is completed, or we are not carrying out a line order build
		if (psDroid->order.type != DORDER_LINEBUILD ||
			map_coord(psDroid->order.pos) == map_coord(psDroid->order.pos2))
		{
			cancelBuild(psDroid);
		}
		else
		{
			psDroid->action = DACTION_NONE; // make us continue line build
			setDroidTarget(psDroid, nullptr);
			setDroidActionTarget(psDroid, nullptr, 0);
		}
		return false;
	}

	// make sure we still 'own' the building in question
	if (!aiCheckAlliances(psStruct->player, psDroid->player))
	{
		cancelBuild(psDroid); // stop what you are doing fool it isn't ours anymore!
		return false;
	}

	unsigned constructPoints = constructorPoints(asConstructStats + psDroid->
	                                             asBits[COMP_CONSTRUCT], psDroid->player);

	unsigned pointsToAdd = constructPoints * (gameTime - psDroid->time_action_started) /
                         GAME_TICKS_PER_SEC;

	structureBuild(psStruct, psDroid, pointsToAdd - psDroid->action_points_done, constructPoints);

	//store the amount just added
	psDroid->action_points_done = pointsToAdd;

	addConstructorEffect(psStruct);

	return true;
}

bool droidUpdateDemolishing(Droid* psDroid)
{
	ASSERT_OR_RETURN(false, psDroid->action == DACTION_DEMOLISH, "unit is not demolishing");
	auto psStruct = (Structure*)psDroid->order.psObj;
	ASSERT_OR_RETURN(false, psStruct->type == OBJ_STRUCTURE, "target is not a structure");

	int constructRate = 5 * constructorPoints(asConstructStats + psDroid->asBits[COMP_CONSTRUCT], psDroid->player);
	int pointsToAdd = gameTimeAdjustedAverage(constructRate);

	structureDemolish(psStruct, psDroid, pointsToAdd);

	addConstructorEffect(psStruct);

	return true;
}


//void droidStartAction(DROID *psDroid)
//{
//	psDroid->actionStarted = gameTime;
//	psDroid->actionPoints  = 0;
//}

/*continue restoring a structure*/
bool droidUpdateRestore(Droid* psDroid)
{
	ASSERT_OR_RETURN(false, psDroid->action == ACTION::RESTORE, "Unit is not restoring");
	auto psStruct = (Structure*)psDroid->order.psObj;
	ASSERT_OR_RETURN(false, psStruct->type == OBJ_STRUCTURE, "Target is not a structure");
	ASSERT_OR_RETURN(false, psDroid->asWeaps[0].nStat > 0, "Droid doesn't have any weapons");

	unsigned compIndex = psDroid->asWeaps[0].nStat;
	ASSERT_OR_RETURN(false, compIndex < numWeaponStats, "Invalid range referenced for numWeaponStats, %u > %u",
	                 compIndex, numWeaponStats);
	auto psStats = &asWeaponStats[compIndex];

	ASSERT_OR_RETURN(false, psStats->weaponSubClass == WSC_ELECTRONIC, "unit's weapon is not EW");

	unsigned restorePoints = calcDamage(weaponDamage(psStats, psDroid->player),
	                                    psStats->weaponEffect, (SimpleObject*)psStruct);

	unsigned pointsToAdd = restorePoints * (gameTime - psDroid->time_action_started) /
                         GAME_TICKS_PER_SEC;

	psStruct->resistance = (SWORD)(psStruct->resistance + (pointsToAdd - psDroid->action_points_done));

	//store the amount just added
	psDroid->action_points_done = pointsToAdd;

	/* check if structure is restored */
	if (psStruct->resistance < (SDWORD)structureResistance(psStruct->
	                                                       pStructureType, psStruct->player))
	{
		return true;
	}
	else
	{
		addConsoleMessage(_("Structure Restored"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
		psStruct->resistance = (UWORD)structureResistance(psStruct->pStructureType,
		                                                  psStruct->player);
		return false;
	}
}

// Declared in weapondef.h.
int getRecoil(Weapon const& weapon)
{
	if (weapon.nStat != 0)
	{
		// We have a weapon.
		if (graphicsTime >= weapon.time_last_fired && graphicsTime < weapon.time_last_fired + DEFAULT_RECOIL_TIME)
		{
			int recoilTime = graphicsTime - weapon.time_last_fired;
			int recoilAmount = DEFAULT_RECOIL_TIME / 2 - abs(recoilTime - DEFAULT_RECOIL_TIME / 2);
			int maxRecoil = asWeaponStats[weapon.nStat].recoilValue; // Max recoil is 1/10 of this value.
			return maxRecoil * recoilAmount / (DEFAULT_RECOIL_TIME / 2 * 10);
		}
		// Recoil effect is over.
	}
	return 0;
}


bool droidUpdateRepair(Droid* psDroid)
{
	CHECK_DROID(psDroid);

	ASSERT_OR_RETURN(false, psDroid->action == DACTION_REPAIR, "unit does not have repair order");
	Structure* psStruct = (Structure*)actionTarget[0];

	ASSERT_OR_RETURN(false, psStruct->type == OBJ_STRUCTURE, "target is not a structure");
	int iRepairRate = constructorPoints(asConstructStats + psDroid->asBits[COMP_CONSTRUCT], psDroid->player);

	/* add points to structure */
	structureRepair(psStruct, psDroid, iRepairRate);

	/* if not finished repair return true else complete repair and return false */
	if (psStruct->body < structureBody(psStruct))
	{
		return true;
	}
	else
	{
		objTrace(psDroid->id, "Repaired of %s all done with %u", objInfo(psStruct), iRepairRate);
		return false;
	}
}

/*Updates a Repair Droid working on a damaged droid*/
static bool droidUpdateDroidRepairBase(Droid* psRepairDroid, Droid* psDroidToRepair)
{
	CHECK_DROID(psRepairDroid);

	int iRepairRateNumerator = repairPoints(asRepairStats + psRepairDroid->asBits[COMP_REPAIRUNIT],
	                                        psRepairDroid->player);
	int iRepairRateDenominator = 1;

	//if self repair then add repair points depending on the time delay for the stat
	if (psRepairDroid == psDroidToRepair)
	{
		iRepairRateNumerator *= GAME_TICKS_PER_SEC;
		iRepairRateDenominator *= (asRepairStats + psRepairDroid->asBits[COMP_REPAIRUNIT])->time;
	}

	int iPointsToAdd = gameTimeAdjustedAverage(iRepairRateNumerator, iRepairRateDenominator);

	psDroidToRepair->body = clip<UDWORD>(psDroidToRepair->body + iPointsToAdd, 0, psDroidToRepair->original_hp);

	/* add plasma repair effect whilst being repaired */
	if ((ONEINFIVE) && (psDroidToRepair->visibleForLocalDisplay()))
	{
		Vector3i iVecEffect = (psDroidToRepair->getPosition() + Vector3i(DROID_REPAIR_SPREAD, DROID_REPAIR_SPREAD, rand() % 8)).
			xzy();
		effectGiveAuxVar(90 + rand() % 20);
		addEffect(&iVecEffect, EFFECT_EXPLOSION, EXPLOSION_TYPE_LASER, false, nullptr, 0,
		          gameTime - deltaGameTime + 1 + rand() % deltaGameTime);
		droidAddWeldSound(iVecEffect);
	}

	CHECK_DROID(psRepairDroid);
	/* if not finished repair return true else complete repair and return false */
	return psDroidToRepair->body < psDroidToRepair->original_hp;
}

bool droidUpdateDroidRepair(Droid* psRepairDroid)
{
	ASSERT_OR_RETURN(false, psRepairDroid->action == DACTION_DROIDREPAIR, "Unit does not have unit repair order");
	ASSERT_OR_RETURN(false, psRepairDroid->asBits[COMP_REPAIRUNIT] != 0, "Unit does not have a repair turret");

	Droid* psDroidToRepair = (Droid*)psRepairDroid->action_target[0];
	ASSERT_OR_RETURN(false, psDroidToRepair->type == OBJ_DROID, "Target is not a unit");
	bool needMoreRepair = droidUpdateDroidRepairBase(psRepairDroid, psDroidToRepair);
	if (needMoreRepair && psDroidToRepair->order.type == DORDER_RTR && psDroidToRepair->order.rtrType == RTR_TYPE_DROID
		&& psDroidToRepair->action == DACTION_NONE)
	{
		psDroidToRepair->action = DACTION_WAITDURINGREPAIR;
	}
	if (!needMoreRepair && psDroidToRepair->order.type == DORDER_RTR && psDroidToRepair->order.rtrType ==
		RTR_TYPE_DROID)
	{
		// if psDroidToRepair has a commander, commander will call him back anyway
		// if no commanders, just DORDER_GUARD the repair turret
		orderDroidObj(psDroidToRepair, DORDER_GUARD, psRepairDroid, ModeImmediate);
		secondarySetState(psDroidToRepair, DSO_RETURN_TO_LOC, DSS_NONE);
		psDroidToRepair->order.psObj = nullptr;
	}
	return needMoreRepair;
}

static void droidUpdateDroidSelfRepair(Droid* psRepairDroid)
{
	droidUpdateDroidRepairBase(psRepairDroid, psRepairDroid);
}

bool is_idf(const Droid& droid)
{
  using enum DROID_TYPE;
  return (droid.get_type() != WEAPON || !isCyborg(droid)) &&
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
	DROID_TYPE type = DROID_DEFAULT;

	if (psTemplate->type == DROID_PERSON ||
      psTemplate->type == DROID_CYBORG ||
      psTemplate->type == DROID_CYBORG_SUPER ||
      psTemplate->type == DROID_CYBORG_CONSTRUCT ||
      psTemplate->type == DROID_CYBORG_REPAIR ||
      psTemplate->type == DROID_TRANSPORTER ||
      psTemplate->type == DROID_SUPERTRANSPORTER)
	{
		type = psTemplate->type;
	}
	else if (psTemplate->asParts[COMP_BRAIN] != 0)
	{
		type = DROID_COMMAND;
	}
	else if ((asSensorStats + psTemplate->asParts[COMP_SENSOR])->location == LOC_TURRET)
	{
		type = DROID_SENSOR;
	}
	else if ((asECMStats + psTemplate->asParts[COMP_ECM])->location == LOC_TURRET)
	{
		type = DROID_ECM;
	}
	else if (psTemplate->asParts[COMP_CONSTRUCT] != 0)
	{
		type = DROID_CONSTRUCT;
	}
	else if ((asRepairStats + psTemplate->asParts[COMP_REPAIRUNIT])->location == LOC_TURRET)
	{
		type = DROID_REPAIR;
	}
	else if (psTemplate->asWeaps[0] != 0)
	{
		type = DROID_WEAPON;
	}
	/* with more than weapon is still a DROID_WEAPON */
	else if (psTemplate->weaponCount > 1)
	{
		type = DROID_WEAPON;
	}

	return type;
}

template <typename F, typename G>
static unsigned calcSum(const uint8_t (&asParts)[DROID_MAXCOMP], int numWeaps, const uint32_t (&asWeaps)[MAX_WEAPONS],
                        F func, G propulsionFunc)
{
	unsigned sum =
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
	ASSERT_OR_RETURN(retVal, player >= 0 && player < MAX_PLAYERS, "Invalid player: %" PRIu32 "", player);

template <typename F, typename G>
static unsigned calcUpgradeSum(const uint8_t (&asParts)[DROID_MAXCOMP], int numWeaps,
                               const uint32_t (&asWeaps)[MAX_WEAPONS], int player, F func, G propulsionFunc)
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
		if (asWeaps[i] > 0)
		{
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
		this->numWeaps = std::remove_if(this->asWeaps, this->asWeaps + numWeaps, [](uint32_t stat)
		{
			return stat == 0;
		}) - this->asWeaps;
	}

	unsigned numWeaps;
	uint32_t asWeaps[MAX_WEAPONS];
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
	return calcSum(psDroid->asBits, f.numWeaps, f.asWeaps, func, propulsionFunc);
}

template <typename F, typename G>
static unsigned calcUpgradeSum(const DroidTemplate* psTemplate, int player, F func, G propulsionFunc)
{
	return calcUpgradeSum(psTemplate->asParts, psTemplate->weaponCount, psTemplate->asWeaps, player, func, propulsionFunc);
}

template <typename F, typename G>
static unsigned calcUpgradeSum(const Droid* psDroid, int player, F func, G propulsionFunc)
{
	FilterDroidWeaps f = {psDroid->numWeaps, psDroid->asWeaps};
	return calcUpgradeSum(psDroid->asBits, f.numWeaps, f.asWeaps, player, func, propulsionFunc);
}

/* Calculate the weight of a droid from it's template */
UDWORD calcDroidWeight(const DroidTemplate* psTemplate)
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
static uint32_t calcBody(T* obj, int player)
{
	int hitpoints = calcUpgradeSum(obj, player, [](ComponentStats::Upgradeable const& upgrade)
	                               {
		                               return upgrade.hit_points;
	                               }, [](BodyStats::Upgradeable const& bodyUpgrade,
                                       PropulsionStats::Upgradeable const& propUpgrade)
	                               {
		                               // propulsion hitpoints can be a percentage of the body's hitpoints
		                               return bodyUpgrade.hit_points * (100 + propUpgrade.hitpointPctOfBody) / 100 +
                                          propUpgrade.hit_points;
	                               });

	int hitpointPct = calcUpgradeSum(obj, player, [](ComponentStats::Upgradeable const& upgrade)
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
UDWORD calcTemplateBody(const DroidTemplate* psTemplate, UBYTE player)
{
	if (psTemplate == nullptr)
	{
		ASSERT(false, "null template");
		return 0;
	}

	return calcBody(psTemplate, player);
}

// Calculate the base body points of a droid with upgrades
static UDWORD calcDroidBaseBody(Droid* psDroid)
{
	return calcBody(psDroid, psDroid->player);
}


/* Calculate the base speed of a droid from it's template */
UDWORD calcDroidBaseSpeed(const DroidTemplate* psTemplate, UDWORD weight, UBYTE player)
{
	unsigned speed = asPropulsionTypes[asPropulsionStats[psTemplate->asParts[COMP_PROPULSION]].propulsionType].
		powerRatioMult *
		bodyPower(&asBodyStats[psTemplate->asParts[COMP_BODY]], player) / MAX(1, weight);

	// reduce the speed of medium/heavy VTOLs
	if (asPropulsionStats[psTemplate->asParts[COMP_PROPULSION]].propulsionType == PROPULSION_TYPE_LIFT)
	{
		if (asBodyStats[psTemplate->asParts[COMP_BODY]].size == SIZE_HEAVY)
		{
			speed /= 4;
		}
		else if (asBodyStats[psTemplate->asParts[COMP_BODY]].size == SIZE_MEDIUM)
		{
			speed = speed * 3 / 4;
		}
	}

	// applies the engine output bonus if output > weight
	if (asBodyStats[psTemplate->asParts[COMP_BODY]].base.power > weight)
	{
		speed = speed * 3 / 2;
	}

	return speed;
}


/* Calculate the speed of a droid over a terrain */
UDWORD calcDroidSpeed(UDWORD baseSpeed, UDWORD terrainType, UDWORD propIndex, UDWORD level)
{
	PropulsionStats const& propulsion = asPropulsionStats[propIndex];

	// Factor in terrain
	unsigned speed = baseSpeed * getSpeedFactor(terrainType, propulsion.propulsionType) / 100;

	// Need to ensure doesn't go over the max speed possible for this propulsion
	speed = std::min(speed, propulsion.maxSpeed);

	// Factor in experience
	speed *= 100 + EXP_SPEED_BONUS * level;
	speed /= 100;

	return speed;
}

template <typename T>
static uint32_t calcBuild(T* obj)
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
UDWORD calcTemplateBuild(const DroidTemplate* psTemplate)
{
	return calcBuild(psTemplate);
}

UDWORD calcDroidPoints(Droid* psDroid)
{
	return calcBuild(psDroid);
}

template <typename T>
static uint32_t calcPower(const T* obj)
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
UDWORD calcTemplatePower(const DroidTemplate* psTemplate)
{
	return calcPower(psTemplate);
}


/* Calculate the power points required to build/maintain a droid */
UDWORD calcDroidPower(const Droid* psDroid)
{
	return calcPower(psDroid);
}

//Builds an instance of a Droid - the x/y passed in are in world coords.
Droid* reallyBuildDroid(const DroidTemplate* pTemplate, Position pos, UDWORD player, bool onMission, Rotation rot)
{
	// Don't use this assertion in single player, since droids can finish building while on an away mission
	ASSERT(!bMultiPlayer || worldOnMap(pos.x, pos.y), "the build locations are not on the map");

	ASSERT_OR_RETURN(nullptr, player < MAX_PLAYERS, "Invalid player: %" PRIu32 "", player);

	Droid* psDroid = new Droid(generateSynchronisedObjectId(), player);
	droidSetName(psDroid, getStatsName(pTemplate));

	// Set the droids type
	psDroid->type = droidTemplateType(pTemplate); // Is set again later to the same thing, in droidSetBits.
	psDroid->getPosition() = pos;
	psDroid->rot = rot;

	//don't worry if not on homebase cos not being drawn yet
	if (!onMission)
	{
		//set droid height
		psDroid->getPosition().z = map_Height(psDroid->getPosition().x, psDroid->getPosition().y);
	}

	if (isTransporter(psDroid) || psDroid->type == DROID_COMMAND)
	{
		Group* psGrp = grpCreate();
		psGrp->add(psDroid);
	}

	// find the highest stored experience
	// Unless game time is stopped, then we're hopefully loading a game and
	// don't want to use up recycled experience for the droids we just loaded.
	if (!gameTimeIsStopped() &&
      (psDroid->type != DROID_CONSTRUCT) &&
      (psDroid->type != DROID_CYBORG_CONSTRUCT) &&
      (psDroid->type != DROID_REPAIR) &&
      (psDroid->type != DROID_CYBORG_REPAIR) &&
      !isTransporter(psDroid) &&
		!recycled_experience[psDroid->player].empty())
	{
		psDroid->experience = recycled_experience[psDroid->player].top();
		recycled_experience[psDroid->player].pop();
	}
	else
	{
		psDroid->experience = 0;
	}
	psDroid->kills = 0;

	droidSetBits(pTemplate, psDroid);

	//calculate the droids total weight
	psDroid->weight = calcDroidWeight(pTemplate);

	// Initialise the movement stuff
	psDroid->base_speed = calcDroidBaseSpeed(pTemplate, psDroid->weight, (UBYTE)player);

	initDroidMovement(psDroid);

	//allocate 'easy-access' data!
	psDroid->body = calcDroidBaseBody(psDroid); // includes upgrades
	ASSERT(psDroid->body > 0, "Invalid number of hitpoints");
	psDroid->original_hp = psDroid->body;

	/* Set droid's initial illumination */
	psDroid->sDisplay.imd = BODY_IMD(psDroid, psDroid->player);

	//don't worry if not on homebase cos not being drawn yet
	if (!onMission)
	{
		/* People always stand upright */
		if (psDroid->type != DROID_PERSON)
		{
			updateDroidOrientation(psDroid);
		}
		visTilesUpdate(psDroid);
	}

	/* transporter-specific stuff */
	if (isTransporter(psDroid))
	{
		//add transporter launch button if selected player and not a reinforcable situation
		if (player == selectedPlayer && !missionCanReEnforce())
		{
			(void)intAddTransporterLaunch(psDroid);
		}

		//set droid height to be above the terrain
		psDroid->getPosition().z += TRANSPORTER_HOVER_HEIGHT;

		/* reset halt secondary order from guard to hold */
		secondarySetState(psDroid, DSO_HALTTYPE, DSS_HALT_HOLD);
	}

	if (player == selectedPlayer)
	{
		scoreUpdateVar(WD_UNITS_BUILT);
	}

	// Avoid droid appearing to jump or turn on spawn.
	psDroid->previous_location.pos = psDroid->getPosition();
	psDroid->previous_location.rot = psDroid->rot;

	debug(LOG_LIFE, "created droid for player %d, droid = %p, id=%d (%s): position: x(%d)y(%d)z(%d)", player,
        static_cast<void *>(psDroid), (int)psDroid->id, psDroid->name, psDroid->getPosition().x, psDroid->getPosition().y,
        psDroid->getPosition().z);

	return psDroid;
}

Droid* buildDroid(DroidTemplate* pTemplate, UDWORD x, UDWORD y, UDWORD player, bool onMission,
                  const INITIAL_DROID_ORDERS* initialOrders, Rotation rot)
{
	ASSERT_OR_RETURN(nullptr, player < MAX_PLAYERS, "invalid player?: %" PRIu32 "", player);
	// ajl. droid will be created, so inform others
	if (bMultiMessages)
	{
		// Only sends if it's ours, otherwise the owner should send the message.
		SendDroid(pTemplate, x, y, player, generateNewObjectId(), initialOrders);
		return nullptr;
	}
	else
	{
		return reallyBuildDroid(pTemplate, Position(x, y, 0), player, onMission, rot);
	}
}

//initialises the droid movement model
void initDroidMovement(Droid* psDroid)
{
	psDroid->movement.path.clear();
	psDroid->movement.pathIndex = 0;
}

// Set the asBits in a DROID structure given it's template.
void droidSetBits(const DroidTemplate* pTemplate, Droid* psDroid)
{
	psDroid->type = droidTemplateType(pTemplate);
	psDroid->numWeaps = pTemplate->weaponCount;
	psDroid->body = calcTemplateBody(pTemplate, psDroid->player);
	psDroid->original_hp = psDroid->body;
	psDroid->expected_damage_direct = 0; // Begin life optimistically.
	psDroid->expected_damage_indirect = 0; // Begin life optimistically.
	psDroid->time = gameTime - deltaGameTime + 1; // Start at beginning of tick.
	psDroid->previous_location.time = psDroid->time - 1; // -1 for interpolation.

	//create the droids weapons
	for (int inc = 0; inc < MAX_WEAPONS; inc++)
	{
		actionTarget[inc] = nullptr;
		psDroid->asWeaps[inc].time_last_fired = 0;
		psDroid->asWeaps[inc].shots_fired = 0;
		// no weapon (could be a construction droid for example)
		// this is also used to check if a droid has a weapon, so zero it
		psDroid->asWeaps[inc].nStat = 0;
		psDroid->asWeaps[inc].ammo = 0;
		psDroid->asWeaps[inc].rotation.direction = 0;
		psDroid->asWeaps[inc].rotation.pitch = 0;
		psDroid->asWeaps[inc].rotation.roll = 0;
		psDroid->asWeaps[inc].previous_rotation = psDroid->asWeaps[inc].rotation;
		psDroid->asWeaps[inc].origin = ORIGIN_UNKNOWN;
		if (inc < pTemplate->weaponCount)
		{
			psDroid->asWeaps[inc].nStat = pTemplate->asWeaps[inc];
			psDroid->asWeaps[inc].ammo = (asWeaponStats + psDroid->asWeaps[inc].nStat)->upgraded_stats[psDroid->player].
				numRounds;
		}
		psDroid->asWeaps[inc].ammo_used = 0;
	}
	memcpy(psDroid->asBits, pTemplate->asParts, sizeof(psDroid->asBits));

	switch (getPropulsionStats(psDroid)->propulsionType)
	// getPropulsionStats(psDroid) only defined after psDroid->asBits[COMP_PROPULSION] is set.
	{
	case PROPULSION_TYPE_LIFT:
		psDroid->blockedBits = AIR_BLOCKED;
		break;
	case PROPULSION_TYPE_HOVER:
		psDroid->blockedBits = FEATURE_BLOCKED;
		break;
	case PROPULSION_TYPE_PROPELLOR:
		psDroid->blockedBits = FEATURE_BLOCKED | LAND_BLOCKED;
		break;
	default:
		psDroid->blockedBits = FEATURE_BLOCKED | WATER_BLOCKED;
		break;
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
		if (psDroid->asWeaps[inc].nStat > 0)
		{
			psTemplate->weaponCount += 1;
			psTemplate->asWeaps[inc] = psDroid->asWeaps[inc].nStat;
		}
	}
	memcpy(psTemplate->asParts, psDroid->asBits, sizeof(psDroid->asBits));
}

/* Make all the droids for a certain player a member of a specific group */
void assignDroidsToGroup(UDWORD playerNumber, UDWORD groupNumber, bool clearGroup)
{
	Droid* psDroid;
	bool bAtLeastOne = false;
	FLAG_POSITION* psFlagPos;

	ASSERT_OR_RETURN(, playerNumber < MAX_PLAYERS, "Invalid player: %" PRIu32 "", playerNumber);

	if (groupNumber < UBYTE_MAX)
	{
		/* Run through all the droids */
		for (psDroid = apsDroidLists[playerNumber]; psDroid != nullptr; psDroid = psDroid->psNext)
		{
			/* Clear out the old ones */
			if (clearGroup && psDroid->group == groupNumber)
			{
				psDroid->group = UBYTE_MAX;
			}

			/* Only assign the currently selected ones */
			if (psDroid->selected)
			{
				/* Set them to the right group - they can only be a member of one group */
				psDroid->group = (UBYTE)groupNumber;
				bAtLeastOne = true;
			}
		}
	}
	if (bAtLeastOne)
	{
		//clear the Deliv Point if one
		ASSERT_OR_RETURN(, selectedPlayer < MAX_PLAYERS, "Unsupported selectedPlayer: %" PRIu32 "", selectedPlayer);
		for (psFlagPos = apsFlagPosLists[selectedPlayer]; psFlagPos;
		     psFlagPos = psFlagPos->psNext)
		{
			psFlagPos->selected = false;
		}
		groupConsoleInformOfCreation(groupNumber);
		secondarySetAverageGroupState(selectedPlayer, groupNumber);
	}
}


void removeDroidsFromGroup(UDWORD playerNumber)
{
	Droid* psDroid;
	unsigned removedCount = 0;

	ASSERT_OR_RETURN(, playerNumber < MAX_PLAYERS, "Invalid player: %" PRIu32 "", playerNumber);

	for (psDroid = apsDroidLists[playerNumber]; psDroid != nullptr; psDroid = psDroid->psNext)
	{
		if (psDroid->selected)
		{
			psDroid->group = UBYTE_MAX;
			removedCount++;
		}
	}
	if (removedCount)
	{
		groupConsoleInformOfRemoval();
	}
}

bool activateGroupAndMove(UDWORD playerNumber, UDWORD groupNumber)
{
	Droid *psDroid, *psCentreDroid = nullptr;
	bool selected = false;
	FLAG_POSITION* psFlagPos;

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
			if (getWarCamStatus())
			{
				camToggleStatus(); // messy - fix this
				processWarCam(); //odd, but necessary
				camToggleStatus(); // messy - FIXME
			}
			else
			{
				/* Centre display on him if warcam isn't active */
				setViewPos(map_coord(psCentreDroid->getPosition().x), map_coord(psCentreDroid->getPosition().y), true);
			}
		}
	}

	if (selected)
	{
		groupConsoleInformOfCentering(groupNumber);
	}

	return selected;
}

bool activateNoGroup(UDWORD playerNumber, const SELECTIONTYPE selectionType, const SELECTION_CLASS selectionClass,
                     const bool bOnScreen)
{
	Droid* psDroid;
	bool selected = false;
	FLAG_POSITION* psFlagPos;
	SELECTIONTYPE dselectionType = selectionType;
	SELECTION_CLASS dselectionClass = selectionClass;
	bool dbOnScreen = bOnScreen;

	ASSERT_OR_RETURN(false, playerNumber < MAX_PLAYERS, "Invalid player: %" PRIu32 "", playerNumber);

	selDroidSelection(selectedPlayer, dselectionClass, dselectionType, dbOnScreen);
	for (psDroid = apsDroidLists[playerNumber]; psDroid; psDroid = psDroid->psNext)
	{
		/* Wipe out the ones in the wrong group */
		if (psDroid->selected && psDroid->group != UBYTE_MAX)
		{
			DeSelectDroid(psDroid);
		}
	}
	if (selected)
	{
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

bool activateGroup(UDWORD playerNumber, UDWORD groupNumber)
{
	Droid* psDroid;
	bool selected = false;
	FLAG_POSITION* psFlagPos;

	ASSERT_OR_RETURN(false, playerNumber < MAX_PLAYERS, "Invalid player: %" PRIu32 "", playerNumber);

	if (groupNumber < UBYTE_MAX)
	{
		for (psDroid = apsDroidLists[playerNumber]; psDroid; psDroid = psDroid->psNext)
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
				selected = true;
			}
		}
	}

	if (selected)
	{
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

void groupConsoleInformOfSelection(UDWORD groupNumber)
{
	unsigned int num_selected = selNumSelected(selectedPlayer);

	CONPRINTF(ngettext("Group %u selected - %u Unit", "Group %u selected - %u Units", num_selected), groupNumber,
	          num_selected);
}

void groupConsoleInformOfCreation(UDWORD groupNumber)
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

void groupConsoleInformOfCentering(UDWORD groupNumber)
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
//UDWORD getDroidEffectiveLevel(const DROID *psDroid)
//{
//	UDWORD level = getDroidLevel(psDroid);
//	UDWORD cmdLevel = 0;
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

const char* getDroidLevelName(const Droid* psDroid)
{
	const CommanderStats* psStats = getBrainStats(psDroid);
	return PE_("rank", psStats->rankNames[getDroidLevel(psDroid)].c_str());
}

unsigned count_droids_for_level(unsigned player, unsigned level)
{
  const auto& droids = droid_lists[player];
  return std::count_if(droids.begin(), droids.end(),
                       [level](const auto& droid)
  {
      return droid.get_level() == level;
  });
}
//UDWORD	getNumDroidsForLevel(uint32_t player, UDWORD level)
//{
//	DROID	*psDroid;
//	UDWORD	count;
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
//bool noDroid(UDWORD x, UDWORD y)
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
//static bool oneDroidMax(UDWORD x, UDWORD y)
//{
//	UDWORD i;
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
bool zonedPAT(UDWORD x, UDWORD y)
{
	return sensiblePlace(x, y, PROPULSION_TYPE_WHEELED) && noDroid(x, y);
}

static bool canFitDroid(UDWORD x, UDWORD y)
{
	return sensiblePlace(x, y, PROPULSION_TYPE_WHEELED) && oneDroidMax(x, y);
}

/// find a tile for which the function will return true
bool pickATileGen(UDWORD* x, UDWORD* y, UBYTE numIterations,
                  bool (*function)(UDWORD x, UDWORD y))
{
	return pickATileGenThreat(x, y, numIterations, -1, -1, function);
}

bool pickATileGen(Vector2i* pos, unsigned numIterations, bool (*function)(UDWORD x, UDWORD y))
{
	UDWORD x = pos->x, y = pos->y;
	bool ret = pickATileGenThreat(&x, &y, numIterations, -1, -1, function);
	*pos = Vector2i(x, y);
	return ret;
}

static bool ThreatInRange(SDWORD player, SDWORD range, SDWORD rangeX, SDWORD rangeY, bool bVTOLs)
{
	UDWORD i, structType;
	Structure* psStruct;
	Droid* psDroid;

	const int tx = map_coord(rangeX);
	const int ty = map_coord(rangeY);

	for (i = 0; i < MAX_PLAYERS; i++)
	{
		if ((alliances[player][i] == ALLIANCE_FORMED) || (i == player))
		{
			continue;
		}

		//check structures
		for (psStruct = apsStructLists[i]; psStruct; psStruct = psStruct->psNext)
		{
			if (psStruct->visible[player] || psStruct->born == 2) // if can see it or started there
			{
				if (psStruct->status == SS_BUILT)
				{
					structType = psStruct->pStructureType->type;

					switch (structType) //dangerous to get near these structures
					{
					case REF_DEFENSE:
					case REF_CYBORG_FACTORY:
					case REF_FACTORY:
					case REF_VTOL_FACTORY:
					case REF_REARM_PAD:

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
			if (psDroid->visible[player]) //can see this droid?
			{
				if (!objHasWeapon((SimpleObject*)psDroid))
				{
					continue;
				}

				//if VTOLs are excluded, skip them
				if (!bVTOLs && ((asPropulsionStats[psDroid->asBits[COMP_PROPULSION]].propulsionType ==
					PROPULSION_TYPE_LIFT) || isTransporter(psDroid)))
				{
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
bool pickATileGenThreat(UDWORD* x, UDWORD* y, UBYTE numIterations, SDWORD threatRange,
                        SDWORD player, bool (*function)(UDWORD x, UDWORD y))
{
	SDWORD i, j;
	SDWORD startX, endX, startY, endY;
	UDWORD passes;
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
						&& fpathCheck(origin, newPos, PROPULSION_TYPE_WHEELED)
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
PICKTILE pickHalfATile(UDWORD* x, UDWORD* y, UBYTE numIterations)
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
	int order = 0;
	UDWORD i = 0;

	ASSERT_OR_RETURN(0, psStruct != nullptr && psStruct->pStructureType != nullptr, "Invalid structure pointer");

	int next = psStruct->status == SS_BUILT ? 1 : 0;
	// If complete, next is one after the current number of modules, otherwise next is the one we're working on.
	int max;
	switch (psStruct->pStructureType->type)
	{
	case REF_POWER_GEN:
		//check room for one more!
		max = std::max<int>(psStruct->capacity + next, lastOrderedModule + 1);
		if (max <= 1)
		{
			i = powerModuleStat;
			order = max;
		}
		break;
	case REF_FACTORY:
	case REF_VTOL_FACTORY:
		//check room for one more!
		max = std::max<int>(psStruct->capacity + next, lastOrderedModule + 1);
		if (max <= NUM_FACTORY_MODULES)
		{
			i = factoryModuleStat;
			order = max;
		}
		break;
	case REF_RESEARCH:
		//check room for one more!
		max = std::max<int>(psStruct->capacity + next, lastOrderedModule + 1);
		if (max <= 1)
		{
			i = researchModuleStat;
			order = max; // Research modules are weird. Build one, get three free.
		}
		break;
	default:
		//no other structures can have modules attached
		break;
	}

	if (order)
	{
		// Check availability of Module
		if (!((i < numStructureStats) &&
			(apStructTypeLists[psStruct->player][i] == AVAILABLE)))
		{
			order = 0;
		}
	}

	return order;
}

/*Deals with building a module - checking if any droid is currently doing this
 - if so, helping to build the current one*/
void setUpBuildModule(Droid* psDroid)
{
	Vector2i tile = map_coord(psDroid->order.pos);

	//check not another Truck started
	Structure* psStruct = getTileStructure(tile.x, tile.y);
	if (psStruct)
	{
		// if a droid is currently building, or building is in progress of being built/upgraded the droid's order should be DORDER_HELPBUILD
		if (checkDroidsBuilding(psStruct) || !psStruct->status)
		{
			//set up the help build scenario
			psDroid->order.type = DORDER_HELPBUILD;
			setDroidTarget(psDroid, (SimpleObject *)psStruct);
			if (droidStartBuild(psDroid))
			{
				psDroid->action = DACTION_BUILD;
				return;
			}
		}
		else
		{
			if (nextModuleToBuild(psStruct, -1) > 0)
			{
				//no other droids building so just start it off
				if (droidStartBuild(psDroid))
				{
					psDroid->action = DACTION_BUILD;
					return;
				}
			}
		}
	}
	cancelBuild(psDroid);
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


//bool electronicDroid(const DROID *psDroid)
//{
//	CHECK_DROID(psDroid);
//
//	//use slot 0 for now
//	if (psDroid->numWeaps > 0 && asWeaponStats[psDroid->asWeaps[0].nStat].weaponSubClass == WSC_ELECTRONIC)
//	{
//		return true;
//	}
//
//	if (psDroid->droidType == DROID_COMMAND && psDroid->psGroup && psDroid->psGroup->psCommander == psDroid)
//	{
//		// if a commander has EW units attached it is electronic
//		for (const DROID *psCurr = psDroid->psGroup->psList; psCurr; psCurr = psCurr->psGrpNext)
//		{
//			if (psDroid != psCurr && electronicDroid(psCurr))
//			{
//				return true;
//			}
//		}
//	}
//
//	return false;
//}

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
      return is_repairer(other_droid) &&
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
      return droid.isCommander();
  });
}
//UBYTE checkCommandExist(UBYTE player)
//{
//	UBYTE	quantity = 0;
//
//	for (DROID *psDroid = apsDroidLists[player]; psDroid != nullptr; psDroid = psDroid->psNext)
//	{
//		if (psDroid->droidType == DROID_COMMAND)
//		{
//			quantity++;
//		}
//	}
//	return quantity;
//}

bool isTransporter(const Droid& droid)
{
  using enum DROID_TYPE;
  return droid.getType() == TRANSPORTER ||
         droid.getType() == SUPER_TRANSPORTER;
}

////access functions for vtols
//bool isVtolDroid(const DROID *psDroid)
//{
//	return asPropulsionStats[psDroid->asBits[COMP_PROPULSION]].propulsionType == PROPULSION_TYPE_LIFT
//		   && !isTransporter(psDroid);
//}
//

///* returns true if the droid has lift propulsion and is moving */
//bool isFlying(const DROID *psDroid)
//{
//	return (asPropulsionStats + psDroid->asBits[COMP_PROPULSION])->propulsionType == PROPULSION_TYPE_LIFT
//		   && (psDroid->sMove.Status != MOVEINACTIVE || isTransporter(psDroid));
//}

bool vtolEmpty(const Droid& droid)
{
  assert(droid.isVtol());
  if (droid.getType() != DROID_TYPE::WEAPON) {
    return false;
  }

  return std::all_of(droid.getWeapons().begin(), droid.getWeapons().end(),
                     [&droid](const auto& weapon)
  {
      return weapon.is_vtol_weapon() &&
        weapon.is_empty_vtol_weapon(droid.getPlayer());
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
      return weapon.is_vtol_weapon() && weapon.hasFullAmmo();
  });
}

bool vtolReadyToRearm(const Droid& droid, const RearmPad& rearmPad)
{
  if (droid.isVtol() || droid.getAction() == ACTION::WAIT_FOR_REARM ||
      !vtolHappy(droid) || rearmPad.is_clear() ||
      !vtolRearming(droid)) {
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
//// true if a vtol droid currently returning to be rearmed
//bool vtolRearming(const DROID *psDroid)
//{
//	CHECK_DROID(psDroid);
//
//	if (!isVtolDroid(psDroid))
//	{
//		return false;
//	}
//	if (psDroid->droidType != DROID_WEAPON)
//	{
//		return false;
//	}
//
//	if (psDroid->action == DACTION_MOVETOREARM ||
//		psDroid->action == DACTION_WAITFORREARM ||
//		psDroid->action == DACTION_MOVETOREARMPOINT ||
//		psDroid->action == DACTION_WAITDURINGREARM)
//	{
//		return true;
//	}
//
//	return false;
//}


//bool droidAttacking(const DROID *psDroid)
//{
//	CHECK_DROID(psDroid);
//
//	//what about cyborgs?
//	if (!(psDroid->droidType == DROID_WEAPON || psDroid->droidType == DROID_CYBORG ||
//		  psDroid->droidType == DROID_CYBORG_SUPER))
//	{
//		return false;
//	}
//
//	if (psDroid->action == DACTION_ATTACK ||
//		psDroid->action == DACTION_MOVETOATTACK ||
//		psDroid->action == DACTION_ROTATETOATTACK ||
//		psDroid->action == DACTION_VTOLATTACK ||
//		psDroid->action == DACTION_MOVEFIRE)
//	{
//		return true;
//	}
//
//	return false;
//}

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
UWORD getNumAttackRuns(const Droid* psDroid, int weapon_slot)
{
	ASSERT_OR_RETURN(0, psDroid->isVtol(), "not a VTOL Droid");
	// if weapon is a salvo weapon, then number of shots that can be fired = vtolAttackRuns * numRounds
	if (asWeaponStats[psDroid->asWeaps[weapon_slot].nStat].upgraded[psDroid->getPlayer()].reloadTime)
	{
		return asWeaponStats[psDroid->asWeaps[weapon_slot].nStat].upgraded[psDroid->getPlayer()].numRounds
			* asWeaponStats[psDroid->asWeaps[weapon_slot].nStat].vtolAttackRuns;
	}
	return asWeaponStats[psDroid->asWeaps[weapon_slot].nStat].vtolAttackRuns;
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
  if (!droid.isVtol() || numWeapons(droid) == 0) {
    return;
  }

  auto& weapon = droid.getWeapons()[weapon_slot];
  if (weapon.get_stats().max_VTOL_attack_runs == 0) {
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
			(psStats->location != LOC_TURRET)) {
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
		psObj->type == OBJ_DROID && ((const Droid*)psObj)->type == DROID_COMMAND) {
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
	if (!proj_Direct(asWeaponStats + psDroid->asWeaps[0].nStat))
	{
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


// ////////////////////////////////////////////////////////////////////////////
// Give a droid from one player to another - used in Electronic Warfare and multiplayer.
// Got to destroy the droid and build another since there are too many complications otherwise.
// Returns the droid created.
Droid* giftSingleDroid(Droid* psD, unsigned to, bool electronic)
{
	ASSERT_OR_RETURN(nullptr, !isDead(psD), "Cannot gift dead unit");
	ASSERT_OR_RETURN(psD, psD->getPlayer() != to, "Cannot gift to self");
	ASSERT_OR_RETURN(nullptr, to < MAX_PLAYERS, "Cannot gift to = %" PRIu32 "", to);

	// Check unit limits (multiplayer only)
	syncDebug("Limits: %u/%d %u/%d %u/%d", getNumDroids(to), getMaxDroids(to), getNumConstructorDroids(to),
	          getMaxConstructors(to), getNumCommandDroids(to), getMaxCommanders(to));
	if (bMultiPlayer
		&& ((int)getNumDroids(to) >= getMaxDroids(to)
			|| ((psD->getType() == DROID_TYPE::CYBORG_CONSTRUCT ||
           psD->getType() == DROID_TYPE::CONSTRUCT)
				&& (int)getNumConstructorDroids(to) >= getMaxConstructors(to))
			|| (psD->getType() == DROID_TYPE::COMMAND && (int)getNumCommandDroids(to) >= getMaxCommanders(to))))
	{
		if (to == selectedPlayer || psD->getPlayer() == selectedPlayer)
		{
			CONPRINTF("%s", _("Unit transfer failed -- unit limits exceeded"));
		}
		return nullptr;
	}

	// electronic or campaign will destroy and recreate the droid.
	if (electronic || !bMultiPlayer) {
		DroidTemplate sTemplate;
		Droid* psNewDroid;

		templateSetParts(psD, &sTemplate); // create a template based on the droid
		sTemplate.name = WzString::fromUtf8(psD->name); // copy the name across
		// update score
		if (psD->getPlayer() == selectedPlayer && to != selectedPlayer && !bMultiPlayer) {
			scoreUpdateVar(WD_UNITS_LOST);
		}
		// make the old droid vanish (but is not deleted until next tick)
		adjustDroidCount(psD, -1);
		vanishDroid(psD);
		// create a new droid
		psNewDroid = reallyBuildDroid(&sTemplate, Position(psD->getPosition().x, psD->getPosition().y, 0), to, false, psD->getRotation());
		ASSERT_OR_RETURN(nullptr, psNewDroid, "Unable to build unit");

		addDroid(psNewDroid, apsDroidLists);
		adjustDroidCount(psNewDroid, 1);

		psNewDroid->body = clip(
						(psD->body * psNewDroid->original_hp + psD->original_hp / 2) / std::max(psD->original_hp, 1u), 1u,
						psNewDroid->original_hp);
		psNewDroid->experience = psD->experience;
		psNewDroid->kills = psD->kills;

		if (!(psNewDroid->type == DROID_TYPE::PERSON || cyborgDroid(psNewDroid) || isTransporter(*psNewDroid)))
		{
			updateDroidOrientation(psNewDroid);
		}

		triggerEventObjectTransfer(psNewDroid, psD->player);
		return psNewDroid;
	}

	int oldPlayer = psD->getPlayer();

	// reset the assigned state of units attached to a leader
	for (Droid* psCurr = apsDroidLists[oldPlayer]; psCurr != nullptr; psCurr = psCurr->psNext)
	{
		SimpleObject* psLeader;

		if (hasCommander(psCurr))
		{
			psLeader = (SimpleObject*)psCurr->group->psCommander;
		}
		else
		{
			//psLeader can be either a droid or a structure
			psLeader = orderStateObj(psCurr, DORDER_FIRESUPPORT);
		}

		if (psLeader && psLeader->id == psD->id)
		{
			psCurr->selected = false;
			orderDroid(psCurr, DORDER_STOP, ModeQueue);
		}
	}

	visRemoveVisibility((SimpleObject*)psD);
	psD->selected = false;

	adjustDroidCount(psD, -1);
	scriptRemoveObject(psD); //Remove droid from any script groups

	if (droidRemove(psD, apsDroidLists))
	{
		psD->player = to;

		addDroid(psD, apsDroidLists);
		adjustDroidCount(psD, 1);

		// the new player may have different default sensor/ecm/repair components
		if ((asSensorStats + psD->asBits[COMP_SENSOR])->location == LOC_DEFAULT)
		{
			if (psD->asBits[COMP_SENSOR] != aDefaultSensor[psD->player])
			{
				psD->asBits[COMP_SENSOR] = aDefaultSensor[psD->player];
			}
		}
		if ((asECMStats + psD->asBits[COMP_ECM])->location == LOC_DEFAULT)
		{
			if (psD->asBits[COMP_ECM] != aDefaultECM[psD->player])
			{
				psD->asBits[COMP_ECM] = aDefaultECM[psD->player];
			}
		}
		if ((asRepairStats + psD->asBits[COMP_REPAIRUNIT])->location == LOC_DEFAULT)
		{
			if (psD->asBits[COMP_REPAIRUNIT] != aDefaultRepair[psD->player])
			{
				psD->asBits[COMP_REPAIRUNIT] = aDefaultRepair[psD->player];
			}
		}
	}
	else
	{
		// if we couldn't remove it, then get rid of it.
		return nullptr;
	}

	// Update visibility
	visTilesUpdate((SimpleObject*)psD);

	// check through the players, and our allies, list of droids to see if any are targetting it
	for (unsigned int i = 0; i < MAX_PLAYERS; ++i)
	{
		if (!aiCheckAlliances(i, to))
		{
			continue;
		}

		for (Droid* psCurr = apsDroidLists[i]; psCurr != nullptr; psCurr = psCurr->psNext)
		{
			if (psCurr->order.psObj == psD || psCurr->action_target[0] == psD)
			{
				orderDroid(psCurr, DORDER_STOP, ModeQueue);
				break;
			}
			for (unsigned iWeap = 0; iWeap < psCurr->numWeaps; ++iWeap)
			{
				if (psCurr->action_target[iWeap] == psD)
				{
					orderDroid(psCurr, DORDER_STOP, ModeImmediate);
					break;
				}
			}
			// check through order list
			orderClearTargetFromDroidList(psCurr, (SimpleObject*)psD);
		}
	}

	for (unsigned int i = 0; i < MAX_PLAYERS; ++i)
	{
		if (!aiCheckAlliances(i, to))
		{
			continue;
		}

		// check through the players list, and our allies, of structures to see if any are targetting it
		for (Structure* psStruct = apsStructLists[i]; psStruct != nullptr; psStruct = psStruct->psNext)
		{
			if (psStruct->psTarget[0] == psD)
			{
				setStructureTarget(psStruct, nullptr, 0, ORIGIN_UNKNOWN);
			}
		}
	}

	triggerEventObjectTransfer(psD, oldPlayer);
	return psD;
}


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
	if (psTemplate->weaponCount == 0)
	{
		return false;
	}

	if (asPropulsionTypes[psPropStats->propulsionType].travel == AIR)
	{
		//check weapon stat for indirect
		if (!proj_Direct(asWeaponStats + psTemplate->asWeaps[0])
			|| !asWeaponStats[psTemplate->asWeaps[0]].vtolAttackRuns)
		{
			return false;
		}
	}
	else
	{
		// VTOL weapons do not go on non-AIR units.
		if (asWeaponStats[psTemplate->asWeaps[0]].vtolAttackRuns)
		{
			return false;
		}
	}

	//also checks that there is no other system component
	if (psTemplate->asParts[COMP_BRAIN] != 0
		&& asWeaponStats[psTemplate->asWeaps[0]].weaponSubClass != WSC_COMMAND)
	{
		assert(false);
		return false;
	}

	return true;
}


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
	if (!isSelectable(psDroid))
	{
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
	auto psDroid = (Droid*)psObj;

	if (psDroid == nullptr) {
		debug(LOG_ERROR, "droid pointer invalid");
		return false;
	}
	if (psDroid->type != OBJ_DROID || psDroid->died) {
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
	if (psDroid->died == NOT_CURRENT_LIST || isTransporter(psDroid)
		|| psDroid->getPosition().x == INVALID_XY || psDroid->getPosition().y == INVALID_XY || missionIsOffworld()
		|| mapHeight == 0)
	{
		// Off world or on a transport or is a transport or in mission list, or on a mission, or no map - ignore
		return true;
	}
	return worldOnMap(psDroid->getPosition().x, psDroid->getPosition().y);
}

/** Teleport a droid to a new position on the map */
void droidSetPosition(Droid* psDroid, int x, int y)
{
	psDroid->getPosition().x = x;
	psDroid->getPosition().y = y;
	psDroid->getPosition().z = map_Height(psDroid->getPosition().x, psDroid->getPosition().y);
	initDroidMovement(psDroid);
	visTilesUpdate((SimpleObject*)psDroid);
}

/** Check validity of a droid. Crash hard if it fails. */
void checkDroid(const Droid* droid, const char* const location, const char* function, const int recurse)
{
	if (recurse < 0)
	{
		return;
	}

	ASSERT_HELPER(droid != nullptr, location, function, "CHECK_DROID: NULL pointer");
	ASSERT_HELPER(droid->type == OBJ_DROID, location, function, "CHECK_DROID: Not droid (type %d)", (int)droid->type);
	ASSERT_HELPER(droid->numWeaps <= MAX_WEAPONS, location, function, "CHECK_DROID: Bad number of droid weapons %d",
	              (int)droid->numWeaps);
	ASSERT_HELPER(
		(unsigned)droid->listSize <= droid->asOrderList.size() && (unsigned)droid->listPendingBegin <= droid->
		asOrderList.size(), location, function, "CHECK_DROID: Bad number of droid orders %d %d %d",
		(int)droid->listSize, (int)droid->listPendingBegin, (int)droid->asOrderList.size());
	ASSERT_HELPER(droid->player < MAX_PLAYERS, location, function, "CHECK_DROID: Bad droid owner %d",
	              (int)droid->player);
	ASSERT_HELPER(droidOnMap(droid), location, function, "CHECK_DROID: Droid off map");
	ASSERT_HELPER(droid->body <= droid->original_hp, location, function,
								"CHECK_DROID: More body points (%u) than original body points (%u).", (unsigned)droid->body,
								(unsigned)droid->original_hp);

	for (int i = 0; i < MAX_WEAPONS; ++i)
	{
		ASSERT_HELPER(droid->asWeaps[i].time_last_fired <= gameTime, location, function,
                  "CHECK_DROID: Bad last fired time for turret %u", i);
	}
}

int droidSqDist(Droid* psDroid, SimpleObject* psObj)
{
	PropulsionStats* psPropStats = asPropulsionStats + psDroid->asBits[COMP_PROPULSION];

	if (!fpathCheck(psDroid->getPosition(), psObj->getPosition(), psPropStats->propulsionType))
	{
		return -1;
	}
	return objectPositionSquareDiff(psDroid->getPosition(), psObj->getPosition());
}

unsigned calculate_max_range(const Droid& droid)
{
  using enum DROID_TYPE;
  if (droid.getType() == SENSOR)
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
  if (numWeapons(droid) == 0 || !structure.hasSensor()) return false;

  if (droid.isVtol())
  {
    return structure.hasVtolInterceptSensor() ||
           structure.hasVtolCbSensor();
  }
  else if (hasArtillery(droid))
  {
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
  if (tile->info_bits & BLOCKING ||
      tile_is_occupied(*tile) ||
      get_terrain_type(*tile) == TER_CLIFFFACE ||
      get_terrain_type(*tile) == TER_WATER) {
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
  auto shortest_distance = UDWORD_MAX;
  std::for_each(droids.begin(), droids.end(), [&](auto& droid)
  {
      if (droid.isVtol())
        return;
      if (selected && !droid.isSelected())
        return;

      const auto distance = iHypot(droid.getPosition().x - x,
                                   droid.getPosition().y - y);
      if (distance < shortest_distance)
      {
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
  std::for_each(droids.begin(), droids.end(), [&droid](const auto &other_droid)
  {
      Vector2i tile{0, 0};
      if (other_droid.isStationary()) {
        tile = map_coord(other_droid.getPosition().xy());
      } else {
        tile = map_coord(other_droid.getDestination());
      }

      if (&droid == &other_droid) {
        return;
      } else if (tile_on_map(tile)) {
        mapTile(tile)->info_bits |= BLOCKING;
      }
  });
}

void clear_blocking_flags(const Droid& droid)
{
  const auto &droids = apsDroidLists[droid.getPlayer()];
  std::for_each(droids.begin(), droids.end(), [&droid](const auto &other_droid)
  {
      Vector2i tile{0, 0};
      if (other_droid.isStationary()) {
        tile = map_coord(other_droid.getPosition().xy());
      } else {
        tile = map_coord(other_droid.getDestination());
      }

      if (tile_on_map(tile)) {
        mapTile(tile)->info_bits &= ~BLOCKING;
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
