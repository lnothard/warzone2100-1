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
 *  Definitions for the structures.
 */

#ifndef __INCLUDED_SRC_STRUCTURE_H__
#define __INCLUDED_SRC_STRUCTURE_H__

#include "lib/framework/string_ext.h"
#include "lib/framework/wzconfig.h"

#include "objectdef.h"
#include "structuredef.h"
#include "visibility.h"
#include "baseobject.h"

// how long to wait between CALL_STRUCT_ATTACKED's - plus how long to flash on radar for
#define ATTACK_CB_PAUSE		5000

/// Extra z padding for assembly points
#define ASSEMBLY_POINT_Z_PADDING 10

#define	STRUCTURE_DAMAGE_SCALING	400

//production loop max
#define INFINITE_PRODUCTION	 9//10

/*This should correspond to the structLimits! */
#define	MAX_FACTORY			5

//used to flag when the Factory is ready to start building
#define ACTION_START_TIME	0

extern std::vector<ProductionRun> asProductionRun[NUM_FACTORY_TYPES];

//Value is stored for easy access to this structure stat
extern UDWORD	factoryModuleStat;
extern UDWORD	powerModuleStat;
extern UDWORD	researchModuleStat;

// the structure that was last hit
extern Structure *psLastStructHit;

//stores which player the production list has been set up for
extern SBYTE         productionPlayer;

//holder for all StructureStats
extern StructureStats *asStructureStats;
extern UDWORD				numStructureStats;

//used to hold the modifiers cross refd by weapon effect and structureStrength
extern STRUCTSTRENGTH_MODIFIER		asStructStrengthModifier[WE_NUMEFFECTS][NUM_STRUCT_STRENGTH];

void handleAbandonedStructures();

int getMaxDroids(UDWORD player);
int getMaxCommanders(UDWORD player);
int getMaxConstructors(UDWORD player);
void setMaxDroids(UDWORD player, int value);
void setMaxCommanders(UDWORD player, int value);
void setMaxConstructors(UDWORD player, int value);

bool structureExists(int player, STRUCTURE_TYPE type, bool built, bool isMission);

bool IsPlayerDroidLimitReached(int player);

bool loadStructureStats(WzConfig &ini);
/*Load the Structure Strength Modifiers from the file exported from Access*/
bool loadStructureStrengthModifiers(WzConfig &ini);

bool structureStatsShutDown();

int requestOpenGate(Structure *psStructure);
int gateCurrentOpenHeight(const Structure *psStructure, uint32_t time, int minimumStub);  ///< Returns how far open the gate is, or 0 if the structure is not a gate.

int32_t structureDamage(Structure *psStructure, unsigned damage, WEAPON_CLASS weaponClass, WEAPON_SUBCLASS weaponSubClass, unsigned impactTime, bool isDamagePerSecond, int minDamage);
void structureBuild(Structure *psStructure, Droid *psDroid, int buildPoints, int buildRate = 1);
void structureDemolish(Structure *psStructure, Droid *psDroid, int buildPoints);
void structureRepair(Structure *psStruct, Droid *psDroid, int buildRate);
/* Set the type of droid for a factory to build */
bool structSetManufacture(Structure *psStruct, DroidStats *psTempl, QUEUE_MODE mode);
uint32_t structureBuildPointsToCompletion(const Structure & structure);
float structureCompletionProgress(const Structure & structure);

//builds a specified structure at a given location
Structure *buildStructure(StructureStats *pStructureType, UDWORD x, UDWORD y, UDWORD player, bool FromSave);
Structure *buildStructureDir(StructureStats *pStructureType, UDWORD x, UDWORD y, uint16_t direction, UDWORD player, bool FromSave);
/// Create a blueprint structure, with just enough information to render it
Structure *buildBlueprint(StructureStats const *psStats, Vector3i xy, uint16_t direction, unsigned moduleIndex, STRUCT_STATES state, uint8_t ownerPlayer);
/* The main update routine for all Structures */
void structureUpdate(Structure *psBuilding, bool bMission);

