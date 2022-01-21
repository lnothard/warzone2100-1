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
 * @file stats.h
 * Interface to the common stats module
 */

#ifndef __INCLUDED_SRC_STATS_H__
#define __INCLUDED_SRC_STATS_H__

#include "lib/framework/wzconfig.h"

class PlayerOwnedObject;
enum class DROID_TYPE;

static constexpr auto SHOOT_ON_GROUND = 0x01;
static constexpr auto SHOOT_IN_AIR = 0x02;

static inline bool stringToEnumSortFunction(std::pair<char const*, unsigned> const& a,
                                            std::pair<char const*, unsigned> const& b)
{
  return strcmp(a.first, b.first) < 0;
}

template <typename Enum>
struct StringToEnum
{
    explicit operator std::pair<char const*, unsigned>() const
    {
      return std::make_pair(string, value);
    }

    char const* string;
    Enum value;
};

template <typename Enum>
struct StringToEnumMap : public std::vector<std::pair<char const*, unsigned>>
{
    typedef std::vector<std::pair<char const*, unsigned>> V;

    template <int N>
    static StringToEnumMap<Enum> const& FromArray(StringToEnum<Enum> const (&map)[N])
    {
      static StringToEnum<Enum> const (&myMap)[N] = map;
      static const StringToEnumMap<Enum> sortedMap(map);
      assert(map == myMap);
      (void)myMap; // Squelch warning in release mode.
      return sortedMap;
    }

    template <int N>
    explicit StringToEnumMap(StringToEnum<Enum> const (&entries)[N]) : V(entries, entries + N)
    {
      std::sort(V::begin(), V::end(), stringToEnumSortFunction);
    }
};

enum class COMPONENT_TYPE
{
    BODY,
    BRAIN,
    PROPULSION,
    REPAIR_UNIT,
    ECM,
    SENSOR,
    CONSTRUCT,
    WEAPON,
    COUNT // MUST BE LAST
};

enum class WEAPON_FLAGS
{
    NO_FRIENDLY_FIRE,
    COUNT // MUST BE LAST
};

/// LOC used for holding locations for Sensors and ECMs
enum class LOC
{
    DEFAULT,
    TURRET,
};

enum class BODY_SIZE
{
    LIGHT,
    MEDIUM,
    HEAVY,
    SUPER_HEAVY,
    COUNT // MUST BE LAST
};

enum class WEAPON_SIZE
{
    LIGHT,
    HEAVY,
    ANY,
    COUNT // MUST BE LAST
};

/// Basic weapon type
enum class WEAPON_CLASS
{
    /// E.g., bullets
    KINETIC,

    /// E.g., lasers
    HEAT,
    COUNT // MUST BE LAST
};

/**
 * Weapon subclass used to define which weapons are affected 
 * by weapon upgrade functions
 */
enum class WEAPON_SUBCLASS
{
    MACHINE_GUN,
    CANNON,
    MORTARS,
    MISSILE,
    ROCKET,
    ENERGY,
    GAUSS,
    FLAME,
    HOWITZERS,
    ELECTRONIC,
    AA_GUN,
    SLOW_MISSILE,
    SLOW_ROCKET,
    LAS_SAT,
    BOMB,
    COMMAND,
    EMP,
    COUNT // MUST BE LAST
};

// Used to define which projectile model to use for the weapon
enum class MOVEMENT_MODEL
{
    DIRECT,
    INDIRECT,
    HOMING_DIRECT,
    HOMING_INDIRECT
};

/**
 * Used to modify the damage to a propulsion type (or structure) 
 * based on weapon type
 */
enum class WEAPON_EFFECT
{
    ANTI_PERSONNEL,
    ANTI_TANK,
    BUNKER_BUSTER,
    ARTILLERY_ROUND,
    FLAMER,
    ANTI_AIRCRAFT,
    COUNT // MUST BE LAST
};

/// Defines the left and right sides for propulsion IMDs
enum class PROP_SIDE
{
    LEFT,
    RIGHT,
    COUNT // MUST BE LAST
};

