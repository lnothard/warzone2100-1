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
 * @file droid.h
 * Type definitions and interface for droids
 */

#ifndef __INCLUDED_SRC_DROID_H__
#define __INCLUDED_SRC_DROID_H__

#include <queue>

#include "group.h"
#include "move.h"
#include "order.h"
#include "stats.h"
#include "unit.h"
#include "action.h"

/// world->screen check - alex
static constexpr auto OFF_SCREEN = 9999;

// Percentage of body points remaining at which to repair droid automatically.
static constexpr auto REPAIRLEV_LOW	= 50;
// Ditto, but this will repair much sooner.
static constexpr auto REPAIRLEV_HIGH = 75;

static constexpr auto DROID_RESISTANCE_FACTOR = 30;

/// Changing this breaks campaign saves!
static constexpr auto MAX_RECYCLED_DROIDS  = 450;

/// Used to stop structures being built too near the edge and droids
/// being placed down
static constexpr auto TOO_NEAR_EDGE  = 3;

/* Experience modifiers */

/// Damage of a droid is reduced by this value per experience level (in percent)
static constexpr auto EXP_REDUCE_DAMAGE  = 6;
/// Accuracy of a droid is increased by this value per experience level (in percent)
static constexpr auto EXP_ACCURACY_BONUS = 5;
/// Speed of a droid is increased by this value per experience level (in percent)
static constexpr auto EXP_SPEED_BONUS = 5;

/**
 * The number of components in the asParts / asBits arrays.
 * Weapons are stored separately, thus the maximum index into the array
 * is 1 smaller than the number of components.
 */
static constexpr auto DROID_MAXCOMP = COMP_NUMCOMPONENTS -  1;

/// The maximum number of droid weapons
static constexpr auto DROID_DAMAGE_SCALING  = 400;

static constexpr auto DEFAULT_RECOIL_TIME	= GAME_TICKS_PER_SEC / 4;

static const auto	DROID_DAMAGE_SPREAD = 16 - rand() % 32;
static const auto DROID_REPAIR_SPREAD=	20 - rand() % 40;

/// Store the experience of recently recycled droids
static std::priority_queue<int> recycled_experience[MAX_PLAYERS];

/// Height the transporter hovers at above the terrain
static constexpr auto TRANSPORTER_HOVER_HEIGHT	= 10;

/// TODO This should really be logarithmic
#define CALC_DROID_SMOKE_INTERVAL(x) ((((100-(x))+10)/10) * DROID_DAMAGE_SCALING)

/// Defines how many times to perform the iteration on looking for a blank location
static constexpr auto LOOK_FOR_EMPTY_TILE  = 20;

enum class PICKTILE
{
    NO_FREE_TILE,
    FREE_TILE,
};

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
    CIRCLE,
    COUNT // MUST BE LAST
};

struct INITIAL_DROID_ORDERS
{
    unsigned secondaryOrder;
    int moveToX;
    int moveToY;
    unsigned factoryId;
};

struct DroidTemplate : public BaseStats
{
    DroidTemplate() = default;

    using enum DROID_TYPE;
    unsigned id = 0;
    unsigned weaponCount = 0;
    DROID_TYPE type = ANY;

    /// Not player designed, not saved, never delete or change
    bool isPrefab = false;

    bool isStored = false;
    bool isEnabled = false;
};

class Droid : public virtual Unit
{
public:
    ~Droid() override = default;

    /* Accessors */
    [[nodiscard]] virtual ACTION getAction() const noexcept = 0;
    [[nodiscard]] virtual const Order& getOrder() const = 0;
    [[nodiscard]] virtual DROID_TYPE getType() const noexcept = 0;
    [[nodiscard]] virtual unsigned getLevel() const = 0;
    [[nodiscard]] virtual unsigned getCommanderLevel() const = 0;
    [[nodiscard]] virtual int getVerticalSpeed() const noexcept = 0;
    [[nodiscard]] virtual unsigned getSecondaryOrder() const noexcept = 0;
    [[nodiscard]] virtual const Vector2i& getDestination() const = 0;
    [[nodiscard]] virtual const std::optional<PropulsionStats>& getPropulsion() const = 0;

    [[nodiscard]] virtual bool hasElectronicWeapon() const = 0;
    [[nodiscard]] virtual bool isVtol() const = 0;
    [[nodiscard]] virtual bool isFlying() const = 0;
    [[nodiscard]] virtual bool isDamaged() const = 0;
    [[nodiscard]] virtual bool hasCommander() const = 0;
    virtual void cancelBuild() = 0;
    virtual void resetAction() = 0;
    virtual void gainExperience(unsigned exp) = 0;
    [[nodiscard]] virtual int calculateHeight() const = 0;
    [[nodiscard]] virtual bool isStationary() const = 0;
    virtual void upgradeHitPoints() = 0;
};

