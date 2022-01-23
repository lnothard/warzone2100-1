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
 * @file structure.cpp
 * Store structure stats
 */

#include <utility>

#include "lib/framework/math_ext.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/sound/audio.h"
#include "lib/sound/audio_id.h"

#include "action.h"
#include "baseobject.h"
#include "cmddroid.h"
#include "combat.h"
#include "console.h"
#include "display3d.h"
#include "displaydef.h"
#include "effects.h"
#include "game.h"
#include "geometry.h"
#include "intdisplay.h"
#include "loop.h"
#include "mapgrid.h"
#include "miscimd.h"
#include "mission.h"
#include "move.h"
#include "multiplay.h"
#include "objmem.h"
#include "projectile.h"
#include "qtscript.h"
#include "scores.h"
#include "structure.h"
#include "template.h"
#include "visibility.h"
#include "multigifts.h"

/* Value is stored for easy access to this structure stat */

unsigned factoryModuleStat;
unsigned powerModuleStat;
unsigned researchModuleStat;

//holder for all StructureStats
StructureStats* asStructureStats = nullptr;
unsigned numStructureStats;
static std::unordered_map<WzString, StructureStats*> lookupStructStatPtr;

/// Used to hold the modifiers cross-referenced by weapon effect and structureStrength
std::array<std::array<STRUCTSTRENGTH_MODIFIER, static_cast<std::size_t>(WEAPON_EFFECT::COUNT)>,
        static_cast<std::size_t>(STRUCTURE_STRENGTH::COUNT)> asStructStrengthModifier;

/// Specifies which numbers have been allocated for the assembly points for the factories
static std::vector<bool> factoryNumFlag[MAX_PLAYERS][NUM_FLAG_TYPES];

// The number of different (types of) droids that can be put into a production run
static constexpr auto MAX_IN_RUN = 9;

/// Stores which player the production list has been set up for
unsigned productionPlayer;

/// Destroy building construction droid stat pointer
static StructureStats* g_psStatDestroyStruct = nullptr;

/// The structure that was last hit
Structure* psLastStructHit;

/// Flag for drawing all sat uplink sees
static std::array<uint8_t, MAX_PLAYERS> satUplinkExists;

/// Flag for when the player has one built - either completely or partially
static std::array<uint8_t, MAX_PLAYERS> lasSatExists;

static bool setFunctionality(Structure* psBuilding, STRUCTURE_TYPE functionType);
static void setFlagPositionInc(unsigned player, uint8_t factoryType);
static void informPowerGen(Structure* psStruct);
static bool electronicReward(Structure* psStructure, uint8_t attackPlayer);
static void factoryReward(uint8_t losingPlayer, uint8_t rewardPlayer);
static void repairFacilityReward(uint8_t losingPlayer, uint8_t rewardPlayer);
static void findAssemblyPointPosition(unsigned* pX, unsigned* pY, unsigned player);
static void removeStructFromMap(Structure* psStruct);
static void resetResistanceLag(Structure* psBuilding);
static int structureTotalReturn(const Structure* psStruct);
static void parseFavoriteStructs();
static void packFavoriteStructs();
static bool structureHasModules(const Structure* psStruct);

// last time the maximum units message was displayed
static unsigned lastMaxUnitMessage;

// max number of units
static int droidLimit[MAX_PLAYERS];
// max number of commanders
static int commanderLimit[MAX_PLAYERS];
// max number of constructors
static int constructorLimit[MAX_PLAYERS];

static WzString favoriteStructs;

#define MAX_UNIT_MESSAGE_PAUSE 40000


struct Structure::Impl
{
  Impl() = default;

  std::shared_ptr<StructureStats> stats;
  /// Whether the structure is being built, doing nothing or performing a function
  STRUCTURE_STATE state = STRUCTURE_STATE::BLUEPRINT_PLANNED;
  /// The build points currently assigned to this structure
  unsigned currentBuildPoints = 0;
  /// Time the resistance was last increased
  unsigned lastResistance = 0;
  /// Rate that this structure is being built, calculated each tick. Only
  /// meaningful if status == BEING_BUILT. If construction hasn't started
  /// and build rate is 0, remove the structure
  int buildRate = 0;
  /// Needed if wanting the buildRate between buildRate being reset to 0
  /// each tick and the trucks calculating it
  int previousBuildRate = 0;
  std::array<BaseObject*, MAX_WEAPONS> target{};
  /// Expected damage to be caused by all currently incoming projectiles.
  /// This info is shared between all players, but shouldn't make a difference
  /// unless 3 mutual enemies happen to be fighting each other at the same time.
  unsigned expectedDamage = 0;
  /// Time of structure's previous tick
  unsigned prevTime = 0;
  int foundationDepth = 0;
  /// Lame name: current number of module upgrades
  /// (*not* maximum number of upgrades)
  uint8_t capacity = 0;
  STRUCTURE_ANIMATION_STATE animationState = STRUCTURE_ANIMATION_STATE::NORMAL;
  unsigned lastStateTime = 0;
  std::shared_ptr<iIMDShape> prebuiltImd;
};

struct ResearchFacility::Impl
{
  ~Impl() = default;
  Impl() = default;

  Impl(Impl const& rhs);
  Impl& operator=(Impl const& rhs);

  Impl(Impl&& rhs) noexcept = default;
  Impl& operator=(Impl&& rhs) = default;

  std::unique_ptr<ResearchItem> psSubject; // The subject the structure is working on.
  std::unique_ptr<ResearchItem> psSubjectPending;
  std::unique_ptr<ResearchItem> psBestTopic; // The topic with the most research points that was last performed
  // The subject the structure is going to work on when the GAME_RESEARCHSTATUS message is received.
  PENDING_STATUS statusPending = PENDING_STATUS::NOTHING_PENDING; ///< Pending = not yet synchronised.
  unsigned pendingCount = 0; ///< Number of messages sent but not yet processed.
  unsigned timeStartHold = 0; /* The time the research facility was put on hold*/
};

struct ProductionRun::Impl
{
  Impl() = default;

  std::shared_ptr<DroidTemplate> target = nullptr;
  unsigned quantityToBuild = 0;
  unsigned quantityBuilt = 0;
};

struct Factory::Impl
{
  ~Impl() = default;
  Impl() = default;

  Impl(Impl const& rhs);
  Impl& operator=(Impl const& rhs);

  Impl(Impl&& rhs) noexcept = default;
  Impl& operator=(Impl&& rhs) noexcept = default;

  uint8_t productionLoops = 0; ///< Number of loops to perform. Not synchronised, and only meaningful for selectedPlayer.
  uint8_t loopsPerformed = 0; /* how many times the loop has been performed*/
  std::shared_ptr<DroidTemplate> psSubject; ///< The subject the structure is working on.
  std::shared_ptr<DroidTemplate> psSubjectPending;
  ///< The subject the structure is going to working on. (Pending = not yet synchronised.)
  PENDING_STATUS statusPending = PENDING_STATUS::NOTHING_PENDING; ///< Pending = not yet synchronised.
  unsigned pendingCount = 0; ///< Number of messages sent but not yet processed.
  unsigned timeStarted = 0; /* The time the building started on the subject*/
  unsigned timeStartHold = 0; /* The time the factory was put on hold*/
  int buildPointsRemaining = 0; ///< Build points required to finish building the droid.
  std::unique_ptr<FlagPosition> psAssemblyPoint; /* Place for the new droids to assemble at */
  Droid* psCommander = nullptr; // command droid to produce droids for (if any)
  unsigned secondaryOrder = 0; ///< Secondary order state for all units coming out of the factory.
};

struct PowerGenerator::Impl
{
  Impl() = default;

  /// Pointers to associated oil derricks
  std::array<ResourceExtractor*, NUM_POWER_MODULES> resource_extractors;
};

struct RepairFacility::Impl
{
  ~Impl() = default;
  Impl() = default;

  Impl(Impl const& rhs);
  Impl& operator=(Impl const& rhs);

  Impl(Impl&& rhs) noexcept = default;
  Impl& operator=(Impl&& rhs) noexcept = default;

  ConstructedObject* psObj = nullptr; /* Object being repaired */
  std::unique_ptr<FlagPosition> psDeliveryPoint; /* Place for the repaired droids to assemble at */
  // The group the droids to be repaired by this facility belong to
  std::shared_ptr<Group> psGroup;
  int droidQueue = 0; /// Last count of droid queue for this facility
};

struct RearmPad::Impl
{
  Impl() = default;

  unsigned timeStarted = 0; /* Time reArm started on current object */
  Droid* psObj = nullptr; /* Object being rearmed */
  unsigned timeLastUpdated = 0; /* Time rearm was last updated */
};

struct ResourceExtractor::Impl
{
  Impl() = default;

  PowerGenerator* power_generator;
};

Structure::Structure(unsigned id, unsigned player)
  : BaseObject(id), Damageable(), PlayerOwned(player)
  , pimpl{std::make_unique<Impl>()}
{
}

Structure::Structure(Structure const& rhs)
  : BaseObject(rhs), Damageable(rhs), PlayerOwned(rhs)
  , pimpl{std::make_unique<Impl>(*rhs.pimpl)}
{
}

Structure& Structure::operator=(Structure const& rhs)
{
  if (this == &rhs) return *this;
  *pimpl = *rhs.pimpl;
  return *this;
}

ResearchFacility::ResearchFacility(unsigned id, unsigned player)
  : Structure(id, player)
  , pimpl{std::make_unique<Impl>()}
{
}

ResearchFacility::ResearchFacility(ResearchFacility const& rhs)
  : Structure(rhs)
  , pimpl{std::make_unique<Impl>(*rhs.pimpl)}
{
}

ResearchFacility& ResearchFacility::operator=(ResearchFacility const& rhs)
{
  if (this == &rhs) return *this;
  *pimpl = *rhs.pimpl;
  return *this;
}

ResearchFacility::Impl::Impl(Impl const& rhs)
  : psSubject{std::make_unique<ResearchItem>(*rhs.psSubject)}
  , psSubjectPending{std::make_unique<ResearchItem>(*rhs.psSubjectPending)}
  , psBestTopic{std::make_unique<ResearchItem>(*rhs.psBestTopic)}
  , statusPending{rhs.statusPending}
  , pendingCount{rhs.pendingCount}
  , timeStartHold{rhs.timeStartHold}
{
}

ResearchFacility::Impl& ResearchFacility::Impl::operator=(Impl const& rhs)
{
  if (this == &rhs) return *this;
  psSubject = std::make_unique<ResearchItem>(*rhs.psSubject);
  psSubjectPending = std::make_unique<ResearchItem>(*rhs.psSubjectPending);
  psBestTopic = std::make_unique<ResearchItem>(*rhs.psBestTopic);
  statusPending = rhs.statusPending;
  pendingCount = rhs.pendingCount;
  timeStartHold = rhs.timeStartHold;
  return *this;
}

ProductionRun::ProductionRun()
  : pimpl{std::make_unique<Impl>()}
{
}

ProductionRun::ProductionRun(ProductionRun const& rhs)
  : pimpl{std::make_unique<Impl>(*rhs.pimpl)}
{
}

ProductionRun& ProductionRun::operator=(ProductionRun const& rhs)
{
  if (this == &rhs) return *this;
  *pimpl = *rhs.pimpl;
  return *this;
}

Factory::Factory(unsigned id, unsigned player)
  : Structure(id, player)
  , pimpl{std::make_unique<Impl>()}
{
}

Factory::Factory(Factory const& rhs)
  : Structure(rhs)
  , pimpl{std::make_unique<Impl>(*rhs.pimpl)}
{
}

Factory& Factory::operator=(Factory const& rhs)
{
  if (this == &rhs) return *this;
  *pimpl = *rhs.pimpl;
  return *this;
}

Factory::Impl::Impl(Impl const& rhs)
  : psAssemblyPoint{std::make_unique<FlagPosition>(*rhs.psAssemblyPoint)}
  , psSubject{rhs.psSubject}
  , psSubjectPending{rhs.psSubjectPending}
  , productionLoops{rhs.productionLoops}
  , loopsPerformed{rhs.loopsPerformed}
  , statusPending{rhs.statusPending}
  , pendingCount{rhs.pendingCount}
  , timeStarted{rhs.timeStarted}
  , buildPointsRemaining{rhs.buildPointsRemaining}
  , timeStartHold{rhs.timeStartHold}
  , psCommander{rhs.psCommander}
  , secondaryOrder{rhs.secondaryOrder}
{
}

Factory::Impl& Factory::Impl::operator=(Impl const& rhs)
{
  if (this == &rhs) return *this;
  psAssemblyPoint = std::make_unique<FlagPosition>(*rhs.psAssemblyPoint);
  psSubject = rhs.psSubject;
  psSubjectPending = rhs.psSubjectPending;
  productionLoops = rhs.productionLoops;
  loopsPerformed = rhs.loopsPerformed;
  statusPending = rhs.statusPending;
  pendingCount = rhs.pendingCount;
  timeStarted = rhs.timeStarted;
  buildPointsRemaining = rhs.buildPointsRemaining;
  timeStartHold = rhs.timeStartHold;
  psCommander = rhs.psCommander;
  secondaryOrder = rhs.secondaryOrder;
  return *this;
}

ResourceExtractor::ResourceExtractor(unsigned id, unsigned player)
  : Structure(id, player)
  , pimpl{std::make_unique<Impl>()}
{
}

ResourceExtractor::ResourceExtractor(ResourceExtractor const& rhs)
  : Structure(rhs)
  , pimpl{std::make_unique<Impl>(*rhs.pimpl)}
{
}

ResourceExtractor& ResourceExtractor::operator=(ResourceExtractor const& rhs)
{
  if (this == &rhs) return *this;
  *pimpl = *rhs.pimpl;
  return *this;
}

PowerGenerator::PowerGenerator(unsigned id, unsigned player)
  : Structure(id, player)
  , pimpl{std::make_unique<Impl>()}
{
}

PowerGenerator::PowerGenerator(PowerGenerator const& rhs)
  : Structure(rhs)
  , pimpl{std::make_unique<Impl>(*rhs.pimpl)}
{
}

PowerGenerator& PowerGenerator::operator=(PowerGenerator const& rhs)
{
  if (this == &rhs) return *this;
  *pimpl = *rhs.pimpl;
  return *this;
}

RepairFacility::RepairFacility(unsigned id, unsigned player)
  : Structure(id, player)
  , pimpl{std::make_unique<Impl>()}
{
}

RepairFacility::RepairFacility(RepairFacility const& rhs)
  : Structure(rhs)
  , pimpl{std::make_unique<Impl>(*rhs.pimpl)}
{
}

RepairFacility& RepairFacility::operator=(RepairFacility const& rhs)
{
  if (this == &rhs) return *this;
  *pimpl = *rhs.pimpl;
  return *this;
}

RepairFacility::Impl::Impl(Impl const& rhs)
  : psDeliveryPoint{std::make_unique<FlagPosition>(*rhs.psDeliveryPoint)}
  , psObj{rhs.psObj}
  , psGroup{rhs.psGroup}
  , droidQueue{rhs.droidQueue}
{
}

RepairFacility::Impl& RepairFacility::Impl::operator=(Impl const& rhs)
{
  if (this == &rhs) return *this;
  psDeliveryPoint = std::make_unique<FlagPosition>(*rhs.psDeliveryPoint);
  psObj = rhs.psObj;
  psGroup = rhs.psGroup;
  droidQueue = rhs.droidQueue;
  return *this;
}

RearmPad::RearmPad(unsigned id, unsigned player)
  : Structure(id, player)
  , pimpl{std::make_unique<Impl>()}
{
}

RearmPad::RearmPad(RearmPad const& rhs)
  : Structure(rhs)
  , pimpl{std::make_unique<Impl>(*rhs.pimpl)}
{
}

RearmPad& RearmPad::operator=(RearmPad const& rhs)
{
  if (this == &rhs) return *this;
  *pimpl = *rhs.pimpl;
  return *this;
}

StructureBounds::StructureBounds()
  : map(0, 0), size(0, 0)
{
}

StructureBounds::StructureBounds(Vector2i top_left_coords, Vector2i size_in_coords)
  : map{top_left_coords}, size{size_in_coords}
{
}

bool StructureBounds::isValid() const
{
  return size.x >= 0;
}

StructureBounds get_bounds(const Structure& structure) noexcept
{
  return  {map_coord(structure.getPosition().xy()) -
                    structure.getSize() / 2, structure.getSize()
  };
}

Vector2i StructureStats::size(unsigned direction) const
{
  Vector2i size(base_width, base_breadth);
  if ((snapDirection(direction) & 0x4000) != 0)  {
    // if building is rotated left or right by 90°, swap width and height
    std::swap(size.x, size.y);
  }
  return size;
}

bool StructureStats::is_expansion_module() const noexcept
{
  return type == POWER_MODULE ||
         type == FACTORY_MODULE ||
         type == RESEARCH_MODULE;
}

void auxStructureNonblocking(const Structure& structure)
{
  const auto bounds = getBounds(structure);
  for (auto i = 0; i < bounds.size.x; ++i)
  {
    for (auto j = 0; j < bounds.size.y; ++j)
    {
      auxClearBlocking(bounds.map.x + i,
                bounds.map.y + j,
                AUXBITS_BLOCKING | AUXBITS_OUR_BUILDING | AUXBITS_NONPASSABLE);
    }
  }
}

void auxStructureBlocking(const Structure& structure)
{
  const auto bounds = getBounds(structure);
  for (auto i = 0; i < bounds.size.x; ++i)
  {
    for (auto j = 0; j < bounds.size.y; ++j)
    {
      auxSetAllied(bounds.map.x + i,
                     bounds.map.y + j,
                     structure.getPlayer(),
                     AUXBITS_OUR_BUILDING);

      auxSetAll(bounds.map.x + i,
                  bounds.map.y + j,
                  AUXBITS_BLOCKING | AUXBITS_NONPASSABLE);
    }
  }
}

void auxStructureOpenGate(const Structure& structure)
{
  const auto bounds = getBounds(structure);
  for (auto i = 0; i < bounds.size.x; ++i)
  {
    for (auto j = 0; j < bounds.size.y; ++j)
    {
      auxClearBlocking(bounds.map.x + i,
                       bounds.map.y + j,
                       AUXBITS_BLOCKING);
    }
  }
}

void auxStructureClosedGate(const Structure& structure)
{
  const auto bounds = getBounds(structure);
  for (auto i = 0; i < bounds.size.x; ++i)
  {
    for (auto j = 0; j < bounds.size.y; ++j)
    {
      auxSetEnemy(bounds.map.x + i,
                    bounds.map.y + j,
                    structure.getPlayer(),
                    AUXBITS_NONPASSABLE);

      auxSetAll(bounds.map.x + i,
                  bounds.map.y + j,
                  AUXBITS_BLOCKING);
    }
  }
}

Structure::~Structure()
{
  // Make sure to get rid of some final references in the sound
  // code to this object first
  audio_RemoveObj(this);
}

bool Structure::isBlueprint() const noexcept
{
  using enum STRUCTURE_STATE;
  return pimpl &&
         (pimpl->state == BLUEPRINT_VALID ||
          pimpl->state == BLUEPRINT_INVALID ||
          pimpl->state == BLUEPRINT_PLANNED ||
          pimpl->state == BLUEPRINT_PLANNED_BY_ALLY);
}

unsigned Structure::buildPointsToCompletion() const
{
  return pimpl
         ? pimpl->stats->build_point_cost - pimpl->currentBuildPoints
         : 0;
}

iIMDShape const* Structure::getImdShape() const
{
  return pimpl ? pimpl->prebuiltImd.get() : nullptr;
}

void Structure::setFoundationDepth(int depth) noexcept
{
  if (!pimpl) return;
  pimpl->foundationDepth = depth;
}

int Structure::objRadius() const
{
  if (!getDisplayData()) return -1;
  return getDisplayData()->imd_shape->radius / 2;
}

bool Structure::hasModules() const noexcept
{
  return pimpl && pimpl->capacity > 0;
}

bool Structure::isRadarDetector() const
{
  return pimpl && pimpl->stats->
          sensor_stats->type == SENSOR_TYPE::RADAR_DETECTOR;
}

bool Structure::hasSensor() const
{
  return pimpl && pimpl->stats->sensor_stats;
}

bool Structure::hasCbSensor() const
{
  if (!hasSensor()) return false;
  const auto sensor_type = pimpl->stats->sensor_stats->type;
  return sensor_type == SENSOR_TYPE::INDIRECT_CB ||
         sensor_type == SENSOR_TYPE::SUPER;
}

bool Structure::hasStandardSensor() const
{
  if (!hasSensor()) return false;
  const auto sensor_type = pimpl->stats->sensor_stats->type;
  return sensor_type == SENSOR_TYPE::STANDARD ||
         sensor_type == SENSOR_TYPE::SUPER;
}

bool Structure::hasVtolInterceptSensor() const
{
  if (!hasSensor()) return false;
  const auto sensor_type = pimpl->stats->sensor_stats->type;
  return sensor_type == SENSOR_TYPE::VTOL_INTERCEPT ||
         sensor_type == SENSOR_TYPE::SUPER;
}

bool Structure::hasVtolCbSensor() const
{
  if (!hasSensor()) return false;
  const auto sensor_type = pimpl->stats->sensor_stats->type;
  return sensor_type == SENSOR_TYPE::VTOL_CB ||
         sensor_type == SENSOR_TYPE::SUPER;
}

/// Add buildPoints to the structures currentBuildPts, due to construction work by the droid
/// Also can deconstruct (demolish) a building if passed negative buildpoints
void Structure::structureBuild(Droid* psDroid, int buildPoints, int buildRate_)
{
  if (!pimpl) return;

  // we probably just started demolishing, if this is true
  auto checkResearchButton = pimpl->state == STRUCTURE_STATE::BUILT;
  auto prevResearchState = 0;

  if (checkResearchButton) {
    prevResearchState = intGetResearchState();
  }

  // enemy structure
  if (psDroid && !aiCheckAlliances(getPlayer(), psDroid->getPlayer())) return;

  else if (pimpl->stats->type != STRUCTURE_TYPE::FACTORY_MODULE) {
    for (unsigned player = 0; player < MAX_PLAYERS; player++)
    {
      for (auto& psCurr : apsDroidLists[player])
      {
        // An enemy droid is blocking it
        if (dynamic_cast<Structure*>(orderStateObj(
                &psCurr, ORDER_TYPE::BUILD)) == this &&
            !aiCheckAlliances(player, psCurr.getPlayer())) {
          return;
        }
      }
    }
  }
  // buildRate = buildPoints/GAME_UPDATES_PER_SEC, but might be rounded up
  // or down each tick, so can't use buildPoints to get a stable number
  pimpl->buildRate += buildRate_;

  if (pimpl->currentBuildPoints <= 0 && buildPoints > 0) {
    // Just starting to build structure, need power for it.
    auto haveEnoughPower = requestPowerFor(
            this, structPowerToBuildOrAddNextModule(this));

    if (!haveEnoughPower) {
      buildPoints = 0; // No power to build.
    }
  }

  auto newBuildPoints = pimpl->currentBuildPoints + buildPoints;
  ASSERT(newBuildPoints <= 1 + 3 * (int)structureBuildPointsToCompletion(*this), "unsigned int underflow?");
  CLIP(newBuildPoints, 0, structureBuildPointsToCompletion(*this));

  if (pimpl->currentBuildPoints > 0 && newBuildPoints <= 0) {
    // Demolished structure, return some power.
    addPower(getPlayer(), structureTotalReturn(this));
  }

  ASSERT(newBuildPoints <= 1 + 3 * (int)structureBuildPointsToCompletion(*this), "unsigned int underflow?");
  CLIP(newBuildPoints, 0, structureBuildPointsToCompletion(*this));

  auto deltaBody = quantiseFraction(9 * structureBody(this), 10 * structureBuildPointsToCompletion(*this),
                                   newBuildPoints, pimpl->currentBuildPoints);
  pimpl->currentBuildPoints = newBuildPoints;
  setHp(std::max<unsigned>(getHp() + deltaBody, 1));

  // check if structure is built
  if (buildPoints > 0 &&
      pimpl->currentBuildPoints >= structureBuildPointsToCompletion(*this)) {

    buildingComplete(this);

    // only play the sound if selected player
    if (psDroid && getPlayer() == selectedPlayer &&
        (psDroid->getOrder()->type != ORDER_TYPE::LINE_BUILD ||
         map_coord(psDroid->getOrder()->pos) ==
           map_coord(psDroid->getOrder()->pos2))) {

      audio_QueueTrackPos(ID_SOUND_STRUCTURE_COMPLETED,
                          getPosition().x,
                          getPosition().y,
                          getPosition().z);

      intRefreshScreen(); // update any open interface bars.
    }

    /* must reset here before the callback, droid must have DACTION_NONE
         in order to be able to start a new built task, doubled in actionUpdateDroid() */
    if (psDroid) {
      // Clear all orders for helping hands. Needed for AI script which runs next frame.
      for (auto& psIter : apsDroidLists[getPlayer()])
      {
        if ((psIter.getOrder()->type == ORDER_TYPE::BUILD ||
             psIter.getOrder()->type == ORDER_TYPE::HELP_BUILD ||
             psIter.getOrder()->type == ORDER_TYPE::LINE_BUILD) &&
            psIter.getOrder()->target == this &&
            (psIter.getOrder()->type != ORDER_TYPE::LINE_BUILD ||
             map_coord(psIter.getOrder()->pos) ==
               map_coord(psIter.getOrder()->pos2))) {

          objTrace(psIter.getId(), "Construction order %s complete (%d, %d -> %d, %d)",
                   getDroidOrderName(psDroid->getOrder()->type).c_str(),
                   psIter.getOrder()->pos2.x, psIter.getOrder()->pos.y,
                   psIter.getOrder()->pos2.x, psIter.getOrder()->pos2.y);

          psIter.action = ACTION::NONE;
          psIter.order = Order(ORDER_TYPE::NONE);
          psIter.setActionTarget(nullptr, 0);
        }
      }

      audio_StopObjTrack(psDroid, ID_SOUND_CONSTRUCTION_LOOP);
    }
    triggerEventStructBuilt(this, psDroid);
    checkPlayerBuiltHQ(this);
  }
  else {
    auto prevStatus = pimpl->state;
    pimpl->state = STRUCTURE_STATE::BEING_BUILT;
    if (prevStatus == STRUCTURE_STATE::BUILT) {
      // starting to demolish.
      triggerEventStructDemolish(this, psDroid);

      if (getPlayer() == selectedPlayer) {
        intRefreshScreen();
      }

      switch (pimpl->stats->type) {
        case STRUCTURE_TYPE::POWER_GEN:
          releasePowerGen(this);
          break;
        case STRUCTURE_TYPE::RESOURCE_EXTRACTOR:
          releaseResExtractor(this);
          break;
        default:
          break;
      }
    }
  }
  if (buildPoints < 0 && pimpl->currentBuildPoints == 0) {
    triggerEvent(TRIGGER_OBJECT_RECYCLED, this);
    removeStruct(this, true);
  }

  if (checkResearchButton) {
    intNotifyResearchButton(prevResearchState);
  }
}
  
/// Give a structure from one player to another - used in Electronic Warfare.
/// @return a pointer to the new structure
Structure* Structure::giftSingleStructure(unsigned attackPlayer, bool electronic_warfare)
{
  if (!pimpl) return nullptr;

  Structure *psNewStruct;
  StructureStats *psType, *psModule;
  unsigned x, y;
  uint8_t capacity_ = 0, originalPlayer;
  unsigned buildPoints = 0;
  bool bPowerOn;
  uint16_t direction;

  ASSERT_OR_RETURN(nullptr, attackPlayer < MAX_PLAYERS,
                   "attackPlayer (%" PRIu32 ") must be < MAX_PLAYERS",
                   attackPlayer);
  CHECK_STRUCTURE(this);
  visRemoveVisibility(this);

  auto prevState = intGetResearchState();
  auto reward = electronicReward(this, attackPlayer);

  if (bMultiPlayer) {
    // certain structures give specific results - the rest swap sides!
    if (!electronic_warfare || !reward) {
      originalPlayer = getPlayer();
      //tell the system the structure no longer exists
      (void)removeStruct(this, false);

      // remove structure from one list
      removeStructureFromList(this, apsStructLists);

      setSelected(false);

      // change player id
      setPlayer(attackPlayer);

      //restore the resistance value
      setResistance((uint16_t)structureResistance(pimpl->stats.get(), getPlayer()));

      // add to other list.
      addStructure(this);

      // check through the 'attackPlayer' players list of droids to
      // see if any are targeting it
      for (auto& psCurr : apsDroidLists[attackPlayer])
      {
        if (psCurr.getOrder()->target == this) {
          orderDroid(&psCurr, ORDER_TYPE::STOP, ModeImmediate);
          break;
        }
        for (auto i = 0; i < numWeapons(psCurr); ++i)
        {
          if (psCurr.getTarget(i) == this) {
            orderDroid(&psCurr, ORDER_TYPE::STOP, ModeImmediate);
            break;
          }
        }
        // check through order list
        orderClearTargetFromDroidList(&psCurr, this);
      }

      // check through the 'attackPlayer' players list of structures
      // to see if any are targeting it
      for (auto& psStruct : apsStructLists[attackPlayer])
      {
        if (psStruct->psTarget[0] == this) {
          setStructureTarget(psStruct.get(), nullptr, 0, TARGET_ORIGIN::UNKNOWN);
        }
      }

      if (pimpl->state == STRUCTURE_STATE::BUILT) {
        buildingComplete(this);
      }
      //since the structure isn't being rebuilt, the visibility code needs to be adjusted
      //make sure this structure is visible to selectedPlayer
      visibilityState[attackPlayer] = UINT8_MAX;
      triggerEventObjectTransfer(this, originalPlayer);
    }
    intNotifyResearchButton(prevState);
    return nullptr;
  }

  // save info about the structure
  psType = pimpl->stats.get();
  x = getPosition().x;
  y = getPosition().y;
  direction = getRotation().direction;
  originalPlayer = getPlayer();

  // save how complete the build process is
  if (pimpl->state == STRUCTURE_STATE::BEING_BUILT) {
    buildPoints = pimpl->currentBuildPoints;
  }
  //check module not attached
  psModule = getModuleStat(this);
  //get rid of the structure
  (void)removeStruct(this, true);

  //make sure power is not used to build
  bPowerOn = powerCalculated;
  powerCalculated = false;
  //build a new one for the attacking player - set last element to true so it doesn't adjust x/y
  psNewStruct = buildStructure(psType, x, y, attackPlayer, true);
  capacity_ = capacity_;
  if (psNewStruct) {
    psNewStruct->rotation.direction = direction;
    if (capacity_) {
      switch (psType->type) {
        case STRUCTURE_TYPE::POWER_GEN:
        case STRUCTURE_TYPE::RESEARCH:
          //build the module for powerGen and research
          buildStructure(psModule, psNewStruct->getPosition().x,
                         psNewStruct->getPosition().y, attackPlayer, false);
          break;
        case STRUCTURE_TYPE::FACTORY:
        case STRUCTURE_TYPE::VTOL_FACTORY:
          //build the appropriate number of modules
          while (capacity_)
          {
            buildStructure(psModule, psNewStruct->getPosition().x, psNewStruct->getPosition().y, attackPlayer, false);
            capacity_--;
          }
          break;
        default:
          break;
      }
    }
    if (buildPoints) {
      psNewStruct->pimpl->state = STRUCTURE_STATE::BEING_BUILT;
      psNewStruct->pimpl->currentBuildPoints = buildPoints;
    }
    else {
      psNewStruct->pimpl->state = STRUCTURE_STATE::BUILT;
      buildingComplete(psNewStruct);
      triggerEventStructBuilt(psNewStruct, nullptr);
      checkPlayerBuiltHQ(psNewStruct);
    }

    if (!bMultiPlayer) {
      if (originalPlayer == selectedPlayer) {
        //make sure this structure is visible to selectedPlayer if the structure used to be selectedPlayers'
        ASSERT(selectedPlayer < MAX_PLAYERS, "selectedPlayer (%" PRIu32 ") must be < MAX_PLAYERS",
               selectedPlayer);
        psNewStruct->visibilityState[selectedPlayer] = UBYTE_MAX;
      }
      if (!electronic_warfare || !reward) {
        triggerEventObjectTransfer(psNewStruct, originalPlayer);
      }
    }
  }
  powerCalculated = bPowerOn;
  intNotifyResearchButton(prevState);
  return psNewStruct;
}

float Structure::structureCompletionProgress() const
{
  return pimpl
         ? MIN(1, pimpl->currentBuildPoints / (float)structureBuildPointsToCompletion(*this))
         : -1;
}

const StructureStats* Structure::getStats() const
{
  return pimpl ? pimpl->stats.get() : nullptr;
}

STRUCTURE_STATE Structure::getState() const
{
  return pimpl ? pimpl->state : STRUCTURE_STATE::BLUEPRINT_INVALID;
}

uint8_t Structure::getCapacity() const
{
  return pimpl ? pimpl->capacity : 0;
}

int Structure::getFoundationDepth() const noexcept
{
  return pimpl ? pimpl->foundationDepth : -1;
}

