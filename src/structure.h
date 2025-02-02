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
 * @file structure.h
 * Definitions for the structures
 */

#ifndef __INCLUDED_SRC_STRUCTURE_H__
#define __INCLUDED_SRC_STRUCTURE_H__

#include <memory>
#include <vector>

#include "lib/framework/frame.h"
#include "lib/framework/vector.h"
#include "lib/framework/wzconfig.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/ivisdef.h"

#include "basedef.h"
#include "droid.h"
#include "group.h"
#include "order.h"
#include "positiondef.h"
#include "stats.h"
#include "visibility.h"
#include "weapon.h"
#include "baseobject.h"

static constexpr auto NUM_FACTORY_MODULES = 2;
static constexpr auto NUM_POWER_MODULES = 4;

/// Used to indicate any kind of building when calling intGotoNextStructureType()
static constexpr auto NY = 255;

/// Highest number the limit can be set to
static constexpr auto LOTS_OF = 0xFFFFFFFF;

/// This structure must be built side by side with another of the same player
static constexpr auto STRUCTURE_CONNECTED = 0x0001;

static constexpr auto SAS_OPEN_SPEED = GAME_TICKS_PER_SEC;
static constexpr auto SAS_STAY_OPEN_TIME = GAME_TICKS_PER_SEC * 6;

/// Maximum slope of the terrain for building a structure
static constexpr auto MAX_INCLINE	= 50;

/* Droid construction smoke cloud constants */

static constexpr auto DROID_CONSTRUCTION_SMOKE_OFFSET	= 30;
static constexpr auto DROID_CONSTRUCTION_SMOKE_HEIGHT	= 20;

/// How often to increase the resistance level of a structure
static constexpr auto RESISTANCE_INTERVAL = 2000;

/// How long to wait between CALL_STRUCT_ATTACKED's - plus how long to flash on radar for
static constexpr auto ATTACK_CB_PAUSE	= 5000;

/// Extra z padding for assembly points
static constexpr auto ASSEMBLY_POINT_Z_PADDING = 10;

static constexpr auto STRUCTURE_DAMAGE_SCALING = 400;

/// Production loop max
static constexpr auto INFINITE_PRODUCTION = 9;

/// This should correspond to the structLimits!
static constexpr auto MAX_FACTORY	=	5;

/// Used to flag when the Factory is ready to start building
static constexpr auto ACTION_START_TIME = 0;


enum WallOrientation
{
  WallConnectNone = 0,
  WallConnectLeft = 1,
  WallConnectRight = 2,
  WallConnectUp = 4,
  WallConnectDown = 8,
};

enum class PENDING_STATUS
{
	NOTHING_PENDING,
	START_PENDING,
	HOLD_PENDING,
	CANCEL_PENDING
};

enum class STRUCTURE_TYPE
{
  ANY,
  HQ,
  FACTORY,
  FACTORY_MODULE,
  POWER_GEN,
  POWER_MODULE,
  RESOURCE_EXTRACTOR,
  DEFENSE,
  WALL,
  WALL_CORNER,
  GENERIC,
  RESEARCH,
  RESEARCH_MODULE,
  REPAIR_FACILITY,
  COMMAND_CONTROL,
  BRIDGE,
  DEMOLISH,
  CYBORG_FACTORY,
  VTOL_FACTORY,
  LAB,
  REARM_PAD,
  MISSILE_SILO,
  SAT_UPLINK,
  GATE,
  LASSAT,
  COUNT
};

enum class STRUCTURE_STATE
{
	BEING_BUILT,
	BUILT,
	BLUEPRINT_VALID,
	BLUEPRINT_INVALID,
	BLUEPRINT_PLANNED,
	BLUEPRINT_PLANNED_BY_ALLY
};

enum class STRUCTURE_STRENGTH
{
  SOFT,
  MEDIUM,
  HARD,
  BUNKER,
  COUNT // MUST BE LAST
};

typedef UWORD STRUCTSTRENGTH_MODIFIER;

enum class STRUCTURE_ANIMATION_STATE
{
  NORMAL,
  OPEN,
  OPENING,
  CLOSING
};

enum FLAG_TYPE
{
  FACTORY_FLAG,
  CYBORG_FLAG,
  VTOL_FLAG,
  REPAIR_FLAG,
  //separate the numfactory from numflag
  NUM_FLAG_TYPES,
  NUM_FACTORY_TYPES = REPAIR_FLAG
};

