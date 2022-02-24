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
/*
 * ObjMem.c
 *
 * Object memory management functions.
 *
 */
#include <cstring>

#include "lib/framework/frame.h"
#include "objects.h"
#include "lib/gamelib/gtime.h"
#include "lib/netplay/netplay.h"
#include "hci.h"
#include "map.h"
#include "power.h"
#include "objects.h"
#include "structuredef.h"
#include "structure.h"
#include "droid.h"
#include "mapgrid.h"
#include "combat.h"
#include "visibility.h"
#include "qtscript.h"
#include "group.h"

// the initial value for the object ID
#define OBJ_ID_INIT 20000

/* The id number for the next object allocated
 * Each object will have a unique id number irrespective of type
 */
uint32_t                unsynchObjID;
uint32_t                synchObjID;

/* Forward function declarations */
#ifdef DEBUG
static void objListIntegCheck();
#endif

/* The lists of objects allocated */
std::array<std::vector<DROID>, MAX_PLAYERS> apsDroidLists;
std::array<std::vector<STRUCTURE>, MAX_PLAYERS> apsStructLists;
std::array<std::vector<FLAG_POSITION>, MAX_PLAYERS> apsFlagPosLists;
std::vector<FEATURE> apsFeatureLists;

std::array<std::vector<STRUCTURE*>, MAX_PLAYERS> apsExtractorLists;
std::vector<BASE_OBJECT*> apsSensorList;
std::vector<FEATURE*> apsOilList;

/* The list of destroyed objects */
std::vector<BASE_OBJECT> psDestroyedObj;

/* Initialise the object heaps */
bool objmemInitialise()
{
	// reset the object ID number
	unsynchObjID = OBJ_ID_INIT / 2; // /2 so that object IDs start around OBJ_ID_INIT*8, in case that's important when loading maps.
	synchObjID   = OBJ_ID_INIT * 4; // *4 so that object IDs start around OBJ_ID_INIT*8, in case that's important when loading maps.

	return true;
}

// Check that psVictim is not referred to by any other object in the game. We can dump out some extra data in debug builds that help track down sources of dangling pointer errors.
#ifdef DEBUG
#define BADREF(func, line) "Illegal reference to object %d from %s line %d", psVictim->id, func, line
#else
#define BADREF(func, line) "Illegal reference to object %d", psVictim->id
#endif
static bool checkReferences(BASE_OBJECT *psVictim)
{
	for (int plr = 0; plr < MAX_PLAYERS; ++plr)
	{
		for (auto const& psStruct : apsStructLists[plr])
		{
			if (&psStruct == psVictim)
			{
				continue;  // Don't worry about self references.
			}

			for (unsigned i = 0; i < psStruct.numWeaps; ++i)
			{
				ASSERT_OR_RETURN(false, psStruct.psTarget[i] != psVictim, BADREF(psStruct->targetFunc[i], psStruct->targetLine[i]));
			}
		}

		for (auto const& psDroid : apsDroidLists[plr])
		{
			if (&psDroid == psVictim)
			{
				continue;  // Don't worry about self references.
			}

			ASSERT_OR_RETURN(false, psDroid.order.psObj != psVictim, "Illegal reference to object %d", psVictim->id);

			ASSERT_OR_RETURN(false, psDroid.psBaseStruct != psVictim, "Illegal reference to object %d", psVictim->id);

			for (unsigned i = 0; i < psDroid.numWeaps; ++i)
			{
				if (psDroid.psActionTarget[i] == psVictim)
				{
					ASSERT_OR_RETURN(false, psDroid.psActionTarget[i] != psVictim, BADREF(psDroid.actionTargetFunc[i], psDroid.actionTargetLine[i]));
				}
			}
		}
	}
	return true;
}

/* Remove an object from the destroyed list, finally freeing its memory
 * Hopefully by this time, no pointers still refer to it! */
