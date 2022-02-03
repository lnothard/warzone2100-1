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
 * @file objmem.cpp
 * Object memory management functions
 */

#include "objmem.h"
#include "qtscript.h"
#include "mission.h"

// the initial value for the object ID
static constexpr auto OBJ_ID_INIT = 20000;

/* The id number for the next object allocated
 * Each object will have a unique id number irrespective of type
 */
unsigned unsynchObjID;
unsigned synchObjID;

/* Forward function declarations */
#ifdef DEBUG
static void objListIntegCheck();
#endif


/* Initialise the object heaps */
bool objmemInitialise()
{
	// reset the object ID number
	unsynchObjID = OBJ_ID_INIT / 2;
	// /2 so that object IDs start around OBJ_ID_INIT*8, in case that's important when loading maps.
	synchObjID = OBJ_ID_INIT * 4;
	// *4 so that object IDs start around OBJ_ID_INIT*8, in case that's important when loading maps.

	return true;
}

// Check that psVictim is not referred to by any other object in the game. We can dump out some extra data in debug builds that help track down sources of dangling pointer errors.
#ifdef DEBUG
#define BADREF(func, line) "Illegal reference to object %d from %s line %d", psVictim->id, func, line
#else
#define BADREF(func, line) "Illegal reference to object %d", psVictim->getId()
#endif

static bool checkReferences(BaseObject const* psVictim)
{
	for (auto plr = 0; plr < MAX_PLAYERS; ++plr)
	{
		for (auto& psStruct : apsStructLists[plr])
		{
			if (psStruct.get() == psVictim) {
        // don't worry about self references
				continue;
			}

			for (auto i = 0; i < numWeapons(*psStruct); ++i)
			{
				ASSERT_OR_RETURN(false, psStruct->getTarget(i) != psVictim,
				                 BADREF(psStruct->targetFunc[i], psStruct->targetLine[i]));
			}
		}
		for (auto& psDroid : apsDroidLists[plr])
		{
			if (&psDroid == psVictim) {
				continue; // Don't worry about self references.
			}

			ASSERT_OR_RETURN(false, psDroid.getOrder()->target != psVictim, "Illegal reference to object %d", psVictim->getId());
			ASSERT_OR_RETURN(false, psDroid.getBase() != psVictim, "Illegal reference to object %d", psVictim->getId());

			for (auto i = 0; i < numWeapons(psDroid); ++i)
			{
				if (psDroid.getTarget(i) == psVictim) {
					ASSERT_OR_RETURN(false, psDroid.getTarget(i) != psVictim,
					                 BADREF(psDroid->actionTargetFunc[i], psDroid->actionTargetLine[i]));
				}
			}
		}
	}
	return true;
}

/* General housekeeping for the object system */
void objmemUpdate()
{
  BaseObject *psCurr, *psNext, *psPrev;

#ifdef DEBUG
	// do a general validity check first
	objListIntegCheck();
#endif

	/* Go through the destroyed objects list looking for objects that
	   were destroyed before this turn */

	/* First remove the objects from the start of the list */
	for (auto obj : psDestroyedObj)//!psDestroyedObj.empty() && psDestroyedObj->died <= gameTime - deltaGameTime)
	{
		objmemDestroy(psDestroyedObj);
	}

	/* Now see if there are any further down the list
	Keep track of the previous object to set its Next pointer*/
	for (auto& psCurr : psDestroyedObj)
	{
		if (psCurr->damageManager->getTimeOfDeath() <= gameTime - deltaGameTime) {
			/*set the linked list up - you will never be deleting the top
			of the list, so don't have to check*/
			objmemDestroy(psCurr);
		}
		else {
			// do the object died callback
			triggerEventDestroyed(psCurr);
			psPrev = psCurr;
		}
	}
}

unsigned generateNewObjectId()
{
	// Generate even ID for unsynchronized objects. This is needed for debug objects, templates and other border lines cases that should preferably be removed one day.
	return unsynchObjID++ * MAX_PLAYERS * 2 + selectedPlayer * 2;
	// Was taken from createObject, where 'player' was used instead of 'selectedPlayer'. Hope there are no stupid hacks that try to recover 'player' from the last 3 bits.
}