namespace Impl
{
  class Droid : public virtual ::Droid, public Impl::Unit
  {
  public:
      ~Droid() override = default;

      Droid(unsigned id, unsigned player);

      /* Accessors */
      [[nodiscard]] ACTION getAction() const noexcept final;
      [[nodiscard]] const Order& getOrder() const final;
      [[nodiscard]] DROID_TYPE getType() const noexcept final;
      [[nodiscard]] unsigned getLevel() const final;
      [[nodiscard]] unsigned getCommanderLevel() const final;
      [[nodiscard]] const iIMDShape& getImdShape() const final;
      [[nodiscard]] int getVerticalSpeed() const noexcept final;
      [[nodiscard]] unsigned getSecondaryOrder() const noexcept final;
      [[nodiscard]] const Vector2i& getDestination() const final;
      [[nodiscard]] const SimpleObject& getTarget(int weapon_slot) const final;
      [[nodiscard]] const std::optional<PropulsionStats>& getPropulsion() const final;

      [[nodiscard]] bool isProbablyDoomed(bool is_direct_damage) const;
      [[nodiscard]] bool isVtol() const final;
      [[nodiscard]] bool isFlying() const final;
      [[nodiscard]] bool isRadarDetector() const final;
      [[nodiscard]] bool isStationary() const final;
      [[nodiscard]] bool isDamaged() const final;
      [[nodiscard]] bool isAttacking() const noexcept;
      void upgradeHitPoints();

      void setUpBuildModule();

      void actionDroidBase(Action* psAction);

      int droidDamage(unsigned damage, WEAPON_CLASS weaponClass,
                      WEAPON_SUBCLASS weaponSubClass, unsigned impactTime,
                      bool isDamagePerSecond, int minDamage);

      /**
       *
       * @param attacker
       * @param weapon_slot
       * @return
       */
      [[nodiscard]] bool isValidTarget(const ::Unit *attacker,
                                       int weapon_slot) const final;

      [[nodiscard]] bool hasCommander() const final;
      [[nodiscard]] bool hasStandardSensor() const final;
      [[nodiscard]] bool hasCbSensor() const final;
      [[nodiscard]] bool hasElectronicWeapon() const final;
      void actionUpdateTransporter();
      void actionUpdateDroid();
      void gainExperience(unsigned exp) final;
      void cancelBuild() final;
      void resetAction() noexcept final;
      void actionSanity();
      void updateExpectedDamage(unsigned damage, bool is_direct) noexcept final;
      [[nodiscard]] unsigned calculateSensorRange() const final;
      [[nodiscard]] int calculateHeight() const final;
      [[nodiscard]] int spaceOccupiedOnTransporter() const;
      void incrementKills() noexcept;
      void assignVtolToRearmPad(RearmPad *rearmPad);
      [[nodiscard]] int calculateElectronicResistance() const;
      [[nodiscard]] bool isSelectable() const final;
      [[nodiscard]] unsigned getArmourPointsAgainstWeapon(WEAPON_CLASS weaponClass) const;
      [[nodiscard]] int calculateAttackPriority(const ::Unit *target, int weapon_slot) const final;

  private:
      using enum DROID_TYPE;
      using enum ACTION;
      std::string name;
      DROID_TYPE type;

      unsigned weight = 0;

      /// Base speed depends on propulsion type
      unsigned baseSpeed = 0;

      unsigned originalHp = 0;
      unsigned experience = 0;
      unsigned kills = 0;

      /// Set when stuck. Used for, e.g., firing indiscriminately
      /// at map features to clear the way
      unsigned lastFrustratedTime;

      int resistance_to_electric;

      std::shared_ptr<Group> group;

      /// A structure that this droid might be associated with.
      /// For VTOLs this is the rearming pad
      Structure *associatedStructure = nullptr;

      // Number of queued orders
      int listSize;

      /// The number of synchronised orders. Orders from `listSize` to
      /// the real end of the list may not affect game state.
      std::vector<Order> asOrderList;

      /// The range [0; listSize - 1] corresponds to synchronised orders, and the range
      /// [listPendingBegin; listPendingEnd - 1] corresponds to the orders that will
      /// remain, once all orders are synchronised.
      unsigned listPendingBegin;

