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
 * @file selection.cpp
 * Attempt to rationalise the unit selection procedure and
 * also necessary as we now need to return the number of
 * units selected according to specified criteria.
 *
 * Alex McLean, Pumpkin studios, EIDOS
*/

#include <algorithm>
#include <functional>
#include <vector>

#include "lib/framework/frame.h"
#include "lib/framework/math_ext.h"
#include "lib/framework/strres.h"

#include "objects.h"
#include "basedef.h"
#include "geometry.h"
#include "console.h"
#include "selection.h"
#include "hci.h"
#include "map.h"
#include "selection.h"
#include "display3d.h"
#include "warcam.h"
#include "display.h"
#include "qtscript.h"
#include "objmem.h"

// stores combinations of unit components
static std::vector<std::vector<unsigned>> combinations;

template <typename T>
static unsigned selSelectUnitsIf(unsigned player, T condition, bool onlyOnScreen)
{
	if (player >= MAX_PLAYERS) { return 0; }

	unsigned count = 0;

	selDroidDeselect(player);

	// Go through all.
	for (auto& psDroid : playerList[player].droids)
	{
		bool shouldSelect = (!onlyOnScreen || objectOnScreen(&psDroid, 0)) &&
			condition(&psDroid);
		count += shouldSelect;
		if (shouldSelect && !psDroid.damageManager->isSelected() &&
        !psDroid.testFlag(static_cast<size_t>(OBJECT_FLAG::UNSELECTABLE))) {
			SelectDroid(&psDroid);
		}
		else if (!shouldSelect && psDroid.damageManager->isSelected()) {
			DeSelectDroid(&psDroid);
		}
	}

	return count;
}

template <typename T, typename U>
static unsigned selSelectUnitsIf(unsigned player, T condition, U value, bool onlyOnScreen)
{
	return selSelectUnitsIf(player, [condition, value](Droid* psDroid) { return condition(psDroid, value); },
	                        onlyOnScreen);
}

static bool selTransporter(Droid const* droid)
{
	return isTransporter(*droid);
}

static bool selTrue(Droid* droid)
{
	return !selTransporter(droid);
}

static bool selProp(Droid* droid, PROPULSION_TYPE prop)
{
  auto propulsion = dynamic_cast<PropulsionStats const*>(droid->getComponent(COMPONENT_TYPE::PROPULSION));
	return propulsion->propulsionType == prop && !selTransporter(droid);
}

static bool selPropArmed(Droid* droid, PROPULSION_TYPE prop)
{
  auto propulsion = dynamic_cast<PropulsionStats const*>(droid->getComponent(COMPONENT_TYPE::PROPULSION));
	return propulsion->propulsionType == prop &&
         vtolFull(*droid) && !selTransporter(droid);
}

static bool selType(Droid const* droid, DROID_TYPE type)
{
	return droid->getType() == type;
}

static bool selCombat(Droid const* droid)
{
	return droid->asWeaps[0].nStat > 0 && !selTransporter(droid);
}

static bool selCombatLand(Droid* droid)
{
  auto propulsion = dynamic_cast<PropulsionStats const*>(droid->getComponent(COMPONENT_TYPE::PROPULSION));
  auto type = propulsion->propulsionType;
	return droid->asWeaps[0].nStat > 0 && (type == PROPULSION_TYPE::WHEELED ||
		type == PROPULSION_TYPE::HALF_TRACKED ||
		type == PROPULSION_TYPE::TRACKED ||
		type == PROPULSION_TYPE::HOVER ||
		type == PROPULSION_TYPE::LEGGED);
}

static bool selCombatCyborg(Droid* droid)
{
  auto propulsion = dynamic_cast<PropulsionStats const*>(droid->getComponent(COMPONENT_TYPE::PROPULSION));
	return droid->asWeaps[0].nStat > 0 && propulsion->propulsionType == PROPULSION_TYPE::LEGGED;
}

static bool selDamaged(Droid const* droid)
{
	return PERCENT(droid->damageManager->getHp(), 
                 droid->damageManager->getOriginalHp()) < REPAIRLEV_LOW && 
         !selTransporter(droid);
}

static bool selNoGroup(Droid* psDroid)
{
	return psDroid->group != UBYTE_MAX;
}