/* The main update routine for all Structures */
void Structure::structureUpdate(bool bMission)
{
  using enum STRUCTURE_TYPE;
  using enum STRUCTURE_ANIMATION_STATE;
  unsigned widthScatter, breadthScatter;
  unsigned emissionInterval, iPointsToAdd, iPointsRequired;
  Vector3i dv;
  int i;

  syncDebugStructure(this, '<');

  if (flags.test(static_cast<size_t>(OBJECT_FLAG::DIRTY)) && !bMission) {
    visTilesUpdate(this);
    flags.set(static_cast<size_t>(OBJECT_FLAG::DIRTY), false);
  }

  if (pimpl->stats->type == GATE) {
    if (pimpl->animationState == OPEN &&
        pimpl->lastStateTime + SAS_STAY_OPEN_TIME < gameTime) {
      bool found = false;

      static GridList gridList; // static to avoid allocations.
      gridList = gridStartIterate(getPosition().x, getPosition().y, TILE_UNITS);
      for (auto gi = gridList.begin(); !found && gi != gridList.end(); ++gi)
      {
        found = (bool)dynamic_cast<Droid*>(*gi);
      }

      if (!found) // no droids on our tile, safe to close
      {
        pimpl->animationState = CLOSING;
        auxStructureClosedGate(*this); // closed
        pimpl->lastStateTime = gameTime; // reset timer
      }
    }
    else if (pimpl->animationState == OPENING &&
             pimpl->lastStateTime + SAS_OPEN_SPEED < gameTime) {
      pimpl->animationState = OPEN;
      auxStructureOpenGate(*this); // opened
      pimpl->lastStateTime = gameTime; // reset timer
    }
    else if (pimpl->animationState == CLOSING &&
             pimpl->lastStateTime + SAS_OPEN_SPEED < gameTime) {
      pimpl->animationState = NORMAL;
      pimpl->lastStateTime = gameTime; // reset timer
    }
  }
  else if (pimpl->stats->type == RESOURCE_EXTRACTOR) {
    if (!psBuilding->pFunctionality->resourceExtractor.power_generator
        && animationEvent == ANIM_EVENT_ACTIVE) // no power generator connected
    {
      timeAnimationStarted = 0; // so turn off animation, if any
      animationEvent = ANIM_EVENT_NONE;
    }
    else if (psBuilding->pFunctionality->resourceExtractor.power_generator
             && animationEvent == ANIM_EVENT_NONE) // we have a power generator, but no animation
    {
      animationEvent = ANIM_EVENT_ACTIVE;

      auto strFirstImd = getDisplayData()->imd_shape->objanimpie[static_cast<int>(pimpl->animationState)];
      if (strFirstImd != nullptr && strFirstImd->next != nullptr) {
        auto strImd = strFirstImd->next; // first imd isn't animated
        timeAnimationStarted = gameTime + (rand() % (strImd->objanimframes * strImd->objanimtime));
        // vary animation start time
      }
      else {
        ASSERT(strFirstImd != nullptr && strFirstImd->next != nullptr, "Unexpected objanimpie");
        timeAnimationStarted = gameTime; // so start animation
      }
    }

    if (getPlayer() == selectedPlayer) {
      if (visibleToSelectedPlayer()
          // check for display(audio)-only - does not impact simulation / game state
          && psBuilding->pFunctionality->resourceExtractor.power_generator
          && animationEvent == ANIM_EVENT_ACTIVE) {
        audio_PlayObjStaticTrack(this, ID_SOUND_OIL_PUMP_2);
      }
      else {
        audio_StopObjTrack(this, ID_SOUND_OIL_PUMP_2);
      }
    }
  }

  // Remove invalid targets. This must be done each frame.
  for (i = 0; i < MAX_WEAPONS; i++)
  {
    if (psTarget[i] && psTarget[i]->died) {
      syncDebugObject(psTarget[i], '-');
      setStructureTarget(this, nullptr, i, TARGET_ORIGIN::UNKNOWN);
    }
  }

  //update the manufacture/research of the building once complete
  if (pimpl->state == STRUCTURE_STATE::BUILT) {
    aiUpdateStructure(bMission);
  }

  if (pimpl->state != STRUCTURE_STATE::BUILT) {
    if (isSelected()) {
      setSelected(false);
    }
  }

  if (!bMission) {
    if (pimpl->state == STRUCTURE_STATE::BEING_BUILT &&
        pimpl->buildRate == 0 &&
        !structureHasModules(this)) {
      if (pimpl->stats->power_cost == 0) {
        // Building is free, and not currently being built, so deconstruct slowly over 1 minute.
        pimpl->currentBuildPoints -= std::min<int>(pimpl->currentBuildPoints,
                                                     gameTimeAdjustedAverage(
                                                             structureBuildPointsToCompletion(*this), 60));
      }

      if (pimpl->currentBuildPoints == 0) {
        removeStruct(this, true);
        // If giving up on building something, remove the structure (and remove it from the power queue).
      }
    }
    pimpl->previousBuildRate = pimpl->buildRate;
   pimpl-> buildRate = 0; // Reset to 0, each truck building us will add to our buildRate.
  }

  /* Only add smoke if they're visible and they can 'burn' */
  if (!bMission && visibleToSelectedPlayer() && smokeWhenDamaged()) {
    const auto damage = getStructureDamage(this);

    // Is there any damage?
    if (damage > 0.) {
      emissionInterval = static_cast<unsigned>(CalcStructureSmokeInterval(damage / 65536.f));
      auto effectTime = std::max(gameTime - deltaGameTime + 1, pimpl->lastEmissionTime + emissionInterval);
      if (gameTime >= effectTime) {
        const auto size = getSize();
        widthScatter = size.x * TILE_UNITS / 2 / 3;
        breadthScatter = size.y * TILE_UNITS / 2 / 3;
        dv.x = getPosition().x + widthScatter - rand() % (2 * widthScatter);
        dv.z = getPosition().y + breadthScatter - rand() % (2 * breadthScatter);
        dv.y = getPosition().z;
        dv.y += (getDisplayData()->imd_shape->max.y * 3) / 4;
        addEffect(&dv, EFFECT_GROUP::SMOKE, EFFECT_TYPE::SMOKE_TYPE_DRIFTING_HIGH,
                  false, nullptr, 0, effectTime);
        lastEmissionTime = effectTime;
      }
    }
  }

  /* Update the fire damage data */
  if (periodicalDamageStartTime != 0 && periodicalDamageStartTime != gameTime - deltaGameTime)
    // -deltaGameTime, since projectiles are updated after structures.
  {
    // The periodicalDamageStartTime has been set, but is not from the previous tick, so we must be out of the fire.
    periodicalDamage = 0; // Reset burn damage done this tick.
    // Finished burning.
    periodicalDamageStartTime = 0;
  }

  //check the resistance level of the structure
  iPointsRequired = structureResistance(pimpl->stats.get(), getPlayer());
  if (getResistance() < (SWORD)iPointsRequired) {
    //start the resistance increase
    if (pimpl->lastResistance == ACTION_START_TIME) {
      pimpl->lastResistance = gameTime;
    }
    //increase over time if low
    if ((gameTime - pimpl->lastResistance) > RESISTANCE_INTERVAL) {
      setResistance(getResistance() + 1);

      //in multiplayer, certain structures do not function whilst low resistance
      if (bMultiPlayer) {
        resetResistanceLag(this);
      }

      pimpl->lastResistance = gameTime;
      //once the resistance is back up reset the last time increased
      if (getResistance() >= (SWORD)iPointsRequired) {
        pimpl->lastResistance = ACTION_START_TIME;
      }
    }
  }
  else {
    //if selfrepair has been researched then check the health level of the
    //structure once resistance is fully up
    iPointsRequired = structureBody(this);
    if (selfRepairEnabled(getPlayer()) && getHp() < iPointsRequired &&
        pimpl->state != STRUCTURE_STATE::BEING_BUILT) {
      //start the self repair off
      if (pimpl->lastResistance == ACTION_START_TIME) {
        pimpl->lastResistance = gameTime;
      }

      /*since self repair, then add half repair points depending on the time delay for the stat*/
      iPointsToAdd = (repairPoints(asRepairStats + aDefaultRepair[
              getPlayer()], getPlayer()) / 4)
                     * ((gameTime - pimpl->lastResistance)
                        / (asRepairStats + aDefaultRepair[getPlayer()])->time);

      //add the blue flashing effect for multiPlayer
      if (bMultiPlayer && ONEINTEN && !bMission) {
        Vector3i position;
        Vector3f* point;
        int realY;
        unsigned pointIndex;

        pointIndex = rand() % (getDisplayData()->imd_shape->points.size() - 1);
        point = &(getDisplayData()->imd_shape->points.at(pointIndex));
        position.x = static_cast<int>(getPosition().x + point->x);
        realY = static_cast<int>(structHeightScale(this) * point->y);
        position.y = getPosition().z + realY;
        position.z = static_cast<int>(getPosition().y - point->z);
        const auto psTile = mapTile(map_coord({position.x, position.y}));
        if (tileIsClearlyVisible(psTile)){
          effectSetSize(30);
          addEffect(&position, EFFECT_GROUP::EXPLOSION, EFFECT_TYPE::EXPLOSION_TYPE_SPECIFIED,
                    true, getImdFromIndex(MI_PLASMA),
                    0, gameTime - deltaGameTime + rand() % deltaGameTime);
        }
      }

      if (iPointsToAdd) {
        setHp((getHp() + iPointsToAdd));
        lastResistance = gameTime;
        if (getHp() > iPointsRequired) {
          setHp(iPointsRequired);
          lastResistance = ACTION_START_TIME;
        }
      }
    }
  }
  syncDebugStructure(this, '>');
  CHECK_STRUCTURE(this);
}

bool Structure::isWall() const noexcept
{
  return pimpl && pimpl->stats->type == STRUCTURE_TYPE::WALL ||
                  pimpl->stats->type == STRUCTURE_TYPE::WALL_CORNER;
}

STRUCTURE_ANIMATION_STATE Structure::getAnimationState() const
{
  return pimpl ? pimpl->animationState : STRUCTURE_ANIMATION_STATE::NORMAL;
}
    
void Structure::aiUpdateStructure(bool isMission)
{
  if (!pimpl) return;
  STRUCTURE_TYPE structureMode;
  Droid* psDroid;
  BaseObject* psChosenObjs[MAX_WEAPONS] = {nullptr};
  BaseObject* psChosenObj = nullptr;
  Factory* psFactory;
  RepairFacility* psRepairFac = nullptr;
  Vector3i iVecEffect;
  bool bDroidPlaced = false;
  WeaponStats const* psWStats = nullptr;
  bool bDirect = false;
  int xdiff, ydiff, mindist, currdist;
  TARGET_ORIGIN tmpOrigin = TARGET_ORIGIN::UNKNOWN;

  CHECK_STRUCTURE(this);

  if (getTime() == gameTime) {
    // This isn't supposed to happen, and really shouldn't be possible - if this happens, maybe a structure is being updated twice?
    int count1 = 0, count2 = 0;
    for (auto& s : apsStructLists[getPlayer()])
    {
      count1 += s == this;
    }
    for (auto s : mission.apsStructLists[getPlayer()])
    {
      count2 += s == this;
    }
    debug(LOG_ERROR, "psStructure->prevTime = %u, psStructure->time = %u, gameTime = %u, count1 = %d, count2 = %d",
          prevTime, time, gameTime, count1, count2);
    setTime(time - 1);
  }
  prevTime = time;
  setTime(gameTime);
  for (unsigned i = 0; i < MAX(1, numWeapons(*this)); ++i)
  {
    weapons[i].previousRotation = weapons[i].getRotation();
  }

  if (isMission) {
    using enum STRUCTURE_TYPE;
    switch (pimpl->stats->type) {
      case RESEARCH:
      case FACTORY:
      case CYBORG_FACTORY:
      case VTOL_FACTORY:
        break;
      default:
        return; // nothing to do
    }
  }

  // Will go out into a building EVENT stats/text file
  /* Spin round yer sensors! */
  if (numWeapons(*this) == 0) {
    if (pimpl->stats->type != STRUCTURE_TYPE::REPAIR_FACILITY) {
      // - radar should rotate every three seconds ... 'cause we timed it at Heathrow !
      // gameTime is in milliseconds - one rotation every 3 seconds = 1 rotation event 3000 millisecs
      auto direction = (uint16_t)((uint64_t)gameTime * 65536 / 3000) +
                                                   ((getPosition().x + getPosition().y) % 10) * 6550;
      // Randomize by hashing position as seed for rotating 1/10th turns. Cast wrapping intended.

      weapons[0].setRotation({direction, 0, weapons[0].getRotation().roll});
    }
  }

  /* Check lassat */
  if (isLasSat(pimpl->stats.get()) &&
      gameTime - weapons[0].timeLastFired > weaponFirePause(
              weapons[0].getStats(), getPlayer()) &&
      weapons[0].ammo > 0) {

    triggerEventStructureReady(this);
    weapons[0].ammo = 0; // do not fire more than once
  }

  /* See if there is an enemy to attack */
  if (numWeapons(*this) > 0) {
    //structures always update their targets
    for (auto i = 0; i < numWeapons(*this); i++)
    {
      bDirect = proj_Direct(weapons[i].getStats());
      if (weapons[i].getStats()->weaponSubClass != WEAPON_SUBCLASS::LAS_SAT) {
        if (aiChooseTarget(this, &psChosenObjs[i], i, true, &tmpOrigin)) {
          objTrace(getId(), "Weapon %d is targeting %d at (%d, %d)", i, psChosenObjs[i]->getId(),
                   psChosenObjs[i]->getPosition().x, psChosenObjs[i]->getPosition().y);
          setStructureTarget(this, psChosenObjs[i], i, tmpOrigin);
        }
        else {
          if (aiChooseTarget(this, &psChosenObjs[0], 0, true, &tmpOrigin)) {
            if (psChosenObjs[0]) {
              objTrace(getId(), "Weapon %d is supporting main weapon: %d at (%d, %d)", i,
                       psChosenObjs[0]->getId(), psChosenObjs[0]->getPosition().x, psChosenObjs[0]->getPosition().y);
              setStructureTarget(this, psChosenObjs[0], i, tmpOrigin);
              psChosenObjs[i] = psChosenObjs[0];
            }
            else {
              setStructureTarget(this, nullptr, i, TARGET_ORIGIN::UNKNOWN);
              psChosenObjs[i] = nullptr;
            }
          }
          else {
            setStructureTarget(this, nullptr, i, TARGET_ORIGIN::UNKNOWN);
            psChosenObjs[i] = nullptr;
          }
        }

        if (psChosenObjs[i] != nullptr && !psChosenObjs[i]->isProbablyDoomed(bDirect)) {
          // get the weapon stat to see if there is a visible turret to rotate
          psWStats = weapons[i].getStats();

          //if were going to shoot at something move the turret first then fire when locked on
          if (psWStats->pMountGraphic == nullptr) //no turret so lock on whatever
          {
            weapons[i].rotation.direction = calcDirection(
                    getPosition().x, getPosition().y, psChosenObjs[i]->getPosition().x, psChosenObjs[i]->getPosition().y);
            combFire(&weapons[i], this, psChosenObjs[i], i);
          }
          else if (actionTargetTurret(this, psChosenObjs[i], &weapons[i])) {
            combFire(&weapons[i], this, psChosenObjs[i], i);
          }
        }
        else {
          // realign the turret
          if ((weapons[i].getRotation().direction % DEG(90)) != 0 ||
               weapons[i].getRotation().pitch != 0) {
            actionAlignTurret(this, i);
          }
        }
      }
    }
  }

    /* See if there is an enemy to attack for Sensor Towers that have weapon droids attached*/
  else if (stats->sensor_stats) {
    if (hasStandardSensor() ||
        hasVtolInterceptSensor() ||
        isRadarDetector()) {
      if (aiChooseSensorTarget(this, &psChosenObj)) {
        objTrace(getId(), "Sensing (%d)", psChosenObj->getId());
        if (isRadarDetector()) {
          setStructureTarget(this, psChosenObj, 0, TARGET_ORIGIN::RADAR_DETECTOR);
        }
        else {
          setStructureTarget(this, psChosenObj, 0, TARGET_ORIGIN::SENSOR);
        }
      }
      else {
        setStructureTarget(this, nullptr, 0, TARGET_ORIGIN::UNKNOWN);
      }
      psChosenObj = psTarget[0];
    }
    else {
      psChosenObj = psTarget[0];
    }
  }

  /* Process the functionality according to type
  * determine the subject stats (for research or manufacture)
  * or base object (for repair) or update power levels for resourceExtractor
  */
  BaseStats* pSubject = nullptr;
  using enum STRUCTURE_TYPE;
  switch (pimpl->stats->type) {
    case RESEARCH:
    {
      pSubject = dynamic_cast<ResearchFacility*>(this)->psSubject;
      structureMode = RESEARCH;
      break;
    }
    case FACTORY:
    case CYBORG_FACTORY:
    case VTOL_FACTORY:
    {
      auto psFact = dynamic_cast<Factory*>(this);
      pSubject = psFact->psSubject;
      structureMode = FACTORY;
      //check here to see if the factory's commander has died
      if (psFact->psCommander &&
          psFact->psCommander->isDead()) {
        //remove the commander from the factory
        syncDebugDroid(psStructure->pFunctionality->factory.psCommander, '-');
        assignFactoryCommandDroid(this, nullptr);
      }
      break;
    }
    case REPAIR_FACILITY: // FIXME FIXME FIXME: Magic numbers in this section
    {
      psRepairFac = &psStructure->pFunctionality->repairFacility;
      psChosenObj = psRepairFac->psObj;
      structureMode = REPAIR_FACILITY;
      psDroid = dynamic_cast<::Droid*>(psChosenObj);

      // If the droid we're repairing just died, find a new one
      if (psDroid && psDroid->isDead()) {
//          syncDebugDroid(psDroid, '-');
        psDroid = nullptr;
        psChosenObj = nullptr;
        psRepairFac->psObj = nullptr;
      }

      // skip droids that are trying to get to other repair factories
      if (psDroid != nullptr
          && (!orderState(psDroid, ORDER_TYPE::RETURN_TO_REPAIR)
              || psDroid->getOrder().target != this)) {
        psDroid = dynamic_cast<::Droid*>(psChosenObj);
        xdiff = (int)psDroid->getPosition().x - (int)getPosition().x;
        ydiff = (int)psDroid->getPosition().y - (int)getPosition().y;
        // unless it has orders to repair here, forget about it when it gets out of range
        if (xdiff * xdiff + ydiff * ydiff > (TILE_UNITS * 5 / 2) * (TILE_UNITS * 5 / 2)) {
          psChosenObj = nullptr;
          psDroid = nullptr;
          psRepairFac->psObj = nullptr;
        }
      }

      // select next droid if none being repaired,
      // or look for a better droid if not repairing one with repair orders
      if (psChosenObj == nullptr ||
          (dynamic_cast<::Droid*>(psChosenObj)->getOrder().type != ORDER_TYPE::RETURN_TO_REPAIR &&
           dynamic_cast<::Droid*>(psChosenObj)->getOrder().type != ORDER_TYPE::RTR_SPECIFIED)) {
        //FIX ME: (doesn't look like we need this?)
        ASSERT(psRepairFac->group != nullptr, "invalid repair facility group pointer");

        // Tries to find most important droid to repair
        // Lower dist = more important
        // mindist contains lowest dist found so far
        mindist = (TILE_UNITS * 8) * (TILE_UNITS * 8) * 3;
        if (psChosenObj) {
          // We already have a valid droid to repair, no need to look at
          // droids without a repair order.
          mindist = (TILE_UNITS * 8) * (TILE_UNITS * 8) * 2;
        }
        psRepairFac->droidQueue = 0;
        for (auto& psDroid : apsDroidLists[getPlayer()])
        {
          auto const* psTarget = orderStateObj(&psDroid, ORDER_TYPE::RETURN_TO_REPAIR);

          // Highest priority:
          // Take any droid with orders to Return to Repair (DORDER_RTR),
          // or that have been ordered to this repair facility (DORDER_RTR_SPECIFIED),
          // or any "lost" unit with one of those two orders.
          if (((psDroid.getOrder().type == ORDER_TYPE::RETURN_TO_REPAIR ||
               (psDroid.getOrder().type == ORDER_TYPE::RTR_SPECIFIED &&
               (!psTarget || psTarget == this))) &&
               psDroid.getAction() != ACTION::WAIT_FOR_REPAIR &&
               psDroid.getAction() != ACTION::MOVE_TO_REPAIR_POINT &&
               psDroid.getAction() != ACTION::WAIT_DURING_REPAIR)
              || (psTarget && psTarget == this)) {

            if (psDroid.getHp() >= psDroid.getOriginalHp()) {
              objTrace(getId(), "Repair not needed of droid %d", (int)psDroid.getId());

              /* set droid points to max */
              psDroid.setHp(psDroid.getOriginalHp());

              // if completely repaired reset order
              psDroid.secondarySetState(SECONDARY_ORDER::RETURN_TO_LOCATION, DSS_NONE);

              if (psDroid.hasCommander()) {
                // return a droid to it's command group
                auto& psCommander = psDroid.getGroup().getCommander();

                orderDroidObj(&psDroid, ORDER_TYPE::GUARD, psCommander, ModeImmediate);
              }
              else if (psRepairFac->psDeliveryPoint != nullptr) {
                // move the droid out the way
                objTrace(psDroid.getId(), "Repair not needed - move to delivery point");
                orderDroidLoc(&psDroid, ORDER_TYPE::MOVE,
                              psRepairFac->psDeliveryPoint->coords.x,
                              psRepairFac->psDeliveryPoint->coords.y, ModeQueue);
                // ModeQueue because delivery points are not yet synchronised!
              }
              continue;
            }
            xdiff = (int)psDroid.getPosition().x - (int)getPosition().x;
            ydiff = (int)psDroid.getPosition().y - (int)getPosition().y;
            currdist = xdiff * xdiff + ydiff * ydiff;
            if (currdist < mindist && currdist < (TILE_UNITS * 8) * (TILE_UNITS * 8)) {
              mindist = currdist;
              psChosenObj = &psDroid;
            }
            if (psTarget && psTarget == this) {
              psRepairFac->droidQueue++;
            }
          }
            // Second highest priority:
            // Help out another nearby repair facility
          else if (psTarget && mindist > (TILE_UNITS * 8) * (TILE_UNITS * 8)
                   && psTarget != this && psDroid.getAction() == ACTION::WAIT_FOR_REPAIR) {
            int distLimit = mindist;
            if (dynamic_cast<Structure*>(psTarget) &&
                ((Structure*)psTarget)->stats->type == REPAIR_FACILITY) // Is a repair facility (not the HQ).
            {
              RepairFacility* stealFrom = &((Structure*)psTarget)->pFunctionality->repairFacility;
              // make a wild guess about what is a good distance
              distLimit = world_coord(stealFrom->droidQueue) * world_coord(stealFrom->droidQueue) * 10;
            }

            xdiff = (int)psDroid.getPosition().x - (int)getPosition().x;
            ydiff = (int)psDroid.getPosition().y - (int)getPosition().y;
            currdist = xdiff * xdiff + ydiff * ydiff + (TILE_UNITS * 8) * (TILE_UNITS * 8);
            // lower priority
            if (currdist < mindist && currdist - (TILE_UNITS * 8) * (TILE_UNITS * 8) < distLimit) {
              mindist = currdist;
              psChosenObj = &psDroid;
              psRepairFac->droidQueue++; // shared queue
              objTrace(psChosenObj->getId(),
                       "Stolen by another repair facility, currdist=%d, mindist=%d, distLimit=%d",
                       (int)currdist, (int)mindist, distLimit);
            }
          }
            // Lowest priority:
            // Just repair whatever is nearby and needs repairing.
          else if (mindist > (TILE_UNITS * 8) * (TILE_UNITS * 8) * 2 && psDroid.getHp() < psDroid.getOriginalHp()) {
            xdiff = (int)psDroid.getPosition().x - (int)getPosition().x;
            ydiff = (int)psDroid.getPosition().y - (int)getPosition().y;
            currdist = xdiff * xdiff + ydiff * ydiff + (TILE_UNITS * 8) * (TILE_UNITS * 8) * 2;
            // even lower priority
            if (currdist < mindist && currdist < (TILE_UNITS * 5 / 2) * (TILE_UNITS * 5 / 2) + (TILE_UNITS * 8) * (TILE_UNITS * 8) * 2)
            {
              mindist = currdist;
              psChosenObj = &psDroid;
            }
          }
        }
        if (!psChosenObj) // Nothing to repair? Repair allied units!
        {
          mindist = (TILE_UNITS * 5 / 2) * (TILE_UNITS * 5 / 2);

          for (auto i = 0; i < MAX_PLAYERS; i++)
          {
            if (aiCheckAlliances(i, getPlayer()) && i != getPlayer()) {
              for (auto& psDroid : apsDroidLists[i])
              {
                if (psDroid.getHp() < psDroid.getOriginalHp()) {
                  xdiff = (int)psDroid.getPosition().x - (int)getPosition().x;
                  ydiff = (int)psDroid.getPosition().y - (int)getPosition().y;
                  currdist = xdiff * xdiff + ydiff * ydiff;
                  if (currdist < mindist) {
                    mindist = currdist;
                    psChosenObj = &psDroid;
                  }
                }
              }
            }
          }
        }
        psDroid = dynamic_cast<::Droid*>(psChosenObj);
        if (psDroid) {
          if (psDroid->getOrder()->type == ORDER_TYPE::RETURN_TO_REPAIR ||
              psDroid->getOrder()->type == ORDER_TYPE::RTR_SPECIFIED) {
            // Hey, droid, it's your turn! Stop what you're doing and get ready to get repaired!
            psDroid->action = ACTION::WAIT_FOR_REPAIR;
            psDroid->order.target = this;
          }
          objTrace(getId(), "Chose to repair droid %d", (int)psDroid->getId());
          objTrace(psDroid->getId(), "Chosen to be repaired by repair structure %d", (int)getId());
        }
      }

      // send the droid to be repaired
      if (psDroid) {
        /* set chosen object */
        psChosenObj = psDroid;

        /* move droid to repair point at rear of facility */
        xdiff = (int)psDroid->getPosition().x - (int)getPosition().x;
        ydiff = (int)psDroid->getPosition().y - (int)getPosition().y;
        if (psDroid->getAction() == ACTION::WAIT_FOR_REPAIR ||
            (psDroid->getAction() == ACTION::WAIT_DURING_REPAIR
             && xdiff * xdiff + ydiff * ydiff > (TILE_UNITS * 5 / 2) * (TILE_UNITS * 5 / 2))) {
          objTrace(getId(), "Requesting droid %d to come to us", (int)psDroid->getId());
          ::actionDroid(psDroid, ACTION::MOVE_TO_REPAIR_POINT,
                      this, getPosition().x, getPosition().y);
        }
        /* reset repair started if we were previously repairing something else */
        if (psRepairFac->psObj != psDroid) {
          psRepairFac->psObj = psDroid;
        }
      }

      // update repair arm position
      if (psChosenObj) {
        actionTargetTurret(this, psChosenObj, &weapons[0]);
      }
      else if ((weapons[0].getRotation().direction % DEG(90)) != 0 ||
               weapons[0].getRotation().pitch != 0) {
        // realign the turret
        actionAlignTurret(this, 0);
      }
      break;
    }
    case REARM_PAD:
    {
      RearmPad* psReArmPad = &psStructure->pFunctionality->rearmPad;

      psChosenObj = psReArmPad->psObj;
      structureMode = REARM_PAD;
      psDroid = nullptr;

      /* select next droid if none being rearmed*/
      if (psChosenObj == nullptr) {
        objTrace(getId(), "Rearm pad idle - look for victim");
        for (auto& psDroid : apsDroidLists[player])
        {
          // move next droid waiting on ground to rearm pad
          if (vtolReadyToRearm(psDroid, *this) &&
              (psChosenObj == nullptr ||
              ((dynamic_cast<::Droid*>(psChosenObj)->timeActionStarted > psDroid.timeActionStarted)))) {
            objTrace(psDroid.getId(), "rearm pad candidate");
            objTrace(getId(), "we found %s to rearm", objInfo(&psDroid));
            psChosenObj = &psDroid;
          }
        }
        // None available? Try allies.
        for (auto i = 0; i < MAX_PLAYERS && !psChosenObj; i++)
        {
          if (aiCheckAlliances(i, player) && i != player) {
            for (auto& psDroid : apsDroidLists[i]) {
              // move next droid waiting on ground to rearm pad
              if (vtolReadyToRearm(psDroid, *this)) {
                psChosenObj = &psDroid;
                objTrace(psDroid.getId(), "allied rearm pad candidate");
                objTrace(getId(), "we found allied %s to rearm", objInfo(&psDroid));
                break;
              }
            }
          }
        }
        psDroid = (Droid*)psChosenObj;
        if (psDroid != nullptr) {
          ::actionDroid(psDroid, ACTION::MOVE_TO_REARM_POINT, this);
        }
      }
      else {
        psDroid = (Droid*)psChosenObj;
        if ((psDroid->getMovementData().status == MOVE_STATUS::INACTIVE ||
             psDroid->getMovementData().status == MOVE_STATUS::HOVER) &&
            psDroid->getAction() == ACTION::WAIT_FOR_REARM) {
          objTrace(psDroid->getId(), "supposed to go to rearm but not on our way -- fixing");
          // this should never happen...
          actionDroid(psDroid, ACTION::MOVE_TO_REARM_POINT, this);
        }
      }

      // if found a droid to rearm assign it to the rearm pad
      if (psDroid != nullptr) {
        /* set chosen object */
        psChosenObj = psDroid;
        psReArmPad->psObj = psChosenObj;
        if (psDroid->getAction() == ACTION::MOVE_TO_REARM_POINT) {
          /* reset rearm started */
          psReArmPad->timeStarted = ACTION_START_TIME;
          psReArmPad->timeLastUpdated = 0;
        }
        auxStructureBlocking(*this);
      }
      else {
        auxStructureNonblocking(*this);
      }
      break;
    }
    default:
      break;
  }

  /* check subject stats (for research or manufacture) */
  if (pSubject != nullptr) {
    //if subject is research...
    if (structureMode == RESEARCH) {
      ResearchFacility* psResFacility = &psStructure->pFunctionality->researchFacility;

      //if on hold don't do anything
      if (psResFacility->timeStartHold) {
        delPowerRequest(this);
        return;
      }

      //electronic warfare affects the functionality of some structures in multiPlayer
      if (bMultiPlayer && resistance < (int)structureResistance(
              stats.get(), getPlayer())) {
        return;
      }

      int researchIndex = pSubject->ref - STAT_RESEARCH;

      PlayerResearch* pPlayerRes = &asPlayerResList[getPlayer()][researchIndex];
      //check research has not already been completed by another structure
      if (!IsResearchCompleted(pPlayerRes)) {
        auto pResearch = (ResearchStats*)pSubject;

        unsigned pointsToAdd = gameTimeAdjustedAverage(getBuildingResearchPoints(this));
        pointsToAdd = MIN(pointsToAdd, pResearch->researchPointsRequired - pPlayerRes->currentPoints);

        unsigned shareProgress = pPlayerRes->currentPoints;
        // Share old research progress instead of new one, so it doesn't get sped up by multiple players researching.
        bool shareIsFinished = false;

        if (pointsToAdd > 0 && pPlayerRes->currentPoints == 0)
        {
          bool haveEnoughPower = requestPowerFor(this, pResearch->powerCost);
          if (haveEnoughPower)
          {
            shareProgress = 1;
            // Share research payment, to avoid double payment even if starting research in the same game tick.
          }
          else
          {
            pointsToAdd = 0;
          }
        }

        if (pointsToAdd > 0 && pResearch->researchPointsRequired > 0) // might be a "free" research
        {
          pPlayerRes->currentPoints += pointsToAdd;
        }
        syncDebug("Research at %u/%u.", pPlayerRes->currentPoints, pResearch->researchPointsRequired);

        //check if Research is complete
        if (pPlayerRes->currentPoints >= pResearch->researchPointsRequired)
        {
          int prevState = intGetResearchState();

          //store the last topic researched - if its the best
          if (psResFacility->psBestTopic == nullptr)
          {
            psResFacility->psBestTopic = psResFacility->psSubject;
          }
          else
          {
            if (pResearch->researchPointsRequired > psResFacility->psBestTopic->researchPointsRequired)
            {
              psResFacility->psBestTopic = psResFacility->psSubject;
            }
          }
          psResFacility->psSubject = nullptr;
          intResearchFinished(this);
          researchResult(researchIndex, player, true, this, true);

          shareIsFinished = true;

          //check if this result has enabled another topic
          intNotifyResearchButton(prevState);
        }

        // Update allies research accordingly
        if (game.type == LEVEL_TYPE::SKIRMISH && alliancesSharedResearch(game.alliance)) {
          for (uint8_t i = 0; i < MAX_PLAYERS; i++)
          {
            if (alliances[i][getPlayer()] != ALLIANCE_FORMED) continue;
            if (IsResearchCompleted(&asPlayerResList[i][researchIndex])) continue;
            // Share the research for that player.
            auto& allyProgress = asPlayerResList[i][researchIndex].currentPoints;
            allyProgress = std::max(allyProgress, shareProgress);
            if (shareIsFinished) {
              researchResult(researchIndex, i, false, nullptr, true);
            }
          }
        }
      }
      else {
        //cancel this Structure's research since now complete
        psResFacility->psSubject = nullptr;
        intResearchFinished(this);
        syncDebug("Research completed elsewhere.");
      }
    }
      //check for manufacture
    else if (structureMode == FACTORY) {
      psFactory = &psStructure->pFunctionality->factory;

      //if on hold don't do anything
      if (psFactory->timeStartHold)
      {
        return;
      }

      //electronic warfare affects the functionality of some structures in multiPlayer
      if (bMultiPlayer && getResistance() < (int)structureResistance(
              pimpl->stats.get(), getPlayer())) {
        return;
      }

      if (psFactory->timeStarted == ACTION_START_TIME)
      {
        // also need to check if a command droid's group is full

        // If the factory commanders group is full - return
        if (IsFactoryCommanderGroupFull(psFactory) || checkHaltOnMaxUnitsReached(this, isMission))
        {
          return;
        }

        //set the time started
        psFactory->timeStarted = gameTime;
      }

      if (psFactory->buildPointsRemaining > 0)
      {
        int progress = gameTimeAdjustedAverage(getBuildingProductionPoints(psStructure));
        if ((unsigned)psFactory->buildPointsRemaining == calcTemplateBuild(psFactory->psSubject) && progress >
                                                                                                    0)
        {
          // We're just starting to build, check for power.
          bool haveEnoughPower = requestPowerFor(psStructure, calcTemplatePower(psFactory->psSubject));
          if (!haveEnoughPower)
          {
            progress = 0;
          }
        }
        psFactory->buildPointsRemaining -= progress;
      }

      //check for manufacture to be complete
      if (psFactory->buildPointsRemaining <= 0 && !IsFactoryCommanderGroupFull(psFactory) && !
              checkHaltOnMaxUnitsReached(this, isMission)) {
        if (isMission) {
          // put it in the mission list
          psDroid = buildMissionDroid((DroidTemplate*)pSubject,
                                      getPosition().x, getPosition().y,
                                      getPlayer());
          if (psDroid) {
            psDroid->secondaryOrder = psFactory->secondaryOrder;
            psDroid->secondaryOrderPending = psDroid->secondaryOrder;
            setFactorySecondaryState(psDroid, this);
            psDroid->setBase(this);
            bDroidPlaced = true;
          }
        }
        else {
          // place it on the map
          bDroidPlaced = structPlaceDroid(this, (DroidTemplate*)pSubject, &psDroid);
        }

        //script callback, must be called after factory was flagged as idle
        if (bDroidPlaced) {
          //reset the start time
          psFactory->timeStarted = ACTION_START_TIME;
          psFactory->psSubject = nullptr;

          doNextProduction(this, (DroidTemplate*)pSubject, ModeImmediate);

          cbNewDroid(this, psDroid);
        }
      }
    }
  }

  /* check base object (for repair / rearm) */
  if (psChosenObj != nullptr) {
    if (structureMode == REPAIR_FACILITY) {
      psDroid = (Droid*)psChosenObj;
      ASSERT_OR_RETURN(, psDroid != nullptr, "invalid droid pointer");
      psRepairFac = &psStructure->pFunctionality->repairFacility;

      xdiff = (int)psDroid->getPosition().x - (int)getPosition().x;
      ydiff = (int)psDroid->getPosition().y - (int)getPosition().y;
      if (xdiff * xdiff + ydiff * ydiff <= (TILE_UNITS * 5 / 2) * (TILE_UNITS * 5 / 2)) {
        //check droid is not healthy
        if (psDroid->getHp() < psDroid->getOriginalHp()) {
          //if in multiPlayer, and a Transporter - make sure its on the ground before repairing
          if (bMultiPlayer && isTransporter(*psDroid)) {
            if (!(psDroid->getMovementData().status == MOVE_STATUS::INACTIVE &&
                  psDroid->getMovementData().vertical_speed == 0)) {
              objTrace(getId(), "Waiting for transporter to land");
              return;
            }
          }

          //don't do anything if the resistance is low in multiplayer
          if (bMultiPlayer && getResistance() < (int)structureResistance(
                  pimpl->stats.get(), getPlayer())) {
            objTrace(getId(), "Resistance too low for repair");
            return;
          }

          psDroid->setHp(psDroid->getHp() + gameTimeAdjustedAverage(getBuildingRepairPoints(this)));
        }

        if (psDroid->getHp() >= psDroid->getOriginalHp()) {
          objTrace(getId(), "Repair complete of droid %d", (int)psDroid->getId());

          psRepairFac->psObj = nullptr;

          /* set droid points to max */
          psDroid->setHp(psDroid->getOriginalHp());

          if ((psDroid->getOrder().type == ORDER_TYPE::RETURN_TO_REPAIR ||
               psDroid->getOrder().type == ORDER_TYPE::RTR_SPECIFIED) &&
               psDroid->getOrder().target == this) {
            // if completely repaired reset order
            psDroid->secondarySetState(SECONDARY_ORDER::RETURN_TO_LOCATION, DSS_NONE);

            if (psDroid->hasCommander()) {
              // return a droid to it's command group
              auto psCommander = psDroid->group->psCommander;

              objTrace(psDroid->getId(), "Repair complete - move to commander");
              orderDroidObj(psDroid, ORDER_TYPE::GUARD, psCommander, ModeImmediate);
            }
            else if (psRepairFac->psDeliveryPoint != nullptr) {
              // move the droid out the way
              objTrace(psDroid->getId(), "Repair complete - move to delivery point");
              orderDroidLoc(psDroid, ORDER_TYPE::MOVE,
                            psRepairFac->psDeliveryPoint->coords.x,
                            psRepairFac->psDeliveryPoint->coords.y, ModeQueue);
              // ModeQueue because delivery points are not yet synchronised!
            }
          }
        }

        if (visibleToSelectedPlayer() && psDroid->visibleToSelectedPlayer())
          // display only - does not impact simulation state
        {
          /* add plasma repair effect whilst being repaired */
          iVecEffect.x = psDroid->getPosition().x + (10 - rand() % 20);
          iVecEffect.y = psDroid->getPosition().z + (10 - rand() % 20);
          iVecEffect.z = psDroid->getPosition().y + (10 - rand() % 20);
          effectSetSize(100);
          addEffect(&iVecEffect, EFFECT_GROUP::EXPLOSION, EFFECT_TYPE::EXPLOSION_TYPE_SPECIFIED,
                    true, getImdFromIndex(MI_FLAME), 0, gameTime - deltaGameTime + 1);
        }
      }
    }
      //check for rearming
    else if (structureMode == REARM_PAD) {
      RearmPad* psReArmPad = &psStructure->pFunctionality->rearmPad;
      unsigned pointsAlreadyAdded;

      psDroid = (Droid*)psChosenObj;
      ASSERT_OR_RETURN(, psDroid != nullptr, "invalid droid pointer");
      ASSERT_OR_RETURN(, psDroid->isVtol(), "invalid droid type");

      //check hasn't died whilst waiting to be rearmed
      // also clear out any previously repaired droid
      if (psDroid->died || (psDroid->getAction() != ACTION::MOVE_TO_REARM_POINT &&
          psDroid->getAction() != ACTION::WAIT_DURING_REARM)) {
        syncDebugDroid(psDroid, '-');
        psReArmPad->psObj = nullptr;
        objTrace(psDroid->getId(), "VTOL has wrong action or is dead");
        return;
      }
      if (psDroid->getAction() == ACTION::WAIT_DURING_REARM &&
          psDroid->getMovementData().status == MOVE_STATUS::INACTIVE) {
        if (psReArmPad->timeStarted == ACTION_START_TIME) {
          //set the time started and last updated
          psReArmPad->timeStarted = gameTime;
          psReArmPad->timeLastUpdated = gameTime;
        }
        unsigned pointsToAdd = getBuildingRearmPoints(this) * (gameTime - psReArmPad->timeStarted) /
                               GAME_TICKS_PER_SEC;
        pointsAlreadyAdded = getBuildingRearmPoints(this) * (psReArmPad->timeLastUpdated - psReArmPad->
                timeStarted) / GAME_TICKS_PER_SEC;
        if (pointsToAdd >= psDroid->getWeight()) // amount required is a factor of the droid weight
        {
          // We should be fully loaded by now.
          for (auto i = 0; i < numWeapons(*psDroid); i++)
          {
            // set rearm value to no runs made
            psDroid->weapons[i].ammoUsed = 0;
            psDroid->weapons[i].ammo = psDroid->getWeapons()[i].getStats()->upgraded[psDroid->getPlayer()].numRounds;
            psDroid->weapons[i].timeLastFired = 0;
          }
          objTrace(psDroid->getId(), "fully loaded");
        }
        else {
          for (unsigned i = 0; i < numWeapons(*psDroid); i++) // rearm one weapon at a time
          {
            // Make sure it's a rearmable weapon (and so we don't divide by zero)
            if (psDroid->getWeapons()[i].ammoUsed > 0 &&
                psDroid->getWeapons()[i].getStats()->upgraded[psDroid
                    ->getPlayer()].numRounds > 0) {
              // Do not "simplify" this formula.
              // It is written this way to prevent rounding errors.
              auto ammoToAddThisTime =
                      pointsToAdd * getNumAttackRuns(psDroid, i) / psDroid->getWeight() -
                      pointsAlreadyAdded * getNumAttackRuns(psDroid, i) / psDroid->getWeight();
              psDroid->weapons[i].ammoUsed -= std::min<unsigned>(
                      ammoToAddThisTime, psDroid->weapons[i].ammoUsed);
              if (ammoToAddThisTime) {
                // reset ammo and lastFired
                psDroid->weapons[i].ammo = asWeaponStats[psDroid->weapons[i].nStat].upgraded[psDroid->
                        getPlayer()].numRounds;
                psDroid->weapons[i].timeLastFired = 0;
                break;
              }
            }
          }
        }
        if (psDroid->getHp() < psDroid->getOriginalHp()) // do repairs
        {
          psDroid->setHp(psDroid->getHp() + gameTimeAdjustedAverage(getBuildingRepairPoints(this)));
          if (psDroid->getHp() >= psDroid->getOriginalHp()) {
            psDroid->setHp(psDroid->getOriginalHp());
          }
        }
        psReArmPad->timeLastUpdated = gameTime;

        //check for fully armed and fully repaired
        if (vtolHappy(*psDroid)) {
          //clear the rearm pad
          psDroid->getAction() = ACTION::NONE;
          psReArmPad->psObj = nullptr;
          auxStructureNonblocking(*this);
          triggerEventDroidIdle(psDroid);
          objTrace(psDroid->getId(), "VTOL happy and ready for action!");
        }
      }
    }
  }
}

