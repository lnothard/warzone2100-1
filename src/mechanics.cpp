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
 * @file Mechanics.cpp
 * Game world mechanics.
 */

#include "lib/framework/frame.h"

#include "basedef.h"
#include "droid.h"
#include "feature.h"
#include "mechanics.h"
#include "objmem.h"
#include "research.h"
#include "structure.h"

/* Shutdown the mechanics system */
bool mechanicsShutdown()
{
	for (auto psObj : psDestroyedObj)
	{
		delete psObj;
	}
	psDestroyedObj.clear();
	return true;
}


// Allocate the list for a component
bool allocComponentList(COMPONENT_TYPE type, int number)
{
	//allocate the space for the Players' component lists
	for (auto inc = 0; inc < MAX_PLAYERS; inc++)
	{
		if (apCompLists[inc][type])
		{
			free(apCompLists[inc][type]);
		}

		apCompLists[inc][type] = (UBYTE*)malloc(sizeof(UBYTE) * number);

		//initialise the players' lists
		for (auto comp = 0; comp < number; comp++)
		{
			apCompLists[inc][type][comp] = UNAVAILABLE;
		}
	}

	return true;
}

// release all the component lists
void freeComponentLists()
{
	UDWORD inc;

	for (inc = 0; inc < MAX_PLAYERS; inc++)
	{
		//free the component lists
		if (apCompLists[inc][COMPONENT_TYPE::BODY])
		{
			free(apCompLists[inc][COMPONENT_TYPE::BODY]);
			apCompLists[inc][COMPONENT_TYPE::BODY] = nullptr;
		}
		if (apCompLists[inc][COMPONENT_TYPE::BRAIN])
		{
			free(apCompLists[inc][COMPONENT_TYPE::BRAIN]);
			apCompLists[inc][COMPONENT_TYPE::BRAIN] = nullptr;
		}
		if (apCompLists[inc][COMPONENT_TYPE::PROPULSION])
		{
			free(apCompLists[inc][COMPONENT_TYPE::PROPULSION]);
			apCompLists[inc][COMPONENT_TYPE::PROPULSION] = nullptr;
		}
		if (apCompLists[inc][COMPONENT_TYPE::SENSOR])
		{
			free(apCompLists[inc][COMPONENT_TYPE::SENSOR]);
			apCompLists[inc][COMPONENT_TYPE::SENSOR] = nullptr;
		}
		if (apCompLists[inc][COMPONENT_TYPE::ECM])
		{
			free(apCompLists[inc][COMPONENT_TYPE::ECM]);
			apCompLists[inc][COMPONENT_TYPE::ECM] = nullptr;
		}
		if (apCompLists[inc][COMPONENT_TYPE::REPAIR_UNIT])
		{
			free(apCompLists[inc][COMPONENT_TYPE::REPAIRUNIT]);
			apCompLists[inc][COMPONENT_TYPE::REPAIRUNIT] = nullptr;
		}
		if (apCompLists[inc][COMPONENT_TYPE::CONSTRUCT])
		{
			free(apCompLists[inc][COMPONENT_TYPE::CONSTRUCT]);
			apCompLists[inc][COMPONENT_TYPE::CONSTRUCT] = nullptr;
		}
		if (apCompLists[inc][COMPONENT_TYPE::WEAPON])
		{
			free(apCompLists[inc][COMPONENT_TYPE::WEAPON]);
			apCompLists[inc][COMPONENT_TYPE::WEAPON] = nullptr;
		}
	}
}

//allocate the space for the Players' structure lists
bool allocStructLists()
{
	SDWORD inc, stat;

	for (inc = 0; inc < MAX_PLAYERS; inc++)
	{
		if (numStructureStats)
		{
			apStructTypeLists[inc] = (UBYTE*)malloc(sizeof(UBYTE) * numStructureStats);
			for (stat = 0; stat < (SDWORD)numStructureStats; stat++)
			{
				apStructTypeLists[inc][stat] = UNAVAILABLE;
			}
		}
		else
		{
			apStructTypeLists[inc] = nullptr;
		}
	}

	return true;
}


// release the structure lists
void freeStructureLists()
{
	UDWORD inc;

	for (inc = 0; inc < MAX_PLAYERS; inc++)
	{
		//free the structure lists
		if (apStructTypeLists[inc])
		{
			free(apStructTypeLists[inc]);
			apStructTypeLists[inc] = nullptr;
		}
	}
}


//TEST FUNCTION - MAKE EVERYTHING AVAILABLE
void makeAllAvailable()
{
	UDWORD comp, i;

	for (i = 0; i < MAX_PLAYERS; i++)
	{
		for (comp = 0; comp < numWeaponStats; comp++)
		{
			apCompLists[i][COMPONENT_TYPE::WEAPON][comp] = AVAILABLE;
		}
		for (comp = 0; comp < numBodyStats; comp++)
		{
			apCompLists[i][COMPONENT_TYPE::BODY][comp] = AVAILABLE;
		}
		for (comp = 0; comp < numPropulsionStats; comp++)
		{
			apCompLists[i][COMPONENT_TYPE::PROPULSION][comp] = AVAILABLE;
		}
		for (comp = 0; comp < numSensorStats; comp++)
		{
			apCompLists[i][COMPONENT_TYPE::SENSOR][comp] = AVAILABLE;
		}
		for (comp = 0; comp < numECMStats; comp++)
		{
			apCompLists[i][COMPONENT_TYPE::ECM][comp] = AVAILABLE;
		}
		for (comp = 0; comp < numConstructStats; comp++)
		{
			apCompLists[i][COMPONENT_TYPE::CONSTRUCT][comp] = AVAILABLE;
		}
		for (comp = 0; comp < numBrainStats; comp++)
		{
			apCompLists[i][COMPONENT_TYPE::BRAIN][comp] = AVAILABLE;
		}
		for (comp = 0; comp < numRepairStats; comp++)
		{
			apCompLists[i][COMPONENT_TYPE::REPAIR_UNIT][comp] = AVAILABLE;
		}

		//make all the structures available
		for (comp = 0; comp < numStructureStats; comp++)
		{
			apStructTypeLists[i][comp] = AVAILABLE;
		}
		//make all research availble to be performed
		for (comp = 0; comp < asResearch.size(); comp++)
		{
			enableResearch(&asResearch[comp], i);
		}
	}
}
