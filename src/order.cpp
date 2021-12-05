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
 *
 * @file
 * Functions for setting the orders of a droid or group of droids.
 *
 */

#include <string.h>

#include "lib/framework/frame.h"
#include "lib/framework/input.h"
#include "lib/framework/math_ext.h"
#include "lib/ivis_opengl/ivisdef.h"

#include "objects.h"
#include "order.h"
#include "action.h"
#include "map.h"
#include "projectile.h"
#include "effects.h"	// for waypoint display
#include "lib/gamelib/gtime.h"
#include "lib/netplay/netplay.h"
#include "intorder.h"
#include "orderdef.h"
#include "transporter.h"
#include "qtscript.h"
#include "group.h"
#include "cmddroid.h"
#include "move.h"
#include "multiplay.h"  //ajl
#include "random.h" // to balance the damaged units flow to repairs

#include "mission.h"
#include "hci.h"
#include "visibility.h"
#include "display.h"
#include "ai.h"
#include "warcam.h"
#include "lib/sound/audio_id.h"
#include "lib/sound/audio.h"
#include "fpath.h"
#include "display3d.h"
#include "console.h"
#include "mapgrid.h"

#include "random.h"

/** How long a droid runs after it fails do respond due to low moral. */
#define RUN_TIME		8000

/** How long a droid runs burning after it fails do respond due to low moral. */
#define RUN_BURN_TIME	10000

/** The distance a droid has in guard mode. */
#define DEFEND_MAXDIST		(TILE_UNITS * 3)

/** The distance a droid has in guard mode.
 * @todo seems to be used as equivalent to GUARD_MAXDIST.
 */
#define DEFEND_BASEDIST		(TILE_UNITS * 3)

/** The distance a droid has in guard mode. Equivalent to GUARD_MAXDIST, but used for droids being on a command group. */
#define DEFEND_CMD_MAXDIST		(TILE_UNITS * 8)

/** The distance a droid has in guard mode. Equivalent to GUARD_BASEDIST, but used for droids being on a command group. */
#define DEFEND_CMD_BASEDIST		(TILE_UNITS * 5)

/** The maximum distance a constructor droid has in guard mode. */
#define CONSTRUCT_MAXDIST		(TILE_UNITS * 8)

/** The maximum distance allowed to a droid to move out of the path on a patrol/scout. */
#define SCOUT_DIST			(TILE_UNITS * 8)

/** The maximum distance allowed to a droid to move out of the path if already attacking a target on a patrol/scout. */
#define SCOUT_ATTACK_DIST	(TILE_UNITS * 5)

static void orderClearDroidList(Droid *psDroid);

/** Whether an order effect has been displayed
 * @todo better documentation required.
 */
static bool bOrderEffectDisplayed = false;

/** What the droid's action/order it is currently. This is used to debug purposes, jointly with showSAMPLES(). */
extern char DROIDDOING[512];
//////////////////////////////////////////////////////////////////

#define ASSERT_PLAYER_OR_RETURN(retVal, player) \
	ASSERT_OR_RETURN(retVal, player < MAX_PLAYERS, "Invalid player: %" PRIu32 "", player);

//////////////////////////////////////////////////////////////////
struct RtrBestResult
{
	RTR_DATA_TYPE type;
        GameObject *psObj;
	RtrBestResult(RTR_DATA_TYPE type, GameObject *psObj): type(type), psObj(psObj) {}
	RtrBestResult(): type(RTR_TYPE_NO_RESULT), psObj(nullptr) {}
	RtrBestResult(DROID_ORDER_DATA *psOrder): type(psOrder->rtrType), psObj(psOrder->psObj) {}
};

static RtrBestResult decideWhereToRepairAndBalance(Droid *psDroid);

/** This function checks if there are any structures to repair or help build in a given radius near the droid defined by REPAIR_RANGE if it is on hold, and REPAIR_MAXDIST if not on hold.
 * It returns a damaged or incomplete structure if any was found or nullptr if none was found.
 */
static std::pair<Structure *, DROID_ACTION> checkForDamagedStruct(Droid *psDroid)
{
  Structure *psFailedTarget = nullptr;
	if (psDroid->action == DROID_ACTION::SULK)
	{
		psFailedTarget = (Structure *)psDroid->psActionTarget[0];
	}

	unsigned radius = ((psDroid->order.type == DROID_ORDER_TYPE::HOLD) || (psDroid->order.type == DROID_ORDER_TYPE::NONE && secondaryGetState(psDroid, DSO_HALTTYPE) == DSS_HALT_HOLD)) ? REPAIR_RANGE : REPAIR_MAXDIST;

	unsigned bestDistanceSq = radius * radius;
	std::pair<Structure *, DROID_ACTION> best = {nullptr, DROID_ACTION::NONE};

	for (GameObject *object : gridStartIterate(
                 psDroid->getPosition.x, psDroid->getPosition.y, radius))
	{
		unsigned distanceSq = droidSqDist(psDroid, object);  // droidSqDist returns -1 if unreachable, (unsigned)-1 is a big number.

                Structure *structure = castStructure(object);
		if (structure == nullptr ||  // Must be a structure.
		    structure == psFailedTarget ||  // Must not have just failed to reach it.
		    distanceSq > bestDistanceSq ||  // Must be as close as possible.
		    !visibleObject(psDroid, structure, false) ||  // Must be able to sense it.
		    !aiCheckAlliances(psDroid->owningPlayer, structure->owningPlayer) ||  // Must be a friendly structure.
		    checkDroidsDemolishing(structure))  // Must not be trying to get rid of it.
		{
			continue;
		}

		// Check for structures to repair.
		if (structure->status == SS_BUILT && structIsDamaged(structure))
		{
			bestDistanceSq = distanceSq;
			best = {structure, DROID_ACTION::REPAIR};
		}
		// Check for structures to help build.
		else if (structure->status == SS_BEING_BUILT)
		{
			bestDistanceSq = distanceSq;
			best = {structure, DROID_ACTION::BUILD};
		}
	}

	return best;
}

static bool isRepairlikeAction(DROID_ACTION action)
{
	switch (action)
	{
		case DROID_ACTION::BUILD:
		case DROID_ACTION::BUILDWANDER:
		case DROID_ACTION::DEMOLISH:
		case DROID_ACTION::DROIDREPAIR:
		case DROID_ACTION::MOVETOBUILD:
		case DROID_ACTION::MOVETODEMOLISH:
		case DROID_ACTION::MOVETODROIDREPAIR:
		case DROID_ACTION::MOVETOREPAIR:
		case DROID_ACTION::MOVETORESTORE:
		case DROID_ACTION::REPAIR:
		case DROID_ACTION::RESTORE:
			return true;
		default:
			return false;
	}
}

/** This function sends all members of the psGroup the order psData using orderDroidBase().
 * If the order data is to recover an artifact, the order is only given to the closest droid to the artifact.
 */
