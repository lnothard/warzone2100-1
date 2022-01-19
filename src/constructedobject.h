
/**
 * @file unit.h
 */

#ifndef WARZONE2100_CONSTRUCTEDOBJECT_H
#define WARZONE2100_CONSTRUCTEDOBJECT_H

#include "basedef.h"

enum class WEAPON_SUBCLASS;
struct iIMDShape;
struct Weapon;


static constexpr auto LINE_OF_FIRE_MINIMUM = 5;
static constexpr auto TURRET_ROTATION_RATE = 45;

/// The maximum number of weapons attached to a single unit
static constexpr auto MAX_WEAPONS = 3;

/// Abstract base class with shared methods for both structures and droids
class ConstructedObject : public virtual PersistentObject
{
public:
	~ConstructedObject() override = default;

  [[nodiscard]] virtual const std::vector<Weapon>& getWeapons() const = 0;
  [[nodiscard]] virtual const iIMDShape& getImdShape() const = 0;
  [[nodiscard]] virtual const PersistentObject* getTarget(int weapon_slot) const = 0;
  [[nodiscard]] virtual int getResistance() const = 0;
  [[nodiscard]] virtual unsigned getOriginalHp() const = 0;

  [[nodiscard]] virtual bool isAlive() const = 0;
	[[nodiscard]] virtual bool isRadarDetector() const = 0;
	virtual bool isValidTarget(const ConstructedObject* attacker, int weapon_slot) const = 0;
	virtual uint8_t isTargetVisible(const PersistentObject* target, bool walls_block) const = 0;
	[[nodiscard]] virtual unsigned calculateSensorRange() const = 0;
  virtual void alignTurret(int weapon_slot) = 0;
  virtual void updateExpectedDamage(unsigned damage, bool is_direct) noexcept = 0;
  [[nodiscard]] virtual int calculateAttackPriority(const ConstructedObject* target, int weapon_slot) const = 0;
  [[nodiscard]] virtual bool hasCbSensor() const = 0;
  [[nodiscard]] virtual bool hasStandardSensor() const = 0;
  [[nodiscard]] virtual bool hasVtolInterceptSensor() const = 0;
  [[nodiscard]] virtual bool hasVtolCbSensor() const = 0;
};

Vector3i calculateMuzzleBaseLocation(const ConstructedObject& unit, int weapon_slot);

Vector3i calculateMuzzleTipLocation(const ConstructedObject& unit, int weapon_slot);

namespace Impl
{
	class ConstructedObject : public virtual ::ConstructedObject, public PersistentObject
	{
	public:
		ConstructedObject(unsigned id, unsigned player);

    /************************** Accessors *************************/
		[[nodiscard]] const std::vector<Weapon>& getWeapons() const final;
    [[nodiscard]] int getResistance() const final;

		void alignTurret(int weapon_slot) final;
  protected:
    /// Current resistance points, 0 = cannot be attacked electrically
    int resistance = 0;
    unsigned lastEmissionTime;
    WEAPON_SUBCLASS lastHitWeapon;
    std::vector<TILEPOS> watchedTiles;
		std::vector<Weapon> weapons;
	};
}

void checkAngle(int64_t& angle_tan, int start_coord, int height,
                int square_distance, int target_height, bool is_direct);

[[nodiscard]] bool hasFullAmmo(const ConstructedObject& unit) noexcept;

/// @return `true` if `unit` has an indirect weapon attached
[[nodiscard]] bool hasArtillery(const ConstructedObject& unit) noexcept;

/// @return `true` if `unit` has an electronic weapon attached
[[nodiscard]] bool hasElectronicWeapon(const ConstructedObject& unit) noexcept;

/**
 * @return `true` if `unit` may fire upon `target` with the weapon in
 *    `weapon_slot`
 */
[[nodiscard]] bool targetInLineOfFire(const ConstructedObject& unit,
                                      const ::ConstructedObject& target,
                                      int weapon_slot);

/**
 *
 * @param walls_block `true` if
 * @param is_direct `false` if this is an artillery weapon
 * @return
 */
[[nodiscard]] int calculateLineOfFire(const ConstructedObject& unit, const ::PersistentObject& target,
                                      int weapon_slot, bool walls_block = true, bool is_direct = true);


[[nodiscard]] unsigned getMaxWeaponRange(const ConstructedObject& unit);

[[nodiscard]] unsigned numWeapons(const Impl::ConstructedObject& unit);

#endif // WARZONE2100_CONSTRUCTEDOBJECT_H
