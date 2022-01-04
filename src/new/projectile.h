//
// Created by luna on 09/12/2021.
//

#ifndef WARZONE2100_PROJECTILE_H
#define WARZONE2100_PROJECTILE_H

#include "basedef.h"
#include "unit.h"

static constexpr auto PROJECTILE_MAX_PITCH = 45;
static constexpr auto BULLET_FLIGHT_HEIGHT = 16;
static constexpr auto VTOL_HITBOX_MODIFIER = 100;

/// Represents the current stage of a projectile's trajectory
enum class PROJECTILE_STATE
{
	IN_FLIGHT,
	IMPACT,
	POST_IMPACT,
	INACTIVE
};

/// Covers anything fired out of a weapon
struct Projectile
{
  using enum PROJECTILE_STATE;
	PROJECTILE_STATE state = INACTIVE;
  Weapon* firing_weapon = nullptr;
	Unit* source = nullptr;
	Unit* target = nullptr;
	Vector3i destination {0, 0, 0};
	Vector3i origin {0, 0, 0};
	unsigned base_damage = 0;
};

struct Interval
{
  [[nodiscard]] bool is_empty() const noexcept;

  int begin;
  int end;
};

struct Damage
{
    Damage() = default;

    Projectile* projectile = nullptr;
    Unit* target = nullptr;
    unsigned damage = 0;
    WEAPON_CLASS weapon_class;
    WEAPON_SUBCLASS weapon_subclass;
    std::size_t impact_time = 0;
    bool is_ticking_damage = false;
    int min_damage = 0;
};

[[nodiscard]] bool is_friendly_fire(const Damage& damage);

[[nodiscard]] bool should_increase_experience(const Damage& damage);

void update_kills(const Damage& damage);

void set_projectile_target(Projectile& projectile, Unit& unit);

[[nodiscard]] int calculate_height(const Projectile& projectile);

#endif // WARZONE2100_PROJECTILE_H
