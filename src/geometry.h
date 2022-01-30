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

#ifndef __INCLUDED_SRC_GEOMETRY_H__
#define __INCLUDED_SRC_GEOMETRY_H__

#include "map.h"


struct QUAD
{
	Vector2i coords[4] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
};

uint16_t calcDirection(int x0, int y0, int x1, int y1);
bool inQuad(Vector2i const* pt, QUAD const* quad);
Vector2i positionInQuad(Vector2i const& pt, QUAD const& quad);
Droid* getNearestDroid(unsigned x, unsigned y, bool bSelected);
bool objectOnScreen(BaseObject const* object, int tolerance);


static inline Structure* getTileStructure(int x, int y)
{
  auto psObj = mapTile(x, y)->psObject;
	if (psObj && dynamic_cast<Structure*>(psObj)) {
		return dynamic_cast<Structure*>(psObj);
	}
	return nullptr;
}

static inline Feature* getTileFeature(int x, int y)
{
  auto psObj = mapTile(x, y)->psObject;
	if (psObj && dynamic_cast<Feature*>(psObj)) {
		return dynamic_cast<Feature*>(psObj);
	}
	return nullptr;
}

/// WARNING: Returns NULL if tile not visible to selectedPlayer.
/// Must *NOT* be used for anything game-state/simulation-calculation related
static inline BaseObject* getTileOccupier(int x, int y)
{
	auto psTile = mapTile(x, y);
	if (TEST_TILE_VISIBLE_TO_SELECTEDPLAYER(psTile)) {
		return mapTile(x, y)->psObject;
	}
	else {
    return nullptr;
	}
}

#endif // __INCLUDED_SRC_GEOMETRY_H__
