//
// Created by luna on 08/12/2021.
//

#ifndef WARZONE2100_DROID_H
#define WARZONE2100_DROID_H

#include <memory>
#include <optional>
#include <vector>

#include "droid_group.h"
#include "movement.h"
#include "obj_lists.h"
#include "order.h"

static constexpr auto MAX_COMPONENTS = COMPONENT_TYPE::COUNT - 1;
static constexpr auto ALLIANCE_FORMED = 3;
static constexpr auto ALLIANCE_BROKEN = 0;

extern PlayerMask satellite_uplink_bits;
extern std::array<PlayerMask, MAX_PLAYER_SLOTS> alliance_bits;
extern std::array<std::array<uint8_t, MAX_PLAYER_SLOTS>, MAX_PLAYER_SLOTS>
alliances;

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
	CIRCLE
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

class Droid : public virtual ::Unit, public Impl::Unit
{
public:
	Droid(unsigned id, unsigned player);

	[[nodiscard]] ACTION get_current_action() const noexcept;
	[[nodiscard]] const Order& get_current_order() const;
	[[nodiscard]] bool is_probably_doomed(bool is_direct_damage) const;
	[[nodiscard]] bool is_commander() const noexcept;
	[[nodiscard]] bool is_VTOL() const;
	[[nodiscard]] bool is_flying() const;
	[[nodiscard]] bool is_transporter() const;
	[[nodiscard]] bool is_builder() const;
	[[nodiscard]] bool is_cyborg() const;
	[[nodiscard]] bool is_repairer() const;
	[[nodiscard]] bool is_IDF() const;
	[[nodiscard]] bool is_radar_detector() const final;
	[[nodiscard]] bool is_stationary() const;
	[[nodiscard]] bool is_rearming() const;
	[[nodiscard]] bool is_damaged() const;
	[[nodiscard]] bool is_attacking() const noexcept;
	[[nodiscard]] bool is_VTOL_rearmed_and_repaired() const;
	[[nodiscard]] bool is_VTOL_empty() const;
	[[nodiscard]] bool is_VTOL_full() const;
	[[nodiscard]] bool is_valid_target(const ::Unit* attacker,
	                                   int weapon_slot) const final;
	[[nodiscard]] bool has_commander() const;
	[[nodiscard]] bool has_standard_sensor() const;
	[[nodiscard]] bool has_CB_sensor() const;
	[[nodiscard]] bool has_electronic_weapon() const;
	void gain_experience(unsigned exp);
	void commander_gain_experience(unsigned exp) const;
	void move_to_rearming_pad();
	void cancel_build();
	void reset_action() noexcept;
	void update_expected_damage(unsigned damage, bool is_direct) noexcept;
	[[nodiscard]] DROID_TYPE get_type() const noexcept;
	[[nodiscard]] unsigned get_level() const;
	[[nodiscard]] unsigned get_commander_level() const;
	[[nodiscard]] unsigned commander_max_group_size() const;
	[[nodiscard]] const iIMDShape& get_IMD_shape() const final;
	[[nodiscard]] unsigned calculate_sensor_range() const final;
	[[nodiscard]] unsigned calculate_max_range() const;
	[[nodiscard]] int calculate_height() const;
  [[nodiscard]] int space_occupied_on_transporter() const;
  [[nodiscard]] int get_vertical_speed() const noexcept;
  void increment_kills() noexcept;
  void increment_commander_kills() const;
private:
	using enum ACTION;
	using enum DROID_TYPE;

	std::string name;
	ACTION action{NONE};
	DROID_TYPE type{ANY};
	Structure* associated_structure{nullptr};
	std::shared_ptr<Droid_Group> group;
	std::unique_ptr<Order> order;
	std::unique_ptr<Movement> movement;
	std::unique_ptr<Body_Stats> body;
	std::optional<Propulsion_Stats> propulsion;
	std::optional<Commander_Stats> brain;
	std::optional<Sensor_Stats> sensor;
	std::optional<ECM_Stats> ecm;
	unsigned weight{0};
	unsigned base_speed{0};
	unsigned original_hp{0};
	unsigned expected_damage_direct{0};
	unsigned expected_damage_indirect{0};
	unsigned kills{0};
	unsigned experience{0};
	unsigned action_points_done{0};
	int resistance_to_electric{0};
	std::size_t time_action_started{0};
};

[[nodiscard]] unsigned count_player_command_droids(unsigned player);
[[nodiscard]] bool still_building(const Droid& droid);
[[nodiscard]] bool can_assign_fire_support(const Droid& droid,
                                           const Structure& structure);
[[nodiscard]] unsigned get_effective_level(const Droid& droid);
[[nodiscard]] bool all_VTOLs_rearmed(const Droid& droid);
[[nodiscard]] bool VTOL_ready_to_rearm(const Droid& droid,
                                       const Rearm_Pad& rearm_pad);
[[nodiscard]] bool being_repaired(const Droid& droid);
// return UBYTE_MAX if directly visible, UBYTE_MAX / 2 if shown as radar blip, 0
// if not visible
[[nodiscard]] uint8_t is_target_visible(const Droid& droid,
                                        const Simple_Object* target,
                                        bool walls_block);
[[nodiscard]] bool target_within_action_range(const Droid& droid,
                                              const Unit& target,
                                              int weapon_slot);
[[nodiscard]] bool target_within_weapon_range(const Droid& droid,
                                              const Unit& target,
                                              int weapon_slot);

[[nodiscard]] constexpr bool tile_occupied_by_droid(unsigned x, unsigned y)
{
	for (const auto& player_droids : droid_lists)
	{
		if (std::any_of(player_droids.begin(), player_droids.end(),
		                [x, y](const auto& droid)
		                {
			                return map_coord(droid.get_position().x) == x &&
				                map_coord(droid.get_position().y == y);
		                }))
		{
			return true;
		}
	}
	return false;
}

struct Droid_Template
{
	using enum DROID_TYPE;

	DROID_TYPE type;
	uint8_t weapon_count;
	bool is_prefab;
	bool is_stored;
	bool is_enabled;
};

inline bool VTOL_may_land_here(const int x, const int y);

template <typename T>
unsigned calculate_required_build_points(const T& object);

template <typename T>
unsigned calculate_required_power(const T& object);

#endif  // WARZONE2100_DROID_H
