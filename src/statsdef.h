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
 * Definitions for the stats system.
 */
 
#ifndef __INCLUDED_STATSDEF_H__
#define __INCLUDED_STATSDEF_H__

struct iIMDShape;

#include <vector>
#include <algorithm>
#include <bitset>

#include "lib/framework/wzstring.h"

#include "droid.h"

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
	explicit BaseStats(unsigned ref = 0)
    : ref{ref}
	{
	}

	[[nodiscard]] bool hasType(StatType type) const { return (ref & STAT_MASK) == type; }

	WzString id; ///< Text id (i.e. short language-independent name)
	WzString name; ///< Full / real name of the item
	unsigned ref; ///< Unique ID of the item
	std::size_t index = 0; ///< Index into containing array
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
		unsigned hit_points = 0;
		/// Adjust final droid hit points by this percentage amount
		int hitpointPct = 100;
	};

	[[nodiscard]] virtual Upgradeable const& getBase() const = 0;
	[[nodiscard]] virtual Upgradeable const& getUpgrade(unsigned player) const = 0;
	Upgradeable& getBase() { return const_cast<Upgradeable&>(const_cast<ComponentStats const*>(this)->getBase()); }

  using enum DROID_TYPE;
	iIMDShape* pIMD = nullptr; /**< The IMD to draw for this component */
	unsigned buildPower = 0; /**< Power required to build the component */
	unsigned buildPoints = 0; /**< Time required to build the component */
	unsigned weight = 0; /**< Component's weight */
	COMPONENT_TYPE compType = COMPONENT_TYPE::COUNT;
	DROID_TYPE droidTypeOverride = ANY;
	bool designable = false; ///< Flag to indicate whether this component can be used in the design screen
};

struct PropulsionStats : public ComponentStats
{
	[[nodiscard]] Upgradeable const& getBase() const override { return base; }
	[[nodiscard]] Upgradeable const& getUpgrade(unsigned player) const override { return upgrade[player]; }

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
	} upgrade[MAX_PLAYERS], base;
};

struct SensorStats : public ComponentStats
{
	[[nodiscard]] Upgradeable const& getBase() const override { return base; }
	[[nodiscard]] Upgradeable const& getUpgrade(unsigned player) const override { return upgrade[player]; }

	iIMDShape* pMountGraphic = nullptr; ///< The turret mount to use
	unsigned location = 0; ///< specifies whether the Sensor is default or for the Turret
	SENSOR_TYPE type = SENSOR_TYPE::STANDARD; ///< used for combat

	struct : Upgradeable
	{
		unsigned range = 0;
	} upgrade[MAX_PLAYERS], base;
};

struct EcmStats : public ComponentStats
{
	[[nodiscard]] Upgradeable const& getBase() const override { return base; }
	[[nodiscard]] Upgradeable const& getUpgrade(unsigned player) const override { return upgrade[player]; }

	iIMDShape* pMountGraphic = nullptr; ///< The turret mount to use
	unsigned location = 0; ///< Specifies whether the ECM is default or for the Turret

	struct : Upgradeable
	{
		unsigned range = 0;
	} upgrade[MAX_PLAYERS], base;
};

struct RepairStats : public ComponentStats
{
	[[nodiscard]] Upgradeable const& getBase() const override { return base; }
	[[nodiscard]] Upgradeable const& getUpgrade(unsigned player) const override { return upgrade[player]; }

	iIMDShape* pMountGraphic = nullptr; ///< The turret mount to use
	unsigned location = 0; ///< Specifies whether the Repair is default or for the Turret
	unsigned time = 0; ///< Time delay for repair cycle

	struct : Upgradeable
	{
		unsigned repairPoints = 0; ///< The number of points contributed each cycle
	} upgrade[MAX_PLAYERS], base;
};

