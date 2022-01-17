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

#include "lib/gamelib/gtime.h"

#include "constructedobject.h"
#include "droid.h"
#include "order.h"


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

enum class PENDING_STATUS
{
	NOTHING_PENDING,
	START_PENDING,
	HOLD_PENDING,
	CANCEL_PENDING
};

enum class STRUCTURE_TYPE
{
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
    LASSAT
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
    StructureBounds(const Vector2i& top_left_coords, const Vector2i& size_in_coords);

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
    std::unique_ptr<iIMDShape> base_imd; /*The base IMD to draw for this structure */
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

class Structure : public virtual ConstructedObject
{
public:
    ~Structure() override = default;

    /************************** Accessors *************************/
    [[nodiscard]] virtual int getFoundationDepth() const = 0;
    [[nodiscard]] virtual Vector2i getSize() const = 0;
    [[nodiscard]] virtual STRUCTURE_STATE getState() const = 0;
    [[nodiscard]] virtual const StructureStats& getStats() const = 0;
    [[nodiscard]] virtual STRUCTURE_ANIMATION_STATE getAnimationState() const = 0;
    [[nodiscard]] virtual unsigned getArmourValue(WEAPON_CLASS weaponClass) const = 0;
    [[nodiscard]] virtual uint8_t getCapacity() const = 0;

    [[nodiscard]] virtual bool isWall() const = 0;
    virtual void printInfo() const = 0;
    [[nodiscard]] virtual bool hasSensor() const = 0;
    virtual Structure* giftSingleStructure(unsigned attackPlayer, bool electronic_warfare) = 0;
    [[nodiscard]] virtual float structureCompletionProgress() const = 0;
     virtual void aiUpdateStructure(bool isMission) = 0;
     virtual void structureUpdate(bool bMission) = 0;
     virtual void setFoundationDepth(int depth) = 0;
     virtual int requestOpenGate() = 0;
     virtual void structureBuild(::Droid* psDroid, int builtPoints, int buildRate_) = 0;

    [[nodiscard]] virtual std::unique_ptr<Structure> buildStructureDir(
            StructureStats* pStructureType, unsigned x, unsigned y,
            uint16_t direction, unsigned player, bool FromSave) = 0;
};

namespace Impl
{
    class Structure : public virtual ::Structure, public ConstructedObject
    {
    public:
        ~Structure() override;
        Structure(unsigned id, unsigned player);

        /************************** Accessors *************************/
        [[nodiscard]] STRUCTURE_ANIMATION_STATE getAnimationState() const final;
        [[nodiscard]] unsigned getOriginalHp() const final;
        [[nodiscard]] unsigned getArmourValue(WEAPON_CLASS weaponClass) const final;
        [[nodiscard]] Vector2i getSize() const final;
        [[nodiscard]] int getFoundationDepth() const noexcept final;
        [[nodiscard]] const iIMDShape& getImdShape() const final;
        [[nodiscard]] const ::PersistentObject& getTarget(int weapon_slot) const final;
        [[nodiscard]] STRUCTURE_STATE getState() const final;
        [[nodiscard]] const StructureStats& getStats() const final;
        [[nodiscard]] uint8_t getCapacity() const final;

