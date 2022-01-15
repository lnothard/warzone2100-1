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
 * @file order.cpp
 * Functions for setting the orders of a droid or group of droids.
 */

#include <cstring>

#include "lib/framework/frame.h"
#include "lib/framework/input.h"
#include "lib/framework/math_ext.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/netplay/netplay.h"
#include "lib/sound/audio_id.h"
#include "lib/sound/audio.h"

#include "objects.h"
#include "order.h"
#include "action.h"
#include "map.h"
#include "projectile.h"
#include "effects.h"
#include "intorder.h"
#include "transporter.h"
#include "qtscript.h"
#include "group.h"
#include "cmddroid.h"
#include "move.h"
#include "multiplay.h"
#include "random.h"
#include "mission.h"
#include "hci.h"
#include "visibility.h"
#include "display.h"
#include "ai.h"
#include "warcam.h"
#include "fpath.h"
#include "display3d.h"
#include "console.h"
#include "mapgrid.h"

static void orderClearDroidList(Droid* psDroid);

/** Whether an order effect has been displayed
 * @todo better documentation required.
 */

//////////////////////////////////////////////////////////////////

#define ASSERT_PLAYER_OR_RETURN(retVal, player) \
	ASSERT_OR_RETURN(retVal, player < MAX_PLAYERS, "Invalid player: %" PRIu32 "", player);

Order::Order(ORDER_TYPE type)
  : type{type}, pos{0, 0}, pos2{0, 0}, direction{0}, index{0},
    rtrType{NO_RESULT}, target{nullptr}, structure_stats{nullptr}
{
}

Order::Order(ORDER_TYPE type, Vector2i pos)
  : type{type}, pos{pos}, pos2{0, 0}, direction{0}, index{0},
    rtrType{NO_RESULT}, target{nullptr}, structure_stats{nullptr}
{
}

Order::Order(ORDER_TYPE type, Vector2i pos, RTR_DATA_TYPE rtrType)
  : type{type}, pos{pos}, pos2{0, 0}, direction{0}, index{0},
    rtrType{rtrType}, target{nullptr}, structure_stats{nullptr}
{
}

Order::Order(ORDER_TYPE type, StructureStats& stats, Vector2i pos, unsigned direction)
  : type{type}, pos{pos}, pos2{0, 0}, direction{direction}, index{0},
    rtrType{NO_RESULT}, target{nullptr}, structure_stats{&stats}
{
}

RtrBestResult::RtrBestResult(RTR_DATA_TYPE type, SimpleObject* obj)
  : type{type}, target{obj}
{
}

RtrBestResult::RtrBestResult()
  : type{NO_RESULT}, target{nullptr}
{
}

RtrBestResult::RtrBestResult(Order& order)
  : type{order.rtrType}, target{order.target}
{
}

Order::Order(ORDER_TYPE type, StructureStats& stats, Vector2i pos, Vector2i pos2, unsigned direction)
  : type{type}, pos{pos}, pos2{pos2}, direction{direction}, index{0},
    rtrType{NO_RESULT}, target{nullptr}, structure_stats{&stats}
{
}

Order::Order(ORDER_TYPE type, SimpleObject& target)
  : type{type}, pos{0, 0}, pos2{0, 0}, direction{0}, index{0},
    rtrType{NO_RESULT}, target{&target}, structure_stats{nullptr}
{
}

Order::Order(ORDER_TYPE type, SimpleObject& target, RTR_DATA_TYPE rtrType)
  : type{type}, pos{0, 0}, pos2{0, 0}, direction{0}, index{0},
    rtrType{rtrType}, target{&target}, structure_stats{nullptr}
{
}

Order::Order(ORDER_TYPE type, SimpleObject& target, unsigned index)
  : type{type}, pos{0, 0}, pos2{0, 0}, direction{0}, index{index},
    rtrType{NO_RESULT}, target{&target}, structure_stats{nullptr}
{
}

static RtrBestResult decideWhereToRepairAndBalance(Droid* psDroid);

/**
 * This function checks if there are any damaged droids within a defined range.
 * It returns the damaged droid if there is any, or nullptr if none was found
 */
Droid* checkForRepairRange(Droid* psDroid)
{
	const Droid* psFailedTarget;
	if (psDroid->getAction() == ACTION::SULK) {
		psFailedTarget = &dynamic_cast<const Droid&>(
            psDroid->getTarget(0));
	}

	ASSERT(psDroid->getType() == DROID_TYPE::REPAIRER ||
         psDroid->getType() == DROID_TYPE::CYBORG_REPAIR,
         "Invalid droid type");

	const auto radius = (psDroid->getOrder().type == ORDER_TYPE::HOLD ||
                                (psDroid->getOrder().type == ORDER_TYPE::NONE &&
		                            psDroid->secondaryGetState(
                                        SECONDARY_ORDER::HALT_TYPE) == DSS_HALT_HOLD))
		                  ? REPAIR_RANGE
		                  : REPAIR_MAXDIST;

	auto bestDistanceSq = radius * radius;
	Droid* best = nullptr;

	for (SimpleObject* object : gridStartIterate(psDroid->getPosition().x,
                                               psDroid->getPosition().y,
                                               radius))
	{
		auto distanceSq = droidSqDist(psDroid, object);
		// droidSqDist returns -1 if unreachable, (unsigned)-1 is a big number
		if (object == orderStateObj(psDroid, ORDER_TYPE::GUARD)) {
      // if guarding a unit — always do that first
			distanceSq = 0;
		}

		auto droid = dynamic_cast<Droid*>(object);
		if (!droid && // must be a droid
			  droid != psFailedTarget && // must not have just failed to reach it
		  	distanceSq <= bestDistanceSq && // must be as close as possible
		  	aiCheckAlliances(psDroid->getPlayer(),
                         droid->getPlayer()) && // must be a friendly droid
		  	droidIsDamaged(droid) && // must need repairing
		  	visibleObject(psDroid,
                      droid,
                      false)) // must be able to sense it
		{
			bestDistanceSq = distanceSq;
			best = droid;
		}
	}
	return best;
}

/**
 * This function checks if there are any structures to repair or help
 * build in a given radius near the droid defined by REPAIR_RANGE if it
 * is on hold, and REPAIR_MAXDIST if not on hold. It returns a damaged
 * or incomplete structure if any was found or nullptr if none was found.
 */
static std::pair<Structure*, ACTION> checkForDamagedStruct(Droid* psDroid)
{
	const Structure* psFailedTarget = nullptr;
	if (psDroid->getAction() == ACTION::SULK) {
		psFailedTarget = &dynamic_cast<const Structure&>(
            psDroid->getTarget(0));
	}

	const auto radius = ((psDroid->getOrder().type == ORDER_TYPE::HOLD) ||
                     (psDroid->getOrder().type == ORDER_TYPE::NONE &&
		                  psDroid->secondaryGetState(SECONDARY_ORDER::HALT_TYPE) == DSS_HALT_HOLD))
		                  ? REPAIR_RANGE
		                  : REPAIR_MAXDIST;

	auto bestDistanceSq = radius * radius;
	std::pair<Structure*, ACTION> best = {nullptr, ACTION::NONE};

	for (SimpleObject* object : gridStartIterate(psDroid->getPosition().x,
                                               psDroid->getPosition().y,
                                               radius))
	{
		auto distanceSq = droidSqDist(psDroid, object);
		// droidSqDist returns -1 if unreachable, (unsigned)-1 is a big number

		auto structure = dynamic_cast<Structure*>(object);
		if (!structure || // must be a structure.
			  structure == psFailedTarget || // must not have just failed to reach it
			  distanceSq > bestDistanceSq || // must be as close as possible
			  !visibleObject(psDroid,
                       structure, false) || // must be able to sense it
			  !aiCheckAlliances(psDroid->getPlayer(),
                          structure->getPlayer()) || // must be a friendly structure
			  checkDroidsDemolishing(structure)) // must not be trying to get rid of it
		{
			continue;
		}

		// check for structures to repair
		if (structure->getState() == STRUCTURE_STATE::BUILT &&
        structIsDamaged(structure)) {
			bestDistanceSq = distanceSq;
			best = {structure, ACTION::REPAIR};
		}
		// check for structures to help build
		else if (structure->getState() == STRUCTURE_STATE::BEING_BUILT) {
			bestDistanceSq = distanceSq;
			best = {structure, ACTION::BUILD};
		}
	}
	return best;
}

