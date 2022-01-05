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

/** @file structure.h
 *
 *  Definitions for the structures.
 */

#ifndef __INCLUDED_SRC_STRUCTURE_H__
#define __INCLUDED_SRC_STRUCTURE_H__

#include "lib/framework/string_ext.h"
#include "lib/framework/wzconfig.h"

#include "objectdef.h"
#include "visibility.h"
#include "baseobject.h"
#include "positiondef.h"
#include "basedef.h"
#include "statsdef.h"
#include "weapondef.h"

#include <vector>

#define NUM_FACTORY_MODULES 2
#define NUM_POWER_MODULES 4
#define	REF_ANY 255		// Used to indicate any kind of building when calling intGotoNextStructureType()

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
	BLUEPRINT_PLANNED_BY_ALLY,
};

enum class STRUCTURE_STRENGTH
{
    SOFT,
    MEDIUM,
    HARD,
    BUNKER
};

typedef UWORD STRUCTSTRENGTH_MODIFIER;

#define SAS_OPEN_SPEED		(GAME_TICKS_PER_SEC)
#define SAS_STAY_OPEN_TIME	(GAME_TICKS_PER_SEC * 6)

enum class STRUCTURE_ANIMATION_STATE
{
    NORMAL,
    OPEN,
    OPENING,
    CLOSING
};

#define STRUCTURE_CONNECTED 0x0001 ///< This structure must be built side by side with another of the same player

struct FlagPosition : public ObjectPosition
{
    Vector3i coords {0, 0, 0};
    uint8_t factory_inc = 0;
    uint8_t factory_type = 0;
};

/**
 *
 */
struct StructureBounds
{
    StructureBounds();
    StructureBounds(const Vector2i& top_left_coords, const Vector2i& size_in_coords);

    [[nodiscard]] bool is_valid() const;

    Vector2i top_left_coords {0, 0};
    Vector2i size_in_coords {0, 0};
};

struct StructureStats : public BASE_STATS
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
    struct ECM_STATS* ecm_stats; /*Which ECM is standard for the structure -if any*/
    struct SENSOR_STATS* sensor_stats; /*Which Sensor is standard for the structure -if any*/
    unsigned weapon_slots; /*Number of weapons that can be attached to the building*/
    unsigned numWeaps; /*Number of weapons for default */
    struct WEAPON_STATS* psWeapStat[MAX_WEAPONS];
    uint64_t flags;
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
        unsigned hitpoints;
        unsigned resistance; // resist enemy takeover; 0 = immune
        unsigned limit; // current max limit for this type, LOTS_OF = no limit
    } upgraded_stats[MAX_PLAYERS], base;

    bool is_favourite = false; ///< on Favorites list
};

struct ResearchItem
{
    uint8_t tech_code;
    unsigned research_point_cost;
    unsigned power_cost;
};

struct RESEARCH_FACILITY
{
    RESEARCH* psSubject; // The subject the structure is working on.
    RESEARCH* psSubjectPending;
    // The subject the structure is going to work on when the GAME_RESEARCHSTATUS message is received.
    StatusPending statusPending; ///< Pending = not yet synchronised.
    unsigned pendingCount; ///< Number of messages sent but not yet processed.
    RESEARCH* psBestTopic; // The topic with the most research points that was last performed
    UDWORD timeStartHold; /* The time the research facility was put on hold*/
};

struct DroidTemplate;

