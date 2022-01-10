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
 * @file cmddroid.cpp
 * Function definitions for command droids
 */

#include <cstring>

#include "lib/framework/frame.h"
#include "lib/sound/audio.h"
#include "lib/sound/audio_id.h"

#include "cmddroid.h"
#include "console.h"
#include "droid.h"
#include "group.h"
#include "objects.h"
#include "objmem.h"
#include "order.h"
#include "lib/gamelib/gtime.h"

/// This global instance is responsible for dealing with each
/// player's target designator
std::array<Droid*, MAX_PLAYERS> apsCmdDesignator;

// Last time the max commander limit message was displayed
static unsigned lastMaxCmdLimitMsgTime = 0;

static constexpr auto MAX_COMMAND_LIMIT_MESSAGE_PAUSE = 10000;

/**
 * This function adds the droid to the command group commanded by
 * psCommander. It creates a group if it doesn't exist. If the
 * group is not full, it adds the droid to it and sets all the
 * droid's states and orders to the group's.
 */
bool cmdDroidAddDroid(Droid* psCommander, Droid* psDroid)
{
	std::unique_ptr<Group> psGroup;
	bool addedToGroup = false;

	ASSERT_OR_RETURN(false, psCommander != nullptr, "psCommander is null?");
	ASSERT_OR_RETURN(false, psDroid != nullptr, "psDroid is null?");

	if (psCommander->group == nullptr)
	{
		psGroup = grpCreate();
		psGroup->add(psCommander);
		psDroid->group = UBYTE_MAX;
	}

	if (psCommander->group->getNumMembers() < cmdDroidMaxGroup(psCommander))
	{
		addedToGroup = true;

		psCommander->group->add(psDroid);
		psDroid->group = UBYTE_MAX;

		// set the secondary states for the unit
		// dont reset DSO_ATTACK_RANGE, because there is no way to modify it under commander
		secondarySetState(psDroid, DSO_REPAIR_LEVEL, (SECONDARY_STATE)(psCommander->secondary_order & DSS_REPLEV_MASK),
		                  ModeImmediate);
		secondarySetState(psDroid, DSO_ATTACK_LEVEL, (SECONDARY_STATE)(psCommander->secondary_order & DSS_ALEV_MASK),
		                  ModeImmediate);
		secondarySetState(psDroid, DSO_HALTTYPE, (SECONDARY_STATE)(psCommander->secondary_order & DSS_HALT_MASK),
		                  ModeImmediate);

		orderDroidObj(psDroid, DORDER_GUARD, (SimpleObject*)psCommander, ModeImmediate);
	}
	else if (psCommander->getPlayer() == selectedPlayer)
	{
		//Do not potentially spam the console with this message
		if (lastMaxCmdLimitMsgTime + MAX_COMMAND_LIMIT_MESSAGE_PAUSE < gameTime)
		{
			addConsoleMessage(
				_("Commander needs a higher level to command more units"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
			lastMaxCmdLimitMsgTime = gameTime;
		}
	}

	return addedToGroup;
}

Droid* cmdDroidGetDesignator(unsigned player)
{
	return apsCmdDesignator[player];
}

void cmdDroidSetDesignator(Droid* psDroid)
{
	ASSERT_OR_RETURN(, psDroid != nullptr, "Invalid droid!");
	if (psDroid->getType() != DROID_TYPE::COMMAND) {
		return;
	}

	apsCmdDesignator[psDroid->getPlayer()] = psDroid;
}

void cmdDroidClearDesignator(unsigned player)
{
	apsCmdDesignator[player] = nullptr;
}

long get_commander_index(const Droid& commander)
{
  assert(commander.getType() == DROID_TYPE::COMMAND);

  const auto& droids = apsDroidLists[commander.getPlayer()];
  return std::find_if(droids.begin(), droids.end(),
                      [&commander](const auto& droid)
  {
      return droid.getType() == DROID_TYPE::COMMAND &&
             &droid == &commander;
  }) - droids.begin();
}
///** This function returns the index of the command droid.
// * It does this by searching throughout all the player's droids.
// * @todo try to find something more efficient, has this function is of O(TotalNumberOfDroidsOfPlayer).
// */
//SDWORD cmdDroidGetIndex(DROID *psCommander)
//{
//	SDWORD	index = 1;
//	DROID	*psCurr;
//
//	if (psCommander->droidType != DROID_COMMAND)
//	{
//		return 0;
//	}
//
//	for (psCurr = apsDroidLists[psCommander->player]; psCurr; psCurr = psCurr->psNext)
//	{
//		if (psCurr->droidType == DROID_COMMAND &&
//		    psCurr->id < psCommander->id)
//		{
//			index += 1;
//		}
//	}
//
//	return index;
//}

///** This function returns the maximum group size of the command droid.*/
//unsigned int cmdDroidMaxGroup(const DROID *psCommander)
//{
//	const BRAIN_STATS *psStats = getBrainStats(psCommander);
//	return getDroidLevel(psCommander) * psStats->upgrade[psCommander->player].maxDroidsMult + psStats->upgrade[psCommander->player].maxDroids;
//}

///** This function adds experience to the command droid of the psShooter's command group.*/
//void cmdDroidUpdateExperience(DROID *psShooter, uint32_t experienceInc)
//{
//	ASSERT_OR_RETURN(, psShooter != nullptr, "invalid Unit pointer");
//
//	if (hasCommander(psShooter))
//	{
//		DROID *psCommander = psShooter->psGroup->psCommander;
//		psCommander->experience += MIN(experienceInc, UINT32_MAX - psCommander->experience);
//	}
//}

///** This function returns true if the droid is assigned to a commander group and it is not the commander.*/
//bool hasCommander(const DROID *psDroid)
//{
//	ASSERT_OR_RETURN(false, psDroid != nullptr, "invalid droid pointer");
//
//	if (psDroid->droidType != DROID_COMMAND &&
//	    psDroid->psGroup != nullptr &&
//	    psDroid->psGroup->type == GT_COMMAND)
//	{
//		return true;
//	}
//
//	return false;
//}

///** This function returns the level of a droids commander. If the droid doesn't have commander, it returns 0.*/
//unsigned int cmdGetCommanderLevel(const DROID *psDroid)
//{
//	const DROID *psCommander;
//
//	ASSERT(psDroid != nullptr, "invalid droid pointer");
//
//	// If this droid is not the member of a Commander's group
//	// Return an experience level of 0
//	if (!hasCommander(psDroid))
//	{
//		return 0;
//	}
//
//	// Retrieve this group's commander
//	psCommander = psDroid->psGroup->psCommander;
//
//	// Return the experience level of this commander
//	return getDroidLevel(psCommander);
//}