unsigned generateSynchronisedObjectId()
{
	// Generate odd ID for synchronized objects
	unsigned ret = synchObjID++ * 2 + 1;
	syncDebug("New objectId = %u", ret);
	return ret;
}

/* Add the object to its list
 * \param list is a pointer to the object list
 */
template <typename OBJECT>
static inline void addObjectToList(OBJECT* list[], OBJECT* object, unsigned player)
{
	ASSERT_OR_RETURN(, object != nullptr, "Invalid pointer");

	// Prepend the object to the top of the list
	object->psNext = list[player];
	list[player] = object;
}

/* Add the object to its list
 * \param list is a pointer to the object list
 */
template <typename OBJECT>
static inline void addObjectToFuncList(OBJECT* list[], OBJECT* object, unsigned player)
{
	ASSERT_OR_RETURN(, object != nullptr, "Invalid pointer");
	ASSERT_OR_RETURN(, static_cast<OBJECT *>(object->psNextFunc) == nullptr, "%s(%p) is already in a function list!",
	                   objInfo(object), static_cast<void *>(object));

	// Prepend the object to the top of the list
	object->psNextFunc = list[player];
	list[player] = object;
}

/* Move an object from the active list to the destroyed list.
 * \param list is a pointer to the object list
 * \param del is a pointer to the object to remove
 */
template <typename OBJECT>
static inline void destroyObject(OBJECT* list[], OBJECT* object)
{
	ASSERT_OR_RETURN(, object != nullptr, "Invalid pointer");
	ASSERT(gameTime - deltaGameTime <= gameTime || gameTime == 2, "Expected %u <= %u, bad time",
	       gameTime - deltaGameTime, gameTime);

	// If the message to remove is the first one in the list then mark the next one as the first
	if (list[object->player] == object)
	{
		list[object->player] = list[object->player]->psNext;
		object->psNext = psDestroyedObj;
		psDestroyedObj = (PlayerOwnedObject *)object;
		object->died = gameTime;
		scriptRemoveObject(object);
		return;
	}

	// Iterate through the list and find the item before the object to delete
	OBJECT *psPrev = nullptr, *psCurr;
	for (psCurr = list[object->player]; (psCurr != object) && (psCurr != nullptr); psCurr = psCurr->psNext)
	{
		psPrev = psCurr;
	}

	ASSERT(psCurr != nullptr, "Object %s(%d) not found in list", objInfo(object), object->id);

	if (psCurr != nullptr)
	{
		// Modify the "next" pointer of the previous item to
		// point to the "next" item of the item to delete.
		psPrev->psNext = psCurr->psNext;

		// Prepend the object to the destruction list
		object->psNext = psDestroyedObj;
		psDestroyedObj = object;

		// Set destruction time
		object->died = gameTime;
	}
	scriptRemoveObject(object);
}

/* Remove an object from the active list
 * \param list is a pointer to the object list
 * \param remove is a pointer to the object to remove
 * \param type is the type of the object
 */
template <typename OBJECT>
static inline void removeObjectFromList(OBJECT* list[], OBJECT* object, unsigned player)
{
	ASSERT_OR_RETURN(, object != nullptr, "Invalid pointer");

	// If the message to remove is the first one in the list then mark the next one as the first
	if (list[player] == object)
	{
		list[player] = list[player]->psNext;
		return;
	}

	// Iterate through the list and find the item before the object to delete
	OBJECT *psPrev = nullptr, *psCurr;
	for (psCurr = list[player]; (psCurr != object) && (psCurr != nullptr); psCurr = psCurr->psNext)
	{
		psPrev = psCurr;
	}

	ASSERT_OR_RETURN(, psCurr != nullptr, "Object %p not found in list", static_cast<void *>(object));

	// Modify the "next" pointer of the previous item to
	// point to the "next" item of the item to delete.
	psPrev->psNext = psCurr->psNext;
}