static bool objmemDestroy(BASE_OBJECT *psObj)
{
	switch (psObj->type)
	{
	case OBJ_DROID:
		debug(LOG_MEMORY, "freeing droid at %p", static_cast<void *>(psObj));
		break;

	case OBJ_STRUCTURE:
		debug(LOG_MEMORY, "freeing structure at %p", static_cast<void *>(psObj));
		break;

	case OBJ_FEATURE:
		debug(LOG_MEMORY, "freeing feature at %p", static_cast<void *>(psObj));
		break;

	default:
		ASSERT(!"unknown object type", "unknown object type in destroyed list at 0x%p", static_cast<void *>(psObj));
	}
	if (!checkReferences(psObj))
	{
		return false;
	}
	debug(LOG_MEMORY, "BASE_OBJECT* 0x%p is freed.", static_cast<void *>(psObj));
	delete psObj;
	return true;
}

/* General housekeeping for the object system */
void objmemUpdate()
{
	BASE_OBJECT		*psCurr, *psNext, *psPrev;

#ifdef DEBUG
	// do a general validity check first
	objListIntegCheck();
#endif

	/* Go through the destroyed objects list looking for objects that
	   were destroyed before this turn */

	/* First remove the objects from the start of the list */
  for (auto& obj : psDestroyedObj)
  {
    if (obj.died > gameTime - deltaGameTime) {
      triggerEventDestroyed(&obj);
      continue;
    }

    objmemDestroy(&obj);
  }
}

uint32_t generateNewObjectId()
{
	// Generate even ID for unsynchronized objects. This is needed for debug objects, templates and other border lines cases that should preferably be removed one day.
	return unsynchObjID++*MAX_PLAYERS * 2 + selectedPlayer * 2; // Was taken from createObject, where 'player' was used instead of 'selectedPlayer'. Hope there are no stupid hacks that try to recover 'player' from the last 3 bits.
}

uint32_t generateSynchronisedObjectId()
{
	// Generate odd ID for synchronized objects
	uint32_t ret = synchObjID++ * 2 + 1;
	syncDebug("New objectId = %u", ret);
	return ret;
}

/* Move an object from the active list to the destroyed list.
 * \param list is a pointer to the object list
 * \param del is a pointer to the object to remove
 */
template <typename OBJECT>
static inline void destroyObject(OBJECT *object)
{
	ASSERT_OR_RETURN(, object != nullptr, "Invalid pointer");
	ASSERT(gameTime - deltaGameTime <= gameTime || gameTime == 2,
         "Expected %u <= %u, bad time", gameTime - deltaGameTime, gameTime);

  psDestroyedObj.push_back(std::move(*object));
  object->died = gameTime;
  scriptRemoveObject(object);
}

/* add the droid to the Droid Lists */
void addDroid(DROID *psDroidToAdd)
{
  apsDroidLists[psDroidToAdd->player].push_back(*psDroidToAdd);
  psDroidToAdd->died = false;

  if (psDroidToAdd->droidType == DROID_SENSOR) {
    apsSensorList.push_back(psDroidToAdd);
  }

  // commanders have to get their group back if not already loaded
  if (psDroidToAdd->droidType == DROID_COMMAND && !psDroidToAdd->psGroup) {
    auto psGroup = grpCreate();
    psGroup->add(psDroidToAdd);
  }
}

/* Destroy a droid */
void killDroid(DROID *psDel)
{
	ASSERT(psDel->type == OBJ_DROID, "killUnit: pointer is not a unit");
	ASSERT(psDel->player < MAX_PLAYERS, "killUnit: invalid player for unit");

	setDroidTarget(psDel, nullptr);
	for (auto i = 0; i < MAX_WEAPONS; i++)
	{
		setDroidActionTarget(psDel, nullptr, i);
	}

	setDroidBase(psDel, nullptr);
	if (psDel->droidType == DROID_SENSOR) {
    std::erase(apsSensorList, psDel);
	}

	destroyObject(psDel);
}

/* Remove all droids */
void freeAllDroids()
{
  for (auto& vec : apsDroidLists)
  {
    vec.clear();
  }
}

/*Remove a single Droid from a list*/
void removeDroid(DROID *psDroidToRemove)
{
	ASSERT_OR_RETURN(, psDroidToRemove->type == OBJ_DROID, "Pointer is not a unit");
	ASSERT_OR_RETURN(, psDroidToRemove->player < MAX_PLAYERS, "Invalid player for unit");

  std::erase_if(apsDroidLists[psDroidToRemove->player], [psDroidToRemove](auto& curr) {
    return &curr == psDroidToRemove;
  });

	/* Whenever a droid is removed from the current list its died
	 * flag is set to NOT_CURRENT_LIST so that anything targetting
	 * it will cancel itself, and we know it is not really on the map. */
  if (psDroidToRemove->droidType == DROID_SENSOR) {
    std::erase(apsSensorList, psDroidToRemove);
  }
  psDroidToRemove->died = NOT_CURRENT_LIST;
}