/*this is called whenever a structure has finished building*/
void Structure::buildingComplete()
{
  CHECK_STRUCTURE(this);

  using enum STRUCTURE_TYPE;
  int prevState = 0;
  if (stats->type == RESEARCH) {
    prevState = intGetResearchState();
  }

  currentBuildPoints = structureBuildPointsToCompletion(*this);
  state = BUILT;

  visTilesUpdate(this);

  if (prebuiltImd != nullptr) {
    // We finished building a module, now use the combined IMD.
    auto IMDs = stats->IMDs;
    int imdIndex = std::min<int>(numStructureModules(this) * 2, IMDs.size() - 1);
    // *2 because even-numbered IMDs are structures, odd-numbered IMDs are just the modules.
    prebuiltImd = nullptr;
    display->imd_shape = IMDs[imdIndex];
  }

  switch (stats->type) {
    case POWER_GEN:
      checkForResExtractors(this);
      if (selectedPlayer == getPlayer()) {
        audio_PlayObjStaticTrack(this, ID_SOUND_POWER_HUM);
      }
      break;
    case RESOURCE_EXTRACTOR:
      checkForPowerGen(this);
      break;
    case RESEARCH:
      //this deals with research facilities that are upgraded whilst mid-research
      releaseResearch(this, ModeImmediate);
      intNotifyResearchButton(prevState);
      break;
    case FACTORY:
    case CYBORG_FACTORY:
    case VTOL_FACTORY:
      //this deals with factories that are upgraded whilst mid-production
      releaseProduction(this, ModeImmediate);
      break;
    case SAT_UPLINK:
      revealAll(getPlayer());
      break;
    case GATE:
      auxStructureNonblocking(*this); // Clear outdated flags.
      auxStructureClosedGate(*this); // Don't block for the sake of allied pathfinding.
      break;
    default:
      //do nothing
      break;
  }
}

  std::unique_ptr<Structure> Structure::buildStructureDir(StructureStats* pStructureType,
                                                          unsigned x, unsigned y, uint16_t direction,
                                                          unsigned player, bool FromSave)
  {
    const auto size = pStructureType->size(direction);

    ASSERT_OR_RETURN(nullptr, player < MAX_PLAYERS, "Cannot build structure for player %" PRIu32 " (>= MAX_PLAYERS)",
                     player);
    ASSERT_OR_RETURN(nullptr, pStructureType && pStructureType->type != STRUCTURE_TYPE::DEMOLISH, "You cannot build demolition!");

    std::unique_ptr<Structure> psBuilding;

    if (!IsStatExpansionModule(pStructureType)) {
      int preScrollMinX = 0, preScrollMinY = 0, preScrollMaxX = 0, preScrollMaxY = 0;
      auto max = pStructureType - asStructureStats;
      int i;

      ASSERT_OR_RETURN(nullptr, max <= numStructureStats, "Invalid structure type");

      // Don't allow more than interface limits
      if (asStructureStats[max].curCount[player] + 1 > asStructureStats[max].upgraded_stats[player].limit) {
        debug(LOG_ERROR, "Player %u: Building %s could not be built due to building limits (has %u, max %u)!",
              player, getStatsName(pStructureType), asStructureStats[max].curCount[player],
              asStructureStats[max].upgraded_stats[player].limit);
        return nullptr;
      }

      // snap the coords to a tile
      x = (x & ~TILE_MASK) + size.x % 2 * TILE_UNITS / 2;
      y = (y & ~TILE_MASK) + size.y % 2 * TILE_UNITS / 2;

      //check not trying to build too near the edge
      if (map_coord(x) < TOO_NEAR_EDGE ||
          map_coord(x) > (mapWidth - TOO_NEAR_EDGE)) {
        debug(LOG_WARNING, "attempting to build too closely to map-edge, "
                           "x coord (%u) too near edge (req. distance is %u)", x, TOO_NEAR_EDGE);
        return nullptr;
      }
      if (map_coord(y) < TOO_NEAR_EDGE ||
          map_coord(y) > (mapHeight - TOO_NEAR_EDGE)) {
        debug(LOG_WARNING, "attempting to build too closely to map-edge, "
                           "y coord (%u) too near edge (req. distance is %u)", y, TOO_NEAR_EDGE);
        return nullptr;
      }

      WallOrientation wallOrientation = WallConnectNone;
      if (!FromSave && isWallCombiningStructureType(pStructureType)) {
        for (int dy = 0; dy < size.y; ++dy)
          for (int dx = 0; dx < size.x; ++dx)
          {
            auto pos = map_coord(Vector2i(x, y) - size * TILE_UNITS / 2) + Vector2i(dx, dy);
            wallOrientation = structChooseWallType(player, pos);
            // This makes neighbouring walls match us, even if we're a hardpoint, not a wall.
          }
      }

      // allocate memory for and initialize a structure object
      psBuilding = std::make_unique<Structure>(generateSynchronisedObjectId(), player);
      if (!psBuilding) {
        return nullptr;
      }

      //fill in other details
      psBuilding->stats = std::make_shared<StructureStats>(pStructureType);

      psBuilding->setPosition({x, y, psBuilding->getPosition().z});
      psBuilding->setRotation({snapDirection(direction), 0, 0});

      //This needs to be done before the functionality bit...
      //load into the map data and structure list if not an upgrade
      Vector2i map = map_coord(Vector2i(x, y)) - size / 2;

      //set up the imd to use for the display
      psBuilding->display->imd_shape = std::make_unique<iIMDShape>(*pStructureType->IMDs[0]);

      psBuilding->animationState = STRUCTURE_ANIMATION_STATE::NORMAL;
      psBuilding->lastStateTime = gameTime;

      /* if resource extractor - need to remove oil feature first, but do not do any
       * consistency checking here - save games do not have any feature to remove
       * to remove when placing oil derricks! */
      if (pStructureType->type == STRUCTURE_TYPE::RESOURCE_EXTRACTOR) {
        Feature* psFeature = getTileFeature(map_coord(x), map_coord(y));

        if (psFeature && psFeature->getStats()->subType == FEATURE_TYPE::OIL_RESOURCE) {
          if (fireOnLocation(psFeature->getPosition().x, psFeature->getPosition().y)) {
            // Can't build on burning oil resource
            return nullptr;
          }
          // remove it from the map
          turnOffMultiMsg(true); // don't send this one!
          removeFeature(psFeature);
          turnOffMultiMsg(false);
        }
      }

      for (int tileY = map.y; tileY < map.y + size.y; ++tileY)
      {
        for (int tileX = map.x; tileX < map.x + size.x; ++tileX)
        {
          Tile* psTile = mapTile(tileX, tileY);

          /* Remove any walls underneath the building. You can build defense buildings on top
           * of walls, you see. This is not the place to test whether we own it! */
          if (isBuildableOnWalls(pStructureType->type) &&
              TileHasWall(psTile)) {
            removeStruct(dynamic_cast<Structure*>(psTile->psObject), true);
          }
          else if (TileHasStructure(psTile)) {
#if defined(WZ_CC_GNU) && !defined(WZ_CC_INTEL) && !defined(WZ_CC_CLANG) && (7 <= __GNUC__)
            # pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wnull-dereference"
#endif
            debug(LOG_ERROR, "Player %u (%s): is building %s at (%d, %d) but found %s already at (%d, %d)",
                  player, isHumanPlayer(player) ? "Human" : "AI", getStatsName(pStructureType), map.x, map.y,
                  getStatsName(getTileStructure(tileX, tileY)->getStats()), tileX, tileY);
#if defined(WZ_CC_GNU) && !defined(WZ_CC_INTEL) && !defined(WZ_CC_CLANG) && (7 <= __GNUC__)
# pragma GCC diagnostic pop
#endif
            return nullptr;
          }
        }
      }
      for (int tileY = map.y; tileY < map.y + size.y; ++tileY)
      {
        for (int tileX = map.x; tileX < map.x + size.x; ++tileX)
        {
          // We now know the previous loop didn't return early, so it is safe to save references to psBuilding now.
          auto psTile = mapTile(tileX, tileY);
          psTile->psObject = this;

          // if it's a tall structure then flag it in the map.
          if (psBuilding->getDisplayData().imd_shape->max.y > TALLOBJECT_YMAX) {
            auxSetBlocking(tileX, tileY, AIR_BLOCKED);
          }
        }
      }

      switch (pStructureType->type) {
        case STRUCTURE_TYPE::REARM_PAD:
          break; // Not blocking.
        default:
          auxStructureBlocking(*psBuilding);
          break;
      }

      //set up the rest of the data
      for (i = 0; i < MAX_WEAPONS; i++)
      {
        psBuilding->weapons[i].setRotation({0, 0, 0});
        psBuilding->weapons[i].previousRotation = psBuilding->weapons[i].getRotation();
        psBuilding->weapons[i].origin = TARGET_ORIGIN::UNKNOWN;
        psBuilding->weapons[i] = nullptr;
      }

      psBuilding->periodicalDamageStartTime = 0;
      psBuilding->periodicalDamage = 0;

      psBuilding->state = STRUCTURE_STATE::BEING_BUILT;
      psBuilding->currentBuildPoints = 0;

      alignStructure(psBuilding.get());

      /* Store the weapons */
      if (numWeapons(*pStructureType) > 0) {
        unsigned weapon;

        for (weapon = 0; weapon < pStructureType->numWeaps; weapon++)
        {
          if (pStructureType->psWeapStat[weapon]) {
            psBuilding->weapons[weapon].timeLastFired = 0;
            psBuilding->weapons[weapon].shotsFired = 0;
            //in multiPlayer make the Las-Sats require re-loading from the start
            if (bMultiPlayer &&
                pStructureType->psWeapStat[0]->
                        weaponSubClass == WEAPON_SUBCLASS::LAS_SAT) {
              psBuilding->weapons[0].timeLastFired = gameTime;
            }
            psBuilding->weapons[weapon].stats = pStructureType->psWeapStat[weapon];
            psBuilding->weapons[weapon].ammo = psBuilding->weapons[weapon].getStats()->upgraded[psBuilding->getPlayer()].numRounds;
          }
        }
      }
      else {
        if (pStructureType->psWeapStat[0]) {
          psBuilding->weapons[0].timeLastFired = 0;
          psBuilding->weapons[0].shotsFired = 0;
          //in multiPlayer make the Las-Sats require re-loading from the start
          if (bMultiPlayer &&
              pStructureType->psWeapStat[0]->
                      weaponSubClass == WEAPON_SUBCLASS::LAS_SAT) {
            psBuilding->weapons[0].timeLastFired = gameTime;
          }
          psBuilding->weapons[0].stats = pStructureType->
                  psWeapStat[0] - asWeaponStats;

          psBuilding->weapons[0].ammo = psBuilding->weapons[0].getStats()->
                  upgraded[getPlayer()].numRounds;
        }
      }

      resistance = (uint16_t)structureResistance(pStructureType, player);
      lastResistance = ACTION_START_TIME;

      // Do the visibility stuff before setFunctionality - so placement of DP's can work
      memset(psBuilding->seenThisTick, 0, sizeof(psBuilding->seenThisTick));

      // Structure is visible to anyone with shared vision.
      for (unsigned vPlayer = 0; vPlayer < MAX_PLAYERS; ++vPlayer)
      {
        psBuilding->visibilityState[vPlayer] = hasSharedVision(vPlayer, player) ? UINT8_MAX : 0;
      }

      // Reveal any tiles that can be seen by the structure
      visTilesUpdate(psBuilding.get());

      /*if we're coming from a SAVEGAME and we're on an Expand_Limbo mission,
      any factories that were built previously for the selectedPlayer will
      have DP's in an invalid location - the scroll limits will have been
      changed to not include them. This is the only HACK I can think of to
      enable them to be loaded up. So here goes...*/
      if (FromSave && player == selectedPlayer && missionLimboExpand()) {
        //save the current values
        preScrollMinX = scrollMinX;
        preScrollMinY = scrollMinY;
        preScrollMaxX = scrollMaxX;
        preScrollMaxY = scrollMaxY;
        //set the current values to mapWidth/mapHeight
        scrollMinX = 0;
        scrollMinY = 0;
        scrollMaxX = mapWidth;
        scrollMaxY = mapHeight;
        // NOTE: resizeRadar() may be required here, since we change scroll limits?
      }
      //set the functionality dependent on the type of structure
      if (!setFunctionality(this, pStructureType->type)) {
        removeStructFromMap(this);
        delete psBuilding;
        //better reset these if you couldn't build the structure!
        if (FromSave && player == selectedPlayer && missionLimboExpand()) {
          //reset the current values
          scrollMinX = preScrollMinX;
          scrollMinY = preScrollMinY;
          scrollMaxX = preScrollMaxX;
          scrollMaxY = preScrollMaxY;
          // NOTE: resizeRadar() may be required here, since we change scroll limits?
        }
        return nullptr;
      }

      //reset the scroll values if adjusted
      if (FromSave && player == selectedPlayer && missionLimboExpand()) {
        //reset the current values
        scrollMinX = preScrollMinX;
        scrollMinY = preScrollMinY;
        scrollMaxX = preScrollMaxX;
        scrollMaxY = preScrollMaxY;
        // NOTE: resizeRadar() may be required here, since we change scroll limits?
      }

      // rotate a wall if necessary
      if (!FromSave &&
          (pStructureType->type == STRUCTURE_TYPE::WALL ||
           pStructureType->type == STRUCTURE_TYPE::GATE)) {
        psBuilding->pFunctionality->wall.type = wallType(wallOrientation);

        if (wallOrientation != WallConnectNone) {
          psBuilding->setRotation({wallDir(wallOrientation),
                                   psBuilding->getRotation().pitch,
                                   psBuilding->getRotation().roll});

          psBuilding->display->imd_shape = psBuilding->getStats()->IMDs[std::min<unsigned>(
                  psBuilding->pFunctionality->wall.type, psBuilding->getStats()->IMDs.size() - 1)];
        }
      }

      psBuilding->setHp(structureBody(psBuilding.get()));
      psBuilding->expectedDamage = 0; // Begin life optimistically.

      //add the structure to the list - this enables it to be drawn whilst being built
      ::addStructure(psBuilding.get());

      asStructureStats[max].curCount[player]++;

      if (isLasSat(psBuilding->getStats())) {
        psBuilding->weapons[0].ammo = 1; // ready to trigger the fire button
      }

      // Move any delivery points under the new structure.
      auto bounds = getStructureBounds(psBuilding.get());
      for (unsigned playerNum = 0; playerNum < MAX_PLAYERS; ++playerNum)
      {
        for (auto& psStruct : apsStructLists[playerNum])
        {
          FlagPosition* fp = nullptr;
          if (StructIsFactory(psStruct)) {
            fp = psStruct->pFunctionality->factory.psAssemblyPoint;
          }
          else if (psStruct->getStats()->type == STRUCTURE_TYPE::REPAIR_FACILITY) {
            fp = psStruct->pFunctionality->repairFacility.psDeliveryPoint;
          }
          if (fp != nullptr) {
            Vector2i pos = map_coord(fp->coords.xy());
            if (unsigned(pos.x - bounds.map.x) < unsigned(bounds.size.x) &&
                unsigned(pos.y - bounds.map.y) < unsigned(bounds.size.y)) {
              // Delivery point fp is under the new structure. Need to move it.
              setAssemblyPoint(fp, fp->coords.x, fp->coords.y, playerNum, true);
            }
          }
        }
      }
    }
    else //its an upgrade {
      bool bUpgraded = false;
      auto bodyDiff = 0;

      //don't create the Structure use existing one
      psBuilding = getTileStructure(map_coord(x), map_coord(y));

      if (!psBuilding) {
        return nullptr;
      }

      int prevResearchState = intGetResearchState();

      if (pStructureType->type == STRUCTURE_TYPE::FACTORY_MODULE) {
        if (psBuilding->getStats()->type != STRUCTURE_TYPE::FACTORY &&
            psBuilding->getStats()->type != STRUCTURE_TYPE::VTOL_FACTORY) {
          return nullptr;
        }
        //increment the capacity and output for the owning structure
        if (psBuilding->capacity < BODY_SIZE::SUPER_HEAVY) {
          //store the % difference in body points before upgrading
          bodyDiff = 65536 - getStructureDamage(psBuilding.get());

          psBuilding->capacity++;
          bUpgraded = true;
          //put any production on hold
          holdProduction(psBuilding.get(), ModeImmediate);
        }
      }

      if (pStructureType->type == STRUCTURE_TYPE::RESEARCH_MODULE) {
        if (psBuilding->getStats()->type != STRUCTURE_TYPE::RESEARCH) {
          return nullptr;
        }
        //increment the capacity and research points for the owning structure
        if (psBuilding->capacity == 0) {
          //store the % difference in body points before upgrading
          bodyDiff = 65536 - getStructureDamage(psBuilding.get());

          psBuilding->capacity++;
          bUpgraded = true;
          //cancel any research - put on hold now
          if (psBuilding->pFunctionality->researchFacility.psSubject) {
            //cancel the topic
            holdResearch(psBuilding.get(), ModeImmediate);
          }
        }
      }

      if (pStructureType->type == STRUCTURE_TYPE::POWER_MODULE) {
        if (psBuilding->getStats()->type != STRUCTURE_TYPE::POWER_GEN) {
          return nullptr;
        }
        //increment the capacity and research points for the owning structure
        if (psBuilding->capacity == 0) {
          //store the % difference in body points before upgrading
          bodyDiff = 65536 - getStructureDamage(psBuilding.get());

          //increment the power output, multiplier and capacity
          psBuilding->capacity++;
          bUpgraded = true;

          //need to inform any res Extr associated that not digging until complete
          releasePowerGen(psBuilding.get());
        }
      }
      if (bUpgraded) {
        auto IMDs = psBuilding->getStats()->IMDs;
        int imdIndex = std::min<int>(psBuilding->capacity * 2, IMDs.size() - 1) - 1;
        // *2-1 because even-numbered IMDs are structures, odd-numbered IMDs are just the modules, and we want just the module since we cache the fully-built part of the building in psBuilding->prebuiltImd.
        if (imdIndex < 0) {
          // Looks like we don't have a model for this structure's upgrade
          // Log it and default to the base model (to avoid a crash)
          debug(LOG_ERROR, "No upgraded structure model to draw.");
          imdIndex = 0;
        }
        psBuilding->prebuiltImd = std::make_unique<iIMDShape>(*psBuilding->getDisplayData().imd_shape);
        psBuilding->display->imd_shape = std::make_unique<iIMDShape>(*IMDs[imdIndex]);

        //calculate the new body points of the owning structure
        psBuilding->setHp(structureBody(psBuilding.get()) * bodyDiff / 65536);

        //initialise the build points
        psBuilding->currentBuildPoints = 0;
        //start building again
        psBuilding->state = STRUCTURE_STATE::BEING_BUILT;
        psBuilding->buildRate = 1; // Don't abandon the structure first tick, so set to nonzero.

        if (!FromSave) {
          triggerEventStructureUpgradeStarted(psBuilding.get());

          if (psBuilding->getPlayer() == selectedPlayer) {
            intRefreshScreen();
          }
        }
      }
      intNotifyResearchButton(prevResearchState);
    if (pStructureType->type != STRUCTURE_TYPE::WALL &&
        pStructureType->type != STRUCTURE_TYPE::WALL_CORNER) {
      if (player == selectedPlayer) {
        scoreUpdateVar(WD_STR_BUILT);
      }
    }

    /* why is this necessary - it makes tiles under the structure visible */
    setUnderTilesVis(psBuilding.get(), player);

    psBuilding->prevTime = gameTime - deltaGameTime; // Structure hasn't been updated this tick, yet.
    psBuilding->setTime(psBuilding->prevTime - 1); // -1, so the times are different, even before updating.

    return psBuilding;
  }

  std::unique_ptr<Structure> Structure::buildBlueprint(StructureStats const* psStats, Vector3i pos, uint16_t direction,
                                                       unsigned moduleIndex, STRUCTURE_STATE state_, uint8_t ownerPlayer)
  {
    ASSERT_OR_RETURN(nullptr, psStats != nullptr, "No blueprint stats");
    ASSERT_OR_RETURN(nullptr, psStats->IMDs[0] != nullptr, "No blueprint model for %s", getStatsName(psStats));
    ASSERT_OR_RETURN(nullptr, ownerPlayer < MAX_PLAYERS, "invalid ownerPlayer: %" PRIu8 "", ownerPlayer);

    Rotation rot {direction, 0, 0};

    auto moduleNumber = 0;
    const auto pIMD = &psStats->IMDs;
    if (IsStatExpansionModule(psStats)) {
      auto baseStruct = dynamic_cast<Structure*>(worldTile(pos.xy())->psObject);
      if (baseStruct != nullptr) {
        if (moduleIndex == 0) {
          moduleIndex = nextModuleToBuild(baseStruct, 0);
        }
        auto baseModuleNumber = moduleIndex * 2 - 1;
        // *2-1 because even-numbered IMDs are structures, odd-numbered IMDs are just the modules.
        const auto basepIMD = &baseStruct->getStats()->IMDs;
        if ((unsigned)baseModuleNumber < basepIMD->size()) {
          // Draw the module.
          moduleNumber = baseModuleNumber;
          pIMD = basepIMD;
          pos = baseStruct->getPosition();
          rot = baseStruct->getRotation();
        }
      }
    }

    auto blueprint = std::make_unique<Structure>(0, ownerPlayer);
    // construct the fake structure
    blueprint->stats = std::make_shared<StructureStats>(psStats);
    // Couldn't be bothered to fix const correctness everywhere.
    if (selectedPlayer < MAX_PLAYERS) {
      blueprint->visibilityState[selectedPlayer] = UBYTE_MAX;
    }
    blueprint->display->imd_shape = (*pIMD)[std::min<int>(moduleNumber, pIMD->size() - 1)];
    blueprint->setPosition(pos);
    blueprint->setRotation(rot);
    blueprint->selected = false;

    blueprint->numWeaps = 0;
    blueprint->weapons[0].stats = 0;

    // give defensive structures a weapon
    if (psStats->psWeapStat[0]) {
      blueprint->weapons[0].nStat = psStats->psWeapStat[0] - asWeaponStats;
    }
    // things with sensors or ecm (or repair facilities) need these set, even if they have no official weapon
    blueprint->numWeaps = 0;
    blueprint->weapons[0].timeLastFired = 0;
    blueprint->weapons[0].setRotation({0, 0, 0});
    blueprint->weapons[0].previousRotation = blueprint->weapons[0].getRotation();

    blueprint->expectedDamage = 0;

    // Times must be different, but don't otherwise matter.
    blueprint->setTime(23);
    blueprint->prevTime = 42;

    blueprint->state = state_;

    // Rotate wall if needed.
    if (blueprint->getStats()->type == STRUCTURE_TYPE::WALL ||
        blueprint->getStats()->type == STRUCTURE_TYPE::GATE) {
      WallOrientation scanType = structChooseWallTypeBlueprint(map_coord(blueprint->getPosition().xy()));
      unsigned type = wallType(scanType);
      if (scanType != WallConnectNone) {
        blueprint->setRotation({direction = wallDir(scanType), blueprint->getRotation().pitch, blueprint->getRotation().roll});
        blueprint->display->imd_shape = blueprint->getStats()->IMDs[std::min<unsigned>(
                type, blueprint->getStats()->IMDs.size() - 1)];
      }
    }
    return blueprint;
  }

  int Structure::requestOpenGate()
  {
    if (state != STRUCTURE_STATE::BUILT ||
        stats->type != STRUCTURE_TYPE::GATE) {
      return 0; // Can't open.
    }

    using enum STRUCTURE_ANIMATION_STATE;
    switch (animationState) {
      case NORMAL:
        lastStateTime = gameTime;
        animationState = OPENING;
        break;
      case OPEN:
        lastStateTime = gameTime;
        return 0; // Already open.
      case OPENING:
        break;
      case CLOSING:
        lastStateTime = 2 * gameTime - lastStateTime - SAS_OPEN_SPEED;
        animationState = OPENING;
        return 0; // Busy
    }
    return lastStateTime + SAS_OPEN_SPEED - gameTime;
  }

