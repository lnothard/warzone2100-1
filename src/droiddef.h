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
/** \file
 *  Definitions for droids.
 */

#ifndef __INCLUDED_DROIDDEF_H__
#define __INCLUDED_DROIDDEF_H__

#include <vector>

#include "basedef.h"
#include "movedef.h"
#include "orderdef.h"
#include "statsdef.h"
#include "stringdef.h"
#include "unitdef.h"
#include "weapondef.h"

/*!
 * The number of components in the asParts / asBits arrays.
 * Weapons are stored separately, thus the maximum index into the array
 * is 1 smaller than the number of components.
 */
#define DROID_MAXCOMP (COMP_NUMCOMPONENTS - 1)

/* The maximum number of droid weapons */
#define	DROID_DAMAGE_SCALING	400

// This should really be logarithmic
#define	CALC_DROID_SMOKE_INTERVAL(x) ((((100-x)+10)/10) * DROID_DAMAGE_SCALING)

//defines how many times to perform the iteration on looking for a blank location
#define LOOK_FOR_EMPTY_TILE		20

/**
 * What a droid is currently doing. Not necessarily the same as its order as the micro-AI may get a droid to do
 * something else whilst carrying out an order.
 */
enum class DROID_ACTION
{
  NONE,					///< 0 not doing anything
  MOVE,					///< 1 moving to a location
  BUILD,					///< 2 building a structure
  DEMOLISH,				///< 4 demolishing a structure
  REPAIR,					///< 5 repairing a structure
  ATTACK,					///< 6 attacking something
  OBSERVE,				///< 7 observing something
  FIRESUPPORT,				///< 8 attacking something visible by a sensor droid
  SULK,					///< 9 refuse to do anything aggressive for a fixed time
  TRANSPORTOUT,				///< 11 move transporter offworld
  TRANSPORTWAITTOFLYIN,			///< 12 wait for timer to move reinforcements in
  TRANSPORTIN,				///< 13 move transporter onworld
  DROIDREPAIR,				///< 14 repairing a droid
  RESTORE,				///< 15 restore resistance points of a structure

  // The states below are used by the action system
  // but should not be given as an action
  MOVEFIRE,				///< 17
  MOVETOBUILD,				///< 18 moving to a new building location
  MOVETODEMOLISH,				///< 19 moving to a new demolition location
  MOVETOREPAIR,				///< 20 moving to a new repair location
  BUILDWANDER,				///< 21 moving around while building
  MOVETOATTACK,				///< 23 moving to a target to attack
  ROTATETOATTACK,				///< 24 rotating to a target to attack
  MOVETOOBSERVE,				///< 25 moving to be able to see a target
  WAITFORREPAIR,				///< 26 waiting to be repaired by a facility
  MOVETOREPAIRPOINT,			///< 27 move to repair facility repair point
  WAITDURINGREPAIR,			///< 28 waiting to be repaired by a facility
  MOVETODROIDREPAIR,			///< 29 moving to a new location next to droid to be repaired
  MOVETORESTORE,				///< 30 moving to a low resistance structure
  MOVETOREARM,				///< 32 moving to a rearming pad - VTOLS
  WAITFORREARM,				///< 33 waiting for rearm - VTOLS
  MOVETOREARMPOINT,			///< 34 move to rearm point - VTOLS - this actually moves them onto the pad
  WAITDURINGREARM,			///< 35 waiting during rearm process- VTOLS
  VTOLATTACK,				///< 36 a VTOL droid doing attack runs
  CLEARREARMPAD,				///< 37 a VTOL droid being told to get off a rearm pad
  RETURNTOPOS,				///< 38 used by scout/patrol order when returning to route
  FIRESUPPORT_RETREAT,			///< 39 used by firesupport order when sensor retreats
  CIRCLE				///< 41 circling while engaging
};

typedef std::vector<DROID_ORDER_DATA> OrderList;

class DroidStats : public StatsObject {
public:
  DroidStats();