enum class PROPULSION_TYPE
{
    WHEELED,
    TRACKED,
    LEGGED,
    HOVER,
    LIFT,
    PROPELLOR,
    HALF_TRACKED,
    COUNT // MUST BE LAST
};

// CB = Counter Battery
enum class SENSOR_TYPE
{
    STANDARD,
    INDIRECT_CB,
    VTOL_CB,
    VTOL_INTERCEPT,
    SUPER,

    /// Works as all of the above together - new for updates
    RADAR_DETECTOR,
};

enum class TRAVEL_MEDIUM
{
    GROUND,
    AIR
};

/* Elements common to all stats structures */

// What number the ref numbers start at for each type of stat
enum StatType
{
    STAT_BODY = 0x010000,
    STAT_BRAIN = 0x020000,
    STAT_PROPULSION = 0x040000,
    STAT_SENSOR = 0x050000,
    STAT_ECM = 0x060000,
    STAT_REPAIR = 0x080000,
    STAT_WEAPON = 0x0a0000,
    STAT_RESEARCH = 0x0b0000,
    STAT_TEMPLATE = 0x0c0000,
    STAT_STRUCTURE = 0x0d0000,
    STAT_FUNCTION = 0x0e0000,
    STAT_CONSTRUCT = 0x0f0000,
    STAT_FEATURE = 0x100000,

    /* The maximum number of refs for a type of stat */
    STAT_MASK = 0xffff0000
};

/// Stats common to all stats structs
struct BaseStats
{
    virtual ~BaseStats() = default;
    explicit BaseStats(unsigned ref = 0);

    [[nodiscard]] bool hasType(StatType type) const;

    /// Text id (i.e. short language-independent name)
    WzString id;

    /// Full/real name of the item
    WzString name;

    /// Unique ID of the item
    unsigned ref;

    /// Index into containing array
    std::size_t index = 0;
};

#define getStatsName(_psStats) ((_psStats)->name.isEmpty() ? "" : gettext((_psStats)->name.toUtf8().c_str()))
#define getID(_psStats) (_psStats)->id.toUtf8().c_str()
#define checkIfZNullStat(_psStats) ((_psStats)->id.toUtf8().find("ZNULL") != std::string::npos)

/// Stats common to all droid components
struct ComponentStats : public BaseStats
{
    ComponentStats() = default;

    struct Upgradeable
    {
        /// Number of upgradeable hit points
        unsigned hitPoints = 0;
        /// Adjust final droid hit points by this percentage amount
        int hitpointPct = 100;
    } base, upgraded[MAX_PLAYERS];

    /// The IMD to draw for this component
    std::shared_ptr<iIMDShape> pIMD;

    /// Power required to build the component
    unsigned buildPower = 0;

    /// Time required to build the component
    unsigned buildPoints = 0;

    unsigned weight = 0;
    COMPONENT_TYPE compType = COMPONENT_TYPE::COUNT;
    DROID_TYPE droidTypeOverride;

    /// `true` iff this component may be used in the design screen
    bool designable = false;
};

struct PropulsionStats : public ComponentStats
{
    PROPULSION_TYPE propulsionType = PROPULSION_TYPE::COUNT;
    unsigned maxSpeed = 0;
    unsigned turnSpeed = 0;
    unsigned spinSpeed = 0;
    unsigned spinAngle = 0;
    unsigned skidDeceleration = 0;
    unsigned deceleration = 0;
    unsigned acceleration = 0;

    struct Upgradeable : ComponentStats::Upgradeable
    {
        /// Increase hit points by this percentage of the body's hit points
        int hitpointPctOfBody = 0;
    } upgraded[MAX_PLAYERS], base;
};

struct SensorStats : public ComponentStats
{
    std::shared_ptr<iIMDShape> pMountGraphic; ///< The turret mount to use
    LOC location; ///< specifies whether the Sensor is default or for the Turret
    SENSOR_TYPE type = SENSOR_TYPE::STANDARD; ///< used for combat

    struct : Upgradeable
    {
        unsigned range = 0;
    } upgraded[MAX_PLAYERS], base;
};

struct EcmStats : public ComponentStats
{
    std::shared_ptr<iIMDShape> pMountGraphic; ///< The turret mount to use
    LOC location; ///< Specifies whether the ECM is default or for the Turret