static void orderCmdGroupBase(DROID_GROUP *psGroup, DROID_ORDER_DATA *psData)
{
  ASSERT_OR_RETURN(, psGroup != nullptr, "Invalid unit group");

  syncDebug("Commander group order");

  if (psData->type == DROID_ORDER_TYPE::RECOVER)
  {
    // picking up an artifact - only need to send one unit
    Droid *psChosen = nullptr;
    int mindist = SDWORD_MAX;
    for (Droid *psCurr = psGroup->psList; psCurr; psCurr = psCurr->psGrpNext)
    {
      if (psCurr->order.type == DROID_ORDER_TYPE::RTR || psCurr->order.type == DROID_ORDER_TYPE::RTB || psCurr->order.type == DROID_ORDER_TYPE::RTR_SPECIFIED)
      {
              // don't touch units returning for repairs
              continue;
      }
      int currdist = objPosDiffSq(psCurr->getPosition,
                                  psData->psObj->getPosition);
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
    const bool isAttackOrder = psData->type == DROID_ORDER_TYPE::ATTACKTARGET || psData->type == DROID_ORDER_TYPE::ATTACK;
    for (Droid *psCurr = psGroup->psList; psCurr; psCurr = psCurr->psGrpNext)
    {
      syncDebug("command %d", psCurr->id);
      if (!orderState(psCurr, DROID_ORDER_TYPE::RTR))		// if you change this, youll need to change sendcmdgroup()
      {
        if (!isAttackOrder)
        {
          orderDroidBase(psCurr, psData);
          continue;
        }
        if (psCurr->droidType == DROID_SENSOR && psData->psObj)
        {
          // sensors must observe, not attack
          auto observeOrder = DroidOrder(DROID_ORDER_TYPE::OBSERVE, psData->psObj);
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
static void orderPlayFireSupportAudio(GameObject *psObj)
{
	SDWORD	iAudioID = NO_SOUND;
        Droid *psDroid;
        Structure *psStruct;

	ASSERT_OR_RETURN(, psObj != nullptr, "Invalid pointer");
	/* play appropriate speech */
	switch (psObj->getType)
	{
	case OBJ_DROID:
		psDroid = (Droid *)psObj;
		if (psDroid->droidType == DROID_COMMAND)
		{
			iAudioID = ID_SOUND_ASSIGNED_TO_COMMANDER;
		}
		else if (psDroid->droidType == DROID_SENSOR)
		{
			iAudioID = ID_SOUND_ASSIGNED_TO_SENSOR;
		}
		break;

	case OBJ_STRUCTURE:
		psStruct = (Structure *)psObj;
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

/** This function compares the current droid's order to the order.
 * Returns true if they are the same, false else.
 */
bool orderState(Droid *psDroid, DROID_ORDER order)
{
	if (order == DROID_ORDER_TYPE::RTR)
	{
		return psDroid->order.type == DROID_ORDER_TYPE::RTR || psDroid->order.type == DROID_ORDER_TYPE::RTR_SPECIFIED;
	}

	return psDroid->order.type == order;
}


/** This function returns true if the order is an acceptable order to give for a given location on the map.*/
bool validOrderForLoc(DROID_ORDER order)
{
	return (order == DROID_ORDER_TYPE::NONE ||	order == DROID_ORDER_TYPE::MOVE ||	order == DROID_ORDER_TYPE::GUARD ||
	        order == DROID_ORDER_TYPE::SCOUT || order == DROID_ORDER_TYPE::PATROL ||
	        order == DROID_ORDER_TYPE::TRANSPORTOUT || order == DROID_ORDER_TYPE::TRANSPORTIN  ||
	        order == DROID_ORDER_TYPE::TRANSPORTRETURN || order == DROID_ORDER_TYPE::DISEMBARK ||
	        order == DROID_ORDER_TYPE::CIRCLE);
}

/** This function returns true if the order is a valid order to give to an object and false if it's not.*/
bool validOrderForObj(DROID_ORDER order)
{
	return (order == DROID_ORDER_TYPE::NONE || order == DROID_ORDER_TYPE::HELPBUILD || order == DROID_ORDER_TYPE::DEMOLISH ||
	        order == DROID_ORDER_TYPE::REPAIR || order == DROID_ORDER_TYPE::ATTACK || order == DROID_ORDER_TYPE::FIRESUPPORT || order == DROID_ORDER_TYPE::COMMANDERSUPPORT ||
	        order == DROID_ORDER_TYPE::OBSERVE || order == DROID_ORDER_TYPE::ATTACKTARGET || order == DROID_ORDER_TYPE::RTR ||
	        order == DROID_ORDER_TYPE::RTR_SPECIFIED || order == DROID_ORDER_TYPE::EMBARK || order == DROID_ORDER_TYPE::GUARD ||
	        order == DROID_ORDER_TYPE::DROIDREPAIR || order == DROID_ORDER_TYPE::RESTORE || order == DROID_ORDER_TYPE::BUILDMODULE ||
	        order == DROID_ORDER_TYPE::REARM || order == DROID_ORDER_TYPE::RECOVER);
}


/** This function sends an order with an object to the droid.
 * If the mode is ModeQueue, the order is added to the droid's order list using sendDroidInfo(), else, a DROID_ORDER_DATA is alloc, the old order list is erased, and the order is sent using orderDroidBase().
 */
void orderDroidObj(Droid *psDroid, DROID_ORDER order, GameObject *psObj, QUEUE_MODE mode)
{
	ASSERT(psDroid != nullptr, "Invalid unit pointer");
	ASSERT(validOrderForObj(order), "Invalid order for object");
	ASSERT_OR_RETURN(, !isBlueprint(psObj), "Target %s is a blueprint", objInfo(psObj));
	ASSERT_OR_RETURN(, !psObj->deathTime, "Target dead");

	DroidOrder sOrder(order, psObj);
	if (mode == ModeQueue) //ajl
	{
		sendDroidInfo(psDroid, sOrder, false);
		return;  // Wait for the order to be received before changing the droid.
	}

	orderClearDroidList(psDroid);
	orderDroidBase(psDroid, &sOrder);
}

/** This function sends the droid an order with a location and stats.
 * If the mode is ModeQueue, the order is added to the droid's order list using sendDroidInfo(), else, a DROID_ORDER_DATA is alloc, the old order list is erased, and the order is sent using orderDroidBase().
 */
void orderDroidStatsLocDir(Droid *psDroid, DROID_ORDER order,
                           StructureStats *psStats, UDWORD x, UDWORD y, uint16_t direction, QUEUE_MODE mode)
{
	ASSERT(psDroid != nullptr, "Invalid unit pointer");
	ASSERT(order == DROID_ORDER_TYPE::BUILD, "Invalid order for location");

	DroidOrder sOrder(order, psStats, Vector2i(x, y), direction);
	if (mode == ModeQueue && bMultiPlayer)
	{
		sendDroidInfo(psDroid, sOrder, false);
		return;  // Wait for our order before changing the droid.
	}

	orderClearDroidList(psDroid);
	orderDroidBase(psDroid, &sOrder);
}


/** This function adds that order to the droid's list using sendDroidInfo().
 * @todo seems closely related with orderDroidStatsLocDir(). See if this one can be incorporated on it.
 */
void orderDroidStatsLocDirAdd(Droid *psDroid, DROID_ORDER order,
                              StructureStats *psStats, UDWORD x, UDWORD y, uint16_t direction, bool add)
{
	ASSERT(psDroid != nullptr, "Invalid unit pointer");

	// can only queue build orders with this function
	if (order != DROID_ORDER_TYPE::BUILD)
	{
		return;
	}

	sendDroidInfo(psDroid, DroidOrder(order, psStats, Vector2i(x, y), direction), add);
}


/** Equivalent to orderDroidStatsLocDir(), but uses two locations.*/
void orderDroidStatsTwoLocDir(Droid *psDroid, DROID_ORDER order,
                              StructureStats *psStats, UDWORD x1, UDWORD y1, UDWORD x2, UDWORD y2, uint16_t direction, QUEUE_MODE mode)
{
	ASSERT(psDroid != nullptr,	"Invalid unit pointer");
	ASSERT(order == DROID_ORDER_TYPE::LINEBUILD, "Invalid order for location");

	DroidOrder sOrder(order, psStats, Vector2i(x1, y1), Vector2i(x2, y2), direction);
	if (mode == ModeQueue && bMultiPlayer)
	{
		sendDroidInfo(psDroid, sOrder, false);
		return;  // Wait for our order before changing the droid.
	}

	orderClearDroidList(psDroid);
	orderDroidBase(psDroid, &sOrder);
}


/** Equivalent to orderDroidStatsLocDirAdd(), but uses two locations.
 * @todo seems closely related with orderDroidStatsTwoLocDir(). See if this can be incorporated on it.
 */
void orderDroidStatsTwoLocDirAdd(Droid *psDroid, DROID_ORDER order,
                                 StructureStats *psStats, UDWORD x1, UDWORD y1, UDWORD x2, UDWORD y2, uint16_t direction)
{
	ASSERT(psDroid != nullptr, "Invalid unit pointer");
	ASSERT(order == DROID_ORDER_TYPE::LINEBUILD, "Invalid order for location");

	sendDroidInfo(psDroid, DroidOrder(order, psStats, Vector2i(x1, y1), Vector2i(x2, y2), direction), true);
}

/** @todo needs documentation.*/
void orderDroidAddPending(Droid *psDroid, DROID_ORDER_DATA *psOrder)
{
	ASSERT_OR_RETURN(, psDroid != nullptr, "Invalid unit pointer");

	psDroid->asOrderList.push_back(*psOrder);

	// Only display one arrow, bOrderEffectDisplayed must be set to false once per arrow.
	if (!bOrderEffectDisplayed)
	{
		Vector3i position(0, 0, 0);
		if (psOrder->psObj == nullptr)
		{
			position.x = psOrder->pos.x;
			position.z = psOrder->pos.y;
		}
		else
		{
			position = psOrder->psObj->getPosition.xzy();
		}
		position.y = map_Height(position.x, position.z) + 32;
		if (psOrder->psObj != nullptr && psOrder->psObj->displayData.imd != nullptr)
		{
			position.y += psOrder->psObj->displayData.imd->max.y;
		}
		addEffect(&position, EFFECT_WAYPOINT, WAYPOINT_TYPE, false, nullptr, 0);
		bOrderEffectDisplayed = true;
	}
}

/** This function goes to the droid's order list and erases its elements from indexBegin to indexEnd.*/
void orderDroidListEraseRange(Droid *psDroid, unsigned indexBegin, unsigned indexEnd)
{
	// Erase elements
	indexEnd = MIN(indexEnd, psDroid->asOrderList.size());  // Do nothing if trying to pop an empty list.
	psDroid->asOrderList.erase(psDroid->asOrderList.begin() + indexBegin, psDroid->asOrderList.begin() + indexEnd);

	// Update indices into list.
	psDroid->listSize         -= MIN(indexEnd, psDroid->listSize)         - MIN(indexBegin, psDroid->listSize);
	psDroid->listPendingBegin -= MIN(indexEnd, psDroid->listPendingBegin) - MIN(indexBegin, psDroid->listPendingBegin);
}


/** This function clears all the synchronised orders from the list, calling orderDroidListEraseRange() from 0 to psDroid->listSize.*/
void orderClearDroidList(Droid *psDroid)
{
	syncDebug("droid%d list cleared", psDroid->id);
	orderDroidListEraseRange(psDroid, 0, psDroid->listSize);
}


/** This function clears all the orders from droid's order list that don't have target as psTarget.*/
void orderClearTargetFromDroidList(Droid *psDroid, GameObject *psTarget)
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
			--i;  // If this underflows, the ++i will overflow it back.
		}
	}
}

/** This function sends the droid an order with a location using sendDroidInfo().
 * @todo it is very close to what orderDroidLoc() function does. Suggestion to refract them.
 */
static bool orderDroidLocAdd(Droid *psDroid, DROID_ORDER order, UDWORD x, UDWORD y, bool add = true)
{
	// can only queue move, scout, and disembark orders
	if (order != DROID_ORDER_TYPE::MOVE && order != DROID_ORDER_TYPE::SCOUT && order != DROID_ORDER_TYPE::DISEMBARK)
	{
		return false;
	}

	sendDroidInfo(psDroid, DroidOrder(order, Vector2i(x, y)), add);

	return true;
}


/** This function sends the droid an order with a location using sendDroidInfo().
 * @todo it is very close to what orderDroidObj() function does. Suggestion to refract them.
 */
static bool orderDroidObjAdd(Droid *psDroid, DroidOrder const &order, bool add)
{
	ASSERT(!isBlueprint(order.psObj), "Target %s for queue is a blueprint", objInfo(order.psObj));

	// check can queue the order
	switch (order.type)
	{
	case DROID_ORDER_TYPE::ATTACK:
	case DROID_ORDER_TYPE::REPAIR:
	case DROID_ORDER_TYPE::OBSERVE:
	case DROID_ORDER_TYPE::DROIDREPAIR:
	case DROID_ORDER_TYPE::FIRESUPPORT:
	case DROID_ORDER_TYPE::DEMOLISH:
	case DROID_ORDER_TYPE::HELPBUILD:
	case DROID_ORDER_TYPE::BUILDMODULE:
		break;
	default:
		return false;
	}

	sendDroidInfo(psDroid, order, add);

	return true;
}

/** This function sends the selected droids an order to given a location. If a delivery point is selected, it is moved to a new location.
 * If add is true then the order is queued.
 * This function should only be called from UI.
 */
void orderSelectedLoc(uint32_t player, uint32_t x, uint32_t y, bool add)
{
  Droid *psCurr;
	DROID_ORDER		order;

	//if were in build select mode ignore all other clicking
	if (intBuildSelectMode())
	{
		return;
	}

	ASSERT_PLAYER_OR_RETURN(, player);

	// note that an order list graphic needs to be displayed
	bOrderEffectDisplayed = false;

	for (psCurr = allDroidLists[player]; psCurr; psCurr = psCurr->psNext)
	{
		if (psCurr->selected)
		{
			// can't use bMultiPlayer since multimsg could be off.
			if (psCurr->droidType == DROID_SUPERTRANSPORTER && game.type == LEVEL_TYPE::CAMPAIGN)
			{
				// Transport in campaign cannot be controlled by players
				DeSelectDroid(psCurr);
				continue;
			}

			order = chooseOrderLoc(psCurr, x, y, specialOrderKeyDown());
			// see if the order can be added to the list
			if (order != DROID_ORDER_TYPE::NONE && !(add && orderDroidLocAdd(psCurr, order, x, y)))
			{
				// if not just do it straight off
				orderDroidLoc(psCurr, order, x, y, ModeQueue);
			}
		}
	}
}

static int highestQueuedModule(DroidOrder const &order,
                               Structure const *structure, int prevHighestQueuedModule)
{
	int thisQueuedModule = -1;
	switch (order.type)
	{
	default:
		break;
	case DROID_ORDER_TYPE::BUILDMODULE:
		if (order.psObj == structure)  // Order must be for this structure.
		{
			thisQueuedModule = order.index;  // Order says which module to build.
		}
		break;
	case DROID_ORDER_TYPE::BUILD:
	case DROID_ORDER_TYPE::HELPBUILD:
		{
			// Current order is weird, the DROID_ORDER_TYPE::BUILDMODULE mutates into a DROID_ORDER_TYPE::BUILD, and we use the order.pos instead of order.psObj.
			// Also, might be DROID_ORDER_TYPE::BUILD if selecting the module from the menu before clicking on the structure.
                        Structure *orderStructure = castStructure(worldTile(order.pos)->psObject);
			if (orderStructure == structure && (order.psStats == orderStructure->stats || order.psStats == getModuleStat(orderStructure)))  // Order must be for this structure.
			{
				thisQueuedModule = nextModuleToBuild(structure, prevHighestQueuedModule);
			}
			break;
		}
	}
	return std::max(prevHighestQueuedModule, thisQueuedModule);
}

static int highestQueuedModule(Droid const *droid, Structure const *structure)
{
	int module = highestQueuedModule(droid->order, structure, -1);
	for (unsigned n = droid->listPendingBegin; n < droid->asOrderList.size(); ++n)
	{
		module = highestQueuedModule(droid->asOrderList[n], structure, module);
	}
	return module;
}

/** This function returns an order according to the droid, object (target) and altOrder.*/
DroidOrder chooseOrderObj(Droid *psDroid, GameObject *psObj, bool altOrder)
{
	DroidOrder order(DROID_ORDER_TYPE::NONE);

	if (isTransporter(psDroid))
	{
		//in multiPlayer, need to be able to get Transporter repaired
		if (bMultiPlayer)
		{
			if (aiCheckAlliances(psObj->owningPlayer, psDroid->owningPlayer) &&
                      psObj->getType == OBJ_STRUCTURE)
			{
                          Structure *psStruct = (Structure *) psObj;
				ASSERT_OR_RETURN(DroidOrder(DROID_ORDER_TYPE::NONE), psObj != nullptr, "Invalid structure pointer");
				if (psStruct->stats->type == REF_REPAIR_FACILITY &&
				    psStruct->status == SS_BUILT)
				{
					return DroidOrder(DROID_ORDER_TYPE::RTR_SPECIFIED, psObj);
				}
			}
		}
		return DroidOrder(DROID_ORDER_TYPE::NONE);
	}

	if (altOrder && (psObj->getType == OBJ_DROID || psObj->getType == OBJ_STRUCTURE) && psDroid->owningPlayer == psObj->owningPlayer)
	{
		if (psDroid->droidType == DROID_SENSOR)
		{
			return DroidOrder(DROID_ORDER_TYPE::OBSERVE, psObj);
		}
		else if ((psDroid->droidType == DROID_REPAIR ||
		          psDroid->droidType == DROID_CYBORG_REPAIR) &&
                           psObj->getType == OBJ_DROID)
		{
			return DroidOrder(DROID_ORDER_TYPE::DROIDREPAIR, psObj);
		}
		else if ((psDroid->droidType == DROID_WEAPON) || cyborgDroid(psDroid) ||
		         (psDroid->droidType == DROID_COMMAND))
		{
			return DroidOrder(DROID_ORDER_TYPE::ATTACK, psObj);
		}
	}
	//check for transporters first
	if (psObj->getType == OBJ_DROID && isTransporter((Droid *)psObj) && psObj->owningPlayer == psDroid->owningPlayer)
	{
		order = DroidOrder(DROID_ORDER_TYPE::EMBARK, psObj);
	}
	// go to recover an artifact/oil drum - don't allow VTOL's to get this order
	else if (psObj->getType == OBJ_FEATURE &&
	         (((Feature *)psObj)->psStats->subType == FEAT_GEN_ARTE ||
	          ((Feature *)psObj)->psStats->subType == FEAT_OIL_DRUM))
	{
		if (!isVtolDroid(psDroid))
		{
			order = DroidOrder(DROID_ORDER_TYPE::RECOVER, psObj);
		}
	}
	// else default to attack if the droid has a weapon
	else if (psDroid->numWeapons > 0
	         && psObj->owningPlayer != psDroid->owningPlayer && !aiCheckAlliances(psObj->owningPlayer, psDroid->owningPlayer))
	{
		// check valid weapon/prop combination
		for (int i = 0; i < MAX_WEAPONS; ++i)
		{
			if (validTarget(psDroid, psObj, i))
			{
				order = DroidOrder(DROID_ORDER_TYPE::ATTACK, psObj);
				break;
			}
		}
	}
	else if (psDroid->droidType == DROID_SENSOR
	         && psObj->owningPlayer != psDroid->owningPlayer && !aiCheckAlliances(psObj->owningPlayer, psDroid->owningPlayer))
	{
		//check for standard sensor or VTOL intercept sensor
		if (asSensorStats[psDroid->asBits[COMP_SENSOR]].type == STANDARD_SENSOR
		    || asSensorStats[psDroid->asBits[COMP_SENSOR]].type == VTOL_INTERCEPT_SENSOR
		    || asSensorStats[psDroid->asBits[COMP_SENSOR]].type == SUPER_SENSOR)
		{
			// a sensor droid observing an object
			order = DroidOrder(DROID_ORDER_TYPE::OBSERVE, psObj);
		}
	}
	else if (droidSensorDroidWeapon(psObj, psDroid))
	{
		// got an indirect weapon droid or vtol doing fire support
		order = DroidOrder(DROID_ORDER_TYPE::FIRESUPPORT, psObj);
		setSensorAssigned();
	}
	else if (psObj->owningPlayer == psDroid->owningPlayer &&
                   psObj->getType == OBJ_DROID &&
	         ((Droid *)psObj)->droidType == DROID_COMMAND &&
	         psDroid->droidType != DROID_COMMAND &&
	         psDroid->droidType != DROID_CONSTRUCT &&
	         psDroid->droidType != DROID_CYBORG_CONSTRUCT)
	{
		// get a droid to join a command droids group
		DeSelectDroid(psDroid);
		order = DroidOrder(DROID_ORDER_TYPE::COMMANDERSUPPORT, psObj);
	}
	//repair droid
	else if (aiCheckAlliances(psObj->owningPlayer, psDroid->owningPlayer) &&
                 psObj->getType == OBJ_DROID &&
	         (psDroid->droidType == DROID_REPAIR ||
	          psDroid->droidType == DROID_CYBORG_REPAIR) &&
	         droidIsDamaged((Droid *)psObj))
	{
		order = DroidOrder(DROID_ORDER_TYPE::DROIDREPAIR, psObj);
	}
	// guarding constructor droids
	else if (aiCheckAlliances(psObj->owningPlayer, psDroid->owningPlayer) &&
                 psObj->getType == OBJ_DROID &&
	         (((Droid *)psObj)->droidType == DROID_CONSTRUCT ||
	          ((Droid *)psObj)->droidType == DROID_CYBORG_CONSTRUCT ||
	          ((Droid *)psObj)->droidType == DROID_SENSOR ||
	          (((Droid *)psObj)->droidType == DROID_COMMAND && psObj->owningPlayer != psDroid->owningPlayer)) &&
	         (psDroid->droidType == DROID_WEAPON ||
	          psDroid->droidType == DROID_CYBORG ||
	          psDroid->droidType == DROID_CYBORG_SUPER) &&
	         proj_Direct(asWeaponStats + psDroid->m_weaponList[0].nStat))
	{
		order = DroidOrder(DROID_ORDER_TYPE::GUARD, psObj);
		assignSensorTarget(psObj);
		psDroid->selected = false;
	}
	else if (aiCheckAlliances(psObj->owningPlayer, psDroid->owningPlayer) &&
                   psObj->getType == OBJ_STRUCTURE)
	{
          Structure *psStruct = (Structure *) psObj;
		ASSERT_OR_RETURN(DroidOrder(DROID_ORDER_TYPE::NONE), psObj != nullptr, "Invalid structure pointer");

		/* check whether construction droid */
		if (psDroid->droidType == DROID_CONSTRUCT ||
		    psDroid->droidType == DROID_CYBORG_CONSTRUCT)
		{
			int moduleIndex = nextModuleToBuild(psStruct, ctrlShiftDown() ? highestQueuedModule(psDroid, psStruct) : -1);

			//Re-written to allow demolish order to be added to the queuing system
			if (intDemolishSelectMode() && psObj->owningPlayer == psDroid->owningPlayer)
			{
				//check to see if anything is currently trying to build the structure
				//can't build and demolish at the same time!
				if (psStruct->status == SS_BUILT || !checkDroidsBuilding(psStruct))
				{
					order = DroidOrder(DROID_ORDER_TYPE::DEMOLISH, psObj);
				}
			}
			//check for non complete structures
			else if (psStruct->status != SS_BUILT)
			{
				//if something else is demolishing, then help demolish
				if (checkDroidsDemolishing(psStruct))
				{
					order = DroidOrder(DROID_ORDER_TYPE::DEMOLISH, psObj);
				}
				//else help build
				else
				{
					order = DroidOrder(DROID_ORDER_TYPE::HELPBUILD, psObj);
					if (moduleIndex > 0)
					{
						order = DroidOrder(DROID_ORDER_TYPE::BUILDMODULE, psObj, moduleIndex);  // Try scheduling a module, instead.
					}
				}
			}
			else if (psStruct->hitPoints < structureBody(psStruct))
			{
				order = DroidOrder(DROID_ORDER_TYPE::REPAIR, psObj);
			}
			//check if can build a module
			else if (moduleIndex > 0)
			{
				order = DroidOrder(DROID_ORDER_TYPE::BUILDMODULE, psObj, moduleIndex);
			}
		}

		if (order.type == DROID_ORDER_TYPE::NONE)
		{
			/* check repair facility and in need of repair */
			if (psStruct->stats->type == REF_REPAIR_FACILITY &&
			    psStruct->status == SS_BUILT)
			{
				order = DroidOrder(DROID_ORDER_TYPE::RTR_SPECIFIED, psObj);
			}
			else if (electronicDroid(psDroid) &&
			         //psStruct->resistance < (SDWORD)(psStruct->pStructureType->resistance))
			         psStruct->resistance < (SDWORD)structureResistance(psStruct->stats, psStruct->owningPlayer))
			{
				order = DroidOrder(DROID_ORDER_TYPE::RESTORE, psObj);
			}
			//check for counter battery assignment
			else if (structSensorDroidWeapon(psStruct, psDroid))
			{
				order = DroidOrder(DROID_ORDER_TYPE::FIRESUPPORT, psObj);
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
				if (psStruct->stats->type == REF_REARM_PAD)
				{
					//don't bother checking cos we want it to go there if directed
					order = DroidOrder(DROID_ORDER_TYPE::REARM, psObj);
				}
			}
			// Some droids shouldn't be guarding
			else if ((psDroid->droidType == DROID_WEAPON ||
			          psDroid->droidType == DROID_CYBORG ||
			          psDroid->droidType == DROID_CYBORG_SUPER)
			         && proj_Direct(asWeaponStats + psDroid->m_weaponList[0].nStat))
			{
				order = DroidOrder(DROID_ORDER_TYPE::GUARD, psObj);
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
static void orderPlayOrderObjAudio(UDWORD player, GameObject *psObj)
{
	ASSERT_PLAYER_OR_RETURN(, player);

	/* loop over selected droids */
	for (Droid *psDroid = allDroidLists[player]; psDroid; psDroid = psDroid->psNext)
	{
		if (psDroid->selected)
		{
			/* currently only looks for VTOL */
			if (isVtolDroid(psDroid))
			{
				switch (psDroid->order.type)
				{
				case DROID_ORDER_TYPE::ATTACK:
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
void orderSelectedObjAdd(UDWORD player, GameObject *psObj, bool add)
{
	ASSERT_PLAYER_OR_RETURN(, player);

	// note that an order list graphic needs to be displayed
	bOrderEffectDisplayed = false;

	for (Droid *psCurr = allDroidLists[player]; psCurr; psCurr = psCurr->psNext)
	{
		if (psCurr->selected)
		{
			if (isBlueprint(psObj))
			{
				if (isConstructionDroid(psCurr))
				{
					// Help build the planned structure.
#if defined(WZ_CC_GNU) && !defined(WZ_CC_INTEL) && !defined(WZ_CC_CLANG) && (7 <= __GNUC__)
// avoid false-positive "potential null pointer dereference [-Wnull-dereference]"
// `castStructure(psObj)` will not return nullptr, because `isBlueprint(psObj)` above already checks if psObj is a structure type
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wnull-dereference"
#endif
          orderDroidStatsLocDirAdd(
              psCurr, DROID_ORDER_TYPE::BUILD, castStructure(psObj)->stats,
              psObj->getPosition.x, psObj->getPosition.y,
              castStructure(psObj)->rotation.direction, add);
#if defined(WZ_CC_GNU) && !defined(WZ_CC_INTEL) && !defined(WZ_CC_CLANG) && (7 <= __GNUC__)
# pragma GCC diagnostic pop
#endif
				}
				else
				{
					// Help watch the structure being built.
                                        orderDroidLocAdd(
                                            psCurr, DROID_ORDER_TYPE::MOVE,
                                            psObj->getPosition.x,
                                            psObj->getPosition.y, add);
				}
				continue;
			}

			DroidOrder order = chooseOrderObj(psCurr, psObj, specialOrderKeyDown());
			// see if the order can be added to the list
			if (order.type != DROID_ORDER_TYPE::NONE && !orderDroidObjAdd(psCurr, order, add))
			{
				// if not just do it straight off
				orderDroidObj(psCurr, order.type, order.psObj, ModeQueue);
			}
		}
	}

	orderPlayOrderObjAudio(player, psObj);
}


/** This function just calls orderSelectedObjAdd with add = false.*/
void orderSelectedObj(UDWORD player, GameObject *psObj)
{
	ASSERT_PLAYER_OR_RETURN(, player);
	orderSelectedObjAdd(player, psObj, false);
}


/** Given a player, this function send an order with localization and status to selected droids.
 * If add is true, the orders are queued.
 * @todo this function runs through all the player's droids, but only uses the selected ones and the ones that are construction droids. Consider an efficiency improvement.
 */
void orderSelectedStatsLocDir(UDWORD player, DROID_ORDER order,
                              StructureStats *psStats, UDWORD x, UDWORD y, uint16_t direction, bool add)
{
	ASSERT_PLAYER_OR_RETURN(, player);

	for (Droid *psCurr = allDroidLists[player]; psCurr; psCurr = psCurr->psNext)
	{
		if (psCurr->selected && isConstructionDroid(psCurr))
		{
			if (add)
			{
				orderDroidStatsLocDirAdd(psCurr, order, psStats, x, y, direction);
			}
			else
			{
				orderDroidStatsLocDir(psCurr, order, psStats, x, y, direction, ModeQueue);
			}
		}
	}
}


/** Same as orderSelectedStatsLocDir() but with two locations.
 * @todo this function runs through all the player's droids, but only uses the selected ones. Consider an efficiency improvement.
 */
void orderSelectedStatsTwoLocDir(UDWORD player, DROID_ORDER order,
                                 StructureStats *psStats, UDWORD x1, UDWORD y1, UDWORD x2, UDWORD y2, uint16_t direction, bool add)
{
	ASSERT_PLAYER_OR_RETURN(, player);

	for (Droid *psCurr = allDroidLists[player]; psCurr; psCurr = psCurr->psNext)
	{
		if (psCurr->selected)
		{
			if (add)
			{
				orderDroidStatsTwoLocDirAdd(psCurr, order, psStats, x1, y1, x2, y2, direction);
			}
			else
			{
				orderDroidStatsTwoLocDir(psCurr, order, psStats, x1, y1, x2, y2, direction, ModeQueue);
			}
		}
	}
}


/** This function runs though all player's droids to check if any of then is a transporter. Returns the transporter droid if any was found, and NULL else.*/
Droid *FindATransporter(Droid const *embarkee)
{
	bool isCyborg = cyborgDroid(embarkee);

        Droid *bestDroid = nullptr;
	unsigned bestDist = ~0u;

	for (Droid *psDroid = allDroidLists[embarkee->owningPlayer]; psDroid != nullptr; psDroid = psDroid->psNext)
	{
		if ((isCyborg && psDroid->droidType == DROID_TRANSPORTER) || psDroid->droidType == DROID_SUPERTRANSPORTER)
		{
			unsigned dist = iHypot((psDroid->getPosition - embarkee->getPosition).xy());
			if (!checkTransporterSpace(psDroid, embarkee, false))
			{
				dist += 0x8000000;  // Should prefer transports that aren't full.
			}
			if (dist < bestDist)
			{
				bestDroid = psDroid;
				bestDist = dist;
			}
		}
	}

	return bestDroid;
}


/** Given a factory type, this function runs though all player's structures to check if any is of factory getType. Returns the structure if any was found, and NULL else.*/
static Structure *FindAFactory(UDWORD player, UDWORD factoryType)
{
	ASSERT_PLAYER_OR_RETURN(nullptr, player);

	for (Structure *psStruct = apsStructLists[player]; psStruct != nullptr; psStruct = psStruct->psNext)
	{
		if (psStruct->stats->type == factoryType)
		{
			return psStruct;
		}
	}

	return nullptr;
}


/** This function runs though all player's structures to check if any of then is a repair facility. Returns the structure if any was found, and NULL else.*/
static Structure *FindARepairFacility(unsigned player)
{
	ASSERT_PLAYER_OR_RETURN(nullptr, player);

	for (Structure *psStruct = apsStructLists[player]; psStruct != nullptr; psStruct = psStruct->psNext)
	{
		if (psStruct->stats->type == REF_REPAIR_FACILITY)
		{
			return psStruct;
		}
	}

	return nullptr;
}


/** This function returns true if the droid supports the secondary order, and false if not.*/
bool secondarySupported(Droid *psDroid, SECONDARY_ORDER sec)
{
	bool supported;

	supported = true;	// Default to supported.

	switch (sec)
	{
	case DSO_ASSIGN_PRODUCTION:
	case DSO_ASSIGN_CYBORG_PRODUCTION:
	case DSO_ASSIGN_VTOL_PRODUCTION:
	case DSO_CLEAR_PRODUCTION:		// remove production from a command droid
	case DSO_FIRE_DESIGNATOR:
		if (psDroid->droidType != DROID_COMMAND)
		{
			supported = false;
		}
		if ((sec == DSO_ASSIGN_PRODUCTION && FindAFactory(psDroid->owningPlayer, REF_FACTORY) == nullptr) ||
		    (sec == DSO_ASSIGN_CYBORG_PRODUCTION && FindAFactory(psDroid->owningPlayer, REF_CYBORG_FACTORY) == nullptr) ||
		    (sec == DSO_ASSIGN_VTOL_PRODUCTION && FindAFactory(psDroid->owningPlayer, REF_VTOL_FACTORY) == nullptr))
		{
			supported = false;
		}
		// don't allow factories to be assigned to commanders during a Limbo Expand mission
		if ((sec == DSO_ASSIGN_PRODUCTION || sec == DSO_ASSIGN_CYBORG_PRODUCTION || sec == DSO_ASSIGN_VTOL_PRODUCTION)
		    && missionLimboExpand())
		{
			supported = false;
		}
		break;

	case DSO_ATTACK_RANGE:
		if (psDroid->droidType == DROID_SENSOR)
		{
			supported = false;
		}
		// don't show the range levels if the droid doesn't have a weapon with different ranges
		if (psDroid->numWeapons > 0)
		{
			for (unsigned i = 0; i < psDroid->numWeapons; ++i)
			{
				const WEAPON_STATS *weaponStats = asWeaponStats + psDroid->m_weaponList[i].nStat;

				if (proj_GetLongRange(weaponStats, psDroid->owningPlayer) == proj_GetShortRange(weaponStats, psDroid->owningPlayer))
				{
					supported = false;
				}
				else
				{
					supported = true;
					break;
				}
			}
		}
		// fall-through

	case DSO_ATTACK_LEVEL:
		if (psDroid->droidType == DROID_REPAIR || psDroid->droidType == DROID_CYBORG_REPAIR)
		{
			supported = false;
		}
		if (psDroid->droidType == DROID_CONSTRUCT || psDroid->droidType == DROID_CYBORG_CONSTRUCT)
		{
			supported = false;
		}
		if (psDroid->droidType == DROID_ECM || objRadarDetector(psDroid))
		{
			supported = false;
		}
		break;

	case DSO_CIRCLE:
		if (!isVtolDroid(psDroid))
		{
			supported = false;
		}
		break;

	case DSO_REPAIR_LEVEL:
	case DSO_PATROL:
	case DSO_HALTTYPE:
	case DSO_RETURN_TO_LOC:
		break;

	case DSO_RECYCLE:			// Only if player has got a factory.
		if ((FindAFactory(psDroid->owningPlayer, REF_FACTORY) == nullptr) &&
		    (FindAFactory(psDroid->owningPlayer, REF_CYBORG_FACTORY) == nullptr) &&
		    (FindAFactory(psDroid->owningPlayer, REF_VTOL_FACTORY) == nullptr) &&
		    (FindARepairFacility(psDroid->owningPlayer) == nullptr))
		{
			supported = false;
		}
		break;

	default:
		supported = false;
		break;
	}

	return supported;
}


/** This function returns the droid order's secondary state of the secondary order.*/
SECONDARY_STATE secondaryGetState(Droid *psDroid, SECONDARY_ORDER sec, QUEUE_MODE mode)
{
	uint32_t state = psDroid->secondaryOrder;

	if (mode == ModeQueue)
	{
		state = psDroid->secondaryOrderPending;  // The UI wants to know the state, so return what the state will be after orders are synchronised.
	}

	switch (sec)
	{
	case DSO_ATTACK_RANGE:
		return (SECONDARY_STATE)(state & DSS_ARANGE_MASK);
		break;
	case DSO_REPAIR_LEVEL:
		return (SECONDARY_STATE)(state & DSS_REPLEV_MASK);
		break;
	case DSO_ATTACK_LEVEL:
		return (SECONDARY_STATE)(state & DSS_ALEV_MASK);
		break;
	case DSO_ASSIGN_PRODUCTION:
	case DSO_ASSIGN_CYBORG_PRODUCTION:
	case DSO_ASSIGN_VTOL_PRODUCTION:
		return (SECONDARY_STATE)(state & DSS_ASSPROD_MASK);
		break;
	case DSO_RECYCLE:
		return (SECONDARY_STATE)(state & DSS_RECYCLE_MASK);
		break;
	case DSO_PATROL:
		return (SECONDARY_STATE)(state & DSS_PATROL_MASK);
		break;
	case DSO_CIRCLE:
		return (SECONDARY_STATE)(state & DSS_CIRCLE_MASK);
		break;
	case DSO_HALTTYPE:
		if (psDroid->order.type == DROID_ORDER_TYPE::HOLD)
		{
			return DSS_HALT_HOLD;
		}
		return (SECONDARY_STATE)(state & DSS_HALT_MASK);
		break;
	case DSO_RETURN_TO_LOC:
		return (SECONDARY_STATE)(state & DSS_RTL_MASK);
		break;
	case DSO_FIRE_DESIGNATOR:
		if (cmdDroidGetDesignator(psDroid->owningPlayer) == psDroid)
		{
			return DSS_FIREDES_SET;
		}
		break;
	default:
		break;
	}

	return DSS_NONE;
}


#ifdef DEBUG
static char *secondaryPrintFactories(UDWORD state)
{
	static		char aBuff[255];

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
static bool secondaryCheckDamageLevelDeselect(Droid *psDroid, SECONDARY_STATE repairState)
{
	unsigned repairLevel;
	switch (repairState)
	{
	case DSS_REPLEV_LOW:   repairLevel = REPAIRLEV_HIGH; break;  // LOW → HIGH, seems DSS_REPLEV_LOW and DSS_REPLEV_HIGH are badly named?
	case DSS_REPLEV_HIGH:  repairLevel = REPAIRLEV_LOW;  break;
	default:
	case DSS_REPLEV_NEVER: repairLevel = 0;              break;
	}

	// psDroid->body / psDroid->originalBody < repairLevel / 100, without integer truncation
	if (psDroid->hitPoints * 100 <= repairLevel * psDroid->originalBody)
	{
		// Only deselect the droid if there is another droid selected.
		if (psDroid->selected && selectedPlayer < MAX_PLAYERS)
		{
                  Droid *psTempDroid;
			for (psTempDroid = allDroidLists[selectedPlayer]; psTempDroid; psTempDroid = psTempDroid->psNext)
			{
				if (psTempDroid != psDroid && psTempDroid->selected)
				{
					DeSelectDroid(psDroid);
					break;
				}
			}
		}
		return true;
	}
	return false;
}

/** This function checks the droid damage level against its secondary state. If the damage level is too high, then it sends an order to the droid to return to repair.*/
void secondaryCheckDamageLevel(Droid *psDroid)
{
	if (secondaryCheckDamageLevelDeselect(psDroid, secondaryGetState(psDroid, DSO_REPAIR_LEVEL)))
	{
		if (!isVtolDroid(psDroid))
		{
			psDroid->group = UBYTE_MAX;
		}

		/* set return to repair if not on hold */
		if (psDroid->order.type != DROID_ORDER_TYPE::RTR &&
		    psDroid->order.type != DROID_ORDER_TYPE::RTB &&
		    !vtolRearming(psDroid))
		{
			if (isVtolDroid(psDroid))
			{
				moveToRearm(psDroid);
			}
			else
			{
				RtrBestResult result = decideWhereToRepairAndBalance(psDroid);
				if (result.type == RTR_TYPE_REPAIR_FACILITY)
				{
					ASSERT(result.psObj != nullptr, "RTR_FACILITY but target is null");
					orderDroidObj(psDroid, DROID_ORDER_TYPE::RTR, result.psObj, ModeImmediate);
					return;
				}
				else if (result.type == RTR_TYPE_HQ)
				{
					ASSERT(result.psObj != nullptr, "RTR_TYPE_HQ but target is null");
					orderDroid(psDroid, DROID_ORDER_TYPE::RTB, ModeImmediate);
					return;
				}
				else if (result.type == RTR_TYPE_DROID)
				{
					ASSERT(result.psObj != nullptr, "RTR_DROID but target is null");
					orderDroidObj(psDroid, DROID_ORDER_TYPE::RTR, result.psObj, ModeImmediate);
				}

			}
		}
	}
}




/** This function assigns all droids of the group to the state.
 * @todo this function runs through all the player's droids. Consider something more efficient to select a group.
 * @todo SECONDARY_STATE argument is called "state", which is not current style. Suggestion to change it to "pState".
 */
static void secondarySetGroupState(UDWORD player, UDWORD group, SECONDARY_ORDER sec, SECONDARY_STATE state)
{
	ASSERT_PLAYER_OR_RETURN(, player);

	for (Droid *psCurr = allDroidLists[player]; psCurr; psCurr = psCurr->psNext)
	{
		if (psCurr->group == group &&
		    secondaryGetState(psCurr, sec) != state)
		{
			secondarySetState(psCurr, sec, state);
		}
	}
}


/** This function returns the average secondary state of a numerical group of a player.
 * @todo this function runs through all the player's droids. Consider something more efficient to select a group.
 * @todo this function uses a "local" define. Consider removing it, refactoring this function.
 */
static SECONDARY_STATE secondaryGetAverageGroupState(UDWORD player, UDWORD group, UDWORD mask)
{
	ASSERT_PLAYER_OR_RETURN(DSS_NONE, player);

#define MAX_STATES		5
	struct
	{
		UDWORD state, num;
	} aStateCount[MAX_STATES];
	SDWORD	i, numStates, max;
        Droid *psCurr;

	// count the number of units for each state
	numStates = 0;
	memset(aStateCount, 0, sizeof(aStateCount));
	for (psCurr = allDroidLists[player]; psCurr; psCurr = psCurr->psNext)
	{
		if (psCurr->group == group)
		{
			for (i = 0; i < numStates; i++)
			{
				if (aStateCount[i].state == (psCurr->secondaryOrder & mask))
				{
					aStateCount[i].num += 1;
					break;
				}
			}

			if (i == numStates)
			{
				aStateCount[numStates].state = psCurr->secondaryOrder & mask;
				aStateCount[numStates].num = 1;
				numStates += 1;
			}
		}
	}

	max = 0;
	for (i = 0; i < numStates; i++)
	{
		if (aStateCount[i].num > aStateCount[max].num)
		{
			max = i;
		}
	}

	return (SECONDARY_STATE)aStateCount[max].state;
}


/** This function sets all the group's members to have the same secondary state as the average secondary state of the group.
 * @todo this function runs through all the player's droids. Consider something more efficient to select a group.
 * @todo this function uses a "local" define. Consider removing it, refactoring this function.
 */
void secondarySetAverageGroupState(UDWORD player, UDWORD group)
{
	ASSERT_PLAYER_OR_RETURN(, player);

	// lookup table for orders and masks
#define MAX_ORDERS	4
	struct
	{
		SECONDARY_ORDER order;
		UDWORD mask;
	} aOrders[MAX_ORDERS] =
	{
		{ DSO_ATTACK_RANGE, DSS_ARANGE_MASK },
		{ DSO_REPAIR_LEVEL, DSS_REPLEV_MASK },
		{ DSO_ATTACK_LEVEL, DSS_ALEV_MASK },
		{ DSO_HALTTYPE, DSS_HALT_MASK },
	};
	SDWORD	i, state;

	for (i = 0; i < MAX_ORDERS; i++)
	{
		state = secondaryGetAverageGroupState(player, group, aOrders[i].mask);
		secondarySetGroupState(player, group, aOrders[i].order, (SECONDARY_STATE)state);
	}
}


/** This function changes the structure's secondary state to be the function input's state.
 * Returns true if the function changed the structure's state, and false if it did not.
 * @todo SECONDARY_STATE argument is called "State", which is not current style. Suggestion to change it to "pState".
 */
bool setFactoryState(Structure *psStruct, SECONDARY_ORDER sec, SECONDARY_STATE State)
{
	if (!StructIsFactory(psStruct))
	{
		ASSERT(false, "setFactoryState: structure is not a factory");
		return false;
	}

        Factory *psFactory = (Factory *)psStruct->pFunctionality;

	UDWORD CurrState = psFactory->secondaryOrder;

	bool retVal = true;
	switch (sec)
	{
	case DSO_ATTACK_RANGE:
		CurrState = (CurrState & ~DSS_ARANGE_MASK) | State;
		break;

	case DSO_REPAIR_LEVEL:
		CurrState = (CurrState & ~DSS_REPLEV_MASK) | State;
		break;

	case DSO_ATTACK_LEVEL:
		CurrState = (CurrState & ~DSS_ALEV_MASK) | State;
		break;

	case DSO_PATROL:
		if (State & DSS_PATROL_SET)
		{
			CurrState |= DSS_PATROL_SET;
		}
		else
		{
			CurrState &= ~DSS_PATROL_MASK;
		}
		break;

	case DSO_HALTTYPE:
		switch (State & DSS_HALT_MASK)
		{
		case DSS_HALT_PURSUE:
			CurrState &= ~ DSS_HALT_MASK;
			CurrState |= DSS_HALT_PURSUE;
			break;
		case DSS_HALT_GUARD:
			CurrState &= ~ DSS_HALT_MASK;
			CurrState |= DSS_HALT_GUARD;
			break;
		case DSS_HALT_HOLD:
			CurrState &= ~ DSS_HALT_MASK;
			CurrState |= DSS_HALT_HOLD;
			break;
		}
		break;
	default:
		break;
	}

	psFactory->secondaryOrder = CurrState;

	return retVal;
}


/** This function sets the structure's secondary state to be pState.
 *  return true except on an ASSERT (which is not a good design.)
 *  or, an invalid factory.
 */
bool getFactoryState(Structure *psStruct, SECONDARY_ORDER sec, SECONDARY_STATE *pState)
{
	ASSERT_OR_RETURN(false, StructIsFactory(psStruct), "Structure is not a factory");
	if ((Factory *)psStruct->pFunctionality)
	{
		UDWORD state = ((Factory *)psStruct->pFunctionality)->secondaryOrder;

		switch (sec)
		{
		case DSO_ATTACK_RANGE:
			*pState = (SECONDARY_STATE)(state & DSS_ARANGE_MASK);
			break;
		case DSO_REPAIR_LEVEL:
			*pState = (SECONDARY_STATE)(state & DSS_REPLEV_MASK);
			break;
		case DSO_ATTACK_LEVEL:
			*pState = (SECONDARY_STATE)(state & DSS_ALEV_MASK);
			break;
		case DSO_PATROL:
			*pState = (SECONDARY_STATE)(state & DSS_PATROL_MASK);
			break;
		case DSO_HALTTYPE:
			*pState = (SECONDARY_STATE)(state & DSS_HALT_MASK);
			break;
		default:
			*pState = (SECONDARY_STATE)0;
			break;
		}

		return true;
	}
	return false;
}


/** lasSat structure can select a target
 * @todo improve documentation: it is not clear what this function performs by the current documentation.
 */
void orderStructureObj(UDWORD player, GameObject *psObj)
{
	ASSERT_PLAYER_OR_RETURN(, player);

        Structure *psStruct;

	for (psStruct = apsStructLists[player]; psStruct; psStruct = psStruct->psNext)
	{
		if (lasSatStructSelected(psStruct))
		{
			// send the weapon fire
			sendLasSat(player, psStruct, psObj);

			break;
		}
	}
}


/** This function maps the order enum to its name, returning its enum name as a (const char*)
 * Formally, this function is equivalent to a stl map: for a given key (enum), returns a mapped value (char*).
 */
const char *getDroidOrderName(DROID_ORDER order)
{
	switch (order)
	{
	case DROID_ORDER_TYPE::NONE:                     return "DROID_ORDER_TYPE::NONE";
	case DROID_ORDER_TYPE::STOP:                     return "DROID_ORDER_TYPE::STOP";
	case DROID_ORDER_TYPE::MOVE:                     return "DROID_ORDER_TYPE::MOVE";
	case DROID_ORDER_TYPE::ATTACK:                   return "DROID_ORDER_TYPE::ATTACK";
	case DROID_ORDER_TYPE::BUILD:                    return "DROID_ORDER_TYPE::BUILD";
	case DROID_ORDER_TYPE::HELPBUILD:                return "DROID_ORDER_TYPE::HELPBUILD";
	case DROID_ORDER_TYPE::LINEBUILD:                return "DROID_ORDER_TYPE::LINEBUILD";
	case DROID_ORDER_TYPE::DEMOLISH:                 return "DROID_ORDER_TYPE::DEMOLISH";
	case DROID_ORDER_TYPE::REPAIR:                   return "DROID_ORDER_TYPE::REPAIR";
	case DROID_ORDER_TYPE::OBSERVE:                  return "DROID_ORDER_TYPE::OBSERVE";
	case DROID_ORDER_TYPE::FIRESUPPORT:              return "DROID_ORDER_TYPE::FIRESUPPORT";
	case DROID_ORDER_TYPE::RTB:                      return "DROID_ORDER_TYPE::RTB";
	case DROID_ORDER_TYPE::RTR:                      return "DROID_ORDER_TYPE::RTR";
	case DROID_ORDER_TYPE::UNUSED_5:                 return "DROID_ORDER_TYPE::UNUSED_5";
	case DROID_ORDER_TYPE::EMBARK:                   return "DROID_ORDER_TYPE::EMBARK";
	case DROID_ORDER_TYPE::DISEMBARK:                return "DROID_ORDER_TYPE::DISEMBARK";
	case DROID_ORDER_TYPE::ATTACKTARGET:             return "DROID_ORDER_TYPE::ATTACKTARGET";
	case DROID_ORDER_TYPE::COMMANDERSUPPORT:         return "DROID_ORDER_TYPE::COMMANDERSUPPORT";
	case DROID_ORDER_TYPE::BUILDMODULE:              return "DROID_ORDER_TYPE::BUILDMODULE";
	case DROID_ORDER_TYPE::RECYCLE:                  return "DROID_ORDER_TYPE::RECYCLE";
	case DROID_ORDER_TYPE::TRANSPORTOUT:             return "DROID_ORDER_TYPE::TRANSPORTOUT";
	case DROID_ORDER_TYPE::TRANSPORTIN:              return "DROID_ORDER_TYPE::TRANSPORTIN";
	case DROID_ORDER_TYPE::TRANSPORTRETURN:          return "DROID_ORDER_TYPE::TRANSPORTRETURN";
	case DROID_ORDER_TYPE::GUARD:                    return "DROID_ORDER_TYPE::GUARD";
	case DROID_ORDER_TYPE::DROIDREPAIR:              return "DROID_ORDER_TYPE::DROIDREPAIR";
	case DROID_ORDER_TYPE::RESTORE:                  return "DROID_ORDER_TYPE::RESTORE";
	case DROID_ORDER_TYPE::SCOUT:                    return "DROID_ORDER_TYPE::SCOUT";
	case DROID_ORDER_TYPE::UNUSED_3:                 return "DROID_ORDER_TYPE::UNUSED_3";
	case DROID_ORDER_TYPE::UNUSED:                   return "DROID_ORDER_TYPE::UNUSED";
	case DROID_ORDER_TYPE::PATROL:                   return "DROID_ORDER_TYPE::PATROL";
	case DROID_ORDER_TYPE::REARM:                    return "DROID_ORDER_TYPE::REARM";
	case DROID_ORDER_TYPE::RECOVER:                  return "DROID_ORDER_TYPE::RECOVER";
	case DROID_ORDER_TYPE::UNUSED_6:                 return "DROID_ORDER_TYPE::UNUSED_6";
	case DROID_ORDER_TYPE::RTR_SPECIFIED:            return "DROID_ORDER_TYPE::RTR_SPECIFIED";
	case DROID_ORDER_TYPE::CIRCLE:                   return "DROID_ORDER_TYPE::CIRCLE";
	case DROID_ORDER_TYPE::HOLD:                     return "DROID_ORDER_TYPE::HOLD";
	};

	ASSERT(false, "DROID_ORDER out of range: %u", order);

	return "DROID_ORDER_TYPE::#INVALID#";
}

const char *getDroidOrderKey(DROID_ORDER order)
{
	switch (order)
	{
	case DROID_ORDER_TYPE::NONE:                     return "N";
	case DROID_ORDER_TYPE::STOP:                     return "Stop";
	case DROID_ORDER_TYPE::MOVE:                     return "M";
	case DROID_ORDER_TYPE::ATTACK:                   return "A";
	case DROID_ORDER_TYPE::BUILD:                    return "B";
	case DROID_ORDER_TYPE::HELPBUILD:                return "hB";
	case DROID_ORDER_TYPE::LINEBUILD:                return "lB";
	case DROID_ORDER_TYPE::DEMOLISH:                 return "D";
	case DROID_ORDER_TYPE::REPAIR:                   return "R";
	case DROID_ORDER_TYPE::OBSERVE:                  return "O";
	case DROID_ORDER_TYPE::FIRESUPPORT:              return "F";
	case DROID_ORDER_TYPE::UNUSED_4:                 return "Err";
	case DROID_ORDER_TYPE::UNUSED_2:                 return "Err";
	case DROID_ORDER_TYPE::RTB:                      return "RTB";
	case DROID_ORDER_TYPE::RTR:                      return "RTR";
	case DROID_ORDER_TYPE::UNUSED_5:                 return "Err";
	case DROID_ORDER_TYPE::EMBARK:                   return "E";
	case DROID_ORDER_TYPE::DISEMBARK:                return "!E";
	case DROID_ORDER_TYPE::ATTACKTARGET:             return "AT";
	case DROID_ORDER_TYPE::COMMANDERSUPPORT:         return "CS";
	case DROID_ORDER_TYPE::BUILDMODULE:              return "BM";
	case DROID_ORDER_TYPE::RECYCLE:                  return "RCY";
	case DROID_ORDER_TYPE::TRANSPORTOUT:             return "To";
	case DROID_ORDER_TYPE::TRANSPORTIN:              return "Ti";
	case DROID_ORDER_TYPE::TRANSPORTRETURN:          return "Tr";
	case DROID_ORDER_TYPE::GUARD:                    return "G";
	case DROID_ORDER_TYPE::DROIDREPAIR:              return "DR";
	case DROID_ORDER_TYPE::RESTORE:                  return "RES";
	case DROID_ORDER_TYPE::SCOUT:                    return "S";
	case DROID_ORDER_TYPE::UNUSED_3:                 return "Err";
	case DROID_ORDER_TYPE::UNUSED:                   return "Err";
	case DROID_ORDER_TYPE::PATROL:                   return "P";
	case DROID_ORDER_TYPE::REARM:                    return "RE";
	case DROID_ORDER_TYPE::RECOVER:                  return "RCV";
	case DROID_ORDER_TYPE::UNUSED_6:                 return "Err";
	case DROID_ORDER_TYPE::RTR_SPECIFIED:            return "RTR";
	case DROID_ORDER_TYPE::CIRCLE:                   return "C";
	case DROID_ORDER_TYPE::HOLD:                     return "H";
	};
	ASSERT(false, "DROID_ORDER out of range: %u", order);
	return "Err";
}