        int objRadius() const final;
        [[nodiscard]] bool isBlueprint() const noexcept;
        [[nodiscard]] bool isWall() const noexcept final;
        [[nodiscard]] bool isRadarDetector() const final;
        [[nodiscard]] bool isProbablyDoomed() const final;
        [[nodiscard]] bool hasModules() const noexcept;
        [[nodiscard]] bool hasSensor() const final;
        [[nodiscard]] bool hasStandardSensor() const final;
        [[nodiscard]] bool hasCbSensor() const final;
        [[nodiscard]] bool hasVtolInterceptSensor() const final;
        [[nodiscard]] bool hasVtolCbSensor() const final;
        [[nodiscard]] bool smokeWhenDamaged() const noexcept;
        void updateExpectedDamage(unsigned damage, bool is_direct) noexcept override;
        [[nodiscard]] unsigned calculateSensorRange() const final;
        [[nodiscard]] int calculateGateHeight(std::size_t time, int minimum) const;
        void setFoundationDepth(int depth) noexcept final;
        void printInfo() const override;
        [[nodiscard]] unsigned buildPointsToCompletion() const;
        [[nodiscard]] unsigned calculateRefundedPower() const;
        [[nodiscard]] int calculateAttackPriority(const ::ConstructedObject* target, int weapon_slot) const final;
        ::Structure* giftSingleStructure(unsigned attackPlayer, bool electronic_warfare) final;
        [[nodiscard]] float structureCompletionProgress() const final;
        void aiUpdateStructure(bool isMission) final;
        void structureUpdate(bool bMission) final;
        int requestOpenGate() final;
        void structureBuild(::Droid* psDroid, int builtPoints, int buildRate_) final;

        [[nodiscard]] std::unique_ptr<::Structure> buildStructureDir(
                StructureStats* pStructureType, unsigned x, unsigned y,
                uint16_t direction, unsigned player, bool FromSave) final;
    protected:
        using enum STRUCTURE_ANIMATION_STATE;
        using enum STRUCTURE_STATE;
        std::shared_ptr<StructureStats> stats;

        /// Whether the structure is being built, doing nothing or performing a function
        STRUCTURE_STATE state;

        /// The build points currently assigned to this structure
        unsigned currentBuildPoints;

        /// Time the resistance was last increased
        std::size_t lastResistance;

        /// Rate that this structure is being built, calculated each tick. Only
        /// meaningful if status == BEING_BUILT. If construction hasn't started
        /// and build rate is 0, remove the structure
        int buildRate;

        /// Needed if wanting the buildRate between buildRate being reset to 0
        /// each tick and the trucks calculating it
        int previousBuildRate;

        std::array<::PersistentObject*, MAX_WEAPONS> target;

        /// Expected damage to be caused by all currently incoming projectiles.
        /// This info is shared between all players, but shouldn't make a difference
        /// unless 3 mutual enemies happen to be fighting each other at the same time.
        unsigned expectedDamage;

        /// Time of structure's previous tick
        std::size_t prevTime;
        int foundationDepth;

        /// Lame name: current number of module upgrades
        /// (*not* maximum number of upgrades)
        uint8_t capacity;

        STRUCTURE_ANIMATION_STATE animationState = NORMAL;
        std::size_t lastStateTime;
        std::unique_ptr<iIMDShape> prebuiltImd;
    };

    StructureBounds getBounds(const Structure& structure) noexcept;
}

struct ResearchItem
{
    uint8_t tech_code;
    unsigned research_point_cost;
    unsigned power_cost;
};

class ResearchFacility : public virtual Structure, public Impl::Structure
{
public:
    [[nodiscard]] const ResearchItem* getSubject() const;
private:
    std::unique_ptr<ResearchItem> psSubject; // The subject the structure is working on.
    std::unique_ptr<ResearchItem> psSubjectPending;
    // The subject the structure is going to work on when the GAME_RESEARCHSTATUS message is received.
    PENDING_STATUS statusPending; ///< Pending = not yet synchronised.
    unsigned pendingCount; ///< Number of messages sent but not yet processed.
    std::unique_ptr<ResearchItem> psBestTopic; // The topic with the most research points that was last performed
    std::size_t timeStartHold; /* The time the research facility was put on hold*/
};

struct ProductionRun
{
    ProductionRun() = default;
    bool operator ==(const DroidTemplate& rhs) const;

    void restart();
    [[nodiscard]] bool is_valid() const;
    [[nodiscard]] bool is_complete() const;
    [[nodiscard]] int tasks_remaining() const;