    struct : Upgradeable
    {
        unsigned range = 0;
    } upgraded[MAX_PLAYERS], base;
};

struct RepairStats : public ComponentStats
{
    std::shared_ptr<iIMDShape> pMountGraphic; ///< The turret mount to use
    LOC location; ///< Specifies whether the Repair is default or for the Turret
    unsigned time = 0; ///< Time delay for repair cycle

    struct : Upgradeable
    {
        unsigned repairPoints = 0; ///< The number of points contributed each cycle
    } upgraded[MAX_PLAYERS], base;
};

struct WeaponStats : public ComponentStats
{
    struct : Upgradeable
    {
        unsigned shortRange = 0;
        unsigned maxRange = 0; ///< Max distance to target for long range shot
        unsigned minRange = 0; ///< Min distance to target for shot
        unsigned hitChance = 0; ///< Chance to hit at
        unsigned shortHitChance = 0;
        unsigned firePause = 0; ///< Pause between each shot
        uint8_t numRounds = 0; ///< The number of rounds per salvo
        unsigned reloadTime = 0; ///< Time to reload the round of ammo
        unsigned damage = 0;
        unsigned radius = 0; ///< Basic blast radius of weapon
        unsigned radiusDamage = 0; ///< "Splash damage"
        unsigned periodicalDamage = 0; ///< Repeat damage each second after hit
        unsigned periodicalDamageRadius = 0; ///< Repeat damage radius
        unsigned periodicalDamageTime = 0; ///< How long the round keeps damaging
        unsigned minimumDamage = 0; ///< Minimum amount of damage done, in percentage of damage
    } base, upgraded[MAX_PLAYERS];

    WEAPON_CLASS periodicalDamageWeaponClass = WEAPON_CLASS::COUNT;
    ///< Periodical damage weapon class by damage type (KINETIC, HEAT)
    WEAPON_SUBCLASS periodicalDamageWeaponSubClass = WEAPON_SUBCLASS::COUNT;
    ///< Periodical damage weapon subclass (research class)
    WEAPON_EFFECT periodicalDamageWeaponEffect = WEAPON_EFFECT::COUNT;
    ///< Periodical damage weapon effect (propulsion/body damage modifier)
    WEAPON_CLASS weaponClass = WEAPON_CLASS::COUNT; ///< the class of weapon  (KINETIC, HEAT)
    WEAPON_SUBCLASS weaponSubClass = WEAPON_SUBCLASS::COUNT; ///< the subclass to which the weapon belongs (research class)
    MOVEMENT_MODEL movementModel = MOVEMENT_MODEL::DIRECT; ///< which projectile model to use for the bullet
    WEAPON_EFFECT weaponEffect = WEAPON_EFFECT::COUNT;
    ///< which type of warhead is associated with the weapon (propulsion/body damage modifier)
    WEAPON_SIZE weaponSize = WEAPON_SIZE::COUNT; ///< eg light weapons can be put on light bodies or as sidearms
    unsigned flightSpeed = 0; ///< speed ammo travels at
    unsigned recoilValue = 0; ///< used to compare with weight to see if recoils or not
    int distanceExtensionFactor = 0; ///< max extra distance a projectile can travel if misses target
    short rotate = 0; ///< amount the weapon(turret) can rotate 0 = none
    short maxElevation = 0; ///< max amount the turret can be elevated up
    short minElevation = 0; ///< min amount the turret can be elevated down
    uint16_t effectSize = 0; ///< size of the effect 100 = normal, 50 = half etc
    short vtolAttackRuns = 0; ///< number of attack runs a VTOL droid can do with this weapon
    UBYTE facePlayer = 0; ///< flag to make the (explosion) effect face the player when drawn
    UBYTE faceInFlight = 0; ///< flag to make the inflight effect face the player when drawn
    UBYTE surfaceToAir = 0; ///< indicates how good in the air - SHOOT_ON_GROUND, SHOOT_IN_AIR or both
    bool lightWorld = false; ///< flag to indicate whether the effect lights up the world
    bool penetrate = false; ///< flag to indicate whether pentrate droid or not
    bool fireOnMove = false; ///< indicates whether the droid has to stop before firing

