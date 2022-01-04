//
// Created by luna on 08/12/2021.
//

#ifndef WARZONE2100_STATS_H
#define WARZONE2100_STATS_H

#include <memory>

#include "lib/framework/frame.h"
#include "lib/ivis_opengl/ivisdef.h"

#include "weapon.h"

/// Bit mask for weaponized, flying droids
static constexpr auto SHOOT_IN_AIR = 0x02;

/// Bit mask for weaponized, grounded droids
static constexpr auto SHOOT_ON_GROUND = 0x01;

/// The movement mechanics assigned to a droid
enum class PROPULSION_TYPE
{
	WHEELED,
	TRACKED,
	LEGGED,
	HOVER,
	LIFT,
	PROPELLER,
	HALF_TRACKED
};

/// The possible module types given to droids
enum COMPONENT_TYPE
{
  /** 
   * The `Body` component contains the weight class
   * of a unit, its base armour, resistance to electric
   * and thermal weaponry
   */ 
	BODY,

  /**
   * Commander-only component containing parameters
   * constraining membership and group rankings
   */
	BRAIN,

	PROPULSION,
	REPAIR_UNIT,

  /**
   * ECM = Electronic Counter Measures
   * 
   * Units which have ECM components installed
   * are harder for enemies to detect. If a unit's
   * ECM rating is higher than the enemy's sensor
   * power rating, reduce the detection range of this
   * unit by a third.
   */
	ECM,

	SENSOR,
	CONSTRUCT,
	WEAPON,
	COUNT // MUST BE LAST
};

/**
 * Sensors can detect and fire upon units within a given
 * range. The type will determine which units are targeted.
 * (CB = Counter-Battery)
 */
enum class SENSOR_TYPE
{
	STANDARD,
	INDIRECT_CB,
	VTOL_CB,
	VTOL_INTERCEPT,
	SUPER,
	RADAR_DETECTOR
};

/// Unit weight class
enum class BODY_SIZE
{
	LIGHT,
	MEDIUM,
	HEAVY,
	SUPER_HEAVY
};

/// Base class subclassed by all component types
struct ComponentStats
{
  /** Values which may be changed by upgrades. Subclassed
   * by each component struct
   */
	struct Upgradeable
	{
		unsigned hit_points;

    /// This is the modifier used for adjusting a unit's final hit points
		unsigned hit_point_percent;
	};

  /// The texture for this component
	std::unique_ptr<iIMDShape> imd_shape;

	COMPONENT_TYPE type;

  /// Power cost
	unsigned power_to_build;

	unsigned weight;

  /// `true` if available through the design UI
	bool is_designable;
};

/// Contains the detection range of a sensor
struct SensorStats : public ComponentStats
{
	using enum SENSOR_TYPE;

  /// Default sensor is `standard`
	SENSOR_TYPE type = STANDARD;

	struct : Upgradeable
	{
    /// The maximum distance at which a unit is detectable
		unsigned range;
    
    /**
     * Each player has a separate upgradeable stats object. There is
     * also a shared copy of the base stats
     */
	} upgraded[MAX_PLAYERS], base;
};

/// Object containing all stats relevant to a unit's `propulsion_type`
struct PropulsionStats : public ComponentStats
{
	using enum PROPULSION_TYPE;
	PROPULSION_TYPE propulsion_type;
	bool is_airborne = false;
	unsigned power_ratio_multiplier = 0;
	int start_sound = 0;
	int idle_sound = 0;
	int move_off_sound = 0;
	int move_sound = 0;
	int hiss_sound = 0;
	int shutdown_sound = 0;
	unsigned max_speed = 0;
	unsigned turn_speed = 0;
	unsigned spin_speed = 0;
	unsigned spin_angle = 0;
	unsigned skid_deceleration = 0;
	unsigned deceleration = 0;
	unsigned acceleration = 0;

	struct : Upgradeable
	{
		int hp_percent_increase = 0;
	} upgraded[MAX_PLAYERS], base;
};

/**
 *
 */
struct CommanderStats : public ComponentStats
{
  ///
	std::unique_ptr<WeaponStats> weapon_stats;

	struct : Upgradeable
	{
    ///
		std::vector<int> rank_thresholds;

    ///
		int max_droids_assigned = 0;

    ///
		int max_droids_multiplier = 0;
	} upgraded[MAX_PLAYERS], base;
};

struct BodyStats : public ComponentStats
{
	BODY_SIZE size;

	struct : Upgradeable
	{
		unsigned power_output;

    /// Protection against physical weapons, e.g., bullets
		unsigned armour;

    /// Protection against flamethrowers
		int thermal;

    /// Protection against electronic weaponry
		int resistance;
	} upgraded[MAX_PLAYERS], base;
};

/**
 *
 */
struct ECMStats : public ComponentStats
{
	struct : Upgradeable
	{
		unsigned range = 0;
	} upgraded[MAX_PLAYERS], base;
};

#endif // WARZONE2100_STATS_H