struct FlagPosition : public ObjectPosition
{
  Vector3i coords {0, 0, 0};
  uint8_t factoryInc = 0;
  uint8_t factoryType = 0;
};

struct StructureBounds
{
  StructureBounds();
  StructureBounds(Vector2i top_left_coords, Vector2i size_in_coords);

  [[nodiscard]] bool isValid() const;

  Vector2i map {0, 0};
  Vector2i size {0, 0};
};

struct StructureStats : public BaseStats
{
  [[nodiscard]] Vector2i size(unsigned direction) const;
  [[nodiscard]] bool is_expansion_module() const noexcept;

  using enum STRUCTURE_TYPE;
  STRUCTURE_TYPE type; /* the type of structure */
  STRUCTURE_STRENGTH strength; /* strength against the weapon effects */
  unsigned base_width; /*The width of the base in tiles*/
  unsigned base_breadth; /*The breadth of the base in tiles*/
  unsigned build_point_cost; /*The number of build points required to build the structure*/
  unsigned height; /*The height above/below the terrain - negative values denote below the terrain*/
  unsigned power_cost; /*How much power the structure requires to build*/
  std::vector< std::unique_ptr<iIMDShape> > IMDs; // The IMDs to draw for this structure, for each possible number of modules.
  std::shared_ptr<iIMDShape> base_imd; /*The base IMD to draw for this structure */
  std::shared_ptr<EcmStats> ecm_stats; /*Which ECM is standard for the structure -if any*/
  std::shared_ptr<SensorStats> sensor_stats;/*Which Sensor is standard for the structure -if any*/
  unsigned weapon_slots; /*Number of weapons that can be attached to the building*/
  unsigned numWeaps; /*Number of weapons for default */
  std::array<WeaponStats*, MAX_WEAPONS> psWeapStat;
  unsigned long flags;
  bool combines_with_wall; //If the structure will trigger nearby walls to try combining with it

  unsigned minLimit; ///< lowest value user can set limit to (currently unused)
  unsigned maxLimit; ///< highest value user can set limit to, LOTS_OF = no limit
  unsigned curCount[MAX_PLAYERS]; ///< current number of instances of this type

  struct
  {
      unsigned research;
      unsigned moduleResearch;
      unsigned repair;
      unsigned power;
      unsigned modulePower;
      unsigned production;
      unsigned moduleProduction;
      unsigned rearm;
      unsigned armour;
      unsigned thermal;
      unsigned hitPoints;
      unsigned resistance; // resist enemy takeover; 0 = immune
      unsigned limit; // current max limit for this type, LOTS_OF = no limit
  } upgraded_stats[MAX_PLAYERS], base;

  bool is_favourite = false; ///< on Favorites list
};

struct WALL
{
    /// Type of wall --
    /// 0 = ─, 1 = ┼, 2 = ┴, 3 = ┘
    unsigned type;
};

class Structure : public ConstructedObject
{
public:
  ~Structure() override;

  Structure(unsigned id, Player* player);

  Structure(Structure const& rhs);
  Structure& operator=(Structure const& rhs);

  Structure(Structure&& rhs) noexcept = default;
  Structure& operator=(Structure&& rhs) noexcept = default;

  [[nodiscard]] virtual bool isIdle() const;

