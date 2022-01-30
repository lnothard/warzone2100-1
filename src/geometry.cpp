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
 * @file geometry.cpp
 * Holds trig/vector deliverance specific stuff for 3D
 *
 * Alex McLean, Pumpkin Studios, EIDOS Interactive
 */

#include "lib/framework/frame.h"
#include "lib/ivis_opengl/pieclip.h"
#include "lib/ivis_opengl/piematrix.h"

#include "display.h"
#include "displaydef.h"
#include "geometry.h"
#include "objectdef.h"

uint16_t calcDirection(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
	return iAtan2(x1 - x0, y1 - y0);
}

/* Returns non-zero if a point is in a 4 sided polygon */
/* See header file for definition of QUAD */
bool inQuad(const Vector2i* pt, const QUAD* quad)
{
	// Early out.
	int minX = std::min(std::min(quad->coords[0].x, quad->coords[1].x), std::min(quad->coords[2].x, quad->coords[3].x));
	if (pt->x < minX)
	{
		return false;
	}
	int maxX = std::max(std::max(quad->coords[0].x, quad->coords[1].x), std::max(quad->coords[2].x, quad->coords[3].x));
	if (pt->x > maxX)
	{
		return false;
	}
	int minY = std::min(std::min(quad->coords[0].y, quad->coords[1].y), std::min(quad->coords[2].y, quad->coords[3].y));
	if (pt->y < minY)
	{
		return false;
	}
	int maxY = std::max(std::max(quad->coords[0].y, quad->coords[1].y), std::max(quad->coords[2].y, quad->coords[3].y));
	if (pt->y > maxY)
	{
		return false;
	}

	bool c = false;

	for (int i = 0, j = 3; i < 4; j = i++)
	{
		Vector2i edge = quad->coords[j] - quad->coords[i];
		Vector2i pos = *pt - quad->coords[i];
		if ((0 <= pos.y && pos.y < edge.y && (int64_t)pos.x * (int64_t)edge.y < (int64_t)pos.y * (int64_t)edge.x) ||
			(edge.y <= pos.y && pos.y < 0 && (int64_t)pos.x * (int64_t)edge.y > (int64_t)pos.y * (int64_t)edge.x))
		{
			c = !c;
		}
	}

	return c;
}

Vector2i positionInQuad(Vector2i const& pt, QUAD const& quad)
{
	long lenSq[4];
	long ptDot[4];
	for (int i = 0, j = 3; i < 4; j = i++)
	{
		Vector2i edge = quad.coords[j] - quad.coords[i];
		Vector2i pos = quad.coords[j] - pt;
		Vector2i posRot(pos.y, -pos.x);
		lenSq[i] = dot(edge, edge);
		ptDot[i] = dot(posRot, edge);
	}
	int ret[2];
	for (int i = 0; i < 2; ++i)
	{
		long d1 = ptDot[i] * lenSq[i + 2];
		long d2 = ptDot[i + 2] * lenSq[i];
		ret[i] = d1 + d2 != 0 ? (int64_t)TILE_UNITS * d1 / (d1 + d2) : TILE_UNITS / 2;
	}
	return {ret[0], ret[1]};
}

bool objectOnScreen(BaseObject const* object, int tolerance)
{
  if (!DrawnInLastFrame(object->getDisplayData()->frame_number)) {
    return false;
  }
  const auto dX = object->getDisplayData()->screen_x;
  const auto dY = object->getDisplayData()->screen_y;
  /* Is it on screen */
  if (dX > 0 - tolerance && dY > 0 - tolerance &&
      dX < pie_GetVideoBufferWidth() + tolerance &&
      dY < pie_GetVideoBufferHeight() + tolerance) {
    return true;
  }
  return false;
}