static bool isRepairLikeAction(ACTION action)
{
  using enum ACTION;
	switch (action) {
	case BUILD:
	case BUILD_WANDER:
	case DEMOLISH:
	case DROID_REPAIR:
	case MOVE_TO_BUILD:
	case MOVE_TO_DEMOLISH:
	case MOVE_TO_DROID_REPAIR:
	case MOVE_TO_REPAIR:
	case MOVE_TO_RESTORE:
	case REPAIR:
	case RESTORE:
		return true;
	default:
		return false;
	}
}

/**
 * This function sends all members of the psGroup the order psData
 * using orderDroidBase(). If the order data is to recover an artifact,
 * the order is only given to the closest droid to the artifact.
 */
static void orderCmdGroupBase(Group* psGroup, Order* psData)
{
	ASSERT_OR_RETURN(, psGroup != nullptr, "Invalid unit group");
	syncDebug("Commander group order");

	if (psData->type == ORDER_TYPE::RECOVER) {
		// picking up an artifact - only need to send one unit
		Droid* psChosen = nullptr;
		auto mindist = SDWORD_MAX;
		for (auto psCurr : psGroup->getMembers())
		{
			if (psCurr->getOrder().type == ORDER_TYPE::RETURN_TO_REPAIR ||
          psCurr->getOrder().type == ORDER_TYPE::RETURN_TO_BASE   ||
          psCurr->getOrder().type ==	ORDER_TYPE::RTR_SPECIFIED) {
				// don't touch units returning for repairs
				continue;
			}
			const auto currdist = objectPositionSquareDiff(psCurr->getPosition(), psData->target->getPosition());
			if (currdist < mindist) {
				psChosen = psCurr;
				mindist = currdist;
			}
			syncDebug("command %d,%d", psCurr->getId(), currdist);
		}
		if (psChosen != nullptr) {
			orderDroidBase(psChosen, psData);
		}
	}
	else {
		const bool isAttackOrder = psData->type == ORDER_TYPE::ATTACK_TARGET ||
                               psData->type == ORDER_TYPE::ATTACK;
		for (auto psCurr : psGroup->getMembers())
		{
			syncDebug("command %d", psCurr->getId());
			if (!orderState(psCurr, ORDER_TYPE::RETURN_TO_REPAIR)) {
        // if you change this, you'll need to change sendCmdGroup()
				if (!isAttackOrder) {
					psCurr->orderDroidBase(psData);
					continue;
				}
				if (psCurr->getType() == DROID_TYPE::SENSOR &&
            psData->target) {
					// sensors must observe, not attack
					auto observeOrder = Order(ORDER_TYPE::OBSERVE, *psData->target);
					psCurr->orderDroidBase(&observeOrder);
				}
				else {
					// for non-sensors, check that the designated target is actually valid
					// there is no point in ordering AA gun to attack ground units
					for (int i = 0; i < MAX_WEAPONS; i++)
					{
						if (validTarget(psCurr, psData->target, i)) {
							psCurr->orderDroidBase(psData);
							break;
						}
					}
				}
			}
		}
	}
}

/**
 * The minimum delay to be used on orderPlayFireSupportAudio()
 * for fire support sound.
 */

static constexpr auto AUDIO_DELAY_FIRESUPPORT	=	3 * GAME_TICKS_PER_SEC;

/**
 * This function chooses the sound to play after the object is assigned to fire
 * support a specific unit. Uses audio_QueueTrackMinDelay() to play the sound.
 * @todo this function is about playing audio. I'm not sure this should be in here.
 */
static void orderPlayFireSupportAudio(SimpleObject* psObj)
{
	auto iAudioID = NO_SOUND;
	ASSERT_OR_RETURN(, psObj != nullptr, "Invalid pointer");

	// play appropriate speech
  if (auto psDroid = dynamic_cast<Droid*>(psObj)) {
    if (psDroid->getType() == DROID_TYPE::COMMAND) {
      iAudioID = ID_SOUND_ASSIGNED_TO_COMMANDER;
    }
    else if (psDroid->getType() == DROID_TYPE::SENSOR) {
      iAudioID = ID_SOUND_ASSIGNED_TO_SENSOR;
    }
  }
  else if (auto psStruct = dynamic_cast<Structure*>(psObj)) {
    //check for non-CB first
		if (structStandardSensor(psStruct) ||
        structVTOLSensor(psStruct)) {
			iAudioID = ID_SOUND_ASSIGNED_TO_SENSOR;
		}
		else if (structCBSensor(psStruct) ||
             structVTOLCBSensor(psStruct)) {
			iAudioID = ID_SOUND_ASSIGNED_TO_COUNTER_RADAR;
		}
	}
	if (iAudioID != NO_SOUND) {
		audio_QueueTrackMinDelay(iAudioID, AUDIO_DELAY_FIRESUPPORT);
	}
}

/** This function sends the droid an order. It uses sendDroidInfo() if mode == ModeQueue and orderDroidBase() if not. */
void orderDroid(Droid* psDroid, ORDER_TYPE order, QUEUE_MODE mode)
{
  using enum ORDER_TYPE;
  
	ASSERT(psDroid != nullptr,
	       "orderUnit: Invalid unit pointer");
  
	ASSERT(order == NONE ||
	       order == RETURN_TO_REPAIR ||
	       order == RETURN_TO_BASE ||
	       order == RECYCLE ||
	       order == TRANSPORT_IN ||
	       order == STOP || 
	       order == HOLD,
	       "orderUnit: Invalid order");

	Order sOrder(order);
	if (mode == ModeQueue && bMultiPlayer) {
		sendDroidInfo(psDroid, sOrder, false);
	}
	else {
		orderClearDroidList(psDroid);
		orderDroidBase(psDroid, &sOrder);
	}
}

/**
 * This function compares the current droid's order to the order.
 * Returns true if they are the same, false otherwise
 */
bool orderState(Droid* psDroid, ORDER_TYPE order)
{
	if (order == ORDER_TYPE::RETURN_TO_REPAIR) {
		return psDroid->getOrder().type == ORDER_TYPE::RETURN_TO_REPAIR ||
           psDroid->getOrder().type == ORDER_TYPE::RTR_SPECIFIED;
	}
	return psDroid->getOrder().type == order;
}

/**
 * This function returns true if the order is an acceptable order
 * to give for a given location on the map.
 */
bool validOrderForLoc(ORDER_TYPE order)
{
  using enum ORDER_TYPE;
	return (order == NONE || order == MOVE || order == GUARD ||
		      order == SCOUT || order == PATROL || order == TRANSPORT_OUT ||
          order == TRANSPORT_IN || order == TRANSPORT_RETURN ||
          order == DISEMBARK || order == CIRCLE);
}

/**
 * This function sends the droid an order with a location.
 * If the mode is ModeQueue, the order is added to the droid's order
 * list using sendDroidInfo(), else, a DROID_ORDER_DATA is alloc,
 * the old order list is erased, and the order is sent using orderDroidBase().
 */
void orderDroidLoc(Droid* psDroid, ORDER_TYPE order, unsigned x, unsigned y, QUEUE_MODE mode)
{
	ASSERT_OR_RETURN(, psDroid != nullptr, "Invalid unit pointer");
	ASSERT_OR_RETURN(, validOrderForLoc(order), "Invalid order for location");

	Order sOrder(order, Vector2i(x, y));
	if (mode == ModeQueue) {
		sendDroidInfo(psDroid, sOrder, false);
		return; // Wait to receive our order before changing the droid.
	}

	orderClearDroidList(psDroid);
	orderDroidBase(psDroid, &sOrder);
}

