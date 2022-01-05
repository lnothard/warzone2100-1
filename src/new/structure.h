//
// Created by luna on 08/12/2021.
//

#ifndef WARZONE2100_STRUCTURE_H
#define WARZONE2100_STRUCTURE_H

#include "lib/ivis_opengl/ivisdef.h"
#include "droid.h"
#include "map.h"
#include "positiondef.h"
#include "unit.h"

static constexpr auto MAX_IN_RUN = 9;
static constexpr auto MAX_POWER_MODULES = 4;
static constexpr auto MAX_FACTORY_MODULES = 2;

enum class STRUCTURE_STATE
{
	BEING_BUILT,
	BUILT,
	BLUEPRINT_VALID,
	BLUEPRINT_INVALID,
	BLUEPRINT_PLANNED,
	BLUEPRINT_PLANNED_BY_ALLY,
};

enum class STRUCTURE_TYPE
{
	HQ,
	FACTORY,
	FACTORY_MODULE,
	POWER_GEN,
	POWER_MODULE,
	RESOURCE_EXTRACTOR,
	DEFENSE,
	WALL,
	WALL_CORNER,
	GENERIC,
	RESEARCH,
	RESEARCH_MODULE,
	REPAIR_FACILITY,
	COMMAND_CONTROL,
	BRIDGE,
	DEMOLISH,
	CYBORG_FACTORY,
	VTOL_FACTORY,
	LAB,
	REARM_PAD,
	MISSILE_SILO,
	SAT_UPLINK,
	GATE,
	LASSAT
};

enum class STRUCTURE_STRENGTH
{
	SOFT,
	MEDIUM,
	HARD,
	BUNKER
};

enum class STRUCTURE_ANIMATION_STATE
{
	NORMAL,
	OPEN,
	OPENING,
	CLOSING
};

enum class PENDING_STATUS
{
	NOTHING_PENDING,
	START_PENDING,
	HOLD_PENDING,
	CANCEL_PENDING
};

/**
 * 
 */
struct StructureBounds
{
	StructureBounds();
	StructureBounds(const Vector2i& top_left_coords, const Vector2i& size_in_coords);

	Vector2i top_left_coords {0, 0};
	Vector2i size_in_coords {0, 0};
};

struct FlagPosition : public ObjectPosition
{
	Vector3i coords {0, 0, 0};
	uint8_t factory_inc = 0;
	uint8_t factory_type = 0;
};

/**
 * 
 */
struct Structure_Stats
{
	[[nodiscard]] Vector2i size(unsigned direction) const;
  [[nodiscard]] bool is_expansion_module() const noexcept;

	using enum STRUCTURE_TYPE;

	struct
	{
		unsigned hit_points;
		unsigned power;
		unsigned armour;
    unsigned thermal;
	} upgraded_stats[MAX_PLAYERS], base_stats;

	STRUCTURE_TYPE type;
	STRUCTURE_STRENGTH strength;
	std::vector< std::unique_ptr<iIMDShape> > IMDs;
	std::unique_ptr<SensorStats> sensor_stats;
	std::unique_ptr<ECMStats> ecm_stats;
	std::unique_ptr<iIMDShape> base_imd;
	bool combines_with_wall = false;
	bool is_favourite = false;
	unsigned base_width = 0;
	unsigned base_breadth = 0;
	unsigned build_point_cost = 0;
	unsigned height = 0;
	unsigned power_to_build = 0;
	unsigned weapon_slots = 0;
	unsigned num_weapons_default = 0;
};

class Structure : public virtual Unit
{
public:
	virtual ~Structure() = default;
	Structure(const Structure&) = delete;
	Structure(Structure&&) = delete;
	Structure& operator=(const Structure&) = delete;
	Structure& operator=(Structure&&) = delete;

	virtual void print_info() const = 0;
	virtual bool has_sensor() const = 0;
	virtual bool has_standard_sensor() const = 0;
	virtual bool has_CB_sensor() const = 0;
	virtual bool has_VTOL_intercept_sensor() const = 0;
	virtual bool has_VTOL_CB_sensor() const = 0;
};

namespace Impl
{
	class Structure : public virtual ::Structure, public Impl::Unit
	{
	public:
		Structure(unsigned id, unsigned player);

