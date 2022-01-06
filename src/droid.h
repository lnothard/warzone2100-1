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
 *  Definitions for the droid object.
 */

#ifndef __INCLUDED_SRC_DROID_H__
#define __INCLUDED_SRC_DROID_H__

#include "lib/framework/string_ext.h"
#include "lib/gamelib/gtime.h"

#include "objectdef.h"
#include "stats.h"
#include "visibility.h"
#include "selection.h"
#include "basedef.h"
#include "movedef.h"
#include "orderdef.h"
#include "statsdef.h"
#include "unit.h"
#include "weapondef.h"

#include <vector>
#include <queue>

/// world->screen check - alex
#define OFF_SCREEN 9999

// Percentage of body points remaining at which to repair droid automatically.
#define REPAIRLEV_LOW	50
// Ditto, but this will repair much sooner.
#define REPAIRLEV_HIGH	75

#define DROID_RESISTANCE_FACTOR     30

/// Changing this breaks campaign saves!
#define MAX_RECYCLED_DROIDS 450

/// Used to stop structures being built too near the edge and droids
/// being placed down
#define TOO_NEAR_EDGE 3

/* Experience modifiers */

/// Damage of a droid is reduced by this value per experience level (in percent)
#define EXP_REDUCE_DAMAGE 6
/// Accuracy of a droid is increased by this value per experience level (in percent)
#define EXP_ACCURACY_BONUS	5
/// Speed of a droid is increased by this value per experience level (in percent)
#define EXP_SPEED_BONUS 5

/*!
 * The number of components in the asParts / asBits arrays.
 * Weapons are stored separately, thus the maximum index into the array
 * is 1 smaller than the number of components.
 */
#define DROID_MAXCOMP (COMP_NUMCOMPONENTS - 1)

/// The maximum number of droid weapons
#define DROID_DAMAGE_SCALING 400

/// TODO This should really be logarithmic
#define	CALC_DROID_SMOKE_INTERVAL(x) ((((100-x)+10)/10) * DROID_DAMAGE_SCALING)

/// Defines how many times to perform the iteration on looking for a blank location
#define LOOK_FOR_EMPTY_TILE 20

typedef std::vector<DROID_ORDER_DATA> OrderList;

enum class DROID_TYPE
{
    WEAPON,
    SENSOR,
    ECM,
    CONSTRUCT,
    PERSON,
    CYBORG,
    TRANSPORTER,
    COMMAND,
    REPAIRER,
    DEFAULT,
    CYBORG_CONSTRUCT,
    CYBORG_REPAIR,
    CYBORG_SUPER,
    SUPER_TRANSPORTER,
    ANY
};

enum class ACTION
{
    NONE,
    MOVE,
    BUILD,
    DEMOLISH,
    REPAIR,
    ATTACK,
    OBSERVE,
    FIRE_SUPPORT,
    SULK,
    TRANSPORT_OUT,
    TRANSPORT_WAIT_TO_FLY_IN,
    TRANSPORT_IN,
    DROID_REPAIR,
    RESTORE,
    MOVE_FIRE,
    MOVE_TO_BUILD,
    MOVE_TO_DEMOLISH,
    MOVE_TO_REPAIR,
    BUILD_WANDER,
    MOVE_TO_ATTACK,
    ROTATE_TO_ATTACK,
    MOVE_TO_OBSERVE,
    WAIT_FOR_REPAIR,
    MOVE_TO_REPAIR_POINT,
    WAIT_DURING_REPAIR,
    MOVE_TO_DROID_REPAIR,
    MOVE_TO_RESTORE,
    MOVE_TO_REARM,
    WAIT_FOR_REARM,
    MOVE_TO_REARM_POINT,
    WAIT_DURING_REARM,
    VTOL_ATTACK,
    CLEAR_REARM_PAD,
    RETURN_TO_POS,
    FIRE_SUPPORT_RETREAT,
    CIRCLE
};

struct DroidTemplate : public BaseStats
{
    DroidTemplate() = default;