    std::bitset< static_cast<std::size_t>(WEAPON_FLAGS::COUNT) > flags;

    /* Graphics control stats */
    unsigned radiusLife = 0; ///< How long a blast radius is visible
    unsigned numExplosions = 0; ///< The number of explosions per shot

    /* Graphics used for the weapon */
    std::shared_ptr<iIMDShape> pMountGraphic; ///< The turret mount to use
    std::shared_ptr<iIMDShape> pMuzzleGraphic; ///< The muzzle flash
    std::shared_ptr<iIMDShape> pInFlightGraphic; ///< The ammo in flight
    std::shared_ptr<iIMDShape> pTargetHitGraphic; ///< The ammo hitting a target
    std::shared_ptr<iIMDShape> pTargetMissGraphic; ///< The ammo missing a target
    std::shared_ptr<iIMDShape> pWaterHitGraphic; ///< The ammo hitting water
    std::shared_ptr<iIMDShape> pTrailGraphic; ///< The trail used for in flight

    int iAudioFireID = 0;
    int iAudioImpactID = 0;
};

struct ConstructStats : public ComponentStats
{ 
    /// The turret mount to use
    std::shared_ptr<iIMDShape> pMountGraphic;

    struct : Upgradeable
    {
        /// The number of points contributed each cycle
        unsigned constructPoints;
    } upgraded[MAX_PLAYERS], base;
};

struct CommanderStats : public ComponentStats
{
    /// Weapon stats associated with this brain - for Command Droids
    std::shared_ptr<WeaponStats> psWeaponStat; 

    struct : Upgradeable
    {
        std::vector<int> rankThresholds;

        /// base maximum number of droids that the commander can control
        int maxDroids = 0;

        /// maximum number of controlled droids multiplied by level
        int maxDroidsMult = 0;
    } upgraded[MAX_PLAYERS], base;

    std::vector<std::string> rankNames;
};

struct BodyStats : public ComponentStats
{
    BODY_SIZE size = BODY_SIZE::COUNT;
    unsigned weaponSlots = 0;

    /// List of IMDs to use for propulsion unit - up to numPropulsionStats
    std::vector<iIMDShape> ppIMDList;

    /// List of IMDs to use when droid is moving - up to numPropulsionStats
    std::vector<iIMDShape> ppMoveIMDList;

    /// List of IMDs to use when droid is still - up to numPropulsionStats
    std::vector<iIMDShape> ppStillIMDList;

    /// Rules hint to script about its classification
    WzString bodyClass;

    struct Upgradeable : ComponentStats::Upgradeable
    {
        unsigned power = 0; ///< this is the engine output of the body
        unsigned armour = 0; ///< A measure of how much protection the armour provides
        int thermal = 0;
        int resistance = 0;
    } upgraded[MAX_PLAYERS], base;
};

struct Propulsion
{
    /// Which medium the propulsion travels through
    TRAVEL_MEDIUM travel = TRAVEL_MEDIUM::GROUND;

    /// Multiplier for the calculated power ratio of the droid
    uint16_t powerRatioMult = 0;

    /// The Sound to play when this prop type starts
    int16_t startID = 0;

    /// The sound to play when this prop type is idle
    int16_t idleID = 0;

    /// The sound to link moveID and idleID
    int16_t moveOffID = 0;

    /// The sound to play when this prop type is moving
    int16_t moveID = 0;

    /// The sound to link moveID and idleID
    int16_t hissID = 0;

    /// The sound to play when this prop type shuts down
    int16_t shutDownID = 0;
};

typedef uint16_t WEAPON_MODIFIER;

/* The stores for the different stats */
extern BodyStats* asBodyStats;
extern CommanderStats* asBrainStats;
extern PropulsionStats* asPropulsionStats;
extern SensorStats* asSensorStats;
extern EcmStats* asECMStats;
extern RepairStats* asRepairStats;
extern WeaponStats* asWeaponStats;
extern ConstructStats* asConstructStats;
extern std::vector<Propulsion> asPropulsionTypes;