static bool selCombatLandMildlyOrNotDamaged(Droid* psDroid)
{
	return PERCENT(psDroid->damageManager->getHp(), psDroid->damageManager->getOriginalHp())
         > REPAIRLEV_LOW && selCombatLand(psDroid) && !selNoGroup(psDroid);
}

// Deselects all units for the player
unsigned selDroidDeselect(unsigned player)
{
	unsigned count = 0;
	if (player >= MAX_PLAYERS) return 0; 

	for (auto& psDroid : playerList[player].droids)
	{
		if (psDroid.damageManager->isSelected()) {
			count++;
			DeSelectDroid(&psDroid);
		}
	}
	return count;
}

// ---------------------------------------------------------------------
// Lets you know how many are selected for a given player
unsigned selNumSelected(unsigned player)
{
	unsigned count = 0;
	if (player >= MAX_PLAYERS) { return 0; }

	for (auto& psDroid : playerList[player].droids)
	{
		if (psDroid.damageManager->isSelected()) {
			count++;
		}
	}

	return count;
}

// Helper function to check whether the component stats of a unit can be found
// in the combinations vector and, optionally, to add them to it if not
static bool componentsInCombinations(Droid* psDroid, bool add)
{
	std::vector<uint32_t> components;
	uint32_t stat = 0;

	// stats are sorted by their estimated usefulness to distinguish units:
	// * the first weapon turret is the most common difference between them
	//   followed by propulsions and bodies
	// * the second weapon turret is least important because Hydras are rare
	for (int c = 0; c < DROID_MAXCOMP + 2; c++)
	{
		switch (c)
		{
		case 0: stat = psDroid->asWeaps[1].nStat;
			break;
		case 1: stat = psDroid->asBits[COMP_ECM];
			break;
		case 2: stat = psDroid->asBits[COMP_BRAIN];
			break;
		case 3: stat = psDroid->asBits[COMP_SENSOR];
			break;
		case 4: stat = psDroid->asBits[COMP_REPAIRUNIT];
			break;
		case 5: stat = psDroid->asBits[COMP_CONSTRUCT];
			break;
		case 6: stat = psDroid->asBits[COMP_BODY];
			break;
		case 7: stat = psDroid->asBits[COMP_PROPULSION];
			break;
		case 8: stat = psDroid->asWeaps[0].nStat;
			break;
		}

		// keep the list of components short by not adding stats with
		// the value 0 to its end, since they are redundant
		if (!(stat == 0 && components.empty()))
		{
			components.push_back(stat);
		}
	}
	auto it = std::find(combinations.begin(), combinations.end(), components);
	if (it != combinations.end())
	{
		return true;
	}
	else
	{
		// add the list of components to the list of combinations unless
		// this would result in a duplicate entry
		if (add)
		{
			combinations.push_back(components);
		}
		return false;
	}
}

// Selects all units with the same propulsion, body and turret(s) as the one(s) selected
static unsigned int selSelectAllSame(unsigned unsigned player, bool bOnScreen)
{
	unsigned int i = 0, selected = 0;
	std::vector<unsigned int> excluded;

	combinations.clear();

	if (player >= MAX_PLAYERS) { return 0; }

	// find out which units will need to be compared to which component combinations
	for (auto& psDroid : playerList[player].droids)
	{
		if (bOnScreen && !objectOnScreen(&psDroid, 0)) {
			excluded.push_back(i);
		}
		else if (psDroid.damageManager->isSelected()) {
			excluded.push_back(i);
			selected++;
			componentsInCombinations(&psDroid, true);
		}
		i++;
	}

	// if all or no units are selected, no more units can be chosen
	if (!excluded.empty() && i != selected)
	{
		// reset unit counter
		i = 0;
		for (auto& psDroid : playerList[player].droids)
		{
			if (excluded.empty() || *excluded.begin() != i) {
				if (componentsInCombinations(&psDroid, false)) {
					SelectDroid(&psDroid);
					selected++;
				}
			}
			else {
				excluded.erase(excluded.begin());
			}
			i++;
		}
	}
	return selected;
}

