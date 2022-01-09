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

/// The maximum number of components per droid
static constexpr auto MAX_COMPONENTS = COMPONENT_TYPE::COUNT - 1;

/**
 * The bit flag used when an alliance exists
 * between two players
 */
static constexpr auto ALLIANCE_FORMED = 3;

/**
 * The bit flag used when no alliance exists
 * between two players
 */
static constexpr auto ALLIANCE_BROKEN = 0;

/**
 * The maximum distance a VTOL droid may travel
 * in a single attack run
 */
static constexpr auto VTOL_ATTACK_LENGTH = 1000;

/**
 * The search radius to use when searching for a
 * landing position for a VTOL
 */
static constexpr auto VTOL_LANDING_RADIUS = 23;

/**
 * The closest distance a droid may be to the
 * edge of the world
 */
static constexpr auto TOO_NEAR_EDGE = 3;

/// How far to retreat if in danger
static constexpr auto FALLBACK_DISTANCE = 10;

/// The maximum number of commanders per player
static constexpr auto MAX_COMMAND_DROIDS = 5;

/* Modifiers used for target selection */

/**
 * How much weight a distance of 1 tile (128 world units)
 * has when looking for the best nearest target selection
 */
static constexpr auto BASE_WEIGHT = 13;

/// Droids are high priority targets
static constexpr auto DROID_DAMAGE_WEIGHT = BASE_WEIGHT * 10;

static constexpr auto STRUCT_DAMAGE_WEIGHT = BASE_WEIGHT * 7;
static constexpr auto NOT_VISIBLE_WEIGHT = 10;
static constexpr auto SERVICE_DROID_WEIGHT = BASE_WEIGHT * 5;
static constexpr auto WEAPON_DROID_WEIGHT = BASE_WEIGHT * 4;
static constexpr auto COMMAND_DROID_WEIGHT = BASE_WEIGHT * 6;
static constexpr auto MILITARY_STRUCT_WEIGHT = BASE_WEIGHT;
static constexpr auto WEAPON_STRUCT_WEIGHT = WEAPON_DROID_WEIGHT;
static constexpr auto DERRICK_WEIGHT = MILITARY_STRUCT_WEIGHT + BASE_WEIGHT * 4;
static constexpr auto UNBUILT_STRUCT_WEIGHT = 8;
static constexpr auto OLD_TARGET_THRESHOLD = BASE_WEIGHT * 4;

extern PlayerMask satellite_uplink_bits;
extern std::array<PlayerMask, MAX_PLAYER_SLOTS> alliance_bits;
extern std::array<std::array<uint8_t, MAX_PLAYER_SLOTS>, MAX_PLAYER_SLOTS> alliances;

/// @return `true` if `p1` and `p2` are allies
constexpr bool alliance_formed(unsigned p1, unsigned p2)
{
  return alliances[p1][p2] == ALLIANCE_FORMED;
}

std::array<Droid*, MAX_PLAYERS> target_designator_list;

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

  /* Accessors */
	[[nodiscard]] ACTION get_current_action() const noexcept;
	[[nodiscard]] const Order& get_current_order() const;
  [[nodiscard]] DROID_TYPE get_type() const noexcept;
  [[nodiscard]] unsigned get_level() const;
  [[nodiscard]] unsigned getCommanderLevel() const;
  [[nodiscard]] const iIMDShape& get_IMD_shape() const final;
  [[nodiscard]] int getVerticalSpeed() const noexcept;
  [[nodiscard]] unsigned getSecondaryOrder() const noexcept;
  [[nodiscard]] const Vector2i& getDestination() const;
  [[nodiscard]] const ::SimpleObject& get_target(int weapon_slot) const final;
  [[nodiscard]] const std::optional<PropulsionStats>& getPropulsion() const;
  
	[[nodiscard]] bool is_probably_doomed(bool is_direct_damage) const;
	[[nodiscard]] bool isVtol() const;
	[[nodiscard]] bool isFlying() const;
	[[nodiscard]] bool isRadarDetector() const final;
	[[nodiscard]] bool is_stationary() const;
	[[nodiscard]] bool is_rearming() const;
	[[nodiscard]] bool is_damaged() const;
	[[nodiscard]] bool is_attacking() const noexcept;
	[[nodiscard]] bool is_VTOL_rearmed_and_repaired() const;
	[[nodiscard]] bool is_VTOL_empty() const;
	[[nodiscard]] bool is_VTOL_full() const;

  /**
   *
   * @param attacker
   * @param weapon_slot
   * @return
   */
	[[nodiscard]] bool isValidTarget(const ::Unit* attacker,
                                   int weapon_slot) const final;

	[[nodiscard]] bool hasCommander() const;
	[[nodiscard]] bool has_standard_sensor() const;
	[[nodiscard]] bool has_CB_sensor() const;
	[[nodiscard]] bool has_electronic_weapon() const;
	void gain_experience(unsigned exp);
	void commander_gain_experience(unsigned exp) const;
	void move_to_rearm_pad();
	void cancel_build();
	void reset_action() noexcept;
	void update_expected_damage(unsigned damage, bool is_direct) noexcept final;
	[[nodiscard]] unsigned commander_max_group_size() const;
	[[nodiscard]] unsigned calculateSensorRange() const final;
	[[nodiscard]] int calculate_height() const;
  [[nodiscard]] int space_occupied_on_transporter() const;
  void set_direct_route(int target_x, int target_y) const;
  void increment_kills() noexcept;
  void increment_commander_kills() const;
  void assign_vtol_to_rearm_pad(RearmPad* rearm_pad);
  [[nodiscard]] int calculate_electronic_resistance() const;
  [[nodiscard]] bool isSelectable() const final;
  [[nodiscard]] unsigned get_armour_points_against_weapon(WEAPON_CLASS weapon_class) const;
  [[nodiscard]] int calculate_attack_priority(const Unit* target, int weapon_slot) const final;
  [[nodiscard]] bool is_hovering() const;