/* Remove a structure and free it's memory */
bool destroyStruct(Structure *psDel, unsigned impactTime);

// remove a structure from a game without any visible effects
// bDestroy = true if the object is to be destroyed
// (for example used to change the type of wall at a location)
bool removeStruct(Structure *psDel, bool bDestroy);

//fills the list with Structures that can be built
std::vector<StructureStats *> fillStructureList(UDWORD selectedPlayer, UDWORD limit, bool showFavorites);

/// Checks if the two structures would be too close to build together.
bool isBlueprintTooClose(StructureStats const *stats1, Vector2i pos1, uint16_t dir1, StructureStats const *stats2, Vector2i pos2, uint16_t dir2);

/// Checks that the location is valid to build on.
/// pos in world coords
bool validLocation(StatsObject *psStats, Vector2i pos, uint16_t direction, unsigned player, bool bCheckBuildQueue);

bool isWall(STRUCTURE_TYPE type);                                    ///< Structure is a wall. Not completely sure it handles all cases.
bool isBuildableOnWalls(STRUCTURE_TYPE type);                        ///< Structure can be built on walls. Not completely sure it handles all cases.

void alignStructure(Structure *psBuilding);

/* set the current number of structures of each type built */
void setCurrentStructQuantity(bool displayError);
/* get a stat inc based on the name */
int32_t getStructStatFromName(const WzString &name);
/*check to see if the structure is 'doing' anything  - return true if idle*/
bool  structureIdle(const Structure *psBuilding);
/*sets the point new droids go to - x/y in world coords for a Factory*/
void setAssemblyPoint(FLAG_POSITION *psAssemblyPoint, UDWORD x, UDWORD y, UDWORD player, bool bCheck);

/*initialises the flag before a new data set is loaded up*/
void initFactoryNumFlag();

//called at start of missions
void resetFactoryNumFlag();

/* get demolish stat */
StructureStats *structGetDemolishStat();

/*find a location near to the factory to start the droid of*/
bool placeDroid(Structure *psStructure, UDWORD *droidX, UDWORD *droidY);

//Set the factory secondary orders to a droid
void setFactorySecondaryState(Droid *psDroid, Structure *psStructure);

/* is this a lassat structure? */
static inline bool isLasSat(StructureStats *pStructureType)
{
	ASSERT_OR_RETURN(false, pStructureType != nullptr, "LasSat is invalid?");

	return (pStructureType->psWeapStat[0]
	        && pStructureType->psWeapStat[0]->weaponSubClass == WSC_LAS_SAT);
}

/*sets the flag to indicate a SatUplink Exists - so draw everything!*/
void setSatUplinkExists(bool state, UDWORD player);

/*returns the status of the flag*/
bool getSatUplinkExists(UDWORD player);

/*sets the flag to indicate a Las Sat Exists - ONLY EVER WANT ONE*/
void setLasSatExists(bool state, UDWORD player);

/* added int weapon_slot to fix the alway slot 0 hack */
bool calcStructureMuzzleLocation(const Structure *psStructure, Vector3i *muzzle, int weapon_slot);
bool calcStructureMuzzleBaseLocation(const Structure *psStructure, Vector3i *muzzle, int weapon_slot);

/*this is called whenever a structure has finished building*/
void buildingComplete(Structure *psBuilding);

// these functions are used in game.c inplace of  building complete
void checkForResExtractors(Structure *psPowerGen);
void checkForPowerGen(Structure *psPowerGen);

uint16_t countPlayerUnusedDerricks();

// Set the command droid that factory production should go to struct _command_droid;
void assignFactoryCommandDroid(Structure *psStruct, class Droid *psCommander);

// remove all factories from a command droid
void clearCommandDroidFactory(Droid *psDroid);

/*for a given structure, return a pointer to its module stat */
StructureStats *getModuleStat(const Structure *psStruct);

/*called when a Res extractor is destroyed or runs out of power or is disconnected
adjusts the owning Power Gen so that it can link to a different Res Extractor if one
is available*/
void releaseResExtractor(Structure *psRelease);