/* Remove an object from the relevant function list. An object can only be in one function list at a time!
 * \param list is a pointer to the object list
 * \param remove is a pointer to the object to remove
 * \param type is the type of the object
 */
template <typename OBJECT>
static inline void removeObjectFromFuncList(OBJECT* list[], OBJECT* object, unsigned player)
{
	ASSERT_OR_RETURN(, object != nullptr, "Invalid pointer");

	// If the message to remove is the first one in the list then mark the next one as the first
	if (list[player] == object)
	{
		list[player] = list[player]->psNextFunc;
		object->psNextFunc = nullptr;
		return;
	}

	// Iterate through the list and find the item before the object to delete
	OBJECT *psPrev = nullptr, *psCurr;
	for (psCurr = list[player]; psCurr != object && psCurr != nullptr; psCurr = psCurr->psNextFunc)
	{
		psPrev = psCurr;
	}

	ASSERT_OR_RETURN(, psCurr != nullptr, "Object %p not found in list", static_cast<void *>(object));

	// Modify the "next" pointer of the previous item to
	// point to the "next" item of the item to delete.
	psPrev->psNextFunc = psCurr->psNextFunc;
	object->psNextFunc = nullptr;
}

template <typename OBJECT>
static inline void releaseAllObjectsInList(OBJECT* list[])
{
	// Iterate through all players' object lists
	for (unsigned i = 0; i < MAX_PLAYERS; ++i)
	{
		// Iterate through all objects in list
		OBJECT* psNext;
		for (OBJECT* psCurr = list[i]; psCurr != nullptr; psCurr = psNext)
		{
			psNext = psCurr->psNext;

			// FIXME: the next call is disabled for now, yes, it will leak memory again.
			// issue is with campaign games, and the swapping pointers 'trick' Pumpkin uses.
			//	visRemoveVisibility(psCurr);
			// Release object's memory
			delete psCurr;
		}
		list[i] = nullptr;
	}
}

/* add the droid to the Droid Lists */
void addDroid(Droid* psDroidToAdd)
{
  apsDroidLists[psDroidToAdd->playerManager->getPlayer()].push_back(*psDroidToAdd);

  psDroidToAdd->damageManager->setTimeOfDeath(0);
  if (psDroidToAdd->getType() == DROID_TYPE::SENSOR) {
    apsSensorList.push_back(psDroidToAdd);
  }

  // commanders have to get their group back if not already loaded
  if (psDroidToAdd->getType() == DROID_TYPE::COMMAND && !psDroidToAdd->getGroup()) {
    auto psGroup = Group::create(-1);
    psGroup->add(psDroidToAdd);
  }
}

/* Destroy a droid */
void killDroid(Droid* psDel)
{
	ASSERT(psDel->playerManager->getPlayer() < MAX_PLAYERS,
	       "killUnit: invalid player for unit");

	setDroidTarget(psDel, nullptr);
	for (auto i = 0; i < MAX_WEAPONS; i++)
	{
		setDroidActionTarget(psDel, nullptr, i);
	}
	setDroidBase(psDel, nullptr);

  if (psDel->getType() == DROID_TYPE::SENSOR) {
    std::erase(apsSensorList, psDel);
	}
  std::erase(apsDroidLists[psDel->playerManager->getPlayer()], psDel);
}

/* Remove all droids */
void freeAllDroids()
{
  std::for_each(apsDroidLists.begin(), apsDroidLists.end(),
                [](auto& list) {
    list.clear();
  });
}

/*Remove a single Droid from a list*/
void removeDroid(Droid* psDroidToRemove)
{
	ASSERT_OR_RETURN(, psDroidToRemove->playerManager->getPlayer() < MAX_PLAYERS, "Invalid player for unit");
  std::erase(apsDroidLists[psDroidToRemove->playerManager->getPlayer()], psDroidToRemove);

	/* Whenever a droid is removed from the current list its died
	 * flag is set to NOT_CURRENT_LIST so that anything targetting
	 * it will cancel itself, and we know it is not really on the map. */
  if (psDroidToRemove->getType() == DROID_TYPE::SENSOR) {
    std::erase(apsSensorList, psDroidToRemove);
  }
  psDroidToRemove->damageManager->setTimeOfDeath(NOT_CURRENT_LIST);
}