// Set the command droid that factory production should go to
void Factory::assignFactoryCommandDroid(Droid* commander)
{
  FlagPosition *psFlag, *psNext, *psPrev;
  int factoryInc, typeFlag;
  CHECK_STRUCTURE(this);

  switch (getStats()->type) {
    case STRUCTURE_TYPE::FACTORY:
      typeFlag = FACTORY_FLAG;
      break;
    case STRUCTURE_TYPE::VTOL_FACTORY:
      typeFlag = VTOL_FLAG;
      break;
    case STRUCTURE_TYPE::CYBORG_FACTORY:
      typeFlag = CYBORG_FLAG;
      break;
    default:
      ASSERT(!"unknown factory type", "unknown factory type");
      typeFlag = FACTORY_FLAG;
      break;
  }

  // removing a commander from a factory
  if (psCommander) {
    if (typeFlag == FACTORY_FLAG) {
      psCommander->secondarySetState(SECONDARY_ORDER::CLEAR_PRODUCTION,
                        (SECONDARY_STATE)(1 << (
                                psAssemblyPoint->factoryInc + DSS_ASSPROD_SHIFT)));
    }
    else if (typeFlag == CYBORG_FLAG) {
      psCommander->secondarySetState(SECONDARY_ORDER::CLEAR_PRODUCTION,
                        (SECONDARY_STATE)(1 << (
                                psAssemblyPoint->factoryInc + DSS_ASSPROD_CYBORG_SHIFT)));
    }
    else {
      psCommander->secondarySetState(SECONDARY_ORDER::CLEAR_PRODUCTION,
                        (SECONDARY_STATE)(1 << (
                                psAssemblyPoint->factoryInc + DSS_ASSPROD_VTOL_SHIFT)));
    }

    psCommander = nullptr;
    // TODO: Synchronise .psCommander.
    //syncDebug("Removed commander from factory %d", psStruct->id);
    if (!missionIsOffworld()) {
      ::addFlagPosition(psAssemblyPoint); // add the assembly point back into the list
    }
    else {
      psAssemblyPoint->psNext = mission.apsFlagPosLists[psAssemblyPoint->player];
      mission.apsFlagPosLists[psAssemblyPoint->player] = psAssemblyPoint.get();
    }
  }

  if (psCommander != nullptr) {
    ASSERT_OR_RETURN(, !missionIsOffworld(), "cannot assign a commander to a factory when off world");

    factoryInc = psAssemblyPoint->factoryInc;
    psPrev = nullptr;

    for (psFlag = apsFlagPosLists[getPlayer()]; psFlag; psFlag = psNext)
    {
      psNext = psFlag->psNext;

      if ((psFlag->factoryInc == factoryInc) && (psFlag->factoryType == typeFlag))
      {
        if (psFlag != psAssemblyPoint.get()) {
          ::removeFlagPosition(psFlag);
        }
        else {
          // need to keep the assembly point(s) for the factory
          // but remove it(the primary) from the list so it doesn't get
          // displayed
          if (psPrev == nullptr) {
            apsFlagPosLists[getPlayer()] = psFlag->psNext;
          }
          else {
            psPrev->psNext = psFlag->psNext;
          }
          psFlag->psNext = nullptr;
        }
      }
      else {
        psPrev = psFlag;
      }
    }
    psCommander = commander;
    syncDebug("Assigned commander %d to factory %d", psCommander->getId(), getId());
  }
}

/**
 * This function sets the structure's secondary state to be pState.
 * return true except on an ASSERT (which is not a good design.)
 * or, an invalid factory.
 */
bool Factory::getFactoryState(SECONDARY_ORDER sec, SECONDARY_STATE* pState)
{
  unsigned state = secondaryOrder;
  using enum SECONDARY_ORDER;
  switch (sec) {
    case ATTACK_RANGE:
      *pState = (SECONDARY_STATE)(state & DSS_ARANGE_MASK);
      break;
    case REPAIR_LEVEL:
      *pState = (SECONDARY_STATE)(state & DSS_REPLEV_MASK);
      break;
    case ATTACK_LEVEL:
      *pState = (SECONDARY_STATE)(state & DSS_ALEV_MASK);
      break;
    case PATROL:
      *pState = (SECONDARY_STATE)(state & DSS_PATROL_MASK);
      break;
    case HALT_TYPE:
      *pState = (SECONDARY_STATE)(state & DSS_HALT_MASK);
      break;
    default:
      *pState = (SECONDARY_STATE)0;
      break;
  }
}

/**
 * This function changes the structure's secondary state to be the
 * function input's state
 *
 * @return true if the function changed the structure's state,
 * and false if it did not
 */
bool Factory::setFactoryState(SECONDARY_ORDER sec, SECONDARY_STATE state)
{
  unsigned currState = secondaryOrder;
  auto retVal = bool{true};

  using enum SECONDARY_ORDER;
  switch (sec) {
    case ATTACK_RANGE:
      currState = (currState & ~DSS_ARANGE_MASK) | state;
      break;
    case REPAIR_LEVEL:
      currState = (currState & ~DSS_REPLEV_MASK) | state;
      break;
    case ATTACK_LEVEL:
      currState = (currState & ~DSS_ALEV_MASK) | state;
      break;
    case PATROL:
      if (state & DSS_PATROL_SET) {
        currState |= DSS_PATROL_SET;
      }
      else {
        currState &= ~DSS_PATROL_MASK;
      }
      break;
    case HALT_TYPE:
      switch (state & DSS_HALT_MASK) {
        case DSS_HALT_PURSUE:
          currState &= ~ DSS_HALT_MASK;
          currState |= DSS_HALT_PURSUE;
          break;
        case DSS_HALT_GUARD:
          currState &= ~ DSS_HALT_MASK;
          currState |= DSS_HALT_GUARD;
          break;
        case DSS_HALT_HOLD:
          currState &= ~ DSS_HALT_MASK;
          currState |= DSS_HALT_HOLD;
          break;
      }
      break;
    default:
      break;
  }
  secondaryOrder = currState;
  return retVal;
}

void initStructLimits()
{
  for (unsigned i = 0; i < numStructureStats; ++i)
  {
    memset(asStructureStats[i].curCount, 0, sizeof(asStructureStats[i].curCount));
  }
}

void structureInitVars()
{
	ASSERT(asStructureStats == nullptr, "Failed to cleanup prior asStructureStats?");

	asStructureStats = nullptr;
	lookupStructStatPtr.clear();
	numStructureStats = 0;
	factoryModuleStat = 0;
	powerModuleStat = 0;
	researchModuleStat = 0;
	lastMaxUnitMessage = 0;

	initStructLimits();
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		droidLimit[i] = INT16_MAX;
		commanderLimit[i] = INT16_MAX;
		constructorLimit[i] = INT16_MAX;
		for (int j = 0; j < NUM_FLAG_TYPES; j++)
		{
			factoryNumFlag[i][j].clear();
		}
	}

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		satUplinkExists[i] = false;
		lasSatExists[i] = false;
	}

	// initialise the selectedPlayer's production run
	for (auto& type : asProductionRun)
	{
		type.clear();
	}
	// set up at beginning of game which player will have a production list
	productionPlayer = selectedPlayer;
}

/*Initialise the production list and set up the production player*/
void changeProductionPlayer(unsigned player)
{
	// clear the production run
	for (auto& type : asProductionRun)
	{
		type.clear();
	}
	// set this player to have the production list
	productionPlayer = player;
}


/*initialises the flag before a new data set is loaded up*/
void initFactoryNumFlag()
{
	for (auto& i : factoryNumFlag)
	{
		// initialise the flag
		for (auto& j : i)
		{
			j.clear();
		}
	}
}

//called at start of missions
void resetFactoryNumFlag()
{
	for (auto psStruct : apsStructLists)
	{
		for (auto type = 0; type < NUM_FLAG_TYPES; type++)
		{
			// reset them all to false
			factoryNumFlag[i][type].clear();
		}

		// look through the list of structures to see which have been used
		for (; psStruct != nullptr; psStruct = psStruct.psNext)
		{
			FLAG_TYPE type;
			switch (psStruct.type) {
        using enum STRUCTURE_TYPE;
		  	case FACTORY:
          type = FACTORY_FLAG;
		  		break;
		  	case CYBORG_FACTORY:
          type = CYBORG_FLAG;
		  		break;
		  	case VTOL_FACTORY:
          type = VTOL_FLAG;
		  		break;
		  	case REPAIR_FACILITY:
          type = REPAIR_FLAG;
		  		break;
		  	default:
          continue;
			}

			int inc = -1;
			if (type == REPAIR_FLAG) {
				auto psRepair = dynamic_cast<RepairFacility*>(psStruct.get());
				if (psRepair->psDeliveryPoint != nullptr) {
					inc = psRepair->psDeliveryPoint->factoryInc;
				}
			}
			else {
				auto psFactory = dynamic_cast<Factory*>(psStruct.get());
				if (psFactory->psAssemblyPoint != nullptr) {
					inc = psFactory->psAssemblyPoint->factoryInc;
				}
			}
			if (inc >= 0) {
				factoryNumFlag[i][type].resize(std::max<size_t>(
                factoryNumFlag[i][type].size(), inc + 1), false);
				factoryNumFlag[i][type][inc] = true;
			}
		}
	}
}

static const StringToEnum<STRUCTURE_TYPE> map_STRUCTURE_TYPE[] =
{
	{"HQ", STRUCTURE_TYPE::HQ},
	{"FACTORY", STRUCTURE_TYPE::FACTORY},
	{"FACTORY MODULE", STRUCTURE_TYPE::FACTORY_MODULE},
	{"RESEARCH", STRUCTURE_TYPE::RESEARCH},
	{"RESEARCH MODULE", STRUCTURE_TYPE::RESEARCH_MODULE},
	{"POWER GENERATOR", STRUCTURE_TYPE::POWER_GEN},
	{"POWER MODULE", STRUCTURE_TYPE::POWER_MODULE},
	{"RESOURCE EXTRACTOR", STRUCTURE_TYPE::RESOURCE_EXTRACTOR},
	{"DEFENSE", STRUCTURE_TYPE::DEFENSE},
	{"WALL", STRUCTURE_TYPE::WALL},
	{"CORNER WALL", STRUCTURE_TYPE::WALL_CORNER},
	{"REPAIR FACILITY", STRUCTURE_TYPE::REPAIR_FACILITY},
	{"COMMAND RELAY", STRUCTURE_TYPE::COMMAND_CONTROL},
	{"DEMOLISH", STRUCTURE_TYPE::DEMOLISH},
	{"CYBORG FACTORY", STRUCTURE_TYPE::CYBORG_FACTORY},
	{"VTOL FACTORY", STRUCTURE_TYPE::VTOL_FACTORY},
	{"LAB", STRUCTURE_TYPE::LAB},
	{"GENERIC", STRUCTURE_TYPE::GENERIC},
	{"REARM PAD", STRUCTURE_TYPE::REARM_PAD},
	{"MISSILE SILO", STRUCTURE_TYPE::MISSILE_SILO},
	{"SAT UPLINK", STRUCTURE_TYPE::SAT_UPLINK},
	{"GATE", STRUCTURE_TYPE::GATE},
	{"LASSAT", STRUCTURE_TYPE::LASSAT},
};

static const StringToEnum<STRUCTURE_STRENGTH> map_STRUCT_STRENGTH[] =
{
	{"SOFT", STRUCTURE_STRENGTH::SOFT},
	{"MEDIUM", STRUCTURE_STRENGTH::MEDIUM},
	{"HARD", STRUCTURE_STRENGTH::HARD},
	{"BUNKER", STRUCTURE_STRENGTH::BUNKER}
};

static void initModuleStats(unsigned i, STRUCTURE_TYPE type)
{
	// need to work out the stats for the modules
  // HACK! - but less hacky than what was here before
	switch (type) {
	case STRUCTURE_TYPE::FACTORY_MODULE:
		//store the stat for easy access later on
		factoryModuleStat = i;
		break;

	case STRUCTURE_TYPE::RESEARCH_MODULE:
		// store the stat for easy access later on
		researchModuleStat = i;
		break;

	case STRUCTURE_TYPE::POWER_MODULE:
		// store the stat for easy access later on
		powerModuleStat = i;
		break;
	default:
		break;
	}
}

template <typename T, size_t N>
inline
std::size_t sizeOfArray(const T (&)[N])
{
	return N;
}

/* load the structure stats from the ini file */
bool loadStructureStats(WzConfig& ini)
{
	std::map<WzString, STRUCTURE_TYPE> structType;
	for (unsigned i = 0; i < sizeOfArray(map_STRUCTURE_TYPE); ++i)
	{
		structType.emplace(WzString::fromUtf8(map_STRUCTURE_TYPE[i].string), map_STRUCTURE_TYPE[i].value);
	}

	std::map<WzString, STRUCTURE_STRENGTH> structStrength;
	for (unsigned i = 0; i < sizeOfArray(map_STRUCT_STRENGTH); ++i)
	{
		structStrength.emplace(WzString::fromUtf8(map_STRUCT_STRENGTH[i].string), map_STRUCT_STRENGTH[i].value);
	}

	ASSERT(ini.isAtDocumentRoot(), "WzConfig instance is in the middle of traversal");
	std::vector<WzString> list = ini.childGroups();
	asStructureStats = new StructureStats[list.size()];
	numStructureStats = list.size();
	for (size_t inc = 0; inc < list.size(); ++inc)
	{
		ini.beginGroup(list[inc]);
		StructureStats* psStats = &asStructureStats[inc];
		loadStructureStats_BaseStats(ini, psStats, inc);
		lookupStructStatPtr.insert(std::make_pair(psStats->id, psStats));

		psStats->ref = STAT_STRUCTURE + inc;

		// set structure type
		WzString type = ini.value("type", "").toWzString();
		ASSERT_OR_RETURN(false, structType.find(type) != structType.end(), "Invalid type '%s' of structure '%s'",
		                 type.toUtf8().c_str(), getID(psStats));
		psStats->type = structType[type];

		// save indexes of special structures for futher use
		initModuleStats(inc, psStats->type); // This function looks like a hack. But slightly less hacky than before.

		if (ini.contains("userLimits"))
		{
			Vector3i limits = ini.vector3i("userLimits");
			psStats->minLimit = limits[0];
			psStats->maxLimit = limits[2];
			psStats->base.limit = limits[1];
		}
		else
		{
			psStats->minLimit = 0;
			psStats->maxLimit = LOTS_OF;
			psStats->base.limit = LOTS_OF;
		}
		psStats->base.research = ini.value("researchPoints", 0).toInt();
		psStats->base.moduleResearch = ini.value("moduleResearchPoints", 0).toInt();
		psStats->base.production = ini.value("productionPoints", 0).toInt();
		psStats->base.moduleProduction = ini.value("moduleProductionPoints", 0).toInt();
		psStats->base.repair = ini.value("repairPoints", 0).toInt();
		psStats->base.power = ini.value("powerPoints", 0).toInt();
		psStats->base.modulePower = ini.value("modulePowerPoints", 0).toInt();
		psStats->base.rearm = ini.value("rearmPoints", 0).toInt();
		psStats->base.resistance = ini.value("resistance", 0).toUInt();
		psStats->base.hitPoints = ini.value("hitpoints", 1).toUInt();
		psStats->base.armour = ini.value("armour", 0).toUInt();
		psStats->base.thermal = ini.value("thermal", 0).toUInt();
		for (auto& upgraded_stat : psStats->upgraded_stats)
		{
			upgraded_stat.limit = psStats->base.limit;
			upgraded_stat.research = psStats->base.research;
			upgraded_stat.moduleResearch = psStats->base.moduleResearch;
			upgraded_stat.power = psStats->base.power;
			upgraded_stat.modulePower = psStats->base.modulePower;
			upgraded_stat.repair = psStats->base.repair;
			upgraded_stat.production = psStats->base.production;
			upgraded_stat.moduleProduction = psStats->base.moduleProduction;
			upgraded_stat.rearm = psStats->base.rearm;
			upgraded_stat.resistance = ini.value("resistance", 0).toUInt();
			upgraded_stat.hitPoints = ini.value("hitpoints", 1).toUInt();
			upgraded_stat.armour = ini.value("armour", 0).toUInt();
			upgraded_stat.thermal = ini.value("thermal", 0).toUInt();
		}

		psStats->flags = 0;
		std::vector<WzString> flags = ini.value("flags").toWzStringList();
		for (auto& flag : flags)
		{
			if (flag == "Connected") {
				psStats->flags |= STRUCTURE_CONNECTED;
			}
		}

		// set structure strength
		WzString strength = ini.value("strength", "").toWzString();
		ASSERT_OR_RETURN(false, structStrength.find(strength) != structStrength.end(),
		                 "Invalid strength '%s' of structure '%s'", strength.toUtf8().c_str(), getID(psStats));
		psStats->strength = structStrength[strength];

		// set baseWidth
		psStats->base_width = ini.value("width", 0).toUInt();
		ASSERT_OR_RETURN(false, psStats->base_width <= 100, "Invalid width '%d' for structure '%s'", psStats->base_width,
                     getID(psStats));

		// set baseBreadth
		psStats->base_breadth = ini.value("breadth", 0).toUInt();
		ASSERT_OR_RETURN(false, psStats->base_breadth < 100, "Invalid breadth '%d' for structure '%s'",
                     psStats->base_breadth, getID(psStats));

		psStats->height = ini.value("height").toUInt();
		psStats->power_cost = ini.value("buildPower").toUInt();
		psStats->build_point_cost = ini.value("buildPoints").toUInt();

		// set structure models
		std::vector<WzString> models = ini.value("structureModel").toWzStringList();
		for (auto& model : models)
		{
			auto imd = modelGet(model.trimmed());
			ASSERT(imd != nullptr, "Cannot find the PIE structureModel '%s' for structure '%s'",
			       model.toUtf8().c_str(), getID(psStats));
			psStats->IMDs.push_back(std::make_unique<iIMDShape>(*imd));
		}

		// set base model
		WzString baseModel = ini.value("baseModel", "").toWzString();
		if (baseModel.compare("") != 0)
		{
			auto imd = modelGet(baseModel);
			ASSERT(imd != nullptr, "Cannot find the PIE baseModel '%s' for structure '%s'", baseModel.toUtf8().c_str(),
			       getID(psStats));
			psStats->base_imd = std::make_unique<iIMDShape>(*imd);
		}

		int ecm = getCompFromName(COMPONENT_TYPE::ECM, ini.value("ecmID", "ZNULLECM").toWzString());
		ASSERT(ecm >= 0, "Invalid ECM found for '%s'", getID(psStats));
		psStats->ecm_stats = asECMStats + ecm;

		int sensor = getCompFromName(COMPONENT_TYPE::SENSOR, ini.value("sensorID", "ZNULLSENSOR").toWzString());
		ASSERT(sensor >= 0, "Invalid sensor found for structure '%s'", getID(psStats));
		psStats->sensor_stats = asSensorStats + sensor;

		// set list of weapons
		std::fill_n(psStats->psWeapStat, MAX_WEAPONS, (WeaponStats*)nullptr);
		std::vector<WzString> weapons = ini.value("weapons").toWzStringList();
		ASSERT_OR_RETURN(false, weapons.size() <= MAX_WEAPONS,
		                 "Too many weapons are attached to structure '%s'. Maximum is %d", getID(psStats), MAX_WEAPONS);
		psStats->numWeaps = weapons.size();
		for (unsigned j = 0; j < psStats->numWeaps; ++j)
		{
			WzString weaponsID = weapons[j].trimmed();
			int weapon = getCompFromName(COMPONENT_TYPE::WEAPON, weaponsID);
			ASSERT_OR_RETURN(false, weapon >= 0, "Invalid item '%s' in list of weapons of structure '%s' ",
			                 weaponsID.toUtf8().c_str(), getID(psStats));
			WeaponStats* pWeap = asWeaponStats + weapon;
			psStats->psWeapStat[j] = pWeap;
		}

		// check used structure turrets
		int types = 0;
		types += psStats->numWeaps != 0;
		types += psStats->ecm_stats != nullptr && psStats->ecm_stats->location == static_cast<int>(LOC::TURRET);
    types += psStats->sensor_stats != nullptr && psStats->sensor_stats->location == static_cast<int>(LOC::TURRET);
		ASSERT(types <= 1, "Too many turret types for structure '%s'", getID(psStats));

		psStats->combines_with_wall = ini.value("combinesWithWall", false).toBool();
		ini.endGroup();
	}
	parseFavoriteStructs();

	/* get global dummy stat pointer - GJ */
	g_psStatDestroyStruct = nullptr;
	for (unsigned iID = 0; iID < numStructureStats; ++iID)
	{
		if (asStructureStats[iID].type == STRUCTURE_TYPE::DEMOLISH) {
			g_psStatDestroyStruct = asStructureStats + iID;
			break;
		}
	}
	ASSERT_OR_RETURN(false, g_psStatDestroyStruct, "Destroy structure stat not found");
	return true;
}

/* set the current number of structures of each type built */
void setCurrentStructQuantity(bool displayError)
{
	for (auto player = 0; player < MAX_PLAYERS; player++)
	{
		for (auto inc = 0; inc < numStructureStats; inc++)
		{
			asStructureStats[inc].curCount[player] = 0;
		}
		for (const auto& psCurr : apsStructLists[player])
		{
			auto inc = psCurr->pStructureType - asStructureStats;
			asStructureStats[inc].curCount[player]++;
			if (displayError) {
				//check quantity never exceeds the limit
				ASSERT(asStructureStats[inc].curCount[player] <=
            asStructureStats[inc].upgraded_stats[player].limit,
				  "There appears to be too many %s on this map!",
          getStatsName(&asStructureStats[inc]));
			}
		}
	}
}

/*Load the Structure Strength Modifiers from the file exported from Access*/
bool loadStructureStrengthModifiers(WzConfig& ini)
{
	//initialise to 100%
	for (unsigned i = 0; i < WEAPON_EFFECT::COUNT; ++i)
	{
		for (unsigned j = 0; j < STRUCTURE_STRENGTH::COUNT; ++j)
		{
			asStructStrengthModifier[i][j] = 100;
		}
	}
	ASSERT(ini.isAtDocumentRoot(), "WzConfig instance is in the middle of traversal");
	std::vector<WzString> list = ini.childGroups();
	for (auto& i : list)
	{
		WEAPON_EFFECT effectInc;
		ini.beginGroup(i);
		if (!getWeaponEffect(i, &effectInc)) {
			debug(LOG_FATAL, "Invalid Weapon Effect - %s", i.toUtf8().c_str());
			ini.endGroup();
			continue;
		}
		std::vector<WzString> keys = ini.childKeys();
		for (auto& strength : keys)
		{
			auto modifier = ini.value(strength).toInt();
			// FIXME - add support for dynamic categories
			if (strength.compare("SOFT") == 0) {
				asStructStrengthModifier[static_cast<int>(effectInc)][0] = modifier;
			}
			else if (strength.compare("MEDIUM") == 0) {
				asStructStrengthModifier[static_cast<int>(effectInc)][1] = modifier;
			}
			else if (strength.compare("HARD") == 0) {
				asStructStrengthModifier[static_cast<int>(effectInc)][2] = modifier;
			}
			else if (strength.compare("BUNKER") == 0) {
				asStructStrengthModifier[static_cast<int>(effectInc)][3] = modifier;
			}
			else {
				debug(LOG_ERROR, "Unsupported structure strength %s", strength.toUtf8().c_str());
			}
		}
		ini.endGroup();
	}
	return true;
}

bool structureStatsShutDown()
{
	packFavoriteStructs();
	if (asStructureStats) {
		for (unsigned i = 0; i < numStructureStats; ++i)
		{
			unloadStructureStats_BaseStats(asStructureStats[i]);
		}
	}
	lookupStructStatPtr.clear();
	delete[] asStructureStats;
	asStructureStats = nullptr;
	numStructureStats = 0;
	return true;
}

/* Deals damage to a Structure.
 * \param psStructure structure to deal damage to
 * \param damage amount of damage to deal
 * \param weaponClass the class of the weapon that deals the damage
 * \param weaponSubClass the subclass of the weapon that deals the damage
 * \return < 0 when the dealt damage destroys the structure, > 0 when the structure survives
 */
int structureDamage(Structure* psStructure, unsigned damage, WEAPON_CLASS weaponClass, 
                    WEAPON_SUBCLASS weaponSubClass, unsigned impactTime, 
                    bool isDamagePerSecond, int minDamage)
{
	CHECK_STRUCTURE(psStructure);

	debug(LOG_ATTACK, "structure id %d, body %d, armour %d, damage: %d",
	      psStructure->getId(), psStructure->getHp(), objArmour(psStructure, weaponClass), damage);

	auto relativeDamage = objDamage(psStructure, damage,
                                       structureBody(psStructure), 
                                        weaponClass, weaponSubClass,
	                                      isDamagePerSecond, minDamage);

	// if the shell did sufficient damage to destroy the structure
	if (relativeDamage < 0) {
		debug(LOG_ATTACK, "Structure (id %d) DESTROYED", psStructure->getId());
		destroyStruct(psStructure, impactTime);
	}
	else {
		// survived
		CHECK_STRUCTURE(psStructure);
	}

	return relativeDamage;
}

int getStructureDamage(const Structure* psStructure)
{
	CHECK_STRUCTURE(psStructure);
	auto maxBody = structureBodyBuilt(psStructure);

	auto health = (int64_t)65536 * psStructure->getHp() / MAX(1, maxBody);
	CLIP(health, 0, 65536);

	return 65536 - health;
}

unsigned structureBuildPointsToCompletion(const Structure& structure)
{
	if (structureHasModules(&structure)) {
		auto moduleStat = getModuleStat(&structure);

		if (moduleStat != nullptr) {
			return moduleStat->build_point_cost;
		}
	}
	return structure.getStats()->build_point_cost;
}

//static bool structureHasModules(const STRUCTURE* psStruct)
//{
//	return psStruct->capacity != 0;
//}

// Power returned on demolish, which is half the power taken to build the structure and any modules
static int structureTotalReturn(const Structure* psStruct)
{
	auto power = psStruct->getStats()->power_cost;
	const auto moduleStats = getModuleStat(psStruct);

	if (nullptr != moduleStats) {
		power += psStruct->getCapacity() * moduleStats->power_cost;
	}
	return power / 2;
}

void structureDemolish(Structure* psStruct, Droid* psDroid, int buildPoints)
{
	structureBuild(psStruct, psDroid, -buildPoints);
}

void structureRepair(Structure* psStruct, Droid* psDroid, int buildRate)
{
	auto repairAmount = gameTimeAdjustedAverage(
          buildRate * structureBody(psStruct),
	            psStruct->getStats()->build_point_cost);
	/*	(droid construction power * current max hitpoints [incl. upgrades])
			/ construction power that was necessary to build structure in the first place

	=> to repair a building from 1HP to full health takes as much time as building it.
	=> if buildPoints = 1 and structureBody < buildPoints, repairAmount might get truncated to zero.
		This happens with expensive, but weak buildings like mortar pits. In this case, do nothing
		and notify the caller (read: droid) of your idleness by returning false.
	*/
	psStruct->setHp(clip<unsigned>(
          psStruct->getHp() + repairAmount,
          0, structureBody(psStruct)));
}

void Factory::refundBuildPower()
{
	if (psSubject) {
		if (buildPointsRemaining < (int)calcTemplateBuild(psSubject.get())) {
			// We started building, so give the power back that was used.
			addPower(getPlayer(), calcTemplatePower(psSubject.get()));
		}
	}
}

/* Set the type of droid for a factory to build */
bool Factory::structSetManufacture(DroidTemplate* psTempl, QUEUE_MODE mode)
{
	/* psTempl might be NULL if the build is being cancelled in the middle */
	ASSERT_OR_RETURN(false, !psTempl ||

                   (validTemplateForFactory(psTempl,
                                            this,
                                            true) &&

                       researchedTemplate(psTempl,
                                          getPlayer(),
                                   true, true)) ||

                    getPlayer() == scavengerPlayer() || !bMultiPlayer,
                    "Wrong template for player %d factory.",
                    getPlayer());

	if (mode == ModeQueue) {
		sendStructureInfo(this,
                      STRUCTUREINFO_MANUFACTURE, psTempl);
		setStatusPendingStart(*this, psTempl);
		return true; // Wait for our message before doing anything.
	}

	//assign it to the Factory
  refundBuildPower();
	psSubject = psTempl;

	//set up the start time and build time
	if (psTempl != nullptr) {
		//only use this for non selectedPlayer
		if (getPlayer() != selectedPlayer) {
			//set quantity to produce
			productionLoops = 1;
		}

		timeStarted = ACTION_START_TIME; //gameTime;
		timeStartHold = 0;

		buildPointsRemaining = calcTemplateBuild(psTempl);
		//check for zero build time - usually caused by 'silly' data! If so, set to 1 build point - ie very fast!
		buildPointsRemaining = std::max(buildPointsRemaining, 1);
	}
	return true;
}

/*****************************************************************************/
/*
* All this wall type code is horrible, but I really can't think of a better way to do it.
*        John.
*/

enum WallOrientation
{
	WallConnectNone = 0,
	WallConnectLeft = 1,
	WallConnectRight = 2,
	WallConnectUp = 4,
	WallConnectDown = 8,
};

// Orientations are:
//
//  0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
//                  |   |   |   |                   |   |   |   |
//  *  -*   *- -*-  *  -*   *- -*-  *  -*   *- -*-  *  -*   *- -*-
//                                  |   |   |   |   |   |   |   |

// IMDs are:
//
//  0   1   2   3
//      |   |   |
// -*- -*- -*- -*
//      |

// Orientations are:                   IMDs are:
// 0 1 2 3 4 5 6 7 8 9 A B C D E F     0 1 2 3
//   ╴ ╶ ─ ╵ ┘ └ ┴ ╷ ┐ ┌ ┬ │ ┤ ├ ┼     ─ ┼ ┴ ┘

static uint16_t wallDir(WallOrientation orient)
{
	const uint16_t d0 = DEG(0), d1 = DEG(90), d2 = DEG(180), d3 = DEG(270); // d1 = rotate ccw, d3 = rotate cw
	//                    0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
	uint16_t dirs[16] = {d0, d0, d2, d0, d3, d0, d3, d0, d1, d1, d2, d2, d3, d1, d3, d0};
	return dirs[orient];
}

static uint16_t wallType(WallOrientation orient)
{
	//               0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
	int types[16] = {0, 0, 0, 0, 0, 3, 3, 2, 0, 3, 3, 2, 0, 2, 2, 1};
	return types[orient];
}

// look at where other walls are to decide what type of wall to build
static WallOrientation structWallScan(bool aWallPresent[5][5], int x, int y)
{
	WallOrientation left = aWallPresent[x - 1][y] ? WallConnectLeft : WallConnectNone;
	WallOrientation right = aWallPresent[x + 1][y] ? WallConnectRight : WallConnectNone;
	WallOrientation up = aWallPresent[x][y - 1] ? WallConnectUp : WallConnectNone;
	WallOrientation down = aWallPresent[x][y + 1] ? WallConnectDown : WallConnectNone;
	return WallOrientation(left | right | up | down);
}

static bool isWallCombiningStructureType(StructureStats const* pStructureType)
{
  using enum STRUCTURE_TYPE;
	STRUCTURE_TYPE type = pStructureType->type;
	return type == WALL ||
		     type == GATE ||
		     type == WALL_CORNER ||
		     pStructureType->combines_with_wall; // hardpoints and fortresses by default
}



bool isBuildableOnWalls(STRUCTURE_TYPE type)
{
	return type == STRUCTURE_TYPE::DEFENSE || 
         type == STRUCTURE_TYPE::GATE;
}

static void structFindWalls(unsigned player, Vector2i map, bool aWallPresent[5][5], Structure* apsStructs[5][5])
{
	for (int y = -2; y <= 2; ++y)
		for (int x = -2; x <= 2; ++x)
		{
		  auto psStruct = dynamic_cast<Structure*>(mapTile(map.x + x, map.y + y)->psObject);
			if (psStruct != nullptr && 
          isWallCombiningStructureType(psStruct->getStats()) &&
          player < MAX_PLAYERS &&
				  aiCheckAlliances(player, psStruct->getPlayer())) {
				  aWallPresent[x + 2][y + 2] = true;
          
				apsStructs[x + 2][y + 2] = psStruct;
			}
		}
	// add in the wall about to be built
	aWallPresent[2][2] = true;
}

static void structFindWallBlueprints(Vector2i map, bool aWallPresent[5][5])
{
	for (int y = -2; y <= 2; ++y)
		for (int x = -2; x <= 2; ++x)
		{
			StructureStats const* stats = getTileBlueprintStats(map.x + x, map.y + y);
			if (stats != nullptr && isWallCombiningStructureType(stats))
			{
				aWallPresent[x + 2][y + 2] = true;
			}
		}
}

static bool wallBlockingTerrainJoin(Vector2i map)
{
	auto psTile = mapTile(map);
	return terrainType(psTile) == TER_WATER ||
         terrainType(psTile) == TER_CLIFFFACE || 
         psTile->psObject != nullptr;
}

static WallOrientation structWallScanTerrain(bool aWallPresent[5][5], Vector2i map)
{
	auto orientation = structWallScan(aWallPresent, 2, 2);

	if (orientation == WallConnectNone) {
		// If neutral, try choosing horizontal or vertical based on terrain, but don't change to corner type.
		aWallPresent[2][1] = wallBlockingTerrainJoin(map + Vector2i(0, -1));
		aWallPresent[2][3] = wallBlockingTerrainJoin(map + Vector2i(0, 1));
		aWallPresent[1][2] = wallBlockingTerrainJoin(map + Vector2i(-1, 0));
		aWallPresent[3][2] = wallBlockingTerrainJoin(map + Vector2i(1, 0));
		orientation = structWallScan(aWallPresent, 2, 2);
		if ((orientation & (WallConnectLeft | WallConnectRight)) != 0 && 
        (orientation & (WallConnectUp |
			   WallConnectDown)) != 0) {
      
			orientation = WallConnectNone;
		}
	}
	return orientation;
}

