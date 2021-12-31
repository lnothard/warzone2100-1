//
// Created by luna on 08/12/2021.
//

#ifndef WARZONE2100_STATS_H
#define WARZONE2100_STATS_H

#include <memory>

#include "lib/framework/frame.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "weapon.h"

static constexpr auto SHOOT_IN_AIR = 0x02;
static constexpr auto SHOOT_ON_GROUND = 0x01;

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

enum COMPONENT_TYPE
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

enum class SENSOR_TYPE
{
	STANDARD,
	INDIRECT_CB,
	VTOL_CB,
	VTOL_INTERCEPT,
	SUPER,
	RADAR_DETECTOR
};

enum class BODY_SIZE
{
	LIGHT,
	MEDIUM,
	HEAVY,
	SUPER_HEAVY
};

struct Component_Stats
{
	struct Upgradeable
	{
		unsigned hit_points;
		unsigned hit_point_percent;
	};

	std::unique_ptr<iIMDShape> imd_shape;
	COMPONENT_TYPE type;
	unsigned power_to_build;
	unsigned weight;
	bool is_designable;
};

struct Sensor_Stats : public Component_Stats
{
	using enum SENSOR_TYPE;

	SENSOR_TYPE type = STANDARD;

	struct : Upgradeable
	{
		unsigned range;
	} upgraded[MAX_PLAYERS], base;
};

struct Propulsion_Stats : public Component_Stats
{
	using enum PROPULSION_TYPE;

	PROPULSION_TYPE propulsion_type;
	bool is_airborne{false};
	unsigned power_ratio_multiplier{0};
	int start_sound{0};
	int idle_sound{0};
	int move_off_sound{0};
	int move_sound{0};
	int hiss_sound{0};
	int shutdown_sound{0};
	unsigned max_speed{0};
	unsigned turn_speed{0};
	unsigned spin_speed{0};
	unsigned spin_angle{0};
	unsigned skid_deceleration{0};
	unsigned deceleration{0};
	unsigned acceleration{0};

	struct : Upgradeable
	{
		int hp_percent_increase{0};
	} upgraded[MAX_PLAYERS], base;
};

struct Commander_Stats : public Component_Stats
{
	std::unique_ptr<Weapon_Stats> weapon_stats;

	struct : Upgradeable
	{
		std::vector<int> rank_thresholds;
		int max_droids_assigned{0};
		int max_droids_multiplier{0};
	} upgraded[MAX_PLAYERS], base;
};

struct Body_Stats : public Component_Stats
{
	BODY_SIZE size;

	struct : Upgradeable
	{
		unsigned power_output;
		unsigned armour;
		int thermal;
		int resistance;
	} upgraded[MAX_PLAYERS], base;
};

struct ECM_Stats : public Component_Stats
{
	struct : Upgradeable
	{
		unsigned range{0};
	} upgraded[MAX_PLAYERS], base;
};

#endif // WARZONE2100_STATS_H
