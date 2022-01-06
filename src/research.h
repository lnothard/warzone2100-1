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
 * @file
 * Structures required for research stats
 */

#ifndef __INCLUDED_SRC_RESEARCH_H__
#define __INCLUDED_SRC_RESEARCH_H__

struct VIEWDATA;

static constexpr auto NO_RESEARCH_ICON = 0;

/// Max 'research complete' console message length
static constexpr auto MAX_RESEARCH_MSG_SIZE = 200;

static constexpr auto  STARTED_RESEARCH = 0x01; // research in progress
static constexpr auto  CANCELLED_RESEARCH = 0x02; // research has been canceled
static constexpr auto  RESEARCHED = 0x04; // research is complete
static constexpr auto  CANCELLED_RESEARCH_PENDING = 0x08; // research almost cancelled, waiting for GAME_RESEARCHSTATUS message to be processed.
static constexpr auto  STARTED_RESEARCH_PENDING = 0x10; // research almost in progress, waiting for GAME_RESEARCHSTATUS message to be processed.
static constexpr auto  RESEARCH_IMPOSSIBLE = 0x00; // research is (temporarily) not possible
static constexpr auto  RESEARCH_POSSIBLE = 0x01; // research is possible
static constexpr auto  RESEARCH_DISABLED = 0x02; // research is disabled (e.g. most VTOL research in no-VTOL games)

#define RESBITS (STARTED_RESEARCH|CANCELLED_RESEARCH|RESEARCHED)
#define RESBITS_PENDING_ONLY (STARTED_RESEARCH_PENDING|CANCELLED_RESEARCH_PENDING)
#define RESBITS_PENDING (RESBITS|RESBITS_PENDING_ONLY)


struct VIEWDATA;

/// Used for loading in the research stats into the appropriate list
enum
{
    REQ_LIST,
    RED_LIST,
    RES_LIST
};

enum
{
    RID_ROCKET,
    RID_CANNON,
    RID_HOVERCRAFT,
    RID_ECM,
    RID_PLASCRETE,
    RID_TRACKS,
    RID_DROIDTECH,
    RID_WEAPONTECH,
    RID_COMPUTERTECH,
    RID_POWERTECH,
    RID_SYSTEMTECH,
    RID_STRUCTURETECH,
    RID_CYBORGTECH,
    RID_DEFENCE,
    RID_QUESTIONMARK,
    RID_GRPACC,
    RID_GRPUPG,
    RID_GRPREP,
    RID_GRPROF,
    RID_GRPDAM,
    RID_MAXRID
};

enum class BODY_CLASS
{
    TANK,
    CYBORG
};

enum class TECH_CODE
{
    MAJOR,
    MINOR,
};

struct RES_COMP_REPLACEMENT
{
    ComponentStats* pOldComponent;
    ComponentStats* pNewComponent;
};

// Per-player statistics about research upgrades
struct PlayerUpgradeCounts
{
    std::unordered_map<std::string, unsigned> numBodyClassArmourUpgrades;
    std::unordered_map<std::string, unsigned> numBodyClassThermalUpgrades;
    std::unordered_map<std::string, unsigned> numWeaponImpactClassUpgrades;

    // Helper functions
    unsigned getNumWeaponImpactClassUpgrades(WEAPON_SUBCLASS subClass);
    unsigned getNumBodyClassArmourUpgrades(BODY_CLASS bodyClass);
    unsigned getNumBodyClassThermalArmourUpgrades(BODY_CLASS bodyClass);
};

struct ResearchStats : public BaseStats
{
    ResearchStats() = default;

    uint8_t techCode;

    /// Subgroup of the item - an iconID from 'Framer' to depict in the button
    unsigned subGroup;

    unsigned researchPointsRequired;
    unsigned powerCost;
    uint8_t keyTopic; /* Flag to indicate whether in single player
										   this topic must be explicitly enabled*/
    /// Flags when to disable tech
    uint8_t disabledWhen;

    ///<List of research pre-requisites
    std::vector<unsigned> pPRList;
    std::vector<unsigned> pStructList; /// List of structures that when built would enable this research
    std::vector<unsigned> pRedStructs; /// List of Structures that become redundant
    std::vector<ComponentStats*> pRedArtefacts; /// List of Artefacts that become redundant
    std::vector<unsigned> pStructureResults; /// List of Structures that are possible after this research
    std::vector<ComponentStats*> componentResults; /// List of Components that are possible after this research
    std::vector<RES_COMP_REPLACEMENT> componentReplacement;

    /// List of Components that are automatically replaced with new ones after research
    nlohmann::json results; ///< Research upgrades

    unsigned iconID = 0; /* the ID from 'Framer' for which graphic to draw in interface*/
    std::unique_ptr<VIEWDATA> pViewData  = nullptr; ///< Data used to display a message in the Intelligence Screen
    std::unique_ptr<BaseStats> psStat = nullptr; /* A stat used to define which graphic is drawn instead of the two fields below */
    std::unique_ptr<iIMDShape> pIMD = nullptr; /* the IMD to draw for this research topic */
    std::unique_ptr<iIMDShape> pIMD2 = nullptr; /* the 2nd IMD for base plates/turrets*/
    int index; ///< Unique index for this research, set incrementally
};

