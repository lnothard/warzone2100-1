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
 *  Definitions for structures.
 */

#ifndef __INCLUDED_STRUCTUREDEF_H__
#define __INCLUDED_STRUCTUREDEF_H__

#include "positiondef.h"
#include "basedef.h"
#include "statsdef.h"
#include "weapondef.h"
#include "unitdef.h"
#include "droid.h"

#include <vector>

#define NUM_FACTORY_MODULES 2
#define NUM_POWER_MODULES 4
#define	REF_ANY 255		// Used to indicate any kind of building when calling intGotoNextStructureType()

struct FLAG_POSITION : public OBJECT_POSITION
{
  Vector3i coords = Vector3i(0, 0, 0); //the world coords of the Position
  UBYTE    factoryInc;           //indicates whether the first, second etc factory
  UBYTE    factoryType;          //indicates whether standard, cyborg or vtol factory
  FLAG_POSITION *psNext;
};

enum class STRUCT_STRENGTH
{
  SOFT,
  MEDIUM,
  HARD,
  BUNKER,
  COUNT  // LAST
};

typedef UWORD STRUCTSTRENGTH_MODIFIER;

#define SAS_OPEN_SPEED		(GAME_TICKS_PER_SEC)
#define SAS_STAY_OPEN_TIME	(GAME_TICKS_PER_SEC * 6)

enum class STRUCT_ANIM_STATES
{
  NORMAL,
  OPEN,
  OPENING,
  CLOSING,
};

#define STRUCTURE_CONNECTED 0x0001 ///< This structure must be built side by side with another of the same player

//this structure is used to hold the permanent stats for each type of building
class StructureStats : public StatsObject {
public:
  StructureStats();

  Vector2i size(uint16_t direction) const;
  bool canSmoke() const;
protected:
  STRUCT_STRENGTH strength;       /* strength against the weapon effects */
  UDWORD baseWidth;               /*The width of the base in tiles*/
  UDWORD baseBreadth;             /*The breadth of the base in tiles*/
  UDWORD buildPoints;             /*The number of build points required to build the structure*/
  UDWORD height;                  /*The height above/below the terrain - negative values denote below the terrain*/
  UDWORD powerToBuild;            /*How much power the structure requires to build*/
  std::vector<iIMDShape *> pIMD;  // The IMDs to draw for this structure, for each possible number of modules.
  iIMDShape *pBaseIMD;            /*The base IMD to draw for this structure */
  struct ECM_STATS *pECM;         /*Which ECM is standard for the structure -if any*/
  struct SENSOR_STATS *pSensor;   /*Which Sensor is standard for the structure -if any*/
  UDWORD weaponSlots;             /*Number of weapons that can be attached to the building*/
  UDWORD numWeaps;                /*Number of weapons for default */
  struct WEAPON_STATS *psWeapStat[MAX_WEAPONS];
  uint64_t flags;
  bool combinesWithWall;			//If the structure will trigger nearby walls to try combining with it

  unsigned minLimit;		///< lowest value user can set limit to (currently unused)
  unsigned maxLimit;		///< highest value user can set limit to, LOTS_OF = no limit
  unsigned curCount[MAX_PLAYERS];	///< current number of instances of this getType

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
    unsigned resistance;	// resist enemy takeover; 0 = immune
    unsigned limit;		// current max limit for this type, LOTS_OF = no limit
  } upgrade[MAX_PLAYERS], base;

  /* Defines for indexing an appropriate IMD object given a buildings purpose. */
  enum class {
    HQ,
    FACTORY,
    FACTORY_MODULE,           //draw as factory 2
    POWER_GEN,
    POWER_MODULE,
    RESOURCE_EXTRACTOR,
    DEFENSE,
    WALL,
    WALLCORNER,				//corner wall - no gun
    GENERIC,
    RESEARCH,
    RESEARCH_MODULE,
    REPAIR_FACILITY,
    COMMAND_CONTROL,		//control centre for command droids
    BRIDGE,			//NOT USED, but removing it would change savegames
    DEMOLISH,			//the demolish structure type - should only be one stat with this type
    CYBORG_FACTORY,
    VTOL_FACTORY,
    LAB,
    REARM_PAD,
    MISSILE_SILO,
    SAT_UPLINK,         //added for updates - AB 8/6/99
    GATE,
    LASSAT,
    COUNT		//need to keep a count of how many types for IMD loading
  } type;

  bool isFavorite;		///< on Favorites list
};