//used to hold the modifiers cross refd by weapon effect and propulsion type
extern WEAPON_MODIFIER asWeaponModifier[(size_t)WEAPON_EFFECT::COUNT][(size_t)PROPULSION_TYPE::COUNT];
extern WEAPON_MODIFIER asWeaponModifierBody[(size_t)WEAPON_EFFECT::COUNT][(size_t)BODY_SIZE::COUNT];

/* The number of different stats stored */
extern unsigned numBodyStats;
extern unsigned numBrainStats;
extern unsigned numPropulsionStats;
extern unsigned numSensorStats;
extern unsigned numECMStats;
extern unsigned numRepairStats;
extern unsigned numWeaponStats;
extern unsigned numConstructStats;
extern unsigned numTerrainTypes;

//stores for each players component states - see below
extern uint8_t* apCompLists[MAX_PLAYERS][(size_t)COMPONENT_TYPE::COUNT];

//store for each players Structure states
extern uint8_t* apStructTypeLists[MAX_PLAYERS];

//Values to fill apCompLists and apStructTypeLists. Not a bitfield, values are in case that helps with savegame compatibility.
enum ItemAvailability
{
	AVAILABLE = 1,
	// This item can be used to design droids.
	UNAVAILABLE = 2,
	// The player does not know about this item.
	FOUND = 4,
	// This item has been found, but is unresearched.
	REDUNDANT = 10,
	// The player no longer needs this item.
};

/* Allocate Weapon stats */
bool statsAllocWeapons(unsigned numEntries);

/*Allocate Body stats*/
bool statsAllocBody(unsigned numEntries);

/*Allocate Brain stats*/
bool statsAllocBrain(unsigned numEntries);

/*Allocate Propulsion stats*/
bool statsAllocPropulsion(unsigned numEntries);

/*Allocate Sensor stats*/
bool statsAllocSensor(unsigned numEntries);

/*Allocate Ecm Stats*/
bool statsAllocECM(unsigned numEntries);

/*Allocate Repair Stats*/
bool statsAllocRepair(unsigned numEntries);

/*Allocate Construct Stats*/
bool statsAllocConstruct(unsigned numEntries);

/* Load stats functions */

struct StructureStats;
// Used from structure.cpp
void loadStructureStats_BaseStats(WzConfig& json, StructureStats* psStats, size_t index);
void unloadStructureStats_BaseStats(const StructureStats& psStats);

/*Load the weapon stats from the file exported from Access*/
bool loadWeaponStats(WzConfig& ini);

/*Load the body stats from the file exported from Access*/
bool loadBodyStats(WzConfig& ini);

/*Load the brain stats from the file exported from Access*/
bool loadBrainStats(WzConfig& ini);

/*Load the propulsion stats from the file exported from Access*/
bool loadPropulsionStats(WzConfig& ini);

/*Load the sensor stats from the file exported from Access*/
bool loadSensorStats(WzConfig& ini);

/*Load the ecm stats from the file exported from Access*/
bool loadECMStats(WzConfig& ini);

/*Load the repair stats from the file exported from Access*/
bool loadRepairStats(WzConfig& ini);

/*Load the construct stats from the file exported from Access*/
bool loadConstructStats(WzConfig& ini);

/*Load the Propulsion Types from the file exported from Access*/
bool loadPropulsionTypes(WzConfig& ini);

/*Load the propulsion sounds from the file exported from Access*/
bool loadPropulsionSounds(const char* pFileName);

/*Load the Terrain Table from the file exported from Access*/
bool loadTerrainTable(WzConfig& ini);

/*Load the Weapon Effect Modifiers from the file exported from Access*/
bool loadWeaponModifiers(WzConfig& ini);

/*calls the STATS_DEALLOC macro for each set of stats*/
bool statsShutDown();

unsigned getSpeedFactor(unsigned terrainType, unsigned propulsionType);

/// Get the component index for a component based on the name, verifying with type.
/// It is currently identical to getCompFromID, but may not be in the future.
int getCompFromName(COMPONENT_TYPE compType, const WzString& name);

/// This function only allows you to use the old, deprecated ID name.
int getCompFromID(COMPONENT_TYPE compType, const WzString& name);