/*called when a Power Gen is destroyed or is disconnected
adjusts the associated Res Extractors so that they can link to different Power
Gens if any are available*/
void releasePowerGen(Structure *psRelease);

//print some info at the top of the screen dependent on the structure
void printStructureInfo(Structure *psStructure);

/*Checks the template type against the factory type - returns false
if not a good combination!*/
bool validTemplateForFactory(const DroidStats *psTemplate, Structure *psFactory, bool complain);

/*calculates the damage caused to the resistance levels of structures*/
bool electronicDamage(GameObject *psTarget, UDWORD damage, UBYTE attackPlayer);

/* EW works differently in multiplayer mode compared with single player.*/
bool validStructResistance(const Structure *psStruct);

/*checks to see if a specific structure type exists -as opposed to a structure
stat type*/
bool checkSpecificStructExists(UDWORD structInc, UDWORD player);

int32_t getStructureDamage(const Structure *psStructure);

unsigned structureBodyBuilt(const Structure
        *psStruct);  ///< Returns the maximum body points of a structure with the current number of build points.
UDWORD structureBody(const Structure *psStruct);
UDWORD structureResistance(const StructureStats *psStats, UBYTE player);

void hqReward(UBYTE losingPlayer, UBYTE rewardPlayer);

// Is a structure a factory of somekind?
bool StructIsFactory(const Structure *Struct);

// Is a flag a factory delivery point?
bool FlagIsFactory(const FLAG_POSITION *psCurrFlag);

// Find a factories corresonding delivery point.
FLAG_POSITION *FindFactoryDelivery(const Structure *Struct);

//Find the factory associated with the delivery point - returns NULL if none exist
Structure *findDeliveryFactory(FLAG_POSITION *psDelPoint);

/*this is called when a factory produces a droid. The Template returned is the next
one to build - if any*/
DroidStats *factoryProdUpdate(Structure *psStructure, DroidStats *psTemplate);

//increment the production run for this type
void factoryProdAdjust(Structure *psStructure, DroidStats *psTemplate, bool add);

//returns the quantity of a specific template in the production list
ProductionRunEntry getProduction(Structure *psStructure,
                                 DroidStats *psTemplate);

//looks through a players production list to see if a command droid is being built
UBYTE checkProductionForCommand(UBYTE player);

//check that delivery points haven't been put down in invalid location
void checkDeliveryPoints(UDWORD version);

//adjust the loop quantity for this factory
void factoryLoopAdjust(Structure *psStruct, bool add);

/*cancels the production run for the factory and returns any power that was
accrued but not used*/
void cancelProduction(Structure *psBuilding, QUEUE_MODE mode, bool mayClearProductionRun = true);

/*set a factory's production run to hold*/
void holdProduction(Structure *psBuilding, QUEUE_MODE mode);

/*release a factory's production run from hold*/
void releaseProduction(Structure *psBuilding, QUEUE_MODE mode);

/// Does the next item in the production list.
void doNextProduction(Structure *psStructure, DroidStats *current, QUEUE_MODE mode);

// Count number of factories assignable to a command droid.
UWORD countAssignableFactories(UBYTE player, UWORD FactoryType);

/*Used for determining how much of the structure to draw as being built or demolished*/
float structHeightScale(const Structure *psStruct);

/*compares the structure sensor type with the droid weapon type to see if the
FIRE_SUPPORT order can be assigned*/
bool structSensorDroidWeapon(const Structure *psStruct, const Droid *psDroid);

/*checks if the structure has a Counter Battery sensor attached - returns
true if it has*/
bool structCBSensor(const Structure *psStruct);
/*checks if the structure has a Standard Turret sensor attached - returns
true if it has*/
bool structStandardSensor(const Structure *psStruct);

/*checks if the structure has a VTOL Intercept sensor attached - returns
true if it has*/
bool structVTOLSensor(const Structure *psStruct);

