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

#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/sound/audio_id.h"
#include "wzmaplib/map.h"

#include "basedef.h"
#include "order.h"
#include "stats.h"
#include "weapon.h"

/* forward declarations */
struct Action;
struct Group;
struct Movement;
struct RearmPad;
struct RtrBestResult;
enum class FPATH_MOVETYPE;
enum class MOVE_STATUS;
enum class SELECTIONTYPE;
enum class SELECTION_CLASS;


static constexpr auto MAX_SPEED_PITCH = 60;

// Watermelon:fix these magic number...the collision radius
// should be based on pie imd radius not some static int's...
static constexpr auto mvPersRad = 20, mvCybRad = 30, mvSmRad = 40, mvMedRad = 50, mvLgRad = 60;

static constexpr auto PITCH_LIMIT = 150;

/// How long a droid runs after it fails do respond due to low morale
static constexpr auto RUN_TIME = 8000;

/// How long a droid runs burning after it fails to respond due to low morale
static constexpr auto RUN_BURN_TIME = 10000;

/// The distance a droid has in guard mode
static constexpr auto DEFEND_MAXDIST = TILE_UNITS * 3;

/// The distance a droid has in guard mode
static constexpr auto DEFEND_BASEDIST = TILE_UNITS * 3;

/**
 * The distance a droid has in guard mode. Equivalent to GUARD_MAXDIST,
 * but used for droids being on a command group
 */
static constexpr auto DEFEND_CMD_MAXDIST = TILE_UNITS * 8;

/**
 * The distance a droid has in guard mode. Equivalent to GUARD_BASEDIST,
 * but used for droids being on a command group
 */
static constexpr auto DEFEND_CMD_BASEDIST	= TILE_UNITS * 5;

/// The maximum distance a constructor droid has in guard mode
static constexpr auto CONSTRUCT_MAXDIST = TILE_UNITS * 8;

/**
 * The maximum distance allowed to a droid to move out of the
 * path on a patrol/scout
 */
static constexpr auto SCOUT_DIST = TILE_UNITS * 8;

/**
 * The maximum distance allowed to a droid to move out of the
 * path if already attacking a target on a patrol/scout
 */
static constexpr auto SCOUT_ATTACK_DIST	= TILE_UNITS * 5;

/// Percentage of body points remaining at which to repair droid automatically
static constexpr auto REPAIRLEV_LOW	= 50;
/// Ditto, but this will repair much sooner
static constexpr auto REPAIRLEV_HIGH = 75;

static constexpr auto DROID_RESISTANCE_FACTOR = 30;

/// Changing this breaks campaign saves!
static constexpr auto MAX_RECYCLED_DROIDS  = 450;

/// Used to stop structures being built too near the edge and droids being placed down
static constexpr auto TOO_NEAR_EDGE  = 3;

/// Damage of a droid is reduced by this value per experience level (in percent)
static constexpr auto EXP_REDUCE_DAMAGE  = 6;
/// Accuracy of a droid is increased by this value per experience level (in percent)
static constexpr auto EXP_ACCURACY_BONUS = 5;
/// Speed of a droid is increased by this value per experience level (in percent)
static constexpr auto EXP_SPEED_BONUS = 5;
// Minimum damage a weapon will deal to its target
static constexpr auto MIN_WEAPON_DAMAGE	 = 1;

/**
 * The number of components in the asParts / asBits arrays.
 * Weapons are stored separately, thus the maximum index into the array
 * is 1 smaller than the number of components
 */
static constexpr auto DROID_MAXCOMP = (int)COMPONENT_TYPE::COUNT -  1;

static constexpr auto DROID_DAMAGE_SCALING  = 400;


static const auto	DROID_DAMAGE_SPREAD = 16 - rand() % 32;
static const auto DROID_REPAIR_SPREAD=	20 - rand() % 40;

/// Store the experience of recently recycled droids
static std::priority_queue<int> recycled_experience[MAX_PLAYERS];

/// The height at which the transporter hovers above the terrain
static constexpr auto TRANSPORTER_HOVER_HEIGHT	= 10;

/// TODO This should really be logarithmic
#define CALC_DROID_SMOKE_INTERVAL(x) ((((100-(x))+10)/10) * DROID_DAMAGE_SCALING)

#define UNIT_LOST_DELAY	(5*GAME_TICKS_PER_SEC)

/// Defines how many times to perform the iteration on looking for a blank location
static constexpr auto LOOK_FOR_EMPTY_TILE  = 20;