struct FACTORY
{
    uint8_t productionLoops; ///< Number of loops to perform. Not synchronised, and only meaningful for selectedPlayer.
    UBYTE loopsPerformed; /* how many times the loop has been performed*/
    DroidTemplate* psSubject; ///< The subject the structure is working on.
    DroidTemplate* psSubjectPending;
    ///< The subject the structure is going to working on. (Pending = not yet synchronised.)
    StatusPending statusPending; ///< Pending = not yet synchronised.
    unsigned pendingCount; ///< Number of messages sent but not yet processed.
    UDWORD timeStarted; /* The time the building started on the subject*/
    int buildPointsRemaining; ///< Build points required to finish building the droid.
    UDWORD timeStartHold; /* The time the factory was put on hold*/
    FLAG_POSITION* psAssemblyPoint; /* Place for the new droids to assemble at */
    struct DROID* psCommander; // command droid to produce droids for (if any)
    uint32_t secondaryOrder; ///< Secondary order state for all units coming out of the factory.
};

struct RES_EXTRACTOR
{
    struct Structure* psPowerGen; ///< owning power generator
};

struct POWER_GEN
{
    struct Structure* apResExtractors[NUM_POWER_MODULES]; ///< Pointers to associated oil derricks
};

class DROID_GROUP;

struct REPAIR_FACILITY
{
    BASE_OBJECT* psObj; /* Object being repaired */
    FLAG_POSITION* psDeliveryPoint; /* Place for the repaired droids to assemble at */
    // The group the droids to be repaired by this facility belong to
    DROID_GROUP* psGroup;
    int droidQueue; ///< Last count of droid queue for this facility
};

struct REARM_PAD
{
    UDWORD timeStarted; /* Time reArm started on current object */
    BASE_OBJECT* psObj; /* Object being rearmed */
    UDWORD timeLastUpdated; /* Time rearm was last updated */
};

struct WALL
{
    unsigned type; // Type of wall, 0 = ─, 1 = ┼, 2 = ┴, 3 = ┘.
};

class Structure : public virtual Unit
{
public:
    virtual ~Structure() = default;
    Structure(const Structure&) = delete;
    Structure(Structure&&) = delete;
    Structure& operator=(const Structure&) = delete;
    Structure& operator=(Structure&&) = delete;

    virtual void print_info() const = 0;
    [[nodiscard]] virtual bool has_sensor() const = 0;
    [[nodiscard]] virtual bool has_standard_sensor() const = 0;
    [[nodiscard]] virtual bool has_CB_sensor() const = 0;
    [[nodiscard]] virtual bool has_VTOL_intercept_sensor() const = 0;
    [[nodiscard]] virtual bool has_VTOL_CB_sensor() const = 0;
};

namespace Impl
{
    class Structure : public virtual ::Structure, public Impl::Unit
    {
    public:
        Structure(uint32_t id, unsigned player);

        [[nodiscard]] bool is_blueprint() const noexcept;
        [[nodiscard]] bool is_wall() const noexcept;
        [[nodiscard]] bool is_radar_detector() const final;
        [[nodiscard]] bool is_probably_doomed() const;
        [[nodiscard]] bool is_pulled_to_terrain() const;
        [[nodiscard]] bool has_modules() const noexcept;
        [[nodiscard]] bool has_sensor() const final;
        [[nodiscard]] bool has_standard_sensor() const final;
        [[nodiscard]] bool has_CB_sensor() const final;
        [[nodiscard]] bool has_VTOL_intercept_sensor() const final;
        [[nodiscard]] bool has_VTOL_CB_sensor() const final;
        [[nodiscard]] bool smoke_when_damaged() const noexcept;
        [[nodiscard]] unsigned get_original_hp() const;
        [[nodiscard]] unsigned get_armour_value(WEAPON_CLASS weapon_class) const;
        [[nodiscard]] Vector2i get_size() const;
        [[nodiscard]] int get_foundation_depth() const noexcept;
        [[nodiscard]] const iIMDShape& get_IMD_shape() const final;
        void update_expected_damage(unsigned damage, bool is_direct) noexcept override;
        [[nodiscard]] unsigned calculate_sensor_range() const final;
        [[nodiscard]] int calculate_gate_height(std::size_t time, int minimum) const;
        void set_foundation_depth(int depth) noexcept;
        void print_info() const override;
        [[nodiscard]] unsigned build_points_to_completion() const;
        [[nodiscard]] unsigned calculate_refunded_power() const;
        [[nodiscard]] int calculate_attack_priority(const Unit* target, int weapon_slot) const final;
        [[nodiscard]] const ::SimpleObject& get_target(int weapon_slot) const final;
        [[nodiscard]] STRUCTURE_STATE get_state() const;
    private:
        using enum STRUCTURE_ANIMATION_STATE;
        std::shared_ptr<StructureStats> stats; /* pointer to the structure stats for this type of building */
        STRUCTURE_STATE state; /* defines whether the structure is being built, doing nothing or performing a function */
        unsigned current_build_points; /* the build points currently assigned to this structure */
        int resistance; /* current resistance points, 0 = cannot be attacked electrically */
        UDWORD lastResistance; /* time the resistance was last increased*/