/**
 * This function attributes the order's location to (pX,pY)
 * if the order is the same as the droid. Returns true if it
 * was attributed and false if not.
 */
bool orderStateLoc(Droid* psDroid, ORDER_TYPE order, unsigned* pX, unsigned* pY)
{
	if (order != psDroid->getOrder().type) {
		return false;
	}

	// check the order is one with a location
	switch (psDroid->getOrder().type) {
    case ORDER_TYPE::MOVE:
	  	*pX = psDroid->getOrder().pos.x;
	  	*pY = psDroid->getOrder().pos.y;
	  	return true;
    default:
      // not a location order - return false
      break;
	}
	return false;
}

/**
 * This function returns true if the order is a valid order
 * to give to an object and false if it's not.
 */
bool validOrderForObj(ORDER_TYPE order)
{
  using enum ORDER_TYPE;
  return (order == NONE || order == HELP_BUILD || order == DEMOLISH ||
          order == REPAIR || order == ATTACK || order == FIRE_SUPPORT ||
          order == COMMANDER_SUPPORT || order == OBSERVE ||
          order == ATTACK_TARGET || order == RETURN_TO_REPAIR ||
          order == RTR_SPECIFIED || order == EMBARK || order == GUARD ||
          order == DROID_REPAIR || order == RESTORE ||
          order == BUILD_MODULE || order == REARM || order == RECOVER);
}

/** 
 * This function sends an order with an object to the droid.
 * If the mode is ModeQueue, the order is added to the droid's 
 * order list using sendDroidInfo(), else, a DROID_ORDER_DATA 
 * is alloc, the old order list is erased, and the order is sent 
 * using orderDroidBase().
 */
void orderDroidObj(Droid* psDroid, ORDER_TYPE order, SimpleObject* psObj, QUEUE_MODE mode)
{
	ASSERT(psDroid != nullptr, "Invalid unit pointer");
	ASSERT(validOrderForObj(order), "Invalid order for object");
	ASSERT_OR_RETURN(, !isBlueprint(psObj), "Target %s is a blueprint", objInfo(psObj));
	ASSERT_OR_RETURN(, !psObj->died, "Target dead");

	Order sOrder(order, *psObj);
	if (mode == ModeQueue)  {
		sendDroidInfo(psDroid, sOrder, false);
		return; // Wait for the order to be received before changing the droid.
	}

	orderClearDroidList(psDroid);
	orderDroidBase(psDroid, &sOrder);
}

/**
 * This function returns the order's target if it has one, and NULL if the order is not a target order.
 * @todo the first switch can be removed and substituted by orderState() function.
 * @todo the use of this function is somewhat superfluous on some cases. Investigate.
 */
SimpleObject* orderStateObj(Droid* psDroid, ORDER_TYPE order)
{
	bool match = false;

  using enum ORDER_TYPE;
	switch (order) {
	case BUILD:
	case LINE_BUILD:
	case HELP_BUILD:
		if (psDroid->getOrder().type == BUILD ||
			  psDroid->getOrder().type == HELP_BUILD ||
			  psDroid->getOrder().type == LINE_BUILD) {
			match = true;
		}
		break;
	case ATTACK:
	case FIRE_SUPPORT:
	case OBSERVE:
	case DEMOLISH:
	case DROID_REPAIR:
	case REARM:
	case GUARD:
		if (psDroid->getOrder().type == order) {
			match = true;
		}
		break;
	case RETURN_TO_REPAIR:
		if (psDroid->getOrder().type == RETURN_TO_REPAIR ||
		   	psDroid->getOrder().type == RTR_SPECIFIED) {
			match = true;
		}
	default:
		break;
	}

	if (!match) {
		return nullptr;
	}

	// check the order is one with an object
	switch (psDroid->getOrder().type) {
	default:
		// not an object order -- return false
		return nullptr;
		break;
	case BUILD:
	case LINE_BUILD:
		if (psDroid->getAction() == ACTION::BUILD ||
			  psDroid->getAction() == ACTION::BUILD_WANDER) {
			return psDroid->getOrder().target;
		}
		break;
	case HELP_BUILD:
		if (psDroid->getAction() == ACTION::BUILD ||
			psDroid->getAction() == ACTION::BUILD_WANDER ||
			psDroid->getAction() == ACTION::MOVE_TO_BUILD) {
			return psDroid->getOrder().target;
		}
		break;
	case ATTACK:
	case FIRE_SUPPORT:
	case OBSERVE:
	case DEMOLISH:
	case RETURN_TO_REPAIR:
	case RTR_SPECIFIED:
	case DROID_REPAIR:
	case REARM:
	case GUARD:
		return psDroid->getOrder().target;
		break;
	}
	return nullptr;
}


/**
 * This function sends the droid an order with a location and stats.
 * If the mode is ModeQueue, the order is added to the droid's order
 * list using sendDroidInfo(), else, a DROID_ORDER_DATA is alloc, the
 * old order list is erased, and the order is sent using orderDroidBase().
 */
void orderDroidStatsLocDir(Droid* psDroid, ORDER_TYPE order, StructureStats* psStats,
                           unsigned x, unsigned y, uint16_t direction, QUEUE_MODE mode)
{
	ASSERT(psDroid != nullptr, "Invalid unit pointer");
	ASSERT(order == ORDER_TYPE::BUILD, "Invalid order for location");

	Order sOrder(order, *psStats, Vector2i(x, y), direction);
	if (mode == ModeQueue && bMult) {
		sendDroidInfo(psDroid, sOrder, false);
		return; // Wait for our order before changing the droid.
	}

	orderClearDroidList(psDroid);
	orderDroidBase(psDroid, &sOrder);
}

/**
 * This function adds that order to the droid's list using sendDroidInfo().
 * @todo seems closely related with orderDroidStatsLocDir(). See if this one can be incorporated on it.
 */
void orderDroidStatsLocDirAdd(Droid* psDroid, ORDER_TYPE order, StructureStats* psStats, unsigned x, unsigned y,
                              uint16_t direction, bool add)
{
	ASSERT(psDroid != nullptr, "Invalid unit pointer");

	// can only queue build orders with this function
	if (order != ORDER_TYPE::BUILD) {
		return;
	}
	sendDroidInfo(psDroid, Order(order, *psStats,
                               Vector2i(x, y), direction), add);
}


/// Equivalent to orderDroidStatsLocDir(), but uses two locations.
void orderDroidStatsTwoLocDir(Droid* psDroid, ORDER_TYPE order, StructureStats* psStats, unsigned x1, unsigned y1,
                              unsigned x2, unsigned y2, uint16_t direction, QUEUE_MODE mode)
{
	ASSERT(psDroid != nullptr, "Invalid unit pointer");
	ASSERT(order == ORDER_TYPE::LINE_BUILD, "Invalid order for location");

	Order sOrder(order, *psStats,
               Vector2i(x1, y1),
               Vector2i(x2, y2), direction);
	if (mode == ModeQueue && bMultiPlayer) {
		sendDroidInfo(psDroid, sOrder, false);
    // wait for our order before changing the droid
		return;
	}
	orderClearDroidList(psDroid);
	orderDroidBase(psDroid, &sOrder);
}

/** Equivalent to orderDroidStatsLocDirAdd(), but uses two locations.
 * @todo seems closely related with orderDroidStatsTwoLocDir(). See if this can be incorporated on it.
 */
void orderDroidStatsTwoLocDirAdd(Droid* psDroid, ORDER_TYPE order, StructureStats* psStats,
                                 unsigned x1, unsigned y1, unsigned x2, unsigned y2, unsigned direction)
{
	ASSERT(psDroid != nullptr, "Invalid unit pointer");
	ASSERT(order == ORDER_TYPE::LINE_BUILD, "Invalid order for location");

	sendDroidInfo(psDroid, Order(order, *psStats,
                                Vector2i(x1, y1),
                               Vector2i(x2, y2),
                               direction), true);
}