/// Get the component pointer for a component based on the name
ComponentStats* getCompStatsFromName(const WzString& name);

/// Get the structure pointer for a structure based on the name
StructureStats* getStructStatsFromName(const WzString& name);

/// Get the base stat pointer for a stat based on the name
BaseStats* getBaseStatsFromName(const WzString& name);

/*returns the weapon sub class based on the string name passed in */
bool getWeaponSubClass(const char* subClass, WEAPON_SUBCLASS* wclass);
const char* getWeaponSubClass(WEAPON_SUBCLASS wclass);
/*sets the store to the body size based on the name passed in - returns false
if doesn't compare with any*/
bool getBodySize(const WzString& size, BODY_SIZE* pStore);

/**
 * Determines the propulsion type indicated by the @c typeName string passed
 * in.
 *
 * @param typeName  name of the propulsion type to determine the enumerated
 *                  constant for.
 * @param[out] type Will contain an enumerated constant representing the given
 *                  propulsion type, if successful (as indicated by the return
 *                  value).
 *
 * @return true if successful, false otherwise. If successful, @c *type will
 *         contain a valid propulsion type enumerator, otherwise its value will
 *         be left unchanged.
 */
bool getPropulsionType(const char* typeName, PROPULSION_TYPE* type);

/**
 * Determines the weapon effect indicated by the @c weaponEffect string passed
 * in.
 *
 * @param weaponEffect name of the weapon effect to determine the enumerated
 *                     constant for.
 * @param[out] effect  Will contain an enumerated constant representing the
 *                     given weapon effect, if successful (as indicated by the
 *                     return value).
 *
 * @return true if successful, false otherwise. If successful, @c *effect will
 *         contain a valid weapon effect enumerator, otherwise its value will
 *         be left unchanged.
 */
extern const StringToEnumMap<WEAPON_EFFECT> map_WEAPON_EFFECT;

WZ_DECL_PURE int weaponROF(const WeaponStats* psStat, unsigned player);
WZ_DECL_PURE int weaponFirePause(const WeaponStats* psStats, unsigned player);
WZ_DECL_PURE int weaponReloadTime(const WeaponStats* psStats, unsigned player);
WZ_DECL_PURE int weaponShortHit(const WeaponStats* psStats, unsigned player);
WZ_DECL_PURE int weaponLongHit(const WeaponStats* psStats, unsigned player);
WZ_DECL_PURE int weaponDamage(const WeaponStats* psStats, unsigned player);
WZ_DECL_PURE int weaponRadDamage(const WeaponStats* psStats, unsigned player);
WZ_DECL_PURE int weaponPeriodicalDamage(const WeaponStats* psStats, unsigned player);
WZ_DECL_PURE int sensorRange(const SensorStats* psStats, unsigned player);
WZ_DECL_PURE int ecmRange(const EcmStats* psStats, unsigned player);
WZ_DECL_PURE int repairPoints(const RepairStats* psStats, unsigned player);
WZ_DECL_PURE int constructorPoints(const ConstructStats* psStats, unsigned player);
WZ_DECL_PURE int bodyPower(const BodyStats* psStats, unsigned player);
WZ_DECL_PURE int bodyArmour(const BodyStats* psStats, unsigned player, WEAPON_CLASS weaponClass);

WZ_DECL_PURE bool objHasWeapon(const PlayerOwnedObject * psObj);

void statsInitVars();

bool getWeaponEffect(const WzString& weaponEffect, WEAPON_EFFECT* effect);
/*returns the weapon effect string based on the enum passed in */
std::string getWeaponEffect(WEAPON_EFFECT effect);

bool getWeaponClass(const WzString& weaponClassStr, WEAPON_CLASS* weaponClass);

/* Wrappers */

/** If object is an active radar (has sensor turret), then return a pointer to its sensor stats. If not, return NULL. */
WZ_DECL_PURE SensorStats* objActiveRadar(const PlayerOwnedObject * psObj);

/** Returns whether object has a radar detector sensor. */
WZ_DECL_PURE bool objRadarDetector(const PlayerOwnedObject * psObj);

#endif // __INCLUDED_SRC_STATS_H__