static WallOrientation structChooseWallTypeBlueprint(Vector2i map)
{
	bool aWallPresent[5][5];
	Structure* apsStructs[5][5];

	// scan around the location looking for walls
	memset(aWallPresent, 0, sizeof(aWallPresent));
	structFindWalls(selectedPlayer, map, aWallPresent, apsStructs);
	structFindWallBlueprints(map, aWallPresent);

	// finally return the type for this wall
	return structWallScanTerrain(aWallPresent, map);
}

// Choose a type of wall for a location - and update any neighbouring walls
static WallOrientation structChooseWallType(unsigned player, Vector2i map)
{
	bool aWallPresent[5][5];
	Structure* psStruct;
	Structure* apsStructs[5][5];

	// scan around the location looking for walls
	memset(aWallPresent, 0, sizeof(aWallPresent));
	structFindWalls(player, map, aWallPresent, apsStructs);

	// now make sure that all the walls around this one are OK
	for (int x = 1; x <= 3; ++x)
	{
		for (int y = 1; y <= 3; ++y)
		{
			// do not look at walls diagonally from this wall
			if (((x == 2 && y != 2) ||
					(x != 2 && y == 2)) &&
				aWallPresent[x][y])
			{
				// figure out what type the wall currently is
				psStruct = apsStructs[x][y];
				if (psStruct->getStats()->type != STRUCTURE_TYPE::WALL &&
            psStruct->getStats()->type != STRUCTURE_TYPE::GATE) {
					// do not need to adjust anything apart from walls
					continue;
				}

				// see what type the wall should be
				auto scanType = structWallScan(aWallPresent, x, y);

				// Got to change the wall
				if (scanType != WallConnectNone) {
					psStruct->pFunctionality->wall.type = wallType(scanType);
					psStruct->rotation.direction = wallDir(scanType);
          
					psStruct->display->imd_shape = psStruct->getStats()->IMDs[std::min<unsigned>(
						psStruct->pFunctionality->wall.type, psStruct->getStats()->IMDs.size() - 1)];
				}
			}
		}
	}
	// finally return the type for this wall
	return structWallScanTerrain(aWallPresent, map);
}

/**
 * For now all this does is work out what height the terrain needs to be set to
 * An actual foundation structure may end up being placed down
 * The x and y passed in are the CENTRE of the structure
 */
int foundationHeight(const Structure *psStruct)
{
	auto b = getStructureBounds(psStruct);

	// check the terrain is the correct type return -1 if not
	// may also have to check that overlapping terrain can be set to the average height
	// e.g., water - don't want it to 'flow' into the structure if this effect is coded!

	//initialise the starting values so they get set in loop
	auto foundationMin = INT32_MAX;
	auto foundationMax = INT32_MIN;

	for (auto breadth = 0; breadth <= b.size.y; breadth++)
	{
		for (auto width = 0; width <= b.size.x; width++)
		{
			auto height = map_TileHeight(b.map.x + width, b.map.y + breadth);
			foundationMin = std::min(foundationMin, height);
			foundationMax = std::max(foundationMax, height);
		}
	}
	// return the average of max/min height
	return (foundationMin + foundationMax) / 2;
}


void buildFlatten(Structure *pStructure, int h)
{
	auto b = getStructureBounds(pStructure);

	for (auto breadth = 0; breadth <= b.size.y; ++breadth)
	{
		for (auto width = 0; width <= b.size.x; ++width)
		{
			setTileHeight(b.map.x + width, b.map.y + breadth, h);
			// we need to raise features on raised tiles to the new height
			if (TileHasFeature(mapTile(
              b.map.x + width, b.map.y + breadth))) {
				getTileFeature(b.map.x + width, b.map.y + breadth)->position.z = h;
			}
		}
	}
}

bool isPulledToTerrain(const Structure *psBuilding)
{
	auto type = psBuilding->getStats()->type;

  using enum STRUCTURE_TYPE;
	return type == DEFENSE || type == GATE ||
         type == WALL || type == WALL_CORNER ||
         type == REARM_PAD;
}

void alignStructure(Structure* psBuilding)
{
	// DEFENSIVE structures are pulled to the terrain
	if (!isPulledToTerrain(psBuilding)) {
		auto mapH = foundationHeight(psBuilding);

		buildFlatten(psBuilding, mapH);
		psBuilding->position.z = mapH;
		psBuilding->setFoundationDepth(psBuilding->getPosition().z);

		// align surrounding structures.
		auto b = getStructureBounds(psBuilding);
		syncDebug("Flattened (%d+%d, %d+%d) to %d for %d(p%d)",
              b.map.x, b.size.x, b.map.y, b.size.y, mapH,
              psBuilding->getId(), psBuilding->getPlayer());

		for (int breadth = -1; breadth <= b.size.y; ++breadth)
		{
			for (int width = -1; width <= b.size.x; ++width)
			{
				auto neighbourStructure = dynamic_cast<Structure*>(
                mapTile(b.map.x + width, b.map.y + breadth)->psObject);
				if (neighbourStructure != nullptr &&
            isPulledToTerrain(neighbourStructure)) {
          // recursive call, but will go to the else case, so will not re-recurse.
					alignStructure(neighbourStructure);
				}
			}
		}
	}
	else {
		// sample points around the structure to find a good
    // depth for the foundation
		auto& s = psBuilding->getDisplayData().imd_shape;

		psBuilding->position.z = TILE_MIN_HEIGHT;
		psBuilding->setFoundationDepth(TILE_MAX_HEIGHT);

		auto dir = iSinCosR(
            psBuilding->getRotation().direction, 1);

		// rotate s->max.{x, z} and s->min.{x, z} by angle rot.direction.
		Vector2i p1 {s->max.x * dir.y - s->max.z * dir.x,
                 s->max.x * dir.x + s->max.z * dir.y};
		Vector2i p2 {s->min.x * dir.y - s->min.z * dir.x,
                 s->min.x * dir.x + s->min.z * dir.y};

		auto h1 = map_Height(psBuilding->getPosition().x + p1.x,
                         psBuilding->getPosition().y + p2.y);
		auto h2 = map_Height(psBuilding->getPosition().x + p1.x,
                         psBuilding->getPosition().y + p1.y);
		auto h3 = map_Height(psBuilding->getPosition().x + p2.x,
                         psBuilding->getPosition().y + p1.y);
		auto h4 = map_Height(psBuilding->getPosition().x + p2.x,
                         psBuilding->getPosition().y + p2.y);

		auto minH = std::min({h1, h2, h3, h4});
		auto maxH = std::max({h1, h2, h3, h4});
		psBuilding->position.z = std::max(psBuilding->getPosition().z, maxH);
		psBuilding->setFoundationDepth(std::min<int>(psBuilding->getFoundationDepth(), minH));
    // s->max is based on floats! If this causes desynchs, need to fix!
		syncDebug("minH=%d,maxH=%d,pointHeight=%d", minH, maxH, psBuilding->getPosition().z);
	}
}

/*Builds an instance of a Structure - the x/y passed in are in world coords. */
Structure* buildStructure(StructureStats* pStructureType, unsigned x, unsigned y, unsigned player, bool FromSave)
{
	return buildStructureDir(pStructureType, x, y, 0, player, FromSave);
}


static Vector2i defaultAssemblyPointPos(Structure* psBuilding)
{
	const auto size = psBuilding->getSize() + Vector2i(1, 1);
	// Adding Vector2i(1, 1) to select the middle of the tile outside the building instead of the corner.
	const auto pos = psBuilding->getPosition();
	switch (snapDirection(psBuilding->getRotation().direction)) {
	  case 0x0000: return pos + TILE_UNITS / 2 * Vector2i(size.x, size.y);
	  case 0x4000: return pos + TILE_UNITS / 2 * Vector2i(size.x, -size.y);
	  case 0x8000: return pos + TILE_UNITS / 2 * Vector2i(-size.x, -size.y);
	  case 0xC000: return pos + TILE_UNITS / 2 * Vector2i(-size.x, size.y);
	}
	return {}; // Unreachable.
}

static bool setFunctionality(Structure* psBuilding, STRUCTURE_TYPE functionType)
{
	ASSERT_OR_RETURN(false, psBuilding != nullptr, "Invalid pointer");
	CHECK_STRUCTURE(psBuilding);

  using enum STRUCTURE_TYPE;
	switch (functionType) {
  	case FACTORY:
  	case CYBORG_FACTORY:
  	case VTOL_FACTORY:
  	case RESEARCH:
  	case POWER_GEN:
  	case RESOURCE_EXTRACTOR:
  	case REPAIR_FACILITY:
  	case REARM_PAD:
  	case WALL:
  	case GATE:
  		// Allocate space for the buildings functionality
  		psBuilding->pFunctionality = (FUNCTIONALITY*)calloc(1, sizeof(*psBuilding->pFunctionality));
  		ASSERT_OR_RETURN(false, psBuilding != nullptr, "Out of memory");
  		break;
  	default:
  		psBuilding->pFunctionality = nullptr;
  		break;
	}

	switch (functionType) {
	case FACTORY:
	case CYBORG_FACTORY:
	case VTOL_FACTORY:
		{
			auto psFactory = &psBuilding->pFunctionality->factory;

			psFactory->psSubject = nullptr;

			// Default the secondary order - AB 22/04/99
			psFactory->secondaryOrder = DSS_ARANGE_LONG | DSS_REPLEV_NEVER | DSS_ALEV_ALWAYS | DSS_HALT_GUARD;

			// Create the assembly point for the factory
			if (!createFlagPosition(&psFactory->psAssemblyPoint, psBuilding->getPlayer())) {
				return false;
			}

			// Set the assembly point
			Vector2i pos = defaultAssemblyPointPos(psBuilding);
			setAssemblyPoint(psFactory->psAssemblyPoint,
                       pos.x, pos.y, psBuilding->getPlayer(), true);

			// Add the flag to the list
			addFlagPosition(psFactory->psAssemblyPoint);
			switch (functionType) {
			case FACTORY:
				setFlagPositionInc(psBuilding->pFunctionality, psBuilding->getPlayer(), FACTORY_FLAG);
				break;
			case CYBORG_FACTORY:
				setFlagPositionInc(psBuilding->pFunctionality, psBuilding->getPlayer(), CYBORG_FLAG);
				break;
			case VTOL_FACTORY:
				setFlagPositionInc(psBuilding->pFunctionality, psBuilding->getPlayer(), VTOL_FLAG);
				break;
			default:
				ASSERT_OR_RETURN(false, false, "Invalid factory type");
			}
			break;
		}
	case POWER_GEN:
	case HQ:
	case REARM_PAD:
		{
			break;
		}
	case RESOURCE_EXTRACTOR:
		{
			auto psResExtracter = &psBuilding->pFunctionality->resourceExtractor;

			// Make the structure inactive
			psResExtracter->power_generator = nullptr;
			break;
		}
	case REPAIR_FACILITY:
		{
			RepairFacility* psRepairFac = &psBuilding->pFunctionality->repairFacility;

			psRepairFac->psObj = nullptr;
			psRepairFac->droidQueue = 0;
			psRepairFac->psGroup = grpCreate();

			// Add NULL droid to the group
			psRepairFac->psGroup->add(nullptr);

			// Create an assembly point for repaired droids
			if (!createFlagPosition(psRepairFac->psDeliveryPoint.get(), psBuilding->getPlayer())) {
				return false;
			}

			// Set the assembly point
			Vector2i pos = defaultAssemblyPointPos(psBuilding);
			setAssemblyPoint(psRepairFac->psDeliveryPoint.get(), pos.x, pos.y, psBuilding->getPlayer(), true);

			// Add the flag (triangular marker on the ground) at the delivery point
			::addFlagPosition(psRepairFac->psDeliveryPoint.get());
			setFlagPositionInc(psBuilding->pFunctionality, psBuilding->getPlayer(), REPAIR_FLAG);
			break;
		}
	// Structure types without a FUNCTIONALITY
	default:
		break;
	}
	return true;
}


// remove all factories from a command droid
void clearCommandDroidFactory(Droid* psDroid)
{
	ASSERT_OR_RETURN(, selectedPlayer < MAX_PLAYERS, "invalid selectedPlayer: %" PRIu32 "", selectedPlayer);

	for (auto& psCurr : apsStructLists[selectedPlayer])
	{
		if ((psCurr->getStats()->type == STRUCTURE_TYPE::FACTORY) ||
			(psCurr->getStats()->type == STRUCTURE_TYPE::CYBORG_FACTORY) ||
			(psCurr->getStats()->type == STRUCTURE_TYPE::VTOL_FACTORY)) {
      
			if (psCurr->pFunctionality->factory.psCommander == psDroid) {
				assignFactoryCommandDroid(psCurr, nullptr);
			}
		}
	}
  
	for (auto& psCurr : mission.apsStructLists[selectedPlayer])
	{
		if ((psCurr->getStats()->type == STRUCTURE_TYPE::FACTORY) ||
			(psCurr->getStats()->type == STRUCTURE_TYPE::CYBORG_FACTORY) ||
			(psCurr->getStats()->type == STRUCTURE_TYPE::VTOL_FACTORY)) {
      
			if (psCurr->pFunctionality->factory.psCommander == psDroid) {
				assignFactoryCommandDroid(psCurr, nullptr);
			}
		}
	}
}

/* Check that a tile is vacant for a droid to be placed */
static bool structClearTile(uint16_t x, uint16_t y)
{
	/* Check for a structure */
	if (fpathBlockingTile(x, y, PROPULSION_TYPE::WHEELED)) {
		debug(LOG_NEVER, "failed - blocked");
		return false;
	}

	/* Check for a droid */
	for (auto player = 0; player < MAX_PLAYERS; player++)
	{
		for (auto& psCurr : apsDroidLists[player])
		{
			if (map_coord(psCurr.getPosition().x) == x
				&& map_coord(psCurr.getPosition().y) == y) {
				debug(LOG_NEVER, "failed - not vacant");
				return false;
			}
		}
	}
	debug(LOG_NEVER, "succeeded");
	return true;
}

/* An auxiliary function for std::stable_sort in placeDroid */
static bool comparePlacementPoints(Vector2i a, Vector2i b)
{
	return abs(a.x) + abs(a.y) < abs(b.x) + abs(b.y);
}

/* Find a location near to a structure to start the droid of */
bool placeDroid(Structure* psStructure, unsigned* droidX, unsigned* droidY)
{
	CHECK_STRUCTURE(psStructure);

	// Find the four corners of the square
	auto bounds = getStructureBounds(psStructure);
	auto xmin = std::max(bounds.map.x - 1, 0);
	auto xmax = std::min(bounds.map.x + bounds.size.x, mapWidth);
	auto ymin = std::max(bounds.map.y - 1, 0);
	auto ymax = std::min(bounds.map.y + bounds.size.y, mapHeight);

	// Round direction to nearest 90°.
	auto direction = snapDirection(psStructure->getRotation().direction);

	/* We sort all adjacent tiles by their Manhattan distance to the
	target droid exit tile, misplaced by (1/3, 1/4) tiles.
	Since only whole coordinates are sorted, this makes sure sorting
	is deterministic. Target coordinates, multiplied by 12 to eliminate
	floats, are stored in (sx, sy). */
	int sx, sy;

	if (direction == 0x0) {
		sx = 12 * (xmin + 1) + 4;
		sy = 12 * ymax + 3;
	}
	else if (direction == 0x4000) {
		sx = 12 * xmax + 3;
		sy = 12 * (ymax - 1) - 4;
	}
	else if (direction == 0x8000) {
		sx = 12 * (xmax - 1) - 4;
		sy = 12 * ymin - 3;
	}
	else {
		sx = 12 * xmin - 3;
		sy = 12 * (ymin + 1) + 4;
	}

	std::vector<Vector2i> tiles;
	for (int y = ymin; y <= ymax; ++y)
	{
		for (int x = xmin; x <= xmax; ++x)
		{
			if (structClearTile(x, y)) {
				tiles.emplace_back(12 * x - sx, 12 * y - sy);
			}
		}
	}

	if (tiles.empty()) {
		return false;
	}

	std::sort(tiles.begin(), tiles.end(), comparePlacementPoints);

	/* Store best tile coordinates in (sx, sy),
	which are also map coordinates of its north-west corner.
	Store world coordinates of this tile's center in (wx, wy) */
	sx = (tiles[0].x + sx) / 12;
	sy = (tiles[0].y + sy) / 12;
	auto wx = world_coord(sx) + TILE_UNITS / 2;
	auto wy = world_coord(sy) + TILE_UNITS / 2;

	/* Finally, find world coordinates of the structure point closest to (mx, my).
	For simplicity, round to grid vertices. */
	if (2 * sx <= xmin + xmax) {
		wx += TILE_UNITS / 2 - 1;
	}
	if (2 * sx >= xmin + xmax) {
		wx -= TILE_UNITS / 2 - 1;
	}
	if (2 * sy <= ymin + ymax) {
		wy += TILE_UNITS / 2 - 1;
	}
	if (2 * sy >= ymin + ymax) {
		wy -= TILE_UNITS / 2 - 1;
	}

	*droidX = wx;
	*droidY = wy;
	return true;
}

//Set the factory secondary orders to a droid
void setFactorySecondaryState(Droid* psDroid, Structure* psStructure)
{
	CHECK_STRUCTURE(psStructure);
	ASSERT_OR_RETURN(, StructIsFactory(psStructure), "structure not a factory");

	if (myResponsibility(psStructure->getPlayer())) {
		auto newState = psStructure->pFunctionality->factory.secondaryOrder;
		auto diff = newState ^ psDroid->getSecondaryOrder();
		if ((diff & DSS_ARANGE_MASK) != 0) {
			secondarySetState(psDroid, SECONDARY_ORDER::ATTACK_RANGE,
                        (SECONDARY_STATE)(newState & DSS_ARANGE_MASK));
		}
		if ((diff & DSS_REPLEV_MASK) != 0) {
			secondarySetState(psDroid, SECONDARY_ORDER::REPAIR_LEVEL,
                        (SECONDARY_STATE)(newState & DSS_REPLEV_MASK));
		}
		if ((diff & DSS_ALEV_MASK) != 0) {
			secondarySetState(psDroid, SECONDARY_ORDER::ATTACK_LEVEL,
                        (SECONDARY_STATE)(newState & DSS_ALEV_MASK));
		}
		if ((diff & DSS_CIRCLE_MASK) != 0) {
			secondarySetState(psDroid, SECONDARY_ORDER::CIRCLE,
                        (SECONDARY_STATE)(newState & DSS_CIRCLE_MASK));
		}
		if ((diff & DSS_HALT_MASK) != 0) {
			secondarySetState(psDroid, SECONDARY_ORDER::HALT_TYPE,
                        (SECONDARY_STATE)(newState & DSS_HALT_MASK));
		}
	}
}

/**
 * Place a newly manufactured droid next to a factory and then send if off
 * to the assembly point, returns true if droid was placed successfully
 */
bool Factory::structPlaceDroid(DroidTemplate* psTempl, Droid** ppsDroid)
{
	unsigned x, y;
	bool placed; //bTemp = false;
	Droid* psNewDroid;
	Factory* psFact;
	FlagPosition* psFlag;
	Vector3i iVecEffect;
	uint8_t factoryType;
	bool assignCommander;

	CHECK_STRUCTURE(this);

	placed = placeDroid(this, &x, &y);

	if (placed) {
		InitialOrders initialOrders = {
			secondaryOrder,
			psAssemblyPoint->coords.x,
			psAssemblyPoint->coords.y, getId()
		};
		//create a droid near to the structure
		syncDebug("Placing new droid at (%d,%d)", x, y);
		turnOffMultiMsg(true);
		psNewDroid = buildDroid(psTempl, x, y, getPlayer(),
                            false, &initialOrders, 
                            getRotation()).get();
		turnOffMultiMsg(false);
		if (!psNewDroid) {
			*ppsDroid = nullptr;
			return false;
		}

		setFactorySecondaryState(psNewDroid, this);
		const auto mapCoord = map_coord({x, y});
		const auto psTile = mapTile(mapCoord);
    
		if (tileIsClearlyVisible(psTile)) // display only - does not affect game state
		{
			/* add smoke effect to cover the droid's emergence from the factory */
			iVecEffect.x = psNewDroid->getPosition().x;
			iVecEffect.y = map_Height(psNewDroid->getPosition().x, psNewDroid->getPosition().y) + DROID_CONSTRUCTION_SMOKE_HEIGHT;
			iVecEffect.z = psNewDroid->getPosition().y;
			addEffect(&iVecEffect, EFFECT_GROUP::CONSTRUCTION, 
                EFFECT_TYPE::CONSTRUCTION_TYPE_DRIFTING, 
                false, nullptr, 0,
			          gameTime - deltaGameTime + 1);
			iVecEffect.x = psNewDroid->getPosition().x - DROID_CONSTRUCTION_SMOKE_OFFSET;
			iVecEffect.z = psNewDroid->getPosition().y - DROID_CONSTRUCTION_SMOKE_OFFSET;
			addEffect(&iVecEffect, EFFECT_GROUP::CONSTRUCTION, 
                EFFECT_TYPE::CONSTRUCTION_TYPE_DRIFTING, 
                false, nullptr, 0,
			          gameTime - deltaGameTime + 1);
			iVecEffect.z = psNewDroid->getPosition().y + DROID_CONSTRUCTION_SMOKE_OFFSET;
			addEffect(&iVecEffect, EFFECT_GROUP::CONSTRUCTION, 
                EFFECT_TYPE::CONSTRUCTION_TYPE_DRIFTING, 
                false, nullptr, 0,
			          gameTime - deltaGameTime + 1);
			iVecEffect.x = psNewDroid->getPosition().x + DROID_CONSTRUCTION_SMOKE_OFFSET;
			addEffect(&iVecEffect, EFFECT_GROUP::CONSTRUCTION, 
                EFFECT_TYPE::CONSTRUCTION_TYPE_DRIFTING, 
                false, nullptr, 0,
			          gameTime - deltaGameTime + 1);
			iVecEffect.z = psNewDroid->getPosition().y - DROID_CONSTRUCTION_SMOKE_OFFSET;
			addEffect(&iVecEffect, EFFECT_GROUP::CONSTRUCTION, 
                EFFECT_TYPE::CONSTRUCTION_TYPE_DRIFTING, 
                false, nullptr, 0,
			          gameTime - deltaGameTime + 1);
		}
    
		// add the droid to the list
		addDroid(psNewDroid, apsDroidLists);
		*ppsDroid = psNewDroid;
		if (psNewDroid->getPlayer() == selectedPlayer) {
			audio_QueueTrack(ID_SOUND_DROID_COMPLETED);
			intRefreshScreen(); // update any interface implications.
		}

		// update the droid counts
		adjustDroidCount(psNewDroid, 1);

		// if we've built a command droid - make sure that it isn't assigned to another commander
		assignCommander = false;
		if ((psNewDroid->getType() == DROID_TYPE::COMMAND) &&
        (psCommander != nullptr)) {
			assignFactoryCommandDroid(nullptr);
			assignCommander = true;
		}

		if (psNewDroid->isVtol() && !isTransporter(*psNewDroid)) {
			moveToRearm(psNewDroid);
		}
		if (psFact->psCommander != nullptr && 
        myResponsibility(static_cast<int>(getPlayer()) )) {
			if (isTransporter(*psNewDroid)) {
				// transporters can't be assigned to commanders, due to abuse of psGroup.
        // try to land on the commander instead. hopefully the transport is
        // heavy enough to crush the commander
				::orderDroidLoc(psNewDroid, ORDER_TYPE::MOVE,
                      psFact->psCommander->getPosition().x,
                      psFact->psCommander->getPosition().y,
                      ModeQueue);
			}
			else if (isIdf(psNewDroid) ||
               psNewDroid->isVtol()) {
				::orderDroidObj(psNewDroid, ORDER_TYPE::FIRE_SUPPORT, psFact->psCommander, ModeQueue);
				//moveToRearm(psNewDroid);
			}
			else {
				::orderDroidObj(psNewDroid, ORDER_TYPE::COMMANDER_SUPPORT, psFact->psCommander, ModeQueue);
			}
		}
		else {
			//check flag against factory type
			factoryType = FACTORY_FLAG;
			if (getStats()->type == STRUCTURE_TYPE::CYBORG_FACTORY) {
				factoryType = CYBORG_FLAG;
			}
			else if (getStats()->type == STRUCTURE_TYPE::VTOL_FACTORY) {
				factoryType = VTOL_FLAG;
			}
			//find flag in question.
			for (psFlag = apsFlagPosLists[psFact->psAssemblyPoint->player];
			     psFlag
			     && !(psFlag->factoryInc == psFact->psAssemblyPoint->factoryInc // correct fact.
				     && psFlag->factoryType == factoryType); // correct type
			     psFlag = psFlag->psNext)
			{
			}
			ASSERT(psFlag, "No flag found for %s at (%d, %d)", 
             objInfo(this), 
             getPosition().x, getPosition().y);
      
			// if vtol droid - send it to RearmPad if one exists
			if (psFlag && psNewDroid->isVtol()) {
				Vector2i pos = psFlag->coords.xy();
				//find a suitable location near the delivery point
				actionVTOLLandingPos(psNewDroid, &pos);
				::orderDroidLoc(psNewDroid, ORDER_TYPE::MOVE, pos.x, pos.y, ModeQueue);
			}
			else if (psFlag) {
				::orderDroidLoc(psNewDroid, ORDER_TYPE::MOVE, psFlag->coords.x, psFlag->coords.y, ModeQueue);
			}
		}
		if (assignCommander) {
			assignFactoryCommandDroid(psNewDroid);
		}
		return true;
	}
	else {
		syncDebug("Droid placement failed");
		*ppsDroid = nullptr;
	}
	return false;
}

bool Factory::IsFactoryCommanderGroupFull()
{
	if (bMultiPlayer) {
		// TODO: Synchronise .psCommander. Have to return false here, to avoid desynch.
		return false;
	}

	unsigned int DroidsInGroup;

	// If we don't have a commander return false (group not full)
	if (psCommander == nullptr) {
		return false;
	}

	// allow any number of IDF droids
	if (templateIsIDF(psSubject.get()) ||
      asPropulsionStats[psSubject->asParts[COMPONENT_TYPE::PROPULSION]].
		propulsionType == PROPULSION_TYPE::LIFT) {
		return false;
	}

	// Get the number of droids in the commanders group
	DroidsInGroup = psCommander->getGroup().getNumMembers();

	// if the number in group is less than the maximum allowed then return false (group not full)
	if (DroidsInGroup < cmdDroidMaxGroup(psCommander)) {
		return false;
	}
	// the number in group has reached the maximum
	return true;
}

// Check if a player has a certain structure. Optionally, checks if there is
// at least one that is built.
bool structureExists(unsigned player, STRUCTURE_TYPE type, bool built, bool isMission)
{
	bool found = false;

	ASSERT_OR_RETURN(false, player >= 0, "invalid player: %d", player);
	if (player >= MAX_PLAYERS) {
		return false;
	}

	for (auto& psCurr : isMission ? mission.apsStructLists[player] : apsStructLists[player])
	{
		if (psCurr->getStats().type == type &&
        (!built || (built && psCurr->getState() == STRUCTURE_STATE::BUILT))) {
			found = true;
			break;
		}
	}
	return found;
}

// Disallow manufacture of units once these limits are reached,
// doesn't mean that these numbers can't be exceeded if units are
// put down in the editor or by the scripts.

void setMaxDroids(unsigned player, int value)
{
	ASSERT_OR_RETURN(, player < MAX_PLAYERS, "player = %" PRIu32 "", player);
	droidLimit[player] = value;
}

void setMaxCommanders(unsigned player, int value)
{
	ASSERT_OR_RETURN(, player < MAX_PLAYERS, "player = %" PRIu32 "", player);
	commanderLimit[player] = value;
}

void setMaxConstructors(unsigned player, int value)
{
	ASSERT_OR_RETURN(, player < MAX_PLAYERS, "player = %" PRIu32 "", player);
	constructorLimit[player] = value;
}

int getMaxDroids(unsigned player)
{
	ASSERT_OR_RETURN(0, player < MAX_PLAYERS, "player = %" PRIu32 "", player);
	return droidLimit[player];
}

int getMaxCommanders(unsigned player)
{
	ASSERT_OR_RETURN(0, player < MAX_PLAYERS, "player = %" PRIu32 "", player);
	return commanderLimit[player];
}

int getMaxConstructors(unsigned player)
{
	ASSERT_OR_RETURN(0, player < MAX_PLAYERS, "player = %" PRIu32 "", player);
	return constructorLimit[player];
}

bool IsPlayerDroidLimitReached(unsigned player)
{
	auto numDroids = getNumDroids(player) + 
          getNumMissionDroids(player) + getNumTransporterDroids(player);
  
	return numDroids >= getMaxDroids(player);
}

// Check for max number of units reached and halt production.
bool Factory::checkHaltOnMaxUnitsReached(bool isMission)
{
	CHECK_STRUCTURE(this);

	char limitMsg[300];
	bool isLimit = false;
	auto player = getPlayer();

	auto templ = psSubject;

	// if the players that owns the factory has reached his (or hers) droid limit
	// then put production on hold & return - we need a message to be displayed here !!!!!!!
	if (IsPlayerDroidLimitReached(static_cast<int>(player))) {
		isLimit = true;
		sstrcpy(limitMsg, _("Can't build any more units, Unit Limit Reached — Production Halted"));
	}
	else
		switch (droidTemplateType(templ.get())) {
      case DROID_TYPE::COMMAND:
			if (!structureExists(static_cast<int>(player), 
                           STRUCTURE_TYPE::COMMAND_CONTROL, 
                           true, isMission)) {
				isLimit = true;
				ssprintf(limitMsg, _("Can't build \"%s\" without a Command Relay Center — Production Halted"),
				         templ->name.toUtf8().c_str());
			}
			else if (getNumCommandDroids(player) >= getMaxCommanders(player)) {
				isLimit = true;
				ssprintf(limitMsg, _("Can't build \"%s\", Commander Limit Reached — Production Halted"),
				         templ->name.toUtf8().c_str());
			}
			break;
    case DROID_TYPE::CONSTRUCT:
    case DROID_TYPE::CYBORG_CONSTRUCT:
			if (getNumConstructorDroids(player) >= getMaxConstructors(player)) {
				isLimit = true;
				ssprintf(limitMsg,
				         _("Can't build any more \"%s\","
                   " Construction Unit Limit Reached — Production Halted"),
				         templ->name.toUtf8().c_str());
			}
			break;
		default:
			break;
		}

	if (isLimit && player == selectedPlayer && 
      (lastMaxUnitMessage == 0 ||
       lastMaxUnitMessage + MAX_UNIT_MESSAGE_PAUSE <= gameTime)) {
		addConsoleMessage(limitMsg, CONSOLE_TEXT_JUSTIFICATION::DEFAULT, SYSTEM_MESSAGE);
		lastMaxUnitMessage = gameTime;
	}
	return isLimit;
}

/** Decides whether a structure should emit smoke when it's damaged */
static bool canSmoke(const Structure* psStruct)
{
  using enum STRUCTURE_TYPE;
	if (psStruct->getStats()->type == WALL ||
      psStruct->getStats()->type == WALL_CORNER ||
      psStruct->getState() == STRUCTURE_STATE::BEING_BUILT ||
      psStruct->getStats()->type == GATE) {
		return false;
	}
	else {
		return true;
	}
}

static float CalcStructureSmokeInterval(float damage)
{
	return static_cast<float>((((1. - damage) + 0.1) * 10) * STRUCTURE_DAMAGE_SCALING);
}

const ResearchItem* ResearchFacility::getSubject() const
{
  return psSubject.get();
}

const DroidTemplate* Factory::getSubject() const
{
  return psSubject.get();
}

void _syncDebugStructure(const char* function, Structure const* psStruct, char ch)
{
	auto ref = 0;
	auto refChr = ' ';

  using enum STRUCTURE_TYPE;
	// Print what the structure is producing, too.
	switch (psStruct->getStats()->type) {
	case RESEARCH: 
    {
      auto psRes = dynamic_cast<const ResearchFacility*>(psStruct);
      if (psRes->getSubject() != nullptr) {
        ref = psRes->getSubject()->ref;
        refChr = 'r';
      }
      break;
    }
	  case FACTORY:
	  case CYBORG_FACTORY:
	  case VTOL_FACTORY:
    {
      auto psFact = dynamic_cast<const Factory*>(psStruct);
      if (psFact->getSubject() != nullptr) {
        ref = psFact->getSubject()->id;
        refChr = 'p';
      }
      break;
    }
	  default:
	  	break;
	}

	int list[] =
	{
		ch,
		(int)psStruct->getId(),
    (int)psStruct->getPlayer(),
		psStruct->getPosition().x,
    psStruct->getPosition().y,
    psStruct->getPosition().z,
		(int)psStruct->getState(),
		(int)psStruct->getStats()->type, refChr, ref,
		(int)psStruct->currentBuildPoints,
		(int)psStruct->getHp(),
	};
	_syncDebugIntList(function, "%c structure%d = p%d;pos(%d,%d,%d),status%d,type%d,%c%.0d,bld%d,body%d", list,
	                  ARRAY_SIZE(list));
}