    using enum DROID_TYPE;
    unsigned id = 0;
    unsigned weapon_count = 0;
    DROID_TYPE type = ANY;

    /// Not player designed, not saved, never delete or change
    bool is_prefab = false;

    bool is_stored = false;
    bool is_enabled = false;
};

class Droid : public virtual ::Unit, public Impl::Unit
{
public:
    Droid(unsigned id, unsigned player);

    /* Accessors */
    [[nodiscard]] ACTION get_current_action() const noexcept;
    [[nodiscard]] const Order& get_current_order() const;
    [[nodiscard]] DROID_TYPE get_type() const noexcept;
    [[nodiscard]] unsigned get_level() const;
    [[nodiscard]] unsigned get_commander_level() const;
    [[nodiscard]] const iIMDShape& get_IMD_shape() const final;
    [[nodiscard]] int get_vertical_speed() const noexcept;
    [[nodiscard]] unsigned get_secondary_order() const noexcept;
    [[nodiscard]] const Vector2i& get_destination() const;
    [[nodiscard]] const ::SimpleObject& get_target(int weapon_slot) const final;
    [[nodiscard]] const std::optional<PropulsionStats>& get_propulsion() const;

    [[nodiscard]] bool is_probably_doomed(bool is_direct_damage) const;
    [[nodiscard]] bool is_VTOL() const;
    [[nodiscard]] bool is_flying() const;
    [[nodiscard]] bool is_radar_detector() const final;
    [[nodiscard]] bool is_stationary() const;
    [[nodiscard]] bool is_rearming() const;
    [[nodiscard]] bool is_damaged() const;
    [[nodiscard]] bool is_attacking() const noexcept;
    [[nodiscard]] bool is_VTOL_rearmed_and_repaired() const;
    [[nodiscard]] bool is_VTOL_empty() const;
    [[nodiscard]] bool is_VTOL_full() const;

    /**
     *
     * @param attacker
     * @param weapon_slot
     * @return
     */
    [[nodiscard]] bool is_valid_target(const ::Unit* attacker,
                                       int weapon_slot) const final;

    [[nodiscard]] bool has_commander() const;
    [[nodiscard]] bool has_standard_sensor() const;
    [[nodiscard]] bool has_CB_sensor() const;
    [[nodiscard]] bool has_electronic_weapon() const;
    void gain_experience(unsigned exp);
    void commander_gain_experience(unsigned exp) const;
    void move_to_rearm_pad();
    void cancel_build();
    void reset_action() noexcept;
    void update_expected_damage(unsigned damage, bool is_direct) noexcept final;
    [[nodiscard]] unsigned commander_max_group_size() const;
    [[nodiscard]] unsigned calculate_sensor_range() const final;
    [[nodiscard]] int calculate_height() const;
    [[nodiscard]] int space_occupied_on_transporter() const;
    void set_direct_route(int target_x, int target_y) const;
    void increment_kills() noexcept;
    void increment_commander_kills() const;
    void assign_vtol_to_rearm_pad(RearmPad* rearm_pad);
    [[nodiscard]] int calculate_electronic_resistance() const;
    [[nodiscard]] bool is_selectable() const final;
    [[nodiscard]] unsigned get_armour_points_against_weapon(WEAPON_CLASS weapon_class) const;
    [[nodiscard]] int calculate_attack_priority(const Unit* target, int weapon_slot) const final;
    [[nodiscard]] bool is_hovering() const;
private:
    std::string name;
    DROID_TYPE type;

    /** Holds the specifics for the component parts - allows damage
     *  per part to be calculated. Indexed by COMPONENT_TYPE.
     *  Weapons need to be dealt with separately.
     */
    uint8_t asBits[DROID_MAXCOMP];

    unsigned weight = 0;

    /// Base speed depends on propulsion type
    unsigned base_speed = 0;

    unsigned original_hp = 0;
    unsigned experience = 0;
    unsigned kills = 0;

    /// Set when stuck. Used for, e.g., firing indiscriminately
    /// at map features to clear the way
    unsigned lastFrustratedTime;

    int resistance_to_electric;

    DROID_GROUP* group;