  [[nodiscard]] unsigned getCurrentBuildPoints() const;
  [[nodiscard]] STRUCTURE_ANIMATION_STATE getAnimationState() const;
  [[nodiscard]] unsigned getArmourValue(WEAPON_CLASS weaponClass) const;
  [[nodiscard]] Vector2i getSize() const;
  [[nodiscard]] unsigned getLastResistance() const;
  [[nodiscard]] int getFoundationDepth() const noexcept;
  [[nodiscard]] iIMDShape const* getImdShape() const override;
  [[nodiscard]] BaseObject const* getTarget(int weapon_slot) const override;
  [[nodiscard]] STRUCTURE_STATE getState() const;
  [[nodiscard]] StructureStats const* getStats() const;
  [[nodiscard]] uint8_t getCapacity() const;
  [[nodiscard]] int objRadius() const override;
  [[nodiscard]] bool isBlueprint() const noexcept;
  [[nodiscard]] bool isWall() const noexcept;
  [[nodiscard]] bool isRadarDetector() const override;
  [[nodiscard]] bool hasModules() const noexcept;
  [[nodiscard]] bool hasSensor() const;
  [[nodiscard]] bool hasStandardSensor() const override;
  [[nodiscard]] bool hasCbSensor() const override;
  [[nodiscard]] bool hasVtolInterceptSensor() const override;
  [[nodiscard]] bool hasVtolCbSensor() const override;
  [[nodiscard]] bool smokeWhenDamaged() const noexcept;
  void updateExpectedDamage(unsigned damage, bool is_direct) noexcept;
  [[nodiscard]] unsigned calculateSensorRange() const override;
  [[nodiscard]] int calculateGateHeight(unsigned time, int minimum) const;
  void setFoundationDepth(int depth) noexcept;
  void printInfo() const;
  [[nodiscard]] unsigned buildPointsToCompletion() const;
  [[nodiscard]] unsigned calculateRefundedPower() const;
  [[nodiscard]] int calculateAttackPriority(BaseObject const* target, int weapon_slot) const;
  Structure* giftSingleStructure(unsigned attackPlayer, bool electronic_warfare);
  [[nodiscard]] float structureCompletionProgress() const;
  void aiUpdateStructure(bool isMission);
  void structureUpdate(bool bMission);
  int requestOpenGate();
  bool loadSaveStructure(char* pFileData, unsigned filesize);
  bool loadSaveStructure2(const char* pFileName, Structure** ppList);
  void actionDroidTarget(Droid* droid, ACTION action, int idx);
  void setTarget(BaseObject* psNewTarget, unsigned idx, TARGET_ORIGIN targetOrigin);
  void structureBuild(Droid* psDroid, int builtPoints, int buildRate_);
  [[nodiscard]] std::unique_ptr<Structure> buildStructureDir(
        StructureStats* pStructureType, unsigned x, unsigned y,
        uint16_t direction, unsigned player, bool FromSave);
private:
  struct Impl;
  std::unique_ptr<Impl> pimpl;
};

StructureBounds getBounds(Structure const& structure) noexcept;

struct ResearchItem
{
  uint8_t tech_code;
  unsigned research_point_cost;
  unsigned power_cost;
};

class ResearchFacility : public Structure
{
public:
  ~ResearchFacility() override = default;
  ResearchFacility(unsigned id, Player* player);

  ResearchFacility(ResearchFacility const& rhs);
  ResearchFacility& operator=(ResearchFacility const& rhs);

  ResearchFacility(ResearchFacility&& rhs) noexcept = default;
  ResearchFacility& operator=(ResearchFacility&& rhs) noexcept = default;

  [[nodiscard]] const ResearchItem* getSubject() const;
  [[nodiscard]] bool isIdle() const override;
private:
  struct Impl;
  std::unique_ptr<Impl> pimpl;
};

class ProductionRun
{
public:
  ProductionRun();

  ProductionRun(ProductionRun const& rhs);
  ProductionRun& operator=(ProductionRun const& rhs);

  ProductionRun(ProductionRun&& rhs) noexcept = default;
  ProductionRun& operator=(ProductionRun&& rhs) noexcept = default;

  bool operator ==(DroidTemplate const& rhs) const;

  void restart();
  [[nodiscard]] bool isValid() const;
  [[nodiscard]] bool isComplete() const;
  [[nodiscard]] int tasksRemaining() const;
private:
  struct Impl;
  std::unique_ptr<Impl> pimpl;
};

class Factory : public Structure
{
public:
  ~Factory() override = default;
  Factory(unsigned id, Player* player);

  Factory(Factory const& rhs);
  Factory& operator=(Factory const& rhs);

  Factory(Factory&& rhs) noexcept = default;
  Factory& operator=(Factory&& rhs) noexcept = default;