// ffs am
// ---------------------------------------------------------------------
void selNextSpecifiedUnit(DROID_TYPE unitType)
{
	static Droid* psOldRD = nullptr; // pointer to last selected repair unit
	Droid *psResult = nullptr, *psFirst = nullptr;
	bool bLaterInList = false;

	ASSERT_OR_RETURN(, selectedPlayer < MAX_PLAYERS, "invalid selectedPlayer: %" PRIu32 "", selectedPlayer);

	for (auto& psCurr : playerList[selectedPlayer].droids)
	{
    using enum DROID_TYPE;
		//exceptions - as always...
		bool bMatch = false;
		if (unitType == CONSTRUCT)
		{
			if (psCurr.getType() == CONSTRUCT ||
					psCurr.getType() == CYBORG_CONSTRUCT)
			{
				bMatch = true;
			}
		}
		else if (unitType == REPAIRER)
		{
			if (psCurr.getType() == REPAIRER ||
					psCurr.getType() == CYBORG_REPAIR)
			{
				bMatch = true;
			}
		}
		else if (psCurr.getType() == unitType)
		{
			bMatch = true;
		}
		if (bMatch)
		{
			/* Always store away the first one we find */
			if (!psFirst)
			{
				psFirst = &psCurr;
			}

			if (&psCurr == psOldRD)
			{
				bLaterInList = true;
			}

			/* Nothing previously found... */
			if (!psOldRD)
			{
				psResult = &psCurr;
			}

			/* Only select is this isn't the old one and it's further on in list */
			else if (&psCurr != psOldRD && bLaterInList)
			{
				psResult = &psCurr;
			}
		}
	}

	/* Did we get one? */
	if (!psResult && psFirst)
	{
		psResult = psFirst;
	}

	if (psResult && !psResult->damageManager->isDead()) {
		selDroidDeselect(selectedPlayer);
		SelectDroid(psResult);
		if (getWarCamStatus())
		{
			camToggleStatus(); // messy - fix this
			// setViewPos(map_coord(psCentreDroid->pos.x), map_coord(psCentreDroid->pos.y));
			processWarCam(); //odd, but necessary
			camToggleStatus(); // messy - FIXME
		}
		else
		{
			// camToggleStatus();
			/* Centre display on him if warcam isn't active */
			setViewPos(map_coord(psResult->getPosition().x), map_coord(psResult->getPosition().y), true);
		}
		psOldRD = psResult;
	}
	else
	{
		switch (unitType) {
      using enum DROID_TYPE;
		case REPAIRER:
			addConsoleMessage(_("Unable to locate any repair units!"), CONSOLE_TEXT_JUSTIFICATION::LEFT, SYSTEM_MESSAGE);
			break;
		case CONSTRUCT:
			addConsoleMessage(_("Unable to locate any Trucks!"), CONSOLE_TEXT_JUSTIFICATION::LEFT, SYSTEM_MESSAGE);
			break;
		case SENSOR:
			addConsoleMessage(_("Unable to locate any Sensor Units!"), CONSOLE_TEXT_JUSTIFICATION::LEFT, SYSTEM_MESSAGE);
			break;
		case COMMAND:
			addConsoleMessage(_("Unable to locate any Commanders!"), CONSOLE_TEXT_JUSTIFICATION::LEFT, SYSTEM_MESSAGE);
		default:
			break;
		}
	}
}

// ---------------------------------------------------------------------
void selNextUnassignedUnit()
{
	static Droid* psOldNS = nullptr;
	Droid *psResult = nullptr, *psFirst = nullptr;
	bool bLaterInList = false;

	ASSERT_OR_RETURN(, selectedPlayer < MAX_PLAYERS, "invalid selectedPlayer: %" PRIu32 "", selectedPlayer);

	for (auto& psCurr : apsDroidLists[selectedPlayer])
	{
		/* Only look at unselected ones */
		if (psCurr.group == UBYTE_MAX)
		{
			/* Keep a record of first one */
			if (!psFirst)
			{
				psFirst = &psCurr;
			}

			if (&psCurr == psOldNS)
			{
				bLaterInList = true;
			}

			/* First one...? */
			if (!psOldNS)
			{
				psResult = &psCurr;
			}

			/* Dont choose same one again */
			else if (&psCurr != psOldNS && bLaterInList)
			{
				psResult = &psCurr;
			}
		}
	}

	/* If we didn't get one - then select first one */
	if (!psResult && psFirst)
	{
		psResult = psFirst;
	}

	if (psResult && !psResult->damageManager->isDead())
	{
		selDroidDeselect(selectedPlayer);
		SelectDroid(psResult);
		if (getWarCamStatus())
		{
			camToggleStatus(); // messy - fix this
			// setViewPos(map_coord(psCentreDroid->pos.x), map_coord(psCentreDroid->pos.y));
			processWarCam(); //odd, but necessary
			camToggleStatus(); // messy - FIXME
		}
		else
		{
			// camToggleStatus();
			/* Centre display on him if warcam isn't active */
			setViewPos(map_coord(psResult->getPosition().x), map_coord(psResult->getPosition().y), true);
		}
		psOldNS = psResult;
	}
	else
	{
		addConsoleMessage(_("Unable to locate any repair units!"),
                      CONSOLE_TEXT_JUSTIFICATION::LEFT, SYSTEM_MESSAGE);
	}
}