/*checks if the structure has a VTOL Counter Battery sensor attached - returns
true if it has*/
bool structVTOLCBSensor(const Structure *psStruct);

// return the nearest rearm pad
// psTarget can be NULL
Structure *findNearestReArmPad(Droid *psDroid, Structure *psTarget, bool bClear);

// check whether a rearm pad is clear
bool clearRearmPad(const Structure *psStruct);

// clear a rearm pad for a vtol to land on it
void ensureRearmPadClear(Structure *psStruct, Droid *psDroid);

// return whether a rearm pad has a vtol on it
bool vtolOnRearmPad(Structure *psStruct, Droid *psDroid);

/* Just returns true if the structure's present body points aren't as high as the original*/
bool	structIsDamaged(Structure *psStruct);

// give a structure from one player to another - used in Electronic Warfare
Structure *giftSingleStructure(Structure *psStructure, UBYTE attackPlayer, bool electronic_warfare = true);

/*Initialise the production list and set up the production player*/
void changeProductionPlayer(UBYTE player);

// La!
bool IsStatExpansionModule(const StructureStats *psStats);

/// is this a blueprint and not a real structure?
bool structureIsBlueprint(const Structure *psStructure);
bool isBlueprint(const GameObject *psObject);

/*returns the power cost to build this structure, or to add its next module */
UDWORD structPowerToBuildOrAddNextModule(const Structure *psStruct);

// check whether a factory of a certain number and type exists
bool checkFactoryExists(UDWORD player, UDWORD factoryType, UDWORD inc);

/*checks the structure passed in is a Las Sat structure which is currently
selected - returns true if valid*/
bool lasSatStructSelected(const Structure *psStruct);

void cbNewDroid(Structure *psFactory, Droid *psDroid);

StructureBounds getStructureBounds(const Structure *object);
StructureBounds getStructureBounds(const StructureStats *stats, Vector2i pos, uint16_t direction);

bool canStructureHaveAModuleAdded(const Structure * const structure);

static inline int structSensorRange(const Structure *psObj)
{
	return objSensorRange((const GameObject *)psObj);
}

static inline int structJammerPower(const Structure *psObj)
{
	return objJammerPower((const GameObject *)psObj);
}

static inline Rotation structureGetInterpolatedWeaponRotation(Structure *psStructure, int weaponSlot, uint32_t time)
{
	return interpolateRot(psStructure->weaponList[weaponSlot].prevRot, psStructure->weaponList[weaponSlot].rot, psStructure->prevTime, psStructure->time, time);
}

#define setStructureTarget(_psBuilding, _psNewTarget, _idx, _targetOrigin) _setStructureTarget(_psBuilding, _psNewTarget, _idx, _targetOrigin, __LINE__, __FUNCTION__)
static inline void _setStructureTarget(Structure *psBuilding,
                                       GameObject *psNewTarget, UWORD idx, TARGET_ORIGIN targetOrigin, int line, const char *func)
{
	ASSERT_OR_RETURN(, idx < MAX_WEAPONS, "Bad index");
	ASSERT_OR_RETURN(, psNewTarget == nullptr || !psNewTarget->deathTime, "setStructureTarget set dead target");
	psBuilding->psTarget[idx] = psNewTarget;
	psBuilding->weaponList[idx].origin = targetOrigin;
#ifdef DEBUG
	psBuilding->targetLine[idx] = line;
	sstrcpy(psBuilding->targetFunc[idx], func);
#else
	// Prevent warnings about unused parameters
	(void)line;
	(void)func;
#endif
}

// Functions for the GUI to know what's pending, before it's synchronised.
template<typename Functionality, typename Subject>
static inline void setStatusPendingStart(Functionality &functionality, Subject *subject)
{
	functionality.psSubjectPending = subject;
	functionality.statusPending = FACTORY_START_PENDING;
	++functionality.pendingCount;
}

template<typename Functionality>
static inline void setStatusPendingCancel(Functionality &functionality)
{
	functionality.psSubjectPending = nullptr;
	functionality.statusPending = FACTORY_CANCEL_PENDING;
	++functionality.pendingCount;
}