int gateCurrentOpenHeight(const Structure* psStructure, unsigned time, int minimumStub)
{
	const auto psStructureStats = psStructure->getStats();
	if (psStructureStats->type == STRUCTURE_TYPE::GATE) {
		auto height = psStructure->getDisplayData().imd_shape->max.y;
		int openHeight;

    using enum STRUCTURE_ANIMATION_STATE;
		switch (psStructure->animationState) {
		  case OPEN:
		  	openHeight = height;
		  	break;
		  case OPENING:
		  	openHeight = (height * std::max<int>(time + GAME_TICKS_PER_UPDATE - psStructure->lastStateTime, 0)) / SAS_OPEN_SPEED;
		  	break;
		  case CLOSING:
		  	openHeight = height - (height * std::max<int>(time - psStructure->lastStateTime, 0)) / SAS_OPEN_SPEED;
		  	break;
		  default:
		  	return 0;
		  }
		return std::max(std::min(openHeight, height - minimumStub), 0);
	}
	return 0;
}

/*
fills the list with Structure that can be built. There is a limit on how many can
be built at any one time. Pass back the number available.
There is now a limit of how many of each type of structure are allowed per mission
*/
std::vector<StructureStats*> fillStructureList(unsigned _selectedPlayer, unsigned limit, bool showFavorites)
{
  using enum STRUCTURE_TYPE;
	std::vector<StructureStats*> structureList;
	unsigned inc;
	Structure* psCurr;
	StructureStats* psBuilding;

	ASSERT_OR_RETURN(structureList, _selectedPlayer < MAX_PLAYERS, "_selectedPlayer = %" PRIu32 "", _selectedPlayer);

	// counters for current nb of buildings, max buildings, current nb modules
	int8_t researchLabCurrMax[] = {0, 0};
	int8_t factoriesCurrMax[] = {0, 0};
	int8_t vtolFactoriesCurrMax[] = {0, 0};
	int8_t powerGenCurrMax[] = {0, 0};
	int8_t factoryModules = 0;
	int8_t powerGenModules = 0;
	int8_t researchModules = 0;

	//if currently on a mission can't build factory/research/power/derricks
	if (!missionIsOffworld()) {
		for (psCurr = apsStructLists[_selectedPlayer]; psCurr != nullptr; psCurr = psCurr->psNext)
		{
			if (psCurr->getStats()->type == RESEARCH &&
          psCurr->getState() == STRUCTURE_STATE::BUILT) {
				researchModules += psCurr->getCapacity();
			}
			else if (psCurr->getStats()->type == FACTORY &&
               psCurr->getState() == STRUCTURE_STATE::BUILT) {
				factoryModules += psCurr->getCapacity();
			}
			else if (psCurr->getStats()->type == POWER_GEN &&
               psCurr->getState() == STRUCTURE_STATE::BUILT) {
				powerGenModules += psCurr->getCapacity();
			}
			else if (psCurr->getStats()->type == VTOL_FACTORY &&
               psCurr->getState() == STRUCTURE_STATE::BUILT) {
				// same as REF_FACTORY
				factoryModules += psCurr->getCapacity();
			}
		}
	}

	// find maximum allowed limits (current built numbers already available, just grab them)
	for (inc = 0; inc < numStructureStats; inc++)
	{
		if (apStructTypeLists[_selectedPlayer][inc] == AVAILABLE ||
        (includeRedundantDesigns &&
         apStructTypeLists[_selectedPlayer][inc] == REDUNDANT)) {
			int8_t* counter;

			if (asStructureStats[inc].type == RESEARCH) {
				counter = researchLabCurrMax;
			}
			else if (asStructureStats[inc].type == FACTORY) {
				counter = factoriesCurrMax;
			}
			else if (asStructureStats[inc].type == VTOL_FACTORY) {
				counter = vtolFactoriesCurrMax;
			}
			else if (asStructureStats[inc].type == POWER_GEN) {
				counter = powerGenCurrMax;
			}
			else {
				continue;
			}
			counter[0] = asStructureStats[inc].curCount[_selectedPlayer];
			counter[1] = asStructureStats[inc].upgraded_stats[_selectedPlayer].limit;
		}
	}

	debug(LOG_NEVER, "structures: RL %i/%i (%i), F %i/%i (%i), VF %i/%i, PG %i/%i (%i)",
	      researchLabCurrMax[0], researchLabCurrMax[1], researchModules,
	      factoriesCurrMax[0], factoriesCurrMax[1], factoryModules,
	      vtolFactoriesCurrMax[0], vtolFactoriesCurrMax[1],
	      powerGenCurrMax[0], powerGenCurrMax[1], powerGenModules);

	// set the list of Structures to build
	for (inc = 0; inc < numStructureStats; inc++)
	{
		// if the structure is flagged as available, add it to the list
		if (apStructTypeLists[_selectedPlayer][inc] == AVAILABLE ||
        (includeRedundantDesigns &&
         apStructTypeLists[_selectedPlayer][inc] == REDUNDANT)) {
			// check not built the maximum allowed already
			if (asStructureStats[inc].curCount[_selectedPlayer] < asStructureStats[inc].upgraded_stats[_selectedPlayer].limit)
			{
				psBuilding = asStructureStats + inc;

				//don't want corner wall to appear in list
				if (psBuilding->type == WALL_CORNER) {
					continue;
				}

				// remove the demolish stat from the list for tutorial
				if (bInTutorial) {
					if (psBuilding->type == DEMOLISH) {
						continue;
					}
				}

				//can't build list when offworld
				if (missionIsOffworld()) {
					if (psBuilding->type == FACTORY ||
						  psBuilding->type == POWER_GEN ||
						  psBuilding->type == RESOURCE_EXTRACTOR ||
						  psBuilding->type == RESEARCH ||
					  	psBuilding->type == CYBORG_FACTORY ||
					  	psBuilding->type == VTOL_FACTORY) {
						continue;
					}
				}

				if (psBuilding->type == RESEARCH_MODULE) {
					//don't add to list if Research Facility not presently built 
					//or if all labs already have a module
					if (!researchLabCurrMax[0] || researchModules >= researchLabCurrMax[1]) {
						continue;
					}
				}
				else if (psBuilding->type == FACTORY_MODULE) {
					//don't add to list if Factory not presently built
					//or if all factories already have all possible modules
					if (!factoriesCurrMax[0] || (factoryModules >= (factoriesCurrMax[1] + vtolFactoriesCurrMax[1]) * 2)) {
						continue;
					}
				}
				else if (psBuilding->type == POWER_MODULE) {
					//don't add to list if Power Gen not presently built
					//or if all generators already have a module
					if (!powerGenCurrMax[0] || (powerGenModules >= powerGenCurrMax[1])) {
						continue;
					}
				}
				// show only favorites?
				if (showFavorites && !asStructureStats[inc].is_favourite) {
					continue;
				}

				debug(LOG_NEVER, "adding %s (%x)", getStatsName(psBuilding), apStructTypeLists[_selectedPlayer][inc]);
				structureList.push_back(psBuilding);
				if (structureList.size() == limit) {
					return structureList;
				}
			}
		}
	}
	return structureList;
}

enum STRUCTURE_PACKABILITY
{
	PACKABILITY_EMPTY = 0,
	PACKABILITY_DEFENSE = 1,
	PACKABILITY_NORMAL = 2,
	PACKABILITY_REPAIR = 3
};

static inline bool canPack(STRUCTURE_PACKABILITY a, STRUCTURE_PACKABILITY b)
{
	return (int)a + (int)b <= 3;
	// Defense can be put next to anything except repair facilities, normal base structures can't be put next to each other, and anything goes next to empty tiles.
}

static STRUCTURE_PACKABILITY baseStructureTypePackability(STRUCTURE_TYPE type)
{
  using enum STRUCTURE_TYPE;
	switch (type) {
	case DEFENSE:
	case WALL:
	case WALL_CORNER:
	case GATE:
	case REARM_PAD:
	case MISSILE_SILO:
		return PACKABILITY_DEFENSE;
  case REPAIR_FACILITY:
    return PACKABILITY_REPAIR;
	default:
		return PACKABILITY_NORMAL;
	}
}

static STRUCTURE_PACKABILITY baseObjectPackability(BaseObject * psObject)
{
	if (psObject == nullptr) {
		return PACKABILITY_EMPTY;
	}
	if (dynamic_cast<Structure*>(psObject)) {
    return baseStructureTypePackability(
            dynamic_cast<Structure*>(psObject)->getStats()->type);
  }
  else if (dynamic_cast<Feature*>(psObject)) {
    return (dynamic_cast<Feature*>(psObject)->getStats()->subType == FEATURE_TYPE::OIL_RESOURCE
           ? PACKABILITY_NORMAL
           : PACKABILITY_EMPTY);
  }
	return PACKABILITY_EMPTY;
}

bool isBlueprintTooClose(StructureStats const* stats1, Vector2i pos1, uint16_t dir1,
                         StructureStats const* stats2, Vector2i pos2, uint16_t dir2)
{
	if (stats1 == stats2 && pos1 == pos2 && dir1 == dir2) {
		return false; // Same blueprint, so ignore it.
	}

	bool packable = canPack(baseStructureTypePackability(stats1->type), baseStructureTypePackability(stats2->type));
	int minDist = packable ? 0 : 1;
	StructureBounds b1 = getStructureBounds(stats1, pos1, dir1);
	StructureBounds b2 = getStructureBounds(stats2, pos2, dir2);
	Vector2i delta12 = b2.map - (b1.map + b1.size);
	Vector2i delta21 = b1.map - (b2.map + b2.size);
	int dist = std::max(std::max(delta12.x, delta21.x), std::max(delta12.y, delta21.y));
	return dist < minDist;
}

bool validLocation(BaseStats* psStats, Vector2i pos, uint16_t direction, unsigned player, bool bCheckBuildQueue)
{
	ASSERT_OR_RETURN(false, player < MAX_PLAYERS, "player (%u) >= MAX_PLAYERS", player);
	auto b = getStructureBounds(psStats, pos, direction);

	//make sure we are not too near map edge and not going to go over it
	if (b.map.x < scrollMinX + TOO_NEAR_EDGE || b.map.x + b.size.x > scrollMaxX - TOO_NEAR_EDGE ||
		b.map.y < scrollMinY + TOO_NEAR_EDGE || b.map.y + b.size.y > scrollMaxY - TOO_NEAR_EDGE) {
		return false;
	}

	if (bCheckBuildQueue) {
		// cant place on top of a delivery point...
		for (FlagPosition const* psCurrFlag = apsFlagPosLists[selectedPlayer]; psCurrFlag; psCurrFlag = psCurrFlag->
		     psNext)
		{
			ASSERT_OR_RETURN(false, psCurrFlag->coords.x != ~0, "flag has invalid position");
			Vector2i flagTile = map_coord(psCurrFlag->coords.xy());
			if (flagTile.x >= b.map.x && flagTile.x < b.map.x + b.size.x && flagTile.y >= b.map.y && flagTile.y < b.map.
				y + b.size.y) {
				return false;
			}
		}
	}

	auto psBuilding = dynamic_cast<StructureStats*>(psStats);
	auto psTemplate = dynamic_cast<StructureStats*>(psStats);
	if (psBuilding != nullptr) {
		for (int j = 0; j < b.size.y; ++j)
			for (int i = 0; i < b.size.x; ++i)
			{
				// Don't allow building structures (allow delivery points, though) outside visible area in single-player with debug mode off. (Why..?)
				const DebugInputManager& dbgInputManager = gInputManager.debugManager();
				if (!bMultiPlayer && !dbgInputManager.debugMappingsAllowed() && !TEST_TILE_VISIBLE(
					player, mapTile(b.map.x + i, b.map.y + j))) {
					return false;
				}
			}

    using enum STRUCTURE_TYPE;
		switch (psBuilding->type) {
		case DEMOLISH:
			break;
		case NUM_DIFF_BUILDINGS:
		case BRIDGE:
			ASSERT(!"invalid structure type", "Bad structure type %u", psBuilding->type);
			break;
		case HQ:
		case FACTORY:
		case LAB:
		case RESEARCH:
		case POWER_GEN:
		case WALL:
		case WALL_CORNER:
		case GATE:
		case DEFENSE:
		case REPAIR_FACILITY:
		case COMMAND_CONTROL:
		case CYBORG_FACTORY:
		case VTOL_FACTORY:
		case GENERIC:
		case REARM_PAD:
		case MISSILE_SILO:
		case SAT_UPLINK:
		case LASSAT:
			{
				/*need to check each tile the structure will sit on is not water*/
				for (int j = 0; j < b.size.y; ++j)
					for (int i = 0; i < b.size.x; ++i)
					{
						Tile const* psTile = mapTile(b.map.x + i, b.map.y + j);
						if ((terrainType(psTile) == TER_WATER) ||
							(terrainType(psTile) == TER_CLIFFFACE)) {
							return false;
						}
					}
				//check not within landing zone
				for (int j = 0; j < b.size.y; ++j)
					for (int i = 0; i < b.size.x; ++i)
					{
						if (withinLandingZone(b.map.x + i, b.map.y + j)) {
							return false;
						}
					}

				//walls/defensive structures can be built along any ground
				if (!(psBuilding->type == REPAIR_FACILITY ||
					psBuilding->type == DEFENSE ||
					psBuilding->type == GATE ||
					psBuilding->type == WALL)) {
					/*cannot build on ground that is too steep*/
					for (int j = 0; j < b.size.y; ++j)
						for (int i = 0; i < b.size.x; ++i)
						{
							int max, min;
							getTileMaxMin(b.map.x + i, b.map.y + j, &max, &min);
							if (max - min > MAX_INCLINE)
							{
								return false;
							}
						}
				}

				// don't bother checking if already found a problem
				auto packThis = baseStructureTypePackability(psBuilding->type);

				// skirmish AIs don't build nondefensives next to anything. (route hack)
				if (packThis == PACKABILITY_NORMAL && bMultiPlayer &&
            game.type == LEVEL_TYPE::SKIRMISH && !
					  isHumanPlayer(player)) {
					packThis = PACKABILITY_REPAIR;
				}

				/* need to check there is one tile between buildings */
				for (int j = -1; j < b.size.y + 1; ++j)
					for (int i = -1; i < b.size.x + 1; ++i)
					{
						//skip the actual area the structure will cover
						if (i < 0 || i >= b.size.x || j < 0 || j >= b.size.y) {
							auto object = mapTile(b.map.x + i, b.map.y + j)->psObject;
							auto structure = dynamic_cast<Structure*>(object);
							if (structure != nullptr && !structure->visibilityState[player] && !aiCheckAlliances(
								player, structure->getPlayer())) {
								continue; // Ignore structures we can't see.
							}

							STRUCTURE_PACKABILITY packObj = baseObjectPackability(object);

							if (!canPack(packThis, packObj)) {
								return false;
							}
						}
					}
				if (psBuilding->flags & STRUCTURE_CONNECTED) {
					bool connection = false;
					for (int j = -1; j < b.size.y + 1; ++j)
					{
						for (int i = -1; i < b.size.x + 1; ++i)
						{
							//skip the actual area the structure will cover
							if (i < 0 || i >= b.size.x || j < 0 || j >= b.size.y) {
								Structure const* psStruct = getTileStructure(b.map.x + i, b.map.y + j);
								if (psStruct != nullptr && psStruct->getPlayer() == player && psStruct->getState() == STRUCTURE_STATE::BUILT) {
									connection = true;
									break;
								}
							}
						}
					}
					if (!connection) {
						return false; // needed to be connected to another building
					}
				}

				/*need to check each tile the structure will sit on*/
				for (int j = 0; j < b.size.y; ++j)
					for (int i = 0; i < b.size.x; ++i)
					{
						Tile const* psTile = mapTile(b.map.x + i, b.map.y + j);
						if (TileIsKnownOccupied(psTile, player)) {
							if (TileHasWall(psTile) && (psBuilding->type == DEFENSE || psBuilding->type == GATE
								|| psBuilding->type == WALL)) {
								Structure const* psStruct = getTileStructure(b.map.x + i, b.map.y + j);
								if (psStruct != nullptr && psStruct->getPlayer() != player) {
									return false;
								}
							}
							else {
								return false;
							}
						}
					}
				break;
			}
		case FACTORY_MODULE:
			if (TileHasStructure(worldTile(pos))) {
				Structure const* psStruct = getTileStructure(map_coord(pos.x), map_coord(pos.y));
				if (psStruct && (psStruct->getStats()->type == FACTORY ||
						psStruct->getStats()->type == VTOL_FACTORY)
					&& psStruct->getState() == STRUCTURE_STATE::BUILT && aiCheckAlliances(player, psStruct->getPlayer())
					&& nextModuleToBuild(psStruct, -1) > 0)
				{
					break;
				}
			}
			return false;
		case RESEARCH_MODULE:
			if (TileHasStructure(worldTile(pos))) {
				Structure const* psStruct = getTileStructure(map_coord(pos.x), map_coord(pos.y));
				if (psStruct && psStruct->getStats()->type == RESEARCH
					&& psStruct->getState() == STRUCTURE_STATE::BUILT
					&& aiCheckAlliances(player, psStruct->getPlayer())
					&& nextModuleToBuild(psStruct, -1) > 0)
				{
					break;
				}
			}
			return false;
		case POWER_MODULE:
			if (TileHasStructure(worldTile(pos)))
			{
				Structure const* psStruct = getTileStructure(map_coord(pos.x), map_coord(pos.y));
				if (psStruct && psStruct->getStats()->type == STRUCTURE_TYPE::POWER_GEN
					&& psStruct->getState() == STRUCTURE_STATE::BUILT
					&& aiCheckAlliances(player, psStruct->getPlayer())
					&& nextModuleToBuild(psStruct, -1) > 0) {
					break;
				}
			}
			return false;
    case STRUCTURE_TYPE::RESOURCE_EXTRACTOR:
			if (TileHasFeature(worldTile(pos))) {
				Feature const* psFeat = getTileFeature(map_coord(pos.x), map_coord(pos.y));
				if (psFeat && psFeat->getStats()->subType == FEATURE_TYPE::OIL_RESOURCE) {
					break;
				}
			}
			return false;
		}
		//if setting up a build queue need to check against future sites as well - AB 4/5/99
		if (ctrlShiftDown() && player == selectedPlayer && bCheckBuildQueue &&
			anyBlueprintTooClose(psBuilding, pos, direction)) {
			return false;
		}
	}
	else if (psTemplate != nullptr) {
		PropulsionStats* psPropStats = asPropulsionStats + psTemplate->asParts[COMP_PROPULSION];

		if (fpathBlockingTile(b.map.x, b.map.y, psPropStats->propulsionType)) {
			return false;
		}
	}
	else {
		// not positioning a structure or droid, ie positioning a feature
		if (fpathBlockingTile(b.map.x, b.map.y, PROPULSION_TYPE::WHEELED)) {
			return false;
		}
	}
	return true;
}

//remove a structure from the map
static void removeStructFromMap(Impl::Structure* psStruct)
{
	auxStructureNonblocking(*psStruct);

	/* set tiles drawing */
	StructureBounds b = getStructureBounds(psStruct);
	for (int j = 0; j < b.size.y; ++j)
	{
		for (int i = 0; i < b.size.x; ++i)
		{
			Tile* psTile = mapTile(b.map.x + i, b.map.y + j);
			psTile->psObject = nullptr;
			auxClearBlocking(b.map.x + i, b.map.y + j, AIR_BLOCKED);
		}
	}
}

// remove a structure from a game without any visible effects
// bDestroy = true if the object is to be destroyed
// (for example used to change the type of wall at a location)
bool removeStruct(Structure* psDel, bool bDestroy)
{
	bool resourceFound = false;
	FlagPosition* psAssemblyPoint = nullptr;

	ASSERT_OR_RETURN(false, psDel != nullptr, "Invalid structure pointer");

	int prevResearchState = intGetResearchState();

	if (bDestroy) {
		removeStructFromMap(psDel);
	}

	if (bDestroy) {
		//if the structure is a resource extractor, need to put the resource back in the map
		/*ONLY IF ANY POWER LEFT - HACK HACK HACK!!!! OIL POOLS NEED TO KNOW
		HOW MUCH IS THERE && NOT RES EXTRACTORS */
		if (psDel->getStats()->type == STRUCTURE_TYPE::RESOURCE_EXTRACTOR) {
			Feature* psOil = buildFeature(oilResFeature, psDel->getPosition().x, psDel->getPosition().y, false);
			memcpy(psOil->seenThisTick, psDel->visibilityState, sizeof(psOil->seenThisTick));
			resourceFound = true;
		}
	}

	if (psDel->getStats()->type == STRUCTURE_TYPE::RESOURCE_EXTRACTOR) {
		//tell associated Power Gen
		releaseResExtractor(psDel);
	}

	if (psDel->getStats()->type == STRUCTURE_TYPE::POWER_GEN) {
		//tell associated Res Extractors
		releasePowerGen(psDel);
	}

	//check for a research topic currently under way
	if (psDel->getStats()->type == STRUCTURE_TYPE::RESEARCH) {
		if (psDel->pFunctionality->researchFacility.psSubject) {
			//cancel the topic
			cancelResearch(psDel, ModeImmediate);
		}
	}

	//subtract one from the structLimits list so can build another - don't allow to go less than zero!
	if (asStructureStats[psDel->pStructureType - asStructureStats].curCount[psDel->getPlayer()]) {
		asStructureStats[psDel->pStructureType - asStructureStats].curCount[psDel->getPlayer()]--;
	}

	//if it is a factory - need to reset the factoryNumFlag
	if (StructIsFactory(psDel)) {
		Factory* psFactory = &psDel->pFunctionality->factory;

		//need to initialise the production run as well
		cancelProduction(psDel, ModeImmediate);

		psAssemblyPoint = psFactory->psAssemblyPoint.get();
	}
	else if (psDel->getStats()->type == STRUCTURE_TYPE::REPAIR_FACILITY) {
		psAssemblyPoint = psDel->pFunctionality->repairFacility.psDeliveryPoint;
	}

	if (psAssemblyPoint != nullptr) {
		if (psAssemblyPoint->factoryInc < factoryNumFlag[psDel->getPlayer()][psAssemblyPoint->factoryType].size()) {
			factoryNumFlag[psDel->getPlayer()][psAssemblyPoint->factoryType][psAssemblyPoint->factoryInc] = false;
		}

		//need to cancel the repositioning of the DP if selectedPlayer and currently moving
		if (psDel->getPlayer() == selectedPlayer && psAssemblyPoint->selected) {
			cancelDeliveryRepos();
		}
	}

	if (bDestroy)
	{
		debug(LOG_DEATH, "Killing off %s id %d (%p)", objInfo(psDel), psDel->getId(), static_cast<void *>(psDel));
		::killStruct(psDel);
	}

	if (psDel->getPlayer() == selectedPlayer) {
		intRefreshScreen();
	}

	delPowerRequest(psDel);

	intNotifyResearchButton(prevResearchState);

	return resourceFound;
}

/* Remove a structure */
bool destroyStruct(Structure* psDel, unsigned impactTime)
{
	unsigned widthScatter, breadthScatter, heightScatter;

	const auto burnDurationWall = 1000;
	const auto burnDurationOilWell = 60000;
	const auto burnDurationOther = 10000;

	CHECK_STRUCTURE(psDel);
	ASSERT(gameTime - deltaGameTime <= impactTime, "Expected %u <= %u, gameTime = %u, bad impactTime",
	       gameTime - deltaGameTime, impactTime, gameTime);

	/* Firstly, are we dealing with a wall section */
	const STRUCTURE_TYPE type = psDel->getStats()->type;
  using enum STRUCTURE_TYPE;
	const bool bMinor = type == WALL || type == WALL_CORNER;
	const bool bDerrick = type == RESOURCE_EXTRACTOR;
	const bool bPowerGen = type == POWER_GEN;
	unsigned burnDuration = bMinor ? burnDurationWall : bDerrick ? burnDurationOilWell : burnDurationOther;
	if (psDel->getState() == STRUCTURE_STATE::BEING_BUILT) {
		burnDuration = static_cast<unsigned>(burnDuration * structureCompletionProgress(*psDel));
	}

	/* Only add if visible */
	if (psDel->visibleToSelectedPlayer()) {
		Vector3i pos;
		int i;

		/* Set off some explosions, but not for walls */
		/* First Explosions */
		widthScatter = TILE_UNITS;
		breadthScatter = TILE_UNITS;
		heightScatter = TILE_UNITS;
		for (i = 0; i < (bMinor ? 2 : 4); ++i) // only add two for walls - gets crazy otherwise
		{
			pos.x = psDel->getPosition().x + widthScatter - rand() % (2 * widthScatter);
			pos.z = psDel->getPosition().y + breadthScatter - rand() % (2 * breadthScatter);
			pos.y = psDel->getPosition().z + 32 + rand() % heightScatter;
			addEffect(&pos, EFFECT_GROUP::EXPLOSION, EFFECT_TYPE::EXPLOSION_TYPE_MEDIUM, false, nullptr, 0, impactTime);
		}

		/* Get coordinates for everybody! */
		pos.x = psDel->getPosition().x;
		pos.z = psDel->getPosition().y; // z = y [sic] intentional
		pos.y = map_Height(pos.x, pos.z);

		// Set off a fire, provide dimensions for the fire
		if (bMinor) {
			effectGiveAuxVar(world_coord(psDel->getStats()->base_width) / 4);
		}
		else {
			effectGiveAuxVar(world_coord(psDel->getStats()->base_width) / 3);
		}
		/* Give a duration */
		effectGiveAuxVarSec(burnDuration);
		if (bDerrick) // oil resources
		{
			/* Oil resources burn AND puff out smoke AND for longer*/
			addEffect(&pos, EFFECT_GROUP::FIRE, EFFECT_TYPE::FIRE_TYPE_SMOKY, false, nullptr, 0, impactTime);
		}
		else // everything else
		{
			addEffect(&pos, EFFECT_GROUP::FIRE, EFFECT_TYPE::FIRE_TYPE_LOCALISED, false, nullptr, 0, impactTime);
		}

		/* Power stations have their own destruction sequence */
		if (bPowerGen) {
			addEffect(&pos, EFFECT_GROUP::DESTRUCTION, EFFECT_TYPE::DESTRUCTION_TYPE_POWER_STATION, false, nullptr, 0, impactTime);
			pos.y += SHOCK_WAVE_HEIGHT;
			addEffect(&pos, EFFECT_GROUP::EXPLOSION, EFFECT_TYPE::EXPLOSION_TYPE_SHOCKWAVE, false, nullptr, 0, impactTime);
		}
		/* As do wall sections */
		else if (bMinor) {
			addEffect(&pos, EFFECT_GROUP::DESTRUCTION, EFFECT_TYPE::DESTRUCTION_TYPE_WALL_SECTION, false, nullptr, 0, impactTime);
		}
		else // and everything else goes here.....
		{
			addEffect(&pos, EFFECT_GROUP::DESTRUCTION, EFFECT_TYPE::DESTRUCTION_TYPE_STRUCTURE, false, nullptr, 0, impactTime);
		}

		// shake the screen if we're near enough and it is explosive in nature
		if (clipXY(pos.x, pos.z)) {
			switch (type) {
			// These are the types that would cause a explosive outcome if destoryed
			case HQ:
			case POWER_GEN:
			case MISSILE_SILO: // for campaign
				shakeStart(1500);
				break;
			case COMMAND_CONTROL:
			case VTOL_FACTORY:
			case CYBORG_FACTORY:
			case FACTORY:
				shakeStart(750);
				break;
			case RESOURCE_EXTRACTOR:
				shakeStart(400);
				break;
			default:
				break;
			}
		}

		// and add a sound effect
		audio_PlayStaticTrack(psDel->getPosition().x, psDel->getPosition().y, ID_SOUND_EXPLOSION);
	}

	// Actually set the tiles on fire - even if the effect is not visible.
	tileSetFire(psDel->getPosition().x, psDel->getPosition().y, burnDuration);

	const bool resourceFound = removeStruct(psDel, true);
	psDel->died = impactTime;

	// Leave burn marks in the ground where building once stood
	if (psDel->visibleToSelectedPlayer() && !resourceFound && !bMinor) {
		auto b = getStructureBounds(psDel);
		for (int breadth = 0; breadth < b.size.y; ++breadth)
		{
			for (int width = 0; width < b.size.x; ++width)
			{
				Tile* psTile = mapTile(b.map.x + width, b.map.y + breadth);
				if (TEST_TILE_VISIBLE_TO_SELECTEDPLAYER(psTile)) {
					psTile->illumination /= 2;
				}
			}
		}
	}

	if (bMultiPlayer) {
		technologyGiveAway(psDel); // Drop an artifact, if applicable.
	}

	// updates score stats only if not wall
	if (!bMinor) {
		if (psDel->getPlayer() == selectedPlayer) {
			scoreUpdateVar(WD_STR_LOST);
		}
		// only counts as a kill if structure doesn't belong to our ally
		else if (selectedPlayer < MAX_PLAYERS && !aiCheckAlliances(psDel->getPlayer(), selectedPlayer)) {
			scoreUpdateVar(WD_STR_KILLED);
		}
	}
	return true;
}

/* gets a structure stat from its name - relies on the name being unique (or it will
return the first one it finds!! */
int getStructStatFromName(const WzString& name)
{
	auto psStat = getStructStatsFromName(name);
	if (psStat) {
		return psStat->index;
	}
	return -1;
}

StructureStats* getStructStatsFromName(const WzString& name)
{
	StructureStats* psStat = nullptr;
	auto it = lookupStructStatPtr.find(name);
	if (it != lookupStructStatPtr.end()) {
		psStat = it->second;
	}
	return psStat;
}

/*check to see if the structure is 'doing' anything  - return true if idle*/
bool structureIdle(const Structure* psBuilding)
{
	BaseStats* pSubject = nullptr;
	CHECK_STRUCTURE(psBuilding);

  using enum STRUCTURE_TYPE;
	// determine the Subject
	switch (psBuilding->getStats()->type) {
	case RESEARCH:
		{
			pSubject = dynamic_cast<ResearchFacility*>(psBuilding)->psSubject;
			break;
		}
	case FACTORY:
	case CYBORG_FACTORY:
	case VTOL_FACTORY:
		{
			pSubject = dynamic_cast<const Factory*>(psBuilding)->psSubject.get();
			break;
		}
	default:
		break;
	}

	if (pSubject != nullptr) {
		return false;
	}
	return true;
}

/*checks to see if a specific structure type exists -as opposed to a structure
stat type*/
bool checkSpecificStructExists(unsigned structInc, unsigned player)
{
	ASSERT_OR_RETURN(false, structInc < numStructureStats, "Invalid structure inc");

	for (auto& psStructure : apsStructLists[player])
	{
		if (psStructure->getState() == STRUCTURE_STATE::BUILT) {
			if (psStructure->getStats()->ref - STAT_STRUCTURE == structInc) {
				return true;
			}
		}
	}
	return false;
}

/*finds a suitable position for the assembly point based on one passed in*/
void findAssemblyPointPosition(unsigned* pX, unsigned* pY, unsigned player)
{
	//set up a dummy stat pointer
	StructureStats sStats;
	unsigned passes = 0;
	int i, j, startX, endX, startY, endY;

	sStats.ref = 0;
	sStats.base_width = 1;
	sStats.base_breadth = 1;

	/* Initial box dimensions and set iteration count to zero */
	startX = endX = *pX;
	startY = endY = *pY;
	passes = 0;

	//if the value passed in is not a valid location - find one!
	if (!validLocation(&sStats, world_coord(Vector2i(*pX, *pY)), 0, player, false))
	{
		/* Keep going until we get a tile or we exceed distance */
		while (passes < LOOK_FOR_EMPTY_TILE)
		{
			/* Process whole box */
			for (i = startX; i <= endX; i++)
			{
				for (j = startY; j <= endY; j++)
				{
					/* Test only perimeter as internal tested previous iteration */
					if (i == startX || i == endX || j == startY || j == endY) {
						/* Good enough? */
						if (validLocation(&sStats, world_coord(Vector2i(i, j)), 0, player, false)) {
							/* Set exit conditions and get out NOW */
							*pX = i;
							*pY = j;
							//jump out of the loop
							return;
						}
					}
				}
			}
			/* Expand the box out in all directions - off map handled by validLocation() */
			startX--;
			startY--;
			endX++;
			endY++;
			passes++;
		}
	}
	else {
		//the first location was valid
		return;
	}
	/* If we got this far, then we failed - passed in values will be unchanged */
	ASSERT(!"unable to find a valid location", "unable to find a valid location!");
}


/*sets the point new droids go to - x/y in world coords for a Factory
bCheck is set to true for initial placement of the Assembly Point*/
void setAssemblyPoint(FlagPosition* psAssemblyPoint, unsigned x, unsigned y,
                      unsigned player, bool bCheck)
{
	ASSERT_OR_RETURN(, psAssemblyPoint != nullptr, "invalid AssemblyPoint pointer");

	//check its valid
	x = map_coord(x);
	y = map_coord(y);
	if (bCheck) {
		findAssemblyPointPosition(&x, &y, player);
	}
	//add half a tile so the centre is in the middle of the tile
	x = world_coord(x) + TILE_UNITS / 2;
	y = world_coord(y) + TILE_UNITS / 2;

	psAssemblyPoint->coords.x = x;
	psAssemblyPoint->coords.y = y;

	// Deliv Point sits at the height of the tile it's centre is on + arbitrary amount!
	psAssemblyPoint->coords.z = map_Height(x, y) + ASSEMBLY_POINT_Z_PADDING;
}


