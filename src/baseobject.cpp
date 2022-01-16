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

#include "baseobject.h"
#include "feature.h"
#include "intdisplay.h"
#include "map.h"

static inline uint16_t interpolateAngle(uint16_t v1, uint16_t v2, unsigned t1, unsigned t2, unsigned t)
{
	const auto numer = t - t1, denom = t2 - t1;
	return v1 + angleDelta(v2 - v1) * numer / denom;
}

static Position interpolatePos(Position p1, Position p2, unsigned t1, unsigned t2, unsigned t)
{
	const int numer = t - t1, denom = t2 - t1;
	return p1 + (p2 - p1) * numer / denom;
}

Rotation interpolateRot(Rotation v1, Rotation v2, unsigned t1, unsigned t2, unsigned t)
{
	// return v1 + (v2 - v1) * (t - t1) / (t2 - t1);
	return {interpolateAngle(v1.direction, v2.direction, t1, t2, t),
          interpolateAngle(v1.pitch, v2.pitch, t1, t2, t),
          interpolateAngle(v1.roll, v2.roll, t1, t2, t)
	};
}

static Spacetime interpolateSpacetime(Spacetime st1, Spacetime st2, std::size_t t)
{
	// Cyp says this should never happen, #3037 and #3238 say it does though.
	ASSERT_OR_RETURN(st1, st1.time != st2.time, "Spacetime overlap!");
	return {t, interpolatePos(st1.position, st2.position,
                            st1.time, st2.time, t),
	                 interpolateRot(st1.rotation, st2.rotation,
                                  st1.time, st2.time, t)};
}

Spacetime interpolateObjectSpacetime(const PersistentObject* obj, unsigned t)
{
	if (auto psDroid = dynamic_cast<const Droid*>(obj)) {
    return interpolateSpacetime(psDroid->getPreviousLocation(),
                                obj->getSpacetime(), t);
  }
  if (auto psStruct = dynamic_cast<const Structure*>(obj)) {
    return interpolateSpacetime(psStruct->getPreviousLocation(),
                                obj->getSpacetime(), t);
  }
  return obj->getSpacetime();
}

//void checkObject(const SimpleObject* psObject, const char* const location_description, const char* function,
//                 const int recurse)
//{
//	if (recurse < 0) {
//		return;
//	}
//
//	ASSERT(psObject != nullptr, "NULL pointer");
//
//	switch (psObject->type) {
//	case OBJ_DROID:
//		checkDroid((const Droid*)psObject, location_description, function, recurse - 1);
//		break;
//
//	case OBJ_STRUCTURE:
//		checkStructure((const Structure*)psObject, location_description, function, recurse - 1);
//		break;
//
//	case OBJ_PROJECTILE:
//		checkProjectile((const Projectile*)psObject, location_description, function, recurse - 1);
//		break;
//
//	case OBJ_FEATURE:
//		break;
//
//	default:
//		ASSERT_HELPER(!"invalid object type", location_description, function,
//		              "CHECK_OBJECT: Invalid object type (type num %u)", (unsigned int)psObject->type);
//		break;
//	}
//}
//
//void _syncDebugObject(const char* function, SimpleObject const* psObject, char ch)
//{
//	switch (psObject->type)
//	{
//	case OBJ_DROID: _syncDebugDroid(function, (const Droid*)psObject, ch);
//		break;
//	case OBJ_STRUCTURE: _syncDebugStructure(function, (const Structure*)psObject, ch);
//		break;
//	case OBJ_FEATURE: _syncDebugFeature(function, (const Feature*)psObject, ch);
//		break;
//	case OBJ_PROJECTILE: dynamic_cast<const Projectile*>(psObject)->debug_(function, ch);
//		break;
//	default: _syncDebug(function, "%c unidentified_object%d = p%d;objectType%d", ch, psObject->id, psObject->player,
//	                    psObject->type);
//		ASSERT_HELPER(!"invalid object type", "_syncDebugObject", function,
//		              "syncDebug: Invalid object type (type num %u)", (unsigned int)psObject->type);
//		break;
//	}
//}

Vector2i getStatsSize(BaseStats const* pType, uint16_t direction)
{
	if (StatIsStructure(pType)) {
		return static_cast<StructureStats const*>(pType)->size(direction);
	}
	else if (StatIsFeature(pType)) {
		return dynamic_cast<FeatureStats const*>(pType)->size();
	}
	return {1, 1};
}

StructureBounds getStructureBounds(PersistentObject const* object)
{
	auto const psStructure = dynamic_cast<const Structure*>(object);
	auto const psFeature = dynamic_cast<const Feature*>(object);

	if (psStructure != nullptr) {
		return getStructureBounds(psStructure);
	}
	else if (psFeature != nullptr) {
		return getStructureBounds(psFeature);
	}

	return {Vector2i(32767, 32767), Vector2i(-65535, -65535)}; // Default to an invalid area.
}

StructureBounds getStructureBounds(BaseStats const* stats, Vector2i pos, uint16_t direction)
{
	if (StatIsStructure(stats)) {
		return getStructureBounds(static_cast<StructureStats const*>(stats), pos, direction);
	}
	else if (StatIsFeature(stats)) {
		return getStructureBounds(dynamic_cast<FeatureStats const*>(stats), pos);
	}

	return {map_coord(pos), Vector2i(1, 1)}; // Default to a 1×1 tile.
}