  [[nodiscard]] bool isIdle() const override;
  [[nodiscard]] DroidTemplate const* getSubject() const;
  [[nodiscard]] FlagPosition const* getAssemblyPoint() const;
  [[nodiscard]] Droid const* getCommander() const;
  void aiUpdate();
  bool structSetManufacture(DroidTemplate* psTempl);
  void refundBuildPower();
  void releaseProduction();
  void holdProduction();
  void assignFactoryCommandDroid(Droid* commander);
  bool setFactoryState(SECONDARY_ORDER sec, SECONDARY_STATE state);
  bool getFactoryState(SECONDARY_ORDER sec, SECONDARY_STATE* pState);
  bool structPlaceDroid(DroidTemplate* psTempl, Droid** ppsDroid);
  [[nodiscard]] bool IsFactoryCommanderGroupFull();
  bool checkHaltOnMaxUnitsReached(bool isMission);
  ProductionRun getProduction(DroidTemplate *psTemplate);
  void factoryLoopAdjust(bool add);
  [[nodiscard]] FlagPosition* FindFactoryDelivery() const;
  void cancelProduction(bool mayClearProductionRun);
  DroidTemplate* factoryProdUpdate(DroidTemplate* psTemplate);
private:
  struct Impl;
  std::unique_ptr<Impl> pimpl;
};

class PowerGenerator;

class ResourceExtractor : public Structure
{
public:
  ~ResourceExtractor() override = default;
  ResourceExtractor(unsigned id, Player* player);

  ResourceExtractor(ResourceExtractor const& rhs);
  ResourceExtractor& operator=(ResourceExtractor const& rhs);

  ResourceExtractor(ResourceExtractor&& rhs) noexcept = default;
  ResourceExtractor& operator=(ResourceExtractor&& rhs) noexcept = default;

  [[nodiscard]] PowerGenerator const* getPowerGen() const;
  void setPowerGen(PowerGenerator* gen);
  void releaseResExtractor();
  void informPowerGen();
  void checkForPowerGen();
private:
  struct Impl;
  std::unique_ptr<Impl> pimpl;
};

class PowerGenerator : public Structure
{
public:
  ~PowerGenerator() override = default;
  PowerGenerator(unsigned id, Player* player);

  PowerGenerator(PowerGenerator const& rhs);
  PowerGenerator& operator=(PowerGenerator const& rhs);

  PowerGenerator(PowerGenerator&& rhs) noexcept = default;
  PowerGenerator& operator=(PowerGenerator&& rhs) noexcept = default;

  [[nodiscard]] ResourceExtractor const* getExtractor(int idx) const;
  void setExtractor(int idx, ResourceExtractor* extractor);
  void releasePowerGen();
private:
  struct Impl;
  std::unique_ptr<Impl> pimpl;
};

class RepairFacility : public Structure
{
public:
  ~RepairFacility() override = default;
  RepairFacility(unsigned id, Player* player);

  RepairFacility(RepairFacility const& rhs);
  RepairFacility& operator=(RepairFacility const& rhs);

  RepairFacility(RepairFacility&& rhs) noexcept = default;
  RepairFacility& operator=(RepairFacility&& rhs) noexcept = default;

  [[nodiscard]] FlagPosition const* getDeliveryPoint() const;
  void aiUpdate();
private:
  struct Impl;
  std::unique_ptr<Impl> pimpl;
};

class RearmPad : public Structure
{
public:
  ~RearmPad() override = default;
  RearmPad(unsigned id, Player* player);

  RearmPad(RearmPad const& rhs);
  RearmPad& operator=(RearmPad const& rhs);

  RearmPad(RearmPad&& rhs) noexcept = default;
  RearmPad& operator=(RearmPad&& rhs) noexcept = default;

  [[nodiscard]] bool isClear() const;
  void aiUpdate();
private:
  struct Impl;
  std::unique_ptr<Impl> pimpl;
};

//this is used for module graphics - factory and vtol factory
static const int NUM_FACMOD_TYPES = 2;

extern std::vector<ProductionRun> asProductionRun[NUM_FACTORY_TYPES];

struct UPGRADE_MOD
{
    UWORD modifier; //% to increase the stat by
};

typedef UPGRADE_MOD REPAIR_FACILITY_UPGRADE;
typedef UPGRADE_MOD POWER_UPGRADE;
typedef UPGRADE_MOD REARM_UPGRADE;


//Value is stored for easy access to this structure stat
extern unsigned factoryModuleStat;
extern unsigned powerModuleStat;
extern unsigned researchModuleStat;

// the structure that was last hit
extern Structure* psLastStructHit;