/**
 * This function returns false if droid's order and order don't match or the
 * order is not a location order. Else ppsStats = psDroid->psTarStats,
 * (pX,pY) = psDroid.(orderX,orderY) and it returns true.
 */
bool orderStateStatsLoc(Droid* psDroid, ORDER_TYPE order, StructureStats** ppsStats)
{
	bool match = false;

  using enum ORDER_TYPE;
	switch (order) {
	case BUILD:
	case LINE_BUILD:
		if (psDroid->getOrder().type == BUILD ||
			psDroid->getOrder().type == LINE_BUILD) {
			match = true;
		}
		break;
	default:
		break;
	}

	if (!match) {
		return false;
	}

	// check the order is one with stats and a location
	switch (psDroid->getOrder().type) {
	case BUILD:
	case LINE_BUILD:
		if (psDroid->getAction() == ACTION::MOVE_TO_BUILD) {
			*ppsStats = psDroid->getOrder().structure_stats.get();
			return true;
		}
		break;
  default:
    return false;
	}
	return false;
}

/** This function clears all the synchronised orders from the list, calling orderDroidListEraseRange() from 0 to psDroid->listSize.*/
void orderClearDroidList(Droid* psDroid)
{
	syncDebug("droid%d list cleared", psDroid->getId());
	orderDroidListEraseRange(psDroid, 0, psDroid->listSize);
}

/**
 * This function sends the droid an order with a location using sendDroidInfo().
 * @todo it is very close to what orderDroidLoc() function does. Suggestion to refract them.
 */
static bool orderDroidLocAdd(Droid* psDroid, ORDER_TYPE order, unsigned x, unsigned y, bool add = true)
{
  using enum ORDER_TYPE;
	// can only queue move, scout, and disembark orders
	if (order != MOVE && order != SCOUT && order != DISEMBARK) {
		return false;
	}
	sendDroidInfo(psDroid, Order(order, Vector2i(x, y)), add);
	return true;
}

/** This function sends the droid an order with a location using sendDroidInfo().
 * @todo it is very close to what orderDroidObj() function does. Suggestion to refract them.
 */
static bool orderDroidObjAdd(Droid* psDroid, Order const& order, bool add)
{
	ASSERT(!isBlueprint(order.target),
         "Target %s for queue is a blueprint",
         objInfo(order.target));

  using enum ORDER_TYPE;
	// check can queue the order
	switch (order.type) {
	case ATTACK:
	case REPAIR:
	case OBSERVE:
	case DROID_REPAIR:
	case FIRE_SUPPORT:
	case DEMOLISH:
	case HELP_BUILD:
	case BUILD_MODULE:
		break;
	default:
		return false;
	}
	sendDroidInfo(psDroid, order, add);
	return true;
}

/**
 * This function returns an order which is assigned according to
 * the location and droid. Uses altOrder flag to choose between a
 * direct order or an altOrder.
 */
ORDER_TYPE chooseOrderLoc(Droid* psDroid, int x, int y, bool altOrder)
{
  using enum ORDER_TYPE;
	ORDER_TYPE order = NONE;
	auto propulsion = psDroid->getPropulsion()->propulsionType;

	if (isTransporter(*psDroid) &&
      game.type == LEVEL_TYPE::CAMPAIGN) {
		// transporter cannot be player-controlled in campaign
		return NONE;
	}

	// default to move; however, we can only end up on a tile
	// where can stay, i.e., VTOLs must be able to land as well
	if (psDroid->isVtol()) {
		propulsion = PROPULSION_TYPE::WHEELED;
	}
	if (!fpathBlockingTile(map_coord(x),
                         map_coord(y),
                         propulsion)) {
		order = MOVE;
	}

	// scout if alt was pressed
	if (altOrder) {
		order = SCOUT;
		if (psDroid->isVtol()) {
			// patrol if in VTOL
			order = PATROL;
		}
	}

	// and now we want transporters to fly - in multiplayer!
	if (isTransporter(*psDroid) &&
      game.type == LEVEL_TYPE::SKIRMISH) {
		// in multiplayer - if ALT key is pressed then need to get
    // the transporter to fly to location and all units to disembark
		if (altOrder) {
			order = DISEMBARK;
		}
	}
	else if (psDroid->secondaryGetState(SECONDARY_ORDER::CIRCLE,
                                      ModeQueue) == DSS_CIRCLE_SET) {
    // ModeQueue here means to check whether we pressed the circle button,
    // whether or not it sync'd yet. The reason for this weirdness is that
    // a circle order makes no sense as a secondary state in the first place
    // (the circle button _should_ have been only in the UI, not in the game
    // state..!), so anything dealing with circle orders will necessarily be weird.
    order = CIRCLE;
		psDroid->secondarySetState(SECONDARY_ORDER::CIRCLE, DSS_NONE);
	}
	else if (psDroid->secondaryGetState(SECONDARY_ORDER::PATROL,
                             ModeQueue) == DSS_PATROL_SET) {
    // ModeQueue here means to check whether we pressed the patrol button,
    // whether or not it synched yet. The reason for this weirdness is that
    // a patrol order makes no sense as a secondary state in the first place
    // (the patrol button _should_ have been only in the UI, not in the game
    // state..!), so anything dealing with patrol orders will necessarily be weird.
		order = PATROL;
		psDroid->secondarySetState(SECONDARY_ORDER::PATROL, DSS_NONE);
	}
	return order;
}

/**
 * This function sends the selected droids an order to given a location.
 * If a delivery point is selected, it is moved to a new location. If add
 * is true then the order is queued. This function should only be called from UI.
 */
void orderSelectedLoc(unsigned player, unsigned x, unsigned y, bool add)
{
	ORDER_TYPE order;

	// if we're in build select mode ignore all other clicking
	if (intBuildSelectMode()) {
		return;
	}

	ASSERT_PLAYER_OR_RETURN(, player);

	// note that an order list graphic needs to be displayed
	bOrderEffectDisplayed = false;

	for (auto& psCurr : apsDroidLists[player])
	{
		if (psCurr.selected) {
			// can't use bMultiPlayer since multimsg could be off.
			if (psCurr.getType() == DROID_TYPE::SUPER_TRANSPORTER &&
          game.type == LEVEL_TYPE::CAMPAIGN) {
				// a transporter in campaign mode cannot be controlled by players
				DeSelectDroid(&psCurr);
				continue;
			}

			order = chooseOrderLoc(&psCurr, x, y, specialOrderKeyDown());
			// see if the order can be added to the list
			if (order != ORDER_TYPE::NONE &&
          !(add && orderDroidLocAdd(&psCurr, order, x, y))) {
				// if not just do it straight off
				orderDroidLoc(&psCurr, order, x, y, ModeQueue);
			}
		}
	}
}

static int highestQueuedModule(Order const& order, Structure const* structure, int prevHighestQueuedModule)
{
	auto thisQueuedModule = -1;
  using enum ORDER_TYPE;
	switch (order.type) {
	  case BUILD_MODULE:
	  	if (order.target == structure) {
        // ^^ order must be for this structure
        // order says which module to build
	  		thisQueuedModule = static_cast<int>(order.index);
	  	}
	  	break;
	  case BUILD:
	  case HELP_BUILD:
    {
      // current order is weird, the BUILD_MODULE mutates into a BUILD, and we
      // use the order.pos instead of order.psObj. also, might be BUILD if selecting
      // the module from the menu before clicking on the structure
      auto orderStructure = dynamic_cast<Structure*>(worldTile(order.pos)->psObject);
      if (orderStructure == structure &&
          (order.structure_stats.get() == &orderStructure->getStats() ||
           order.structure_stats.get() == getModuleStat(orderStructure))) {
        // ^^ order must be for this structure
        thisQueuedModule = nextModuleToBuild(structure, prevHighestQueuedModule);
      }
      break;
    }
    default:
      break;
	}
	return std::max(prevHighestQueuedModule, thisQueuedModule);
}