/**
 * The droid's current action/order. This is used for
 * debugging purposes, jointly with showSAMPLES()
 */
extern char DROIDDOING[512];

/* Set up a droid to build a structure - returns true if successful */
enum DroidStartBuild
{
  DroidStartBuildFailed,
  DroidStartBuildSuccess,
  DroidStartBuildPending
};

enum class PICK_TILE
{
  NO_FREE_TILE,
  FREE_TILE
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

struct InitialOrders
{
  unsigned secondaryOrder;
  int moveToX;
  int moveToY;
  unsigned factoryId;
};

struct DroidTemplate : public BaseStats
{
  DroidTemplate() = default;

  [[nodiscard]] ComponentStats const* getComponent(COMPONENT_TYPE compName) const;

  unsigned id = 0;
  std::unordered_map<COMPONENT_TYPE, ComponentStats> components;
  std::array<Weapon, MAX_WEAPONS> weapons;
  DROID_TYPE type = DROID_TYPE::ANY;

  /// Not player designed, not saved, never delete or change
  bool isPrefab = false;

  bool isStored = false;
  bool isEnabled = false;
};

class Droid : public BaseObject
{
public:
  ~Droid() override;
  Droid(unsigned id, Player* player);

  Droid(Droid const& rhs);
  Droid& operator=(Droid const& rhs);

  Droid(Droid&& rhs) noexcept = default;
  Droid& operator=(Droid&& rhs) noexcept = default;