/*Removes all droids that may be stored in the mission lists*/
void freeAllMissionDroids()
{
  std::for_each(mission.apsDroidLists.begin(),
                mission.apsDroidLists.end(),
                [](auto& list) {
    list.clear();
  });
}

/*Removes all droids that may be stored in the limbo lists*/
void freeAllLimboDroids()
{
  std::for_each(apsLimboDroids.begin(),
                apsLimboDroids.end(),
                [](auto& list) {
    list.clear();
  });
}

/* add the structure to the Structure Lists */
void addStructure(Structure* psStructToAdd)
{
  apsStructLists[psStructToAdd->playerManager->getPlayer()].push_back(std::make_unique<Structure>(*psStructToAdd));
	if (psStructToAdd->getStats()->sensor_stats && psStructToAdd->getStats()->sensor_stats->location == LOC::TURRET) {
    apsSensorList.push_back(psStructToAdd);
	}
	else if (psStructToAdd->getStats()->type == STRUCTURE_TYPE::RESOURCE_EXTRACTOR) {
    apsExtractorLists[psStructToAdd->playerManager->getPlayer()].push_back(
            dynamic_cast<ResourceExtractor*>(psStructToAdd));
	}
}

/* Destroy a structure */
void killStruct(Structure* psBuilding)
{
	ASSERT(psBuilding->playerManager->getPlayer() < MAX_PLAYERS,
	       "killStruct: invalid player for stucture");

	if (psBuilding->getStats()->sensor_stats &&
      psBuilding->getStats()->sensor_stats->location == LOC::TURRET) {
    std::erase(apsSensorList, psBuilding);
	}
	else if (psBuilding->getStats()->type == STRUCTURE_TYPE::RESOURCE_EXTRACTOR) {
    std::erase(apsExtractorLists[psBuilding->playerManager->getPlayer()], psBuilding);
	}

	for (auto i = 0; i < MAX_WEAPONS; i++)
	{
		setStructureTarget(psBuilding, nullptr, i, TARGET_ORIGIN::UNKNOWN);
	}

  if (StructIsFactory(psBuilding)) {
    Factory* psFactory = &psBuilding->pFunctionality->factory;

    // remove any commander from the factory
    if (psFactory->psCommander != nullptr) {
      assignFactoryCommandDroid(psBuilding, nullptr);
    }

    // remove any assembly points
    if (psFactory->psAssemblyPoint != nullptr)
    {
      removeFlagPosition(psFactory->psAssemblyPoint);
      psFactory->psAssemblyPoint = nullptr;
    }
  }
  else if (psBuilding->getStats()->type == STRUCTURE_TYPE::REPAIR_FACILITY)
  {
    RepairFacility* psRepair = &psBuilding->pFunctionality->repairFacility;

    if (psRepair->psDeliveryPoint) {
      // free up repair fac stuff
      removeFlagPosition(psRepair->psDeliveryPoint);
      psRepair->psDeliveryPoint = nullptr;
    }
  }
	destroyObject(apsStructLists, psBuilding);
}

/* Remove heapall structures */
void freeAllStructs()
{
  std::for_each(apsStructLists.begin(), apsStructLists.end(),
                [](auto& list) {
    list.clear();
  });
}

/*Remove a single Structure from a list*/
void removeStructureFromList(Structure* psStructToRemove)
{
  auto player = psStructToRemove->playerManager->getPlayer();
	ASSERT(player < MAX_PLAYERS, "removeStructureFromList: invalid player for structure");

  std::erase(apsStructLists[player], psStructToRemove);

	if (psStructToRemove->getStats()->sensor_stats &&
      psStructToRemove->getStats()->sensor_stats->location == LOC::TURRET) {
    std::erase(apsSensorList, psStructToRemove);
	}
	else if (psStructToRemove->getStats()->type == STRUCTURE_TYPE::RESOURCE_EXTRACTOR) {
    std::erase(apsExtractorLists[player], psStructToRemove);
	}
}