    /// A structure that this droid might be associated with.
    /// For VTOLs this is the rearming pad
    Structure* associated_structure = nullptr;

    // Number of queued orders
    int listSize;

    /// The number of synchronised orders. Orders from `listSize` to
    /// the real end of the list may not affect game state.
    OrderList asOrderList;

    /// The range [0; listSize - 1] corresponds to synchronised orders, and the range
    /// [listPendingBegin; listPendingEnd - 1] corresponds to the orders that will
    /// remain, once all orders are synchronised.
    unsigned listPendingBegin;

    /// Index of first order which will not be erased by a pending order. After all
    /// messages are processed, the orders in the range [listPendingBegin; listPendingEnd - 1]
    /// will remain.
    std::unique_ptr<DROID_ORDER_DATA> order;

    unsigned secondary_order;

    /// What `secondary_order` will be after synchronisation.
    unsigned secondaryOrderPending;

    /// Number of pending `secondary_order` synchronisations.
    int secondaryOrderPendingCount;

    DROID_ACTION action;
    Vector2i actionPos;

    std::array<SimpleObject*, MAX_WEAPONS> action_target;
    std::size_t time_action_started;
    unsigned action_points_done;
    UDWORD expected_damage_direct = 0;
    UDWORD expected_damage_indirect = 0;
    UBYTE illumination_level;
    MOVE_CONTROL movement;

    /// The location of this droid in the previous tick.
    Spacetime previous_location;

    /// Bit set telling which tiles block this type of droid (TODO)
    uint8_t blockedBits;

    int iAudioID;
};

enum PICKTILE
{
	NO_FREE_TILE,
	FREE_TILE,
};

// the structure that was last hit
extern Droid* psLastDroidHit;

std::priority_queue<int> copy_experience_queue(int player);
void add_to_experience_queue(int player, int value);

// initialise droid module
bool droidInit();

bool removeDroidBase(Droid* psDel);

struct INITIAL_DROID_ORDERS
{
	uint32_t secondaryOrder;
	int32_t moveToX;
	int32_t moveToY;
	uint32_t factoryId;
};

/*Builds an instance of a Structure - the x/y passed in are in world coords.*/
/// Sends a GAME_DROID message if bMultiMessages is true, or actually creates it if false. Only uses initialOrders if sending a GAME_DROID message.
Droid* buildDroid(DroidTemplate* pTemplate, UDWORD x, UDWORD y, UDWORD player, bool onMission,
                  const INITIAL_DROID_ORDERS* initialOrders, Rotation rot = Rotation());
/// Creates a droid locally, instead of sending a message, even if the bMultiMessages HACK is set to true.
Droid* reallyBuildDroid(const DroidTemplate* pTemplate, Position pos, UDWORD player, bool onMission,
                        Rotation rot = Rotation());

/* Set the asBits in a DROID structure given it's template. */
void droidSetBits(const DroidTemplate* pTemplate, Droid* psDroid);

/* Calculate the weight of a droid from it's template */
UDWORD calcDroidWeight(const DroidTemplate* psTemplate);

/* Calculate the power points required to build/maintain a droid */
UDWORD calcDroidPower(const Droid* psDroid);

// Calculate the number of points required to build a droid
UDWORD calcDroidPoints(Droid* psDroid);

/* Calculate the body points of a droid from it's template */
UDWORD calcTemplateBody(const DroidTemplate* psTemplate, UBYTE player);

/* Calculate the base speed of a droid from it's template */
UDWORD calcDroidBaseSpeed(const DroidTemplate* psTemplate, UDWORD weight, UBYTE player);

/* Calculate the speed of a droid over a terrain */
UDWORD calcDroidSpeed(UDWORD baseSpeed, UDWORD terrainType, UDWORD propIndex, UDWORD level);

/* Calculate the points required to build the template */
UDWORD calcTemplateBuild(const DroidTemplate* psTemplate);

/* Calculate the power points required to build/maintain the droid */
UDWORD calcTemplatePower(const DroidTemplate* psTemplate);