static int highestQueuedModule(Droid const* droid, Structure const* structure)
{
	auto module = highestQueuedModule(droid->getOrder(), structure, -1);
	for (unsigned n = droid->listPendingBegin; n < droid->asOrderList.size(); ++n)
	{
		module = highestQueuedModule(droid->asOrderList[n], structure, module);
	}
	return module;
}

/**
 * This function returns an order according to the droid,
 * object (target) and altOrder.
 */
Order chooseOrderObj(Droid* psDroid, SimpleObject* psObj, bool altOrder)
{
  using enum ORDER_TYPE;
	Order order {NONE};

	if (isTransporter(*psDroid)) {
		// in multiPlayer, need to be able to get transporter repaired
		if (bMultiPlayer) {
      auto psStruct = dynamic_cast<Structure*>(psObj);
			if (aiCheckAlliances(psObj->getPlayer(), psDroid->getPlayer()) && psObj) {

				ASSERT_OR_RETURN(Order(NONE), psObj != nullptr, "Invalid structure pointer");
				if (psStruct->getStats().type == STRUCTURE_TYPE::REPAIR_FACILITY &&
					psStruct->getState() == STRUCTURE_STATE::BUILT) {
					return {RTR_SPECIFIED, *psObj};
				}
			}
		}
		return Order(NONE);
	}

	if (altOrder &&
      (dynamic_cast<Droid*>(psObj) || dynamic_cast<Structure*>(psObj)) &&
      psDroid->getPlayer() == psObj->getPlayer()) {

		if (psDroid->getType() == DROID_TYPE::SENSOR) {
			return {OBSERVE, *psObj};
		}
		else if ((psDroid->getType() == DROID_TYPE::REPAIRER ||
              psDroid->getType() == DROID_TYPE::CYBORG_REPAIR) &&
              dynamic_cast<Droid*>(psObj)) {
			return {DROID_REPAIR, *psObj};
		}
		else if ((psDroid->getType() == DROID_TYPE::WEAPON) || isCyborg(psDroid) ||
             (psDroid->getType() == DROID_TYPE::COMMAND)) {
			return {ATTACK, *psObj};
		}
	}

  auto psFeat = dynamic_cast<Feature*>(psObj);
	// check for transporters first
	if (dynamic_cast<Droid*>(psObj) &&
      isTransporter(*dynamic_cast<Droid*>(psObj)) &&
      psObj->getPlayer() == psDroid->getPlayer()) {
		order = Order(EMBARK, *psObj);
	}
	// go to recover an artifact/oil drum - don't allow VTOLs to get this order
	else if (psFeat &&
		       (psFeat->getStats()->subType == FEATURE_TYPE::GEN_ARTE ||
           (psFeat->getStats()->subType == FEATURE_TYPE::OIL_DRUM))) {
		if (!psDroid->isVtol()) {
			order = Order(RECOVER, *psObj);
		}
	}
	// else default to attack if the droid has a weapon
	else if (numWeapons(*psDroid) > 0 &&
           psObj->getPlayer() != psDroid->getPlayer() &&
           !aiCheckAlliances(psObj->getPlayer(), psDroid->getPlayer())) {
		// check valid weapon/prop combination
		for (int i = 0; i < MAX_WEAPONS; ++i)
		{
			if (validTarget(psDroid, psObj, i)) {
				order = Order(ATTACK, *psObj);
				break;
			}
		}
	}
	else if (psDroid->getType() == DROID_TYPE::SENSOR
		&& psObj->getPlayer() != psDroid->getPlayer()
		&& !aiCheckAlliances(psObj->getPlayer(), psDroid->getPlayer())) {
		// check for standard sensor or VTOL intercept sensor
		if (asSensorStats[psDroid->asBits[COMP_SENSOR]].type == SENSOR_TYPE::STANDARD
			|| asSensorStats[psDroid->asBits[COMP_SENSOR]].type == SENSOR_TYPE::VTOL_INTERCEPT
			|| asSensorStats[psDroid->asBits[COMP_SENSOR]].type == SENSOR_TYPE::SUPER) {
			// a sensor droid observing an object
			order = Order(OBSERVE, *psObj);
		}
	}
	else if (droidSensorDroidWeapon(psObj, psDroid)) {
		// got an indirect weapon droid or vtol doing fire support
		order = Order(FIRE_SUPPORT, *psObj);
		setSensorAssigned();
	}
	else if (psObj->getPlayer() == psDroid->getPlayer() &&
		       dynamic_cast<Droid*>(psObj) &&
           dynamic_cast<Droid*>(psObj)->getType() == DROID_TYPE::COMMAND &&
           psDroid->getType() != DROID_TYPE::COMMAND &&
           psDroid->getType() != DROID_TYPE::CONSTRUCT &&
           psDroid->getType() != DROID_TYPE::CYBORG_CONSTRUCT) {

		// get a droid to join a command droid's group
		DeSelectDroid(psDroid);
		order = Order(COMMANDER_SUPPORT, *psObj);
	}
	// repair droid
	else if (aiCheckAlliances(psObj->getPlayer(), psDroid->getPlayer()) &&
           dynamic_cast<Droid*>(psObj) &&
           (psDroid->getType() == DROID_TYPE::REPAIRER ||
            psDroid->getType() == DROID_TYPE::CYBORG_REPAIR) &&
           droidIsDamaged(dynamic_cast<Droid*>(psObj))) {

		order = Order(DROID_REPAIR, *psObj);
	}
	// guarding constructor droids
	else if (aiCheckAlliances(psObj->getPlayer(), psDroid->getPlayer()) &&
		       dynamic_cast<Droid*>(psObj) &&
           (dynamic_cast<Droid*>(psObj)->getType() == DROID_TYPE::CONSTRUCT ||
            dynamic_cast<Droid*>(psObj)->getType() == DROID_TYPE::CYBORG_CONSTRUCT ||
            dynamic_cast<Droid*>(psObj)->getType() == DROID_TYPE::SENSOR ||
            dynamic_cast<Droid*>(psObj)->getType() == DROID_TYPE::COMMAND) &&
           psObj->getPlayer() != psDroid->getPlayer() &&
           (psDroid->getType() == DROID_TYPE::WEAPON ||
            psDroid->getType() == DROID_TYPE::CYBORG ||
            psDroid->getType() == DROID_TYPE::CYBORG_SUPER) &&
           proj_Direct(&psDroid->getWeapons()[0].getStats())) {

		order = Order(GUARD, *psObj);
		assignSensorTarget(psObj);
		psDroid->selected = false;
	}
	else if (aiCheckAlliances(psObj->getPlayer(), psDroid->getPlayer()) &&
	         dynamic_cast<Structure*>(psObj)) {
		auto psStruct = dynamic_cast<Structure*>(psObj);
		ASSERT_OR_RETURN(Order(NONE), psObj != nullptr, "Invalid structure pointer");

		// check whether construction droid
		if (psDroid->getType() == DROID_TYPE::CONSTRUCT ||
        psDroid->getType() == DROID_TYPE::CYBORG_CONSTRUCT) {

			auto moduleIndex =
				nextModuleToBuild(psStruct, ctrlShiftDown()
        ? highestQueuedModule(psDroid, psStruct)
        : -1);

			// re-written to allow the demolish order to be added to the queuing system
			if (intDemolishSelectMode() && psObj->getPlayer() == psDroid->getPlayer()) {
				// check to see if anything is currently trying to build the structure
				// -- can't build and demolish at the same time!
				if (psStruct->getState() == STRUCTURE_STATE::BUILT ||
            !checkDroidsBuilding(psStruct)) {
					order = Order(DEMOLISH, *psObj);
				}
			}
			// check for incomplete structures
			else if (psStruct->getState() != STRUCTURE_STATE::BUILT) {
				// if something else is demolishing, then help demolish
				if (checkDroidsDemolishing(psStruct)) {
					order = Order(DEMOLISH, *psObj);
				}
				// else help build
				else {
					order = Order(HELP_BUILD, *psObj);
					if (moduleIndex > 0) {
             // try scheduling a module instead
						order = Order(BUILD_MODULE, *psObj, moduleIndex);
					}
				}
			}
			else if (psStruct->getHp() < structureBody(psStruct)) {
				order = Order(REPAIR, *psObj);
			}
			// check if we can build a module
			else if (moduleIndex > 0) {
				order = Order(BUILD_MODULE, *psObj, moduleIndex);
			}
		}

		if (order.type == NONE) {
			// check repair facility and in need of repair
			if (psStruct->getStats().type == STRUCTURE_TYPE::REPAIR_FACILITY &&
				psStruct->getState() == STRUCTURE_STATE::BUILT) {
				order = Order{RTR_SPECIFIED, *psObj};
			}
			else if (electronicDroid(psDroid) &&
				psStruct->getResistance() < (int)structureResistance(
                &psStruct->getStats(), psStruct->getPlayer())) {
				order = Order(RESTORE, *psObj);
			}
			// check for counter battery assignment
			else if (structSensorDroidWeapon(psStruct, psDroid)) {
				order = Order(FIRE_SUPPORT, *psObj);
				// inform display system
				setSensorAssigned();
				// deselect droid
				DeSelectDroid(psDroid);
			}
			// rearm vtols
			else if (psDroid->isVtol()) {
				// check if rearm pad (default to no order)
				if (psStruct->getStats().type == STRUCTURE_TYPE::REARM_PAD) {
					// don't bother checking since we want it to go there if directed
					order = Order(REARM, *psObj);
				}
			}
			// some droids shouldn't be guarding
			else if ((psDroid->getType() == DROID_TYPE::WEAPON ||
                psDroid->getType() == DROID_TYPE::CYBORG ||
                psDroid->getType() == DROID_TYPE::CYBORG_SUPER) &&
               proj_Direct(&psDroid->getWeapons()[0].getStats())) {
				order = Order(GUARD, *psObj);
			}
		}
	}
	return order;
}

