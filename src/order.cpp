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

/** How long a droid runs after it fails do respond due to low moral. */
static constexpr auto RUN_TIME = 8000;

/** How long a droid runs burning after it fails do respond due to low moral. */
static constexpr auto RUN_BURN_TIME = 10000;

/** The distance a droid has in guard mode. */
static constexpr auto DEFEND_MAXDIST = TILE_UNITS * 3;

/** The distance a droid has in guard mode.
 * @todo seems to be used as equivalent to GUARD_MAXDIST.
 */
static constexpr auto DEFEND_BASEDIST = TILE_UNITS * 3;

/** The distance a droid has in guard mode. Equivalent to GUARD_MAXDIST,
 * but used for droids being on a command group. */
static constexpr auto DEFEND_CMD_MAXDIST = TILE_UNITS * 8;

/** The distance a droid has in guard mode. Equivalent to GUARD_BASEDIST, but used for droids being on a command group. */
#define DEFEND_CMD_BASEDIST		(TILE_UNITS * 5)

/** The maximum distance a constructor droid has in guard mode. */
#define CONSTRUCT_MAXDIST		(TILE_UNITS * 8)

/** The maximum distance allowed to a droid to move out of the path on a patrol/scout. */
#define SCOUT_DIST			(TILE_UNITS * 8)

/** The maximum distance allowed to a droid to move out of the path if already attacking a target on a patrol/scout. */
#define SCOUT_ATTACK_DIST	(TILE_UNITS * 5)

static void orderClearDroidList(Droid* psDroid);

/** Whether an order effect has been displayed
 * @todo better documentation required.
 */
static bool bOrderEffectDisplayed = false;

/** What the droid's action/order it is currently. This is used to debug purposes, jointly with showSAMPLES(). */
extern char DROIDDOING[512];
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
 * This function checks if the droid is off range. If yes, it uses
 * @return actionDroid() to make the droid to move to its target if its
 * target is on range, or to move to its order position if not.
 * @todo droid doesn't shoot while returning to the guard position.
 */
static void orderCheckGuardPosition(Droid* psDroid, int range)
{
	if (psDroid->getOrder().target)  {
		unsigned x, y;

		// repair droids always follow behind - don't want them jumping into the line of fire
		if ((!(psDroid->getType() == DROID_TYPE::REPAIRER ||
           psDroid->getType() == DROID_TYPE::CYBORG_REPAIR)) &&
         psDroid->getOrder().target->type == OBJ_DROID &&
         orderStateLoc((Droid*)psDroid->order.psObj,
                       ORDER_TYPE::MOVE, &x, &y))  {
			// got a moving droid - check against where the unit is going
			psDroid->order.pos = {x, y};
		} else  {
			psDroid->order.pos = psDroid->order.psObj->pos.xy();
		}
	}

	auto xdiff = psDroid->getPosition().x - psDroid->getOrder().pos.x;
	auto ydiff = psDroid->getPosition().y - psDroid->getOrder().pos.y;

	if (xdiff * xdiff + ydiff * ydiff > range * range)  {
		if ((psDroid->getMovementData().status != MOVE_STATUS::INACTIVE) &&
        ((psDroid->getAction() == ACTION::MOVE) ||
			   (psDroid->getAction() == ACTION::MOVE_FIRE))) {
      
			xdiff = psDroid->getMovementData().destination.x - psDroid->getOrder().pos.x;
			ydiff = psDroid->getMovementData().destination.y - psDroid->getOrder().pos.y;

			if (xdiff * xdiff + ydiff * ydiff > range * range)  {
				actionDroid(psDroid, ACTION::MOVE, psDroid->getOrder().pos.x, psDroid->getOrder().pos.y);
			}
		} else  {
			actionDroid(psDroid, ACTION::MOVE, psDroid->getOrder().pos.x, psDroid->getOrder().pos.y);
		}
	}
}

/**
 * This function checks if there are any damaged droids within a defined range.
 * It returns the damaged droid if there is any, or nullptr if none was found.
 */