        ///< Rate that this structure is being built, calculated each tick. Only meaningful if status == SS_BEING_BUILT. If construction hasn't started and build rate is 0, remove the structure.
        int build_rate;

        ///< Needed if wanting the buildRate between buildRate being reset to 0 each tick and the trucks calculating it.
        int previous_build_rate;

        std::array<::SimpleObject*, MAX_WEAPONS> target;

        unsigned expected_damage;
        ///< Expected damage to be caused by all currently incoming projectiles. This info is shared between all players,
        ///< but shouldn't make a difference unless 3 mutual enemies happen to be fighting each other at the same time.
        uint32_t prevTime; ///< Time of structure's previous tick.
        int foundation_depth; ///< Depth of structure's foundation
        uint8_t capacity; ///< Lame name: current number of module upgrades (*not* maximum nb of upgrades)
        STRUCTURE_ANIMATION_STATE animation_state = NORMAL;
        std::size_t lastStateTime;
        iIMDShape *prebuiltImd;

        inline Vector2i size() const { return stats->size(rot.direction); }
    };
}
#define LOTS_OF 0xFFFFFFFF  // highest number the limit can be set to

//the three different types of factory (currently) - FACTORY, CYBORG_FACTORY, VTOL_FACTORY
// added repair facilities as they need an assembly point as well
enum FLAG_TYPE
{
    FACTORY_FLAG,
    CYBORG_FLAG,
    VTOL_FLAG,
    REPAIR_FLAG,
    //separate the numfactory from numflag
    NUM_FLAG_TYPES,
    NUM_FACTORY_TYPES = REPAIR_FLAG,
};

//this is used for module graphics - factory and vtol factory
static const int NUM_FACMOD_TYPES = 2;

struct ProductionRun
{
    ProductionRun() = default;
    bool operator ==(const DroidTemplate& rhs) const;

    void restart();
    [[nodiscard]] bool is_valid() const;
    [[nodiscard]] bool is_complete() const;
    [[nodiscard]] int tasks_remaining() const;

    std::shared_ptr<DroidTemplate> target;
    int quantity_to_build = 0;
    int quantity_built = 0;
};

struct UPGRADE_MOD
{
    UWORD modifier; //% to increase the stat by
};

typedef UPGRADE_MOD REPAIR_FACILITY_UPGRADE;
typedef UPGRADE_MOD POWER_UPGRADE;
typedef UPGRADE_MOD REARM_UPGRADE;

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
extern UDWORD factoryModuleStat;
extern UDWORD powerModuleStat;
extern UDWORD researchModuleStat;

// the structure that was last hit
extern Structure* psLastStructHit;

//stores which player the production list has been set up for
extern SBYTE productionPlayer;

//holder for all StructureStats
extern StructureStats* asStructureStats;
extern UDWORD numStructureStats;

//used to hold the modifiers cross refd by weapon effect and structureStrength
extern STRUCTSTRENGTH_MODIFIER asStructStrengthModifier[WE_NUMEFFECTS][NUM_STRUCT_STRENGTH];

void handleAbandonedStructures();