/* add the feature to the Feature Lists */
void addFeature(Feature* psFeatureToAdd)
{
  apsFeatureLists[0].push_back(psFeatureToAdd);
	if (psFeatureToAdd->getStats()->subType == FEATURE_TYPE::OIL_RESOURCE) {
		addObjectToFuncList(apsOilList, psFeatureToAdd, 0);
	}
}

/* Destroy a feature */
// set the player to 0 since features have player = maxplayers+1. This screws up destroyObject
// it's a bit of a hack, but hey, it works
void killFeature(Feature* psDel)
{
	psDel->playerManager->setPlayer(0);
	destroyObject(apsFeatureLists, psDel);

	if (psDel->getStats()->subType == FEATURE_TYPE::OIL_RESOURCE) {
		removeObjectFromFuncList(apsOilList, psDel, 0);
	}
}

/* Remove all features */
void freeAllFeatures()
{
	releaseAllObjectsInList(apsFeatureLists);
}

/**************************  FlagPosition ********************************/

/* Create a new Flag Position */
bool createFlagPosition(FlagPosition** ppsNew, unsigned player)
{
	ASSERT(player < MAX_PLAYERS, "createFlagPosition: invalid player number");

	*ppsNew = (FlagPosition*)calloc(1, sizeof(FlagPosition));
	if (*ppsNew == nullptr) {
		debug(LOG_ERROR, "Out of memory");
		return false;
	}
	(*ppsNew)->type = POSITION_TYPE::POS_DELIVERY;
	(*ppsNew)->player = player;
	(*ppsNew)->frameNumber = 0;
	(*ppsNew)->selected = false;
	(*ppsNew)->coords.x = ~0;
	(*ppsNew)->coords.y = ~0;
	(*ppsNew)->coords.z = ~0;
	return true;
}

static bool isFlagPositionInList(FlagPosition* psFlagPosToAdd)
{
	ASSERT_OR_RETURN(false, psFlagPosToAdd != nullptr, "Invalid FlagPosition pointer");
	ASSERT_OR_RETURN(false, psFlagPosToAdd->player < MAX_PLAYERS, "Invalid FlagPosition player: %u",
	                 psFlagPosToAdd->player);
	for (auto psCurr = apsFlagPosLists[psFlagPosToAdd->player]; (psCurr != nullptr); psCurr = psCurr->psNext)
	{
		if (psCurr == psFlagPosToAdd)
		{
			return true;
		}
	}
	return false;
}

/* add the Flag Position to the Flag Position Lists */
void addFlagPosition(FlagPosition* psFlagPosToAdd)
{
	ASSERT_OR_RETURN(, psFlagPosToAdd != nullptr, "Invalid FlagPosition pointer");
	ASSERT_OR_RETURN(, psFlagPosToAdd->coords.x != ~0, "flag has invalid position");
	ASSERT_OR_RETURN(, psFlagPosToAdd->player < MAX_PLAYERS, "Invalid FlagPosition player: %u", psFlagPosToAdd->player);
	ASSERT_OR_RETURN(, !isFlagPositionInList(psFlagPosToAdd), "FlagPosition is already in the list!");

	psFlagPosToAdd->psNext = apsFlagPosLists[psFlagPosToAdd->player];
	apsFlagPosLists[psFlagPosToAdd->player] = psFlagPosToAdd;
}

/* Remove a Flag Position from the Lists */
void removeFlagPosition(FlagPosition* psDel)
{
	ASSERT_OR_RETURN(, psDel != nullptr, "Invalid Flag Position pointer");
  for (auto& list : apsFlagPosLists)
  {
    std::erase(list, psDel);
  }
}