  DROID_TYPE droidTemplateType();
  UDWORD calcDroidBaseSpeed(UDWORD weight, UBYTE player);
  /*!
   * The droid components.
   *
   * This array is indexed by COMPONENT_TYPE so the ECM would be accessed
   * using asParts[COMP_ECM].
   *
   * Weapons are stored in asWeaps, _not_ here at index COMP_WEAPON! (Which is the reason we do not have a COMP_NUMCOMPONENTS sized array here.)
   */
private:
  uint8_t         asParts[DROID_MAXCOMP];
  /* The weapon systems */
  int8_t          numWeaps;                   ///< Number of weapons
  uint32_t        asWeaps[MAX_WEAPONS];       ///< weapon indices
  DROID_TYPE      droidType;                  ///< The type of droid
  UDWORD          multiPlayerID;              ///< multiplayer unique descriptor(cant use id's for templates). Used for save games as well now - AB 29/10/98
  bool            prefab;                     ///< Not player designed, not saved, never delete or change
  bool            stored;                     ///< Stored template
  bool            enabled;                    ///< Has been enabled
};

static inline DroidStats *castDroidTemplate(StatsObject *stats);
static inline DroidStats const *castDroidTemplate(StatsObject const *stats);

class DROID_GROUP;
class Structure;

class Droid : public Unit {
public:
  Droid(uint32_t id, unsigned player);

  bool aiUnitHasRange(const GameObject& targetObj, int weapon_slot) override;