// return whether a droid is IDF
bool idfDroid(Droid* psDroid);

/* Do damage to a droid */
int32_t droidDamage(Droid* psDroid, unsigned damage, WEAPON_CLASS weaponClass, WEAPON_SUBCLASS weaponSubClass,
                    unsigned impactTime, bool isDamagePerSecond, int minDamage);

/* The main update routine for all droids */
void droidUpdate(Droid* psDroid);

/* Set up a droid to build a structure - returns true if successful */
enum DroidStartBuild { DroidStartBuildFailed, DroidStartBuildSuccess, DroidStartBuildPending };

DroidStartBuild droidStartBuild(Droid* psDroid);

/* Update a construction droid while it is demolishing
   returns true while demolishing */
bool droidUpdateDemolishing(Droid* psDroid);

/* Sets a droid to start a generic action */
void droidStartAction(Droid* psDroid);

/* Update a construction droid while it is repairing
   returns true while repairing */
bool droidUpdateRepair(Droid* psDroid);

/*Updates a Repair Droid working on a damaged droid - returns true whilst repairing*/
bool droidUpdateDroidRepair(Droid* psRepairDroid);

/* Update a construction droid while it is building
   returns true while building continues */
bool droidUpdateBuild(Droid* psDroid);

/*continue restoring a structure*/
bool droidUpdateRestore(Droid* psDroid);

// recycle a droid (retain it's experience and some of it's cost)
void recycleDroid(Droid* psDel);

/* Remove a droid and free it's memory */
bool destroyDroid(Droid* psDel, unsigned impactTime);

/* Same as destroy droid except no graphical effects */
void vanishDroid(Droid* psDel);

/* Remove a droid from the apsDroidLists so doesn't update or get drawn etc*/
//returns true if successfully removed from the list
bool droidRemove(Droid* psDroid, Droid* pList[MAX_PLAYERS]);

//free the storage for the droid templates
bool droidTemplateShutDown();

/* Return the type of a droid */
DROID_TYPE droidType(Droid* psDroid);

/* Return the type of a droid from it's template */
DROID_TYPE droidTemplateType(const DroidTemplate* psTemplate);

void assignDroidsToGroup(UDWORD playerNumber, UDWORD groupNumber, bool clearGroup);
void removeDroidsFromGroup(UDWORD playerNumber);

bool activateNoGroup(UDWORD playerNumber, const SELECTIONTYPE selectionType, const SELECTION_CLASS selectionClass,
                     const bool bOnScreen);

bool activateGroup(UDWORD playerNumber, UDWORD groupNumber);

UDWORD getNumDroidsForLevel(uint32_t player, UDWORD level);

bool activateGroupAndMove(UDWORD playerNumber, UDWORD groupNumber);
/* calculate muzzle tip location in 3d world added int weapon_slot to fix the always slot 0 hack*/
bool calcDroidMuzzleLocation(const Droid* psDroid, Vector3i* muzzle, int weapon_slot);
/* calculate muzzle base location in 3d world added int weapon_slot to fix the always slot 0 hack*/
bool calcDroidMuzzleBaseLocation(const Droid* psDroid, Vector3i* muzzle, int weapon_slot);

/* Droid experience stuff */
unsigned int getDroidLevel(const Droid* psDroid);
UDWORD getDroidEffectiveLevel(const Droid* psDroid);
const char* getDroidLevelName(const Droid* psDroid);

// Get a droid's name.
const char* droidGetName(const Droid* psDroid);

// Set a droid's name.
void droidSetName(Droid* psDroid, const char* pName);

// returns true when no droid on x,y square.
bool noDroid(UDWORD x, UDWORD y); // true if no droid at x,y
// returns an x/y coord to place a droid
PICKTILE pickHalfATile(UDWORD* x, UDWORD* y, UBYTE numIterations);
bool zonedPAT(UDWORD x, UDWORD y);
bool pickATileGen(UDWORD* x, UDWORD* y, UBYTE numIterations, bool (*function)(UDWORD x, UDWORD y));
bool pickATileGen(Vector2i* pos, unsigned numIterations, bool (*function)(UDWORD x, UDWORD y));
bool pickATileGenThreat(UDWORD* x, UDWORD* y, UBYTE numIterations, SDWORD threatRange,
                        SDWORD player, bool (*function)(UDWORD x, UDWORD y));