/** This function runs through all the player's droids and if the droid is vtol and is selected and is attacking, uses audio_QueueTrack() to play a sound.
 * @todo this function has variable psObj unused. Consider removing it from argument.
 * @todo this function runs through all the player's droids, but only uses the selected ones. Consider an efficiency improvement in here.
 * @todo current scope of this function is quite small. Consider refactoring it.
 */
static void orderPlayOrderObjAudio(unsigned player, SimpleObject* psObj)
{
	ASSERT_PLAYER_OR_RETURN(, player);

	/* loop over selected droids */
	for (auto& psDroid : apsDroidLists[player])
	{
		if (psDroid.selected) {
			/* currently only looks for VTOL */
			if (psDroid.isVtol()) {
				switch (psDroid.getOrder().type) {
          case ORDER_TYPE::ATTACK:
					audio_QueueTrack(ID_SOUND_ON_OUR_WAY2);
					break;
				default:
					break;
				}
			}
			/* only play audio once */
			break;
		}
	}
}

/** This function sends orders to all the selected droids according to the object.
 * If add is true, the orders are queued.
 * @todo this function runs through all the player's droids, but only uses the selected ones. Consider an efficiency improvement in here.
 */
void orderSelectedObjAdd(unsigned player, SimpleObject* psObj, bool add)
{
	ASSERT_PLAYER_OR_RETURN(, player);

	// note that an order list graphic needs to be displayed
	bOrderEffectDisplayed = false;

	for (auto& psCurr : apsDroidLists[player])
	{
		if (psCurr.selected) {
			if (isBlueprint(psObj)) {
				if (isConstructionDroid(&psCurr)) {
          // help build the planned structure.
          orderDroidStatsLocDirAdd(&psCurr, ORDER_TYPE::BUILD,
                                   dynamic_cast<Structure*>(psObj)->getStats(),
                                   psObj->getPosition().x, psObj->getPosition().y,
                                   dynamic_cast<Structure *>(psObj)->getRotation().direction,
                                   add);
        }
				else {
					// help watch the structure being built.
					orderDroidLocAdd(&psCurr, ORDER_TYPE::MOVE,
                           psObj->getPosition().x,
                           psObj->getPosition().y, add);
				}
				continue;
			}

			auto order = chooseOrderObj(&psCurr, psObj, specialOrderKeyDown());
			// see if the order can be added to the list
			if (order.type != ORDER_TYPE::NONE && !orderDroidObjAdd(&psCurr, order, add)) {
				// if not just do it straight off
				orderDroidObj(&psCurr, order.type, order.target, ModeQueue);
			}
		}
	}
	orderPlayOrderObjAudio(player, psObj);
}

/** This function just calls orderSelectedObjAdd with add = false.*/
void orderSelectedObj(unsigned player, SimpleObject* psObj)
{
	ASSERT_PLAYER_OR_RETURN(, player);
	orderSelectedObjAdd(player, psObj, false);
}

/** Given a player, this function send an order with localization and status to selected droids.
 * If add is true, the orders are queued.
 * @todo this function runs through all the player's droids, but only uses the selected ones and the ones that are construction droids. Consider an efficiency improvement.
 */
void orderSelectedStatsLocDir(unsigned player, ORDER_TYPE order, StructureStats* psStats,
                              unsigned x, unsigned y, uint16_t direction, bool add)
{
	ASSERT_PLAYER_OR_RETURN(, player);

	for (auto& psCurr : apsDroidLists[player])
	{
		if (psCurr.selected && isConstructionDroid(&psCurr)) {
			if (add) {
				orderDroidStatsLocDirAdd(&psCurr, order, psStats, x, y, direction);
			}
			else {
				orderDroidStatsLocDir(&psCurr, order, psStats, x, y, direction, ModeQueue);
			}
		}
	}
}

/** Same as orderSelectedStatsLocDir() but with two locations.
 * @todo this function runs through all the player's droids, but only uses the selected ones. Consider an efficiency improvement.
 */
void orderSelectedStatsTwoLocDir(unsigned player, ORDER_TYPE order, StructureStats* psStats,
                                 unsigned x1, unsigned y1, unsigned x2, unsigned y2,
                                 uint16_t direction, bool add)
{
	ASSERT_PLAYER_OR_RETURN(, player);

	for (auto& psCurr : apsDroidLists[player])
	{
		if (psCurr.selected) {
			if (add) {
				orderDroidStatsTwoLocDirAdd(&psCurr, order, psStats, x1, y1, x2, y2, direction);
			}
			else {
				orderDroidStatsTwoLocDir(&psCurr, order, psStats, x1, y1, x2, y2, direction, ModeQueue);
			}
		}
	}
}

/**
 * This function runs though all player's droids to check if any of them
 * is a transporter. Returns the transporter droid if any was found, and NULL else.*/
Droid* FindATransporter(Droid const* embarkee)
{
	auto isCyborg_ = isCyborg(embarkee);
	Droid* bestDroid = nullptr;
	auto bestDist = ~0u;

	for (auto& psDroid : apsDroidLists[embarkee->getPlayer()])
	{
		if ((isCyborg_ && psDroid.getType() == DROID_TYPE::TRANSPORTER) ||
        psDroid.getType() == DROID_TYPE::SUPER_TRANSPORTER) {

			auto dist = iHypot((psDroid.getPosition() - embarkee->getPosition()).xy());

			if (!checkTransporterSpace(&psDroid, embarkee, false)) {
				dist += 0x8000000; // Should prefer transports that aren't full.
			}
			if (dist < bestDist) {
				bestDroid = &psDroid;
				bestDist = dist;
			}
		}
	}
	return bestDroid;
}

