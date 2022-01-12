//
// Created by luna on 09/12/2021.
//

#ifndef WARZONE2100_WEAPON_H
#define WARZONE2100_WEAPON_H

#include <cstdint>

#include "lib/framework/vector.h"
#include "lib/gamelib/gtime.h"
#include "basedef.h"
#include "stats.h"

///
static constexpr auto DEFAULT_RECOIL_TIME = GAME_TICKS_PER_SEC / 4;

enum class WEAPON_SIZE
{
	LIGHT,
	HEAVY
};

/// Basic weapon type
enum class WEAPON_CLASS
{
	KINETIC,

  /// Flamethrower class - paired against thermal armour points
	HEAT
};

/// Secondary weapon type
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
	HOWITZER,
	ELECTRONIC,
	SLOW_MISSILE,
	SLOW_ROCKET,
	BOMB,
	EMP
};

/// Specialisation (if any) of the weapon
enum class WEAPON_EFFECT
{
	ANTI_PERSONNEL,
	ANTI_TANK,

  ///
	BUNKER_BUSTER,

	FLAMER,
	ANTI_AIRCRAFT
};

/// The projectile trajectory of this weapon type
enum class MOVEMENT_TYPE
{
	DIRECT,

  /// Artillery
	INDIRECT,

	HOMING_DIRECT,
	HOMING_INDIRECT
};

/**
 * Parameters affecting a weapon's effectiveness, such
 * as range, accuracy and damage
 */
struct WeaponStats : public ComponentStats
{
	using enum WEAPON_CLASS;
	using enum WEAPON_SUBCLASS;
	using enum WEAPON_EFFECT;
	using enum WEAPON_SIZE;
	using enum MOVEMENT_TYPE;

	struct : Upgradeable
	{
		unsigned min_range;
		unsigned max_range;
    unsigned short_range;
		unsigned hit_chance;
    unsigned short_range_hit_chance;
		unsigned direct_damage;
		unsigned blast_radius;
		unsigned splash_damage;
		unsigned min_damage_percent;
		std::size_t reload_time;
		std::size_t pause_between_shots;
		unsigned ticking_damage;
		unsigned ticking_damage_radius;
		std::size_t ticking_damage_duration;
		uint8_t rounds_per_volley;
	} base_stats, upgraded_stats[MAX_PLAYERS];

	WEAPON_CLASS weapon_class;
	WEAPON_SUBCLASS subclass;
	WEAPON_EFFECT effect;
	WEAPON_SIZE size;
	MOVEMENT_TYPE movement_type;

	unsigned flight_speed;
	unsigned recoil_value = DEFAULT_RECOIL_TIME;
	unsigned effect_magnitude;
	short max_rotation;
	short min_elevation;
	short max_elevation;
	short max_VTOL_attack_runs;
	bool can_penetrate;
	bool can_fire_while_moving;

  ///
  uint8_t surface_to_air;

  /// `true` if firing this weapon affects visibility
	bool effect_emits_light;

  /// Main weapon texture
	std::unique_ptr<iIMDShape> weapon_graphic;

  /// Texture for the turret mount
	std::unique_ptr<iIMDShape> mount_graphic;

	std::unique_ptr<iIMDShape> muzzle_graphic;
	std::unique_ptr<iIMDShape> in_flight_graphic;
	std::unique_ptr<iIMDShape> hit_graphic;
	std::unique_ptr<iIMDShape> miss_graphic;
	std::unique_ptr<iIMDShape> splash_graphic;
	std::unique_ptr<iIMDShape> trail_graphic;
};

/// Which kind of object chose the target?
enum class ATTACKER_TYPE
{
	UNKNOWN,
	PLAYER,
	VISUAL,
	ALLY,
	COMMANDER,
	SENSOR,
	CB_SENSOR,
	AIR_DEF_SENSOR,
	RADAR_DETECTOR,

  /// Target specifier is unknown by default
  DEFAULT = UNKNOWN
};

/**
 * Represents a weapon attachment. Used by units;
 * currently structures and droids
 */
class Weapon final : public virtual ::SimpleObject, public Impl::SimpleObject
{
public:
	[[nodiscard]] bool hasAmmo() const;
	[[nodiscard]] bool hasFullAmmo() const noexcept;
	[[nodiscard]] bool isArtillery() const noexcept;
	[[nodiscard]] bool isVtolWeapon() const;
	[[nodiscard]] bool isEmptyVtolWeapon(unsigned player) const;
	[[nodiscard]] const WeaponStats& getStats() const;
	[[nodiscard]] unsigned getRecoil() const;
	[[nodiscard]] unsigned getMaxRange(unsigned player) const;
	[[nodiscard]] unsigned getMinRange(unsigned player) const;
  [[nodiscard]] unsigned getShortRange(unsigned player) const;
  [[nodiscard]] unsigned getHitChance(unsigned player) const;
  [[nodiscard]] unsigned getShortRangeHitChance(unsigned player) const;
	[[nodiscard]] unsigned getNumAttackRuns(unsigned player) const;
	[[nodiscard]] unsigned getShotsFired() const noexcept;
	[[nodiscard]] const iIMDShape& getImdShape() const;
	[[nodiscard]] const iIMDShape& getMountGraphic() const;
	[[nodiscard]] WEAPON_SUBCLASS getSubclass() const;
  [[nodiscard]] unsigned calculateRateOfFire(unsigned player) const;
  void use_ammo();
private:
	using enum ATTACKER_TYPE;

	ATTACKER_TYPE attacker_type;

  /**
   * Has shared ownership of the `WeaponStats` object, since
   * there will usually be several weapons of the same type
   */
	std::shared_ptr<WeaponStats> stats;

	Rotation rotation { 0, 0, 0};
	Rotation previousRotation {0, 0, 0};
	unsigned ammo = 0;
	unsigned ammoUsed = 0;
	unsigned shotsFired = 0;
	std::size_t timeLastFired = gameTime;
};

#endif // WARZONE2100_WEAPON_H