//stores which player the production list has been set up for
extern unsigned productionPlayer;

void handleAbandonedStructures();

int getMaxDroids(unsigned player);
int getMaxCommanders(unsigned player);
int getMaxConstructors(unsigned player);
void setMaxDroids(unsigned player, int value);
void setMaxCommanders(unsigned player, int value);
void setMaxConstructors(unsigned player, int value);

bool structureExists(unsigned player, STRUCTURE_TYPE type, bool built, bool isMission);

bool IsPlayerDroidLimitReached(unsigned player);

bool loadStructureStats(WzConfig& ini);
/*Load the Structure Strength Modifiers from the file exported from Access*/
bool loadStructureStrengthModifiers(WzConfig& ini);

bool structureStatsShutDown();

int requestOpenGate(Structure* psStructure);
int gateCurrentOpenHeight(const Structure* psStructure, uint32_t time, int minimumStub);
///< Returns how far open the gate is, or 0 if the structure is not a gate.

int structureDamage(Structure* psStructure, unsigned damage, WEAPON_CLASS weaponClass,
                        WEAPON_SUBCLASS weaponSubClass, unsigned impactTime, bool isDamagePerSecond, int minDamage);
void structureBuild(Structure* psStructure, Droid* psDroid, int buildPoints, int buildRate = 1);
void structureDemolish(Structure* psStructure, Droid* psDroid, int buildPoints);
void structureRepair(Structure* psStruct, int buildRate);
/* Set the type of droid for a factory to build */
bool structSetManufacture(Structure* psStruct, DroidTemplate* psTempl);
unsigned structureBuildPointsToCompletion(const Structure& structure);
float structureCompletionProgress(const Structure& structure);

//builds a specified structure at a given location
Structure* buildStructure(StructureStats* pStructureType, unsigned x, unsigned y, unsigned player, bool FromSave);
Structure* buildStructureDir(StructureStats* pStructureType, unsigned x, unsigned y, uint16_t direction, unsigned player,
                             bool FromSave);

/// Create a blueprint structure, with just enough information to render it
Structure* buildBlueprint(StructureStats const* psStats, Vector3i xy,
                          uint16_t direction, unsigned moduleIndex,
                          STRUCTURE_STATE state, uint8_t ownerPlayer);

/* Remove a structure and free it's memory */
bool destroyStruct(Structure* psDel, unsigned impactTime);

// remove a structure from a game without any visible effects
// bDestroy = true if the object is to be destroyed
// (for example used to change the type of wall at a location)
bool removeStruct(Structure* psDel, bool bDestroy);

//fills the list with Structures that can be built
std::vector<StructureStats*> fillStructureList(unsigned selectedPlayer, unsigned limit, bool showFavorites);

/// Checks if the two structures would be too close to build together.
bool isBlueprintTooClose(StructureStats const* stats1, Vector2i pos1, uint16_t dir1, StructureStats const* stats2,
                         Vector2i pos2, uint16_t dir2);

/// Checks that the location is valid to build on.
/// pos in world coords
bool validLocation(BaseStats* psStats, Vector2i pos, uint16_t direction, unsigned player, bool bCheckBuildQueue);

bool isWall(STRUCTURE_TYPE type); ///< Structure is a wall. Not completely sure it handles all cases.
bool isBuildableOnWalls(STRUCTURE_TYPE type);
///< Structure can be built on walls. Not completely sure it handles all cases.

void alignStructure(Structure* psBuilding);

/* set the current number of structures of each type built */
void setCurrentStructQuantity(bool displayError);
/* get a stat inc based on the name */
int32_t getStructStatFromName(const WzString& name);
/*check to see if the structure is 'doing' anything  - return true if idle*/
bool structureIdle(const Structure* psBuilding);
/*sets the point new droids go to - x/y in world coords for a Factory*/
void setAssemblyPoint(FlagPosition* psAssemblyPoint, unsigned x, unsigned y, unsigned player, bool bCheck);

/*initialises the flag before a new data set is loaded up*/
void initFactoryNumFlag();

//called at start of missions
void resetFactoryNumFlag();

/* get demolish stat */
StructureStats* structGetDemolishStat();

/*find a location near to the factory to start the droid of*/
bool placeDroid(Structure* psStructure, unsigned* droidX, unsigned* droidY);