  [[nodiscard]] int objRadius() const override;
  [[nodiscard]] ANIMATION_EVENTS getAnimationEvent() const;
  [[nodiscard]] unsigned getBaseSpeed() const;
  [[nodiscard]] ACTION getAction() const noexcept;
  [[nodiscard]] Order const* getOrder() const;
  [[nodiscard]] DROID_TYPE getType() const noexcept;
  [[nodiscard]] Vector2i getActionPos() const;
  [[nodiscard]] unsigned getLevel() const;
  [[nodiscard]] Droid const* getCommander() const;
  [[nodiscard]] unsigned getCommanderLevel() const;
  [[nodiscard]] int getVerticalSpeed() const noexcept;
  [[nodiscard]] unsigned getSecondaryOrder() const noexcept;
  [[nodiscard]] Vector2i getDestination() const;
  [[nodiscard]] Movement const* getMovementData() const;
  [[nodiscard]] unsigned getLastFrustratedTime() const;
  [[nodiscard]] unsigned getWeight() const;
  [[nodiscard]] BaseObject const* getTarget(int idx) const override;
  [[nodiscard]] Group const* getGroup() const;
  [[nodiscard]] Structure const* getBase() const;
  [[nodiscard]] ComponentStats const* getComponent(COMPONENT_TYPE compName) const;
  [[nodiscard]] unsigned getTimeActionStarted() const;
  [[nodiscard]] unsigned getExperience() const;
  [[nodiscard]] unsigned getKills() const;
  [[nodiscard]] int getAudioId() const;
  [[nodiscard]] bool hasElectronicWeapon() const;
  [[nodiscard]] bool isVtol() const;
  [[nodiscard]] bool isFlying() const;
  [[nodiscard]] bool isDamaged() const;
  [[nodiscard]] bool hasCommander() const;
  [[nodiscard]] bool isStationary() const;
  [[nodiscard]] bool isRepairDroid() const noexcept;
  [[nodiscard]] std::string getDroidLevelName() const;
  [[nodiscard]] unsigned calculateSensorRange() const;
  [[nodiscard]] int spaceOccupiedOnTransporter() const;
  [[nodiscard]] bool isAttacking() const noexcept;
  [[nodiscard]] int calculateElectronicResistance() const;
  [[nodiscard]] bool isRadarDetector() const override;
  [[nodiscard]] bool hasStandardSensor() const;
  [[nodiscard]] bool hasCbSensor() const override;
  [[nodiscard]] int droidDamage(unsigned damage, WEAPON_CLASS weaponClass, WEAPON_SUBCLASS weaponSubClass,
                                unsigned impactTime, bool isDPS, int minDamage);
  void orderDroidCmd(ORDER_TYPE order, QUEUE_MODE mode);
  void setAudioId(int audio);
  void setUpBuildModule();
  void moveToRearm();
  void actionUpdateDroid();
  void initVisibility();
  bool loadSaveDroid(const char* pFileName);
  void actionSanity();
  void fpathSetDirectRoute(int targetX, int targetY);
  void actionUpdateTransporter();
  void setOrder(std::unique_ptr<Order> order);
  void setAction(ACTION action);
  void setActionTarget(BaseObject* psNewTarget, unsigned idx);
  void setTarget(BaseObject* psNewTarget);
  void setBase(Structure* psNewBase);
  void cancelBuild();
  void resetAction() noexcept;
  void gainExperience(unsigned exp);
  void upgradeHitPoints();
  bool moveDroidToBase(unsigned x, unsigned y, bool bFormation, FPATH_MOVETYPE moveType);
  void moveDroidToDirect(unsigned x, unsigned y);
  void moveTurnDroid(unsigned x, unsigned y);
  void moveShuffleDroid(Vector2i s);
  bool secondarySetState(SECONDARY_ORDER sec, SECONDARY_STATE state, QUEUE_MODE mode = ModeQueue);
  void actionDroidBase(Action* psAction);
  RtrBestResult decideWhereToRepairAndBalance();
  SECONDARY_STATE secondaryGetState(SECONDARY_ORDER sec, QUEUE_MODE mode = ModeImmediate);
  void orderDroidAdd(Order* order_);
  void orderDroidAddPending(Order* order_);
  void orderCheckList();
  void orderDroidBase(Order* psOrder);
  void incrementKills() noexcept;
  bool tryDoRepairlikeAction();
  void assignVtolToRearmPad(RearmPad* rearmPad);
  void orderUpdateDroid();
  std::unique_ptr<Droid> reallyBuildDroid(DroidTemplate const* pTemplate, Position pos,
                                          unsigned player, bool onMission, Rotation rot);
  void droidUpdate();
  [[nodiscard]] unsigned getArmourPointsAgainstWeapon(WEAPON_CLASS weaponClass) const;
  DroidStartBuild droidStartBuild();
  void aiUpdateDroid();
  bool droidUpdateRestore();
  bool droidUpdateDroidRepair();
  bool droidUpdateBuild();
  void recycleDroid();
  void initDroidMovement();
  std::unique_ptr<Droid> giftSingleDroid(unsigned to, bool electronic);
  void droidSetBits(DroidTemplate const* pTemplate);
  void orderDroidListEraseRange(int indexBegin, int indexEnd);
  void orderClearTargetFromDroidList(BaseObject const* psTarget);
  void removeDroidBase();
  void setOrderTarget(BaseObject* target);
  void setGroup(Group* group);
  void removeDroidFromGroup(Droid* droid);
  void orderCheckGuardPosition(int range);
  bool orderDroidList();
  void moveStopDroid();
  void moveReallyStopDroid();
  void updateDroidOrientation();
  int moveDirectPathToWaypoint(unsigned positionIndex);
  bool moveBestTarget();
  bool moveNextTarget();
  bool moveBlocked();
  void moveCalcBlockingSlide(int* pmx, int* pmy, uint16_t tarDir, uint16_t* pSlideDir);
  void movePlayAudio(bool bStarted, bool bStoppedBefore, int iMoveSpeed);
  void movePlayDroidMoveAudio();
  void moveDescending();
  void moveAdjustVtolHeight(int32_t iMapHeight);
  Vector2i moveGetObstacleVector(Vector2i dest);
  int moveCalcDroidSpeed();
  void moveCombineNormalAndPerpSpeeds(int fNormalSpeed, int fPerpSpeed, uint16_t iDroidDir);
  void moveUpdateGroundModel(int speed, uint16_t direction);
  void moveCalcDroidSlide(int* pmx, int* pmy);
  void moveUpdatePersonModel(int speed, uint16_t direction);
  void moveUpdateVtolModel(int speed, uint16_t direction);
  void moveUpdateCyborgModel(int moveSpeed, uint16_t moveDir, MOVE_STATUS oldStatus);
  void moveUpdateDroid();
  void updateExpectedDamage(unsigned damage, bool isDirect) noexcept;
  bool droidUpdateDemolishing();
  bool droidSensorDroidWeapon(BaseObject const* psObj) const;
private:
  friend class Group;
  struct Impl;
  std::unique_ptr<Impl> pimpl;
};


// the structure that was last hit
extern Droid* psLastDroidHit;

std::priority_queue<int> copy_experience_queue(unsigned player);
void add_to_experience_queue(unsigned player, int value);

// initialise droid module
bool droidInit();

bool removeDroidBase(Droid* psDel);

/*Builds an instance of a Structure - the x/y passed in are in world coords.*/
/// Sends a GAME_DROID message if bMultiMessages is true, or actually creates it if false. Only uses initialOrders if sending a GAME_DROID message.
std::unique_ptr<Droid> buildDroid(DroidTemplate* pTemplate, unsigned x, unsigned y, unsigned player, bool onMission,
                                  const InitialOrders* initialOrders, Rotation rot = Rotation());
/// Creates a droid locally, instead of sending a message, even if the bMultiMessages HACK is set to true.
std::unique_ptr<Droid> reallyBuildDroid(const DroidTemplate* pTemplate, Position pos, unsigned player, bool onMission,
                        Rotation rot = Rotation());

/* Set the asBits in a DROID structure given it's template. */
void droidSetBits(const DroidTemplate* pTemplate, Droid* psDroid);

/* See if a droid is next to a structure */
static bool droidNextToStruct(Droid* psDroid, Structure* psStruct);

static bool droidBuildStartAudioCallback(void* psObj);

static void addConstructorEffect(Structure* psStruct);

/* Calculate the weight of a droid from it's template */
unsigned calcDroidWeight(const DroidTemplate* psTemplate);

/* Calculate the power points required to build/maintain a droid */
int calcDroidPower(const Droid* psDroid);

// Calculate the number of points required to build a droid
unsigned calcDroidPoints(Droid* psDroid);

/* Calculate the body points of a droid from it's template */
unsigned calcTemplateBody(const DroidTemplate* psTemplate, UBYTE player);

/* Calculate the base speed of a droid from it's template */
unsigned calcDroidBaseSpeed(const DroidTemplate* psTemplate, unsigned weight, UBYTE player);

/* Calculate the speed of a droid over a terrain */
unsigned calcDroidSpeed(unsigned baseSpeed, unsigned terrainType, PropulsionStats const* propulsion, unsigned level);

/* Calculate the points required to build the template */
unsigned calcTemplateBuild(const DroidTemplate* psTemplate);

/* Calculate the power points required to build/maintain the droid */
int calcTemplatePower(const DroidTemplate* psTemplate);

// return whether a droid is IDF
bool isIdf(Droid* psDroid);

/* The main update routine for all droids */
void droidUpdate(Droid* psDroid);


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
bool droidRemove(Droid* psDroid);

//free the storage for the droid templates
bool droidTemplateShutDown();

/* Return the type of a droid */
DROID_TYPE droidType(Droid* psDroid);

/* Return the type of a droid from it's template */
DROID_TYPE droidTemplateType(const DroidTemplate* psTemplate);

void assignDroidsToGroup(unsigned playerNumber, unsigned groupNumber, bool clearGroup);
void removeDroidsFromGroup(unsigned playerNumber);

bool activateNoGroup(unsigned playerNumber, SELECTIONTYPE selectionType,
                     SELECTION_CLASS selectionClass, bool bOnScreen);

bool activateGroup(unsigned playerNumber, unsigned groupNumber);

unsigned getNumDroidsForLevel(uint32_t player, unsigned level);

bool activateGroupAndMove(unsigned playerNumber, unsigned groupNumber);
/* calculate muzzle tip location in 3d world added int weapon_slot to fix the always slot 0 hack*/
bool calcDroidMuzzleLocation(const Droid* psDroid, Vector3i* muzzle, int weapon_slot);
/* calculate muzzle base location in 3d world added int weapon_slot to fix the always slot 0 hack*/
bool calcDroidMuzzleBaseLocation(const Droid* psDroid, Vector3i* muzzle, int weapon_slot);

/* Droid experience stuff */
unsigned int getDroidLevel(const Droid* psDroid);
unsigned getDroidEffectiveLevel(const Droid* psDroid);
std::string getDroidLevelName(const Droid* psDroid);

// Get a droid's name.
const char* droidGetName(const Droid* psDroid);

// Set a droid's name.
void droidSetName(Droid* psDroid, const char* pName);

// returns true when no droid on x,y square.
bool noDroid(unsigned x, unsigned y); // true if no droid at x,y
// returns an x/y coord to place a droid
PICK_TILE pickHalfATile(unsigned* x, unsigned* y, UBYTE numIterations);
bool zonedPAT(unsigned x, unsigned y);
bool pickATileGen(unsigned* x, unsigned* y, UBYTE numIterations, bool (*function)(unsigned x, unsigned y));
bool pickATileGen(Vector2i* pos, unsigned numIterations, bool (*function)(unsigned x, unsigned y));
bool pickATileGenThreat(unsigned* x, unsigned* y, UBYTE numIterations, SDWORD threatRange,
                        SDWORD player, bool (*function)(unsigned x, unsigned y));


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
bool electronicDroid(Droid const* psDroid);

/// checks to see if the droid is currently being repaired by another
bool droidUnderRepair(Droid const* psDroid);

/// Count how many Command Droids exist in the world at any one moment
UBYTE checkCommandExist(UBYTE player);

/// For a given repair droid, check if there are any damaged droids within a defined range
BaseObject* checkForRepairRange(Droid* psDroid, Droid* psTarget);

/// @return `true` if the droid is a transporter
[[nodiscard]] bool isTransporter(Droid const& psDroid);
[[nodiscard]] bool isTransporter(DroidTemplate const& templ);

/// Returns true if the droid has VTOL propulsion and is moving.
bool isFlying(Droid const* psDroid);

/// @return true if a VTOL weapon droid which has completed all runs
bool vtolEmpty(Droid const& psDroid);

/// @return true if a VTOL weapon droid which still has full ammo
bool vtolFull(Droid const& psDroid);

/**
 * Checks a vtol for being fully armed and fully repaired
 * to see if ready to leave the rearm pad
 */
bool vtolHappy(Droid const& droid);

/**
 * Checks if the droid is a VTOL droid and updates the
 * attack runs as required
 */
void updateVtolAttackRun(Droid& droid, int weapon_slot);

/*returns a count of the base number of attack runs for the weapon attached to the droid*/
unsigned getNumAttackRuns(Droid const* psDroid, int weapon_slot);

//assign rearmPad to the VTOL
void assignVTOLPad(Droid* psNewDroid, Structure* psReArmPad);

/// @return true if a vtol is waiting to be rearmed by a particular rearm pad
bool vtolReadyToRearm(Droid& droid, RearmPad const& rearmPad);

/// @return true if a vtol droid is currently returning to be rearmed
[[nodiscard]] bool vtolRearming(Droid const& droid);

// true if a droid is currently attacking
bool droidAttacking(Droid const* psDroid);
// see if there are any other vtols attacking the same target
// but still rearming
bool allVtolsRearmed(Droid const* psDroid);

/// Compares the droid sensor type with the droid weapon type to see if the FIRE_SUPPORT order can be assigned
bool droidSensorDroidWeapon(BaseObject const* psObj, Droid const* psDroid);

// give a droid from one player to another - used in Electronic Warfare and multiplayer
Droid* giftSingleDroid(Droid* psD, unsigned to, bool electronic = false);

/// Calculates the electronic resistance of a droid based on its experience level
SWORD droidResistance(Droid const* psDroid);

/// This is called to check the weapon is allowed
bool checkValidWeaponForProp(DroidTemplate* psTemplate);

const char* getDroidNameForRank(unsigned rank);

/*called when a Template is deleted in the Design screen*/
void deleteTemplateFromProduction(DroidTemplate* psTemplate, unsigned player, QUEUE_MODE mode);
// ModeQueue deletes from production queues, which are not yet synchronised. ModeImmediate deletes from current production which is synchronised.

// Select a droid and do any necessary housekeeping.
void SelectDroid(Droid* psDroid);

// De-select a droid and do any necessary housekeeping.
void DeSelectDroid(Droid* psDroid);

/* audio finished callback */
bool droidAudioTrackStopped(Droid* psDroid);

void set_blocking_flags(const Droid& droid);
void clear_blocking_flags(const Droid& droid);

/*returns true if droid type is one of the Cyborg types*/
bool isCyborg(const Droid* psDroid);

bool isConstructionDroid(Droid const* psDroid);

/** Check if droid is in a legal world position and is not on its way to drive off the map. */
bool droidOnMap(const Droid* psDroid);

void droidSetPosition(Droid* psDroid, int x, int y);

/// Return a percentage of how fully armed the object is, or -1 if N/A.
int droidReloadBar(BaseObject const* psObj, Weapon const* psWeap, int weapon_slot);

/** If droid can get to given object using its current propulsion, return the square distance. Otherwise return -1. */
int droidSqDist(Droid* psDroid, BaseObject* psObj);


void templateSetParts(Droid const* psDroid, DroidTemplate* psTemplate);

static unsigned droidSensorRange(Droid const* psDroid);

static bool droidUpdateDroidRepairBase(Droid* psRepairDroid, Droid* psDroidToRepair);
static Rotation getInterpolatedWeaponRotation(Droid const* psDroid, int weaponSlot, unsigned time);
bool vtolCanLandHere(int x, int y);

#endif // __INCLUDED_SRC_DROID_H__
