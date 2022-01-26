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
 * @file objmem.h
 * Routines for managing object's memory
 */

#ifndef __INCLUDED_SRC_OBJMEM_H__
#define __INCLUDED_SRC_OBJMEM_H__

#include "lib/framework/frame.h"

#include "droid.h"
#include "feature.h"
#include "structure.h"


/* The lists of objects allocated */
extern std::array<std::vector<Droid>,MAX_PLAYERS> apsDroidLists;

extern std::array<std::vector<
        std::unique_ptr<Structure> >, MAX_PLAYERS> apsStructLists;

extern std::vector<FlagPosition*> apsFlagPosLists;
extern std::array<std::vector<ResourceExtractor*>, MAX_PLAYERS> apsExtractorLists;
extern std::vector<BaseObject*> apsSensorList;
extern std::vector<Feature*> apsOilList;

/* The list of destroyed objects */
extern std::vector<BaseObject*> psDestroyedObj;

/* Initialise the object heaps */
bool objmemInitialise();

/* Release the object heaps */
void objmemShutdown();

/* General housekeeping for the object system */
void objmemUpdate();

/// Generates a new, (hopefully) unique object id.
unsigned generateNewObjectId();

/// Generates a new, (hopefully) unique object id, which all clients agree on.
unsigned generateSynchronisedObjectId();

/* add the droid to the Droid Lists */
void addDroid(Droid* psDroidToAdd, Droid* pList[MAX_PLAYERS]);

/*destroy a droid */
void killDroid(Droid* psDel);

/* Remove all droids */
void freeAllDroids();

/*Remove a single Droid from its list*/
void removeDroid(Droid* psDroidToRemove, Droid* pList[MAX_PLAYERS]);

/*Removes all droids that may be stored in the mission lists*/
void freeAllMissionDroids();

/*Removes all droids that may be stored in the limbo lists*/
void freeAllLimboDroids();

/* add the structure to the Structure Lists */
void addStructure(Structure* psStructToAdd);

/* Destroy a structure */
void killStruct(Structure* psDel);

/* Remove all structures */
void freeAllStructs();

/*Remove a single Structure from a list*/
void removeStructureFromList(Structure* psStructToRemove, Structure* pList[MAX_PLAYERS]);

/* add the feature to the Feature Lists */
void addFeature(Feature* psFeatureToAdd);

/* Destroy a feature */
void killFeature(Feature* psDel);

/* Remove all features */
void freeAllFeatures();

/* Create a new Flag Position */
bool createFlagPosition(FlagPosition** ppsNew, unsigned player);
/* add the Flag Position to the Flag Position Lists */
void addFlagPosition(FlagPosition* psFlagPosToAdd);
/* Remove a Flag Position from the Lists */
void removeFlagPosition(FlagPosition* psDel);
// free all flag positions
void freeAllFlagPositions();

// Find a base object from it's id
PlayerOwnedObject * getBaseObjFromData(unsigned id, unsigned player);
PlayerOwnedObject * getBaseObjFromId(unsigned id);

unsigned getRepairIdFromFlag(FlagPosition* psFlag);

void objCount(int* droids, int* structures, int* features);

#ifdef DEBUG
void checkFactoryFlags();
#endif

#endif // __INCLUDED_SRC_OBJMEM_H__