int getMaxDroids(UDWORD player);
int getMaxCommanders(UDWORD player);
int getMaxConstructors(UDWORD player);
void setMaxDroids(UDWORD player, int value);
void setMaxCommanders(UDWORD player, int value);
void setMaxConstructors(UDWORD player, int value);

bool structureExists(int player, STRUCTURE_TYPE type, bool built, bool isMission);

bool IsPlayerDroidLimitReached(int player);

bool loadStructureStats(WzConfig& ini);
/*Load the Structure Strength Modifiers from the file exported from Access*/
bool loadStructureStrengthModifiers(WzConfig& ini);

bool structureStatsShutDown();

int requestOpenGate(Structure* psStructure);
int gateCurrentOpenHeight(const Structure* psStructure, uint32_t time, int minimumStub);
///< Returns how far open the gate is, or 0 if the structure is not a gate.

int32_t structureDamage(Structure* psStructure, unsigned damage, WEAPON_CLASS weaponClass,
                        WEAPON_SUBCLASS weaponSubClass, unsigned impactTime, bool isDamagePerSecond, int minDamage);
void structureBuild(Structure* psStructure, Droid* psDroid, int buildPoints, int buildRate = 1);
void structureDemolish(Structure* psStructure, Droid* psDroid, int buildPoints);
void structureRepair(Structure* psStruct, Droid* psDroid, int buildRate);
/* Set the type of droid for a factory to build */
bool structSetManufacture(Structure* psStruct, DroidTemplate* psTempl, QUEUE_MODE mode);
uint32_t structureBuildPointsToCompletion(const Structure& structure);
float structureCompletionProgress(const Structure& structure);

//builds a specified structure at a given location
Structure* buildStructure(StructureStats* pStructureType, UDWORD x, UDWORD y, UDWORD player, bool FromSave);
Structure* buildStructureDir(StructureStats* pStructureType, UDWORD x, UDWORD y, uint16_t direction, UDWORD player,
                             bool FromSave);
/// Create a blueprint structure, with just enough information to render it
Structure* buildBlueprint(StructureStats const* psStats, Vector3i xy, uint16_t direction, unsigned moduleIndex,
                          STRUCT_STATES state, uint8_t ownerPlayer);
/* The main update routine for all Structures */
void structureUpdate(Structure* psBuilding, bool bMission);

/* Remove a structure and free it's memory */
bool destroyStruct(Structure* psDel, unsigned impactTime);

// remove a structure from a game without any visible effects
// bDestroy = true if the object is to be destroyed
// (for example used to change the type of wall at a location)
bool removeStruct(Structure* psDel, bool bDestroy);

//fills the list with Structures that can be built
std::vector<StructureStats*> fillStructureList(UDWORD selectedPlayer, UDWORD limit, bool showFavorites);

/// Checks if the two structures would be too close to build together.
bool isBlueprintTooClose(StructureStats const* stats1, Vector2i pos1, uint16_t dir1, StructureStats const* stats2,
                         Vector2i pos2, uint16_t dir2);

/// Checks that the location is valid to build on.
/// pos in world coords
bool validLocation(BASE_STATS* psStats, Vector2i pos, uint16_t direction, unsigned player, bool bCheckBuildQueue);

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
void setAssemblyPoint(FLAG_POSITION* psAssemblyPoint, UDWORD x, UDWORD y, UDWORD player, bool bCheck);

/*initialises the flag before a new data set is loaded up*/
void initFactoryNumFlag();

//called at start of missions
void resetFactoryNumFlag();

/* get demolish stat */
StructureStats* structGetDemolishStat();

/*find a location near to the factory to start the droid of*/
bool placeDroid(Structure* psStructure, UDWORD* droidX, UDWORD* droidY);

//Set the factory secondary orders to a droid
void setFactorySecondaryState(Droid* psDroid, Structure* psStructure);