//initialises the droid movement model
void initDroidMovement(Droid* psDroid);

/// Looks through the players list of droids to see if any of them are building the specified structure - returns true if finds one
bool checkDroidsBuilding(Structure* psStructure);

/// Looks through the players list of droids to see if any of them are demolishing the specified structure - returns true if finds one
bool checkDroidsDemolishing(Structure* psStructure);

/// Returns the next module which can be built after lastOrderedModule, or returns 0 if not possible.
int nextModuleToBuild(Structure const* psStruct, int lastOrderedModule);

/// Deals with building a module - checking if any droid is currently doing this if so, helping to build the current one
void setUpBuildModule(Droid* psDroid);

/// Just returns true if the droid's present body points aren't as high as the original
bool droidIsDamaged(const Droid* psDroid);

char const* getDroidResourceName(char const* pName);

/// Checks to see if an electronic warfare weapon is attached to the droid
bool electronicDroid(const Droid* psDroid);

/// checks to see if the droid is currently being repaired by another
bool droidUnderRepair(const Droid* psDroid);

/// Count how many Command Droids exist in the world at any one moment
UBYTE checkCommandExist(UBYTE player);

/// For a given repair droid, check if there are any damaged droids within a defined range
SimpleObject* checkForRepairRange(Droid* psDroid, Droid* psTarget);

// Returns true if the droid is a transporter.
bool isTransporter(Droid const* psDroid);
bool isTransporter(DroidTemplate const* psTemplate);
/// Returns true if the droid has VTOL propulsion, and is not a transport.
bool isVtolDroid(const Droid* psDroid);
/// Returns true if the droid has VTOL propulsion and is moving.
bool isFlying(const Droid* psDroid);
/*returns true if a VTOL weapon droid which has completed all runs*/
bool vtolEmpty(const Droid* psDroid);
/*returns true if a VTOL weapon droid which still has full ammo*/
bool vtolFull(const Droid* psDroid);
/*Checks a vtol for being fully armed and fully repaired to see if ready to
leave reArm pad */
bool vtolHappy(const Droid* psDroid);
/*checks if the droid is a VTOL droid and updates the attack runs as required*/
void updateVtolAttackRun(Droid* psDroid, int weapon_slot);
/*returns a count of the base number of attack runs for the weapon attached to the droid*/
UWORD getNumAttackRuns(const Droid* psDroid, int weapon_slot);
//assign rearmPad to the VTOL
void assignVTOLPad(Droid* psNewDroid, Structure* psReArmPad);
// true if a vtol is waiting to be rearmed by a particular rearm pad
bool vtolReadyToRearm(Droid* psDroid, Structure* psStruct);
// true if a vtol droid currently returning to be rearmed
bool vtolRearming(const Droid* psDroid);
// true if a droid is currently attacking
bool droidAttacking(const Droid* psDroid);
// see if there are any other vtols attacking the same target
// but still rearming
bool allVtolsRearmed(const Droid* psDroid);

/// Compares the droid sensor type with the droid weapon type to see if the FIRE_SUPPORT order can be assigned
bool droidSensorDroidWeapon(const SimpleObject* psObj, const Droid* psDroid);

/// Return whether a droid has a CB sensor on it
bool cbSensorDroid(const Droid* psDroid);

/// Return whether a droid has a standard sensor on it (standard, VTOL strike, or wide spectrum)
bool standardSensorDroid(const Droid* psDroid);

// give a droid from one player to another - used in Electronic Warfare and multiplayer
Droid* giftSingleDroid(Droid* psD, UDWORD to, bool electronic = false);

/// Calculates the electronic resistance of a droid based on its experience level
SWORD droidResistance(const Droid* psDroid);

/// This is called to check the weapon is allowed
bool checkValidWeaponForProp(DroidTemplate* psTemplate);