/* add the structure to the Structure Lists */
void addStructure(STRUCTURE *psStructToAdd)
{
  apsStructLists[psStructToAdd->player].push_back(*psStructToAdd);

	if (psStructToAdd->pStructureType->pSensor
	    && psStructToAdd->pStructureType->pSensor->location == LOC_TURRET) {
    apsSensorList.push_back(psStructToAdd);
	}
	else if (psStructToAdd->pStructureType->type == REF_RESOURCE_EXTRACTOR) {
    apsExtractorLists[psStructToAdd->player].push_back(psStructToAdd);
	}
}

/* Destroy a structure */
void killStruct(STRUCTURE *psBuilding)
{
	ASSERT(psBuilding->type == OBJ_STRUCTURE, "killStruct: pointer is not a droid");
	ASSERT(psBuilding->player < MAX_PLAYERS, "killStruct: invalid player for stucture");

	if (psBuilding->pStructureType->pSensor
	    && psBuilding->pStructureType->pSensor->location == LOC_TURRET) {
    std::erase(apsSensorList, psBuilding);
	}
	else if (psBuilding->pStructureType->type == REF_RESOURCE_EXTRACTOR) {
    std::erase(apsExtractorLists[psBuilding->player], psBuilding);
	}

	for (auto i = 0; i < MAX_WEAPONS; i++)
	{
		setStructureTarget(psBuilding, nullptr, i, ORIGIN_UNKNOWN);
	}

	if (psBuilding->pFunctionality != nullptr) {
		if (StructIsFactory(psBuilding)) {
			FACTORY *psFactory = &psBuilding->pFunctionality->factory;

			// remove any commander from the factory
			if (psFactory->psCommander != nullptr) {
				assignFactoryCommandDroid(psBuilding, nullptr);
			}

			// remove any assembly points
			if (psFactory->psAssemblyPoint != nullptr) {
				removeFlagPosition(psFactory->psAssemblyPoint);
				psFactory->psAssemblyPoint = nullptr;
			}
		}
		else if (psBuilding->pStructureType->type == REF_REPAIR_FACILITY)
		{
			REPAIR_FACILITY *psRepair = &psBuilding->pFunctionality->repairFacility;

			if (psRepair->psDeliveryPoint)
			{
				// free up repair fac stuff
				removeFlagPosition(psRepair->psDeliveryPoint);
				psRepair->psDeliveryPoint = nullptr;
			}
		}
	}

	destroyObject(psBuilding);
}

/* Remove heapall structures */
void freeAllStructs()
{
  for (auto& vec : apsStructLists)
  {
    vec.clear();
  }
}

/*Remove a single Structure from a list*/
void removeStructureFromList(STRUCTURE *psStructToRemove)
{
	ASSERT(psStructToRemove->type == OBJ_STRUCTURE, "removeStructureFromList: pointer is not a structure");
	ASSERT(psStructToRemove->player < MAX_PLAYERS, "removeStructureFromList: invalid player for structure");
  std::erase_if(apsStructLists[psStructToRemove->player], [psStructToRemove](auto& curr) {
    return &curr == psStructToRemove;
  });

	if (psStructToRemove->pStructureType->pSensor
	    && psStructToRemove->pStructureType->pSensor->location == LOC_TURRET) {
    std::erase(apsSensorList, psStructToRemove);
	}
	else if (psStructToRemove->pStructureType->type == REF_RESOURCE_EXTRACTOR) {
    std::erase(apsExtractorLists[psStructToRemove->player], psStructToRemove);
	}
}

/* add the feature to the Feature Lists */
void addFeature(FEATURE *psFeatureToAdd)
{
  apsFeatureLists.push_back(*psFeatureToAdd);
	if (psFeatureToAdd->psStats->subType == FEAT_OIL_RESOURCE) {
    apsOilList.push_back(psFeatureToAdd);
	}
}