/* is this a lassat structure? */
static inline bool isLasSat(StructureStats* pStructureType)
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
bool electronicDamage(BASE_OBJECT* psTarget, UDWORD damage, UBYTE attackPlayer);

/* EW works differently in multiplayer mode compared with single player.*/
bool validStructResistance(const Structure* psStruct);

/*checks to see if a specific structure type exists -as opposed to a structure
stat type*/
bool checkSpecificStructExists(UDWORD structInc, UDWORD player);

int32_t getStructureDamage(const Structure* psStructure);

unsigned structureBodyBuilt(const Structure* psStruct);
///< Returns the maximum body points of a structure with the current number of build points.
UDWORD structureBody(const Structure* psStruct);
UDWORD structureResistance(const StructureStats* psStats, UBYTE player);

void hqReward(UBYTE losingPlayer, UBYTE rewardPlayer);

// Is a structure a factory of somekind?
bool StructIsFactory(const Structure* Struct);

// Is a flag a factory delivery point?
bool FlagIsFactory(const FLAG_POSITION* psCurrFlag);

// Find a factories corresonding delivery point.
FLAG_POSITION* FindFactoryDelivery(const Structure* Struct);

//Find the factory associated with the delivery point - returns NULL if none exist
Structure* findDeliveryFactory(FLAG_POSITION* psDelPoint);

/*this is called when a factory produces a droid. The Template returned is the next
one to build - if any*/
DroidTemplate* factoryProdUpdate(Structure* psStructure, DroidTemplate* psTemplate);

//increment the production run for this type
void factoryProdAdjust(Structure* psStructure, DroidTemplate* psTemplate, bool add);

//returns the quantity of a specific template in the production list
ProductionRunEntry getProduction(Structure* psStructure, DroidTemplate* psTemplate);

//looks through a players production list to see if a command droid is being built
UBYTE checkProductionForCommand(UBYTE player);

//check that delivery points haven't been put down in invalid location
void checkDeliveryPoints(UDWORD version);

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
Structure* giftSingleStructure(Structure* psStructure, UBYTE attackPlayer, bool electronic_warfare = true);

/*Initialise the production list and set up the production player*/
void changeProductionPlayer(UBYTE player);

// La!
bool IsStatExpansionModule(const StructureStats* psStats);

/// is this a blueprint and not a real structure?
bool structureIsBlueprint(const Structure* psStructure);
bool isBlueprint(const BASE_OBJECT* psObject);

/*returns the power cost to build this structure, or to add its next module */
UDWORD structPowerToBuildOrAddNextModule(const Structure* psStruct);

// check whether a factory of a certain number and type exists
bool checkFactoryExists(UDWORD player, UDWORD factoryType, UDWORD inc);

/*checks the structure passed in is a Las Sat structure which is currently
selected - returns true if valid*/
bool lasSatStructSelected(const Structure* psStruct);

void cbNewDroid(Structure* psFactory, Droid* psDroid);

StructureBounds getStructureBounds(const Structure* object);
StructureBounds getStructureBounds(const StructureStats* stats, Vector2i pos, uint16_t direction);

bool canStructureHaveAModuleAdded(const Structure* const structure);

static inline int structSensorRange(const Structure* psObj)
{
	return objSensorRange((const BASE_OBJECT*)psObj);
}

static inline int structJammerPower(const Structure* psObj)
{
	return objJammerPower((const BASE_OBJECT*)psObj);
}

static inline Rotation structureGetInterpolatedWeaponRotation(Structure* psStructure, int weaponSlot, uint32_t time)
{
	return interpolateRot(psStructure->asWeaps[weaponSlot].prevRot, psStructure->asWeaps[weaponSlot].rot,
	                      psStructure->prevTime, psStructure->time, time);
}

#define setStructureTarget(_psBuilding, _psNewTarget, _idx, _targetOrigin) _setStructureTarget(_psBuilding, _psNewTarget, _idx, _targetOrigin, __LINE__, __FUNCTION__)