static inline StructureStats *castStructureStats(StatsObject *stats)
{
  return stats != nullptr && stats->hasType(STAT_STRUCTURE)? dynamic_cast<StructureStats *>(stats) : nullptr;
}

static inline StructureStats const *castStructureStats(StatsObject const *stats)
{
  return stats != nullptr && stats->hasType(STAT_STRUCTURE)? dynamic_cast<StructureStats const *>(stats) : nullptr;
}



enum class PENDING_STATUS
{
  NOTHING_PENDING,
  START_PENDING,
  HOLD_PENDING,
  CANCEL_PENDING
};

struct RESEARCH;

class ResearchFacility
{
  RESEARCH *psSubject;              // The subject the structure is working on.
  RESEARCH *psSubjectPending;       // The subject the structure is going to work on when the GAME_RESEARCHSTATUS message is received.
  PENDING_STATUS statusPending;      ///< Pending = not yet synchronised.
  unsigned pendingCount;            ///< Number of messages sent but not yet processed.
  RESEARCH *psBestTopic;            // The topic with the most research points that was last performed
  UDWORD timeStartHold;             /* The time the research facility was put on hold*/
};

class DroidStats;

struct RES_EXTRACTOR
{
  class Structure *psPowerGen;    ///< owning power generator
};

struct POWER_GEN
{
  class Structure
  *apResExtractors[NUM_POWER_MODULES];   ///< Pointers to associated oil derricks
};

class DROID_GROUP;

struct WALL
{
  unsigned type;             // Type of wall, 0 = ─, 1 = ┼, 2 = ┴, 3 = ┘.
};

//this structure is used whenever an instance of a building is required in game
class Structure : public Unit {
public:
  Structure(uint32_t id, unsigned player);

  Vector2i size() const;

  virtual void printStructureInfo() = 0;

  bool aiUnitHasRange(const GameObject& targetObj, int weapon_slot) override;
  int sensorRange() override;
  bool turretOnTarget(GameObject *targetObj, Weapon *weapon) override;

  void addConstructorEffect();
  void alignStructure();
  int  structureTotalReturn() const;
  void structureBuild(Droid *psDroid, int buildPoints, int buildRate);
  void structureUpdate(bool bMission);
  int  requestOpenGate();
  void aiUpdateStructure(bool isMission);
  bool isBlueprint() const;
  bool canSmoke() const;
protected:
  std::unique_ptr<StructureStats> stats;            /* pointer to the structure stats for this type of building */

  enum class {
    BEING_BUILT,
    BUILT,
    BLUEPRINT_VALID,
    BLUEPRINT_INVALID,
    BLUEPRINT_PLANNED,
    BLUEPRINT_PLANNED_BY_ALLY,
  } status;

  uint32_t            currentBuildPts;            /* the build points currently assigned to this structure */
  int                 resistance;                 /* current resistance points, 0 = cannot be attacked electrically */
  UDWORD              lastResistance;             /* time the resistance was last increased*/
  int                 buildRate;                  ///< Rate that this structure is being built, calculated each tick. Only meaningful if status == SS_BEING_BUILT. If construction hasn't started and build rate is 0, remove the structure.
  int                 lastBuildRate;              ///< Needed if wanting the buildRate between buildRate being reset to 0 each tick and the trucks calculating it.
  GameObject *psTarget[MAX_WEAPONS];
  UDWORD expectedDamage;           ///< Expected damage to be caused by all currently incoming projectiles. This info is shared between all players,
                                      ///< but shouldn't make a difference unless 3 mutual enemies happen to be fighting each other at the same time.
  uint32_t prevTime;               ///< Time of structure's previous tick.
  float foundationDepth;           ///< Depth of structure's foundation
  uint8_t capacity;                ///< Lame name: current number of module upgrades (*not* maximum nb of upgrades)
  STRUCT_ANIM_STATES state;
  UDWORD lastStateTime;
  iIMDShape *prebuiltImd;
};

