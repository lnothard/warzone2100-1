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
/** @file
 *  Routines for managing object's memory
 */

#ifndef __INCLUDED_SRC_OBJMEM_H__
#define __INCLUDED_SRC_OBJMEM_H__

#include "objectdef.h"

/* The lists of objects allocated */
extern std::array<std::vector<DROID>, MAX_PLAYERS> apsDroidLists;
extern std::array<std::vector<STRUCTURE>, MAX_PLAYERS> apsStructLists;
extern std::array<std::vector<FLAG_POSITION>, MAX_PLAYERS> apsFlagPosLists;
extern std::vector<FEATURE> apsFeatureLists;

extern std::array<std::vector<STRUCTURE*>, MAX_PLAYERS> apsExtractorLists;
extern std::vector<BASE_OBJECT*> apsSensorList;
extern std::vector<FEATURE*> apsOilList;


/* The list of destroyed objects */
extern std::vector<BASE_OBJECT> psDestroyedObj;

/* Initialise the object heaps */
bool objmemInitialise();

void objmemShutdown();

/* General housekeeping for the object system */
void objmemUpdate();

/// Generates a new, (hopefully) unique object id.
uint32_t generateNewObjectId();
/// Generates a new, (hopefully) unique object id, which all clients agree on.
uint32_t generateSynchronisedObjectId();

/* add the droid to the Droid Lists */
void addDroid(DROID *psDroidToAdd);

/*destroy a droid */
void killDroid(DROID *psDel);

/* Remove all droids */
void freeAllDroids();

/*Remove a single Droid from its list*/
void removeDroid(DROID *psDroidToRemove);

/*Removes all droids that may be stored in the limbo lists*/
void freeAllLimboDroids();

/* add the structure to the Structure Lists */
void addStructure(STRUCTURE *psStructToAdd);

/* Destroy a structure */
void killStruct(STRUCTURE *psDel);

/* Remove all structures */
void freeAllStructs();

/*Remove a single Structure from a list*/
void removeStructureFromList(STRUCTURE *psStructToRemove);

/* add the feature to the Feature Lists */
void addFeature(FEATURE *psFeatureToAdd);

/* Destroy a feature */
void killFeature(FEATURE *psDel);

/* Remove all features */
void freeAllFeatures();

/* Create a new Flag Position */
bool createFlagPosition(FLAG_POSITION **ppsNew, UDWORD player);
/* add the Flag Position to the Flag Position Lists */
void addFlagPosition(FLAG_POSITION *psFlagPosToAdd);
/* Remove a Flag Position from the Lists */
void removeFlagPosition(FLAG_POSITION *psDel);
// free all flag positions
void freeAllFlagPositions();

// Find a base object from it's id
BASE_OBJECT *getBaseObjFromData(unsigned id, unsigned player, OBJECT_TYPE type);
BASE_OBJECT *getBaseObjFromId(UDWORD id, OBJECT_TYPE type);

UDWORD getRepairIdFromFlag(FLAG_POSITION *psFlag);
void objCount(int *droids, int *structures, int *features);

template <typename Func, typename... Ts>
void forEachList(Func f, Ts&&... n)
{
  ([&](auto&& list) {
      for (auto& el : list)
      {
        f(&el);
      }
  } (std::forward<Ts>(n)), ...);
}

template <typename Func, typename... Args, typename... Ts>
void forEachList(Func f, std::tuple<Args...> t, Ts&&... n)
{
  ([&](auto&& list) {
    for (auto& el : list)
    {
      auto args = std::tuple_cat(std::make_tuple(&el), t);
      std::apply(f, args);
    }
  } (std::forward<Ts>(n)), ...);
}

#ifdef DEBUG
void checkFactoryFlags();
#endif

#endif // __INCLUDED_SRC_OBJMEM_H__