private:
	using enum ACTION;
	using enum DROID_TYPE;
	std::string name;
	ACTION action = NONE;
	DROID_TYPE type = ANY;
	Structure* associated_structure = nullptr;
  std::array<SimpleObject*, MAX_WEAPONS> action_target;
	std::shared_ptr<DroidGroup> group;
	std::unique_ptr<Order> order;
	std::unique_ptr<Movement> movement;
	std::unique_ptr<BodyStats> body;
	std::optional<PropulsionStats> propulsion;
	std::optional<CommanderStats> brain;
	std::optional<SensorStats> sensor;
	std::optional<ECMStats> ecm;
  unsigned secondary_order = 0;
	unsigned weight = 0;
	unsigned base_speed = 0;
	unsigned original_hp = 0;
	unsigned expected_damage_direct = 0;
	unsigned expected_damage_indirect = 0;
	unsigned kills = 0;
	unsigned experience = 0;
	unsigned action_points_done = 0;
	int resistance_to_electric = 0;
	std::size_t time_action_started = 0;
};

[[nodiscard]] bool is_cyborg(const Droid& droid);
[[nodiscard]] bool is_transporter(const Droid& droid);
[[nodiscard]] bool is_builder(const Droid& droid);
[[nodiscard]] bool is_repairer(const Droid& droid);
[[nodiscard]] bool is_idf(const Droid& droid);
[[nodiscard]] bool is_commander(const Droid& droid) noexcept;

/**
 *
 * @param droid
 * @return
 */
[[nodiscard]] unsigned calculate_max_range(const Droid& droid);

[[nodiscard]] unsigned count_player_command_droids(unsigned player);

[[nodiscard]] bool still_building(const Droid& droid);

/**
 *
 * @param droid
 * @param structure
 * @return
 */
[[nodiscard]] bool can_assign_fire_support(const Droid& droid,
                                           const Structure& structure);

[[nodiscard]] unsigned get_effective_level(const Droid& droid);

[[nodiscard]] bool all_VTOLs_rearmed(const Droid& droid);

[[nodiscard]] bool VTOL_ready_to_rearm(const Droid& droid,
                                       const RearmPad& rearm_pad);

[[nodiscard]] bool being_repaired(const Droid& droid);

/**
 * Checks whether a droid can see a target unit
 *
 * @param droid
 * @param target
 * @param walls_block
 *
 * @return UBYTE_MAX if directly visible, 
 * @return UBYTE_MAX / 2 if shown as radar blip
 * @return 0 if not visible
 */
[[nodiscard]] uint8_t is_target_visible(const Droid& droid,
                                        const SimpleObject* target,
                                        bool walls_block);

/**
 *
 * @param droid
 * @param target
 * @param weapon_slot
 * @return
 */
[[nodiscard]] bool action_target_inside_minimum_weapon_range(const Droid& droid,
                                              const Unit& target,
                                              int weapon_slot);

/**
 *
 * @param droid
 * @param target
 * @param weapon_slot
 * @return
 */
[[nodiscard]] bool target_within_weapon_range(const Droid& droid,
                                              const Unit& target,
                                              int weapon_slot);

long get_commander_index(const Droid& commander);
void add_VTOL_attack_run(Droid& droid);
void update_vtol_attack(Droid& droid);
const RearmPad* find_nearest_rearm_pad(const Droid& droid);
bool valid_position_for_droid(int x, int y, PROPULSION_TYPE propulsion);
bool vtol_can_land_here(int x, int y);
Droid* find_nearest_droid(unsigned x, unsigned y, bool selected);

/**
 * Performs a space-filling spiral-like search from `start_pos`, up to (and
 * including) radius.
 *
 * @param start_pos the starting (x, y) coordinates
 * @param max_radius the radius to examine. Search will finish
 *  when this value is exceeded.
 *
 * @return
 */
Vector2i spiral_search(Vector2i start_pos, int max_radius);

void set_blocking_flags(const Droid& droid);
void clear_blocking_flags(const Droid& droid);

/**
 * Find a valid position for droids to retreat to if they
 * need to distance themselves from the target
 *
 * @param unit
 * @param target
 *
 * @return
 */
Vector2i determine_fallback_position(Unit& unit, Unit& target);

/// @return `true` if two droids are adjacently located
bool droids_are_neighbours(const Droid& first, const Droid& second)

/// @return `true` if the tile at (x, y) houses a droid
[[nodiscard]] bool tile_occupied_by_droid(unsigned x, unsigned y);

struct DroidTemplate
{
  DroidTemplate() = default;

	using enum DROID_TYPE;
  unsigned id = 0;
	DROID_TYPE type = ANY;
	uint8_t weaponCount = 0;
	bool isPrefab = false;
	bool isStored = false;
	bool isEnabled = false;
};

template <typename T>
unsigned calculate_required_build_points(const T& object);

template <typename T>
unsigned calculate_required_power(const T& object);

#endif  // WARZONE2100_DROID_H