class Factory : public Structure
{
private:
  uint8_t productionLoops;          ///< Number of loops to perform. Not synchronised, and only meaningful for selectedPlayer.
  UBYTE loopsPerformed;             /* how many times the loop has been performed*/
  DroidStats *psSubject;        ///< The subject the structure is working on.
  DroidStats *psSubjectPending; ///< The subject the structure is going to working on. (Pending = not yet synchronised.)
  PENDING_STATUS statusPending;      ///< Pending = not yet synchronised.
  unsigned pendingCount;            ///< Number of messages sent but not yet processed.
  UDWORD timeStarted;               /* The time the building started on the subject*/
  int buildPointsRemaining;         ///< Build points required to finish building the droid.
  UDWORD timeStartHold;             /* The time the factory was put on hold*/
  FLAG_POSITION *psAssemblyPoint;   /* Place for the new droids to assemble at */
  class Droid *psCommander;        // command droid to produce droids for (if any)
  uint32_t secondaryOrder;          ///< Secondary order state for all units coming out of the factory.
public:
  void refundFactoryBuildPower();
  bool structSetManufacture(DroidStats *psTempl, QUEUE_MODE mode);
  bool isCommanderGroupFull() const;
};

class RepairFacility : public Structure
{
  GameObject&    repairTarget;                /* Object being repaired */
  FLAG_POSITION* psDeliveryPoint;    /* Place for the repaired droids to assemble at */

  DROID_GROUP*   psGroup;                       // The group the droids to be repaired by this facility belong to
  int            droidQueue;                    ///< Last count of droid queue for this facility
};

class RearmPad : public Structure
{
  UDWORD timeStarted;            /* Time reArm started on current object */
  GameObject *psObj;            /* Object being rearmed */
  UDWORD timeLastUpdated;        /* Time rearm was last updated */
};

#define LOTS_OF 0xFFFFFFFF  // highest number the limit can be set to

//the three different types of factory (currently) - FACTORY, CYBORG_FACTORY, VTOL_FACTORY
// added repair facilities as they need an assembly point as well
enum class FLAG_TYPE
{
  FACTORY,
  CYBORG,
  VTOL,
  REPAIR,
//separate the numfactory from numflag
  NUM_TYPES,
  NUM_FACTORY_TYPES = REPAIR,
};

//this is used for module graphics - factory and vtol factory
static const int NUM_FACMOD_TYPES = 2;

struct ProductionRunEntry
{
  ProductionRunEntry() : quantity(0), built(0), psTemplate(nullptr) {}
  void restart()
  {
          built = 0;
  }
  void removeComplete()
  {
          quantity -= built;
          built = 0;
  }
  int numRemaining() const
  {
          return quantity - built;
  }
  bool isComplete() const
  {
          return numRemaining() <= 0;
  }
  bool isValid() const
  {
          return psTemplate != nullptr && quantity > 0 && built <= quantity;
  }
  bool operator ==(DroidStats *t) const;

  int quantity;                 //number to build
  int built;                    //number built on current run
  DroidStats *psTemplate;   //template to build
};
typedef std::vector<ProductionRunEntry> ProductionRun;

struct UPGRADE_MOD
{
  UWORD modifier;      //% to increase the stat by
};

typedef UPGRADE_MOD REPAIR_FACILITY_UPGRADE;
typedef UPGRADE_MOD POWER_UPGRADE;
typedef UPGRADE_MOD REARM_UPGRADE;

#endif // __INCLUDED_STRUCTUREDEF_H__