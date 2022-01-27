/*
	This file is part of Warzone 2100.
	Copyright (C) 1999-2004  Eidos Interactive
	Copyright (C) 2005-2020  Warzone 2100 Project

	Warzone 2100 is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Warzone 2100 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Warzone 2100; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/

/**
 * @file feature.h
 * Definitions for the features
 */

#ifndef __INCLUDED_SRC_FEATURE_H__
#define __INCLUDED_SRC_FEATURE_H__

#include "basedef.h"
#include "stats.h"

struct iIMDShape;
struct StructureBounds;


enum class FEATURE_TYPE
{
    TANK,
    GEN_ARTE,
    OIL_RESOURCE,
    BOULDER,
    VEHICLE,
    BUILDING,
    UNUSED,
    LOS_OBJ,
    OIL_DRUM,
    TREE,
    SKYSCRAPER,
    COUNT
};

struct FeatureStats : public BaseStats
{
    explicit FeatureStats(int idx = 0);

    [[nodiscard]] Vector2i size() const;

    FEATURE_TYPE subType = FEATURE_TYPE::COUNT; ///< type of feature
    std::shared_ptr<iIMDShape> psImd = nullptr; ///< Graphic for the feature
    unsigned baseWidth = 0; ///< The width of the base in tiles
    unsigned baseBreadth = 0; ///< The breadth of the base in tiles
    bool tileDraw = false; ///< Whether the tile needs to be drawn
    bool allowLOS = false; ///< Whether the feature allows the LOS. true = can see through the feature
    bool visibleAtStart = false; ///< Whether the feature is visible at the start of the mission
    bool damageable = false; ///< Whether the feature can be destroyed
    unsigned body = 0; ///< Number of body points
    unsigned armourValue = 0; ///< Feature armour
};

class Feature : public BaseObject
{
public:
  ~Feature() override;
  Feature(unsigned id, FeatureStats* psStats);

  Feature(Feature const& rhs);
  Feature& operator=(Feature const& rhs);

  Feature(Feature&& rhs) noexcept = default;
  Feature& operator=(Feature&& rhs) noexcept = default;

  [[nodiscard]] Vector2i size() const;
  [[nodiscard]] int objRadius() const;
  [[nodiscard]] FeatureStats const* getStats() const;

  void update();
  static std::unique_ptr<Feature> buildFeature(FeatureStats* stats, unsigned x, unsigned y, bool fromSave);
private:
  struct Impl;
  std::unique_ptr<Impl> pimpl;
};

/* The statistics for the features */
extern std::unique_ptr<FeatureStats> asFeatureStats;
extern unsigned numFeatureStats;

//Value is stored for easy access to this feature in destroyDroid()/destroyStruct()
extern FeatureStats* oilResFeature;

/* Load the feature stats */
void loadFeatureStats(WzConfig& ini);

/* Release the feature stats memory */
void featureStatsShutDown();

/* Update routine for features */
void featureUpdate(Feature* psFeat);

// free up a feature with no visual effects
bool removeFeature(Feature* psDel);

/* Remove a Feature and free its memory */
bool destroyFeature(Feature* psDel, unsigned impactTime);

/* get a feature stat id from its name */
int getFeatureStatFromName(const WzString& name);

int featureDamage(Feature* psFeature, unsigned damage, WEAPON_CLASS weaponClass, WEAPON_SUBCLASS weaponSubClass,
                      unsigned impactTime, bool isDamagePerSecond, int minDamage);

StructureBounds getStructureBounds(Feature const* object);
StructureBounds getStructureBounds(FeatureStats const* stats, Vector2i pos);

//#define syncDebugFeature(psFeature, ch) _syncDebugFeature(__FUNCTION__, psFeature, ch)
//void _syncDebugFeature(const char* function, Feature const* psFeature, char ch);

#endif // __INCLUDED_SRC_FEATURE_H__