/*sets the factory Inc for the Assembly Point*/
void setFlagPositionInc(Structure* psStruct, unsigned player, uint8_t factoryType)
{
	ASSERT_OR_RETURN(, player < MAX_PLAYERS, "invalid player number");

	// find the first vacant slot
	auto inc = std::find(factoryNumFlag[player][factoryType].begin(),
                            factoryNumFlag[player][factoryType].end(),
                            false) - factoryNumFlag[player][factoryType].begin();
	if (inc == factoryNumFlag[player][factoryType].size()) {
		// first time init for this factory flag slot, set it to false
		factoryNumFlag[player][factoryType].push_back(false);
	}

	if (factoryType == REPAIR_FLAG) {
		// this is a special case, there are no flag numbers for this "factory"
		auto psRepair = dynamic_cast<RepairFacility*>(psStruct);
		psRepair->psDeliveryPoint->factoryInc = 0;
		psRepair->psDeliveryPoint->factoryType = factoryType;
		// factoryNumFlag[player][factoryType][inc] = true;
	}
	else {
		auto psFactory = dynamic_cast<Factory*>(psStruct);
		psFactory->psAssemblyPoint->factoryInc = inc;
		psFactory->psAssemblyPoint->factoryType = factoryType;
		factoryNumFlag[player][factoryType][inc] = true;
	}
}

StructureStats* structGetDemolishStat()
{
	ASSERT_OR_RETURN(nullptr, g_psStatDestroyStruct != nullptr, "Demolish stat not initialised");
	return g_psStatDestroyStruct;
}


/*sets the flag to indicate a SatUplink Exists - so draw everything!*/
void setSatUplinkExists(bool state, unsigned player)
{
	satUplinkExists[player] = (uint8_t)state;
	if (state) {
		satuplinkbits |= (1 << player);
	}
	else {
		satuplinkbits &= ~(1 << player);
	}
}

/*returns the status of the flag*/
bool getSatUplinkExists(unsigned player)
{
	return satUplinkExists[player];
}

/*sets the flag to indicate a Las Sat Exists - ONLY EVER WANT ONE*/
void setLasSatExists(bool state, unsigned player)
{
	lasSatExists[player] = (uint8_t)state;
}

/* calculate muzzle base location in 3d world */
bool calcStructureMuzzleBaseLocation(const Structure* psStructure, Vector3i* muzzle, int weapon_slot)
{
	const auto& psShape = psStructure->getStats()->IMDs[0];
	CHECK_STRUCTURE(psStructure);

	if (psShape && psShape->nconnectors) {
		Vector3i barrel(0, 0, 0);

		Affine3F af;

		af.Trans(psStructure->getPosition().x, -psStructure->getPosition().z,
             psStructure->getPosition().y);

		//matrix = the center of droid
		af.RotY(psStructure->getRotation().direction);
		af.RotX(psStructure->getRotation().pitch);
		af.RotZ(-psStructure->getRotation().roll);
		af.Trans(psShape->connectors[weapon_slot].x, -psShape->connectors[weapon_slot].z,
		         -psShape->connectors[weapon_slot].y); //note y and z flipped


		*muzzle = (af * barrel).xzy();
		muzzle->z = -muzzle->z;
	}
	else {
		*muzzle = psStructure->getPosition() + Vector3i(
            0, 0, psStructure->getDisplayData().imd_shape->max.y);
	}
	return true;
}

/* calculate muzzle tip location in 3d world */
bool calcStructureMuzzleLocation(const Structure* psStructure, Vector3i* muzzle, int weapon_slot)
{
	const auto& psShape = psStructure->getStats()->IMDs[0];

	CHECK_STRUCTURE(psStructure);

	if (psShape && psShape->nconnectors) {
		Vector3i barrel(0, 0, 0);
		auto nWeaponStat = psStructure->getWeapons()[weapon_slot].getStats();
		const iIMDShape *psWeaponImd = nullptr, *psMountImd = nullptr;

    psWeaponImd = nWeaponStat->pIMD.get();
    psMountImd = nWeaponStat->pMountGraphic.get();

		Affine3F af;

		af.Trans(psStructure->getPosition().x, -psStructure->getPosition().z, psStructure->getPosition().y);

		//matrix = the center of droid
		af.RotY(psStructure->getRotation().direction);
		af.RotX(psStructure->getRotation().pitch);
		af.RotZ(-psStructure->getRotation().roll);
		af.Trans(psShape->connectors[weapon_slot].x, -psShape->connectors[weapon_slot].z,
		         -psShape->connectors[weapon_slot].y); //note y and z flipped

		//matrix = the weapon[slot] mount on the body
		af.RotY(psStructure->getWeapons()[weapon_slot].getRotation().direction); // +ve anticlockwise

		// process turret mount
		if (psMountImd && psMountImd->nconnectors) {
			af.Trans(psMountImd->connectors->x, -psMountImd->connectors->z, -psMountImd->connectors->y);
		}

		//matrix = the turret connector for the gun
		af.RotX(psStructure->getWeapons()[weapon_slot].getRotation().pitch); // +ve up

		//process the gun
		if (psWeaponImd && psWeaponImd->nconnectors) {
			unsigned int connector_num = 0;

			// which barrel is firing if model have multiple muzzle connectors?
			if (psStructure->getWeapons()[weapon_slot].shotsFired && (psWeaponImd->nconnectors > 1)) {
				// shoot first, draw later - substract one shot to get correct results
				connector_num = (psStructure->getWeapons()[weapon_slot].shotsFired - 1) % (psWeaponImd->nconnectors);
			}

			barrel = Vector3i(psWeaponImd->connectors[connector_num].x, -psWeaponImd->connectors[connector_num].z,
			                  -psWeaponImd->connectors[connector_num].y);
		}

		*muzzle = (af * barrel).xzy();
		muzzle->z = -muzzle->z;
	}
	else {
		*muzzle = psStructure->getPosition() + Vector3i(0, 0, 0 + psStructure->getDisplayData().imd_shape->max.y);
	}

	return true;
}


/*Looks through the list of structures to see if there are any inactive
resource extractors*/
void checkForResExtractors(Structure* psBuilding)
{
	ASSERT_OR_RETURN(, psBuilding->getStats()->type == STRUCTURE_TYPE::POWER_GEN, "invalid structure type");

	// find derricks, sorted by unused first, then ones attached to power generators without modules.
	typedef std::pair<int, Structure*> Derrick;
	typedef std::vector<Derrick> Derricks;
	Derricks derricks;
	derricks.reserve(NUM_POWER_MODULES + 1);
	for (auto& currExtractor : apsExtractorLists[psBuilding->getPlayer()])
  {
		if (currExtractor->getState() != STRUCTURE_STATE::BUILT) {
			continue; // derrick not complete.
		}
		auto priority = currExtractor->power_generator != nullptr
            ? currExtractor->power_generator->capacity
            : -1;
		auto d = derricks.begin();
		while (d != derricks.end() && d->first <= priority)
		{
			++d;
		}
		derricks.insert(d, Derrick(priority, currExtractor));
		derricks.resize(std::min<unsigned>(derricks.size(), NUM_POWER_MODULES));
		// no point remembering more derricks than this.
	}

	// attach derricks.
	auto d = derricks.begin();
	for (auto i = 0; i < NUM_POWER_MODULES; ++i)
	{
		auto powerGen = dynamic_cast<PowerGenerator*>(psBuilding);
		if (powerGen->resource_extractors[i] != nullptr) {
			continue; // slot full.
		}

		auto priority = psBuilding->capacity;
		if (d == derricks.end() || d->first >= priority) {
			continue; // No more derricks to transfer to this power generator.
		}

		auto derrick = d->second;
		auto resExtractor = dynamic_cast<ResourceExtractor*>(derrick);
		if (resExtractor->power_generator != nullptr) {
			informPowerGen(derrick); // Remove the derrick from the previous power generator.
		}
		// Assign the derrick to the power generator.
		powerGen->resource_extractors[i] = derrick;
		resExtractor->power_generator = psBuilding;

		++d;
	}
}

uint16_t countPlayerUnusedDerricks()
{
	uint16_t total = 0;

	if (selectedPlayer >= MAX_PLAYERS) {
    return 0;
  }

	for (auto psStruct : apsExtractorLists[selectedPlayer])
	{
		if (psStruct->getState() == STRUCTURE_STATE::BUILT &&
        psStruct->getStats().type == STRUCTURE_TYPE::RESOURCE_EXTRACTOR) {
			if (!dynamic_cast<ResourceExtractor*>(psStruct)->power_generator) {
				total++;
			}
		}
	}
	return total;
}

/*Looks through the list of structures to see if there are any Power Gens
with available slots for the new Res Ext*/
void ResourceExtractor::checkForPowerGen()
{
	if (power_generator != nullptr) {
		return;
	}

	// find a power generator, if possible with a power module.
	Structure* bestPowerGen = nullptr;
	int bestSlot = 0;
	for (auto psCurr : apsStructLists[getPlayer()])
	{
		if (psCurr->getStats()->type == STRUCTURE_TYPE::POWER_GEN &&
        psCurr->getState() == STRUCTURE_STATE::BUILT) {

			if (bestPowerGen != nullptr &&
          bestPowerGen->getCapacity() >= psCurr->getCapacity()) {
				continue; // power generator not better.
			}

			auto psPG = dynamic_cast<PowerGenerator*>(psCurr);
			for (int i = 0; i < NUM_POWER_MODULES; ++i)
			{
				if (psPG->resource_extractors[i] == nullptr) {
					bestPowerGen = psCurr;
					bestSlot = i;
					break;
				}
			}
		}
	}

	if (bestPowerGen != nullptr) {
		// attach the derrick to the power generator.
		auto psPG = dynamic_cast<PowerGenerator*>(bestPowerGen);
		psPG->resource_extractors[bestSlot] = this;
		power_generator = bestPowerGen;
	}
}

/*initialise the slot the Resource Extractor filled in the owning Power Gen*/
void informPowerGen(ResourceExtractor* psRE)
{
	// get the owning power generator
	auto psPowerGen = psRE->power_generator;
	if (psPowerGen) {
		for (auto i = 0; i < NUM_POWER_MODULES; i++)
		{
			if (psPowerGen->resource_extractors[i] == psRE) {
				// initialise the 'slot'
				psPowerGen->resource_extractors[i] = nullptr;
				break;
			}
		}
	}
}

/*called when a Res extractor is destroyed or runs out of power or is disconnected
adjusts the owning Power Gen so that it can link to a different Res Extractor if one
is available*/
void ResourceExtractor::releaseResExtractor()
{
	// tell associated Power Gen
	if (power_generator) {
		informPowerGen(this);
	}
	power_generator = nullptr;

	// there may be spare resource extractors
	for (auto psCurr : apsExtractorLists[getPlayer()])
	{
		//check not connected and power left and built!
		if (psCurr != this &&
        psCurr->power_generator == nullptr &&
        psCurr->getState() == STRUCTURE_STATE::BUILT) {
			checkForPowerGen(psCurr);
		}
	}
}

/*called when a Power Gen is destroyed or is disconnected
adjusts the associated Res Extractors so that they can link to different Power
Gens if any are available*/
void PowerGenerator::releasePowerGen()
{
	unsigned i;

	//go through list of res extractors, setting them to inactive
	for (i = 0; i < NUM_POWER_MODULES; i++)
	{
		if (resource_extractors[i]) {
			resource_extractors[i]->power_generator = nullptr;
			resource_extractors[i] = nullptr;
		}
	}
	// may have a power gen with spare capacity
	for (auto psCurr : apsStructLists[getPlayer()])
	{
		if (psCurr->getStats()->type == STRUCTURE_TYPE::POWER_GEN &&
			psCurr != this && psCurr->getState() == STRUCTURE_STATE::BUILT) {
			checkForResExtractors(psCurr);
		}
	}
}


/*for a given structure, return a pointer to its module stat */
StructureStats* getModuleStat(const Structure* psStruct)
{
	ASSERT_OR_RETURN(nullptr, psStruct != nullptr, "Invalid structure pointer");

  using enum STRUCTURE_TYPE;
	switch (psStruct->getStats()->type) {
	case POWER_GEN:
		return &asStructureStats[powerModuleStat];
	case FACTORY:
	case VTOL_FACTORY:
		return &asStructureStats[factoryModuleStat];
	case RESEARCH:
		return &asStructureStats[researchModuleStat];
	default:
		// no other structures can have modules attached
		return nullptr;
	}
}

unsigned countAssignedDroids(const Structure& structure)
{
  const auto& droids = apsDroidLists[selectedPlayer];
  return std::count_if(droids.begin(), droids.end(),
                       [&structure](const auto& droid)
  {
    if (droid.getOrder().target->getId() == structure.getId() &&
            droid.getPlayer() == structure.getPlayer())  {
      return droid.isVtol() || hasArtillery(structure);
    }
  });
}
/**
 * Count the artillery and VTOL droids assigned to a structure.
 */
//static unsigned int countAssignedDroids(const STRUCTURE* psStructure)
//{
//	const DROID* psCurr;
//	unsigned int num;
//
//	CHECK_STRUCTURE(psStructure);
//
//	// For non-debug builds
//	if (psStructure == nullptr)
//	{
//		return 0;
//	}
//
//	if (selectedPlayer >= MAX_PLAYERS)
//	{
//		return 0;
//	}
//
//	num = 0;
//	for (psCurr = apsDroidLists[selectedPlayer]; psCurr; psCurr = psCurr->psNext)
//	{
//		if (psCurr->order.psObj
//			&& psCurr->order.psObj->id == psStructure->id
//			&& psCurr->player == psStructure->player)
//		{
//			const MOVEMENT_MODEL weapontype = asWeaponStats[psCurr->asWeaps[0].nStat].movementModel;
//
//			if (weapontype == MM_INDIRECT
//				|| weapontype == MM_HOMINGINDIRECT
//				|| isVtolDroid(psCurr))
//			{
//				num++;
//			}
//		}
//	}
//
//	return num;
//}

/// Print some info at the top of the screen dependent on the structure
void printStructureInfo(Structure* psStructure)
{
	unsigned numConnected;
	PowerGenerator* psPowerGen;

	ASSERT_OR_RETURN(, psStructure != nullptr, "Invalid Structure pointer");

	if (isBlueprint(psStructure)) {
		return; // Don't print anything about imaginary structures. Would crash, anyway.
	}

	const DebugInputManager& dbgInputManager = gInputManager.debugManager();
  using enum STRUCTURE_TYPE;
	switch (psStructure->getStats()->type) {
	case HQ:
		{
			auto assigned_droids = countAssignedDroids(*psStructure);
			console(ngettext("%s - %u Unit assigned - Hitpoints %d/%d",
                       "%s - %u Units assigned - Hitpoints %d/%d",
			                 assigned_droids),
			        getStatsName(psStructure->getStats()), assigned_droids, psStructure->getHp(),
			        structureBody(psStructure));
			if (dbgInputManager.debugMappingsAllowed()) {
				console(_("ID %d - sensor range %d - ECM %d"),
                psStructure->getId(),
                structSensorRange(psStructure),
				        structJammerPower(psStructure));
			}
			break;
		}
	case DEFENSE:
    using enum SENSOR_TYPE;
		if (psStructure->getStats()->sensor_stats != nullptr
			&& (psStructure->getStats()->sensor_stats->type == STANDARD
				|| psStructure->getStats()->sensor_stats->type == INDIRECT_CB
				|| psStructure->getStats()->sensor_stats->type == VTOL_INTERCEPT
				|| psStructure->getStats()->sensor_stats->type == VTOL_CB
				|| psStructure->getStats()->sensor_stats->type == SUPER
				|| psStructure->getStats()->sensor_stats->type == RADAR_DETECTOR)
			&& psStructure->getStats()->sensor_stats->location == LOC::TURRET) {
			auto assigned_droids = countAssignedDroids(*psStructure);
			console(ngettext("%s - %u Unit assigned - Damage %d/%d", "%s - %u Units assigned - Hitpoints %d/%d",
			                 assigned_droids),
			        getStatsName&psStructure->getStats()), assigned_droids, psStructure->getHp(),
			        structureBody(psStructure));
		}
		else {
			console(_("%s - Hitpoints %d/%d"),
              getStatsName(psStructure->getStats()), psStructure->getHp(),
			        structureBody(psStructure));
		}
		if (dbgInputManager.debugMappingsAllowed()) {
			console(_("ID %d - armour %d|%d - sensor range %d - ECM %d - born %u - depth %.02f"),
			        psStructure->getId(), objArmour(psStructure, WEAPON_CLASS::KINETIC), objArmour(psStructure, WEAPON_CLASS::HEAT),
			        structSensorRange(psStructure), structJammerPower(psStructure), psStructure->bornTime,
			        psStructure->getFoundationDepth());
		}
		break;
	case REPAIR_FACILITY:
		console(_("%s - Hitpoints %d/%d"), getStatsName(psStructure->getStats()), psStructure->getHp(),
		        structureBody(psStructure));
		if (dbgInputManager.debugMappingsAllowed()) {
			console(_("ID %d - Queue %d"), psStructure->getId(),
              dynamic_cast<RepairFacility*>(psStructure)->droidQueue);
		}
		break;
	case RESOURCE_EXTRACTOR:
		console(_("%s - Hitpoints %d/%d"), getStatsName(psStructure->getStats()), psStructure->getHp(),
		        structureBody(psStructure));
		if (dbgInputManager.debugMappingsAllowed() && selectedPlayer < MAX_PLAYERS) {
			console(_("ID %d - %s"), psStructure->getId(),
			        (auxTile(map_coord(psStructure->getPosition().x), map_coord(psStructure->getPosition().y), selectedPlayer) &
				        AUXBITS_DANGER)
				        ? "danger"
				        : "safe");
		}
		break;
	case POWER_GEN:
		psPowerGen = dynamic_cast<PowerGenerator*>(psStructure);
		numConnected = 0;
		for (int i = 0; i < NUM_POWER_MODULES; i++)
		{
			if (psPowerGen->resource_extractors[i]) {
				numConnected++;
			}
		}
		console(_("%s - Connected %u of %u - Hitpoints %d/%d"), getStatsName(psStructure->getStats()), numConnected,
		        NUM_POWER_MODULES, psStructure->getHp(), structureBody(psStructure));
		if (dbgInputManager.debugMappingsAllowed()) {
			console(_("ID %u - Multiplier: %u"), psStructure->getId(), getBuildingPowerPoints(psStructure));
		}
		break;
	case CYBORG_FACTORY:
	case VTOL_FACTORY:
	case FACTORY:
		console(_("%s - Hitpoints %d/%d"), getStatsName(psStructure->getStats()), psStructure->getHp(),
		        structureBody(psStructure));
		if (dbgInputManager.debugMappingsAllowed()) {
			console(
				_("ID %u - Production Output: %u - BuildPointsRemaining: %u - Resistance: %d / %d"),
        psStructure->getId(), getBuildingProductionPoints(psStructure),
        dynamic_cast<Factory*>(psStructure)->buildPointsRemaining,
				psStructure->getResistance(),
        structureResistance(psStructure->getStats(), psStructure->getPlayer()));
		}
		break;
	case RESEARCH:
		console(_("%s - Hitpoints %d/%d"), getStatsName(psStructure->getStats()), psStructure->getHp(),
		        structureBody(psStructure));
		if (dbgInputManager.debugMappingsAllowed()) {
			console(_("ID %u - Research Points: %u"), psStructure->getId(), getBuildingResearchPoints(psStructure));
		}
		break;
	case REARM_PAD:
		console(_("%s - Hitpoints %d/%d"), getStatsName(psStructure->getStats()), psStructure->getHp(),
		        structureBody(psStructure));
		if (dbgInputManager.debugMappingsAllowed()) {
			console(_("tile %d,%d - target %s"), psStructure->getPosition().x / TILE_UNITS, psStructure->getPosition().y / TILE_UNITS,
			        objInfo(dynamic_cast<RearmPad*>(psStructure)->psObj));
		}
		break;
	default:
		console(_("%s - Hitpoints %d/%d"), getStatsName(psStructure->getStats()), psStructure->getHp(),
		        structureBody(psStructure));
		if (dbgInputManager.debugMappingsAllowed()) {
			console(_("ID %u - sensor range %d - ECM %d"), psStructure->getId(), structSensorRange(psStructure),
			        structJammerPower(psStructure));
		}
		break;
	}
}

/*Checks the template type against the factory type - returns false
if not a good combination!*/
bool validTemplateForFactory(const DroidTemplate* psTemplate, Structure* psFactory, bool complain)
{
	ASSERT_OR_RETURN(false, psTemplate, "Invalid template!");
	enum code_part level = complain ? LOG_ERROR : LOG_NEVER;

	//not in multiPlayer! - AB 26/5/99
	//ignore Transporter Droids
	if (!bMultiPlayer && isTransporter(*psTemplate)) {
		debug(level, "Cannot build transporter in campaign.");
		return false;
	}

	//check if droid is a cyborg
	if (psTemplate->type == DROID_TYPE::CYBORG ||
      psTemplate->type == DROID_TYPE::CYBORG_SUPER ||
      psTemplate->type == DROID_TYPE::CYBORG_CONSTRUCT ||
      psTemplate->type == DROID_TYPE::CYBORG_REPAIR) {
		if (psFactory->getStats()->type != STRUCTURE_TYPE::CYBORG_FACTORY) {
			debug(level, "Cannot build cyborg except in cyborg factory, not in %s.", objInfo(psFactory));
			return false;
		}
	}
	//check for VTOL droid
	else if (psTemplate->asParts[COMP_PROPULSION] &&
		((asPropulsionStats + psTemplate->asParts[COMP_PROPULSION])->propulsionType == PROPULSION_TYPE::LIFT)) {
		if (psFactory->getStats()->type != STRUCTURE_TYPE::VTOL_FACTORY) {
			debug(level, "Cannot build vtol except in vtol factory, not in %s.", objInfo(psFactory));
			return false;
		}
	}

	//check if cyborg factory
	if (psFactory->getStats()->type == STRUCTURE_TYPE::CYBORG_FACTORY) {
		if (!(psTemplate->type == DROID_TYPE::CYBORG ||
          psTemplate->type == DROID_TYPE::CYBORG_SUPER ||
          psTemplate->type == DROID_TYPE::CYBORG_CONSTRUCT ||
          psTemplate->type == DROID_TYPE::CYBORG_REPAIR)) {
			debug(level, "Can only build cyborg in cyborg factory, not droidType %d in %s.",
            psTemplate->type, objInfo(psFactory));
			return false;
		}
	}
	//check if vtol factory
	else if (psFactory->getStats()->type == STRUCTURE_TYPE::VTOL_FACTORY) {
		if (!psTemplate->asParts[COMP_PROPULSION] ||
			((asPropulsionStats + psTemplate->asParts[COMP_PROPULSION])->propulsionType != PROPULSION_TYPE::LIFT)) {
			debug(level, "Can only build vtol in vtol factory, not in %s.", objInfo(psFactory));
			return false;
		}
	}
	//got through all the tests...
	return true;
}

/*calculates the damage caused to the resistance levels of structures - returns
true when captured*/
bool electronicDamage(BaseObject * psTarget, unsigned damage, uint8_t attackPlayer)
{
	bool bCompleted = true;
	Vector3i pos;
	ASSERT_OR_RETURN(false, attackPlayer < MAX_PLAYERS, "Invalid player id %d", (int)attackPlayer);
	ASSERT_OR_RETURN(false, psTarget != nullptr, "Target is NULL");

	//structure electronic damage
	if (auto psStructure = dynamic_cast<Structure*>(psTarget)) {
		bCompleted = false;

		if (psStructure->getStats()->upgraded_stats[psStructure->getPlayer()].resistance == 0) {
			return false; // this structure type cannot be taken over
		}

		//if resistance is already less than 0 don't do any more
		if (psStructure->getResistance() < 0) {
			bCompleted = true;
		}
		else {
			//store the time it was hit
			int lastHit = psStructure->timeLastHit;
			psStructure->timeLastHit = gameTime;

			psStructure->lastHitWeapon = WEAPON_SUBCLASS::ELECTRONIC;

			triggerEventAttacked(psStructure, g_pProjLastAttacker, lastHit);

			psStructure->resistance = (SWORD)(psStructure->getResistance() - damage);

			if (psStructure->getResistance() < 0) {
				//add a console message for the selected Player
				if (psStructure->getPlayer() == selectedPlayer) {
					console(_("%s - Electronically Damaged"),
					        getStatsName(psStructure->getStats()));
				}
				bCompleted = true;
				//give the structure to the attacking player
				(void)giftSingleStructure(psStructure, attackPlayer);
			}
		}
	}
	//droid electronic damage
	else if (auto psDroid = dynamic_cast<Droid*>(psTarget)) {
		bCompleted = false;
		int lastHit = psDroid->timeLastHit;
		psDroid->timeLastHit = gameTime;
		psDroid->lastHitWeapon = WEAPON_SUBCLASS::ELECTRONIC;

		//in multiPlayer cannot attack a Transporter with EW
		if (bMultiPlayer) {
			ASSERT_OR_RETURN(true, !isTransporter(*psDroid), "Cannot attack a Transporter in multiPlayer");
		}

		if (psDroid->getResistance() == ACTION_START_TIME) {
			//need to set the current resistance level since not been previously attacked (by EW)
			psDroid->resistance = droidResistance(psDroid);
		}

		if (psDroid->getResistance() < 0) {
			bCompleted = true;
		}
		else {
			triggerEventAttacked(psDroid, g_pProjLastAttacker, lastHit);

			psDroid->resistance = (SWORD)(psDroid->resistance - damage);

			if (psDroid->getResistance() <= 0) {
				//add a console message for the selected Player
				if (psDroid->getPlayer() == selectedPlayer) {
					console(_("%s - Electronically Damaged"), "Unit");
				}
				bCompleted = true;

				//give the droid to the attacking player

				if (psDroid->visibleToSelectedPlayer()) // display-only check for adding effect
				{
					for (int i = 0; i < 5; i++)
					{
						pos.x = psDroid->getPosition().x + (30 - rand() % 60);
						pos.z = psDroid->getPosition().y + (30 - rand() % 60);
						pos.y = psDroid->getPosition().z + (rand() % 8);
						effectGiveAuxVar(80);
						addEffect(&pos, EFFECT_GROUP::EXPLOSION,
                      EFFECT_TYPE::EXPLOSION_TYPE_FLAMETHROWER,
                      false, nullptr, 0,gameTime - deltaGameTime);
					}
				}
				if (!psDroid->isDead() && !giftSingleDroid(psDroid, attackPlayer, true))
				{
					// droid limit reached, recycle
					// don't check for transporter/mission coz multiplayer only issue.
					recycleDroid(psDroid);
				}
			}
		}
	}
	return bCompleted;
}

/* EW works differently in multiplayer mode compared with single player.*/
bool validStructResistance(const Structure* psStruct)
{
	bool bTarget = false;
	ASSERT_OR_RETURN(false, psStruct != nullptr, "Invalid structure pointer");

  using enum STRUCTURE_TYPE;
	if (psStruct->getStats()->upgraded_stats[psStruct->getPlayer()].resistance != 0) {
		/*certain structures will only provide rewards in multiplayer so
		before they can become valid targets their resistance must be at least
		half the base value*/
		if (bMultiPlayer) {
			switch (psStruct->getStats()->type) {
			case RESEARCH:
			case FACTORY:
			case VTOL_FACTORY:
			case CYBORG_FACTORY:
			case HQ:
			case REPAIR_FACILITY:
				if (psStruct->getResistance() >= structureResistance(
                psStruct->getStats(), psStruct->getPlayer()) / 2) {
					bTarget = true;
				}
				break;
			default:
				bTarget = true;
			}
		}
		else {
			bTarget = true;
		}
	}
	return bTarget;
}

unsigned structureBodyBuilt(const Structure* psStructure)
{
	auto maxBody = structureBody(psStructure);
	if (psStructure->getState() == STRUCTURE_STATE::BEING_BUILT) {
		// Calculate the body points the structure would have, if not damaged.
		auto unbuiltBody = (maxBody + 9) / 10; // See droidStartBuild() in droid.cpp.
		auto deltaBody = static_cast<unsigned>(maxBody * 9 * structureCompletionProgress(*psStructure) / 10);
		// See structureBuild() in structure.cpp.
		maxBody = unbuiltBody + deltaBody;
	}
	return maxBody;
}

/*Access functions for the upgradeable stats of a structure*/
unsigned structureBody(const Structure* psStructure)
{
	return psStructure->getStats()->upgraded_stats[psStructure->getPlayer()].hitPoints;
}

unsigned structureResistance(const StructureStats* psStats, uint8_t player)
{
	return psStats->upgraded_stats[player].resistance;
}


/*gives the attacking player a reward based on the type of structure that has
been attacked*/
bool electronicReward(Structure* psStructure, uint8_t attackPlayer)
{
	if (!bMultiPlayer) {
		return false; //campaign should not give rewards (especially to the player)
	}

	ASSERT_OR_RETURN(false, attackPlayer < MAX_PLAYERS, "Invalid player id %d", (int)attackPlayer);

	bool bRewarded = false;

  using enum STRUCTURE_TYPE;
	switch (psStructure->getStats()->type) {
	case RESEARCH:
		researchReward(psStructure->getPlayer(), attackPlayer);
		bRewarded = true;
		break;
	case FACTORY:
	case VTOL_FACTORY:
	case CYBORG_FACTORY:
		factoryReward(psStructure->getPlayer(), attackPlayer);
		bRewarded = true;
		break;
	case HQ:
		hqReward(psStructure->getPlayer(), attackPlayer);
		if (attackPlayer == selectedPlayer) {
			addConsoleMessage(_("Electronic Reward - Visibility Report"), CONSOLE_TEXT_JUSTIFICATION::DEFAULT, SYSTEM_MESSAGE);
		}
		bRewarded = true;
		break;
	case REPAIR_FACILITY:
		repairFacilityReward(psStructure->getPlayer(), attackPlayer);
		bRewarded = true;
		break;
	default:
		bRewarded = false;
	}

	return bRewarded;
}

/*find the 'best' prop/body/weapon component the losing player has and
'give' it to the reward player*/
void factoryReward(uint8_t losingPlayer, uint8_t rewardPlayer)
{
	unsigned comp = 0;

	ASSERT_OR_RETURN(, losingPlayer < MAX_PLAYERS, "Invalid losingPlayer id %d", (int)losingPlayer);
	ASSERT_OR_RETURN(, rewardPlayer < MAX_PLAYERS, "Invalid rewardPlayer id %d", (int)rewardPlayer);

	//search through the propulsions first
	for (unsigned inc = 0; inc < numPropulsionStats; inc++)
	{
		if (apCompLists[losingPlayer][COMP_PROPULSION][inc] == AVAILABLE &&
			apCompLists[rewardPlayer][COMP_PROPULSION][inc] != AVAILABLE) {
			if (asPropulsionStats[inc].buildPower > asPropulsionStats[comp].buildPower) {
				comp = inc;
			}
		}
	}
	if (comp != 0) {
		apCompLists[rewardPlayer][COMP_PROPULSION][comp] = AVAILABLE;
		if (rewardPlayer == selectedPlayer) {
			console("%s :- %s", _("Factory Reward - Propulsion"), getStatsName(&asPropulsionStats[comp]));
		}
		return;
	}

	//haven't found a propulsion - look for a body
	for (unsigned inc = 0; inc < numBodyStats; inc++)
	{
		if (apCompLists[losingPlayer][COMP_BODY][inc] == AVAILABLE &&
			apCompLists[rewardPlayer][COMP_BODY][inc] != AVAILABLE) {
			if (asBodyStats[inc].buildPower > asBodyStats[comp].buildPower) {
				comp = inc;
			}
		}
	}
	if (comp != 0) {
		apCompLists[rewardPlayer][COMP_BODY][comp] = AVAILABLE;
		if (rewardPlayer == selectedPlayer) {
			console("%s :- %s", _("Factory Reward - Body"), getStatsName(&asBodyStats[comp]));
		}
		return;
	}

	//haven't found a body - look for a weapon
	for (unsigned inc = 0; inc < numWeaponStats; inc++)
	{
		if (apCompLists[losingPlayer][COMP_WEAPON][inc] == AVAILABLE &&
			apCompLists[rewardPlayer][COMP_WEAPON][inc] != AVAILABLE) {
			if (asWeaponStats[inc].buildPower > asWeaponStats[comp].buildPower) {
				comp = inc;
			}
		}
	}
	if (comp != 0) {
		apCompLists[rewardPlayer][COMP_WEAPON][comp] = AVAILABLE;
		if (rewardPlayer == selectedPlayer) {
			console("%s :- %s", _("Factory Reward - Weapon"), getStatsName(&asWeaponStats[comp]));
		}
		return;
	}

	//losing Player hasn't got anything better so don't gain anything!
	if (rewardPlayer == selectedPlayer) {
		addConsoleMessage(_("Factory Reward - Nothing"), CONSOLE_TEXT_JUSTIFICATION::DEFAULT, SYSTEM_MESSAGE);
	}
}

/*find the 'best' repair component the losing player has and
'give' it to the reward player*/
void repairFacilityReward(uint8_t losingPlayer, uint8_t rewardPlayer)
{
	unsigned comp = 0;

	ASSERT_OR_RETURN(, losingPlayer < MAX_PLAYERS, "Invalid losingPlayer id %d", (int)losingPlayer);
	ASSERT_OR_RETURN(, rewardPlayer < MAX_PLAYERS, "Invalid rewardPlayer id %d", (int)rewardPlayer);

	//search through the repair stats
	for (unsigned inc = 0; inc < numRepairStats; inc++)
	{
		if (apCompLists[losingPlayer][COMP_REPAIRUNIT][inc] == AVAILABLE &&
			apCompLists[rewardPlayer][COMP_REPAIRUNIT][inc] != AVAILABLE) {
			if (asRepairStats[inc].buildPower > asRepairStats[comp].buildPower) {
				comp = inc;
			}
		}
	}
	if (comp != 0) {
		apCompLists[rewardPlayer][COMP_REPAIRUNIT][comp] = AVAILABLE;
		if (rewardPlayer == selectedPlayer) {
			console("%s :- %s", _("Repair Facility Award - Repair"), getStatsName(&asRepairStats[comp]));
		}
		return;
	}
	if (rewardPlayer == selectedPlayer) {
		addConsoleMessage(_("Repair Facility Award - Nothing"), CONSOLE_TEXT_JUSTIFICATION::DEFAULT, SYSTEM_MESSAGE);
	}
}

