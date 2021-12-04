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

/** This function checks if the droid is off range. If yes, it uses actionDroid() to make the droid to move to its target if its target is on range, or to move to its order position if not.
 * @todo droid doesn't shoot while returning to the guard position.
 */
static void orderCheckGuardPosition(Droid *psDroid, SDWORD range)
{
	if (psDroid->order.psObj != nullptr)
	{
		UDWORD x, y;

		// repair droids always follow behind - don't want them jumping into the line of fire
		if ((!(psDroid->droidType == DROID_REPAIR || psDroid->droidType == DROID_CYBORG_REPAIR))
		    && psDroid->order.psObj->type == OBJ_DROID && orderStateLoc((Droid *)psDroid->order.psObj, DROID_ORDER_TYPE::MOVE, &x, &y))
		{
			// got a moving droid - check against where the unit is going
			psDroid->order.pos = Vector2i(x, y);
		}
		else
		{
			psDroid->order.pos = psDroid->order.psObj->position.xy();
		}
	}

	int xdiff = psDroid->position.x - psDroid->order.pos.x;
	int ydiff = psDroid->position.y - psDroid->order.pos.y;
	if (xdiff * xdiff + ydiff * ydiff > range * range)
	{
		if ((psDroid->sMove.Status != MOVEINACTIVE) &&
		    ((psDroid->action == DROID_ACTION::MOVE) ||
		     (psDroid->action == DROID_ACTION::MOVEFIRE)))
		{
			xdiff = psDroid->sMove.destination.x - psDroid->order.pos.x;
			ydiff = psDroid->sMove.destination.y - psDroid->order.pos.y;
			if (xdiff * xdiff + ydiff * ydiff > range * range)
			{
				actionDroid(psDroid, DROID_ACTION::MOVE, psDroid->order.pos.x, psDroid->order.pos.y);
			}
		}
		else
		{
			actionDroid(psDroid, DROID_ACTION::MOVE, psDroid->order.pos.x, psDroid->order.pos.y);
		}
	}
}


/** This function checks if there are any damaged droids within a defined range.
 * It returns the damaged droid if there is any, or nullptr if none was found.
 */
Droid *checkForRepairRange(Droid *psDroid)
{
  Droid *psFailedTarget = nullptr;
	if (psDroid->action == DROID_ACTION::SULK)
	{
		psFailedTarget = (Droid *)psDroid->psActionTarget[0];
	}

	ASSERT(psDroid->droidType == DROID_REPAIR || psDroid->droidType == DROID_CYBORG_REPAIR, "Invalid droid type");

	unsigned radius = ((psDroid->order.type == DROID_ORDER_TYPE::HOLD) || (psDroid->order.type == DROID_ORDER_TYPE::NONE && secondaryGetState(psDroid, DSO_HALTTYPE) == DSS_HALT_HOLD)) ? REPAIR_RANGE : REPAIR_MAXDIST;

	unsigned bestDistanceSq = radius * radius;
        Droid *best = nullptr;

	for (GameObject *object : gridStartIterate(psDroid->position.x, psDroid->position.y, radius))
	{
		unsigned distanceSq = droidSqDist(psDroid, object);  // droidSqDist returns -1 if unreachable, (unsigned)-1 is a big number.
		if (object == orderStateObj(psDroid, DROID_ORDER_TYPE::GUARD))
		{
			distanceSq = 0;  // If guarding a unit — always do that first.
		}

                Droid *droid = castDroid(object);
		if (droid != nullptr &&  // Must be a droid.
		    droid != psFailedTarget &&   // Must not have just failed to reach it.
		    distanceSq <= bestDistanceSq &&  // Must be as close as possible.
		    aiCheckAlliances(psDroid->owningPlayer, droid->owningPlayer) &&  // Must be a friendly droid.
		    droidIsDamaged(droid) &&  // Must need repairing.
		    visibleObject(psDroid, droid, false))  // Must be able to sense it.
		{
			bestDistanceSq = distanceSq;
			best = droid;
		}
	}

	return best;
}

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

	for (GameObject *object : gridStartIterate(psDroid->position.x, psDroid->position.y, radius))
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