const char* getDroidNameForRank(UDWORD rank);

/*called when a Template is deleted in the Design screen*/
void deleteTemplateFromProduction(DroidTemplate* psTemplate, unsigned player, QUEUE_MODE mode);
// ModeQueue deletes from production queues, which are not yet synchronised. ModeImmediate deletes from current production which is synchronised.

// Check if a droid can be selected.
bool isSelectable(Droid const* psDroid);

// Select a droid and do any necessary housekeeping.
void SelectDroid(Droid* psDroid);

// De-select a droid and do any necessary housekeeping.
void DeSelectDroid(Droid* psDroid);

/* audio finished callback */
bool droidAudioTrackStopped(void* psObj);

/*returns true if droid type is one of the Cyborg types*/
bool cyborgDroid(const Droid* psDroid);

bool isConstructionDroid(Droid const* psDroid);
bool isConstructionDroid(SimpleObject const* psObject);

/** Check if droid is in a legal world position and is not on its way to drive off the map. */
bool droidOnMap(const Droid* psDroid);

void droidSetPosition(Droid* psDroid, int x, int y);

/// Return a percentage of how fully armed the object is, or -1 if N/A.
int droidReloadBar(const SimpleObject* psObj, const Weapon* psWeap, int weapon_slot);

static inline int droidSensorRange(const Droid* psDroid)
{
	return objSensorRange((const SimpleObject*)psDroid);
}

/*
 * Component stat helper functions
 */
static inline BodyStats* getBodyStats(const Droid* psDroid)
{
	return asBodyStats + psDroid->asBits[COMP_BODY];
}

static inline CommanderStats* getBrainStats(const Droid* psDroid)
{
	return asBrainStats + psDroid->asBits[COMP_BRAIN];
}

static inline PropulsionStats* getPropulsionStats(const Droid* psDroid)
{
	return asPropulsionStats + psDroid->asBits[COMP_PROPULSION];
}

static inline SensorStats* getSensorStats(const Droid* psDroid)
{
	return asSensorStats + psDroid->asBits[COMP_SENSOR];
}

static inline EcmStats* getECMStats(const Droid* psDroid)
{
	return asECMStats + psDroid->asBits[COMP_ECM];
}

static inline RepairStats* getRepairStats(const Droid* psDroid)
{
	return asRepairStats + psDroid->asBits[COMP_REPAIRUNIT];
}

static inline ConstructStats* getConstructStats(const Droid* psDroid)
{
	return asConstructStats + psDroid->asBits[COMP_CONSTRUCT];
}

static inline WeaponStats* getWeaponStats(const Droid* psDroid, int weapon_slot)
{
	return asWeaponStats + psDroid->asWeaps[weapon_slot].nStat;
}

static inline Rotation getInterpolatedWeaponRotation(const Droid* psDroid, int weaponSlot, uint32_t time)
{
	return interpolateRot(psDroid->asWeaps[weaponSlot].previous_rotation, psDroid->asWeaps[weaponSlot].rotation,
                        psDroid->previous_location.time, psDroid->time, time);
}

/** helper functions for future refcount patch **/

#define setDroidTarget(_psDroid, _psNewTarget) _setDroidTarget(_psDroid, _psNewTarget, __LINE__, __FUNCTION__)

static inline void _setDroidTarget(Droid* psDroid, SimpleObject* psNewTarget, int line, const char* func)
{
	psDroid->order.psObj = psNewTarget;
	ASSERT(psNewTarget == nullptr || !psNewTarget->died, "setDroidTarget: Set dead target");
	ASSERT(
		psNewTarget == nullptr || !psNewTarget->died || (psNewTarget->died == NOT_CURRENT_LIST && psDroid->died ==
			NOT_CURRENT_LIST),
		"setDroidTarget: Set dead target");
#ifdef DEBUG
	psDroid->targetLine = line;
	sstrcpy(psDroid->targetFunc, func);
#else
	// Prevent warnings about unused parameters
	(void)line;
	(void)func;
#endif
}