//Set the factory secondary orders to a droid
void setFactorySecondaryState(Droid* psDroid, Structure* psStructure);

static float CalcStructureSmokeInterval(float damage);

/* is this a lassat structure? */
static inline bool isLasSat(StructureStats const* pStructureType)
{
	ASSERT_OR_RETURN(false, pStructureType != nullptr, "LasSat is invalid?");

	return (pStructureType->psWeapStat[0] &&
          pStructureType->psWeapStat[0]->weaponSubClass == WEAPON_SUBCLASS::LAS_SAT);
}

/*sets the flag to indicate a SatUplink Exists - so draw everything!*/
void setSatUplinkExists(bool state, unsigned player);

/*returns the status of the flag*/
bool getSatUplinkExists(unsigned player);

/*sets the flag to indicate a Las Sat Exists - ONLY EVER WANT ONE*/
void setLasSatExists(bool state, unsigned player);

/* added int weapon_slot to fix the alway slot 0 hack */
bool calcStructureMuzzleLocation(const Structure* psStructure, Vector3i* muzzle, int weapon_slot);
bool calcStructureMuzzleBaseLocation(const Structure* psStructure, Vector3i* muzzle, int weapon_slot);

/*this is called whenever a structure has finished building*/
void buildingComplete(Structure* psBuilding);

// these functions are used in game.c inplace of  building complete
void checkForResExtractors(Structure* psPowerGen);
void checkForPowerGen(Structure* psPowerGen);

uint16_t countPlayerUnusedDerricks();

// Set the command droid that factory production should go to struct _command_droid;
void assignFactoryCommandDroid(Structure* psStruct, struct Droid* psCommander);

// remove all factories from a command droid
void clearCommandDroidFactory(Droid* psDroid);

/*for a given structure, return a pointer to its module stat */
StructureStats* getModuleStat(const Structure* psStruct);

/*called when a Res extractor is destroyed or runs out of power or is disconnected
adjusts the owning Power Gen so that it can link to a different Res Extractor if one
is available*/
void releaseResExtractor(Structure* psRelease);

/*called when a Power Gen is destroyed or is disconnected
adjusts the associated Res Extractors so that they can link to different Power
Gens if any are available*/
void releasePowerGen(Structure* psRelease);

//print some info at the top of the screen dependent on the structure
void printStructureInfo(Structure* psStructure);

/*Checks the template type against the factory type - returns false
if not a good combination!*/
bool validTemplateForFactory(const DroidTemplate* psTemplate, Structure* psFactory, bool complain);

/*calculates the damage caused to the resistance levels of structures*/
bool electronicDamage(BaseObject * psTarget, unsigned damage, UBYTE attackPlayer);

/* EW works differently in multiplayer mode compared with single player.*/
bool validStructResistance(const Structure* psStruct);

/*checks to see if a specific structure type exists -as opposed to a structure
stat type*/
bool checkSpecificStructExists(unsigned structInc, unsigned player);

int32_t getStructureDamage(const Structure* psStructure);

unsigned structureBodyBuilt(const Structure* psStruct);
///< Returns the maximum body points of a structure with the current number of build points.
unsigned structureBody(const Structure* psStruct);
unsigned structureResistance(const StructureStats* psStats, UBYTE player);

void hqReward(UBYTE losingPlayer, UBYTE rewardPlayer);

// Is a structure a factory of some kind?
bool StructIsFactory(const Structure* Struct);

// Is a flag a factory delivery point?
bool FlagIsFactory(const FlagPosition* psCurrFlag);

// Find a factories corresponding delivery point.
FlagPosition* FindFactoryDelivery(const Structure* Struct);

//Find the factory associated with the delivery point - returns NULL if none exist
Structure* findDeliveryFactory(FlagPosition* psDelPoint);

/*this is called when a factory produces a droid. The Template returned is the next
one to build - if any*/
DroidTemplate* factoryProdUpdate(Structure* psStructure, DroidTemplate* psTemplate);

//increment the production run for this type
void factoryProdAdjust(Structure* psStructure, DroidTemplate* psTemplate, bool add);

//returns the quantity of a specific template in the production list
ProductionRun getProduction(Structure* psStructure, DroidTemplate* psTemplate);

//looks through a players production list to see if a command droid is being built
UBYTE checkProductionForCommand(UBYTE player);

