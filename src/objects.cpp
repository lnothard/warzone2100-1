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
 * @file objects.cpp
 * The object system
 */

#include "lib/framework/frame.h"

#include "droid.h"
#include "feature.h"
#include "objects.h"
#include "structure.h"

class Droid;
bool objmemInitialise();
void objmemShutdown();

/* Initialise the object system */
bool objInitialise()
{
	if (!objmemInitialise())
	{
		return false;
	}

	return true;
}


/* Shutdown the object system */
bool objShutdown()
{
	objmemShutdown();

	return true;
}


/*goes thru' the list passed in reversing the order so the first entry becomes
the last and the last entry becomes the first!*/
void reverseObjectList(PlayerOwnedObject ** ppsList)
{
  PlayerOwnedObject * psPrev = nullptr;
  PlayerOwnedObject * psCurrent = *ppsList;

	while (psCurrent != nullptr)
	{
    PlayerOwnedObject * psNext = psCurrent->psNext;
		psCurrent->psNext = psPrev;
		psPrev = psCurrent;
		psCurrent = psNext;
	}
	//set the list passed in to point to the new top
	*ppsList = psPrev;
}

const char* objInfo(const PlayerOwnedObject * psObj)
{
	static char info[PATH_MAX];

	if (!psObj)
	{
		return "null";
	}

	switch (psObj->type)
	{
	case OBJ_DROID:
		{
			const Droid* psDroid = (const Droid*)psObj;
			return droidGetName(psDroid);
		}
	case OBJ_STRUCTURE:
		{
			const Structure* psStruct = (const Structure*)psObj;
			sstrcpy(info, getStatsName(psStruct->pStructureType));
			break;
		}
	case OBJ_FEATURE:
		{
			const Feature* psFeat = (const Feature*)psObj;
			sstrcpy(info, getStatsName(psFeat->psStats));
			break;
		}
	case OBJ_PROJECTILE:
		sstrcpy(info, "Projectile"); // TODO
		break;
	default:
		sstrcpy(info, "Unknown object type");
		break;
	}
	return info;
}