static inline void _setStructureTarget(Structure* psBuilding, BASE_OBJECT* psNewTarget, UWORD idx,
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
	functionality.statusPending = FACTORY_START_PENDING;
	++functionality.pendingCount;
}

template <typename Functionality>
static inline void setStatusPendingCancel(Functionality& functionality)
{
	functionality.psSubjectPending = nullptr;
	functionality.statusPending = FACTORY_CANCEL_PENDING;
	++functionality.pendingCount;
}

template <typename Functionality>
static inline void setStatusPendingHold(Functionality& functionality)
{
	if (functionality.psSubjectPending == nullptr)
	{
		functionality.psSubjectPending = functionality.psSubject;
	}
	functionality.statusPending = FACTORY_HOLD_PENDING;
	++functionality.pendingCount;
}

template <typename Functionality>
static inline void setStatusPendingRelease(Functionality& functionality)
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
		functionality.statusPending = FACTORY_NOTHING_PENDING;
	}
}

void checkStructure(const Structure* psStructure, const char* const location_description, const char* function,
                    const int recurse);

#define CHECK_STRUCTURE(object) checkStructure((object), AT_MACRO, __FUNCTION__, max_check_object_recursion)

void structureInitVars();
void initStructLimits();

#define syncDebugStructure(psStruct, ch) _syncDebugStructure(__FUNCTION__, psStruct, ch)
void _syncDebugStructure(const char* function, Structure const* psStruct, char ch);


// True iff object is a structure.
static inline bool isStructure(SIMPLE_OBJECT const* psObject)
{
	return psObject != nullptr && psObject->type == OBJ_STRUCTURE;
}

// Returns STRUCTURE * if structure or NULL if not.
static inline Structure* castStructure(SIMPLE_OBJECT* psObject)
{
	return isStructure(psObject) ? (Structure*)psObject : (Structure*)nullptr;
}

// Returns STRUCTURE const * if structure or NULL if not.
static inline Structure const* castStructure(SIMPLE_OBJECT const* psObject)
{
	return isStructure(psObject) ? (Structure const*)psObject : (Structure const*)nullptr;
}

static inline int getBuildingResearchPoints(Structure* psStruct)
{
	auto& upgrade = psStruct->pStructureType->upgraded_stats[psStruct->player];
	return upgrade.research + upgrade.moduleResearch * psStruct->capacity;
}

static inline int getBuildingProductionPoints(Structure* psStruct)
{
	auto& upgrade = psStruct->pStructureType->upgraded_stats[psStruct->player];
	return upgrade.production + upgrade.moduleProduction * psStruct->capacity;
}

static inline int getBuildingPowerPoints(Structure* psStruct)
{
	auto& upgrade = psStruct->pStructureType->upgraded_stats[psStruct->player];
	return upgrade.power + upgrade.modulePower * psStruct->capacity;
}

static inline int getBuildingRepairPoints(Structure* psStruct)
{
	return psStruct->pStructureType->upgraded_stats[psStruct->player].repair;
}

static inline int getBuildingRearmPoints(Structure* psStruct)
{
	return psStruct->pStructureType->upgraded_stats[psStruct->player].rearm;
}

WzString getFavoriteStructs();
void setFavoriteStructs(WzString list);

struct LineBuild
{
	Vector2i back() const { return (*this)[count - 1]; }
	Vector2i operator [](int i) const { return begin + i * step; }

	Vector2i begin = {0, 0};
	Vector2i step = {0, 0};
	int count = 0;
};

LineBuild calcLineBuild(Vector2i size, STRUCTURE_TYPE type, Vector2i pos, Vector2i pos2);
LineBuild calcLineBuild(StructureStats const* stats, uint16_t direction, Vector2i pos, Vector2i pos2);

#endif // __INCLUDED_SRC_STRUCTURE_H__