//check that delivery points haven't been put down in invalid location
void checkDeliveryPoints(unsigned version);

//adjust the loop quantity for this factory
void factoryLoopAdjust(Structure* psStruct, bool add);

/*cancels the production run for the factory and returns any power that was
accrued but not used*/
void cancelProduction(Structure* psBuilding, bool mayClearProductionRun = true);

/*set a factory's production run to hold*/
void holdProduction(Structure* psBuilding);

/*release a factory's production run from hold*/
void releaseProduction(Structure* psBuilding);

/// Does the next item in the production list.
void doNextProduction(Structure* psStructure, DroidTemplate* current);

// Count number of factories assignable to a command droid.
UWORD countAssignableFactories(UBYTE player, UWORD FactoryType);

/*Used for determining how much of the structure to draw as being built or demolished*/
float structHeightScale(const Structure* psStruct);

/*compares the structure sensor type with the droid weapon type to see if the
FIRE_SUPPORT order can be assigned*/
bool structSensorDroidWeapon(const Structure* psStruct, const Droid* psDroid);

/*checks if the structure has a Counter Battery sensor attached - returns
true if it has*/
bool structCBSensor(const Structure* psStruct);
/*checks if the structure has a Standard Turret sensor attached - returns
true if it has*/
bool structStandardSensor(const Structure* psStruct);

/*checks if the structure has a VTOL Intercept sensor attached - returns
true if it has*/
bool structVTOLSensor(const Structure* psStruct);

/*checks if the structure has a VTOL Counter Battery sensor attached - returns
true if it has*/
bool structVTOLCBSensor(const Structure* psStruct);

// return the nearest rearm pad
// psTarget can be NULL
Structure* findNearestReArmPad(Droid* psDroid, Structure* psTarget, bool bClear);

// check whether a rearm pad is clear
bool clearRearmPad(const Structure* psStruct);

// clear a rearm pad for a vtol to land on it
void ensureRearmPadClear(Structure* psStruct, Droid* psDroid);

// return whether a rearm pad has a vtol on it
bool vtolOnRearmPad(Structure* psStruct, Droid* psDroid);

/* Just returns true if the structure's present body points aren't as high as the original*/
bool structIsDamaged(Structure* psStruct);

// give a structure from one player to another - used in Electronic Warfare
Structure* giftSingleStructure(Structure* psStructure, UBYTE attackPlayer,
                               bool electronic_warfare = true);

/*Initialise the production list and set up the production player*/
void changeProductionPlayer(unsigned player);

bool IsStatExpansionModule(const StructureStats* psStats);

/// is this a blueprint and not a real structure?
bool structureIsBlueprint(const Structure* psStructure);
bool isBlueprint(const BaseObject * psObject);

/*returns the power cost to build this structure, or to add its next module */
unsigned structPowerToBuildOrAddNextModule(const Structure* psStruct);

// check whether a factory of a certain number and type exists
bool checkFactoryExists(unsigned player, unsigned factoryType, unsigned inc);

/*checks the structure passed in is a Las Sat structure which is currently
selected - returns true if valid*/
bool lasSatStructSelected(const Structure* psStruct);

void cbNewDroid(Structure* psFactory, Droid* psDroid);

StructureBounds getStructureBounds(const Structure* object);
StructureBounds getStructureBounds(const StructureStats* stats, Vector2i pos, uint16_t direction);

bool canStructureHaveAModuleAdded(Structure const* structure);

static inline unsigned structSensorRange(const Structure* psObj)
{
	return objSensorRange(psObj);
}

static inline unsigned structJammerPower(const Structure* psObj)
{
	return objJammerPower(psObj);
}

static inline Rotation structureGetInterpolatedWeaponRotation(Structure const* psStructure, int weaponSlot, uint32_t time)
{
	return interpolateRot(psStructure->weaponManager->weapons[weaponSlot].previousRotation,
                        psStructure->weaponManager->weapons[weaponSlot].getRotation(),
                        psStructure->getPreviousLocation().time, psStructure->getTime(), time);
}

// Functions for the GUI to know what's pending, before it's synchronised.
template <typename Functionality, typename Subject>
static inline void setStatusPendingStart(Functionality& functionality, Subject* subject)
{
	functionality.psSubjectPending = subject;
	functionality.statusPending = PENDING_STATUS::START_PENDING;
	++functionality.pendingCount;
}