struct PlayerResearch
{
    // If the research has been suspended then this value contains the
    // number of points generated at the suspension/cancel point.
    unsigned currentPoints;

    /// Bit flags
    uint8_t researchStatus;

    /// Is the research possible -- so we can enable topics via scripts
    uint8_t possible;
};

struct AllyResearch
{
    unsigned player;
    int completion;
    int powerNeeded;
    int timeToResearch;
    bool active;
};

class CycleDetection
{
public:
    CycleDetection() = default;

    nonstd::optional< std::deque<ResearchStats*> > explore(ResearchStats* research);
    static nonstd::optional<std::deque<ResearchStats*>> detectCycle();
private:
    std::unordered_set<ResearchStats*> visited;
    std::unordered_set<ResearchStats*> exploring;
};

static bool IsResearchPossible(const PlayerResearch* research);

static bool IsResearchDisabled(const PlayerResearch* research);

static void MakeResearchPossible(PlayerResearch* research);

static void DisableResearch(PlayerResearch* research);

static int GetResearchPossible(const PlayerResearch* research);

static void SetResearchPossible(PlayerResearch* research, UBYTE possible);

static bool IsResearchCompleted(PlayerResearch const* x);

static bool IsResearchCancelled(PlayerResearch const* x);

static bool IsResearchStarted(PlayerResearch const* x);

/// Pending means not yet synchronised, so only permitted to affect the UI, not the game state.
static bool IsResearchCancelledPending(PlayerResearch const* x);

static bool IsResearchStartedPending(PlayerResearch const* x);

static void MakeResearchCompleted(PlayerResearch* x);

static void MakeResearchCancelled(PlayerResearch* x);

static void MakeResearchStarted(PlayerResearch* x);

/// Pending means not yet synchronised, so only permitted to affect the UI, not the game state.
static void MakeResearchCancelledPending(PlayerResearch* x);

static void MakeResearchStartedPending(PlayerResearch* x);

static void ResetPendingResearchStatus(PlayerResearch* x);

/// Clear all bits in the status except for the possible bit
static void ResetResearchStatus(PlayerResearch* x);

void RecursivelyDisableResearchByFlags(UBYTE flags);

/* The store for the research stats */
extern std::vector<ResearchStats> asResearch;

// List of pointers to arrays of PLAYER_RESEARCH[numResearch] for each player
extern std::vector<PlayerResearch> asPlayerResList[MAX_PLAYERS];

// Used for callbacks to say which topic was last researched
extern ResearchStats* psCBLastResearch;
extern Structure* psCBLastResStructure;
extern SDWORD CBResFacilityOwner;

/* Default level of sensor, repair and ECM */
extern UDWORD aDefaultSensor[MAX_PLAYERS];
extern UDWORD aDefaultECM[MAX_PLAYERS];
extern UDWORD aDefaultRepair[MAX_PLAYERS];

bool loadResearch(WzConfig& ini);

/*function to check what can be researched for a particular player at any one
  instant. Returns the number to research*/
std::vector<unsigned> fillResearchList(unsigned playerID,
                                      nonstd::optional<unsigned> topic,
                                      unsigned limit);

/* process the results of a completed research topic */
void researchResult(unsigned researchIndex, uint8_t player,
                    bool bDisplay, Structure* psResearchFacility,
                    bool bTrigger);

/// This just inits all the research arrays
bool ResearchShutDown();

/// This free the memory used for the research
void ResearchRelease();

/// For a given view data get the research this is related to
ResearchStats* getResearch(std::string pName);

/**
 * Sets the status of the topic to cancelled and stores the
 * current research points acquired
 */
void cancelResearch(Structure* psBuilding, QUEUE_MODE mode);

/* For a given view data get the research this is related to */
ResearchStats* getResearchForMsg(const VIEWDATA* pViewData);

/* Sets the 'possible' flag for a player's research so the topic will appear in
the research list next time the Research Facility is selected */
bool enableResearch(ResearchStats* psResearch, unsigned player);

/*find the last research topic of importance that the losing player did and
'give' the results to the reward player*/
void researchReward(uint8_t losingPlayer, uint8_t rewardPlayer);

/*check to see if any research has been completed that enables self repair*/
bool selfRepairEnabled(uint8_t player);

int mapIconToRID(unsigned iconID);

/*puts research facility on hold*/
void holdResearch(Structure* psBuilding, QUEUE_MODE mode);

/*release a research facility from hold*/
void releaseResearch(Structure* psBuilding, QUEUE_MODE mode);

void enableSelfRepair(UBYTE player);

void CancelAllResearch(unsigned pl);

bool researchInitVars();

bool researchAvailable(int inc, unsigned playerID, QUEUE_MODE mode);

std::vector<AllyResearch> const& listAllyResearch(unsigned ref);

// various counts / statistics
unsigned getNumWeaponImpactClassUpgrades(unsigned player, WEAPON_SUBCLASS subClass);

unsigned getNumBodyClassArmourUpgrades(unsigned player, BODY_CLASS bodyClass);
unsigned getNumBodyClassThermalArmourUpgrades(unsigned player, BODY_CLASS bodyClass);

#endif // __INCLUDED_SRC_RESEARCH_H__