// ---------------------------------------------------------------------
void selNextSpecifiedBuilding(STRUCTURE_TYPE structType, bool jump)
{
	Structure *psResult = nullptr, *psOldStruct = nullptr, *psFirst = nullptr;
	bool bLaterInList = false;

	ASSERT_OR_RETURN(, selectedPlayer < MAX_PLAYERS, "invalid selectedPlayer: %" PRIu32 "", selectedPlayer);

	/* Firstly, start coughing if the type is invalid */
	ASSERT(structType <= STRUCTURE_TYPE::COUNT, "Invalid structure type %u", structType);

	for (auto& psCurr : apsStructLists[selectedPlayer])
	{
		if (psCurr->getStats()->type == structType &&
        psCurr->getState() == STRUCTURE_STATE::BUILT) {
			if (!psFirst) {
				psFirst = psCurr.get();
			}
			if (psCurr->damageManager->isSelected()) {
				bLaterInList = true;
				psOldStruct = psCurr.get();
			}
			else if (bLaterInList)
			{
				psResult = psCurr.get();
			}
		}
	}

	if (!psResult && psFirst)
	{
		psResult = psFirst;
	}

	if (psResult && !psResult->damageManager->isDead())
	{
		if (getWarCamStatus())
		{
			camToggleStatus();
		}
		if (jump)
		{
			setViewPos(map_coord(psResult->getPosition().x),
                 map_coord(psResult->getPosition().y), false);
		}
		if (psOldStruct)
		{
			psOldStruct->damageManager->setSelected(false);
		}
		psResult->damageManager->setSelected(true);
		triggerEventSelected();
		jsDebugSelected(psResult);
	}
	else
	{
		// Can't find required building
		addConsoleMessage(_("Cannot find required building!"), CONSOLE_TEXT_JUSTIFICATION::LEFT, SYSTEM_MESSAGE);
	}
}

// ---------------------------------------------------------------------
// see if a commander is the n'th command droid
static bool droidIsCommanderNum(Droid* psDroid, SDWORD n)
{
	if (psDroid->getType() != DROID_TYPE::COMMAND) {
		return false;
	}

	int numLess = 0;
	for (const auto& psCurr : apsDroidLists[psDroid->playerManager->getPlayer()])
	{
		if ((psCurr.getType() == DROID_TYPE::COMMAND) && (psCurr.getId() < psDroid->getId())) {
			numLess++;
		}
	}

	return (numLess == (n - 1));
}

// select the n'th command droid
void selCommander(int n)
{
	ASSERT_OR_RETURN(, selectedPlayer < MAX_PLAYERS, "invalid selectedPlayer: %" PRIu32 "", selectedPlayer);

	for (auto& psCurr : apsDroidLists[selectedPlayer])
	{
		if (droidIsCommanderNum(&psCurr, n)) {
			if (!psCurr.damageManager->isSelected() && !psCurr.testFlag(static_cast<size_t>(OBJECT_FLAG::UNSELECTABLE))) {
				clearSelection();
				psCurr.damageManager->setSelected(true);
			}
			else if (!psCurr.testFlag(static_cast<size_t>(OBJECT_FLAG::UNSELECTABLE)))
			{
				clearSelection();
				psCurr.damageManager->setSelected(true);

				// this horrible bit of code is taken from activateGroupAndMove
				// and sets the camera position to that of the commander

				if (getWarCamStatus())
				{
					camToggleStatus(); // messy - fix this
					processWarCam(); // odd, but necessary
					camToggleStatus(); // messy - FIXME
				}
				else {
					/* Centre display on him if warcam isn't active */
					setViewPos(map_coord(psCurr.getPosition().x), map_coord(psCurr.getPosition().y), true);
				}
			}
			return;
		}
	}
}

