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

enum class PROJECTILE_STATE
{
	IN_FLIGHT,
	IMPACT,
	POST_IMPACT,
	INACTIVE
};

struct Projectile
{
	PROJECTILE_STATE state;
	Unit* source;
	Unit* target;
	Vector3i destination;
	Vector3i origin;
	unsigned base_damage;
};

struct Damage
{
    Projectile* projectile;
    Unit* target;
    unsigned damage;
    WEAPON_CLASS weapon_class;
    WEAPON_SUBCLASS weapon_subclass;
    std::size_t impact_time;
    bool is_ticking_damage;
    int min_damage;
};

[[nodiscard]] bool is_friendly_fire(const Damage& damage);
[[nodiscard]] bool should_increase_experience(const Damage& damage);

[[nodiscard]] constexpr int calculate_height(const Projectile& projectile)
{
	return BULLET_FLIGHT_HEIGHT;
}

#endif // WARZONE2100_PROJECTILE_H