  bool actionInsideMinRange(const GameObject& targetObj, const WEAPON_STATS* droidWeapStats) const;
  bool actionInRange(const GameObject& targetObj, int weapon_slot, bool useLongWithOptimum);
  void actionAddVtolAttackRun(const GameObject& targetDroid);
  void moveToRearm();
  void actionSanity();
  bool actionVTOLLandingPos(Vector2i* p);
  void cancelBuild();
  void droidBodyUpgrade();
  void droidSetPosition(int x, int y);
  bool droidOnMap();
  bool cyborgDroid();
  bool isConstructionDroid();
  bool isSelectable() const;
  void selectDroid();
  void DeSelectDroid();
  void actionUpdateTransporter();
  void actionUpdateVtolAttack();
  SWORD droidResistance() const;
  bool standardSensorDroid() const;
  bool cbSensorDroid() const;
  void updateVtolAttackRun(int weapon_slot);
  bool vtolHappy() const;
  UWORD getNumAttackRuns(int weapon_slot);
  bool allVtolsRearmed() const;
  bool droidAttacking() const;
  bool vtolRearming() const;
  unsigned getDroidLevel() const;
  void droidSetBits(const DroidStats* pTemplate);
  void initDroidMovement();
  bool idfDroid();
  bool droidUpdateRepair();
  bool droidUpdateRestore();
  int32_t droidDamage(unsigned damage, WEAPON_CLASS weaponClass, WEAPON_SUBCLASS weaponSubClass, unsigned impactTime, bool isDamagePerSecond, int minDamage);
  void recycleDroid();
  bool removeDroidBase();
  void droidUpdate();
  DroidStartBuild droidStartBuild();
  bool droidUpdateDemolishing();
  void setUpBuildModule();
  bool electronicDroid() const;
  bool droidUnderRepair() const;
  bool vtolEmpty() const;
  bool vtolFull() const;
  void aiUpdateDroid();
  void orderCheckList();
  DROID_ORDER chooseOrderLoc(UDWORD x, UDWORD y, bool altOrder);
  bool orderDroidList();
  void orderDroidAdd(DROID_ORDER_DATA *psOrder);
  bool orderStateStatsLoc(DROID_ORDER order, StructureStats **ppsStats);
  void orderDroidLoc(DROID_ORDER order, UDWORD x, UDWORD y, QUEUE_MODE mode);
  void orderUpdateDroid();
  void orderCheckGuardPosition(SDWORD range);
  Droid* checkForRepairRange();
  bool tryDoRepairlikeAction();
  void orderDroidBase(DROID_ORDER_DATA *psOrder);
  void orderDroid(DROID_ORDER order, QUEUE_MODE mode);
  bool secondarySetState(SECONDARY_ORDER sec, SECONDARY_STATE State, QUEUE_MODE mode);
  bool droidUpdateBuild();
  void _syncDebugDroid(const char *function, char ch);
  bool droidUpdateDroidRepair();
  RtrBestResult decideWhereToRepairAndBalance();
  void actionUpdateDroid();
protected:
  /// UTF-8 name of the droid. This is generated from the droid template
  ///  WARNING: This *can* be changed by the game player after creation & can be translated, do NOT rely on this being the same for everyone!
  char name[MAX_STR_LENGTH];
  DROID_TYPE      droidType;                      ///< The type of droid
  /** Holds the specifics for the component parts - allows damage
   *  per part to be calculated. Indexed by COMPONENT_TYPE.
   *  Weapons need to be dealt with separately.
   */
  uint8_t         asBits[DROID_MAXCOMP];
  /* The other droid data.  These are all derived from the components
   * but stored here for easy access
   */
  UBYTE group = 0;                                ///< Which group selection is the droid currently in?
  UDWORD          weight;
  UDWORD          baseSpeed;                      ///< the base speed dependent on propulsion type
  UDWORD          originalBody;                   ///< the original body points
  uint32_t        experience;
  uint32_t        kills;
  UDWORD          lastFrustratedTime;             ///< Set when eg being stuck; used for eg firing indiscriminately at map features to clear the way
  SWORD           resistance;                     ///< used in Electronic Warfare
  // The group the droid belongs to
  std::unique_ptr<DROID_GROUP> psGroup;
  Droid *psGrpNext;
  Structure
      *psBaseStruct;                   ///< a structure that this droid might be associated with. For VTOLs this is the rearming pad
  // queued orders
  SDWORD          listSize;                       ///< Gives the number of synchronised orders. Orders from listSize to the real end of the list may not affect game state.
  OrderList       asOrderList;                    ///< The range [0; listSize - 1] corresponds to synchronised orders, and the range [listPendingBegin; listPendingEnd - 1] corresponds to the orders that will remain, once all orders are synchronised.
  unsigned        listPendingBegin;               ///< Index of first order which will not be erased by a pending order. After all messages are processed, the orders in the range [listPendingBegin; listPendingEnd - 1] will remain.
  /* Order data */
  DROID_ORDER_DATA order;

  // secondary order data
  UDWORD          secondaryOrder;
  uint32_t        secondaryOrderPending;          ///< What the secondary order will be, after synchronisation.
  int             secondaryOrderPendingCount;     ///< Number of pending secondary order changes.

  /* Action data */
  DROID_ACTION    action;
  Vector2i        actionPos;
  std::unique_ptr<Unit> psActionTarget[MAX_WEAPONS] = {}; ///< Action target object
  UDWORD          actionStarted;                  ///< Game time action started
  UDWORD          actionPoints;                   ///< number of points done by action since start
  UDWORD          expectedDamageDirect;                 ///< Expected damage to be caused by all currently incoming direct projectiles. This info is shared between all players,
  UDWORD          expectedDamageIndirect;                 ///< Expected damage to be caused by all currently incoming indirect projectiles. This info is shared between all players,
  ///< but shouldn't make a difference unless 3 mutual enemies happen to be fighting each other at the same time.
  UBYTE           illumination;
  /* Movement control data */
  MOVE_CONTROL    sMove;
  Spacetime       prevSpacetime;                  ///< Location of droid in previous tick.
  uint8_t         blockedBits;                    ///< Bit set telling which tiles block this type of droid (TODO)
  /* anim data */
  SDWORD          iAudioID;
};

#endif // __INCLUDED_DROIDDEF_H__