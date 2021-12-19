//
// Created by luna on 10/12/2021.
//

#include "map.h"
#include "structure.h"

/**
 * Intersect a tile with a line and report the points of intersection
 * line is gives as point plus 2d directional vector
 * returned are two coordinates at the edge
 * true if the intersection also crosses the tile split line
 * (which has to be taken into account)
 **/
bool map_Intersect(int *Cx, int *Cy, int *Vx, int *Vy, int *Sx, int *Sy)
{
  int	 x, y, ox, oy, Dx, Dy, tileX, tileY;
  int	 ily, iry, itx, ibx;

  // dereference pointers
  x = *Cx;
  y = *Cy;
  Dx = *Vx;
  Dy = *Vy;

  /* Turn into tile coordinates */
  tileX = map_coord(x);
  tileY = map_coord(y);

  /* Inter tile comp */
  ox = map_round(x);
  oy = map_round(y);

  /* allow backwards tracing */
  if (ox == 0 && Dx < 0)
  {
    tileX--;
    ox = TILE_UNITS;
  }
  if (oy == 0 && Dy < 0)
  {
    tileY--;
    oy = TILE_UNITS;
  }

  *Cx = -4 * TILE_UNITS; // to trigger assertion
  *Cy = -4 * TILE_UNITS;
  *Vx = -4 * TILE_UNITS;
  *Vy = -4 * TILE_UNITS;

  // calculate intersection point on the left and right (if any)
  ily = y - 4 * TILE_UNITS; // make sure initial value is way outside of tile
  iry = y - 4 * TILE_UNITS;
  if (Dx != 0)
  {
    ily = y - ox * Dy / Dx;
    iry = y + (TILE_UNITS - ox) * Dy / Dx;
  }
  // calculate intersection point on top and bottom (if any)
  itx = x - 4 * TILE_UNITS; // make sure initial value is way outside of tile
  ibx = x - 4 * TILE_UNITS;
  if (Dy != 0)
  {
    itx = x - oy * Dx / Dy;
    ibx = x + (TILE_UNITS - oy) * Dx / Dy;
  }

  // line comes from the left?
  if (Dx >= 0)
  {
    if (map_coord(ily) == tileY || map_coord(ily - 1) == tileY)
    {
      *Cx = world_coord(tileX);
      *Cy = ily;
    }
    if (map_coord(iry) == tileY || map_coord(iry - 1) == tileY)
    {
      *Vx = world_coord(tileX + 1);
      *Vy = iry;
    }
  }
  else
  {
    if (map_coord(ily) == tileY || map_coord(ily - 1) == tileY)
    {
      *Vx = world_coord(tileX);
      *Vy = ily;
    }
    if (map_coord(iry) == tileY || map_coord(iry - 1) == tileY)
    {
      *Cx = world_coord(tileX + 1);
      *Cy = iry;
    }
  }
  // line comes from the top?
  if (Dy >= 0)
  {
    if (map_coord(itx) == tileX || map_coord(itx - 1) == tileX)
    {
      *Cx = itx;
      *Cy = world_coord(tileY);
    }
    if (map_coord(ibx) == tileX || map_coord(ibx - 1) == tileX)
    {
      *Vx = ibx;
      *Vy = world_coord(tileY + 1);
    }
  }
  else
  {
    if (map_coord(itx) == tileX || map_coord(itx - 1) == tileX)
    {
      *Vx = itx;
      *Vy = world_coord(tileY);
    }
    if (map_coord(ibx) == tileX || map_coord(ibx - 1) == tileX)
    {
      *Cx = ibx;
      *Cy = world_coord(tileY + 1);
    }
  }
  // assertions, no intersections outside of tile
  ASSERT(*Cx >= world_coord(tileX) && *Cx <= world_coord(tileX + 1), "map_Intersect(): tile Bounds %i %i, %i %i -> %i,%i,%i,%i", x, y, Dx, Dy, *Cx, *Cy, *Vx, *Vy);
  ASSERT(*Cy >= world_coord(tileY) && *Cy <= world_coord(tileY + 1), "map_Intersect(): tile Bounds %i %i, %i %i -> %i,%i,%i,%i", x, y, Dx, Dy, *Cx, *Cy, *Vx, *Vy);
  ASSERT(*Vx >= world_coord(tileX) && *Vx <= world_coord(tileX + 1), "map_Intersect(): tile Bounds %i %i, %i %i -> %i,%i,%i,%i", x, y, Dx, Dy, *Cx, *Cy, *Vx, *Vy);
  ASSERT(*Vy >= world_coord(tileY) && *Vy <= world_coord(tileY + 1), "map_Intersect(): tile Bounds %i %i, %i %i -> %i,%i,%i,%i", x, y, Dx, Dy, *Cx, *Cy, *Vx, *Vy);
  ASSERT(tileX >= 0 && tileY >= 0 && tileX < map_width && tileY < map_height, "map_Intersect(): map Bounds %i %i, %i %i -> %i,%i,%i,%i", x, y, Dx, Dy, *Cx, *Cy, *Vx, *Vy);

  //calculate midway line intersection points
  if (((map_coord(itx) == tileX) == (map_coord(ily) == tileY)) && ((map_coord(ibx) == tileX) == (map_coord(iry) == tileY)))
  {
    // line crosses diagonal only
    if (Dx - Dy == 0)
    {
      return false;
    }
    *Sx = world_coord(tileX) + (Dx * oy - Dy * ox) / (Dx - Dy);
    *Sy = world_coord(tileY) + (Dx * oy - Dy * ox) / (Dx - Dy);
    if (map_coord(*Sx) != tileX || map_coord(*Sy) != tileY)
    {
      return false;
    }
    return true;
  }
  else if (((map_coord(ibx) == tileX) == (map_coord(ily) == tileY)) && ((map_coord(itx) == tileX) == (map_coord(iry) == tileY)))
  {
    //line crosses anti-diagonal only
    if (Dx + Dy == 0)
    {
      return false;
    }
    *Sx = world_coord(tileX) + (Dx * (TILE_UNITS - oy) + Dy * ox) / (Dx + Dy);
    *Sy = world_coord(tileY) + (Dy * (TILE_UNITS - ox) + Dx * oy) / (Dx + Dy);
    if (map_coord(*Sx) != tileX || map_coord(*Sy) != tileY)
    {
      return false;
    }
    return true;
  }
  else
  {
    //line crosses both tile diagonals
    //TODO: trunk divides tiles into 4 parts instead of 2 in 2.3.
    //We would need to check and return both intersections here now,
    //but that would require an additional return parameter!
    //Instead we check only one of them and know it might be wrong!
    if (Dx + Dy != 0)
    {
      // check anti-diagonal
      *Sx = world_coord(tileX) + (Dx * (TILE_UNITS - oy) + Dy * ox) / (Dx + Dy);
      *Sy = world_coord(tileY) + (Dy * (TILE_UNITS - ox) + Dx * oy) / (Dx + Dy);
      if (map_coord(*Sx) == tileX && map_coord(*Sy) == tileY)
      {
        return true;
      }
    }
    if (Dx - Dy != 0)
    {
      // check diagonal
      *Sx = world_coord(tileX) + (Dx * oy - Dy * ox) / (Dx - Dy);
      *Sy = world_coord(tileY) + (Dx * oy - Dy * ox) / (Dx - Dy);
      if (map_coord(*Sx) == tileX && map_coord(*Sy) == tileY)
      {
        return true;
      }
    }
  }

  return false;
}
