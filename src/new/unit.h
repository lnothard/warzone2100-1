//
// Created by luna on 08/12/2021.
//

#ifndef WARZONE2100_UNIT_H
#define WARZONE2100_UNIT_H

#include <vector>

#include "weapon.h"

static constexpr auto LINE_OF_FIRE_MINIMUM = 5;
static constexpr auto TURRET_ROTATION_RATE = 45;

class Unit : public virtual ::Simple_Object
{
public:
	Unit() = default;
	~Unit() override = default;
	Unit(const Unit&) = delete;
	Unit(Unit&&) = delete;
	Unit& operator=(const Unit&) = delete;
	Unit& operator=(Unit&&) = delete;

	[[nodiscard]] virtual bool is_alive() const = 0;
	[[nodiscard]] virtual bool is_radar_detector() const = 0;
	virtual bool is_valid_target(const Unit* attacker, int weapon_slot) const = 0;
	virtual uint8_t is_target_visible(const Simple_Object* target, bool walls_block) const = 0;
	[[nodiscard]] virtual unsigned get_hp() const = 0;
	[[nodiscard]] virtual unsigned calculate_sensor_range() const = 0;
	[[nodiscard]] virtual const std::vector<Weapon>& get_weapons() const = 0;
	[[nodiscard]] virtual const iIMDShape& get_IMD_shape() const = 0;
  [[nodiscard]] virtual bool is_selected() const noexcept = 0;
  virtual void update_expected_damage(unsigned damage, bool is_direct) noexcept = 0;
  virtual void use_ammo(int weapon_slot) = 0;
};

Vector3i calculate_muzzle_base_location(const Unit& unit, int weapon_slot);
Vector3i calculate_muzzle_tip_location(const Unit& unit, int weapon_slot);

namespace Impl
{
	class Unit : public virtual ::Unit, public Impl::Simple_Object
	{
	public:
		Unit(uint32_t id, uint32_t player);

		[[nodiscard]] unsigned get_hp() const noexcept final;
		[[nodiscard]] const std::vector<Weapon>& get_weapons() const final;
    [[nodiscard]] bool is_selected() const noexcept final;
		void align_turret(int weapon_slot);
    void use_ammo(int weapon_slot) override;
	private:
		unsigned hit_points{0};
    bool selected = false;
		std::vector<Weapon> weapons{0};
	};

	void check_angle(int64_t& angle_tan, int start_coord, int height, int square_distance, int target_height,
	                 bool is_direct);
	[[nodiscard]] bool has_full_ammo(const Unit& unit) noexcept;
	[[nodiscard]] bool has_artillery(const Unit& unit) noexcept;
	[[nodiscard]] bool has_electronic_weapon(const Unit& unit) noexcept;
	[[nodiscard]] bool target_in_line_of_fire(const Unit& unit, const ::Unit& target, int weapon_slot);
	[[nodiscard]] int calculate_line_of_fire(const Unit& unit, const ::Simple_Object& target, int weapon_slot,
	                                         bool walls_block, bool is_direct);
	[[nodiscard]] unsigned num_weapons(const Unit& unit);
	[[nodiscard]] unsigned get_max_weapon_range(const Unit& unit);
}
#endif // WARZONE2100_UNIT_H