		[[nodiscard]] bool is_blueprint() const noexcept;
		[[nodiscard]] bool is_wall() const noexcept;
		[[nodiscard]] bool is_radar_detector() const final;
		[[nodiscard]] bool is_probably_doomed() const;
		[[nodiscard]] bool is_pulled_to_terrain() const;
		[[nodiscard]] bool has_modules() const noexcept;
		[[nodiscard]] bool has_sensor() const final;
		[[nodiscard]] bool has_standard_sensor() const final;
		[[nodiscard]] bool has_CB_sensor() const final;
		[[nodiscard]] bool has_VTOL_intercept_sensor() const final;
		[[nodiscard]] bool has_VTOL_CB_sensor() const final;
		[[nodiscard]] bool smoke_when_damaged() const noexcept;
		[[nodiscard]] unsigned get_original_hp() const;
    [[nodiscard]] unsigned Structure::get_armour_value(WEAPON_CLASS weapon_class) const;
		[[nodiscard]] Vector2i get_size() const;
		[[nodiscard]] int get_foundation_depth() const noexcept;
		[[nodiscard]] const iIMDShape& get_IMD_shape() const final;
		void update_expected_damage(unsigned damage, bool is_direct) noexcept override;
		[[nodiscard]] unsigned calculate_sensor_range() const final;
		[[nodiscard]] int calculate_gate_height(const std::size_t time, const int minimum) const;
		void set_foundation_depth(int depth) noexcept;
		void print_info() const override;
    [[nodiscard]] unsigned build_points_to_completion() const;
    [[nodiscard]] unsigned calculate_refunded_power() const;
    [[nodiscard]] int calculate_attack_priority(const Unit* target, int weapon_slot) const final;
    [[nodiscard]] const ::SimpleObject& get_target(int weapon_slot) const final;
    [[nodiscard]] STRUCTURE_STATE get_state() const;
	private:
		using enum STRUCTURE_STATE;
		using enum STRUCTURE_ANIMATION_STATE;
		using enum STRUCTURE_TYPE;
		using enum SENSOR_TYPE;

		STRUCTURE_STATE state;
		STRUCTURE_ANIMATION_STATE animation_state = NORMAL;
		std::shared_ptr<Structure_Stats> stats;
    std::array<::SimpleObject*, MAX_WEAPONS> target;
		unsigned current_build_points = 0;
		int build_rate = 0;
		int previous_build_rate = 0;
		unsigned expected_damage = 0;
		uint8_t num_modules = 0;
		int foundation_depth = 0;
		std::size_t last_state_time = 0;
	};

  /// @return The number of artillery and VTOL droids assigned to `structure`
	[[nodiscard]] unsigned count_assigned_droids(const Structure& structure);

	[[nodiscard]] bool being_built(const Structure& structure);
	[[nodiscard]] bool being_demolished(const Structure& structure);
	[[nodiscard]] bool is_damaged(const Structure& structure);
	[[nodiscard]] StructureBounds get_bounds(const Structure& structure) noexcept;
	void adjust_tile_height(const Structure& structure, int new_height);
	[[nodiscard]] int calculate_height(const Structure& structure);
	[[nodiscard]] int calculate_foundation_height(const Structure& structure);
	void align_structure(Structure& structure);
	[[nodiscard]] bool target_within_range(const Structure& structure, const Unit& unit, int weapon_slot);
}

const Structure* find_repair_facility(unsigned player);

struct ProductionRun
{
  ProductionRun() = default;
  bool operator ==(const DroidTemplate& rhs) const;

  void restart();
  bool is_valid() const;
  bool is_complete() const;
  int tasks_remaining() const;

  std::shared_ptr<DroidTemplate> target;
  int quantity_to_build = 0;
  int quantity_built = 0;
};

struct ResearchItem
{
	uint8_t tech_code;
	unsigned research_point_cost;
	unsigned power_cost;
};

class Factory : public virtual Structure, public Impl::Structure
{
public:
	void pause_production(QUEUE_MODE mode);
	void resume_production(QUEUE_MODE mode);
	void cancel_production(QUEUE_MODE mode);
  void increment_production_loops();
  void decrement_production_loops();
private:
	using enum PENDING_STATUS;
  std::shared_ptr<DroidTemplate> current_target;
  std::shared_ptr<DroidTemplate> pending_target;
	std::unique_ptr<FlagPosition> assembly_point;
	PENDING_STATUS pending_status;
	uint8_t production_loops;
	uint8_t loops_performed;
  std::size_t time_started;
};

class ResearchFacility : public virtual Structure, public Impl::Structure
{
private:
	ResearchItem active_research_task;
	ResearchItem pending_research_task;
};

class PowerGenerator : public virtual Structure, public Impl::Structure
{
public:
  void update_current_power();
  void print_info() const final;
private:
	std::vector<Structure*> resource_extractors;
};

class ResourceExtractor : public virtual Structure, public Impl::Structure
{
public:
  int get_extracted_power() const;
private:
	Structure* owning_power_generator = nullptr;
};

class RearmPad : public virtual Structure, public Impl::Structure
{
public:
	bool is_clear() const;
private:
	Droid* rearm_target = nullptr;
	std::size_t time_started = 0;
	std::size_t last_update_time = 0;
};

class RepairFacility : public virtual Structure, public Impl::Structure
{
private:
	Unit* repair_target = nullptr;
	std::unique_ptr<FlagPosition> assembly_point;
};

#endif // WARZONE2100_STRUCTURE_H