// free all flag positions
void freeAllFlagPositions()
{
	for (auto player = 0; player < MAX_PLAYERS; player++)
	{
    apsFlagPosLists[player].clear();
	}
}


#ifdef DEBUG
// check all flag positions for duplicate delivery points
void checkFactoryFlags()
{
	static std::vector<unsigned> factoryDeliveryPointCheck[NUM_FLAG_TYPES]; // Static to save allocations.

	//check the flags
	for (unsigned player = 0; player < MAX_PLAYERS; ++player)
	{
		//clear the check array
		for (int type = 0; type < NUM_FLAG_TYPES; ++type)
		{
			factoryDeliveryPointCheck[type].clear();
		}

		FlagPosition* psFlag = apsFlagPosLists[player];
		while (psFlag)
		{
			if ((psFlag->type == POS_DELIVERY) && //check this is attached to a unique factory
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
			ASSERT(
				std::unique(factoryDeliveryPointCheck[type].begin(), factoryDeliveryPointCheck[type].end()) ==
				factoryDeliveryPointCheck[type].end(), "DUPLICATE FACTORY DELIVERY POINT FOUND");
		}
	}
}
#endif


// Find a base object from its id
BaseObject* getBaseObjFromData(unsigned id, unsigned player, OBJECT_TYPE type)
{
  BaseObject* psObj;
	Droid* psTrans;

	for (auto i = 0; i < 3; ++i)
	{
		psObj = nullptr;
		switch (i) {
		case 0:
			switch (type) {
        case OBJECT_TYPE::DROID: psObj = apsDroidLists[player];
				break;
        case OBJECT_TYPE::STRUCTURE: psObj = apsStructLists[player];
				break;
        case OBJECT_TYPE::FEATURE: psObj = apsFeatureLists[0];
			default: break;
			}
			break;
		case 1:
			switch (type)
			{
        case OBJECT_TYPE::DROID: psObj = mission.apsDroidLists[player];
				break;
        case OBJECT_TYPE::STRUCTURE: psObj = mission.apsStructLists[player];
				break;
        case OBJECT_TYPE::FEATURE: psObj = mission.apsFeatureLists[0];
				break;
			default: break;
			}
			break;
		case 2:
			if (player == 0 && type == OBJECT_TYPE::DROID) {
				psObj = apsLimboDroids[0];
			}
			break;
		}

		while (psObj)
		{
			if (psObj->getId() == id) {
				return psObj;
			}
			// if transporter check any droids in the grp
			if (psObj->type == OBJ_DROID && isTransporter((Droid *) psObj))
			{
				for (psTrans = ((Droid*)psObj)->group->members; psTrans != nullptr; psTrans = psTrans->psGrpNext)
				{
					if (psTrans->getId() == id) {
						return psTrans;
					}
				}
			}
			psObj = psObj->psNext;
		}
	}
	ASSERT(false, "failed to find id %d for player %d", id, player);

	return nullptr;
}

// Find a base object from it's id
BaseObject* getBaseObjFromId(unsigned id)
{
  BaseObject* psObj;
	Droid* psTrans;

	for (auto i = 0; i < 7; ++i)
	{
		for (auto player = 0; player < MAX_PLAYERS; ++player)
		{
			switch (i) {
			case 0:
				psObj = apsDroidLists[player];
				break;
			case 1:
				psObj = apsStructLists[player];
				break;
			case 2:
				if (player == 0) {
					psObj = apsFeatureLists[0];
				}
				else
				{
					psObj = nullptr;
				}
				break;
			case 3:
				psObj = mission.apsDroidLists[player];
				break;
			case 4:
				psObj = mission.apsStructLists[player];
				break;
			case 5:
				if (player == 0)
				{
					psObj = mission.apsFeatureLists[0];
				}
				else
				{
					psObj = nullptr;
				}
				break;
			case 6:
				if (player == 0)
				{
					psObj = apsLimboDroids[0];
				}
				else
				{
					psObj = nullptr;
				}
				break;
			default:
				psObj = nullptr;
				break;
			}

			while (psObj)
			{
				if (psObj->getId() == id) {
					return psObj;
				}
				// if transporter check any droids in the grp
				if ((psObj->type == OBJ_DROID) && isTransporter((Droid*)psObj))
				{
					for (psTrans = ((Droid*)psObj)->group->members; psTrans != nullptr; psTrans = psTrans->psGrpNext)
					{
						if (psTrans->getId() == id)
						{
							return psTrans;
						}
					}
				}
				psObj = psObj->psNext;
			}
		}
	}
	ASSERT(!"couldn't find a BASE_OBJ with ID", "getBaseObjFromId() failed for id %d", id);

	return nullptr;
}

unsigned getRepairIdFromFlag(FlagPosition const* psFlag)
{
	UDWORD player;
	Structure* psObj;
	RepairFacility* psRepair;


	player = psFlag->player;

	//probably don't need to check mission list
	for (auto i = 0; i < 2; ++i)
	{
		switch (i)
		{
		case 0:
			psObj = (Structure*)apsStructLists[player];
			break;
		case 1:
			psObj = (Structure*)mission.apsStructLists[player];
			break;
		default:
			psObj = nullptr;
			break;
		}

		while (psObj)
		{
				if (psObj->getStats()->type == STRUCTURE_TYPE::REPAIR_FACILITY) {
					//check for matching delivery point
					psRepair = ((RepairFacility*)psObj->pFunctionality);
					if (psRepair->psDeliveryPoint == psFlag) {
						return psObj->getId();
					}
				}
			psObj = psObj->psNext;
		}
	}
	ASSERT(!"unable to find repair id for FlagPosition", "getRepairIdFromFlag() failed");

	return UDWORD_MAX;
}

// integrity check the lists
#ifdef DEBUG
static void objListIntegCheck()
{
	SDWORD player;
	SimpleObject* psCurr;

	for (player = 0; player < MAX_PLAYERS; player += 1)
	{
		for (psCurr = (SimpleObject*)apsDroidLists[player]; psCurr; psCurr = psCurr->psNext)
		{
			ASSERT(psCurr->type == OBJ_DROID &&
			       (SDWORD)psCurr->player == player,
			       "objListIntegCheck: misplaced object in the droid list for player %d",
			       player);
		}
	}
	for (player = 0; player < MAX_PLAYERS; player += 1)
	{
		for (psCurr = (SimpleObject*)apsStructLists[player]; psCurr; psCurr = psCurr->psNext)
		{
			ASSERT(psCurr->type == OBJ_STRUCTURE &&
			       (SDWORD)psCurr->player == player,
			       "objListIntegCheck: misplaced %s(%p) in the structure list for player %d, is owned by %d",
			       objInfo(psCurr), (void*) psCurr, player, (int)psCurr->player);
		}
	}
	for (psCurr = (SimpleObject*)apsFeatureLists[0]; psCurr; psCurr = psCurr->psNext)
	{
		ASSERT(psCurr->type == OBJ_FEATURE,
		       "objListIntegCheck: misplaced object in the feature list");
	}
	for (psCurr = (SimpleObject*)psDestroyedObj; psCurr; psCurr = psCurr->psNext)
	{
		ASSERT(psCurr->died > 0, "objListIntegCheck: Object in destroyed list but not dead!");
	}
}
#endif

void objCount(int* droids, int* structures, int* features)
{
	*droids = 0;
	*structures = 0;
	*features = 0;

	for (auto i = 0; i < MAX_PLAYERS; i++)
	{
		for (auto& psDroid : apsDroidLists[i])
		{
			(*droids)++;
			if (isTransporter(psDroid)) {
				Droid* psTrans = psDroid->group->members;

				for (psTrans = psTrans->psGrpNext; psTrans != nullptr; psTrans = psTrans->psGrpNext)
				{
					(*droids)++;
				}
			}
		}

		for (auto& psStruct : apsStructLists[i])
		{
			(*structures)++;
		}
	}

	for (auto psFeat : apsFeatureLists[0])
	{
		(*features)++;
	}
}