      /// Index of first order which will not be erased by
      /// a pending order. After all messages are processed
      /// the orders in the range [listPendingBegin; listPendingEnd - 1]
      /// will remain.
      std::unique_ptr<Order> order;

      unsigned secondaryOrder;

      /// What `secondary_order` will be after synchronisation.
      unsigned secondaryOrderPending;

      /// Number of pending `secondary_order` synchronisations.
      int secondaryOrderPendingCount;

      ACTION action = NONE;
      Vector2i actionPos;
      std::array<SimpleObject*, MAX_WEAPONS> actionTarget;
      std::size_t timeActionStarted;
      std::size_t timeLastHit;
      unsigned actionPointsDone;
      unsigned expectedDamageDirect = 0;
      unsigned expectedDamageIndirect = 0;
      uint8_t illuminationLevel;
      std::unique_ptr<Movement> movement;

      /* Animation stuff */
      std::size_t timeAnimationStarted;
      ANIMATION_EVENTS animationEvent;

      /// The location of this droid in the previous tick.
      Spacetime previousLocation;

      /// Bit set telling which tiles block this type of droid (TODO)
      uint8_t blockedBits;

      int iAudioID;

      WEAPON_SUBCLASS lastHitWeapon;

      std::unique_ptr<BodyStats> body;
      std::optional<PropulsionStats> propulsion;
      std::optional<SensorStats> sensor;
      std::optional<EcmStats> ecm;
      std::optional<CommanderStats> brain;
  };
}
// the structure that was last hit
extern Droid* psLastDroidHit;

std::priority_queue<int> copy_experience_queue(int player);
void add_to_experience_queue(int player, int value);

// initialise droid module
bool droidInit();

bool removeDroidBase(Droid* psDel);

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
bool isIdf(Droid* psDroid);

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

/// @return `true` if the droid is a transporter
[[nodiscard]] bool isTransporter(const Droid& psDroid);

/// Returns true if the droid has VTOL propulsion and is moving.
bool isFlying(const Droid* psDroid);

/// @return true if a VTOL weapon droid which has completed all runs
bool vtolEmpty(const Droid& psDroid);

/// @return true if a VTOL weapon droid which still has full ammo
bool vtolFull(const Droid& psDroid);

/**
 * Checks a vtol for being fully armed and fully repaired
 * to see if ready to leave the rearm pad
 */
bool vtolHappy(const Droid& droid);

/**
 * Checks if the droid is a VTOL droid and updates the
 * attack runs as required
 */
void updateVtolAttackRun(Droid& droid, int weapon_slot);

/*returns a count of the base number of attack runs for the weapon attached to the droid*/
unsigned getNumAttackRuns(const Droid* psDroid, int weapon_slot);

//assign rearmPad to the VTOL
void assignVTOLPad(Droid* psNewDroid, Structure* psReArmPad);

/// @return true if a vtol is waiting to be rearmed by a particular rearm pad
bool vtolReadyToRearm(Droid& droid, const RearmPad& rearmPad);

/// @return true if a vtol droid is currently returning to be rearmed
[[nodiscard]] bool vtolRearming(const Droid& droid);

// true if a droid is currently attacking
bool droidAttacking(const Droid* psDroid);
// see if there are any other vtols attacking the same target
// but still rearming
bool allVtolsRearmed(const Droid* psDroid);

/// Compares the droid sensor type with the droid weapon type to see if the FIRE_SUPPORT order can be assigned
bool droidSensorDroidWeapon(const SimpleObject* psObj, const Droid* psDroid);

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

// Select a droid and do any necessary housekeeping.
void SelectDroid(Droid* psDroid);

// De-select a droid and do any necessary housekeeping.
void DeSelectDroid(Droid* psDroid);

/* audio finished callback */
bool droidAudioTrackStopped(void* psObj);

/*returns true if droid type is one of the Cyborg types*/
bool isCyborg(const Droid* psDroid);

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

/** If droid can get to given object using its current propulsion, return the square distance. Otherwise return -1. */
int droidSqDist(Droid* psDroid, SimpleObject* psObj);

// Minimum damage a weapon will deal to its target
static constexpr auto MIN_WEAPON_DAMAGE	 = 1;

void templateSetParts(const Droid* psDroid, DroidTemplate* psTemplate);

#define syncDebugDroid(psDroid, ch) _syncDebugDroid(__FUNCTION__, psDroid, ch)
void _syncDebugDroid(const char* function, Droid const* psDroid, char ch);

#endif // __INCLUDED_SRC_DROID_H__
