//
// Created by luna on 09/12/2021.
//

#ifndef WARZONE2100_FEATURE_H
#define WARZONE2100_FEATURE_H

#include "basedef.h"

enum class FEATURE_TYPE
{
	TANK,
	OIL_RESOURCE,
	BOULDER,
	VEHICLE,
	BUILDING,
	OIL_DRUM,
	TREE,
	SKYSCRAPER
};

/**
 * A `Feature` is the name for a persistent game entity
 * that is not owned by any one player. For example, trees,
 * vehicles, and resource deposits
 */
class Feature : public virtual ::SimpleObject, public Impl::SimpleObject
{
private:
	using enum FEATURE_TYPE;

	FEATURE_TYPE type;
	bool is_damageable = false;
	unsigned base_width = 0;
	unsigned base_breadth = 0;
	unsigned hit_points = 0;
	unsigned armour_points = 0;
};

[[nodiscard]] inline int calculate_height(const Feature& feature)
{
	auto& imd = feature.get_display_data().imd_shape;
	return imd->max.y + imd->min.y;
}

#endif // WARZONE2100_FEATURE_H