/** Given a factory type, this function runs though all player's structures to check if any is of factory type. Returns the structure if any was found, and NULL else.*/
static Structure* FindAFactory(unsigned player, STRUCTURE_TYPE factoryType)
{
	ASSERT_PLAYER_OR_RETURN(nullptr, player);

	for (auto& psStruct : apsStructLists[player])
	{
		if (psStruct->getStats().type == factoryType) {
			return psStruct.get();
		}
	}
	return nullptr;
}

/** This function runs though all player's structures to check if any of then is a repair facility. Returns the structure if any was found, and NULL else.*/
static Structure* FindARepairFacility(unsigned player)
{
	ASSERT_PLAYER_OR_RETURN(nullptr, player);

	for (auto& psStruct : apsStructLists[player])
	{
		if (psStruct->getStats().type == STRUCTURE_TYPE::REPAIR_FACILITY) {
			return psStruct.get();
		}
	}
	return nullptr;
}

/** This function returns true if the droid supports the secondary order, and false if not.*/
bool secondarySupported(Droid* psDroid, SECONDARY_ORDER sec)
{
  // default to supported.
	auto supported = bool{true};

  using enum SECONDARY_ORDER;
	switch (sec) {
	case ASSIGN_PRODUCTION:
	case ASSIGN_CYBORG_PRODUCTION:
	case ASSIGN_VTOL_PRODUCTION:
	case CLEAR_PRODUCTION: // remove production from a command droid
	case FIRE_DESIGNATOR:
		if (psDroid->getType() != DROID_TYPE::COMMAND) {
			supported = false;
		}
		if ((sec == ASSIGN_PRODUCTION &&
         FindAFactory(psDroid->getPlayer(),
                      STRUCTURE_TYPE::FACTORY) == nullptr) ||

        (sec == ASSIGN_CYBORG_PRODUCTION &&
         FindAFactory(psDroid->getPlayer(),
                      STRUCTURE_TYPE::CYBORG_FACTORY) == nullptr) ||

			  (sec == ASSIGN_VTOL_PRODUCTION &&
         FindAFactory(psDroid->getPlayer(),
                      STRUCTURE_TYPE::VTOL_FACTORY) == nullptr)) {

			supported = false;
		}
	  // don't allow factories to be assigned to commanders during a Limbo Expand mission
		if ((sec == ASSIGN_PRODUCTION || sec == ASSIGN_CYBORG_PRODUCTION ||
         sec == ASSIGN_VTOL_PRODUCTION) && missionLimboExpand()) {
			supported = false;
		}
		break;

	case ATTACK_RANGE:
		if (psDroid->getType() == DROID_TYPE::SENSOR) {
			supported = false;
		}
    // don't show the range levels if the droid doesn't have a weapon with different ranges
		if (numWeapons(*psDroid) > 0) {
			for (auto i = 0; i < numWeapons(*psDroid); ++i)
			{
				const auto& weaponStats = psDroid->getWeapons()[i].getStats();

				if (proj_GetLongRange(&weaponStats, psDroid->getPlayer()) ==
              proj_GetShortRange(&weaponStats, psDroid->getPlayer())) {
					supported = false;
				}
				else {
					supported = true;
					break;
				}
			}
		}
	case ATTACK_LEVEL:
		if (psDroid->getType() == DROID_TYPE::REPAIRER ||
        psDroid->getType() == DROID_TYPE::CYBORG_REPAIR) {
			supported = false;
		}
		if (psDroid->getType() == DROID_TYPE::CONSTRUCT ||
        psDroid->getType() == DROID_TYPE::CYBORG_CONSTRUCT) {
			supported = false;
		}
		if (psDroid->getType() == DROID_TYPE::ECM ||
        objRadarDetector(psDroid)) {
			supported = false;
		}
		break;
	case CIRCLE:
		if (!psDroid->isVtol()) {
			supported = false;
		}
		break;
	case REPAIR_LEVEL:
	case PATROL:
	case HALT_TYPE:
	case RETURN_TO_LOCATION:
		break;
	case RECYCLE: // only if player has got a factory.
		if ((FindAFactory(psDroid->getPlayer(),
                      STRUCTURE_TYPE::FACTORY) == nullptr) &&
		  	(FindAFactory(psDroid->getPlayer(),
                    STRUCTURE_TYPE::CYBORG_FACTORY) == nullptr) &&
		  	(FindAFactory(psDroid->getPlayer(),
                    STRUCTURE_TYPE::VTOL_FACTORY) == nullptr) &&
		  	(FindARepairFacility(psDroid->getPlayer()) == nullptr)) {
			supported = false;
		}
		break;
	default:
		supported = false;
		break;
	}
	return supported;
}

#ifdef DEBUG
static char* secondaryPrintFactories(unsigned state)
{
	static char aBuff[255];

	memset(aBuff, 0, sizeof(aBuff));
	for (int i = 0; i < 5; i++)
	{
		if (state & (1 << (i + DSS_ASSPROD_SHIFT)))
		{
			aBuff[i] = (char)('0' + i);
		}
		else
		{
			aBuff[i] = ' ';
		}
		if (state & (1 << (i + DSS_ASSPROD_CYBORG_SHIFT)))
		{
			aBuff[i * 2 + 5] = 'c';
			aBuff[i * 2 + 6] = (char)('0' + i);
		}
		else
		{
			aBuff[i * 2 + 5] = ' ';
			aBuff[i * 2 + 6] = ' ';
		}
	}

	return aBuff;
}
#else
#define secondaryPrintFactories(x)
#endif

/** This function returns true if the droid needs repair according to the repair state, and in case there are some other droids selected, deselect those that are going to repair.
 * @todo there is some problem related with the values of REPAIRLEV_HIGH and REPAIRLEV_LOW that needs to be fixed.
 */
static bool secondaryCheckDamageLevelDeselect(Droid* psDroid, SECONDARY_STATE repairState)
{
	unsigned repairLevel;
	switch (repairState)
	{
	case DSS_REPLEV_LOW: repairLevel = REPAIRLEV_HIGH;
		break; // LOW → HIGH, seems DSS_REPLEV_LOW and DSS_REPLEV_HIGH are badly named?
	case DSS_REPLEV_HIGH: repairLevel = REPAIRLEV_LOW;
		break;
	default:
	case DSS_REPLEV_NEVER: repairLevel = 0;
		break;
	}

	// psDroid->body / psDroid->originalBody < repairLevel / 100, without integer truncation
  if (psDroid->getHp() * 100 > repairLevel * psDroid->getOriginalHp()) {
    return false;
  }
  // only deselect the droid if there is another droid selected.
  if (!psDroid->selected || selectedPlayer >= MAX_PLAYERS) {
    return true;
  }
  for (auto& psTempDroid : apsDroidLists[selectedPlayer])
  {
    if (&psTempDroid != psDroid && psTempDroid.selected) {
      DeSelectDroid(psDroid);
      break;
    }
  }
  return true;
}

/** This function checks the droid damage level against its secondary state. If the damage level is too high, then it sends an order to the droid to return to repair.*/
void secondaryCheckDamageLevel(Droid* psDroid)
{
	if (secondaryCheckDamageLevelDeselect(
          psDroid, secondaryGetState(
                  psDroid, SECONDARY_ORDER::REPAIR_LEVEL))) {
    
		if (!psDroid->isVtol()) {
			psDroid->group = UBYTE_MAX;
		}

		/* set return to repair if not on hold */
		if (psDroid->getOrder().type != ORDER_TYPE::RETURN_TO_REPAIR &&
			psDroid->getOrder().type != ORDER_TYPE::RETURN_TO_BASE &&
			!vtolRearming(*psDroid)) {
      
			if (psDroid->isVtol()) {
				moveToRearm(psDroid);
			}
			else {
				RtrBestResult result = decideWhereToRepairAndBalance(psDroid);
				if (result.type == RTR_DATA_TYPE::REPAIR_FACILITY) {
					ASSERT(result.target, "RTR_FACILITY but target is null");
					orderDroidObj(psDroid, ORDER_TYPE::RETURN_TO_REPAIR, result.target, ModeImmediate);
					return;
				}
				else if (result.type == RTR_DATA_TYPE::HQ) {
					ASSERT(result.target, "RTR_DATA_TYPE::HQ but target is null");
					orderDroid(psDroid, ORDER_TYPE::RETURN_TO_BASE, ModeImmediate);
					return;
				}
				else if (result.type == RTR_DATA_TYPE::DROID) {
					ASSERT(result.target, "RTR_DROID but target is null");
					orderDroidObj(psDroid, ORDER_TYPE::RETURN_TO_REPAIR, result.target, ModeImmediate);
				}
			}
		}
	}
}

