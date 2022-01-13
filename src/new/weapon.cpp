//
// Created by luna on 09/12/2021.
//

#include <cstdlib>

#include "weapon.h"

bool Weapon::hasFullAmmo() const noexcept
{
	return ammoUsed == 0;
}

bool Weapon::isArtillery() const noexcept
{
	return stats->movement_type == MOVEMENT_TYPE::INDIRECT ||
		stats->movement_type == MOVEMENT_TYPE::HOMING_INDIRECT;
}

bool Weapon::isVtolWeapon() const
{
	return stats->max_VTOL_attack_runs;
}

bool Weapon::isEmptyVtolWeapon(unsigned player) const
{
	if (!isVtolWeapon()) return false;

	return ammoUsed >= getNumAttackRuns(player);
}

const WeaponStats& Weapon::getStats() const
{
	return *stats;
}

unsigned Weapon::getRecoil() const
{
	if (graphicsTime >= timeLastFired && graphicsTime < timeLastFired + DEFAULT_RECOIL_TIME)
	{
		const auto recoil_time = static_cast<int>(graphicsTime - timeLastFired);
		const auto recoil_amount = DEFAULT_RECOIL_TIME / 2 - abs(
			recoil_time - static_cast<int>(DEFAULT_RECOIL_TIME) / 2);
		const auto max_recoil = stats->recoil_value;
		return max_recoil * recoil_amount / (DEFAULT_RECOIL_TIME / 2 * 10);
	}
	return 0;
}

unsigned Weapon::getMaxRange(unsigned player) const
{
	return stats->upgraded_stats[player].max_range;
}

unsigned Weapon::getMinRange(unsigned player) const
{
	return stats->upgraded_stats[player].min_range;
}

unsigned Weapon::getShortRange(unsigned player) const
{
  return stats->upgraded_stats[player].short_range;
}

unsigned Weapon::getHitChance(unsigned unsigned player) const
{
  return stats->upgraded_stats[player].hit_chance;
}

unsigned Weapon::getShortRangeHitChance(unsigned unsigned player) const
{
  return stats->upgraded_stats[player].short_range_hit_chance;
}

WEAPON_SUBCLASS Weapon::getSubclass() const
{
	return stats->subclass;
}

unsigned Weapon::getNumAttackRuns(unsigned player) const
{
	const auto u_stats = stats->upgraded_stats[player];

	if (u_stats.reload_time > 0)
		return u_stats.rounds_per_volley * stats->max_VTOL_attack_runs;

	return stats->max_VTOL_attack_runs;
}

unsigned Weapon::getShotsFired() const noexcept
{
	return shotsFired;
}

const iIMDShape& Weapon::getImdShape() const
{
	return *stats->weapon_graphic;
}

const iIMDShape& Weapon::getMountGraphic() const
{
	return *stats->mount_graphic;
}

unsigned Weapon::calculateRateOfFire(unsigned player) const
{
  const auto& w_stats = stats->upgraded_stats[player];
  return w_stats.rounds_per_volley
          * 60 * GAME_TICKS_PER_SEC / w_stats.reload_time;
}

void Weapon::use_ammo()
{
  ++ammoUsed;
}