/* Destroy a feature */
// set the player to 0 since features have player = maxplayers+1. This screws up destroyObject
// it's a bit of a hack, but hey, it works
void killFeature(FEATURE *psDel)
{
	ASSERT(psDel->type == OBJ_FEATURE, "killFeature: pointer is not a feature");
	psDel->player = 0;
	destroyObject(psDel);

	if (psDel->psStats->subType == FEAT_OIL_RESOURCE) {
    std::erase(apsOilList, psDel);
	}
}

/* Remove all features */
void freeAllFeatures()
{
    apsFeatureLists.clear();
}

/* Create a new Flag Position */
bool createFlagPosition(FLAG_POSITION **ppsNew, UDWORD player)
{
	ASSERT(player < MAX_PLAYERS, "createFlagPosition: invalid player number");

	*ppsNew = (FLAG_POSITION *)calloc(1, sizeof(FLAG_POSITION));
	if (*ppsNew == nullptr)
	{
		debug(LOG_ERROR, "Out of memory");
		return false;
	}
	(*ppsNew)->type = POS_DELIVERY;
	(*ppsNew)->player = player;
	(*ppsNew)->frameNumber = 0;
	(*ppsNew)->selected = false;
	(*ppsNew)->coords.x = ~0;
	(*ppsNew)->coords.y = ~0;
	(*ppsNew)->coords.z = ~0;
	return true;
}

static bool isFlagPositionInList(FLAG_POSITION *psFlagPosToAdd)
{
	ASSERT_OR_RETURN(false, psFlagPosToAdd != nullptr, "Invalid FlagPosition pointer");
	ASSERT_OR_RETURN(false, psFlagPosToAdd->player < MAX_PLAYERS, "Invalid FlagPosition player: %u", psFlagPosToAdd->player);

	for (auto& psCurr : apsFlagPosLists[psFlagPosToAdd->player])
	{
		if (&psCurr == psFlagPosToAdd) {
			return true;
		}
	}
	return false;
}

/* add the Flag Position to the Flag Position Lists */
void addFlagPosition(FLAG_POSITION *psFlagPosToAdd)
{
	ASSERT_OR_RETURN(, psFlagPosToAdd != nullptr, "Invalid FlagPosition pointer");
	ASSERT_OR_RETURN(, psFlagPosToAdd->coords.x != ~0, "flag has invalid position");
	ASSERT_OR_RETURN(, psFlagPosToAdd->player < MAX_PLAYERS, "Invalid FlagPosition player: %u", psFlagPosToAdd->player);
	ASSERT_OR_RETURN(, !isFlagPositionInList(psFlagPosToAdd), "FlagPosition is already in the list!");

	apsFlagPosLists[psFlagPosToAdd->player].push_back(*psFlagPosToAdd);
}

/* Remove a Flag Position from the Lists */
void removeFlagPosition(FLAG_POSITION *psDel)
{
  ASSERT_OR_RETURN(, psDel != nullptr, "Invalid Flag Position pointer");
  for (auto player = 0; player < MAX_PLAYERS; ++player)
  {
    std::erase_if(apsFlagPosLists[player], [psDel](auto& curr) {
      return &curr == psDel;
    });
  }
}

// free all flag positions
void freeAllFlagPositions()
{
  for (auto& vec : apsFlagPosLists)
  {
      vec.clear();
  }
}


#ifdef DEBUG
// check all flag positions for duplicate delivery points
void checkFactoryFlags()
{
	static std::vector<unsigned> factoryDeliveryPointCheck[NUM_FLAG_TYPES];  // Static to save allocations.

	//check the flags
	for (unsigned player = 0; player < MAX_PLAYERS; ++player)
	{
		//clear the check array
		for (int type = 0; type < NUM_FLAG_TYPES; ++type)
		{
			factoryDeliveryPointCheck[type].clear();
		}

		FLAG_POSITION *psFlag = apsFlagPosLists[player];
		while (psFlag)
		{
			if ((psFlag->type == POS_DELIVERY) &&//check this is attached to a unique factory
			    (psFlag->factoryType != REPAIR_FLAG))
			{
				unsigned type = psFlag->factoryType;
				unsigned factory = psFlag->factoryInc;
				factoryDeliveryPointCheck[type].push_back(factory);
			}
			psFlag = psFlag->psNext;
		}
		for (int type = 0; type < NUM_FLAG_TYPES; ++type)
		{
			std::sort(factoryDeliveryPointCheck[type].begin(), factoryDeliveryPointCheck[type].end());
			ASSERT(std::unique(factoryDeliveryPointCheck[type].begin(), factoryDeliveryPointCheck[type].end()) == factoryDeliveryPointCheck[type].end(), "DUPLICATE FACTORY DELIVERY POINT FOUND");
		}
	}
}
#endif