template <typename Functionality>
static inline void setStatusPendingCancel(Functionality& functionality)
{
	functionality.psSubjectPending = nullptr;
	functionality.statusPending = PENDING_STATUS::CANCEL_PENDING;
	++functionality.pendingCount;
}

template <typename Functionality>
static inline void setStatusPendingHold(Functionality& functionality)
{
	if (functionality.psSubjectPending == nullptr)
	{
		functionality.psSubjectPending = functionality.psSubject;
	}
	functionality.statusPending = PENDING_STATUS::HOLD_PENDING;
	++functionality.pendingCount;
}

template <typename Functionality>
static inline void setStatusPendingRelease(Functionality& functionality)
{
	if (functionality.psSubjectPending == nullptr && functionality.statusPending != PENDING_STATUS::CANCEL_PENDING)
	{
		functionality.psSubjectPending = functionality.psSubject;
	}
	if (functionality.psSubjectPending != nullptr)
	{
		functionality.statusPending = PENDING_STATUS::START_PENDING;
	}
	++functionality.pendingCount;
}

template <typename Functionality>
static inline void popStatusPending(Functionality& functionality)
{
	if (functionality.pendingCount == 0)
	{
		++functionality.pendingCount;
	}
	if (--functionality.pendingCount == 0)
	{
		// Subject is now synchronised, remove pending.
		functionality.psSubjectPending = nullptr;
		functionality.statusPending = PENDING_STATUS::NOTHING_PENDING;
	}
}

void checkStructure(const Structure* psStructure, std::string location_description,
                    std::string function, int recurse);

#define CHECK_STRUCTURE(object) checkStructure((object), AT_MACRO, __FUNCTION__, max_check_object_recursion)

void structureInitVars();
void initStructLimits();

#define syncDebugStructure(psStruct, ch) _syncDebugStructure(__FUNCTION__, psStruct, ch)
void _syncDebugStructure(const char* function, Structure const* psStruct, char ch);

static inline int getBuildingResearchPoints(Structure* psStruct)
{
	auto& upgrade = psStruct->getStats()->upgraded_stats[psStruct->playerManager->getPlayer()];
	return upgrade.research + upgrade.moduleResearch * psStruct->getCapacity();
}

static inline int getBuildingProductionPoints(Structure* psStruct)
{
	auto& upgrade = psStruct->getStats()->upgraded_stats[psStruct->playerManager->getPlayer()];
	return upgrade.production + upgrade.moduleProduction * psStruct->getCapacity();
}

static inline int getBuildingPowerPoints(Structure* psStruct)
{
	auto& upgrade = psStruct->getStats()->upgraded_stats[psStruct->playerManager->getPlayer()];
	return upgrade.power + upgrade.modulePower * psStruct->getCapacity();
}

static inline unsigned getBuildingRepairPoints(Structure* psStruct)
{
	return psStruct->getStats()->upgraded_stats[psStruct->playerManager->getPlayer()].repair;
}

static inline unsigned getBuildingRearmPoints(Structure* psStruct)
{
	return psStruct->getStats()->upgraded_stats[psStruct->playerManager->getPlayer()].rearm;
}

WzString getFavoriteStructs();
void setFavoriteStructs(WzString list);

struct LineBuild
{
	[[nodiscard]] Vector2i back() const { return (*this)[count - 1]; }
	Vector2i operator [](int i) const { return begin + i * step; }

	Vector2i begin = {0, 0};
	Vector2i step = {0, 0};
	int count = 0;
};

static bool isWallCombiningStructureType(StructureStats const* pStructureType);
static WallOrientation structChooseWallType(unsigned player, Vector2i map);
static uint16_t wallDir(WallOrientation orient);
static uint16_t wallType(WallOrientation orient);
static WallOrientation structChooseWallTypeBlueprint(Vector2i map);

LineBuild calcLineBuild(Vector2i size, STRUCTURE_TYPE type, Vector2i pos, Vector2i pos2);
LineBuild calcLineBuild(StructureStats const* stats, uint16_t direction, Vector2i pos, Vector2i pos2);

#endif // __INCLUDED_SRC_STRUCTURE_H__
