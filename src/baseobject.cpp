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

#include "lib/framework/vector.h"

#include "basedef.h"
#include "baseobject.h"
#include "droid.h"
#include "feature.h"
#include "projectile.h"
#include "structure.h"

Vector2i map_coord(Vector2i);
bool StatIsStructure(const BaseStats*);
bool StatIsFeature(const BaseStats*);


[[nodiscard]] OBJECT_TYPE getObjectType(BaseObject const* obj)
{
  if (dynamic_cast<Droid const*>(obj)) return OBJECT_TYPE::DROID;
  if (dynamic_cast<Structure const*>(obj)) return OBJECT_TYPE::STRUCTURE;
  if (dynamic_cast<Feature const*>(obj)) return OBJECT_TYPE::FEATURE;
  if (dynamic_cast<Projectile const*>(obj)) return OBJECT_TYPE::PROJECTILE;
}

static inline uint16_t interpolateAngle(uint16_t v1, uint16_t v2, unsigned t1, unsigned t2, unsigned t)
{
  return v1 + angleDelta(v2 - v1) * (t - t1) / (t2 - t1);
}

static Position interpolatePos(Position p1, Position p2, unsigned t1, unsigned t2, unsigned t)
{
  return p1 + (p2 - p1) * (t - t1) / (t2 - t1);
}

Rotation interpolateRot(Rotation v1, Rotation v2, unsigned t1, unsigned t2, unsigned t)
{
	// return v1 + (v2 - v1) * (t - t1) / (t2 - t1);
	return {interpolateAngle(v1.direction, v2.direction, t1, t2, t),
          interpolateAngle(v1.pitch, v2.pitch, t1, t2, t),
          interpolateAngle(v1.roll, v2.roll, t1, t2, t)
	};
}

static Spacetime interpolateSpacetime(Spacetime st1, Spacetime st2, unsigned t)
{
  ASSERT_OR_RETURN(st1, st1.time != st2.time, "Spacetime overlap!");
  return {t, interpolatePos(st1.position, st2.position, st1.time, st2.time, t),
          interpolateRot(st1.rotation, st2.rotation, st1.time, st2.time, t)};
}

Spacetime interpolateObjectSpacetime(BaseObject const* obj, unsigned t)
{
	if (auto psDroid = dynamic_cast<Droid const*>(obj)) {
    return interpolateSpacetime(psDroid->getPreviousLocation(),
                                obj->getSpacetime(), t);
  }
  if (auto psStruct = dynamic_cast<Structure const*>(obj)) {
    return interpolateSpacetime(psStruct->getPreviousLocation(),
                                obj->getSpacetime(), t);
  }
  return obj->getSpacetime();
}

Vector2i getStatsSize(BaseStats const* pType, uint16_t direction)
{
	if (StatIsStructure(pType)) {
		return dynamic_cast<StructureStats const*>(pType)->size(direction);
	}
	if (StatIsFeature(pType)) {
		return dynamic_cast<FeatureStats const*>(pType)->size();
	}
	return {1, 1};
}

StructureBounds getStructureBounds(BaseObject const* object)
{
	if (auto psStructure = dynamic_cast<Structure const*>(object)) {
		return getStructureBounds(psStructure);
	}
	if (auto psFeature = dynamic_cast<Feature const*>(object)) {
		return getStructureBounds(psFeature);
	}

	return {Vector2i(32767, 32767),
          Vector2i(-65535, -65535)}; // Default to an invalid area.
}

StructureBounds getStructureBounds(BaseStats const* stats, Vector2i pos, uint16_t direction)
{
	if (StatIsStructure(stats)) {
		return getStructureBounds(dynamic_cast<StructureStats const*>(stats), pos, direction);
	}
	if (StatIsFeature(stats)) {
		return getStructureBounds(dynamic_cast<FeatureStats const*>(stats), pos);
	}

  // Default to a 1×1 tile.
	return {map_coord(pos),
          Vector2i(1, 1)};
}
