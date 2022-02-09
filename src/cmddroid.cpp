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

#include "cmddroid.h"
#include "console.h"
#include "droid.h"
#include "group.h"
#include "objmem.h"
#include "order.h"

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
	bool addedToGroup = false;

	ASSERT_OR_RETURN(false, psCommander != nullptr, "psCommander is null?");
	ASSERT_OR_RETURN(false, psDroid != nullptr, "psDroid is null?");

	if (psCommander->getGroup() == nullptr) {
		auto psGroup = addGroup(-1);
		psGroup->addDroid(psCommander);
		psDroid->setSelectionGroup(UBYTE_MAX);
	}

	if (psCommander->getGroup()->getMembers()->size() < cmdDroidMaxGroup(psCommander)) {
		addedToGroup = true;

		psCommander->addDroidToGroup(psDroid);
		psDroid->setSelectionGroup(UBYTE_MAX);

		// set the secondary states for the unit don't reset DSO_ATTACK_RANGE,
    // because there is no way to modify it under commander
		secondarySetState(psDroid, SECONDARY_ORDER::REPAIR_LEVEL,
                      (SECONDARY_STATE)(psCommander->getSecondaryOrder() & DSS_REPLEV_MASK),
		                  ModeImmediate);
		secondarySetState(psDroid, SECONDARY_ORDER::ATTACK_LEVEL,
                      (SECONDARY_STATE)(psCommander->getSecondaryOrder() & DSS_ALEV_MASK),
		                  ModeImmediate);
		secondarySetState(psDroid, SECONDARY_ORDER::HALT_TYPE,
                      (SECONDARY_STATE)(psCommander->getSecondaryOrder() & DSS_HALT_MASK),
		                  ModeImmediate);

		orderDroidObj(psDroid, ORDER_TYPE::GUARD, psCommander, ModeImmediate);
	}
	else if (psCommander->playerManager->getPlayer() == selectedPlayer) {
		//Do not potentially spam the console with this message
		if (lastMaxCmdLimitMsgTime + MAX_COMMAND_LIMIT_MESSAGE_PAUSE < gameTime) {
			addConsoleMessage(
				_("Commander needs a higher level to command more units"),
        CONSOLE_TEXT_JUSTIFICATION::DEFAULT, SYSTEM_MESSAGE);
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

	apsCmdDesignator[psDroid->playerManager->getPlayer()] = psDroid;
}

void cmdDroidClearDesignator(unsigned player)
{
	apsCmdDesignator[player] = nullptr;
}

long get_commander_index(Droid const& commander)
{
  assert(commander.getType() == DROID_TYPE::COMMAND);

  auto const& droids = playerList[commander.playerManager->getPlayer()].droids;
  return std::find_if(droids.begin(), droids.end(),
                      [&commander](auto const& droid) {
      return droid.getType() == DROID_TYPE::COMMAND &&
             &droid == &commander;
  }) - droids.begin();
}

/** This function returns the maximum group size of the command droid.*/
unsigned cmdDroidMaxGroup(Droid const* psCommander)
{
	auto psStats = dynamic_cast<CommanderStats const*>(psCommander->getComponent(COMPONENT_TYPE::BRAIN));
	return getDroidLevel(psCommander) * psStats->upgraded[psCommander->playerManager->getPlayer()].maxDroidsMult
         + psStats->upgraded[psCommander->playerManager->getPlayer()].maxDroids;
}