    std::shared_ptr<DroidTemplate> target = nullptr;
    int quantity_to_build = 0;
    int quantity_built = 0;
};

class Factory : public virtual Structure, public Impl::Structure
{
public:
    bool structSetManufacture(DroidTemplate* psTempl, QUEUE_MODE mode);
    void refundBuildPower();
    void releaseProduction(QUEUE_MODE mode);
    void holdProduction(QUEUE_MODE mode);
    void assignFactoryCommandDroid(Droid* commander);
    bool setFactoryState(SECONDARY_ORDER sec, SECONDARY_STATE state);
    bool getFactoryState(SECONDARY_ORDER sec, SECONDARY_STATE* pState);
    bool structPlaceDroid(DroidTemplate* psTempl, Droid** ppsDroid);
    [[nodiscard]] bool IsFactoryCommanderGroupFull();
    bool checkHaltOnMaxUnitsReached(bool isMission);
    ProductionRun getProduction(DroidTemplate *psTemplate);
    void factoryLoopAdjust(bool add);
    [[nodiscard]] const DroidTemplate* getSubject() const;
    [[nodiscard]] FlagPosition* FindFactoryDelivery() const;
    void cancelProduction(QUEUE_MODE mode, bool mayClearProductionRun);
    DroidTemplate* factoryProdUpdate(DroidTemplate* psTemplate);
private:
    uint8_t productionLoops; ///< Number of loops to perform. Not synchronised, and only meaningful for selectedPlayer.
    uint8_t loopsPerformed; /* how many times the loop has been performed*/
    std::shared_ptr<DroidTemplate> psSubject; ///< The subject the structure is working on.
    std::shared_ptr<DroidTemplate> psSubjectPending;
    ///< The subject the structure is going to working on. (Pending = not yet synchronised.)
    PENDING_STATUS statusPending; ///< Pending = not yet synchronised.
    unsigned pendingCount; ///< Number of messages sent but not yet processed.
    std::size_t timeStarted; /* The time the building started on the subject*/
    int buildPointsRemaining; ///< Build points required to finish building the droid.
    std::size_t timeStartHold; /* The time the factory was put on hold*/
    std::unique_ptr<FlagPosition> psAssemblyPoint; /* Place for the new droids to assemble at */
    Droid* psCommander; // command droid to produce droids for (if any)
    unsigned secondaryOrder; ///< Secondary order state for all units coming out of the factory.
};

class PowerGenerator;

class ResourceExtractor : public virtual Structure, public Impl::Structure
{
public:
    void releaseResExtractor();
    void checkForPowerGen();
private:
    PowerGenerator* power_generator;
};

class PowerGenerator : public virtual Structure, public Impl::Structure
{
public:
    void releasePowerGen();
private:
    /// Pointers to associated oil derricks
    std::array<ResourceExtractor*, NUM_POWER_MODULES> resource_extractors;
};

class RepairFacility : public virtual Structure, public Impl::Structure
{
private:
    ConstructedObject* psObj; /* Object being repaired */
    std::unique_ptr<FlagPosition> psDeliveryPoint; /* Place for the repaired droids to assemble at */
    // The group the droids to be repaired by this facility belong to
    std::shared_ptr<Group> psGroup;
    int droidQueue; /// Last count of droid queue for this facility
};

class RearmPad : public virtual Structure, public Impl::Structure
{
public:
    [[nodiscard]] bool isClear() const;
private:
    std::size_t timeStarted; /* Time reArm started on current object */
    Droid* psObj; /* Object being rearmed */
    std::size_t timeLastUpdated; /* Time rearm was last updated */
};

//this is used for module graphics - factory and vtol factory
static const int NUM_FACMOD_TYPES = 2;



extern std::array< std::vector<ProductionRun>, NUM_FACTORY_TYPES> asProductionRun;

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

//holder for all StructureStats
extern StructureStats* asStructureStats;
extern unsigned numStructureStats;

//used to hold the modifiers cross refd by weapon effect and structureStrength
extern STRUCTSTRENGTH_MODIFIER asStructStrengthModifier[WE_NUMEFFECTS][NUM_STRUCT_STRENGTH];

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
void structureRepair(Structure* psStruct, Droid* psDroid, int buildRate);
/* Set the type of droid for a factory to build */
bool structSetManufacture(Structure* psStruct, DroidTemplate* psTempl, QUEUE_MODE mode);
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
static inline bool isLasSat(StructureStats* pStructureType)
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
bool electronicDamage(PersistentObject* psTarget, unsigned damage, UBYTE attackPlayer);

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
void cancelProduction(Structure* psBuilding, QUEUE_MODE mode, bool mayClearProductionRun = true);

/*set a factory's production run to hold*/
void holdProduction(Structure* psBuilding, QUEUE_MODE mode);

/*release a factory's production run from hold*/
void releaseProduction(Structure* psBuilding, QUEUE_MODE mode);

/// Does the next item in the production list.
void doNextProduction(Structure* psStructure, DroidTemplate* current, QUEUE_MODE mode);

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
bool isBlueprint(const PersistentObject* psObject);

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

bool canStructureHaveAModuleAdded(Structure* const structure);

static inline unsigned structSensorRange(const Structure* psObj)
{
	return objSensorRange((const PersistentObject*)psObj);
}

static inline unsigned structJammerPower(const Structure* psObj)
{
	return objJammerPower((const PersistentObject*)psObj);
}

static inline Rotation structureGetInterpolatedWeaponRotation(Structure* psStructure, int weaponSlot, uint32_t time)
{
	return interpolateRot(psStructure->asWeaps[weaponSlot].previousRotation,
                        psStructure->asWeaps[weaponSlot].rotation,
                        psStructure->prevTime, psStructure->time, time);
}

#define setStructureTarget(_psBuilding, _psNewTarget, _idx, _targetOrigin) _setStructureTarget(_psBuilding, _psNewTarget, _idx, _targetOrigin, __LINE__, __FUNCTION__)

static inline void _setStructureTarget(Structure* psBuilding, PersistentObject* psNewTarget, UWORD idx,
                                       TARGET_ORIGIN targetOrigin, int line, const char* func)
{
	ASSERT_OR_RETURN(, idx < MAX_WEAPONS, "Bad index");
	ASSERT_OR_RETURN(, psNewTarget == nullptr || !psNewTarget->died, "setStructureTarget set dead target");
	psBuilding->psTarget[idx] = psNewTarget;
	psBuilding->asWeaps[idx].origin = targetOrigin;
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
	auto& upgrade = psStruct->getStats().upgraded_stats[psStruct->getPlayer()];
	return upgrade.research + upgrade.moduleResearch * psStruct->capacity;
}

static inline int getBuildingProductionPoints(Structure* psStruct)
{
	auto& upgrade = psStruct->getStats().upgraded_stats[psStruct->getPlayer()];
	return upgrade.production + upgrade.moduleProduction * psStruct->capacity;
}

static inline int getBuildingPowerPoints(Structure* psStruct)
{
	auto& upgrade = psStruct->getStats().upgraded_stats[psStruct->getPlayer()];
	return upgrade.power + upgrade.modulePower * psStruct->capacity;
}

static inline unsigned getBuildingRepairPoints(Structure* psStruct)
{
	return psStruct->getStats().upgraded_stats[psStruct->getPlayer()].repair;
}

static inline unsigned getBuildingRearmPoints(Structure* psStruct)
{
	return psStruct->getStats().upgraded_stats[psStruct->getPlayer()].rearm;
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

LineBuild calcLineBuild(Vector2i size, STRUCTURE_TYPE type, Vector2i pos, Vector2i pos2);
LineBuild calcLineBuild(StructureStats const* stats, uint16_t direction, Vector2i pos, Vector2i pos2);

#endif // __INCLUDED_SRC_STRUCTURE_H__