template<typename T>
T* findById(unsigned id, std::vector<T>& vec)
{
  auto it = std::find_if(vec.begin(), vec.end(),
                         [id](auto const& t) {
    return t.id == id;
  });
  return it == vec.end() ? nullptr : &*it;
}

// Find a base object from it's id
BASE_OBJECT *getBaseObjFromData(unsigned id, unsigned player, OBJECT_TYPE type)
{
  switch (type) {
    case OBJ_DROID:
      return findById(id, apsDroidLists[player]);
    case OBJ_STRUCTURE:
      return findById(id, apsStructLists[player]);
    case OBJ_FEATURE:
      return findById(id, apsFeatureLists);
    default:
      return nullptr;
  }
}

BASE_OBJECT* getBaseObjFromId(unsigned id, OBJECT_TYPE type)
{
  if (type == OBJ_FEATURE) {
    return findById(id, apsFeatureLists);
  }

  for (auto player = 0; player < MAX_PLAYERS; ++player)
  {
    return getBaseObjFromData(id, player, type);
  }
}

UDWORD getRepairIdFromFlag(FLAG_POSITION *psFlag)
{
  for (auto& psObj : apsStructLists[psFlag->player])
  {
    if (!psObj.pFunctionality || psObj.pStructureType->type != REF_REPAIR_FACILITY)
      continue;

    //check for matching delivery point
    auto psRepair = (REPAIR_FACILITY *) psObj.pFunctionality;
    if (psRepair->psDeliveryPoint == psFlag) {
      return psObj.id;
    }
  }

	ASSERT(!"unable to find repair id for FLAG_POSITION", "getRepairIdFromFlag() failed");
	return UDWORD_MAX;
}

// integrity check the lists
#ifdef DEBUG
static void objListIntegCheck()
{
	SDWORD			player;
	BASE_OBJECT		*psCurr;

	for (player = 0; player < MAX_PLAYERS; player += 1)
	{
		for (psCurr = (BASE_OBJECT *)apsDroidLists[player]; psCurr; psCurr = psCurr->psNext)
		{
			ASSERT(psCurr->type == OBJ_DROID &&
			       (SDWORD)psCurr->player == player,
			       "objListIntegCheck: misplaced object in the droid list for player %d",
			       player);
		}
	}
	for (player = 0; player < MAX_PLAYERS; player += 1)
	{
		for (psCurr = (BASE_OBJECT *)apsStructLists[player]; psCurr; psCurr = psCurr->psNext)
		{
			ASSERT(psCurr->type == OBJ_STRUCTURE &&
			       (SDWORD)psCurr->player == player,
			       "objListIntegCheck: misplaced %s(%p) in the structure list for player %d, is owned by %d",
			       objInfo(psCurr), (void*) psCurr, player, (int)psCurr->player);
		}
	}
	for (psCurr = (BASE_OBJECT *)apsFeatureLists[0]; psCurr; psCurr = psCurr->psNext)
	{
		ASSERT(psCurr->type == OBJ_FEATURE,
		       "objListIntegCheck: misplaced object in the feature list");
	}
	for (psCurr = (BASE_OBJECT *)psDestroyedObj; psCurr; psCurr = psCurr->psNext)
	{
		ASSERT(psCurr->died > 0, "objListIntegCheck: Object in destroyed list but not dead!");
	}
}
#endif

void objCount(int *droids, int *structures, int *features)
{
	*droids = 0;
	*structures = 0;
	*features = 0;

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		for (auto& psDroid : apsDroidLists[i])
		{
			(*droids)++;
			if (isTransporter(&psDroid)) {
				for (auto psTrans : psDroid.psGroup->psList)
				{
					(*droids)++;
				}
			}
		}

		for (auto psStruct : apsStructLists[i])
		{
			(*structures)++;
		}
	}

	for (auto psFeat : apsFeatureLists)
	{
		(*features)++;
	}
}
