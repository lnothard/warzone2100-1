
/** @file unit.h
 *
 */

#ifndef WARZONE2100_UNIT_H
#define WARZONE2100_UNIT_H

#include <vector>

#include "weapon.h"

///
static constexpr auto LINE_OF_FIRE_MINIMUM = 5;

static constexpr auto TURRET_ROTATION_RATE = 45;

/// The maximum number of weapons attached to a single unit
static constexpr auto MAX_WEAPONS = 3;

/// Abstract base class with shared methods for both structures and droids
class Unit : public virtual ::SimpleObject
{
public:
	Unit() = default;
	~Unit() override = default;

	Unit(const Unit&) = delete;
	Unit(Unit&&) = delete;
	Unit& operator=(const Unit&) = delete;
	Unit& operator=(Unit&&) = delete;

	[[nodiscard]] virtual bool isAlive() const = 0;
	[[nodiscard]] virtual bool isRadarDetector() const = 0;
	virtual bool isValidTarget(const Unit* attacker, int weapon_slot) const = 0;
	virtual uint8_t isTargetVisible(const SimpleObject* target, bool walls_block) const = 0;
	[[nodiscard]] virtual unsigned getHp() const = 0;
	[[nodiscard]] virtual unsigned calculateSensorRange() const = 0;
	[[nodiscard]] virtual const std::vector<Weapon>& getWeapons() const = 0;
	[[nodiscard]] virtual const iIMDShape& getImdShape() const = 0;
  [[nodiscard]] virtual bool is_selected() const noexcept = 0;
  virtual void align_turret(int weapon_slot) = 0;
  virtual void updateExpectedDamage(unsigned damage, bool is_direct) noexcept = 0;
  virtual void use_ammo(int weapon_slot) = 0;
  [[nodiscard]] virtual int calculateAttackPriority(const Unit* target, int weapon_slot) const = 0;
  [[nodiscard]] virtual const SimpleObject& getTarget(int weapon_slot) const = 0;
};

Vector3i calculate_muzzle_base_location(const Unit& unit, int weapon_slot);

Vector3i calculate_muzzle_tip_location(const Unit& unit, int weapon_slot);

namespace Impl
{
	class Unit : public virtual ::Unit, public Impl::SimpleObject
	{
	public:
		Unit(uint32_t id, uint32_t player);

    /* Accessors */
		[[nodiscard]] unsigned getHp() const noexcept final;
		[[nodiscard]] const std::vector<Weapon>& getWeapons() const final;

    /// @return `true` if this unit is being focused by its owner
    [[nodiscard]] bool is_selected() const noexcept final;

		void align_turret(int weapon_slot) final;
    void use_ammo(int weapon_slot) override;
	private:
		unsigned hit_points = 0;
    bool selected = false;
		std::vector<Weapon> weapons;
	};

	void check_angle(int64_t& angle_tan, int start_coord, int height,
                   int square_distance, int target_height, bool is_direct);

	[[nodiscard]] bool has_full_ammo(const Unit& unit) noexcept;

  /// @return `true` if `unit` has an indirect weapon attached
	[[nodiscard]] bool has_artillery(const Unit& unit) noexcept;

  /// @return `true` if `unit` has an electronic weapon attached
	[[nodiscard]] bool has_electronic_weapon(const Unit& unit) noexcept;

  /**
   * @return `true` if `unit` may fire upon `target` with the weapon in
   *    `weapon_slot`
   */
	[[nodiscard]] bool target_in_line_of_fire(const Unit& unit,
                                           const ::Unit& target,
                                           int weapon_slot);

  /**
   *
   * @param walls_block `true` if
   * @param is_direct `false` if this is an artillery weapon
   * @return
   */
	[[nodiscard]] int calculate_line_of_fire(const Unit& unit, const ::SimpleObject& target,
                                           int weapon_slot, bool walls_block, bool is_direct);

	[[nodiscard]] unsigned num_weapons(const Unit& unit);

	[[nodiscard]] unsigned get_max_weapon_range(const Unit& unit);
}
#endif // WARZONE2100_UNIT_H