/*makes the losing players tiles/structures/features visible to the reward player*/
void hqReward(uint8_t losingPlayer, uint8_t rewardPlayer)
{
	ASSERT_OR_RETURN(, losingPlayer < MAX_PLAYERS && rewardPlayer < MAX_PLAYERS,
	                   "losingPlayer (%" PRIu8 "), rewardPlayer (%" PRIu8 ") must both be < MAXPLAYERS", losingPlayer,
	                   rewardPlayer);

	// share exploration info - pretty useless but perhaps a nice touch?
	for (int y = 0; y < mapHeight; ++y)
	{
		for (int x = 0; x < mapWidth; ++x)
		{
			Tile* psTile = mapTile(x, y);
			if (TEST_TILE_VISIBLE(losingPlayer, psTile))
			{
				psTile->tileExploredBits |= alliancebits[rewardPlayer];
			}
		}
	}

	//struct
	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		for (auto psStruct : apsStructLists[i])
		{
			if (psStruct->visible[losingPlayer] && !psStruct->died)
			{
				psStruct->visible[rewardPlayer] = psStruct->visible[losingPlayer];
			}
		}

		//feature
		for (Feature* psFeat = apsFeatureLists[i]; psFeat != nullptr; psFeat = psFeat->psNext)
		{
			if (psFeat->visible[losingPlayer])
			{
				psFeat->visible[rewardPlayer] = psFeat->visible[losingPlayer];
			}
		}

		//droids.
		for (auto& psDroid : apsDroidLists[i])
		{
			if (psDroid.visible[losingPlayer] || psDroid.getPlayer() == losingPlayer)
			{
				psDroid.visible[rewardPlayer] = UINT8_MAX;
			}
		}
	}
}

// Return true if structure is a factory of any type.
//
//bool StructIsFactory(const STRUCTURE* Struct)
//{
//	ASSERT_OR_RETURN(false, Struct != nullptr, "Invalid structure!");
//	ASSERT_OR_RETURN(false, Struct->pStructureType != nullptr, "Invalid structureType!");
//
//	return Struct->type == OBJ_STRUCTURE && (
//		Struct->pStructureType->type == REF_FACTORY ||
//		Struct->pStructureType->type == REF_CYBORG_FACTORY ||
//		Struct->pStructureType->type == REF_VTOL_FACTORY);
//}

// Return true if flag is a delivery point for a factory.
//
bool FlagIsFactory(const FlagPosition* psCurrFlag)
{
	if ((psCurrFlag->factoryType == FACTORY_FLAG) ||
      (psCurrFlag->factoryType == CYBORG_FLAG) ||
		  (psCurrFlag->factoryType == VTOL_FLAG)) {
		return true;
	}

	return false;
}

// Find a structure's delivery point , only if it's a factory.
// Returns NULL if not found or the structure isn't a factory.
//
FlagPosition* Factory::FindFactoryDelivery() const
{
	if (StructIsFactory(this)) {
		// find the factory's delivery point.
		for (auto psCurrFlag : apsFlagPosLists[getPlayer()])
		{
			if (FlagIsFactory(psCurrFlag)
				&& psAssemblyPoint->factoryInc == psCurrFlag->factoryInc
				&& psAssemblyPoint->factoryType == psCurrFlag->factoryType) {
				return psCurrFlag;
			}
		}
	}
	return nullptr;
}

//Find the factory associated with the delivery point - returns NULL if none exist
Structure* findDeliveryFactory(FlagPosition* psDelPoint)
{
	Factory* psFactory;
	RepairFacility* psRepair;

	for (auto psCurr : apsStructLists[psDelPoint->player])
	{
		if (StructIsFactory(psCurr)) {
			psFactory = dynamic_cast<Factory*>(psCurr);
			if (psFactory->psAssemblyPoint->factoryInc == psDelPoint->factoryInc &&
	  			psFactory->psAssemblyPoint->factoryType == psDelPoint->factoryType) {
				return psCurr;
			}
		}
		else if (psCurr->getStats()->type == STRUCTURE_TYPE::REPAIR_FACILITY) {
			psRepair = &psCurr->pFunctionality->repairFacility;
			if (psRepair->psDeliveryPoint.get() == psDelPoint) {
				return psCurr;
			}
		}
	}
	return nullptr;
}

/*cancels the production run for the factory and returns any power that was
accrued but not used*/
void Factory::cancelProduction(QUEUE_MODE mode, bool mayClearProductionRun)
{
	ASSERT_OR_RETURN(, StructIsFactory(this), "structure not a factory");

	if (getPlayer() == productionPlayer && mayClearProductionRun) {
		//clear the production run for this factory
		if (psAssemblyPoint->factoryInc < asProductionRun[psAssemblyPoint->factoryType].size()) {
			asProductionRun[psAssemblyPoint->factoryType][psAssemblyPoint->factoryInc].clear();
		}
		productionLoops = 0;
	}

	if (mode == ModeQueue) {
		sendStructureInfo(this, STRUCTUREINFO_CANCELPRODUCTION, nullptr);
		setStatusPendingCancel(*this);
		return;
	}

	//clear the factory's subject
  refundBuildPower();
	psSubject = nullptr;
	delPowerRequest(this);
}

/*set a factory's production run to hold*/
void Factory::holdProduction(QUEUE_MODE mode)
{
	if (mode == ModeQueue) {
		sendStructureInfo(this,
                      STRUCTUREINFO_HOLDPRODUCTION, nullptr);
		setStatusPendingHold(*this);
		return;
	}

	if (psSubject) {
		//set the time the factory was put on hold
		timeStartHold = gameTime;
		//play audio to indicate on hold
		if (getPlayer() == selectedPlayer) {
			audio_PlayTrack(ID_SOUND_WINDOWCLOSE);
		}
	}
	delPowerRequest(this);
}

/*release a factory's production run from hold*/
void Factory::releaseProduction(QUEUE_MODE mode)
{
  if (!pimpl) return;
	if (mode == ModeQueue) {
		sendStructureInfo(this,
                      STRUCTUREINFO_RELEASEPRODUCTION, nullptr);
		setStatusPendingRelease(*this);
		return;
	}

	if (pimpl->psSubject && pimpl->timeStartHold) {
		//adjust the start time for the current subject
		if (pimpl->timeStarted != ACTION_START_TIME) {
			pimpl->timeStarted += (gameTime - pimpl->timeStartHold);
		}
		pimpl->timeStartHold = 0;
	}
}

void doNextProduction(Structure* psStructure, DroidTemplate* current, QUEUE_MODE mode)
{
	auto psNextTemplate = factoryProdUpdate(psStructure, current);

	if (psNextTemplate) {
		structSetManufacture(psStructure, psNextTemplate, ModeQueue);
		// ModeQueue instead of mode, since production lists aren't currently synchronised.
	}
	else {
		cancelProduction(psStructure, mode);
	}
}

/*this is called when a factory produces a droid. The Template returned is the next
one to build - if any*/
DroidTemplate* Factory::factoryProdUpdate(DroidTemplate* psTemplate)
{
	if (!pimpl || getPlayer() != productionPlayer) {
		return nullptr; // Production lists not currently synchronised.
	}

	if (pimpl->psAssemblyPoint->factoryInc >= asProductionRun[pimpl->psAssemblyPoint->factoryType].size()) {
		return nullptr; // Don't even have a production list.
	}
	auto productionRun = asProductionRun[pimpl->psAssemblyPoint->factoryType][pimpl->psAssemblyPoint->factoryInc];

	if (psTemplate != nullptr) {
		// find the entry in the array for this template
		auto entry = std::find(productionRun.begin(), productionRun.end(), psTemplate);
		if (entry != productionRun.end()) {
			entry->built = std::min(entry->built + 1, entry->quantity);
			if (!entry->isComplete()) {
				return psTemplate; // Build another of the same type.
			}
			if (pimpl->productionLoops == 0) {
				productionRun.erase(entry);
			}
		}
	}
	//find the next template to build - this just looks for the first uncompleted run
	for (unsigned inc = 0; inc < productionRun.size(); ++inc)
	{
		if (!productionRun[inc].isComplete())
		{
			return productionRun[inc].psTemplate;
		}
	}
	// Check that we aren't looping doing nothing.
	if (productionRun.empty())
	{
		if (pimpl->productionLoops != INFINITE_PRODUCTION) {
			pimpl->productionLoops = 0; // Reset number of loops, unless set to infinite.
		}
	}
	else if (pimpl->productionLoops != 0)
	//If you've got here there's nothing left to build unless factory is on loop production
	{
		//reduce the loop count if not infinite
		if (pimpl->productionLoops != INFINITE_PRODUCTION) {
			pimpl->productionLoops--;
		}

		//need to reset the quantity built for each entry in the production list
		std::for_each(productionRun.begin(), productionRun.end(), std::mem_fn(&ProductionRunEntry::restart));

		//get the first to build again
		return productionRun[0].psTemplate;
	}
	//if got to here then nothing left to produce so clear the array
	productionRun.clear();
	return nullptr;
}

//adjust the production run for this template type
void Factory::factoryProdAdjust(DroidTemplate* psTemplate, bool add)
{
  if (!pimpl) return;
	ASSERT_OR_RETURN(, getPlayer() == productionPlayer, "called for incorrect player");
	ASSERT_OR_RETURN(, psTemplate != nullptr, "NULL template");

	if (pimpl->psAssemblyPoint->factoryInc >= asProductionRun[pimpl->psAssemblyPoint->factoryType].size()) {
		asProductionRun[pimpl->psAssemblyPoint->factoryType].resize(pimpl->psAssemblyPoint->factoryInc + 1);
		// Don't have a production list, create it.
	}
	ProductionRun& productionRun = asProductionRun[pimpl->psAssemblyPoint->factoryType][pimpl->psAssemblyPoint->factoryInc];

	//see if the template is already in the list
	auto entry = std::find(productionRun.begin(), productionRun.end(), psTemplate);

	if (entry != productionRun.end()) {
		if (pimpl->productionLoops == 0) {
			entry->removeComplete();
			// We are not looping, so remove the built droids from the list, so that quantity corresponds to the displayed number.
		}

		//adjust the prod run
		entry->quantity += add ? 1 : -1;
		entry->built = std::min(entry->built, entry->quantity);

		// Allows us to queue up more units up to MAX_IN_RUN instead of ignoring how many we have built from that queue
		// check to see if user canceled all orders in queue
		if (entry->quantity <= 0 || entry->quantity > MAX_IN_RUN) {
			productionRun.erase(entry); // Entry empty, so get rid of it.
		}
	}
	else {
		//start off a new template
		ProductionRun tmplEntry;
		tmplEntry.psTemplate = psTemplate;
		tmplEntry.quantity = add ? 1 : MAX_IN_RUN; //wrap around to max value
		tmplEntry.built = 0;
		productionRun.push_back(tmplEntry);
	}
	//if nothing is allocated then the current factory may have been cancelled
	if (productionRun.empty()) {
		//must have cancelled everything - so tell the struct
		if (pimpl->productionLoops != INFINITE_PRODUCTION) {
			pimpl->productionLoops = 0; // Reset number of loops, unless set to infinite.
		}
	}

	//need to check if this was the template that was mid-production
	if (getProduction(FactoryGetTemplate(this)).numRemaining() == 0) {
		doNextProduction(this, FactoryGetTemplate(this), ModeQueue);
	}
	else if (!StructureIsManufacturingPending(this)) {
		structSetManufacture(psTemplate, ModeQueue);
	}

	if (StructureIsOnHoldPending(this)) {
		releaseProduction();
	}
}

/** checks the status of the production of a template
 */
ProductionRun Factory::getProduction(DroidTemplate* psTemplate)
{
	if (!pimpl || getPlayer() != productionPlayer ||
      psTemplate == nullptr || !StructIsFactory(this)) {
		return {}; // not producing any NULL pointers.
	}

	if (!pimpl->psAssemblyPoint || pimpl->psAssemblyPoint->factoryInc >=
        asProductionRun[pimpl->psAssemblyPoint->factoryType].size()) {
		return {}; // don't have a production list.
	}
	auto& productionRun = asProductionRun[pimpl->psAssemblyPoint->factoryType]
          [pimpl->psAssemblyPoint->factoryInc];

	// see if the template is in the list
	auto entry = std::find(productionRun.begin(), productionRun.end(), psTemplate);
	if (entry != productionRun.end()) {
		return *entry;
	}

	// not in the list so none being produced
	return {};
}

/*looks through a players production list to see how many command droids
are being built*/
unsigned checkProductionForCommand(unsigned player)
{
	auto quantity = 0;

  if (player != productionPlayer) {
    return quantity;
  }
  
  // assumes cyborg or vtol droids are not command types!
  auto factoryType = FACTORY_FLAG;

  for (unsigned factoryInc = 0; factoryInc < factoryNumFlag[player][factoryType].size(); ++factoryInc)
  {
    // check to see if there is a factory with a production run
    if (!factoryNumFlag[player][factoryType][factoryInc] || 
        factoryInc >= asProductionRun[factoryType].size()) {
      continue;
    }
    
    auto& productionRun = asProductionRun[factoryType][factoryInc];
    for (unsigned inc = 0; inc < productionRun.size(); ++inc)
    {
      if (productionRun[inc].psTemplate->type == DROID_TYPE::COMMAND) {
        quantity += productionRun[inc].numRemaining();
      }
    }
  }
  return quantity;
}

/// Count number of factories assignable to a command droid
unsigned countAssignableFactories(unsigned player, unsigned factoryType)
{
	auto quantity = 0;
	ASSERT_OR_RETURN(0, player == selectedPlayer,
                   "%s should only be called for selectedPlayer",
                   __FUNCTION__);

	if (player >= MAX_PLAYERS) {
    return 0;
  }

	for (auto&& factoryInc : factoryNumFlag[player][factoryType])
	{
		// check to see if there is a factory
		if (factoryInc) {
			quantity++;
		}
	}
	return quantity;
}

// check whether a factory of a certain number and type exists
bool checkFactoryExists(unsigned player, unsigned factoryType, unsigned inc)
{
	ASSERT_OR_RETURN(false, player < MAX_PLAYERS, "Invalid player");
	ASSERT_OR_RETURN(false, factoryType < NUM_FACTORY_TYPES, "Invalid factoryType");

	return inc < factoryNumFlag[player][factoryType].size() && factoryNumFlag[player][factoryType][inc];
}

/// Check that delivery points haven't been put down in invalid location
void checkDeliveryPoints(unsigned version)
{
	unsigned inc;
	Factory* psFactory;
	RepairFacility* psRepair;
	unsigned x, y;

	// find any factories
	for (inc = 0; inc < MAX_PLAYERS; inc++)
	{
		//don't bother checking selectedPlayer's - causes problems when try and use
		//validLocation since it finds that the DP is on itself! And validLocation
		//will have been called to put in down in the first place
		if (inc != selectedPlayer) {
			for (auto& psStruct : apsStructLists[inc])
			{
				if (StructIsFactory(psStruct)) {
					// check the DP
					psFactory = dynamic_cast<Factory*>(psStruct);

					if (psFactory->psAssemblyPoint == nullptr) {
            // need to add one
						ASSERT_OR_RETURN(, psFactory->psAssemblyPoint != nullptr, "no delivery point for factory");
					}
					else {
						setAssemblyPoint(psFactory->psAssemblyPoint, psFactory->psAssemblyPoint->
						                 coords.x, psFactory->psAssemblyPoint->coords.y, inc, true);
					}
				}
				else if (psStruct->getStats()->type == STRUCTURE_TYPE::REPAIR_FACILITY) {
					psRepair = dynamic_cast<RepairFacility*>(psStruct);
					if (!psRepair->psDeliveryPoint) //need to add one
					{
						if (version >= VERSION_19) {
							ASSERT_OR_RETURN(, psRepair->psDeliveryPoint != nullptr,
							                   "no delivery point for repair facility");
						}
						else {
							// add an assembly point
							if (!createFlagPosition(&psRepair->psDeliveryPoint, psStruct->getPlayer())) {
								ASSERT(!"can't create new delivery point for repair facility",
								       "unable to create new delivery point for repair facility");
								return;
							}
							addFlagPosition(psRepair->psDeliveryPoint.get());
							setFlagPositionInc(psStruct->pFunctionality, psStruct->getPlayer(), REPAIR_FLAG);
							//initialise the assembly point position
							x = map_coord(psStruct->getPosition().x + 256);
							y = map_coord(psStruct->getPosition().y + 256);
							// Belt and braces - shouldn't be able to build too near edge
							setAssemblyPoint(psRepair->psDeliveryPoint.get(), world_coord(x),
							                 world_coord(y), inc, true);
						}
					}
					else //check existing one
					{
						setAssemblyPoint(psRepair->psDeliveryPoint.get(), psRepair->psDeliveryPoint->coords.x,
						                 psRepair->psDeliveryPoint->coords.y, inc, true);
					}
				}
			}
		}
	}
}

//adjust the loop quantity for this factory
void Factory::factoryLoopAdjust(bool add)
{
	ASSERT_OR_RETURN(, StructIsFactory(this), "structure is not a factory");
	ASSERT_OR_RETURN(, getPlayer() == selectedPlayer, "should only be called for selectedPlayer");

	if (add) {
		// check for wrapping to infinite production
		if (productionLoops == MAX_IN_RUN) {
			productionLoops = 0;
		}
		else {
			// increment the count
      ++productionLoops;
			//check for limit - this caters for when on infinite production and want to wrap around
			if (productionLoops > MAX_IN_RUN) {
				productionLoops = INFINITE_PRODUCTION;
			}
		}
	}
	else {
		// decrement the count
		if (productionLoops == 0) {
			productionLoops = INFINITE_PRODUCTION;
		}
		else {
			--productionLoops;
		}
	}
}

/*Used for determining how much of the structure to draw as being built or demolished*/
float structHeightScale(const Structure* psStruct)
{
	return MAX(structureCompletionProgress(*psStruct), 0.05f);
}

/*compares the structure sensor type with the droid weapon type to see if the
FIRE_SUPPORT order can be assigned*/
bool structSensorDroidWeapon(const Structure* psStruct, const Droid* psDroid)
{
	//another crash when nStat is marked as 0xcd... FIXME: Why is nStat not initialized properly?
	//Added a safety check: Only units with weapons can be assigned.
	if (numWeapons(*psStruct) > 0) {
		//Standard Sensor Tower + indirect weapon droid (non VTOL)
		//else if (structStandardSensor(psStruct) && (psDroid->numWeaps &&
		if (structStandardSensor(psStruct) &&
        (!proj_Direct(psDroid->getWeapons()[0].getStats())) &&
         !psDroid->isVtol()) {
			return true;
		}
		//CB Sensor Tower + indirect weapon droid (non VTOL)
		//if (structCBSensor(psStruct) && (psDroid->numWeaps &&
		else if (structCBSensor(psStruct) && (
				!proj_Direct(psDroid->getWeapons()[0].getStats())) &&
			!psDroid->isVtol()) {
			return true;
		}
		//VTOL Intercept Sensor Tower + any weapon VTOL droid
		//else if (structVTOLSensor(psStruct) && psDroid->numWeaps &&
		else if (structVTOLSensor(psStruct) && psDroid->isVtol()) {
			return true;
		}
		//VTOL CB Sensor Tower + any weapon VTOL droid
		//else if (structVTOLCBSensor(psStruct) && psDroid->numWeaps &&
		else if (structVTOLCBSensor(psStruct) && psDroid->isVtol()) {
			return true;
		}
	}
	//case not matched
	return false;
}


///*checks if the structure has a Counter Battery sensor attached - returns
//true if it has*/
//bool structCBSensor(const STRUCTURE* psStruct)
//{
//	// Super Sensor works as any type
//	if (psStruct->pStructureType->pSensor
//		&& (psStruct->pStructureType->pSensor->type == INDIRECT_CB_SENSOR
//			|| psStruct->pStructureType->pSensor->type == SUPER_SENSOR)
//		&& psStruct->pStructureType->pSensor->location == LOC_TURRET)
//	{
//		return true;
//	}
//
//	return false;
//}


///*checks if the structure has a Standard Turret sensor attached - returns
//true if it has*/
//bool structStandardSensor(const STRUCTURE* psStruct)
//{
//	// Super Sensor works as any type
//	if (psStruct->pStructureType->pSensor
//		&& (psStruct->pStructureType->pSensor->type == STANDARD_SENSOR
//			|| psStruct->pStructureType->pSensor->type == SUPER_SENSOR)
//		&& psStruct->pStructureType->pSensor->location == LOC_TURRET)
//	{
//		return true;
//	}
//
//	return false;
//}

///*checks if the structure has a VTOL Intercept sensor attached - returns
//true if it has*/
//bool structVTOLSensor(const STRUCTURE* psStruct)
//{
//	// Super Sensor works as any type
//	if (psStruct->pStructureType->pSensor
//		&& (psStruct->pStructureType->pSensor->type == VTOL_INTERCEPT_SENSOR
//			|| psStruct->pStructureType->pSensor->type == SUPER_SENSOR)
//		&& psStruct->pStructureType->pSensor->location == LOC_TURRET)
//	{
//		return true;
//	}
//
//	return false;
//}

///*checks if the structure has a VTOL Counter Battery sensor attached - returns
//true if it has*/
//bool structVTOLCBSensor(const STRUCTURE* psStruct)
//{
//	// Super Sensor works as any type
//	if (psStruct->pStructureType->pSensor
//		&& (psStruct->pStructureType->pSensor->type == VTOL_CB_SENSOR
//			|| psStruct->pStructureType->pSensor->type == SUPER_SENSOR)
//		&& psStruct->pStructureType->pSensor->location == LOC_TURRET)
//	{
//		return true;
//	}
//
//	return false;
//}

bool RearmPad::isClear() const
{
  return psObj == nullptr ||
         vtolHappy(*psObj);
}

//// check whether a rearm pad is clear
//bool clearRearmPad(const STRUCTURE* psStruct)
//{
//	return psStruct->pStructureType->type == REF_REARM_PAD
//		&& (psStruct->pFunctionality->rearmPad.psObj == nullptr
//			|| vtolHappy((DROID*)psStruct->pFunctionality->rearmPad.psObj));
//}


//// return the nearest rearm pad
//// psTarget can be NULL
//STRUCTURE* findNearestReArmPad(DROID* psDroid, STRUCTURE* psTarget, bool bClear)
//{
//	STRUCTURE *psNearest, *psTotallyClear;
//	int xdiff, ydiff, mindist, currdist, totallyDist;
//	int cx, cy;
//
//	ASSERT_OR_RETURN(nullptr, psDroid != nullptr, "No droid was passed.");
//
//	if (psTarget != nullptr)
//	{
//		if (!vtolOnRearmPad(psTarget, psDroid))
//		{
//			return psTarget;
//		}
//		cx = (int)psTarget->pos.x;
//		cy = (int)psTarget->pos.y;
//	}
//	else
//	{
//		cx = (int)psDroid->pos.x;
//		cy = (int)psDroid->pos.y;
//	}
//	mindist = int_MAX;
//	totallyDist = int_MAX;
//	psNearest = nullptr;
//	psTotallyClear = nullptr;
//	for (STRUCTURE* psStruct = apsStructLists[psDroid->player]; psStruct; psStruct = psStruct->psNext)
//	{
//		if (psStruct->pStructureType->type == REF_REARM_PAD && (!bClear || clearRearmPad(psStruct)))
//		{
//			xdiff = (int)psStruct->pos.x - cx;
//			ydiff = (int)psStruct->pos.y - cy;
//			currdist = xdiff * xdiff + ydiff * ydiff;
//			if (bClear && !vtolOnRearmPad(psStruct, psDroid))
//			{
//				if (currdist < totallyDist)
//				{
//					totallyDist = currdist;
//					psTotallyClear = psStruct;
//				}
//			}
//			else
//			{
//				if (currdist < mindist)
//				{
//					mindist = currdist;
//					psNearest = psStruct;
//				}
//			}
//		}
//	}
//	if (bClear && (psTotallyClear != nullptr))
//	{
//		psNearest = psTotallyClear;
//	}
//	if (!psNearest)
//	{
//		objTrace(psDroid->id, "Failed to find a landing spot (%s)!", bClear ? "req clear" : "any");
//	}
//	return psNearest;
//}


// clear a rearm pad for a droid to land on it
void ensureRearmPadClear(Structure* psStruct, Droid* psDroid)
{
	const auto tx = map_coord(psStruct->getPosition().x);
	const auto ty = map_coord(psStruct->getPosition().y);

	for (auto i = 0; i < MAX_PLAYERS; i++)
	{
		if (aiCheckAlliances(psStruct->getPlayer(), i)) {
			for (auto& psCurr : apsDroidLists[i])
			{
				if (&psCurr != psDroid
					&& map_coord(psCurr.getPosition().x) == tx
					&& map_coord(psCurr.getPosition().y) == ty
					&& psCurr.isVtol()) {
					::actionDroid(&psCurr, ACTION::CLEAR_REARM_PAD, psStruct);
				}
			}
		}
	}
}

bool ProductionRun::operator ==(const DroidTemplate& rhs) const
{
  return target->id == rhs.id;
}

void ProductionRun::restart()
{
  quantity_built = 0;
}

int ProductionRun::tasks_remaining() const
{
  return quantity_to_build - quantity_built;
}

bool ProductionRun::is_valid() const
{
  return target && quantity_to_build > 0 &&
         quantity_built <= quantity_to_build;
}

bool ProductionRun::is_complete() const
{
  return tasks_remaining() == 0;
}

/// @return `true` if a `RearmPad` has a vtol on it
bool vtolOnRearmPad(Structure* psStruct, Droid* psDroid)
{
	auto tx = map_coord(psStruct->getPosition().x);
	auto ty = map_coord(psStruct->getPosition().y);
  auto droids = apsDroidLists[psStruct->getPlayer()];

  return std::any_of(droids.begin(), droids.end(),
                     [&](const auto& droid)
  {
    return &droid != psDroid &&
           map_coord(droid.getPosition().x) == tx &&
           map_coord(droid.getPosition().y) == ty;
  });
}

/* Just returns true if the structure's present body points aren't as high as the original*/
bool structIsDamaged(Structure* psStruct)
{
	return psStruct->getHp() < structureBody(psStruct);
}


/*returns the power cost to build this structure, or to add its next module */
unsigned structPowerToBuildOrAddNextModule(const Structure* psStruct)
{
	if (psStruct->getCapacity() > 0) {
		auto psStats = getModuleStat(psStruct);
		ASSERT(psStats != nullptr, "getModuleStat returned null");
		if (psStats) {
      // return the cost to build the module
			return psStats->power_cost;
		}
	}
	// no module attached so building the base structure
	return psStruct->getStats()->power_cost;
}


//for MULTIPLAYER ONLY
//this adjusts the time the relevant action started if the building is attacked by EW weapon
void resetResistanceLag(Structure* psBuilding)
{
  if (!bMultiPlayer) {
    return;
  }

  switch (psBuilding->getStats()->type) {
    using enum STRUCTURE_TYPE;
    case RESEARCH:
      break;
    case FACTORY:
    case VTOL_FACTORY:
    case CYBORG_FACTORY:
      {
        auto psFactory = dynamic_cast<Factory*>(psBuilding);

        // if working on a unit
        if (psFactory->psSubject) {
          // adjust the start time for the current subject
          if (psFactory->timeStarted != ACTION_START_TIME) {
            psFactory->timeStarted += (gameTime - psBuilding->lastResistance);
          }
        }
      }
    default: //do nothing
      break;
  }
}

/*checks the structure passed in is a Las Sat structure which is currently
selected - returns true if valid*/
bool lasSatStructSelected(const Structure* psStruct)
{
	if ((psStruct->isSelected || (bMultiPlayer &&
      !isHumanPlayer(psStruct->getPlayer()))) &&
      (psStruct->getWeapons()[0].getStats()->weaponSubClass == WEAPON_SUBCLASS::LAS_SAT)) {
		return true;
	}

	return false;
}

/* Call CALL_NEWDROID script callback */
void cbNewDroid(Structure* psFactory, Droid* psDroid)
{
	ASSERT_OR_RETURN(, psDroid != nullptr, "no droid assigned for CALL_NEWDROID callback");
	triggerEventDroidBuilt(psDroid, psFactory);
}

StructureBounds getStructureBounds(const Structure* object)
{
	const auto size = object->getSize();
	const auto map = map_coord(object->getPosition().xy()) - size / 2;
	return {map, size};
}

StructureBounds getStructureBounds(const StructureStats* stats, Vector2i pos, uint16_t direction)
{
	const auto size = stats->size(direction);
	const auto map = map_coord(pos) - size / 2;
	return {map, size};
}

void checkStructure(const Structure* psStructure, const char* const location_description, const char* function,
                    const int recurse)
{
	if (recurse < 0) {
		return;
	}

	ASSERT_HELPER(psStructure != nullptr, location_description, function, "CHECK_STRUCTURE: NULL pointer");
	ASSERT_HELPER(psStructure->getId() != 0, location_description, function, "CHECK_STRUCTURE: Structure with ID 0");
	ASSERT_HELPER(dynamic_cast<const Structure*>(psStructure), location_description, function,
	              "CHECK_STRUCTURE: No structure (type num %u)", (unsigned)psStructure->type);
	ASSERT_HELPER(psStructure->getPlayer() < MAX_PLAYERS, location_description, function,
	              "CHECK_STRUCTURE: Out of bound player num (%u)", (unsigned)psStructure->getPlayer());
	ASSERT_HELPER(psStructure->getStats()->type < NUM_DIFF_BUILDINGS, location_description, function,
	              "CHECK_STRUCTURE: Out of bound structure type (%u)", (unsigned)psStructure->getStats()->type);
	ASSERT_HELPER(numWeapons(*psStructure) <= MAX_WEAPONS, location_description, function,
	              "CHECK_STRUCTURE: Out of bound weapon count (%u)", (unsigned)numWeapons(*psStructure));

	for (auto i = 0; i < psStructure->getWeapons().size(); ++i)
	{
		if (psStructure->getTarget(i)) {
			checkObject(psStructure->getTarget(i), location_description, function, recurse - 1);
		}
	}
}

static void parseFavoriteStructs()
{
	for (unsigned i = 0; i < numStructureStats; ++i)
	{
		if (favoriteStructs.contains(asStructureStats[i].id))	{
			asStructureStats[i].is_favourite = true;
		}
		else {
			asStructureStats[i].is_favourite = false;
		}
	}
}

static void packFavoriteStructs()
{
	favoriteStructs = "";
	bool first = true;

	for (unsigned i = 0; i < numStructureStats; ++i)
	{
    if (!asStructureStats[i].is_favourite) {
      continue;
    }

    if (asStructureStats[i].id.isEmpty()) {
      ASSERT(false, "Invalid struct stats - empty id");
      continue;
    }

    if (first) {
      first = false;
    }

    else {
      favoriteStructs += ",";
    }
    favoriteStructs += asStructureStats[i].id;
  }
}

WzString getFavoriteStructs()
{
	return favoriteStructs;
}

void setFavoriteStructs(WzString list)
{
	favoriteStructs = std::move(list);
}

// This follows the logic in droid.cpp nextModuleToBuild()
bool canStructureHaveAModuleAdded(const Structure* const structure)
{
	if (!structure ||
      structure->getState() != STRUCTURE_STATE::BUILT) {
		return false;
	}

	switch (structure->getStats()->type) {
    using enum STRUCTURE_TYPE;
	  case FACTORY:
	  case CYBORG_FACTORY:
	  case VTOL_FACTORY:
	  	return structure->getCapacity() < NUM_FACTORY_MODULES;
	  case POWER_GEN:
	  case RESEARCH:
	  	return structure->getCapacity() == 0;
	  default:
	  	return false;
	}
}

LineBuild calcLineBuild(Vector2i size, STRUCTURE_TYPE type, Vector2i worldPos, Vector2i worldPos2)
{
	ASSERT_OR_RETURN({}, size.x > 0 && size.y > 0, "Zero-size building");

	bool packed = type == STRUCTURE_TYPE::RESOURCE_EXTRACTOR ||
                baseStructureTypePackability(type) <= PACKABILITY_DEFENSE;

	Vector2i tile {TILE_UNITS, TILE_UNITS};
	auto padding = packed ? Vector2i{0, 0} : Vector2i{1, 1};
	auto paddedSize = size + padding;
	auto worldSize = world_coord(size);
	auto worldPaddedSize = world_coord(paddedSize);

	LineBuild lb;
	lb.begin = round_to_nearest_tile(worldPos - worldSize / 2) + worldSize / 2;

	auto delta = worldPos2 - lb.begin;
	auto count = (abs(delta) + worldPaddedSize / 2) / paddedSize + tile;
	lb.count = map_coord(std::max(count.x, count.y));
	if (lb.count <= 1) {
		lb.step = {0, 0};
	}
	else if (count.x > count.y) {
		lb.step.x = delta.x < 0 ? -worldPaddedSize.x : worldPaddedSize.x;
		lb.step.y = round_to_nearest_tile(delta.y / (lb.count - 1));
	}
	else {
		lb.step.x = round_to_nearest_tile(delta.x / (lb.count - 1));
		lb.step.y = delta.y < 0 ? -worldPaddedSize.y : worldPaddedSize.y;
	}
	return lb;
}

LineBuild calcLineBuild(StructureStats const* stats, uint16_t direction, Vector2i pos, Vector2i pos2)
{
	return calcLineBuild(stats->size(direction), stats->type, pos, pos2);
}