/*
   Selects the units of a given player according to given criteria.
   It is also possible to request whether the units be onscreen or not.
   */
unsigned selDroidSelection(unsigned player, SELECTION_CLASS droidClass, SELECTIONTYPE droidType, bool bOnScreen)
{
	if (player >= MAX_PLAYERS) { return 0; }

	/* So far, we haven't selected any */
	unsigned int retVal = 0;

	/* Establish the class of selection */
	switch (droidClass) {
    using enum DROID_TYPE;
	case SELECTION_CLASS::DS_ALL_UNITS:
		retVal = selSelectUnitsIf(player, selTrue, bOnScreen);
		break;
  case SELECTION_CLASS::DS_BY_TYPE:
		switch (droidType)
		{
		case SELECTIONTYPE::DST_VTOL:
			retVal = selSelectUnitsIf(player, selProp, PROPULSION_TYPE::LIFT, bOnScreen);
			break;
		case SELECTIONTYPE::DST_VTOL_ARMED:
			retVal = selSelectUnitsIf(player, selPropArmed, PROPULSION_TYPE::LIFT, bOnScreen);
			break;
		case SELECTIONTYPE::DST_HOVER:
			retVal = selSelectUnitsIf(player, selProp, PROPULSION_TYPE::HOVER, bOnScreen);
			break;
		case SELECTIONTYPE::DST_WHEELED:
			retVal = selSelectUnitsIf(player, selProp, PROPULSION_TYPE::WHEELED, bOnScreen);
			break;
		case SELECTIONTYPE::DST_TRACKED:
			retVal = selSelectUnitsIf(player, selProp, PROPULSION_TYPE::TRACKED, bOnScreen);
			break;
		case SELECTIONTYPE::DST_HALF_TRACKED:
			retVal = selSelectUnitsIf(player, selProp, PROPULSION_TYPE::HALF_TRACKED, bOnScreen);
			break;
		case SELECTIONTYPE::DST_CYBORG:
			retVal = selSelectUnitsIf(player, selProp, PROPULSION_TYPE::LEGGED, bOnScreen);
			break;
		case SELECTIONTYPE::DST_ENGINEER:
			retVal = selSelectUnitsIf(player, selType, CYBORG_CONSTRUCT, bOnScreen);
			break;
		case SELECTIONTYPE::DST_MECHANIC:
			retVal = selSelectUnitsIf(player, selType, CYBORG_REPAIR, bOnScreen);
			break;
		case SELECTIONTYPE::DST_TRANSPORTER:
			retVal = selSelectUnitsIf(player, selTransporter, bOnScreen);
			break;
		case SELECTIONTYPE::DST_REPAIR_TANK:
			retVal = selSelectUnitsIf(player, selType, REPAIRER, bOnScreen);
			break;
		case SELECTIONTYPE::DST_SENSOR:
			retVal = selSelectUnitsIf(player, selType, SENSOR, bOnScreen);
			break;
		case SELECTIONTYPE::DST_TRUCK:
			retVal = selSelectUnitsIf(player, selType, CONSTRUCT, bOnScreen);
			break;
		case SELECTIONTYPE::DST_ALL_COMBAT:
			retVal = selSelectUnitsIf(player, selCombat, bOnScreen);
			break;
		case SELECTIONTYPE::DST_ALL_COMBAT_LAND:
			retVal = selSelectUnitsIf(player, selCombatLand, bOnScreen);
			break;
		case SELECTIONTYPE::DST_ALL_COMBAT_CYBORG:
			retVal = selSelectUnitsIf(player, selCombatCyborg, bOnScreen);
			break;
		case SELECTIONTYPE::DST_ALL_DAMAGED:
			retVal = selSelectUnitsIf(player, selDamaged, bOnScreen);
			break;
		case SELECTIONTYPE::DST_ALL_SAME:
			retVal = selSelectAllSame(player, bOnScreen);
			break;
		case SELECTIONTYPE::DST_ALL_LAND_MILDLY_OR_NOT_DAMAGED:
			retVal = selSelectUnitsIf(player, selCombatLandMildlyOrNotDamaged, bOnScreen);
			break;
		default:
			ASSERT(false, "Invalid selection type");
		}
		break;
	default:
		ASSERT(false, "Invalid selection attempt");
		break;
	}

	CONPRINTF(ngettext("%u unit selected", "%u units selected", retVal), retVal);

	return retVal;
}