Droid* checkForRepairRange(Droid* psDroid)
{
	const Droid* psFailedTarget;
	if (psDroid->getAction() == ACTION::SULK) {
		psFailedTarget = &dynamic_cast<const Droid&>(psDroid->getTarget(0));
	}

	ASSERT(psDroid->getType() == DROID_TYPE::REPAIRER ||
         psDroid->getType() == DROID_TYPE::CYBORG_REPAIR,
         "Invalid droid type");

	auto radius = ((psDroid->getOrder().type == ORDER_TYPE::HOLD) ||
                 (psDroid->getOrder().type == ORDER_TYPE::NONE &&
		                  secondaryGetState(psDroid, SECONDARY_ORDER::HALT_TYPE) == DSS_HALT_HOLD))
		                  ? REPAIR_RANGE
		                  : REPAIR_MAXDIST;

	auto bestDistanceSq = radius * radius;
	Droid* best = nullptr;

	for (SimpleObject* object : gridStartIterate(psDroid->getPosition().x,
                                               psDroid->getPosition().y,
                                               radius))
	{
		unsigned distanceSq = droidSqDist(psDroid, object);
		// droidSqDist returns -1 if unreachable, (unsigned)-1 is a big number.
		if (object == orderStateObj(psDroid, ORDER_TYPE::GUARD)) {
			distanceSq = 0; // If guarding a unit — always do that first.
		}

		auto droid = dynamic_cast<Droid*>(object);
		if (droid && // Must be a droid.
			  droid != psFailedTarget && // Must not have just failed to reach it.
		  	distanceSq <= bestDistanceSq && // Must be as close as possible.
		  	aiCheckAlliances(psDroid->getPlayer(), droid->getPlayer()) && // Must be a friendly droid.
		  	droidIsDamaged(droid) && // Must need repairing.
		  	visibleObject(psDroid, droid, false)) // Must be able to sense it.
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

	auto radius = ((psDroid->getOrder().type == ORDER_TYPE::HOLD) ||
                 (psDroid->getOrder().type == ORDER_TYPE::NONE &&
		              secondaryGetState(psDroid, SECONDARY_ORDER::HALT_TYPE) == DSS_HALT_HOLD))
		                  ? REPAIR_RANGE
		                  : REPAIR_MAXDIST;

	auto bestDistanceSq = radius * radius;
	std::pair<Structure*, ACTION> best = {nullptr, ACTION::NONE};

	for (SimpleObject* object : gridStartIterate(psDroid->getPosition().x, psDroid->getPosition().y, radius))
	{
		auto distanceSq = droidSqDist(psDroid, object);
		// droidSqDist returns -1 if unreachable, (unsigned)-1 is a big number.

		auto structure = dynamic_cast<Structure*>(object);
		if (structure == nullptr || // Must be a structure.
			  structure == psFailedTarget || // Must not have just failed to reach it.
			  distanceSq > bestDistanceSq || // Must be as close as possible.
			  !visibleObject(psDroid, structure, false) || // Must be able to sense it.
			  !aiCheckAlliances(psDroid->getPlayer(), structure->getPlayer()) || // Must be a friendly structure.
			  checkDroidsDemolishing(structure)) // Must not be trying to get rid of it.
		{
			continue;
		}

		// Check for structures to repair.
		if (structure->getState() == STRUCTURE_STATE::BUILT &&
        structIsDamaged(structure)) {
			bestDistanceSq = distanceSq;
			best = {structure, ACTION::REPAIR};
		}
		// Check for structures to help build.
		else if (structure->getState() == STRUCTURE_STATE::BEING_BUILT) {
			bestDistanceSq = distanceSq;
			best = {structure, ACTION::BUILD};
		}
	}
	return best;
}

static bool isRepairlikeAction(ACTION action)
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


/** This function updates all the orders status, according with psdroid's current order and state.
 */
void orderUpdateDroid(Droid* psDroid)
{
	SimpleObject* psObj = nullptr;
	Structure *psStruct, *psWall;
	SDWORD xdiff, ydiff;
	bool bAttack;
	SDWORD xoffset, yoffset;

	// clear the target if it has died
	if (psDroid->order.psObj && psDroid->order.psObj->died)
	{
		syncDebugObject(psDroid->order.psObj, '-');
		setDroidTarget(psDroid, nullptr);
		objTrace(psDroid->id, "Target dead");
	}

	//clear its base struct if its died
	if (psDroid->associated_structure && psDroid->associated_structure->died)
	{
		syncDebugStructure(psDroid->associated_structure, '-');
		setDroidBase(psDroid, nullptr);
		objTrace(psDroid->id, "Base struct dead");
	}

	// check for died objects in the list
	orderCheckList(psDroid);

	if (isDead(psDroid))
	{
		return;
	}

	switch (psDroid->order.type)
	{
	case NONE:
	case HOLD:
		// see if there are any orders queued up
		if (orderDroidList(psDroid))
		{
			// started a new order, quit
			break;
		}
		// if you are in a command group, default to guarding the commander
		else if (hasCommander(psDroid) && psDroid->order.type != HOLD
			&& psDroid->order.psStats != structGetDemolishStat())
		// stop the constructor auto repairing when it is about to demolish
		{
			orderDroidObj(psDroid, GUARD, psDroid->group->psCommander, ModeImmediate);
		}
		else if (isTransporter(psDroid) && !bMultiPlayer)
		{
		}
		// default to guarding
		else if (!tryDoRepairlikeAction(psDroid)
			&& psDroid->order.type != HOLD
			&& psDroid->order.psStats != structGetDemolishStat()
			&& secondaryGetState(psDroid, DSO_HALTTYPE) == DSS_HALT_GUARD
			&& !isVtolDroid(psDroid))
		{
			orderDroidLoc(psDroid, GUARD, psDroid->pos.x, psDroid->pos.y, ModeImmediate);
		}
		break;
	case TRANSPORTRETURN:
		if (psDroid->action == NONE)
		{
			missionMoveTransporterOffWorld(psDroid);

			/* clear order */
			psDroid->order = Order(NONE);
		}
		break;
	case TRANSPORTOUT:
		if (psDroid->action == NONE)
		{
			if (psDroid->player == selectedPlayer)
			{
				if (getDroidsToSafetyFlag())
				{
					//move droids in Transporter into holding list
					moveDroidsToSafety(psDroid);
					//we need the transporter to just sit off world for a while...
					orderDroid(psDroid, TRANSPORTIN, ModeImmediate);
					/* set action transporter waits for timer */
					actionDroid(psDroid, TRANSPORTWAITTOFLYIN);

					missionSetReinforcementTime(gameTime);
					//don't do this until waited for the required time
					//fly Transporter back to get some more droids
					//orderDroidLoc( psDroid, TRANSPORTIN,
					//getLandingX(selectedPlayer), getLandingY(selectedPlayer));
				}
				else
				{
					//the script can call startMission for this callback for offworld missions
					triggerEvent(TRIGGER_TRANSPORTER_EXIT, psDroid);
					/* clear order */
					psDroid->order = Order(NONE);
				}

				psDroid->movement.speed = 0;
				// Prevent radical movement vector when adjusting from home to away map exit and entry coordinates.
			}
		}
		break;
	case TRANSPORTIN:
		if ((psDroid->action == NONE) &&
			(psDroid->movement.status == MOVEINACTIVE))
		{
			/* clear order */
			psDroid->order = Order(NONE);

			//FFS! You only wan't to do this if the droid being tracked IS the transporter! Not all the time!
			// What about if your happily playing the game and tracking a droid, and re-enforcements come in!
			// And suddenly BLAM!!!! It drops you out of camera mode for no apparent reason! TOTALY CONFUSING
			// THE PLAYER!
			//
			// Just had to get that off my chest....end of rant.....
			//
			if (psDroid == getTrackingDroid()) // Thats better...
			{
				/* deselect transporter if have been tracking */
				if (getWarCamStatus())
				{
					camToggleStatus();
				}
			}

			DeSelectDroid(psDroid);

			/*don't try the unload if moving droids to safety and still got some
			droids left  - wait until full and then launch again*/
			if (psDroid->player == selectedPlayer && getDroidsToSafetyFlag() &&
				missionDroidsRemaining(selectedPlayer))
			{
				resetTransporter();
			}
			else
			{
				unloadTransporter(psDroid, psDroid->pos.x, psDroid->pos.y, false);
			}
		}
		break;
	case MOVE:
		// Just wait for the action to finish then clear the order
		if (psDroid->action == NONE || psDroid->action == ATTACK)
		{
			psDroid->order = Order(NONE);
		}
		break;
	case RECOVER:
		if (psDroid->order.psObj == nullptr)
		{
			psDroid->order = Order(NONE);
		}
		else if (psDroid->action == NONE)
		{
			// stopped moving, but still haven't got the artifact
			actionDroid(psDroid, MOVE, psDroid->order.psObj->pos.x, psDroid->order.psObj->pos.y);
		}
		break;
	case SCOUT:
	case PATROL:
		// if there is an enemy around, attack it
		if (psDroid->action == MOVE || psDroid->action == MOVEFIRE || (psDroid->action == NONE
			&& isVtolDroid(psDroid)))
		{
			bool tooFarFromPath = false;
			if (isVtolDroid(psDroid) && psDroid->order.type == PATROL)
			{
				// Don't stray too far from the patrol path - only attack if we're near it
				// A fun algorithm to detect if we're near the path
				Vector2i delta = psDroid->order.pos - psDroid->order.pos2;
				if (delta == Vector2i(0, 0))
				{
					tooFarFromPath = false;
				}
				else if (abs(delta.x) >= abs(delta.y) &&
					MIN(psDroid->order.pos.x, psDroid->order.pos2.x) - SCOUT_DIST <= psDroid->pos.x &&
					psDroid->pos.x <= MAX(psDroid->order.pos.x, psDroid->order.pos2.x) + SCOUT_DIST)
				{
					tooFarFromPath = (abs((psDroid->pos.x - psDroid->order.pos.x) * delta.y / delta.x +
						psDroid->order.pos.y - psDroid->pos.y) > SCOUT_DIST);
				}
				else if (abs(delta.x) <= abs(delta.y) &&
					MIN(psDroid->order.pos.y, psDroid->order.pos2.y) - SCOUT_DIST <= psDroid->pos.y &&
					psDroid->pos.y <= MAX(psDroid->order.pos.y, psDroid->order.pos2.y) + SCOUT_DIST)
				{
					tooFarFromPath = (abs((psDroid->pos.y - psDroid->order.pos.y) * delta.x / delta.y +
						psDroid->order.pos.x - psDroid->pos.x) > SCOUT_DIST);
				}
				else
				{
					tooFarFromPath = true;
				}
			}
			if (!tooFarFromPath)
			{
				// true if in condition to set actionDroid to attack/observe
				bool attack = secondaryGetState(psDroid, DSO_ATTACK_LEVEL) == DSS_ALEV_ALWAYS &&
					aiBestNearestTarget(psDroid, &psObj, 0, SCOUT_ATTACK_DIST) >= 0;
				switch (psDroid->type)
				{
				case DROID_CONSTRUCT:
				case DROID_CYBORG_CONSTRUCT:
				case DROID_REPAIR:
				case DROID_CYBORG_REPAIR:
					tryDoRepairlikeAction(psDroid);
					break;
				case DROID_WEAPON:
				case DROID_CYBORG:
				case DROID_CYBORG_SUPER:
				case DROID_PERSON:
				case DROID_COMMAND:
					if (attack)
					{
						actionDroid(psDroid, ATTACK, psObj);
					}
					break;
				case DROID_SENSOR:
					if (attack)
					{
						actionDroid(psDroid, OBSERVE, psObj);
					}
					break;
				default:
					actionDroid(psDroid, NONE);
					break;
				}
			}
		}
		if (psDroid->action == NONE)
		{
			xdiff = psDroid->pos.x - psDroid->order.pos.x;
			ydiff = psDroid->pos.y - psDroid->order.pos.y;
			if (xdiff * xdiff + ydiff * ydiff < SCOUT_DIST * SCOUT_DIST)
			{
				if (psDroid->order.type == PATROL)
				{
					// see if we have anything queued up
					if (orderDroidList(psDroid))
					{
						// started a new order, quit
						break;
					}
					if (isVtolDroid(psDroid) && !vtolFull(psDroid) && (psDroid->secondary_order & DSS_ALEV_MASK) !=
                                                            DSS_ALEV_NEVER)
					{
						moveToRearm(psDroid);
						break;
					}
					// head back to the other point
					std::swap(psDroid->order.pos, psDroid->order.pos2);
					actionDroid(psDroid, MOVE, psDroid->order.pos.x, psDroid->order.pos.y);
				}
				else
				{
					psDroid->order = Order(NONE);
				}
			}
			else
			{
				actionDroid(psDroid, MOVE, psDroid->order.pos.x, psDroid->order.pos.y);
			}
		}
		else if (((psDroid->action == ATTACK) ||
				(psDroid->action == VTOLATTACK) ||
				(psDroid->action == MOVETOATTACK) ||
				(psDroid->action == ROTATETOATTACK) ||
				(psDroid->action == OBSERVE) ||
				(psDroid->action == MOVETOOBSERVE)) &&
			secondaryGetState(psDroid, DSO_HALTTYPE) != DSS_HALT_PURSUE)
		{
			// attacking something - see if the droid has gone too far, go up to twice the distance we want to go, so that we don't repeatedly turn back when the target is almost in range.
			if (objPosDiffSq(psDroid->pos, Vector3i(psDroid->actionPos, 0)) > (SCOUT_ATTACK_DIST * 2 * SCOUT_ATTACK_DIST
				* 2))
			{
				actionDroid(psDroid, RETURNTOPOS, psDroid->actionPos.x, psDroid->actionPos.y);
			}
		}
		if (psDroid->order.type == PATROL && isVtolDroid(psDroid) && vtolEmpty(psDroid) && (psDroid->
			secondary_order & DSS_ALEV_MASK) != DSS_ALEV_NEVER)
		{
			moveToRearm(psDroid); // Completely empty (and we're not set to hold fire), don't bother patrolling.
			break;
		}
		break;
	case CIRCLE:
		// if there is an enemy around, attack it
		if (psDroid->action == MOVE &&
			secondaryGetState(psDroid, DSO_ATTACK_LEVEL) == DSS_ALEV_ALWAYS &&
			aiBestNearestTarget(psDroid, &psObj, 0, SCOUT_ATTACK_DIST) >= 0)
		{
			switch (psDroid->type)
			{
			case DROID_WEAPON:
			case DROID_CYBORG:
			case DROID_CYBORG_SUPER:
			case DROID_PERSON:
			case DROID_COMMAND:
				actionDroid(psDroid, ATTACK, psObj);
				break;
			case DROID_SENSOR:
				actionDroid(psDroid, OBSERVE, psObj);
				break;
			default:
				actionDroid(psDroid, NONE);
				break;
			}
		}
		else if (psDroid->action == NONE || psDroid->action == MOVE)
		{
			if (psDroid->action == MOVE)
			{
				// see if we have anything queued up
				if (orderDroidList(psDroid))
				{
					// started a new order, quit
					break;
				}
			}

			Vector2i edgeDiff = psDroid->pos.xy() - psDroid->actionPos;
			if (psDroid->action != MOVE || dot(edgeDiff, edgeDiff) <= TILE_UNITS * 4 * TILE_UNITS * 4)
			{
				//Watermelon:use orderX,orderY as local space origin and calculate droid direction in local space
				Vector2i diff = psDroid->pos.xy() - psDroid->order.pos;
				uint16_t angle = iAtan2(diff) - DEG(30);
				do
				{
					xoffset = iSinR(angle, 1500);
					yoffset = iCosR(angle, 1500);
					angle -= DEG(10);
				}
				while (!worldOnMap(psDroid->order.pos.x + xoffset, psDroid->order.pos.y + yoffset));
				// Don't try to fly off map.
				actionDroid(psDroid, MOVE, psDroid->order.pos.x + xoffset, psDroid->order.pos.y + yoffset);
			}

			if (isVtolDroid(psDroid) && vtolEmpty(psDroid) && (psDroid->secondary_order & DSS_ALEV_MASK) !=
                                                        DSS_ALEV_NEVER)
			{
				moveToRearm(psDroid); // Completely empty (and we're not set to hold fire), don't bother circling.
				break;
			}
		}
		else if (((psDroid->action == ATTACK) ||
				(psDroid->action == VTOLATTACK) ||
				(psDroid->action == MOVETOATTACK) ||
				(psDroid->action == ROTATETOATTACK) ||
				(psDroid->action == OBSERVE) ||
				(psDroid->action == MOVETOOBSERVE)) &&
			secondaryGetState(psDroid, DSO_HALTTYPE) != DSS_HALT_PURSUE)
		{
			// attacking something - see if the droid has gone too far
			xdiff = psDroid->pos.x - psDroid->actionPos.x;
			ydiff = psDroid->pos.y - psDroid->actionPos.y;
			if (xdiff * xdiff + ydiff * ydiff > 2000 * 2000)
			{
				// head back to the target location
				actionDroid(psDroid, RETURNTOPOS, psDroid->order.pos.x, psDroid->order.pos.y);
			}
		}
		break;
	case HELPBUILD:
	case DEMOLISH:
	case OBSERVE:
	case REPAIR:
	case DROIDREPAIR:
	case RESTORE:
		if (psDroid->action == NONE || psDroid->order.psObj == nullptr)
		{
			psDroid->order = Order(NONE);
			actionDroid(psDroid, NONE);
			if (psDroid->player == selectedPlayer)
			{
				intRefreshScreen();
			}
		}
		break;
	case REARM:
		if (psDroid->order.psObj == nullptr || psDroid->action_target[0] == nullptr)
		{
			// arm pad destroyed find another
			psDroid->order = Order(NONE);
			moveToRearm(psDroid);
		}
		else if (psDroid->action == NONE)
		{
			psDroid->order = Order(NONE);
		}
		break;
	case ATTACK:
	case ATTACKTARGET:
		if (psDroid->order.psObj == nullptr || psDroid->order.psObj->died)
		{
			// if vtol then return to rearm pad as long as there are no other
			// orders queued up
			if (isVtolDroid(psDroid))
			{
				if (!orderDroidList(psDroid))
				{
					psDroid->order = Order(NONE);
					moveToRearm(psDroid);
				}
			}
			else
			{
				psDroid->order = Order(NONE);
				actionDroid(psDroid, NONE);
			}
		}
		else if (((psDroid->action == MOVE) ||
				(psDroid->action == MOVEFIRE)) &&
			actionVisibleTarget(psDroid, psDroid->order.psObj, 0) && !isVtolDroid(psDroid))
		{
			// moved near enough to attack change to attack action
			actionDroid(psDroid, ATTACK, psDroid->order.psObj);
		}
		else if ((psDroid->action == MOVETOATTACK) &&
			!isVtolDroid(psDroid) &&
			!actionVisibleTarget(psDroid, psDroid->order.psObj, 0) &&
			secondaryGetState(psDroid, DSO_HALTTYPE) != DSS_HALT_HOLD)
		{
			// lost sight of the target while chasing it - change to a move action so
			// that the unit will fire on other things while moving
			actionDroid(psDroid, MOVE, psDroid->order.psObj->pos.x, psDroid->order.psObj->pos.y);
		}
		else if (!isVtolDroid(psDroid)
			&& psDroid->order.psObj == psDroid->action_target[0]
			&& actionInRange(psDroid, psDroid->order.psObj, 0)
			&& (psWall = visGetBlockingWall(psDroid, psDroid->order.psObj))
			&& !aiCheckAlliances(psWall->player, psDroid->player))
		{
			// there is a wall in the way - attack that
			actionDroid(psDroid, ATTACK, psWall);
		}
		else if ((psDroid->action == NONE) ||
			(psDroid->action == CLEARREARMPAD))
		{
			if ((psDroid->order.type == ATTACKTARGET || psDroid->order.type == ATTACK)
				&& secondaryGetState(psDroid, DSO_HALTTYPE) == DSS_HALT_HOLD
				&& !actionInRange(psDroid, psDroid->order.psObj, 0))
			{
				// target is not in range and DSS_HALT_HOLD: give up, don't move
				psDroid->order = Order(NONE);
			}
			else if (!isVtolDroid(psDroid) || allVtolsRearmed(psDroid))
			{
				actionDroid(psDroid, ATTACK, psDroid->order.psObj);
			}
		}
		break;
	case BUILD:
		if (psDroid->action == BUILD &&
			psDroid->order.psObj == nullptr)
		{
			psDroid->order = Order(NONE);
			actionDroid(psDroid, NONE);
			objTrace(psDroid->id, "Clearing build order since build target is gone");
		}
		else if (psDroid->action == NONE)
		{
			psDroid->order = Order(NONE);
			objTrace(psDroid->id, "Clearing build order since build action is reset");
		}
		break;
	case EMBARK:
		{
			// only place it can be trapped - in multiPlayer can only put cyborgs onto a Cyborg Transporter
			auto* temp = (Droid*)psDroid->order.psObj; // NOTE: It is possible to have a NULL here

			if (temp && temp->type == DROID_TRANSPORTER && !isCyborg(psDroid))
			{
				psDroid->order = Order(NONE);
				actionDroid(psDroid, NONE);
				if (psDroid->player == selectedPlayer)
				{
					audio_PlayBuildFailedOnce();
					addConsoleMessage(
						_("We can't do that! We must be a Cyborg unit to use a Cyborg Transport!"), DEFAULT_JUSTIFY,
						selectedPlayer);
				}
			}
			else
			{
				// Wait for the action to finish then assign to Transporter (if not already flying)
				if (psDroid->order.psObj == nullptr || transporterFlying((Droid*)psDroid->order.psObj))
				{
					psDroid->order = Order(NONE);
					actionDroid(psDroid, NONE);
				}
				else if (abs((SDWORD)psDroid->pos.x - (SDWORD)psDroid->order.psObj->pos.x) < TILE_UNITS
					&& abs((SDWORD)psDroid->pos.y - (SDWORD)psDroid->order.psObj->pos.y) < TILE_UNITS)
				{
					// save the target of current droid (the transporter)
					auto* transporter = (Droid*)psDroid->order.psObj;

					// Make sure that it really is a valid droid
					CHECK_DROID(transporter);

					// order the droid to stop so moveUpdateDroid does not process this unit
					orderDroid(psDroid, STOP, ModeImmediate);
					setDroidTarget(psDroid, nullptr);
					psDroid->order.psObj = nullptr;
					secondarySetState(psDroid, DSO_RETURN_TO_LOC, DSS_NONE);

					/* We must add the droid to the transporter only *after*
					* processing changing its orders (see above).
					*/
					transporterAddDroid(transporter, psDroid);
				}
				else if (psDroid->action == NONE)
				{
					actionDroid(psDroid, MOVE, psDroid->order.psObj->pos.x, psDroid->order.psObj->pos.y);
				}
			}
		}
	// Do we need to clear the secondary order "DSO_EMBARK" here? (PD)
		break;
	case DISEMBARK:
		//only valid in multiPlayer mode
		if (bMultiPlayer)
		{
			//this order can only be given to Transporter droids
			if (isTransporter(psDroid))
			{
				/*once the Transporter has reached its destination (and landed),
				get all the units to disembark*/
				if (psDroid->action != MOVE && psDroid->action != MOVEFIRE &&
            psDroid->movement.status == MOVEINACTIVE && psDroid->movement.vertical_speed == 0)
				{
					unloadTransporter(psDroid, psDroid->pos.x, psDroid->pos.y, false);
					//reset the transporter's order
					psDroid->order = Order(NONE);
				}
			}
		}
		break;
	case RTB:
		// Just wait for the action to finish then clear the order
		if (psDroid->action == NONE)
		{
			psDroid->order = Order(NONE);
			secondarySetState(psDroid, DSO_RETURN_TO_LOC, DSS_NONE);
		}
		break;
	case RTR:
	case RTR_SPECIFIED:
		if (psDroid->order.psObj == nullptr)
		{
			// Our target got lost. Let's try again.
			psDroid->order = Order(NONE);
			orderDroid(psDroid, RTR, ModeImmediate);
		}
		else if (psDroid->action == NONE)
		{
			/* get repair facility pointer */
			psStruct = (Structure*)psDroid->order.psObj;
			ASSERT(psStruct != nullptr,
			       "orderUpdateUnit: invalid structure pointer");


			if (objPosDiffSq(psDroid->pos, psDroid->order.psObj->pos) < (TILE_UNITS * 8) * (TILE_UNITS * 8))
			{
				/* action droid to wait */
				actionDroid(psDroid, WAITFORREPAIR);
			}
			else
			{
				// move the droid closer to the repair point
				// setting target to null will trigger search for nearest repair point: we might have a better option after all
				psDroid->order.psObj = nullptr;
			}
		}
		break;
	case LINEBUILD:
		if (psDroid->action == NONE ||
			(psDroid->action == BUILD && psDroid->order.psObj == nullptr))
		{
			// finished building the current structure
			auto lb = calcLineBuild(psDroid->order.psStats, psDroid->order.direction, psDroid->order.pos,
			                        psDroid->order.pos2);
			if (lb.count <= 1)
			{
				// finished all the structures - done
				psDroid->order = Order(NONE);
				break;
			}

			// update the position for another structure
			psDroid->order.pos = lb[1];

			// build another structure
			setDroidTarget(psDroid, nullptr);
			actionDroid(psDroid, BUILD, psDroid->order.pos.x, psDroid->order.pos.y);
			//intRefreshScreen();
		}
		break;
	case FIRESUPPORT:
		if (psDroid->order.psObj == nullptr)
		{
			psDroid->order = Order(NONE);
			if (isVtolDroid(psDroid) && !vtolFull(psDroid))
			{
				moveToRearm(psDroid);
			}
			else
			{
				actionDroid(psDroid, NONE);
			}
		}
		//before targetting - check VTOL's are fully armed
		else if (vtolEmpty(psDroid))
		{
			moveToRearm(psDroid);
		}
		//indirect weapon droid attached to (standard)sensor droid
		else
		{
			SimpleObject* psFireTarget = nullptr;

			if (psDroid->order.psObj->type == OBJ_DROID)
			{
				auto* psSpotter = (Droid*)psDroid->order.psObj;

				if (psSpotter->action == OBSERVE
					|| (psSpotter->type == DROID_COMMAND && psSpotter->action == ATTACK))
				{
					psFireTarget = psSpotter->action_target[0];
				}
			}
			else if (psDroid->order.psObj->type == OBJ_STRUCTURE)
			{
				auto* psSpotter = (Structure*)psDroid->order.psObj;

				psFireTarget = psSpotter->psTarget[0];
			}

			if (psFireTarget && !psFireTarget->died && checkAnyWeaponsTarget(psDroid, psFireTarget))
			{
				bAttack = false;
				if (isVtolDroid(psDroid))
				{
					if (!vtolEmpty(psDroid) &&
						((psDroid->action == MOVETOREARM) ||
							(psDroid->action == WAITFORREARM)) &&
						(psDroid->movement.status != MOVEINACTIVE))
					{
						// catch vtols that were attacking another target which was destroyed
						// get them to attack the new target rather than returning to rearm
						bAttack = true;
					}
					else if (allVtolsRearmed(psDroid))
					{
						bAttack = true;
					}
				}
				else
				{
					bAttack = true;
				}

				//if not currently attacking or target has changed
				if (bAttack &&
					(!droidAttacking(psDroid) ||
						psFireTarget != psDroid->action_target[0]))
				{
					//get the droid to attack
					actionDroid(psDroid, ATTACK, psFireTarget);
				}
			}
			else if (isVtolDroid(psDroid) &&
				!vtolFull(psDroid) &&
				(psDroid->action != NONE) &&
				(psDroid->action != FIRESUPPORT))
			{
				moveToRearm(psDroid);
			}
			else if ((psDroid->action != FIRESUPPORT) &&
				(psDroid->action != FIRESUPPORT_RETREAT))
			{
				actionDroid(psDroid, FIRESUPPORT, psDroid->order.psObj);
			}
		}
		break;
	case RECYCLE:
		if (psDroid->order.psObj == nullptr)
		{
			psDroid->order = Order(NONE);
			actionDroid(psDroid, NONE);
		}
		else if (actionReachedBuildPos(psDroid, psDroid->order.psObj->pos.x, psDroid->order.psObj->pos.y,
		                               ((Structure*)psDroid->order.psObj)->rot.direction,
		                               ((Structure*)psDroid->order.psObj)->pStructureType))
		{
			recycleDroid(psDroid);
		}
		else if (psDroid->action == NONE)
		{
			actionDroid(psDroid, MOVE, psDroid->order.psObj->pos.x, psDroid->order.psObj->pos.y);
		}
		break;
	case GUARD:
		if (orderDroidList(psDroid))
		{
			// started a queued order - quit
			break;
		}
		else if ((psDroid->action == NONE) ||
			(psDroid->action == MOVE) ||
			(psDroid->action == MOVEFIRE))
		{
			// not doing anything, make sure the droid is close enough
			// to the thing it is defending
			if ((!(psDroid->type == DROID_REPAIR || psDroid->type == DROID_CYBORG_REPAIR))
				&& psDroid->order.psObj != nullptr && psDroid->order.psObj->type == OBJ_DROID
				&& ((Droid*)psDroid->order.psObj)->type == DROID_COMMAND)
			{
				// guarding a commander, allow more space
				orderCheckGuardPosition(psDroid, DEFEND_CMD_BASEDIST);
			}
			else
			{
				orderCheckGuardPosition(psDroid, DEFEND_BASEDIST);
			}
		}
		else if (psDroid->type == DROID_REPAIR || psDroid->type == DROID_CYBORG_REPAIR)
		{
			// repairing something, make sure the droid doesn't go too far
			orderCheckGuardPosition(psDroid, REPAIR_MAXDIST);
		}
		else if (psDroid->type == DROID_CONSTRUCT || psDroid->type == DROID_CYBORG_CONSTRUCT)
		{
			// repairing something, make sure the droid doesn't go too far
			orderCheckGuardPosition(psDroid, CONSTRUCT_MAXDIST);
		}
		else if (isTransporter(psDroid))
		{
		}
		else
		{
			//let vtols return to rearm
			if (!vtolRearming(psDroid))
			{
				// attacking something, make sure the droid doesn't go too far
				if (psDroid->order.psObj != nullptr && psDroid->order.psObj->type == OBJ_DROID &&
            ((Droid*)psDroid->order.psObj)->type == DROID_COMMAND)
				{
					// guarding a commander, allow more space
					orderCheckGuardPosition(psDroid, DEFEND_CMD_MAXDIST);
				}
				else
				{
					orderCheckGuardPosition(psDroid, DEFEND_MAXDIST);
				}
			}
		}

	// get combat units in a command group to attack the commanders target
		if (hasCommander(psDroid) && (psDroid->numWeaps > 0))
		{
			if (psDroid->group->psCommander->action == ATTACK &&
          psDroid->group->psCommander->actionTarget[0] != nullptr &&
          !psDroid->group->psCommander->actionTarget[0]->died)
			{
				psObj = psDroid->group->psCommander->actionTarget[0];
				if (psDroid->action == ATTACK ||
					psDroid->action == MOVETOATTACK)
				{
					if (psDroid->action_target[0] != psObj)
					{
						actionDroid(psDroid, ATTACK, psObj);
					}
				}
				else if (psDroid->action != MOVE)
				{
					actionDroid(psDroid, ATTACK, psObj);
				}
			}

			// make sure units in a command group are actually guarding the commander
			psObj = orderStateObj(psDroid, GUARD); // find out who is being guarded by the droid
			if (psObj == nullptr
				|| psObj != psDroid->group->psCommander)
			{
				orderDroidObj(psDroid, GUARD, psDroid->group->psCommander, ModeImmediate);
			}
		}

		tryDoRepairlikeAction(psDroid);
		break;
	default:
		ASSERT(false, "orderUpdateUnit: unknown order");
	}

	// catch any vtol that is rearming but has finished his order
	if (psDroid->order.type == NONE && vtolRearming(psDroid)
		&& (psDroid->action_target[0] == nullptr || !psDroid->action_target[0]->died))
	{
		psDroid->order = Order(REARM, psDroid->action_target[0]);
	}

	if (psDroid->selected)
	{
		// Tell us what the droid is doing.
		snprintf(DROIDDOING, sizeof(DROIDDOING), "%.12s,id(%d) order(%d):%s action(%d):%s secondary:%x move:%s",
		         droidGetName(psDroid), psDroid->getId(),
		         psDroid->order.type, getDroidOrderName(psDroid->order.type), psDroid->action,
		         getDroidActionName(psDroid->action), psDroid->secondary_order,
		         moveDescription(psDroid->movement.status));
	}
}

/** This function sends all members of the psGroup the order psData using orderDroidBase().
 * If the order data is to recover an artifact, the order is only given to the closest droid to the artifact.
 */
static void orderCmdGroupBase(Group* psGroup, Order* psData)
{
	ASSERT_OR_RETURN(, psGroup != nullptr, "Invalid unit group");
	syncDebug("Commander group order");

	if (psData->type == ORDER_TYPE::RECOVER) {
		// picking up an artifact - only need to send one unit
		Droid* psChosen = nullptr;
		int mindist = SDWORD_MAX;
		for (Droid* psCurr = psGroup->members; psCurr; psCurr = psCurr->psGrpNext)
		{
			if (psCurr->order.type == RTR || psCurr->order.type == RTB || psCurr->order.type ==
				RTR_SPECIFIED) {
				// don't touch units returning for repairs
				continue;
			}
			int currdist = objPosDiffSq(psCurr->pos, psData->psObj->pos);
			if (currdist < mindist)
			{
				psChosen = psCurr;
				mindist = currdist;
			}
			syncDebug("command %d,%d", psCurr->id, currdist);
		}
		if (psChosen != nullptr)
		{
			orderDroidBase(psChosen, psData);
		}
	}
	else
	{
		const bool isAttackOrder = psData->type == ATTACKTARGET || psData->type == ATTACK;
		for (Droid* psCurr = psGroup->members; psCurr; psCurr = psCurr->psGrpNext)
		{
			syncDebug("command %d", psCurr->id);
			if (!orderState(psCurr, RTR)) // if you change this, youll need to change sendcmdgroup()
			{
				if (!isAttackOrder)
				{
					orderDroidBase(psCurr, psData);
					continue;
				}
				if (psCurr->type == DROID_SENSOR && psData->psObj)
				{
					// sensors must observe, not attack
					auto observeOrder = Order(OBSERVE, psData->psObj);
					orderDroidBase(psCurr, &observeOrder);
				}
				else
				{
					// for non-sensors, check that the designated target is actually valid
					// there is no point in ordering AA gun to attack ground units
					for (int i = 0; i < MAX_WEAPONS; i++)
					{
						if (validTarget(psCurr, psData->psObj, i))
						{
							orderDroidBase(psCurr, psData);
							break;
						}
					}
				}
			}
		}
	}
}


/** The minimum delay to be used on orderPlayFireSupportAudio() for fire support sound.*/
#define	AUDIO_DELAY_FIRESUPPORT		(3*GAME_TICKS_PER_SEC)


/** This function chooses the sound to play after the object is assigned to fire support a specific unit. Uses audio_QueueTrackMinDelay() to play the sound.
 * @todo this function is about playing audio. I'm not sure this should be in here.
 */
static void orderPlayFireSupportAudio(SimpleObject* psObj)
{
	SDWORD iAudioID = NO_SOUND;
	Droid* psDroid;
	Structure* psStruct;

	ASSERT_OR_RETURN(, psObj != nullptr, "Invalid pointer");
	/* play appropriate speech */
	switch (psObj->type)
	{
	case OBJ_DROID:
		psDroid = (Droid*)psObj;
		if (psDroid->type == DROID_COMMAND)
		{
			iAudioID = ID_SOUND_ASSIGNED_TO_COMMANDER;
		}
		else if (psDroid->type == DROID_SENSOR)
		{
			iAudioID = ID_SOUND_ASSIGNED_TO_SENSOR;
		}
		break;

	case OBJ_STRUCTURE:
		psStruct = (Structure*)psObj;
	//check for non-CB first
		if (structStandardSensor(psStruct) || structVTOLSensor(psStruct))
		{
			iAudioID = ID_SOUND_ASSIGNED_TO_SENSOR;
		}
		else if (structCBSensor(psStruct) || structVTOLCBSensor(psStruct))
		{
			iAudioID = ID_SOUND_ASSIGNED_TO_COUNTER_RADAR;
		}
		break;
	default:
		break;
	}

	if (iAudioID != NO_SOUND)
	{
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
	if (order == ORDER_TYPE::RETURN_TO_REPAIR {
		return psDroid->order.type == ORDER_TYPE::RETURN_TO_REPAIR ||
           psDroid->order.type == RTR_SPECIFIED;
	}
	return psDroid->order.type == order;
}

/**
 * This function returns true if the order is an acceptable order
 * to give for a given location on the map.
 */
bool validOrderForLoc(ORDER_TYPE order)
{
  using enum ORDER_TYPE;
	return (order == NONE || order == MOVE || order == GUARD ||
		      order == SCOUT || order == PATROL ||
      		order == TRANSPORT_OUT || order == TRANSPORT_IN ||
		      order == TRANSPORT_RETURN || order == DISEMBARK ||
      		order == CIRCLE);
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

/** This function attributes the order's location to (pX,pY) if the order is the same as the droid.
 * Returns true if it was attributed and false if not.
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

/** This function returns true if the order is a valid order to give to an object and false if it's not.*/
bool validOrderForObj(ORDER_TYPE order)
{
  using enum ORDER_TYPE;
	return (order == NONE || order == HELP_BUILD || order == DEMOLISH ||
		order == REPAIR || order == ATTACK || order == FIRE_SUPPORT || order ==
		COMMANDER_SUPPORT ||
		order == OBSERVE || order == ATTACK_TARGET || order == RTR ||
		order == RTR_SPECIFIED || order == EMBARK || order == GUARD ||
		order == DROID_REPAIR || order == RESTORE || order == BUILD_MODULE ||
		order == REARM || order == RECOVER);
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

/** This function returns the order's target if it has one, and NULL if the order is not a target order.
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
		// not an object order - return false
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
	//case HELPBUILD:
	case ATTACK:
	case FIRE_SUPPORT:
	case OBSERVE:
	case DEMOLISH:
	case RETURN_TO_REPAIR:
	case RTR_SPECIFIED:
	case DROID_REPAIR:
	case REARM:
	case GUARD:
		return psDroid->order.psObj;
		break;
	}

	return nullptr;
}


/** This function sends the droid an order with a location and stats.
 * If the mode is ModeQueue, the order is added to the droid's order list using sendDroidInfo(), else, a DROID_ORDER_DATA is alloc, the old order list is erased, and the order is sent using orderDroidBase().
 */
void orderDroidStatsLocDir(Droid* psDroid, ORDER_TYPE order, StructureStats* psStats, unsigned x, unsigned y,
                           uint16_t direction, QUEUE_MODE mode)
{
	ASSERT(psDroid != nullptr, "Invalid unit pointer");
	ASSERT(order == ORDER_TYPE::BUILD, "Invalid order for location");

	Order sOrder(order, psStats, Vector2i(x, y), direction);
	if (mode == ModeQueue && bMult {
		sendDroidInfo(psDroid, sOrder, false);
		return; // Wait for our order before changing the droid.
	}

	orderClearDroidList(psDroid);
	orderDroidBase(psDroid, &sOrder);
}

/** This function adds that order to the droid's list using sendDroidInfo().
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

	sendDroidInfo(psDroid, Order(order, psStats, Vector2i(x, y), direction), add);
}


/** Equivalent to orderDroidStatsLocDir(), but uses two locations.*/
void orderDroidStatsTwoLocDir(Droid* psDroid, ORDER_TYPE order, StructureStats* psStats, unsigned x1, unsigned y1,
                              unsigned x2, unsigned y2, uint16_t direction, QUEUE_MODE mode)
{
	ASSERT(psDroid != nullptr, "Invalid unit pointer");
	ASSERT(order == ORDER_TYPE::LINE_BUILD, "Invalid order for location");

	Order sOrder(order, psStats, Vector2i(x1, y1), Vector2i(x2, y2), direction);
	if (mode == ModeQueue && bMultiPlayer)
	{
		sendDroidInfo(psDroid, sOrder, false);
		return; // Wait for our order before changing the droid.
	}

	orderClearDroidList(psDroid);
	orderDroidBase(psDroid, &sOrder);
}

/** Equivalent to orderDroidStatsLocDirAdd(), but uses two locations.
 * @todo seems closely related with orderDroidStatsTwoLocDir(). See if this can be incorporated on it.
 */
void orderDroidStatsTwoLocDirAdd(Droid* psDroid, ORDER_TYPE order, StructureStats* psStats,
                                 unsigned x1, unsigned y1, unsigned x2, unsigned y2, uint16_t direction)
{
	ASSERT(psDroid != nullptr, "Invalid unit pointer");
	ASSERT(order == ORDER_TYPE::LINE_BUILD, "Invalid order for location");

	sendDroidInfo(psDroid, Order(order, psStats, Vector2i(x1, y1), Vector2i(x2, y2), direction), true);
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



/** This function goes to the droid's order list and sets a new order to it from its order list.*/
bool orderDroidList(Droid* psDroid)
{
	if (psDroid->listSize > 0) {
		// there are some orders to give
		Order sOrder = psDroid->asOrderList[0];
		orderDroidListEraseRange(psDroid, 0, 1);

    using enum ORDER_TYPE;
		switch (sOrder.type) {
		case MOVE:
		case SCOUT:
		case DISEMBARK:
			ASSERT(sOrder.target == nullptr && sOrder.structure_stats == nullptr,
             "Extra %s parameters.",
			       getDroidOrderName(sOrder.type).c_str());
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
             "Extra %s parameters.",
             getDroidOrderName(sOrder.type).c_str());
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

		orderDroidBase(psDroid, &sOrder);
		syncDebugDroid(psDroid, 'o');

		return true;
	}
	return false;
}

/** This function goes to the droid's order list and erases its elements from indexBegin to indexEnd.*/
void orderDroidListEraseRange(Droid* psDroid, unsigned indexBegin, unsigned indexEnd)
{
	// Erase elements
	indexEnd = MIN(indexEnd, psDroid->asOrderList.size()); // Do nothing if trying to pop an empty list.
	psDroid->asOrderList.erase(psDroid->asOrderList.begin() + indexBegin, psDroid->asOrderList.begin() + indexEnd);

	// Update indices into list.
	psDroid->listSize -= MIN(indexEnd, psDroid->listSize) - MIN(indexBegin, psDroid->listSize);
	psDroid->listPendingBegin -= MIN(indexEnd, psDroid->listPendingBegin) - MIN(indexBegin, psDroid->listPendingBegin);
}


/** This function clears all the synchronised orders from the list, calling orderDroidListEraseRange() from 0 to psDroid->listSize.*/
void orderClearDroidList(Droid* psDroid)
{
	syncDebug("droid%d list cleared", psDroid->getId());
	orderDroidListEraseRange(psDroid, 0, psDroid->listSize);
}

/** This function clears all the orders from droid's order list that don't have target as psTarget.*/
void orderClearTargetFromDroidList(Droid* psDroid, SimpleObject* psTarget)
{
	for (unsigned i = 0; i < psDroid->asOrderList.size(); ++i)
	{
		if (psDroid->asOrderList[i].psObj == psTarget)
		{
			if ((int)i < psDroid->listSize)
			{
				syncDebug("droid%d list erase%d", psDroid->id, psTarget->id);
			}
			orderDroidListEraseRange(psDroid, i, i + 1);
			--i; // If this underflows, the ++i will overflow it back.
		}
	}
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
ORDER_TYPE chooseOrderLoc(Droid* psDroid, unsigned x, unsigned y, bool altOrder)
{
  using enum ORDER_TYPE;
	ORDER_TYPE order = NONE;
	auto propulsion = psDroid->getPropulsion()->propulsionType;

	if (isTransporter(*psDroid) && game.type == LEVEL_TYPE::CAMPAIGN) {
		// transports can't be controlled in campaign
		return NONE;
	}

	// default to move; however, we can only end up on a tile
	// where can stay, ie VTOLs must be able to land as well
	if (psDroid->isVtol()) {
		propulsion = PROPULSION_TYPE::WHEELED;
	}
	if (!fpathBlockingTile(map_coord(x), map_coord(y), propulsion)) {
		order = MOVE;
	}

	// scout if alt was pressed
	if (altOrder) {
		order = SCOUT;
		if (psDroid->isVtol()) {
			// patrol if in a VTOL
			order = PATROL;
		}
	}

	// and now we want Transporters to fly! - in multiPlayer!!
	if (isTransporter(*psDroid) &&
      game.type == LEVEL_TYPE::SKIRMISH) {
		/* in MultiPlayer - if ALT-key is pressed then need to get the Transporter
		 * to fly to location and all units disembark */
		if (altOrder) {
			order = DISEMBARK;
		}
	}
	else if (secondaryGetState(psDroid, SECONDARY_ORDER::CIRCLE, ModeQueue) == DSS_CIRCLE_SET)
	// ModeQueue here means to check whether we pressed the circle button, whether or not it synched yet. The reason for this weirdness is that a circle order makes no sense as a secondary state in the first place (the circle button _should_ have been only in the UI, not in the game state..!), so anything dealing with circle orders will necessarily be weird.
	{
		order = CIRCLE;
		secondarySetState(psDroid, SECONDARY_ORDER::CIRCLE, DSS_NONE);
	}
	else if (secondaryGetState(psDroid, SECONDARY_ORDER::PATROL, ModeQueue) == DSS_PATROL_SET)
	// ModeQueue here means to check whether we pressed the patrol button, whether or not it synched yet. The reason for this weirdness is that a patrol order makes no sense as a secondary state in the first place (the patrol button _should_ have been only in the UI, not in the game state..!), so anything dealing with patrol orders will necessarily be weird.
	{
		order = PATROL;
		secondarySetState(psDroid, SECONDARY_ORDER::PATROL, DSS_NONE);
	}
	return order;
}

/** This function sends the selected droids an order to given a location. If a delivery point is selected, it is moved to a new location.
 * If add is true then the order is queued.
 * This function should only be called from UI.
 */
void orderSelectedLoc(unsigned player, unsigned x, unsigned y, bool add)
{
	Droid* psCurr;
	ORDER_TYPE order;

	// if were in build select mode ignore all other clicking
	if (intBuildSelectMode()) {
		return;
	}

	ASSERT_PLAYER_OR_RETURN(, player);

	// note that an order list graphic needs to be displayed
	bOrderEffectDisplayed = false;

	for (psCurr = apsDroidLists[player]; psCurr; psCurr = psCurr->psNext)
	{
		if (psCurr->selected)
		{
			// can't use bMultiPlayer since multimsg could be off.
			if (psCurr->getType() == DROID_TYPE::SUPER_TRANSPORTER &&
         game.type == LEVEL_TYPE::CAMPAIGN) {
				// Transport in campaign cannot be controlled by players
				DeSelectDroid(psCurr);
				continue;
			}

			order = chooseOrderLoc(psCurr, x, y, specialOrderKeyDown());
			// see if the order can be added to the list
			if (order != ORDER_TYPE::NONE && !(add && orderDroidLocAdd(psCurr, order, x, y))) {
				// if not just do it straight off
				orderDroidLoc(psCurr, order, x, y, ModeQueue);
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
	  	if (order.target == structure) // Order must be for this structure.
	  	{
	  		thisQueuedModule = order.index; // Order says which module to build.
	  	}
	  	break;
	  case BUILD:
	  case HELP_BUILD:
    {
      // Current order is weird, the BUILDMODULE mutates into a BUILD, and we use the order.pos instead of order.psObj.
      // Also, might be BUILD if selecting the module from the menu before clicking on the structure.
      Structure* orderStructure = castStructure(worldTile(order.pos)->psObject);
      if (orderStructure == structure && (order.psStats == orderStructure->pStructureType || order.psStats ==
        getModuleStat(orderStructure))) // Order must be for this structure.
      {
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

/** This function returns an order according to the droid, object (target) and altOrder.*/
Order chooseOrderObj(Droid* psDroid, SimpleObject* psObj, bool altOrder)
{
  using enum ORDER_TYPE;
	Order order(NONE);

	if (isTransporter(*psDroid)) {
		//in multiPlayer, need to be able to get Transporter repaired
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
		       (psFeat->psStats->subType == FEATURE_TYPE::GEN_ARTE ||
           (psFeat->psStats->subType == FEATURE_TYPE::OIL_DRUM))) {
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
		//check for standard sensor or VTOL intercept sensor
		if (asSensorStats[psDroid->asBits[COMP_SENSOR]].type == STANDARD_SENSOR
			|| asSensorStats[psDroid->asBits[COMP_SENSOR]].type == VTOL_INTERCEPT_SENSOR
			|| asSensorStats[psDroid->asBits[COMP_SENSOR]].type == SUPER_SENSOR) {
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
		       psObj->type == OBJ_DROID &&
           ((Droid*)psObj)->getType() == DROID_TYPE::COMMAND &&
           psDroid->getType() != DROID_TYPE::COMMAND &&
           psDroid->getType() != DROID_TYPE::CONSTRUCT &&
           psDroid->getType() != DROID_TYPE::CYBORG_CONSTRUCT) {
		// get a droid to join a command droid's group
		DeSelectDroid(psDroid);
		order = Order(COMMANDER_SUPPORT, *psObj);
	}
	// repair droid
	else if (aiCheckAlliances(psObj->getPlayer(), psDroid->getPlayer()) &&
		psObj->type == OBJ_DROID &&
           (psDroid->getType() == DROID_TYPE::REPAIRER ||
            psDroid->getType() == DROID_TYPE::CYBORG_REPAIR) &&
           droidIsDamaged((Droid*)psObj)) {
		order = Order(DROID_REPAIR, *psObj);
	}
	// guarding constructor droids
	else if (aiCheckAlliances(psObj->getPlayer(), psDroid->getPlayer()) &&
		       psObj->type == OBJ_DROID &&
           (((Droid*)psObj)->type == DROID_CONSTRUCT ||
            ((Droid*)psObj)->type == DROID_CYBORG_CONSTRUCT ||
            ((Droid*)psObj)->type == DROID_SENSOR ||
            (((Droid*)psObj)->type == DROID_COMMAND &&
             psObj->getPlayer() != psDroid->getPlayer())) &&
           (psDroid->getType() == DROID_TYPE::WEAPON ||
            psDroid->getType() == DROID_TYPE::CYBORG ||
            psDroid->getType() == DROID_TYPE::CYBORG_SUPER) &&
           proj_Direct(asWeaponStats + psDroid->asWeaps[0].nStat)) {
		order = Order(GUARD, *psObj);
		assignSensorTarget(psObj);
		psDroid->selected = false;
	}
	else if (aiCheckAlliances(psObj->getPlayer(), psDroid->getPlayer()) &&
		psObj->type == OBJ_STRUCTURE) {
		Structure* psStruct = (Structure*)psObj;
		ASSERT_OR_RETURN(Order(NONE), psObj != nullptr, "Invalid structure pointer");

		/* check whether construction droid */
		if (psDroid->getType() == DROID_TYPE::CONSTRUCT ||
        psDroid->getType() == DROID_TYPE::CYBORG_CONSTRUCT) {

			int moduleIndex =
				nextModuleToBuild(psStruct, ctrlShiftDown()
        ? highestQueuedModule(psDroid, psStruct)
        : -1);

			//Re-written to allow demolish order to be added to the queuing system
			if (intDemolishSelectMode() && psObj->getPlayer() == psDroid->getPlayer()) {
				//check to see if anything is currently trying to build the structure
				//can't build and demolish at the same time!
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
						order = Order(BUILD_MODULE, *psObj, moduleIndex); // Try scheduling a module, instead.
					}
				}
			}
			else if (psStruct->getHp() < structureBody(psStruct)) {
				order = Order(REPAIR, *psObj);
			}
			//check if can build a module
			else if (moduleIndex > 0) {
				order = Order(BUILD_MODULE, *psObj, moduleIndex);
			}
		}

		if (order.type == NONE)
		{
			/* check repair facility and in need of repair */
			if (psStruct->pStructureType->type == REF_REPAIR_FACILITY &&
				psStruct->status == SS_BUILT)
			{
				order = Order(RTR_SPECIFIED, psObj);
			}
			else if (electronicDroid(psDroid) &&
				//psStruct->resistance < (SDWORD)(psStruct->pStructureType->resistance))
				psStruct->resistance < (SDWORD)structureResistance(psStruct->
				                                                   pStructureType, psStruct->player))
			{
				order = Order(RESTORE, psObj);
			}
			//check for counter battery assignment
			else if (structSensorDroidWeapon(psStruct, psDroid))
			{
				order = Order(FIRESUPPORT, psObj);
				//inform display system
				setSensorAssigned();
				//deselect droid
				DeSelectDroid(psDroid);
			}
			//REARM VTOLS
			else if (isVtolDroid(psDroid))
			{
				//default to no order
				//check if rearm pad
				if (psStruct->pStructureType->type == REF_REARM_PAD)
				{
					//don't bother checking cos we want it to go there if directed
					order = Order(REARM, psObj);
				}
			}
			// Some droids shouldn't be guarding
			else if ((psDroid->type == DROID_WEAPON ||
                psDroid->type == DROID_CYBORG ||
                psDroid->type == DROID_CYBORG_SUPER)
				&& proj_Direct(asWeaponStats + psDroid->asWeaps[0].nStat))
			{
				order = Order(GUARD, psObj);
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
                                   dynamic_cast<Structure *>(psObj)->getStats(),
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
void orderSelectedStatsLocDir(unsigned player, DROID_ORDER order, StructureStats* psStats, unsigned x, unsigned y,
                              uint16_t direction, bool add)
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
			for (unsigned i = 0; i < numWeapons(*psDroid); ++i)
			{
				const WeaponStats* weaponStats = asWeaponStats + psDroid->asWeaps[i].nStat;

				if (proj_GetLongRange(weaponStats, psDroid->getPlayer()) ==
              proj_GetShortRange(weaponStats, psDroid->getPlayer())) {
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
  if (!(psDroid->getHp() * 100 <= repairLevel * psDroid->originalHp)) {
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
static void secondarySetGroupState(unsigned player, unsigned group, SECONDARY_ORDER sec, SECONDARY_STATE state)
{
	ASSERT_PLAYER_OR_RETURN(, player);

	for (auto& psCurr : apsDroidLists[player])
	{
		if (psCurr.group == group &&
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
static SECONDARY_STATE secondaryGetAverageGroupState(unsigned player, unsigned group, unsigned mask)
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
		if (psCurr.group == group) {
			for (i = 0; i < numStates; i++)
			{
				if (aStateCount[i].state == (psCurr.secondaryOrder & mask)) {
					aStateCount[i].num += 1;
					break;
				}
			}

			if (i == numStates) {
				aStateCount[numStates].state = psCurr.secondaryOrder & mask;
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
void secondarySetAverageGroupState(unsigned player, unsigned group)
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
	case HELP_BUILD: return "HELPBUILD";
	case LINE_BUILD: return "LINEBUILD";
	case DEMOLISH: return "DEMOLISH";
	case REPAIR: return "REPAIR";
	case OBSERVE: return "OBSERVE";
	case FIRE_SUPPORT: return "FIRESUPPORT";
	case RETURN_TO_BASE: return "RTB";
	case RETURN_TO_REPAIR: return "RTR";
	case EMBARK: return "EMBARK";
	case DISEMBARK: return "DISEMBARK";
	case ATTACK_TARGET: return "ATTACKTARGET";
	case COMMANDER_SUPPORT: return "COMMANDERSUPPORT";
	case BUILD_MODULE: return "BUILDMODULE";
	case RECYCLE: return "RECYCLE";
	case TRANSPORT_OUT: return "TRANSPORTOUT";
	case TRANSPORT_IN: return "TRANSPORTIN";
	case TRANSPORT_RETURN: return "TRANSPORTRETURN";
	case GUARD: return "GUARD";
	case DROID_REPAIR: return "DROIDREPAIR";
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