/** This function assigns all droids of the group to the state.
 * @todo this function runs through all the player's droids. Consider something more efficient to select a group.
 * @todo SECONDARY_STATE argument is called "state", which is not current style. Suggestion to change it to "pState".
 */
static void secondarySetGroupState(unsigned player, const Group& group, SECONDARY_ORDER sec, SECONDARY_STATE state)
{
	ASSERT_PLAYER_OR_RETURN(, player);

	for (auto& psCurr : apsDroidLists[player])
	{
		if (&psCurr.getGroup() == &group &&
			secondaryGetState(&psCurr, sec) != state) {
			secondarySetState(&psCurr, sec, state);
		}
	}
}


/** This function returns the average secondary state of a numerical group of a player.
 * @todo this function runs through all the player's droids. Consider something more efficient to select a group.
 * @todo this function uses a "local" define. Consider removing it, refactoring this function.
 */
static constexpr auto MAX_STATES = 5;
static SECONDARY_STATE secondaryGetAverageGroupState(unsigned player, const Group& group, unsigned mask)
{
	ASSERT_PLAYER_OR_RETURN(DSS_NONE, player);

	struct
	{
		unsigned state, num;
	} aStateCount[MAX_STATES];

	// count the number of units for each state
	auto numStates = 0;
  auto i = 0;
	memset(aStateCount, 0, sizeof(aStateCount));
	for (auto& psCurr : apsDroidLists[player])
	{
		if (&psCurr.getGroup() == &group) {
			for (i = 0; i < numStates; i++)
			{
				if (aStateCount[i].state == (psCurr.getSecondaryOrder() & mask)) {
					aStateCount[i].num += 1;
					break;
				}
			}

			if (i == numStates) {
				aStateCount[numStates].state = psCurr.getSecondaryOrder() & mask;
				aStateCount[numStates].num = 1;
				numStates += 1;
			}
		}
	}

	auto max = 0;
	for (i = 0; i < numStates; i++)
	{
		if (aStateCount[i].num > aStateCount[max].num) {
			max = i;
		}
	}
	return static_cast<SECONDARY_STATE>(aStateCount[max].state);
}

/** This function sets all the group's members to have the same secondary state as the average secondary state of the group.
 * @todo this function runs through all the player's droids. Consider something more efficient to select a group.
 * @todo this function uses a "local" define. Consider removing it, refactoring this function.
 */
static constexpr auto MAX_ORDERS = 4;
void secondarySetAverageGroupState(unsigned player, const Group& group)
{
	ASSERT_PLAYER_OR_RETURN(, player);

  using enum SECONDARY_ORDER;
	// lookup table for orders and masks
	struct
	{
		SECONDARY_ORDER order;
		unsigned mask;
	} aOrders[MAX_ORDERS] =
		{
			{ATTACK_RANGE, DSS_ARANGE_MASK},
			{REPAIR_LEVEL, DSS_REPLEV_MASK},
			{ATTACK_LEVEL, DSS_ALEV_MASK},
			{HALT_TYPE, DSS_HALT_MASK},
		};

	for (auto& aOrder : aOrders)
	{
		auto state = secondaryGetAverageGroupState(
            player, group, aOrder.mask);

		secondarySetGroupState(player, group, aOrder.order,
                           static_cast<SECONDARY_STATE>(state));
	}
}

/**
 * lasSat structure can select a target
 */
void orderStructureObj(unsigned player, SimpleObject* psObj)
{
	ASSERT_PLAYER_OR_RETURN(, player);

	for (auto& psStruct : apsStructLists[player])
	{
		if (lasSatStructSelected(psStruct.get())) {
			// send the weapon fire
			sendLasSat(player, psStruct.get(), psObj);
			break;
		}
	}
}

/** This function maps the order enum to its name, returning its enum name as a (const char*)
 * Formally, this function is equivalent to a stl map: for a given key (enum), returns a mapped value (char*).
 */
std::string getDroidOrderName(ORDER_TYPE order)
{
  using enum ORDER_TYPE;
	switch (order) {
	case NONE: return "NONE";
	case STOP: return "STOP";
	case MOVE: return "MOVE";
	case ATTACK: return "ATTACK";
	case BUILD: return "BUILD";
	case HELP_BUILD: return "HELP_BUILD";
	case LINE_BUILD: return "LINE_BUILD";
	case DEMOLISH: return "DEMOLISH";
	case REPAIR: return "REPAIR";
	case OBSERVE: return "OBSERVE";
	case FIRE_SUPPORT: return "FIRE_SUPPORT";
	case RETURN_TO_BASE: return "RETURN_TO_BASE";
	case RETURN_TO_REPAIR: return "RETURN_TO_REPAIR";
	case EMBARK: return "EMBARK";
	case DISEMBARK: return "DISEMBARK";
	case ATTACK_TARGET: return "ATTACK_TARGET";
	case COMMANDER_SUPPORT: return "COMMANDER_SUPPORT";
	case BUILD_MODULE: return "BUILD_MODULE";
	case RECYCLE: return "RECYCLE";
	case TRANSPORT_OUT: return "TRANSPORT_OUT";
	case TRANSPORT_IN: return "TRANSPORT_IN";
	case TRANSPORT_RETURN: return "TRANSPORT_RETURN";
	case GUARD: return "GUARD";
	case DROID_REPAIR: return "DROID_REPAIR";
	case RESTORE: return "RESTORE";
	case SCOUT: return "SCOUT";
	case PATROL: return "PATROL";
	case REARM: return "REARM";
	case RECOVER: return "RECOVER";
	case RTR_SPECIFIED: return "RTR_SPECIFIED";
	case CIRCLE: return "CIRCLE";
	case HOLD: return "HOLD";
	};
	ASSERT(false, "DROID_ORDER out of range: %u", order);
	return "#INVALID#";
}

std::string getDroidOrderKey(ORDER_TYPE order)
{
  using enum ORDER_TYPE;
	switch (order) {
	case NONE: return "N";
	case STOP: return "Stop";
	case MOVE: return "M";
	case ATTACK: return "A";
	case BUILD: return "B";
	case HELP_BUILD: return "hB";
	case LINE_BUILD: return "lB";
	case DEMOLISH: return "D";
	case REPAIR: return "R";
	case OBSERVE: return "O";
	case FIRE_SUPPORT: return "F";
	case RETURN_TO_BASE: return "RTB";
	case RETURN_TO_REPAIR: return "RTR";
	case EMBARK: return "E";
	case DISEMBARK: return "!E";
	case ATTACK_TARGET: return "AT";
	case COMMANDER_SUPPORT: return "CS";
	case BUILD_MODULE: return "BM";
	case RECYCLE: return "RCY";
	case TRANSPORT_OUT: return "To";
	case TRANSPORT_IN: return "Ti";
	case TRANSPORT_RETURN: return "Tr";
	case GUARD: return "G";
	case DROID_REPAIR: return "DR";
	case RESTORE: return "RES";
	case SCOUT: return "S";
	case PATROL: return "P";
	case REARM: return "RE";
	case RECOVER: return "RCV";
	case RTR_SPECIFIED: return "RTR";
	case CIRCLE: return "C";
	case HOLD: return "H";
	};
	ASSERT(false, "DROID_ORDER out of range: %u", order);
	return "Err";
}