#define setDroidActionTarget(_psDroid, _psNewTarget, _idx) _setDroidActionTarget(_psDroid, _psNewTarget, _idx, __LINE__, __FUNCTION__)

static inline void _setDroidActionTarget(Droid* psDroid, SimpleObject* psNewTarget, UWORD idx, int line,
                                         const char* func)
{
	psDroid->action_target[idx] = psNewTarget;
	ASSERT(
		psNewTarget == nullptr || !psNewTarget->died || (psNewTarget->died == NOT_CURRENT_LIST && psDroid->died ==
			NOT_CURRENT_LIST),
		"setDroidActionTarget: Set dead target");
#ifdef DEBUG
	psDroid->actionTargetLine[idx] = line;
	sstrcpy(psDroid->actionTargetFunc[idx], func);
#else
	// Prevent warnings about unused parameters
	(void)line;
	(void)func;
#endif
}

#define setDroidBase(_psDroid, _psNewTarget) _setDroidBase(_psDroid, _psNewTarget, __LINE__, __FUNCTION__)

static inline void _setDroidBase(Droid* psDroid, Structure* psNewBase, int line, const char* func)
{
	psDroid->associated_structure = psNewBase;
	ASSERT(psNewBase == nullptr || !psNewBase->died, "setDroidBase: Set dead target");
#ifdef DEBUG
	psDroid->baseLine = line;
	sstrcpy(psDroid->baseFunc, func);
#else
	// Prevent warnings about unused parameters
	(void)line;
	(void)func;
#endif
}

static inline void setSaveDroidTarget(Droid* psSaveDroid, SimpleObject* psNewTarget)
{
	psSaveDroid->order.psObj = psNewTarget;
#ifdef DEBUG
	psSaveDroid->targetLine = 0;
	sstrcpy(psSaveDroid->targetFunc, "savegame");
#endif
}

static inline void setSaveDroidActionTarget(Droid* psSaveDroid, SimpleObject* psNewTarget, UWORD idx)
{
	psSaveDroid->action_target[idx] = psNewTarget;
#ifdef DEBUG
	psSaveDroid->actionTargetLine[idx] = 0;
	sstrcpy(psSaveDroid->actionTargetFunc[idx], "savegame");
#endif
}

static inline void setSaveDroidBase(Droid* psSaveDroid, Structure* psNewBase)
{
	psSaveDroid->associated_structure = psNewBase;
#ifdef DEBUG
	psSaveDroid->baseLine = 0;
	sstrcpy(psSaveDroid->baseFunc, "savegame");
#endif
}

void checkDroid(const Droid* droid, const char* const location_description, const char* function, const int recurse);

/** assert if droid is bad */
#define CHECK_DROID(droid) checkDroid(droid, AT_MACRO, __FUNCTION__, max_check_object_recursion)

/** If droid can get to given object using its current propulsion, return the square distance. Otherwise return -1. */
int droidSqDist(Droid* psDroid, SimpleObject* psObj);

// Minimum damage a weapon will deal to its target
#define	MIN_WEAPON_DAMAGE	1

void templateSetParts(const Droid* psDroid, DroidTemplate* psTemplate);

void cancelBuild(Droid* psDroid);

#define syncDebugDroid(psDroid, ch) _syncDebugDroid(__FUNCTION__, psDroid, ch)
void _syncDebugDroid(const char* function, Droid const* psDroid, char ch);


// True iff object is a droid.
static inline bool isDroid(SIMPLE_OBJECT const* psObject)
{
	return psObject != nullptr && psObject->type == OBJ_DROID;
}

// Returns DROID * if droid or NULL if not.
static inline Droid* castDroid(SIMPLE_OBJECT* psObject)
{
	return isDroid(psObject) ? (Droid*)psObject : (Droid*)nullptr;
}

// Returns DROID const * if droid or NULL if not.
static inline Droid const* castDroid(SIMPLE_OBJECT const* psObject)
{
	return isDroid(psObject) ? (Droid const*)psObject : (Droid const*)nullptr;
}


#endif // __INCLUDED_SRC_DROID_H__
