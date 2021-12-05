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
/** \file
 *  Definitions for features.
 */

#ifndef __INCLUDED_FEATUREDEF_H__
#define __INCLUDED_FEATUREDEF_H__

#include "basedef.h"
#include "statsdef.h"

enum class FEATURE_TYPE
{
  TANK,
  GEN_ARTE,
  OIL_RESOURCE,
  BOULDER,
  VEHICLE,
  BUILDING,
  LOS_OBJ,
  OIL_DRUM,
  TREE,
  SKYSCRAPER,
  COUNT
};

/* Stats for a feature */
class FeatureStats : public StatsObject {
public:
  inline Vector2i size() const { return {baseWidth, baseBreadth}; }

  FeatureStats(int idx = 0) : StatsObject(idx) {}
private:
  FEATURE_TYPE    subType = FEATURE_TYPE::COUNT;   ///< type of feature
  iIMDShape      *psImd = nullptr;        ///< Graphic for the feature
  UWORD           baseWidth = 0;          ///< The width of the base in tiles
  UWORD           baseBreadth = 0;        ///< The breadth of the base in tiles

  bool            tileDraw = false;       ///< Whether the tile needs to be drawn
  bool            allowLOS = false;       ///< Whether the feature allows the LOS. true = can see through the feature
  bool            visibleAtStart = false; ///< Whether the feature is visible at the start of the mission
  bool            damageable = false;     ///< Whether the feature can be destroyed
  UDWORD		body = 0;               ///< Number of body points
  UDWORD          armourValue = 0;        ///< Feature armour
};

class Feature : public GameObject {
public:
  Feature(uint32_t id, FeatureStats const *psStats);
  ~Feature();

  Vector2i size() const { return psStats->size(); }
  bool destroyFeature(unsigned impactTime);
private:
  FeatureStats const *psStats;
};

#endif // __INCLUDED_FEATUREDEF_H__