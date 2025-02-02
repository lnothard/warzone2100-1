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
 * @file
 * Functions for the base object type
 */

#ifndef __INCLUDED_BASEOBJECT_H__
#define __INCLUDED_BASEOBJECT_H__

#include <cstdlib>
#include <string>

struct BaseStats;
class PlayerOwnedObject;
struct Rotation;
struct Spacetime;
struct StructureBounds;


Rotation interpolateRot(Rotation v1, Rotation v2, unsigned t1, unsigned t2, unsigned t);
Spacetime interpolateObjectSpacetime(BaseObject const* obj, unsigned t);
Vector2i getStatsSize(BaseStats const* pType, uint16_t direction);
StructureBounds getStructureBounds(BaseObject const* object);
StructureBounds getStructureBounds(BaseStats const* stats, Vector2i pos, uint16_t direction);
[[nodiscard]] OBJECT_TYPE getObjectType(BaseObject const* obj);

#endif // __INCLUDED_BASEOBJECT_H__