template<typename Functionality>
static inline void setStatusPendingHold(Functionality &functionality)
{
	if (functionality.psSubjectPending == nullptr)
	{
		functionality.psSubjectPending = functionality.psSubject;
	}
	functionality.statusPending = FACTORY_HOLD_PENDING;
	++functionality.pendingCount;
}

template<typename Functionality>
static inline void setStatusPendingRelease(Functionality &functionality)
{
	if (functionality.psSubjectPending == nullptr && functionality.statusPending != FACTORY_CANCEL_PENDING)
	{
		functionality.psSubjectPending = functionality.psSubject;
	}
	if (functionality.psSubjectPending != nullptr)
	{
		functionality.statusPending = FACTORY_START_PENDING;
	}
	++functionality.pendingCount;
}

template<typename Functionality>
static inline void popStatusPending(Functionality &functionality)
{
	if (functionality.pendingCount == 0)
	{
		++functionality.pendingCount;
	}
	if (--functionality.pendingCount == 0)
	{
		// Subject is now synchronised, remove pending.
		functionality.psSubjectPending = nullptr;
		functionality.statusPending = FACTORY_NOTHING_PENDING;
	}
}

void checkStructure(const Structure *psStructure, const char *const location_description, const char *function, const int recurse);

#define CHECK_STRUCTURE(object) checkStructure((object), AT_MACRO, __FUNCTION__, max_check_object_recursion)

void structureInitVars();
void initStructLimits();

#define syncDebugStructure(psStruct, ch) _syncDebugStructure(__FUNCTION__, psStruct, ch)
void _syncDebugStructure(const char *function, Structure const *psStruct, char ch);


// True iff object is a structure.
static inline bool isStructure(GameObject const *psObject)
{
	return psObject != nullptr && psObject->type == OBJ_STRUCTURE;
}
// Returns STRUCTURE * if structure or NULL if not.
static inline Structure *castStructure(GameObject *psObject)
{
	return isStructure(psObject) ? (Structure *)psObject : (Structure *)nullptr;
}
// Returns STRUCTURE const * if structure or NULL if not.
static inline Structure const *castStructure(GameObject const *psObject)
{
	return isStructure(psObject) ? (Structure const *)psObject : (Structure const *)nullptr;
}

static inline int getBuildingResearchPoints(Structure *psStruct)
{
	auto &upgrade = psStruct->stats->upgrade[psStruct->owningPlayer];
	return upgrade.research + upgrade.moduleResearch * psStruct->capacity;
}

static inline int getBuildingProductionPoints(Structure *psStruct)
{
	auto &upgrade = psStruct->stats->upgrade[psStruct->owningPlayer];
	return upgrade.production + upgrade.moduleProduction * psStruct->capacity;
}

static inline int getBuildingPowerPoints(Structure *psStruct)
{
	auto &upgrade = psStruct->stats->upgrade[psStruct->owningPlayer];
	return upgrade.power + upgrade.modulePower * psStruct->capacity;
}

static inline int getBuildingRepairPoints(Structure *psStruct)
{
	return psStruct->stats->upgrade[psStruct->owningPlayer].repair;
}

static inline int getBuildingRearmPoints(Structure *psStruct)
{
	return psStruct->stats->upgrade[psStruct->owningPlayer].rearm;
}

WzString getFavoriteStructs();
void setFavoriteStructs(WzString list);

struct LineBuild
{
	Vector2i back() const { return (*this)[count - 1]; }
	Vector2i operator [](int i) const { return begin + i*step; }

	Vector2i begin = {0, 0};
	Vector2i step = {0, 0};
	int count = 0;
};

LineBuild calcLineBuild(Vector2i size, STRUCTURE_TYPE type, Vector2i pos, Vector2i pos2);
LineBuild calcLineBuild(StructureStats const *stats, uint16_t direction, Vector2i pos, Vector2i pos2);

#endif // __INCLUDED_SRC_STRUCTURE_H__