static bool tryDoRepairlikeAction(Droid *psDroid)
{
	if (isRepairlikeAction(psDroid->action))
	{
		return true;  // Already doing something.
	}

	switch (psDroid->droidType)
	{
		case DROID_REPAIR:
		case DROID_CYBORG_REPAIR:
			//repair droids default to repairing droids within a given range
			if (Droid *repairTarget = checkForRepairRange(psDroid))
			{
				actionDroid(psDroid, DROID_ACTION::DROIDREPAIR, repairTarget);
			}
			break;
		case DROID_CONSTRUCT:
		case DROID_CYBORG_CONSTRUCT:
		{
			//construct droids default to repairing and helping structures within a given range
			auto damaged = checkForDamagedStruct(psDroid);
			if (damaged.second == DROID_ACTION::REPAIR)
			{
				actionDroid(psDroid, damaged.second, damaged.first);
			}
			else if (damaged.second == DROID_ACTION::BUILD)
			{
				psDroid->order.psStats = damaged.first->stats;
				psDroid->order.direction = damaged.first->rotation.direction;
				actionDroid(psDroid, damaged.second, damaged.first->position.x, damaged.first->position.y);
			}
			break;
		}
		default:
			return false;
	}
	return true;
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
			int currdist = objPosDiffSq(psCurr->position, psData->psObj->position);
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
	switch (psObj->type)
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


/** This function actually tells the droid to perform the psOrder.
 * This function is called everytime to send a direct order to a droid.
 */
void orderDroidBase(Droid *psDroid, DROID_ORDER_DATA *psOrder)
{
	UDWORD		iFactoryDistSq;
        Structure *psStruct, *psFactory;
	const PROPULSION_STATS *psPropStats = asPropulsionStats + psDroid->asBits[COMP_PROPULSION];
	const Vector3i rPos(psOrder->pos, 0);
	syncDebugDroid(psDroid, '-');
	syncDebug("%d ordered %s", psDroid->id, getDroidOrderName(psOrder->type));
	objTrace(psDroid->id, "base set order to %s (was %s)", getDroidOrderName(psOrder->type), getDroidOrderName(psDroid->order.type));
	if (psOrder->type != DROID_ORDER_TYPE::TRANSPORTIN         // transporters special
	    && psOrder->psObj == nullptr			// location-type order
	    && (validOrderForLoc(psOrder->type) || psOrder->type == DROID_ORDER_TYPE::BUILD)
	    && !fpathCheck(psDroid->position, rPos, psPropStats->propulsionType))
	{
		if (!isHumanPlayer(psDroid->owningPlayer))
		{
			debug(LOG_SCRIPT, "Invalid order %s given to player %d's %s for position (%d, %d) - ignoring",
			      getDroidOrderName(psOrder->type), psDroid->owningPlayer, droidGetName(psDroid), psOrder->pos.x, psOrder->pos.y);
		}
		objTrace(psDroid->id, "Invalid order %s for position (%d, %d) - ignoring", getDroidOrderName(psOrder->type), psOrder->pos.x, psOrder->pos.y);
		syncDebugDroid(psDroid, '?');
		return;
	}

	// deal with a droid receiving a primary order
	if (!isTransporter(psDroid) && psOrder->type != DROID_ORDER_TYPE::NONE && psOrder->type != DROID_ORDER_TYPE::STOP && psOrder->type != DROID_ORDER_TYPE::GUARD)
	{
		// reset secondary order
		const unsigned oldState = psDroid->secondaryOrder;
		psDroid->secondaryOrder &= ~(DSS_RTL_MASK | DSS_RECYCLE_MASK | DSS_PATROL_MASK);
		psDroid->secondaryOrderPending &= ~(DSS_RTL_MASK | DSS_RECYCLE_MASK | DSS_PATROL_MASK);
		objTrace(psDroid->id, "secondary order reset due to primary order set");
		if (oldState != psDroid->secondaryOrder && psDroid->owningPlayer == selectedPlayer)
		{
			intRefreshScreen();
		}
	}

	// if this is a command droid - all it's units do the same thing
	if ((psDroid->droidType == DROID_COMMAND) &&
	    (psDroid->psGroup != nullptr) &&
	    (psDroid->psGroup->type == GT_COMMAND) &&
	    (psOrder->type != DROID_ORDER_TYPE::GUARD) &&  //(psOrder->psObj == NULL)) &&
	    (psOrder->type != DROID_ORDER_TYPE::RTR) &&
	    (psOrder->type != DROID_ORDER_TYPE::RECYCLE))
	{
		if (psOrder->type == DROID_ORDER_TYPE::ATTACK)
		{
			// change to attacktarget so that the group members
			// guard order does not get canceled
			psOrder->type = DROID_ORDER_TYPE::ATTACKTARGET;
			orderCmdGroupBase(psDroid->psGroup, psOrder);
			psOrder->type = DROID_ORDER_TYPE::ATTACK;
		}
		else
		{
			orderCmdGroupBase(psDroid->psGroup, psOrder);
		}

		// the commander doesn't have to pick up artifacts, one
		// of his units will do it for him (if there are any in his group).
		if ((psOrder->type == DROID_ORDER_TYPE::RECOVER) &&
		    (psDroid->psGroup->psList != nullptr))
		{
			psOrder->type = DROID_ORDER_TYPE::NONE;
		}
	}

	// A selected campaign transporter shouldn't be given orders by the player.
	// Campaign transporter selection is required for it to be tracked by the camera, and
	// should be the only case when it does get selected.
	if (isTransporter(psDroid) &&
		!bMultiPlayer &&
		psDroid->selected &&
		(psOrder->type != DROID_ORDER_TYPE::TRANSPORTOUT &&
		psOrder->type != DROID_ORDER_TYPE::TRANSPORTIN &&
		psOrder->type != DROID_ORDER_TYPE::TRANSPORTRETURN))
	{
		return;
	}

	switch (psOrder->type)
	{
	case DROID_ORDER_TYPE::NONE:
		// used when choose order cannot assign an order
		break;
	case DROID_ORDER_TYPE::STOP:
		// get the droid to stop doing whatever it is doing
		actionDroid(psDroid, DROID_ACTION::NONE);
		psDroid->order = DroidOrder(DROID_ORDER_TYPE::NONE);
		break;
	case DROID_ORDER_TYPE::HOLD:
		// get the droid to stop doing whatever it is doing and temp hold
		actionDroid(psDroid, DROID_ACTION::NONE);
		psDroid->order = *psOrder;
		break;
	case DROID_ORDER_TYPE::MOVE:
	case DROID_ORDER_TYPE::SCOUT:
		// can't move vtols to blocking tiles
		if (isVtolDroid(psDroid)
		    && fpathBlockingTile(map_coord(psOrder->pos), getPropulsionStats(psDroid)->propulsionType))
		{
			break;
		}
		//in multiPlayer, cannot move Transporter to blocking tile either
		if (game.type == LEVEL_TYPE::SKIRMISH
		    && isTransporter(psDroid)
		    && fpathBlockingTile(map_coord(psOrder->pos), getPropulsionStats(psDroid)->propulsionType))
		{
			break;
		}
		// move a droid to a location
		psDroid->order = *psOrder;
		actionDroid(psDroid, DROID_ACTION::MOVE, psOrder->pos.x, psOrder->pos.y);
		break;
	case DROID_ORDER_TYPE::PATROL:
		psDroid->order = *psOrder;
		psDroid->order.pos2 = psDroid->position.xy();
		actionDroid(psDroid, DROID_ACTION::MOVE, psOrder->pos.x, psOrder->pos.y);
		break;
	case DROID_ORDER_TYPE::RECOVER:
		psDroid->order = *psOrder;
		actionDroid(psDroid, DROID_ACTION::MOVE, psOrder->psObj->position.x, psOrder->psObj->position.y);
		break;
	case DROID_ORDER_TYPE::TRANSPORTOUT:
		// tell a (transporter) droid to leave home base for the offworld mission
		psDroid->order = *psOrder;
		actionDroid(psDroid, DROID_ACTION::TRANSPORTOUT, psOrder->pos.x, psOrder->pos.y);
		break;
	case DROID_ORDER_TYPE::TRANSPORTRETURN:
		// tell a (transporter) droid to return after unloading
		psDroid->order = *psOrder;
		actionDroid(psDroid, DROID_ACTION::TRANSPORTOUT, psOrder->pos.x, psOrder->pos.y);
		break;
	case DROID_ORDER_TYPE::TRANSPORTIN:
		// tell a (transporter) droid to fly onworld
		psDroid->order = *psOrder;
		actionDroid(psDroid, DROID_ACTION::TRANSPORTIN, psOrder->pos.x, psOrder->pos.y);
		break;
	case DROID_ORDER_TYPE::ATTACK:
	case DROID_ORDER_TYPE::ATTACKTARGET:
		if (psDroid->numWeapons == 0
		    || psDroid->m_weaponList[0].nStat == 0
		    || isTransporter(psDroid))
		{
			break;
		}
		else if (psDroid->order.type == DROID_ORDER_TYPE::GUARD && psOrder->type == DROID_ORDER_TYPE::ATTACKTARGET)
		{
			// attacking something while guarding, don't change the order
			actionDroid(psDroid, DROID_ACTION::ATTACK, psOrder->psObj);
		}
		else if (psOrder->psObj && !psOrder->psObj->deathTime)
		{
			//cannot attack a Transporter with EW in multiPlayer
			// FIXME: Why not ?
			if (game.type == LEVEL_TYPE::SKIRMISH && electronicDroid(psDroid)
			    && psOrder->psObj->type == OBJ_DROID && isTransporter((Droid *)psOrder->psObj))
			{
				break;
			}
			psDroid->order = *psOrder;

			if (isVtolDroid(psDroid)
				|| actionInRange(psDroid, psOrder->psObj, 0)
				|| ((psOrder->type == DROID_ORDER_TYPE::ATTACKTARGET || psOrder->type == DROID_ORDER_TYPE::ATTACK)  && secondaryGetState(psDroid, DSO_HALTTYPE) == DSS_HALT_HOLD))
			{
				// when DSS_HALT_HOLD, don't move to attack
				actionDroid(psDroid, DROID_ACTION::ATTACK, psOrder->psObj);
			}
			else
			{
				actionDroid(psDroid, DROID_ACTION::MOVE, psOrder->psObj->position.x, psOrder->psObj->position.y);
			}
		}
		break;
	case DROID_ORDER_TYPE::BUILD:
	case DROID_ORDER_TYPE::LINEBUILD:
		// build a new structure or line of structures
		ASSERT_OR_RETURN(, isConstructionDroid(psDroid), "%s cannot construct things!", objInfo(psDroid));
		ASSERT_OR_RETURN(, psOrder->psStats != nullptr, "invalid structure stats pointer");
		psDroid->order = *psOrder;
		ASSERT_OR_RETURN(, !psDroid->order.psStats || psDroid->order.psStats->type != REF_DEMOLISH, "Cannot build demolition");
		actionDroid(psDroid, DROID_ACTION::BUILD, psOrder->pos.x, psOrder->pos.y);
		objTrace(psDroid->id, "Starting new construction effort of %s", psOrder->psStats ? getStatsName(psOrder->psStats) : "NULL");
		break;
	case DROID_ORDER_TYPE::BUILDMODULE:
		//build a module onto the structure
		if (!isConstructionDroid(psDroid) || psOrder->index < nextModuleToBuild((Structure *)psOrder->psObj, -1))
		{
			break;
		}
		psDroid->order = DroidOrder(DROID_ORDER_TYPE::BUILD, getModuleStat((Structure *)psOrder->psObj), psOrder->psObj->position.xy(), 0);
		ASSERT_OR_RETURN(, psDroid->order.psStats != nullptr, "should have found a module stats");
		ASSERT_OR_RETURN(, !psDroid->order.psStats || psDroid->order.psStats->type != REF_DEMOLISH, "Cannot build demolition");
		actionDroid(psDroid, DROID_ACTION::BUILD, psOrder->psObj->position.x, psOrder->psObj->position.y);
		objTrace(psDroid->id, "Starting new upgrade of %s", psOrder->psStats ? getStatsName(psOrder->psStats) : "NULL");
		break;
	case DROID_ORDER_TYPE::HELPBUILD:
		// help to build a structure that is starting to be built
		ASSERT_OR_RETURN(, isConstructionDroid(psDroid), "Not a constructor droid");
		ASSERT_OR_RETURN(, psOrder->psObj != nullptr, "Help to build a NULL pointer?");
		if (psDroid->action == DROID_ACTION::BUILD && psOrder->psObj == psDroid->psActionTarget[0]
			// skip DROID_ORDER_TYPE::LINEBUILD -> we still want to drop pending structure blueprints
			// this isn't a perfect solution, because ordering a LINEBUILD with negative energy, and then clicking
			// on first structure being built, will remove it, as we change order from DORDR_LINEBUILD to DROID_ORDER_TYPE::BUILD
			&& (psDroid->order.type != DROID_ORDER_TYPE::LINEBUILD))
		{
			// we are already building it, nothing to do
			objTrace(psDroid->id, "Ignoring DROID_ORDER_TYPE::HELPBUILD because already buildig object %i", psOrder->psObj->id);
			break;
		}
		psDroid->order = *psOrder;
		psDroid->order.pos = psOrder->psObj->position.xy();
		psDroid->order.psStats = ((Structure *)psOrder->psObj)->stats;
		ASSERT_OR_RETURN(,!psDroid->order.psStats || psDroid->order.psStats->type != REF_DEMOLISH, "Cannot build demolition");
		actionDroid(psDroid, DROID_ACTION::BUILD, psDroid->order.pos.x, psDroid->order.pos.y);
		objTrace(psDroid->id, "Helping construction of %s", psOrder->psStats ? getStatsName(psDroid->order.psStats) : "NULL");
		break;
	case DROID_ORDER_TYPE::DEMOLISH:
		if (!(psDroid->droidType == DROID_CONSTRUCT || psDroid->droidType == DROID_CYBORG_CONSTRUCT))
		{
			break;
		}
		psDroid->order = *psOrder;
		psDroid->order.pos = psOrder->psObj->position.xy();
		actionDroid(psDroid, DROID_ACTION::DEMOLISH, psOrder->psObj);
		break;
	case DROID_ORDER_TYPE::REPAIR:
		if (!(psDroid->droidType == DROID_CONSTRUCT || psDroid->droidType == DROID_CYBORG_CONSTRUCT))
		{
			break;
		}
		psDroid->order = *psOrder;
		psDroid->order.pos = psOrder->psObj->position.xy();
		actionDroid(psDroid, DROID_ACTION::REPAIR, psOrder->psObj);
		break;
	case DROID_ORDER_TYPE::DROIDREPAIR:
		if (!(psDroid->droidType == DROID_REPAIR || psDroid->droidType == DROID_CYBORG_REPAIR))
		{
			break;
		}
		psDroid->order = *psOrder;
		actionDroid(psDroid, DROID_ACTION::DROIDREPAIR, psOrder->psObj);
		break;
	case DROID_ORDER_TYPE::OBSERVE:
		// keep an object within sensor view
		psDroid->order = *psOrder;
		actionDroid(psDroid, DROID_ACTION::OBSERVE, psOrder->psObj);
		break;
	case DROID_ORDER_TYPE::FIRESUPPORT:
		if (isTransporter(psDroid))
		{
			debug(LOG_ERROR, "Sorry, transports cannot be assigned to commanders.");
			psDroid->order = DroidOrder(DROID_ORDER_TYPE::NONE);
			break;
		}
		if (psDroid->m_weaponList[0].nStat == 0)
		{
			break;
		}
		psDroid->order = *psOrder;
		// let the order update deal with vtol droids
		if (!isVtolDroid(psDroid))
		{
			actionDroid(psDroid, DROID_ACTION::FIRESUPPORT, psOrder->psObj);
		}

		if (psDroid->owningPlayer == selectedPlayer)
		{
			orderPlayFireSupportAudio(psOrder->psObj);
		}
		break;
	case DROID_ORDER_TYPE::COMMANDERSUPPORT:
		if (isTransporter(psDroid))
		{
			debug(LOG_ERROR, "Sorry, transports cannot be assigned to commanders.");
			psDroid->order = DroidOrder(DROID_ORDER_TYPE::NONE);
			break;
		}
		ASSERT_OR_RETURN(, psOrder->psObj != nullptr, "Can't command a NULL");
		if (cmdDroidAddDroid((Droid *)psOrder->psObj, psDroid) && psDroid->owningPlayer == selectedPlayer)
		{
			orderPlayFireSupportAudio(psOrder->psObj);
		}
		else if (psDroid->owningPlayer == selectedPlayer)
		{
			audio_PlayBuildFailedOnce();
		}
		break;
	case DROID_ORDER_TYPE::RTB:
		for (psStruct = apsStructLists[psDroid->owningPlayer]; psStruct; psStruct = psStruct->psNext)
		{
			if (psStruct->stats->type == REF_HQ)
			{
				Vector2i pos = psStruct->position.xy();

				psDroid->order = *psOrder;
				// Find a place to land for vtols. And Transporters in a multiPlay game.
				if (isVtolDroid(psDroid) || (game.type == LEVEL_TYPE::SKIRMISH && isTransporter(psDroid)))
				{
					actionVTOLLandingPos(psDroid, &pos);
				}
				actionDroid(psDroid, DROID_ACTION::MOVE, pos.x, pos.y);
				break;
			}
		}
		// no HQ go to the landing zone
		if (psDroid->order.type != DROID_ORDER_TYPE::RTB)
		{
			// see if the LZ has been set up
			int iDX = getLandingX(psDroid->owningPlayer);
			int iDY = getLandingY(psDroid->owningPlayer);

			if (iDX && iDY)
			{
				psDroid->order = *psOrder;
				actionDroid(psDroid, DROID_ACTION::MOVE, iDX, iDY);
			}
			else
			{
				// haven't got an LZ set up so don't do anything
				actionDroid(psDroid, DROID_ACTION::NONE);
				psDroid->order = DroidOrder(DROID_ORDER_TYPE::NONE);
			}
		}
		break;
	case DROID_ORDER_TYPE::RTR:
	case DROID_ORDER_TYPE::RTR_SPECIFIED:
		{
			if (isVtolDroid(psDroid))
			{
				moveToRearm(psDroid);
				break;
			}
			// if already has a target repair, don't override it: it might be different
			// and we don't want come back and forth between 2 repair points
			if (psDroid->order.type == DROID_ORDER_TYPE::RTR && psOrder->psObj != nullptr && !psOrder->psObj->deathTime)
			{
				objTrace(psDroid->id, "DONE FOR NOW");
				break;
			}
			RtrBestResult rtrData;
			if (psOrder->rtrType == RTR_TYPE_NO_RESULT || psOrder->psObj == nullptr)
			{
				rtrData = decideWhereToRepairAndBalance(psDroid);
			}
			else
			{
				rtrData = RtrBestResult(psOrder);
			}

			/* give repair order if repair facility found */
			if (rtrData.type == RTR_TYPE_REPAIR_FACILITY)
			{
				/* move to front of structure */
				psDroid->order = DroidOrder(psOrder->type, rtrData.psObj, RTR_TYPE_REPAIR_FACILITY);
				psDroid->order.pos = rtrData.psObj->position.xy();
				/* If in multiPlayer, and the Transporter has been sent to be
					* repaired, need to find a suitable location to drop down. */
				if (game.type == LEVEL_TYPE::SKIRMISH && isTransporter(psDroid))
				{
					Vector2i pos = psDroid->order.pos;

					objTrace(psDroid->id, "Repair transport");
					actionVTOLLandingPos(psDroid, &pos);
					actionDroid(psDroid, DROID_ACTION::MOVE, pos.x, pos.y);
				}
				else
				{
					objTrace(psDroid->id, "Go to repair facility at (%d, %d) using (%d, %d)!", rtrData.psObj->position.x, rtrData.psObj->position.y, psDroid->order.pos.x, psDroid->order.pos.y);
					actionDroid(psDroid, DROID_ACTION::MOVE, rtrData.psObj, psDroid->order.pos.x, psDroid->order.pos.y);
				}
			}
			/* give repair order if repair droid found */
			else if (rtrData.type == RTR_TYPE_DROID && !isTransporter(psDroid))
			{
				psDroid->order = DroidOrder(psOrder->type, Vector2i(rtrData.psObj->position.x, rtrData.psObj->position.y), RTR_TYPE_DROID);
				psDroid->order.pos = rtrData.psObj->position.xy();
				psDroid->order.psObj = rtrData.psObj;
				objTrace(psDroid->id, "Go to repair at (%d, %d) using (%d, %d), time %i!", rtrData.psObj->position.x, rtrData.psObj->position.y, psDroid->order.pos.x, psDroid->order.pos.y, gameTime);
				actionDroid(psDroid, DROID_ACTION::MOVE, psDroid->order.pos.x, psDroid->order.pos.y);
			}
			else
			{
				// no repair facility or HQ go to the landing zone
				if (!bMultiPlayer && selectedPlayer == 0)
				{
					objTrace(psDroid->id, "could not RTR, doing RTL instead");
					orderDroid(psDroid, DROID_ORDER_TYPE::RTB, ModeImmediate);
				}
			}
		}
		break;
	case DROID_ORDER_TYPE::EMBARK:
		{
          Droid *embarkee = castDroid(psOrder->psObj);
			if (isTransporter(psDroid)  // require a transporter for embarking.
			    || embarkee == nullptr || !isTransporter(embarkee))  // nor can a transporter load another transporter
			{
				debug(LOG_ERROR, "Sorry, can only load things that aren't transporters into things that are.");
				psDroid->order = DroidOrder(DROID_ORDER_TYPE::NONE);
				break;
			}
			// move the droid to the transporter location
			psDroid->order = *psOrder;
			psDroid->order.pos = psOrder->psObj->position.xy();
			actionDroid(psDroid, DROID_ACTION::MOVE, psOrder->psObj->position.x, psOrder->psObj->position.y);
			break;
		}
	case DROID_ORDER_TYPE::DISEMBARK:
		//only valid in multiPlayer mode
		if (bMultiPlayer)
		{
			//this order can only be given to Transporter droids
			if (isTransporter(psDroid))
			{
				psDroid->order = *psOrder;
				//move the Transporter to the requested location
				actionDroid(psDroid, DROID_ACTION::MOVE, psOrder->pos.x, psOrder->pos.y);
				//close the Transporter interface - if up
				if (widgGetFromID(psWScreen, IDTRANS_FORM) != nullptr)
				{
					intRemoveTrans();
				}
			}
		}
		break;
	case DROID_ORDER_TYPE::RECYCLE:
		psFactory = nullptr;
		iFactoryDistSq = 0;
		for (psStruct = apsStructLists[psDroid->owningPlayer]; psStruct; psStruct = psStruct->psNext)
		{
			// Look for nearest factory or repair facility
			if (psStruct->stats->type == REF_FACTORY || psStruct->stats->type == REF_CYBORG_FACTORY
			    || psStruct->stats->type == REF_VTOL_FACTORY || psStruct->stats->type == REF_REPAIR_FACILITY)
			{
				/* get droid->facility distance squared */
				int iStructDistSq = droidSqDist(psDroid, psStruct);

				/* Choose current structure if first facility found or nearer than previously chosen facility */
				if (psStruct->status == SS_BUILT && iStructDistSq > 0 && (psFactory == nullptr || iFactoryDistSq > iStructDistSq))
				{
					psFactory = psStruct;
					iFactoryDistSq = iStructDistSq;
				}
			}
		}

		/* give recycle order if facility found */
		if (psFactory != nullptr)
		{
			/* move to front of structure */
			psDroid->order = DroidOrder(psOrder->type, psFactory);
			psDroid->order.pos = psFactory->position.xy();
			setDroidTarget(psDroid,  psFactory);
			actionDroid(psDroid, DROID_ACTION::MOVE, psFactory, psDroid->order.pos.x, psDroid->order.pos.y);
		}
		break;
	case DROID_ORDER_TYPE::GUARD:
		psDroid->order = *psOrder;
		if (psOrder->psObj != nullptr)
		{
			psDroid->order.pos = psOrder->psObj->position.xy();
		}
		actionDroid(psDroid, DROID_ACTION::NONE);
		break;
	case DROID_ORDER_TYPE::RESTORE:
		if (!electronicDroid(psDroid))
		{
			break;
		}
		if (psOrder->psObj->type != OBJ_STRUCTURE)
		{
			ASSERT(false, "orderDroidBase: invalid object type for Restore order");
			break;
		}
		psDroid->order = *psOrder;
		psDroid->order.pos = psOrder->psObj->position.xy();
		actionDroid(psDroid, DROID_ACTION::RESTORE, psOrder->psObj);
		break;
	case DROID_ORDER_TYPE::REARM:
		// didn't get executed before
		if (!vtolRearming(psDroid))
		{
			psDroid->order = *psOrder;
			actionDroid(psDroid, DROID_ACTION::MOVETOREARM, psOrder->psObj);
			assignVTOLPad(psDroid, (Structure *)psOrder->psObj);
		}
		break;
	case DROID_ORDER_TYPE::CIRCLE:
		if (!isVtolDroid(psDroid))
		{
			break;
		}
		psDroid->order = *psOrder;
		actionDroid(psDroid, DROID_ACTION::MOVE, psOrder->pos.x, psOrder->pos.y);
		break;
	default:
		ASSERT(false, "orderUnitBase: unknown order");
		break;
	}

	syncDebugDroid(psDroid, '+');
}


/** This function sends the droid an order. It uses sendDroidInfo() if mode == ModeQueue and orderDroidBase() if not. */
void orderDroid(Droid *psDroid, DROID_ORDER order, QUEUE_MODE mode)
{
	ASSERT(psDroid != nullptr,
	       "orderUnit: Invalid unit pointer");
	ASSERT(order == DROID_ORDER_TYPE::NONE ||
	       order == DROID_ORDER_TYPE::RTR ||
	       order == DROID_ORDER_TYPE::RTB ||
	       order == DROID_ORDER_TYPE::RECYCLE ||
	       order == DROID_ORDER_TYPE::TRANSPORTIN ||
	       order == DROID_ORDER_TYPE::STOP ||		// Added this PD.
	       order == DROID_ORDER_TYPE::HOLD,
	       "orderUnit: Invalid order");

	DROID_ORDER_DATA sOrder(order);
	if (mode == ModeQueue && bMultiPlayer)
	{
		sendDroidInfo(psDroid, sOrder, false);
	}
	else
	{
		orderClearDroidList(psDroid);
		orderDroidBase(psDroid, &sOrder);
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


/** This function returns the order's target if it has one, and NULL if the order is not a target order.
 * @todo the first switch can be removed and substituted by orderState() function.
 * @todo the use of this function is somewhat superfluous on some cases. Investigate.
 */
GameObject *orderStateObj(Droid *psDroid, DROID_ORDER order)
{
	bool	match = false;

	switch (order)
	{
	case DROID_ORDER_TYPE::BUILD:
	case DROID_ORDER_TYPE::LINEBUILD:
	case DROID_ORDER_TYPE::HELPBUILD:
		if (psDroid->order.type == DROID_ORDER_TYPE::BUILD ||
		    psDroid->order.type == DROID_ORDER_TYPE::HELPBUILD ||
		    psDroid->order.type == DROID_ORDER_TYPE::LINEBUILD)
		{
			match = true;
		}
		break;
	case DROID_ORDER_TYPE::ATTACK:
	case DROID_ORDER_TYPE::FIRESUPPORT:
	case DROID_ORDER_TYPE::OBSERVE:
	case DROID_ORDER_TYPE::DEMOLISH:
	case DROID_ORDER_TYPE::DROIDREPAIR:
	case DROID_ORDER_TYPE::REARM:
	case DROID_ORDER_TYPE::GUARD:
		if (psDroid->order.type == order)
		{
			match = true;
		}
		break;
	case DROID_ORDER_TYPE::RTR:
		if (psDroid->order.type == DROID_ORDER_TYPE::RTR ||
		    psDroid->order.type == DROID_ORDER_TYPE::RTR_SPECIFIED)
		{
			match = true;
		}
	default:
		break;
	}

	if (!match)
	{
		return nullptr;
	}

	// check the order is one with an object
	switch (psDroid->order.type)
	{
	default:
		// not an object order - return false
		return nullptr;
		break;
	case DROID_ORDER_TYPE::BUILD:
	case DROID_ORDER_TYPE::LINEBUILD:
		if (psDroid->action == DROID_ACTION::BUILD ||
		    psDroid->action == DROID_ACTION::BUILDWANDER)
		{
			return psDroid->order.psObj;
		}
		break;
	case DROID_ORDER_TYPE::HELPBUILD:
		if (psDroid->action == DROID_ACTION::BUILD ||
		    psDroid->action == DROID_ACTION::BUILDWANDER ||
		    psDroid->action == DROID_ACTION::MOVETOBUILD)
		{
			return psDroid->order.psObj;
		}
		break;
	//case DROID_ORDER_TYPE::HELPBUILD:
	case DROID_ORDER_TYPE::ATTACK:
	case DROID_ORDER_TYPE::FIRESUPPORT:
	case DROID_ORDER_TYPE::OBSERVE:
	case DROID_ORDER_TYPE::DEMOLISH:
	case DROID_ORDER_TYPE::RTR:
	case DROID_ORDER_TYPE::RTR_SPECIFIED:
	case DROID_ORDER_TYPE::DROIDREPAIR:
	case DROID_ORDER_TYPE::REARM:
	case DROID_ORDER_TYPE::GUARD:
		return psDroid->order.psObj;
		break;
	}

	return nullptr;
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
			position = psOrder->psObj->position.xzy();
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
			    psObj->type == OBJ_STRUCTURE)
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

	if (altOrder && (psObj->type == OBJ_DROID || psObj->type == OBJ_STRUCTURE) && psDroid->owningPlayer == psObj->owningPlayer)
	{
		if (psDroid->droidType == DROID_SENSOR)
		{
			return DroidOrder(DROID_ORDER_TYPE::OBSERVE, psObj);
		}
		else if ((psDroid->droidType == DROID_REPAIR ||
		          psDroid->droidType == DROID_CYBORG_REPAIR) && psObj->type == OBJ_DROID)
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
	if (psObj->type == OBJ_DROID && isTransporter((Droid *)psObj) && psObj->owningPlayer == psDroid->owningPlayer)
	{
		order = DroidOrder(DROID_ORDER_TYPE::EMBARK, psObj);
	}
	// go to recover an artifact/oil drum - don't allow VTOL's to get this order
	else if (psObj->type == OBJ_FEATURE &&
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
	         psObj->type == OBJ_DROID &&
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
	         psObj->type == OBJ_DROID &&
	         (psDroid->droidType == DROID_REPAIR ||
	          psDroid->droidType == DROID_CYBORG_REPAIR) &&
	         droidIsDamaged((Droid *)psObj))
	{
		order = DroidOrder(DROID_ORDER_TYPE::DROIDREPAIR, psObj);
	}
	// guarding constructor droids
	else if (aiCheckAlliances(psObj->owningPlayer, psDroid->owningPlayer) &&
	         psObj->type == OBJ_DROID &&
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
	         psObj->type == OBJ_STRUCTURE)
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
					orderDroidStatsLocDirAdd(psCurr, DROID_ORDER_TYPE::BUILD, castStructure(psObj)->stats, psObj->position.x, psObj->position.y, castStructure(psObj)->rotation.direction, add);
#if defined(WZ_CC_GNU) && !defined(WZ_CC_INTEL) && !defined(WZ_CC_CLANG) && (7 <= __GNUC__)
# pragma GCC diagnostic pop
#endif
				}
				else
				{
					// Help watch the structure being built.
					orderDroidLocAdd(psCurr, DROID_ORDER_TYPE::MOVE, psObj->position.x, psObj->position.y, add);
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
			unsigned dist = iHypot((psDroid->position - embarkee->position).xy());
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


/** Given a factory type, this function runs though all player's structures to check if any is of factory type. Returns the structure if any was found, and NULL else.*/
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

// balance the load at random
// always prefer faster repairs
static inline RtrBestResult decideWhereToRepairAndBalance(Droid *psDroid)
{
	int bestDistToRepairFac = INT32_MAX, bestDistToRepairDroid = INT32_MAX;
	int thisDistToRepair = 0;
        Structure *psHq = nullptr;
	Position bestDroidPos, bestFacPos;
	// static to save allocations
	static std::vector<Position> vFacilityPos;
	static std::vector<Structure *> vFacility;
	static std::vector<int> vFacilityCloseEnough;
	static std::vector<Position> vDroidPos;
	static std::vector<Droid *> vDroid;
	static std::vector<int> vDroidCloseEnough;
	// clear vectors from previous invocations
	vFacilityPos.clear();
	vFacility.clear();
	vFacilityCloseEnough.clear();
	vDroidCloseEnough.clear();
	vDroidPos.clear();
	vDroid.clear();

	for (Structure *psStruct = apsStructLists[psDroid->owningPlayer]; psStruct; psStruct = psStruct->psNext)
	{
		if (psStruct->stats->type == REF_HQ)
		{
			psHq = psStruct;
			continue;
		}
		if (psStruct->stats->type == REF_REPAIR_FACILITY && psStruct->status == SS_BUILT)
		{
			thisDistToRepair = droidSqDist(psDroid, psStruct);
			if (thisDistToRepair <= 0)
			{
				continue;	// cannot reach position
			}
			vFacilityPos.push_back(psStruct->position);
			vFacility.push_back(psStruct);
			if (bestDistToRepairFac > thisDistToRepair)
			{
				bestDistToRepairFac = thisDistToRepair;
				bestFacPos = psStruct->position;
			}
		}
	}
	// if we are repair droid ourselves, don't consider other repairs droids
	// because that causes havoc on front line: RT repairing themselves,
	// blocking everyone else. And everyone else moving toward RT, also toward front line.s
	// Ideally, we should just avoid retreating toward "danger", but dangerMap is only for multiplayer
	if (psDroid->droidType != DROID_REPAIR && psDroid->droidType != DROID_CYBORG_REPAIR)
	{
		// one of these lists is empty when on mission
                Droid *psdroidList =
                    allDroidLists[psDroid->owningPlayer] != nullptr ? allDroidLists[psDroid->owningPlayer] : mission.apsDroidLists[psDroid->owningPlayer];
		for (Droid *psCurr = psdroidList; psCurr != nullptr; psCurr = psCurr->psNext)
		{
			if (psCurr->droidType == DROID_REPAIR || psCurr->droidType == DROID_CYBORG_REPAIR)
			{
				thisDistToRepair = droidSqDist(psDroid, psCurr);
				if (thisDistToRepair <= 0)
				{
					continue; // unreachable
				}
				vDroidPos.push_back(psCurr->position);
				vDroid.push_back(psCurr);
				if (bestDistToRepairDroid > thisDistToRepair)
				{
					bestDistToRepairDroid = thisDistToRepair;
					bestDroidPos = psCurr->position;
				}
			}
		}
	}

	ASSERT(bestDistToRepairFac > 0, "Bad distance to repair facility");
	ASSERT(bestDistToRepairDroid > 0, "Bad distance to repair droid");
	// debug(LOG_INFO, "found a total of %lu RT, and %lu RF", vDroid.size(), vFacility.size());

	// the center of this area starts at the closest repair droid/facility!
	#define MAGIC_SUITABLE_REPAIR_AREA ((REPAIR_RANGE*3) * (REPAIR_RANGE*3))
	Position bestRepairPoint = bestDistToRepairFac < bestDistToRepairDroid ? bestFacPos: bestDroidPos;
	// find all close enough repairing candidates
	for (int i=0; i < vFacilityPos.size(); i++)
	{
		Vector2i diff = (bestRepairPoint - vFacilityPos[i]).xy();
		if (dot(diff, diff) < MAGIC_SUITABLE_REPAIR_AREA)
		{
			vFacilityCloseEnough.push_back(i);
		}
	}
	for (int i=0; i < vDroidPos.size(); i++)
	{
		Vector2i diff = (bestRepairPoint - vDroidPos[i]).xy();
		if (dot(diff, diff) < MAGIC_SUITABLE_REPAIR_AREA)
		{
			vDroidCloseEnough.push_back(i);
		}
	}

	// debug(LOG_INFO, "found  %lu RT, and %lu RF in suitable area", vDroidCloseEnough.size(), vFacilityCloseEnough.size());
	// prefer facilities, they re much more efficient than droids
	if (vFacilityCloseEnough.size() == 1)
	{
		return RtrBestResult(RTR_TYPE_REPAIR_FACILITY, vFacility[vFacilityCloseEnough[0]]);

	} else if (vFacilityCloseEnough.size() > 1)
	{
		int32_t which = gameRand(vFacilityCloseEnough.size());
		return RtrBestResult(RTR_TYPE_REPAIR_FACILITY, vFacility[vFacilityCloseEnough[which]]);
	}

	// no facilities :( fallback on droids
	if (vDroidCloseEnough.size() == 1)
	{
		return RtrBestResult(RTR_TYPE_DROID, vDroid[vDroidCloseEnough[0]]);
	} else if (vDroidCloseEnough.size() > 1)
	{
		int32_t which = gameRand(vDroidCloseEnough.size());
		return RtrBestResult(RTR_TYPE_DROID, vDroid[vDroidCloseEnough[which]]);
	}

	// go to headquarters, if any
	if (psHq != nullptr)
	{
		return RtrBestResult(RTR_TYPE_HQ, psHq);
	}
	// screw it
	return RtrBestResult(RTR_TYPE_NO_RESULT, nullptr);
}

/** This function assigns a state to a droid. It returns true if it assigned and false if it failed to assign.*/
bool secondarySetState(Droid *psDroid, SECONDARY_ORDER sec, SECONDARY_STATE State, QUEUE_MODE mode)
{
	UDWORD		CurrState, factType, prodType;
        Structure *psStruct;
	SDWORD		factoryInc;
	bool		retVal;
        Droid *psTransport, *psCurr, *psNext;
	DROID_ORDER     order;

	CurrState = psDroid->secondaryOrder;
	if (bMultiMessages && mode == ModeQueue)
	{
		CurrState = psDroid->secondaryOrderPending;
	}

	// Figure out what the new secondary state will be (once the order is synchronised.
	// Why does this have to be so ridiculously complicated?
	uint32_t secondaryMask = 0;
	uint32_t secondarySet = 0;
	switch (sec)
	{
	case DSO_ATTACK_RANGE:
		secondaryMask = DSS_ARANGE_MASK;
		secondarySet = State;
		break;
	case DSO_REPAIR_LEVEL:
		secondaryMask = DSS_REPLEV_MASK;
		secondarySet = State;
		break;
	case DSO_ATTACK_LEVEL:
		secondaryMask = DSS_ALEV_MASK;
		secondarySet = State;
		break;
	case DSO_ASSIGN_PRODUCTION:
		if (psDroid->droidType == DROID_COMMAND)
		{
			secondaryMask = DSS_ASSPROD_FACT_MASK;
			secondarySet = State & DSS_ASSPROD_MASK;
		}
		break;
	case DSO_ASSIGN_CYBORG_PRODUCTION:
		if (psDroid->droidType == DROID_COMMAND)
		{
			secondaryMask = DSS_ASSPROD_CYB_MASK;
			secondarySet = State & DSS_ASSPROD_MASK;
		}
		break;
	case DSO_ASSIGN_VTOL_PRODUCTION:
		if (psDroid->droidType == DROID_COMMAND)
		{
			secondaryMask = DSS_ASSPROD_VTOL_MASK;
			secondarySet = State & DSS_ASSPROD_MASK;
		}
		break;
	case DSO_CLEAR_PRODUCTION:
		if (psDroid->droidType == DROID_COMMAND)
		{
			secondaryMask = State & DSS_ASSPROD_MASK;
		} break;
	case DSO_RECYCLE:
		if (State & DSS_RECYCLE_MASK)
		{
			secondaryMask = DSS_RTL_MASK | DSS_RECYCLE_MASK | DSS_HALT_MASK;
			secondarySet = DSS_RECYCLE_SET | DSS_HALT_GUARD;
		}
		else
		{
			secondaryMask = DSS_RECYCLE_MASK;
		}
		break;
	case DSO_CIRCLE:  // This doesn't even make any sense whatsoever as a secondary order...
		secondaryMask = DSS_CIRCLE_MASK;
		secondarySet = (State & DSS_CIRCLE_SET) ? DSS_CIRCLE_SET : 0;
		break;
	case DSO_PATROL:  // This doesn't even make any sense whatsoever as a secondary order...
		secondaryMask = DSS_PATROL_MASK;
		secondarySet = (State & DSS_PATROL_SET) ? DSS_PATROL_SET : 0;
		break;
	case DSO_HALTTYPE:
		switch (State & DSS_HALT_MASK)
		{
			case DSS_HALT_PURSUE:
			case DSS_HALT_GUARD:
			case DSS_HALT_HOLD:
				secondaryMask = DSS_HALT_MASK;
				secondarySet = State;
				break;
		}
		break;
	case DSO_RETURN_TO_LOC:
		secondaryMask = DSS_RTL_MASK;
		switch (State & DSS_RTL_MASK)
		{
		case DSS_RTL_REPAIR:
		case DSS_RTL_BASE:
			secondarySet = State;
			break;
		case DSS_RTL_TRANSPORT:
			psTransport = FindATransporter(psDroid);
			if (psTransport != nullptr)
			{
				secondarySet = State;
			}
			break;
		}
		if ((CurrState & DSS_HALT_MASK) == DSS_HALT_HOLD)
		{
			secondaryMask |= DSS_HALT_MASK;
			secondarySet |= DSS_HALT_GUARD;
		}
		break;
	case DSO_UNUSED:
	case DSO_FIRE_DESIGNATOR:
		// Do nothing.
		break;
	}
	uint32_t newSecondaryState = (CurrState & ~secondaryMask) | secondarySet;

	if (bMultiMessages && mode == ModeQueue)
	{
		if (sec == DSO_REPAIR_LEVEL)
		{
			secondaryCheckDamageLevelDeselect(psDroid, State);  // Deselect droid immediately, if applicable, so it isn't ordered around by mistake.
		}

		sendDroidSecondary(psDroid, sec, State);
		psDroid->secondaryOrderPending = newSecondaryState;
		++psDroid->secondaryOrderPendingCount;
		return true;  // Wait for our order before changing the droid.
	}


	// set the state for any droids in the command group
	if ((sec != DSO_RECYCLE) &&
	    psDroid->droidType == DROID_COMMAND &&
	    psDroid->psGroup != nullptr &&
	    psDroid->psGroup->type == GT_COMMAND)
	{
		psDroid->psGroup->setSecondary(sec, State);
	}

	retVal = true;
	switch (sec)
	{
	case DSO_ATTACK_RANGE:
		CurrState = (CurrState & ~DSS_ARANGE_MASK) | State;
		break;

	case DSO_REPAIR_LEVEL:
		CurrState = (CurrState & ~DSS_REPLEV_MASK) | State;
		psDroid->secondaryOrder = CurrState;
		secondaryCheckDamageLevel(psDroid);
		break;

	case DSO_ATTACK_LEVEL:
		CurrState = (CurrState & ~DSS_ALEV_MASK) | State;
		if (State == DSS_ALEV_NEVER)
		{
			if (orderState(psDroid, DROID_ORDER_TYPE::ATTACK))
			{
				// just kill these orders
				orderDroid(psDroid, DROID_ORDER_TYPE::STOP, ModeImmediate);
				if (isVtolDroid(psDroid))
				{
					moveToRearm(psDroid);
				}
			}
			else if (droidAttacking(psDroid))
			{
				// send the unit back to the guard position
				actionDroid(psDroid, DROID_ACTION::NONE);
			}
			else if (orderState(psDroid, DROID_ORDER_TYPE::PATROL))
			{
				// send the unit back to the patrol
				actionDroid(psDroid, DROID_ACTION::RETURNTOPOS, psDroid->actionPos.x, psDroid->actionPos.y);
			}
		}
		break;


	case DSO_ASSIGN_PRODUCTION:
	case DSO_ASSIGN_CYBORG_PRODUCTION:
	case DSO_ASSIGN_VTOL_PRODUCTION:
#ifdef DEBUG
		debug(LOG_NEVER, "order factories %s\n", secondaryPrintFactories(State));
#endif
		if (sec == DSO_ASSIGN_PRODUCTION)
		{
			prodType = REF_FACTORY;
		}
		else if (sec == DSO_ASSIGN_CYBORG_PRODUCTION)
		{
			prodType = REF_CYBORG_FACTORY;
		}
		else
		{
			prodType = REF_VTOL_FACTORY;
		}

		if (psDroid->droidType == DROID_COMMAND)
		{
			// look for the factories
			for (psStruct = apsStructLists[psDroid->owningPlayer]; psStruct;
			     psStruct = psStruct->psNext)
			{
				factType = psStruct->stats->type;
				if (factType == REF_FACTORY ||
				    factType == REF_VTOL_FACTORY ||
				    factType == REF_CYBORG_FACTORY)
				{
					factoryInc = ((Factory *)psStruct->pFunctionality)->psAssemblyPoint->factoryInc;
					if (factType == REF_FACTORY)
					{
						factoryInc += DSS_ASSPROD_SHIFT;
					}
					else if (factType == REF_CYBORG_FACTORY)
					{
						factoryInc += DSS_ASSPROD_CYBORG_SHIFT;
					}
					else
					{
						factoryInc += DSS_ASSPROD_VTOL_SHIFT;
					}
					if (!(CurrState & (1 << factoryInc)) &&
					    (State & (1 << factoryInc)))
					{
						assignFactoryCommandDroid(psStruct, psDroid);// assign this factory to the command droid
					}
					else if ((prodType == factType) &&
					         (CurrState & (1 << factoryInc)) &&
					         !(State & (1 << factoryInc)))
					{
						// remove this factory from the command droid
						assignFactoryCommandDroid(psStruct, nullptr);
					}
				}
			}
			if (prodType == REF_FACTORY)
			{
				CurrState &= ~DSS_ASSPROD_FACT_MASK;
			}
			else if (prodType == REF_CYBORG_FACTORY)
			{
				CurrState &= ~DSS_ASSPROD_CYB_MASK;
			}
			else
			{
				CurrState &= ~DSS_ASSPROD_VTOL_MASK;
			}
			CurrState |= (State & DSS_ASSPROD_MASK);
#ifdef DEBUG
			debug(LOG_NEVER, "final factories %s\n", secondaryPrintFactories(CurrState));
#endif
		}
		break;

	case DSO_CLEAR_PRODUCTION:
		if (psDroid->droidType == DROID_COMMAND)
		{
			// simply clear the flag - all the factory stuff is done in assignFactoryCommandDroid
			CurrState &= ~(State & DSS_ASSPROD_MASK);
		}
		break;


	case DSO_RECYCLE:
		if (State & DSS_RECYCLE_MASK)
		{
			if (!orderState(psDroid, DROID_ORDER_TYPE::RECYCLE))
			{
				orderDroid(psDroid, DROID_ORDER_TYPE::RECYCLE, ModeImmediate);
			}
			CurrState &= ~(DSS_RTL_MASK | DSS_RECYCLE_MASK | DSS_HALT_MASK);
			CurrState |= DSS_RECYCLE_SET | DSS_HALT_GUARD;
			psDroid->group = UBYTE_MAX;
			if (psDroid->psGroup != nullptr)
			{
				if (psDroid->droidType == DROID_COMMAND)
				{
					// remove all the units from the commanders group
					for (psCurr = psDroid->psGroup->psList; psCurr; psCurr = psNext)
					{
						psNext = psCurr->psGrpNext;
						psCurr->psGroup->remove(psCurr);
						orderDroid(psCurr, DROID_ORDER_TYPE::STOP, ModeImmediate);
					}
				}
				else if (psDroid->psGroup->type == GT_COMMAND)
				{
					psDroid->psGroup->remove(psDroid);
				}
			}
		}
		else
		{
			if (orderState(psDroid, DROID_ORDER_TYPE::RECYCLE))
			{
				orderDroid(psDroid, DROID_ORDER_TYPE::STOP, ModeImmediate);
			}
			CurrState &= ~DSS_RECYCLE_MASK;
		}
		break;
	case DSO_CIRCLE:
		if (State & DSS_CIRCLE_SET)
		{
			CurrState |= DSS_CIRCLE_SET;
		}
		else
		{
			CurrState &= ~DSS_CIRCLE_MASK;
		}
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
			if (orderState(psDroid, DROID_ORDER_TYPE::GUARD))
			{
				orderDroid(psDroid, DROID_ORDER_TYPE::STOP, ModeImmediate);
			}
			break;
		case DSS_HALT_GUARD:
			CurrState &= ~ DSS_HALT_MASK;
			CurrState |= DSS_HALT_GUARD;
			orderDroidLoc(psDroid, DROID_ORDER_TYPE::GUARD, psDroid->position.x, psDroid->position.y, ModeImmediate);
			break;
		case DSS_HALT_HOLD:
			CurrState &= ~ DSS_HALT_MASK;
			CurrState |= DSS_HALT_HOLD;
			if (!orderState(psDroid, DROID_ORDER_TYPE::FIRESUPPORT))
			{
				orderDroid(psDroid, DROID_ORDER_TYPE::STOP, ModeImmediate);
			}
			break;
		}
		break;
	case DSO_RETURN_TO_LOC:
		if ((State & DSS_RTL_MASK) == 0)
		{
			if (orderState(psDroid, DROID_ORDER_TYPE::RTR) ||
			    orderState(psDroid, DROID_ORDER_TYPE::RTB) ||
			    orderState(psDroid, DROID_ORDER_TYPE::EMBARK))
			{
				orderDroid(psDroid, DROID_ORDER_TYPE::STOP, ModeImmediate);
			}
			CurrState &= ~DSS_RTL_MASK;
		}
		else
		{
			order = DROID_ORDER_TYPE::NONE;
			CurrState &= ~DSS_RTL_MASK;
			if ((CurrState & DSS_HALT_MASK) == DSS_HALT_HOLD)
			{
				CurrState &= ~DSS_HALT_MASK;
				CurrState |= DSS_HALT_GUARD;
			}
			switch (State & DSS_RTL_MASK)
			{
			case DSS_RTL_REPAIR:
				order = DROID_ORDER_TYPE::RTR;
				CurrState |= DSS_RTL_REPAIR;
				// can't clear the selection here cos it breaks the secondary order screen
				break;
			case DSS_RTL_BASE:
				order = DROID_ORDER_TYPE::RTB;
				CurrState |= DSS_RTL_BASE;
				break;
			case DSS_RTL_TRANSPORT:
				psTransport = FindATransporter(psDroid);
				if (psTransport != nullptr)
				{
					order = DROID_ORDER_TYPE::EMBARK;
					CurrState |= DSS_RTL_TRANSPORT;
					if (!orderState(psDroid, DROID_ORDER_TYPE::EMBARK))
					{
						orderDroidObj(psDroid, DROID_ORDER_TYPE::EMBARK, psTransport, ModeImmediate);
					}
				}
				else
				{
					retVal = false;
				}
				break;
			default:
				order = DROID_ORDER_TYPE::NONE;
				break;
			}
			if (!orderState(psDroid, order))
			{
				orderDroid(psDroid, order, ModeImmediate);
			}
		}
		break;

	case DSO_FIRE_DESIGNATOR:
		// don't actually set any secondary flags - the cmdDroid array is
		// always used to determine which commander is the designator
		if (State & DSS_FIREDES_SET)
		{
			cmdDroidSetDesignator(psDroid);
		}
		else if (cmdDroidGetDesignator(psDroid->owningPlayer) == psDroid)
		{
			cmdDroidClearDesignator(psDroid->owningPlayer);
		}
		break;

	default:
		break;
	}

	if (CurrState != newSecondaryState)
	{
		debug(LOG_WARNING, "Guessed the new secondary state incorrectly, expected 0x%08X, got 0x%08X, was 0x%08X, sec = %d, state = 0x%08X.", newSecondaryState, CurrState, psDroid->secondaryOrder, sec, State);
	}
	psDroid->secondaryOrder = CurrState;
	psDroid->secondaryOrderPendingCount = std::max(psDroid->secondaryOrderPendingCount - 1, 0);
	if (psDroid->secondaryOrderPendingCount == 0)
	{
		psDroid->secondaryOrderPending = psDroid->secondaryOrder;  // If no orders are pending, make sure UI uses the actual state.
	}

	return retVal;
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
	case DROID_ORDER_TYPE::UNUSED_4:                 return "DROID_ORDER_TYPE::UNUSED_4";
	case DROID_ORDER_TYPE::UNUSED_2:                 return "DROID_ORDER_TYPE::UNUSED_2";
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