struct WeaponStats : public ComponentStats
{
	[[nodiscard]] Upgradeable const& getBase() const override { return base; }
	[[nodiscard]] Upgradeable const& getUpgrade(unsigned player) const override { return upgrade[player]; }

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
	} base, upgrade[MAX_PLAYERS];

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
	iIMDShape* pMountGraphic = nullptr; ///< The turret mount to use
	iIMDShape* pMuzzleGraphic = nullptr; ///< The muzzle flash
	iIMDShape* pInFlightGraphic = nullptr; ///< The ammo in flight
	iIMDShape* pTargetHitGraphic = nullptr; ///< The ammo hitting a target
	iIMDShape* pTargetMissGraphic = nullptr; ///< The ammo missing a target
	iIMDShape* pWaterHitGraphic = nullptr; ///< The ammo hitting water
	iIMDShape* pTrailGraphic = nullptr; ///< The trail used for in flight

	/* Audio */
	int iAudioFireID = 0;
	int iAudioImpactID = 0;
};

struct ConstructStats : public ComponentStats
{
	[[nodiscard]] Upgradeable const& getBase() const override { return base; }
	[[nodiscard]] Upgradeable const& getUpgrade(unsigned player) const override { return upgrade[player]; }

	iIMDShape* pMountGraphic = nullptr; ///< The turret mount to use

	struct : Upgradeable
	{
		unsigned constructPoints; ///< The number of points contributed each cycle
	} upgrade[MAX_PLAYERS], base;
};

struct CommanderStats : public ComponentStats
{
	[[nodiscard]] Upgradeable const& getBase() const override { return base; }
	[[nodiscard]] Upgradeable const& getUpgrade(unsigned player) const override { return upgrade[player]; }

	WeaponStats* psWeaponStat = nullptr; ///< weapon stats associated with this brain - for Command Droids

	struct : Upgradeable
	{
		std::vector<int> rankThresholds;
		int maxDroids = 0; ///< base maximum number of droids that the commander can control
		int maxDroidsMult = 0; ///< maximum number of controlled droids multiplied by level
	} upgrade[MAX_PLAYERS], base;

	std::vector<std::string> rankNames;
};

#define SHOOT_ON_GROUND 0x01
#define SHOOT_IN_AIR	0x02

struct BodyStats : public ComponentStats
{
	[[nodiscard]] Upgradeable const& getBase() const override { return base; }
	[[nodiscard]] Upgradeable const& getUpgrade(unsigned player) const override { return upgrade[player]; }

	BODY_SIZE size = BODY_SIZE::COUNT; ///< How big the body is - affects how hit
	unsigned weaponSlots = 0; ///< The number of weapon slots on the body

	std::vector<iIMDShape*> ppIMDList; ///< list of IMDs to use for propulsion unit - up to numPropulsionStats
	std::vector<iIMDShape*> ppMoveIMDList; ///< list of IMDs to use when droid is moving - up to numPropulsionStats
	std::vector<iIMDShape*> ppStillIMDList; ///< list of IMDs to use when droid is still - up to numPropulsionStats
	WzString bodyClass; ///< rules hint to script about its classification

	struct Upgradeable : ComponentStats::Upgradeable
	{
		unsigned power = 0; ///< this is the engine output of the body
		unsigned armour = 0; ///< A measure of how much protection the armour provides
		int thermal = 0;
		int resistance = 0;
	} upgrade[MAX_PLAYERS], base;
};

struct PROPULSION_TYPES
{
	TRAVEL_MEDIUM travel = TRAVEL_MEDIUM::GROUND; ///< Which medium the propulsion travels in
	uint16_t powerRatioMult = 0; ///< Multiplier for the calculated power ratio of the droid
	int16_t startID = 0; ///< sound to play when this prop type starts
	int16_t idleID = 0; ///< sound to play when this prop type is idle
	int16_t moveOffID = 0; ///< sound to link moveID and idleID
	int16_t moveID = 0; ///< sound to play when this prop type is moving
	int16_t hissID = 0; ///< sound to link moveID and idleID
	int16_t shutDownID = 0; ///< sound to play when this prop type shuts down
};

typedef uint16_t WEAPON_MODIFIER;

#endif // __INCLUDED_STATSDEF_H__
